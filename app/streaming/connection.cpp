#include <Limelight.h>
#include <SDL.h>

#include <QApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QUrlQuery>
#include <QXmlStreamReader>
#include "streaming/encryption.h"
#include "streaming/share_data.h"
#include "connection.h"
#include "../Core/sessionui.h"
#ifdef Q_OS_WIN32
#  include <windows.h>
#endif
#ifdef HAVE_SLVIDEO
#  include "video/slvid.h"
#endif

EventManager g_Manager;
#define SDL_CODE_FLUSH_WINDOW_EVENT_BARRIER 100
#define SDL_CODE_GAMECONTROLLER_RUMBLE 101

CONNECTION_LISTENER_CALLBACKS g_ConnCallbacks = {
    AsyncConnection::clStageStarting,
    nullptr,
    AsyncConnection::clStageFailed,
    nullptr,
    AsyncConnection::clConnectionTerminated,
    AsyncConnection::clLogMessage,
    AsyncConnection::clRumble,
    AsyncConnection::clConnectionStatusUpdate,
    AsyncConnection::clSetHdrMode,
};

void parseXmlArray(const QString &xmlArray) {
    QJsonParseError error;
    QJsonDocument jsonDoc = QJsonDocument::fromJson(xmlArray.toUtf8(), &error);

    if (error.error == QJsonParseError::NoError) {
        if (jsonDoc.isArray()) {
            QJsonArray jsonArray = jsonDoc.array();
            for (const QJsonValue &value : jsonArray) {
                if (value.isObject()) {
                    QJsonObject jsonObject = value.toObject();
                    QString name = jsonObject["name"].toString();
                    int x = jsonObject["X"].toInt();

                    qDebug() << "Name:" << name << "X:" << x;
                }
            }
        }
    } else
    {
        qWarning() << "parseXmlArray Parse error:" << error.errorString();
    }
}

void AsyncConnection::StopStream(int num)
{
    if(2 == num)
    {
        LiStopStream2();
        SDL_Event event;
        event.type = SDL_CLOSE_WINDOW2_EVENT;
        event.quit.timestamp = SDL_GetTicks();
        SDL_PushEvent(&event);
    }
}
void AsyncConnection::StopConnect(int nStreamNum)
{
    qDebug()<<"AsyncConnection::StopConnect() Begin";
    NvHTTP http(m_Computer);
    try
    {
        http.quitApp();
    }
    catch (const GfeHttpResponseException& e)
    {
        qWarning()<<"http.quitApp() GfeHttpResponseException"<<e.toQString();
    }
    catch (const QtNetworkReplyException& e)
    {
        qWarning()<<"http.quitApp() QtNetworkReplyException"<<e.toQString();
    }
    LiStopConnection(nStreamNum);
    SendSDLEvent();
    qDebug()<<"AsyncConnection::StopConnect() End";
}
void AsyncConnection::InitConnect()
{
    m_VideoCallbacks = m_VideoCallbacksTemp;
    m_AudioCallbacks = m_AudioCallbacksTemp;
}


void AsyncConnection::RequestComputerInfor(QString Csessionid)
{
    QString szUrl;
    QString strtoken;
    QJsonDocument retJson;
    std::string body = parse(m_HttpJsonStr.c_str(), Csessionid, szUrl, strtoken, retJson,m_ClientOS,m_ClientArch);

    if (m_HttpJsonStr.size() > 0 && Csessionid.size() > 0 && szUrl.size() > 0 && strtoken.size() > 0)
    {
        NvPairingManager pairingManager(m_Computer);
        int nRet = pairingManager.sendSelfComputerInforToGws(szUrl, strtoken, retJson);
        qDebug()<<"Session::SetComputerInfor nRet =  "<<nRet;
    }
}

void AsyncConnection::ChangeStatus(ConnectStatus status)
{
    QMutexLocker locker(&m_mutex);
    if(m_Status != emStop)
    {
        m_Status = status;
    }
    m_CondStatusChanged.wakeOne();
}


void AsyncConnection::clStageStarting(int stage) {
    // We know this is called on the same thread as LiStartConnection()
    // which happens to be the main thread, so it's cool to interact
    // with the GUI in these callbacks.
    SDL_Event event;
    event.type = SDL_TIPS_EVENT;
    event.user.data1 = (void *)(uintptr_t)TipType::emNormal;
    event.user.code = stage;
    SDL_PushEvent(&event);
}

void AsyncConnection::clStageFailed(int stage, int errorCode) {
    // Perform the port test now, while we're on the async connection thread and not blocking the UI.
    qWarning()<<"AsyncConnection::clStageFailed stage = "<< stage<<"errorCode = "<< errorCode;
    SDL_Event event;
    event.type = SDL_TIPS_EVENT;
    event.user.data1 = (void *)(uintptr_t)TipType::emErrorNoraml;
    event.user.code = stage;
    SDL_PushEvent(&event);

}

void AsyncConnection::clConnectionTerminated(int  errcode) {

    // Push a  event to the main loop
    qDebug()<<"AsyncConnection:: clConnectionTerminated";
    SDL_Event event;
    event.type = SDL_DISCONNECTION_EVENT;   //tip
    event.quit.timestamp = SDL_GetTicks();
    SDL_PushEvent(&event);
}

void AsyncConnection::clLogMessage(const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    SDL_LogMessageV(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_INFO, format, ap);
    va_end(ap);
}

void AsyncConnection::clRumble(unsigned short controllerNumber, unsigned short lowFreqMotor,
    unsigned short highFreqMotor) {
    // We push an event for the main thread to handle in order to properly synchronize
    // with the removal of game controllers that could result in our game controller
    // going away during this callback.
    qDebug() << "Session::clRumble";
    SDL_Event rumbleEvent = {};
    rumbleEvent.type = SDL_USEREVENT;
    rumbleEvent.user.code = SDL_CODE_GAMECONTROLLER_RUMBLE;
    rumbleEvent.user.data1 = (void *)(uintptr_t)controllerNumber;
    rumbleEvent.user.data2 = (void *)(uintptr_t)((lowFreqMotor << 16) | highFreqMotor);
    SDL_PushEvent(&rumbleEvent);
}

void AsyncConnection::clConnectionStatusUpdate(int connectionStatus) {
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Connection status update: %d", connectionStatus);
    if (!AsyncConnect::getInstance()->m_Preferences->connectionWarnings) {
        return;
    }

    SDL_Event event;
    event.type = SDL_CLIENT_STATUS_EVENT;
    event.user.code = connectionStatus;
    SDL_PushEvent(&event);
}

void AsyncConnection::clSetHdrMode(bool enabled, int displayIndex) {
    // If we're in the process of recreating our decoder when we get
    // this callback, we'll drop it. The main thread will make the
    // callback when it finishes creating the new decoder.
    qDebug()<<"syncConnectionStartThread::clSetHdrMode enabled = " << enabled << "displayIndex = " << displayIndex;
    SDL_Event event;
    event.type = SDL_CLSET_HDRMODE_EVENT;
    event.user.code = enabled;
    event.user.data1 = (void*)displayIndex;
    SDL_PushEvent(&event);
}


void CoonetThreadStart()
{
    AsyncConnect::getInstance()->start();
}

void CoonetThreadStop()
{
    AsyncConnect::getInstance()->stop();
    AsyncConnect::getInstance()->quit();
    AsyncConnect::getInstance()->wait();
}

void SetConnectInfo(StreamingPreferences* Preferences,
    STREAM_CONFIGURATION& StreamConfig,
    DECODER_RENDERER_CALLBACKS VideoCallbacks,
    AUDIO_RENDERER_CALLBACKS AudioCallbacks,
    NvComputer* Computer,
    NvApp& App,
    std::string& ClientUUid,
    std::string& PrivateKeyStr, int nAttachedGamepadMask, bool AudioDisabled,
    std::string& HttpJsonStr, QString& ClientOS,
    QString& ClientArch)
{
    AsyncConnect::getInstance()->SetConnectInfo(Preferences,
        StreamConfig, VideoCallbacks, AudioCallbacks, Computer, App, ClientUUid,
        PrivateKeyStr, nAttachedGamepadMask, AudioDisabled,
        HttpJsonStr, ClientOS, ClientArch);

}

void NotifyReconnect()
{
    qInfo()<<"NotifyReconnect";
    AsyncConnect::getInstance()->connect();
}

void StopStreamByNumber(int number)
{
    AsyncConnect::getInstance()->StopStreamByNumber(number);
}
void NotifyDisConnect()
{
    qInfo()<<"NotifyDisConnect";
    AsyncConnect::getInstance()->disconnect();
}
void SendSDLEvent()
{
    SDL_Event event;
    event.type = SDL_RESET_EVENT;   //tip 谈提示界面。我要重连
    event.quit.timestamp = SDL_GetTicks();
    SDL_PushEvent(&event);
}

void SendClosePopTipSDLEvent()
{
    SDL_Event event;
    event.type = SDL_CLOSE_POPTIP_EVENT;   //tip 谈提示界面。我要重连
    event.quit.timestamp = SDL_GetTicks();
    SDL_PushEvent(&event);
}

#define OutTime 1
void CheckSerStatusTask::run()
{
    return ;
    // Finish cleanup of the connection state
    NvHTTP http(m_address,m_httpsPort,m_serverCert);
    int nValue = -1;
    while (m_bWhileRun)
    {
        m_Mutex.lock();
        m_CondStatus.wait(&m_Mutex,OutTime * 1000);
        m_Mutex.unlock();
        try {

            nValue = http.CheckDisplay();
            auto pdata = std::make_shared<SServerInfoData>(OutTime);  // Example data
            pdata->nDisplayCount = nValue;
            memcpy(pdata->szName,"aaaaaaa",sizeof(pdata->szName));
            g_Manager.pushEvent(emEventDataType::emNoraml,pdata);

            SDL_Event event;
            event.type = SDL_SERVER_DATA_UPDATE_EVENT;
            SDL_PushEvent(&event);

        } catch (const GfeHttpResponseException &) {
        } catch (const QtNetworkReplyException &) {
        }
    }
    qInfo()<<" CheckSerStatusTask while end";
}
void StopSerStatusTask()
{
    gCheckSerStatusTask::getInstance()->Stop();
    gCheckSerStatusTask::getInstance()->quit();
    gCheckSerStatusTask::getInstance()->wait();
}
void ImmediatelyNotity()
{
    gCheckSerStatusTask::getInstance()->Notity();
}
void OperatorTyType(emOperatorType  nOpe)
{
    if(nOpe == emOperatorType::emExit)
    {
        SDL_Event event;
        event.type = SDL_QUIT;
        event.quit.timestamp = SDL_GetTicks();
        SDL_PushEvent(&event);
    }
    else if(nOpe == emOperatorType::emConnect)
    {
        NotifyReconnect();
    }
}
