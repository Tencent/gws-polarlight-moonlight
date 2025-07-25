#pragma once

#include "identitymanager.h"
#include "nvapp.h"
#include "nvaddress.h"

#include <Limelight.h>

#include <QUrl>
#include <QNetworkAccessManager>
#include <QNetworkReply>

class NvComputer;

class NvDisplayMode
{
public:
    bool operator==(const NvDisplayMode& other) const
    {
        return width == other.width &&
               height == other.height &&
               refreshRate == other.refreshRate;
    }

    int width;
    int height;
    int refreshRate;
};
Q_DECLARE_TYPEINFO(NvDisplayMode, Q_PRIMITIVE_TYPE);

class GfeHttpResponseException : public std::exception
{
public:
    GfeHttpResponseException(int statusCode, QString message) :
        m_StatusCode(statusCode),
        m_StatusMessage(message)
    {

    }

    const char* what() const throw()
    {
        return m_StatusMessage.toLatin1();
    }

    const char* getStatusMessage() const
    {
        return m_StatusMessage.toLatin1();
    }

    int getStatusCode() const
    {
        return m_StatusCode;
    }

    QString toQString() const
    {
        return m_StatusMessage + " (Error " + QString::number(m_StatusCode) + ")";
    }

private:
    int m_StatusCode;
    QString m_StatusMessage;
};

class QtNetworkReplyException : public std::exception
{
public:
    QtNetworkReplyException(QNetworkReply::NetworkError error, QString errorText) :
        m_Error(error),
        m_ErrorText(errorText)
    {

    }

    const char* what() const throw()
    {
        return m_ErrorText.toLatin1();
    }

    const char* getErrorText() const
    {
        return m_ErrorText.toLatin1();
    }

    QNetworkReply::NetworkError getError() const
    {
        return m_Error;
    }

    QString toQString() const
    {
        return m_ErrorText + " (Error " + QString::number(m_Error) + ")";
    }

private:
    QNetworkReply::NetworkError m_Error;
    QString m_ErrorText;
};

class NvHTTP : public QObject
{
    Q_OBJECT

public:
    enum NvLogLevel {
        NVLL_NONE,
        NVLL_ERROR,
        NVLL_VERBOSE
    };

    explicit NvHTTP(NvAddress address, uint16_t httpsPort, QSslCertificate serverCert);

    explicit NvHTTP(NvComputer* computer);

    static
    int
    getCurrentGame(QString serverInfo);

    QString
    getServerInfo(NvLogLevel logLevel, bool fastFail = false);

    static
    void
    verifyResponseStatus(QString xml);

    static
    QString
    getXmlString(QString xml,
                 QString tagName);

    static
    QByteArray
    getXmlStringFromHex(QString xml,
                        QString tagName);

    QString
    openConnectionToString(QUrl baseUrl,
                           QString command,
                           QString arguments,
                           int timeoutMs,
                           NvLogLevel logLevel = NvLogLevel::NVLL_VERBOSE);

    void setServerCert(QSslCertificate serverCert);

    void setAddress(NvAddress address);
    void setHttpsPort(uint16_t port);

    NvAddress address();

    QSslCertificate serverCert();

    uint16_t httpPort();

    uint16_t httpsPort();

    static
    QVector<int>
    parseQuad(QString quad);

    void
    quitApp();

    void
    startApp(QString verb,
             bool isGfe,
             int appId,
             PSTREAM_CONFIGURATION streamConfig,
             bool sops,
             bool localAudio,
             int gamepadMask,
             bool persistGameControllersOnDisconnect,
             QString& rtspSessionUrl,QString & rtspCSessionID,std::string &UUid,QString &decrypt_text);
    int CheckDisplay();
    QString SendMsg(QString &msg);
    QVector<NvApp>
    getAppList();

    QImage
    getBoxArt(int appId);

    static
    QVector<NvDisplayMode>
    getDisplayModeList(QString serverInfo);

    QUrl m_BaseUrlHttp;
    QUrl m_BaseUrlHttps;

    QString GwsHttpsPost(QString baseUrl, QString token, QJsonDocument body, int &nRet);

    QString GwsHttpsClientResolution(QString baseUrl, QString token, QJsonDocument body, int& nRet);
private:
    void
    handleSslErrors(QNetworkReply* reply, const QList<QSslError>& errors);
    QNetworkReply *waitForReply(QNetworkRequest& request, QUrl& url, int timeoutMs, NvLogLevel logLevel);
    QNetworkReply*
    openConnection(QUrl baseUrl,
                   QString command,
                   QString arguments,
                   int timeoutMs,
                   NvLogLevel logLevel);

    QNetworkReply* openConnectionByToken(QString baseUrl,
                                         QString token,
                                         QJsonDocument body,
                                         int timeoutMs,
                                         NvLogLevel logLevel,int &nRet);

    QString ConnectionToStringByToken(QString baseUrl,
                                      QString token,
                                      QJsonDocument body,
                                      int timeoutMs,
                                      int &nRet,
                                      NvLogLevel logLevel = NvLogLevel::NVLL_VERBOSE);


    NvAddress m_Address;
    QNetworkAccessManager m_Nam;
    QSslCertificate m_ServerCert;
};


class NvReqHTTPS : public QObject
{
    Q_OBJECT
public:
    explicit NvReqHTTPS();
    std::string RequestByToken(QString &baseUrl,QString token,QJsonDocument body,int timeoutMs,int &nRet);

private:
    QNetworkAccessManager m_Nam;
};
