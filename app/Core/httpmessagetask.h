#pragma once
#include <QObject>
#include <QRunnable>
#include <QByteArray>
#include <QDebug>
#include <QThreadPool>
#include "streaming/session.h"

class HttpMessageTask : public QObject, public QRunnable {
    Q_OBJECT

public:
    HttpMessageTask(Session* session, const QString& msg,int nReq)
        : m_session(session), m_msg(msg), m_Req(nReq) {}
    void onRet(const QObject* receiver, const char* method) {
        connect(this, SIGNAL(sendMsgRet(int, int,QByteArray)), receiver, method);
    }
    void Run()
    {
        QByteArray response;
        bool byteFlag = false;
        m_NvComputer = m_session->getNvComputer();
        try {
            if (m_NvComputer == nullptr) {
                qWarning() << "computer of session is empty";
                emit sendMsgRet(byteFlag,m_Req, response);
                return;
            }

            NvHTTP http(m_NvComputer.get());
            qDebug()<<"http.SendMsg()";
            response = http.SendMsg(m_msg, byteFlag);
        }
        catch (const GfeHttpResponseException& e) {
            qWarning() << "SendMsg error:" << e.toQString();
        }
        catch (const QtNetworkReplyException& e)
        {
            qWarning() << "QtNetworkReplyException caught: " << e.toQString();
        }
        catch (const std::exception& e)
        {
            qWarning() << "std::Exception caught: " << e.what();
        }

        qDebug() << "HttpMessageTask::run: byteFlag =" << byteFlag;
        qDebug() << "HttpMessageTask::run: m_Req =" << m_Req;
        emit sendMsgRet(byteFlag,m_Req, response);
    }
signals:
    void sendMsgRet(int flag,int nReq, const QByteArray& text);

protected:
        void run() override {
            Run();
        }
private:
    Session* m_session;
    QString m_msg;
    int m_Req;
    std::shared_ptr<NvComputer> m_NvComputer;
};
