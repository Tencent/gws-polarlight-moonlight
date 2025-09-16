#ifndef EVENTREPORTER_H
#define EVENTREPORTER_H
#include "QtCore/qeventloop.h"
#include "QtCore/qjsonarray.h"
#include "QtCore/qjsonobject.h"
#include <string>
#include <chrono>

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonDocument>

class ConnectionReporter: public QObject {
    Q_OBJECT
public:
    enum ConnectErrorCode {
        SUCCESS = 0,
        VERSION_MISMATCH,
        TCP_PORT_CONNECT_FAILD,
        TCP_PORT_EMPTY_MAPPING,
        UDP_PORT_EMPTY_MAPPING,
        SDL_INIT_SUBSYSTEM_FAILED,
        ENCYPT_CHECK_FAILED,
        INIT_SESSION_UI_FAILED,
        CONNECT_GET_SERVERINFO_FAILED,
        START_APP_FAILED,
        START_CONNECTION_FAILED,

        COMPLETE_FRAME_NOT_RECEIVED = 1000, // UNKNOWN_ERROR
    };


    ConnectionReporter(QObject *parent = nullptr) : QObject(parent), m_currentState(State::INIT) {
        m_manager = new QNetworkAccessManager(this);
    }

    ~ConnectionReporter() {
        switch (m_currentState) {
        case State::INIT: {
            // visit portal failed or not start in portal
            break;
        }
        case State::UNKNOWN: {
            //
            m_content.errorCode = ConnectErrorCode::COMPLETE_FRAME_NOT_RECEIVED;
            m_content.reason = "A complete frame was not received";
            SetEndTimestamp();
            SendContent();
            break;
        }
        case State::SUCCESS: {
            // send when set success
            break;
        }
        case State::FAILED: {
            SetEndTimestamp();
            SendContent();
            break;
        }
        }
    }


    void SetStateSuccess() {
        if (m_currentState == State::SUCCESS || m_currentState == State::INIT) {
            return; // 已经设置成功状态
        }
        m_currentState = State::SUCCESS;
        m_content.errorCode = ConnectErrorCode::SUCCESS;
        SetEndTimestamp();
        // 同步发送
        SendContent();
    }

    void SetStateFailed(ConnectErrorCode code, QString& reason) {
        // if (m_currentState == State::UNKNOWN) {
            m_currentState = State::FAILED;
            m_content.errorCode = code;
            m_content.reason = reason;
            SetEndTimestamp();
        // }
    }

    void Init(QString &host, QString InstanceId, QString &token) {
        m_url = "https://"+ host +"/gws/v1/client/" + InstanceId + "/send-connection-status-info";
        m_token = token;
        m_content.instanceId = InstanceId;
        m_currentState = State::UNKNOWN;
        SetStartTimestamp();
    }

private slots:
    void handleReply() {
        if (m_reply->error() == QNetworkReply::NoError) {
            qInfo() << "send stream report success!";
        } else {
            qWarning() << "send stream report network error: " + m_reply->errorString();
        }
        m_reply->deleteLater();
    }

private:
    void SetStartTimestamp() {
        using namespace std::chrono;

        m_content.startTimestamp = duration_cast<milliseconds>(
            system_clock::now().time_since_epoch()).count();
    }

    void SetEndTimestamp() {
        using namespace std::chrono;
        m_content.endTimestamp = duration_cast<milliseconds>(
            system_clock::now().time_since_epoch()).count();
        m_content.duration = m_content.endTimestamp - m_content.startTimestamp;
    }

    void SendContent() {
        QNetworkRequest request(m_url);
        std::string Bearer = "Bearer ";
        Bearer += m_token.toStdString();
        request.setRawHeader("Authorization", Bearer.c_str());
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

        QJsonObject json;
        json["instanceId"] = m_content.instanceId;
        json["errorCode"] = m_content.errorCode;
        json["reason"] = m_content.reason;
        json["duration"] = m_content.duration;
        json["startTimestamp"] = m_content.startTimestamp;
        json["endTimestamp"] = m_content.endTimestamp;

        QJsonDocument doc(json);
        m_reply = m_manager->post(request, doc.toJson());

        QEventLoop eventLoop(this);
        connect(m_reply, &QNetworkReply::finished, this, &ConnectionReporter::handleReply);
        connect(m_reply, &QNetworkReply::finished, &eventLoop, &QEventLoop::quit);
        eventLoop.exec();
    }

    // 状态枚举
    enum class State {
        INIT,       // 需要初始化的状态
        UNKNOWN,    // 初始化完成，未知成功/失败状态
        SUCCESS,    // 成功状态（第一帧画面到达时设置）
        FAILED      // 失败状态（可由外部设置）
    };

    State m_currentState;

    struct {
        QString instanceId;
        int errorCode;          // 0 为成功，其他为失败
        QString reason;         // 失败的原因（成功为空）
        long long duration;           // 持续的时间ms
        long long startTimestamp;
        long long endTimestamp;
    } m_content;

    QString m_url;
    QString m_token;
    QNetworkAccessManager *m_manager = nullptr;
    QNetworkReply *m_reply = nullptr;
};


class StreamInfoReporter: public QObject {
    Q_OBJECT
public:
    typedef struct {
        QString name;
        int width;
        int height;
        int streamWidth;
        int streamHeight;
        int fps;
        int bitrate;
        int rotation;
    } DisplayConfig;


    StreamInfoReporter(QObject *parent = nullptr) : QObject(parent) {
    }

    ~StreamInfoReporter() {
    }


    void Init(QString &host, QString InstanceId, QString &token) {
        m_url = "https://"+ host +"/gws/v1/client/" + InstanceId + "/send-stream-info";
        m_token = token;
        m_instanceId = InstanceId;
        m_manager = new QNetworkAccessManager(this);
        m_init = true;
    }

    void SendConfig(QList<DisplayConfig> &displayConfig) {
        if (!m_init) {
            // only start in portal init reporter
            return;
        }
        QNetworkRequest request(m_url);
        std::string Bearer = "Bearer ";
        Bearer += m_token.toStdString();
        request.setRawHeader("Authorization", Bearer.c_str());
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

        QJsonObject json;
        json["instanceId"] = m_instanceId;

        QJsonArray configArray;

        for (auto &cfg : displayConfig) {
            QJsonObject tmp;
            tmp["name"] = cfg.name;
            tmp["width"] = cfg.width;
            tmp["height"] = cfg.height;
            tmp["streamWidth"] = cfg.streamWidth;
            tmp["streamHeight"] = cfg.streamHeight;
            tmp["fps"] = cfg.fps;
            tmp["bitrate"] = cfg.bitrate;
            tmp["rotation"] = cfg.rotation;
            configArray.append(tmp);
        }

        json["streamConfig"] = configArray;

        QJsonDocument doc(json);
        m_reply = m_manager->post(request, doc.toJson());

        // QEventLoop eventLoop(this);
        connect(m_reply, &QNetworkReply::finished, this, &StreamInfoReporter::handleReply);
        // connect(m_reply, &QNetworkReply::finished, &eventLoop, &QEventLoop::quit);
        // eventLoop.exec();
    }

private slots:
    void handleReply() {
        if (m_reply->error() == QNetworkReply::NoError) {
            qInfo() << "send config report success!";
        } else {
            qWarning() << "send config report network error: " + m_reply->errorString();
        }
        m_reply->deleteLater();
    }

private:

    QString m_instanceId;
    QString m_url;
    QString m_token;
    QNetworkAccessManager *m_manager = nullptr;
    QNetworkReply *m_reply = nullptr;
    bool m_init = false;
};


class IdleTimeoutReporter: public QObject {
    Q_OBJECT
public:
    typedef struct {
        QString startTime;
        QString currentTime;
    } TimoutInfo;


    IdleTimeoutReporter(QObject *parent = nullptr) : QObject(parent) {
    }

    ~IdleTimeoutReporter() {
    }


    void Init(QString &host, QString InstanceId, QString &token) {
        m_url = "https://"+ host +"/gws/v1/client/" + InstanceId + "/send-client-idle-timeout-info";
        m_token = token;
        m_instanceId = InstanceId;
        m_manager = new QNetworkAccessManager(this);
        m_init = true;
    }

    void SendTimeoutInfo(TimoutInfo &timeoutInfo) {
        if (!m_init) {
            // only start in portal init reporter
            return;
        }
        QNetworkRequest request(m_url);
        std::string Bearer = "Bearer ";
        Bearer += m_token.toStdString();
        request.setRawHeader("Authorization", Bearer.c_str());
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

        QJsonObject json;
        json["instanceId"] = m_instanceId;

        QJsonObject tmp;
        tmp["startTime"] = timeoutInfo.startTime;
        tmp["currentTime"] = timeoutInfo.currentTime;
        json["timeoutInfo"] = tmp;

        QJsonDocument doc(json);
        m_reply = m_manager->post(request, doc.toJson());

        // QEventLoop eventLoop(this);
        connect(m_reply, &QNetworkReply::finished, this, &IdleTimeoutReporter::handleReply);
        // connect(m_reply, &QNetworkReply::finished, &eventLoop, &QEventLoop::quit);
        // eventLoop.exec();
    }

private slots:
    void handleReply() {
        if (m_reply->error() == QNetworkReply::NoError) {
            qInfo() << "send idle timeout report success!";
        } else {
            qWarning() << "send idle timeout report network error: " + m_reply->errorString();
        }
        m_reply->deleteLater();
    }

private:

    QString m_instanceId;
    QString m_url;
    QString m_token;
    QNetworkAccessManager *m_manager = nullptr;
    QNetworkReply *m_reply = nullptr;
    bool m_init = false;
};

struct TimeoutAction {
    qint64 timeoutSec;
    QString action;
};


extern ConnectionReporter *gConnectionReporter;
extern StreamInfoReporter gStreamInfoReporter;
extern IdleTimeoutReporter gIdleTimeoutReporter;

extern TimeoutAction gTimeoutAction;
#endif // EVENTREPORTER_H
