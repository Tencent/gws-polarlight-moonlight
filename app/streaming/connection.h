#pragma once

#include <QSemaphore>
#include <QProcess>
#include <QSysInfo>
#include <QDir>
#include <Limelight.h>
#include <opus_multistream.h>
#include "settings/streamingpreferences.h"
#include "streaming/session.h"
#include "streaming/eventManager.h"
enum  TipType
{
    emNormal = 0,
    emPopTip = 1,
    emErrorNoraml = 2
};
class AsyncConnection : public QThread
{

    Q_OBJECT
public:
    enum ConnectStatus
    {
        emInit = 0,
        emDisConnect = 1,
        emConnect = 2,
        emStopStream1 = 3,
        emStopStream2 =4,
        emStop = 5
    };
    AsyncConnection():QThread(nullptr)
    {
        setObjectName("AsyncConnection");
        m_Status = ConnectStatus::emInit;
    }
    void StopStream(int num);

    void StopConnect(int nStreamNum);
    void InitConnect();

    void connect()
    {
        QMutexLocker locker(&m_mutex);
        m_Status = ConnectStatus::emConnect;
        m_nTipCount = 0;
        m_CondStatusChanged.wakeOne();
    }
    void disconnect()
    {
        QMutexLocker locker(&m_mutex);
        m_Status = ConnectStatus::emDisConnect;
        m_CondStatusChanged.wakeOne();

    }
    void StopStreamByNumber(int number)
    {
        QMutexLocker locker(&m_mutex);
        if (number == 2) {
            m_Status = ConnectStatus::emStopStream2;
        }
        m_CondStatusChanged.wakeOne();
    }

    void stop()
    {
        qInfo()<<" AsyncConnection stop";
        QMutexLocker locker(&m_mutex);
        m_Status = ConnectStatus::emStop;
        m_CondStatusChanged.wakeOne();
    }

    QMutex &GetMutex()
    {
        return m_mutex;
    }
    void ChangeStatus(ConnectStatus status);
    void SetConnectInfo( StreamingPreferences* Preferences,
                         STREAM_CONFIGURATION &StreamConfig,
                         DECODER_RENDERER_CALLBACKS VideoCallbacks,
                         AUDIO_RENDERER_CALLBACKS AudioCallbacks,
                         NvComputer* Computer,
                         NvApp &App,
                         std::string &ClientUUid,
                         std::string &PrivateKeyStr,int nAttachedGamepadMask,bool AudioDisabled,
                         std::string &HttpJsonStr, QString &ClientOS,
                         QString &ClientArch)
    {
        qDebug()<<"AsyncConnection::SetConnectInfo ";
        m_Preferences = Preferences;
        m_StreamConfig = StreamConfig;
        m_VideoCallbacks = VideoCallbacks;
        m_AudioCallbacks = AudioCallbacks;
        m_Computer = Computer;
        m_App =App ;
        m_ClientUUid = ClientUUid;
        m_PrivateKeyStr = PrivateKeyStr;
        m_nAttachedGamepadMask = nAttachedGamepadMask;
    
        m_AudioDisabled = AudioDisabled;
        m_HttpJsonStr = HttpJsonStr;
        m_ClientOS = ClientOS;
        m_ClientArch = ClientArch;
        m_CurrentGameId = m_Computer?m_Computer->currentGameId:0;
        qDebug()<<"SetConnectInfo m_CurrentGameId = "<<m_CurrentGameId;
        m_VideoCallbacksTemp = VideoCallbacks;
        m_AudioCallbacksTemp = AudioCallbacks;
        m_nStreamNum = m_Computer?(m_Computer->isDualDisplay ? 2 : 1):1;
    }
    void SetConnectFlag(bool flag)
    {
        QMutexLocker locker(&m_mutex);
        m_bConnectFlag  = flag;
    }
    void RequestComputerInfor(QString Csessionid);
public:
    static void clStageStarting(int stage);

    static void clStageFailed(int stage, int errorCode);

    static void clConnectionTerminated(int  errcode);

    static void clLogMessage(const char* format, ...);

    static void clRumble(unsigned short controllerNumber, unsigned short lowFreqMotor, unsigned short highFreqMotor);

    static void clConnectionStatusUpdate(int connectionStatus);

    static void clSetHdrMode(bool enabled, int displayIndex);

public:
    int m_nTipCount = 0;
    bool m_bConnectFlag = false;
    ConnectStatus m_Status;
    QWaitCondition m_CondStatusChanged;
    QMutex m_mutex;

    StreamingPreferences* m_Preferences;
    STREAM_CONFIGURATION m_StreamConfig;
    DECODER_RENDERER_CALLBACKS m_VideoCallbacks;
    AUDIO_RENDERER_CALLBACKS m_AudioCallbacks;
    NvComputer* m_Computer;
    NvApp m_App;
    std::string m_ClientUUid;
    std::string m_PrivateKeyStr;
    int m_nAttachedGamepadMask;
    bool m_AudioDisabled;
    std::string m_HttpJsonStr;
    QString m_ClientOS;
    QString m_ClientArch;
    int m_CurrentGameId;
    int m_nStreamNum;
    DECODER_RENDERER_CALLBACKS m_VideoCallbacksTemp;
    AUDIO_RENDERER_CALLBACKS m_AudioCallbacksTemp;


};

using AsyncConnect = PtrSingleton<AsyncConnection>;
void SetConnectInfo(StreamingPreferences* Preferences,
    STREAM_CONFIGURATION& StreamConfig,
    DECODER_RENDERER_CALLBACKS VideoCallbacks,
    AUDIO_RENDERER_CALLBACKS AudioCallbacks,
    NvComputer* Computer,
    NvApp& App,
    std::string& ClientUUid,
    std::string& PrivateKeyStr, int nAttachedGamepadMask, bool AudioDisabled,
    std::string& HttpJsonStr, QString& ClientOS,
    QString& ClientArch);
void CoonetThreadStop();
void CoonetThreadStart();

void NotifyReconnect();
void NotifyDisConnect();
void SendSDLEvent();
void SendClosePopTipSDLEvent();
void StopStreamByNumber(int number);

class SServerInfoData:public EventBase
{
public:
    int nDisplayCount;
    char szName[32];
    SServerInfoData(int timeoutInSeconds):EventBase(timeoutInSeconds)
    {

    }

    ~SServerInfoData()
    {
    }
};

class CheckSerStatusTask : public QThread {
public:

    CheckSerStatusTask() : QThread(nullptr) {
        setObjectName("CheckSerStatusTask");
        m_bWhileRun = true;

    }

    void Stop()
    {
        qInfo()<<" CheckSerStatusTask stop";
        QMutexLocker locker(&m_Mutex);
        m_bWhileRun = false;
        Notity();
    }

    bool GetStatus()
    {
        return m_bWhileRun;
    }
    void Notity()
    {
        m_CondStatus.wakeOne();
    }
    void SetNvHttp(NvAddress &address, uint16_t &httpsPort, QSslCertificate &serverCert)
    {
        m_address = address;
        m_httpsPort = httpsPort;
        m_serverCert = serverCert;
    }

private:
    void run() override;
    bool m_bWhileRun;
    QWaitCondition m_CondStatus;
    QMutex m_Mutex;

    NvAddress m_address;
    uint16_t m_httpsPort;
    QSslCertificate m_serverCert;

};
using gCheckSerStatusTask = PtrSingleton<CheckSerStatusTask>;


void StopSerStatusTask();

void ImmediatelyNotity();


enum emOperatorType
{
    emExit = 0,
    emConnect = 1,
    emMax,
};

void OperatorTyType(emOperatorType nOpe);























