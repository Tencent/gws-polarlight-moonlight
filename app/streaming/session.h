#pragma once

#include <QSemaphore>
#include <QProcess>
#include <QSysInfo>
#include <QDir>
#include <Limelight.h>
#include <opus_multistream.h>
#include "settings/streamingpreferences.h"
#include "input/input.h"
#include "video/decoder.h"
#include "audio/renderers/renderer.h"
#include "video/overlaymanager.h"
#include "Core/displayInfo.h"

enum emSDL_Screen_Event {
    SDL_NORMAL_EVENT = 0,
    SDL_SERVER_DATA_UPDATE_EVENT = 1,
    SDL_DISCONNECTION_EVENT = 2,
    SDL_RESET_EVENT = 3,
    SDL_RECONNECT_SUCESS_EVENT = 4,
    SDL_CHECKT_READY = 5,
    SDL_RECONNECT_EVENT = 6,
    SDL_CLSET_HDRMODE_EVENT = 7,
    SDL_CLIENT_STATUS_EVENT = 8,
    SDL_TIPS_EVENT = 9,
    SDL_CLOSE_WINDOW2_EVENT = 10,
    SDL_CLOSE_POPTIP_EVENT = 11,
    SDL_DISPLAY_CHANGED = 12,
    SDL_STATUS_CHANGED = 13,
    SDL_RENDER_HAVE_FIRSTFRAME = 14,
    SDL_CONNECT_ERROR_ALL,
};

template <typename T>
class PtrSingleton {
public:
    // 获取单例实例的静态方法，返回指向单例对象的指针
    static T* getInstance() {

        if(m_pInstance == nullptr)
        {
            m_pInstance = new T();
        }
        return m_pInstance;
    }

    // 删除拷贝构造和赋值运算符，确保单例类不可拷贝和赋值
    PtrSingleton(const PtrSingleton&) = delete;
    PtrSingleton& operator=(const PtrSingleton&) = delete;

protected:
    // 禁止直接实例化，只能通过 getInstance() 获取实例
    PtrSingleton() {}
    ~PtrSingleton() {}

private:
    static T *m_pInstance;

};
template<typename T>
T *PtrSingleton<T>::m_pInstance = nullptr;

void RegisterHotkey();
void UnregisterHotkey();
#ifdef _WIN64
#include <windows.h>
LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);
#endif

std::string parse(QString json, QString csessionid, QString &httpCurl, QString &token,
                  QJsonDocument &retJson, QString ClientOS, QString ClientArch);
extern  NvApp m_gNvapp;
class MetricsReportTask;

enum emExitRestart {
    RETCODE_RESTART = 1000,
};

class Session : public QObject
{
    Q_OBJECT

    friend class SdlInputHandler;
    friend class DeferredSessionCleanupTask;
    friend class DeferredSessionCleanupDisplay2Task;
    friend class MetricsReportTask;
    friend class ExecThread;
public:
    Session(NvComputer *computer, NvApp &app,StreamingPreferences *preferences = nullptr) ;
    Session();

    // NB: This may not get destroyed for a long time! Don't put any cleanup here.
    // Use Session::exec() or DeferredSessionCleanupTask instead.
    virtual ~Session() 
    {
        qDebug() << "~Session()";
    };

    void SetComputerInfo(NvComputer *computer, NvApp &app);

    void Release()
    {
        if(m_InputHandler)
        {
            delete m_InputHandler;
            m_InputHandler = nullptr;
        }
    }
    Q_INVOKABLE void exec(int displayOriginX, int displayOriginY);
    //bool TryAgainConnect();
    static
    void getDecoderInfo(SDL_Window* window,
                        bool& isHardwareAccelerated, bool& isFullScreenOnly,
                        bool& isHdrSupported, QSize& maxResolution);

    static Session* get()
    {
        return s_ActiveSession;
    }

    Overlay::OverlayManager& getOverlayManager(int id)
    {
        if (id == 1) {
            return m_OverlayManager;
        } else if (id == 2) {
        return m_OverlayManager2;
        }
        qInfo() << "id is not valid : " << id;
        return m_OverlayManager2;
    }
    Overlay::OverlayManager& getOverlayManager(SDL_Window* pWin)
    {
        return (pWin == m_Window2) ? m_OverlayManager2 : m_OverlayManager;
    }

    SDL_Window* getOverlayWindow(int id)
    {
        return (id == 1) ? m_Window: m_Window2;
    }

    void flushWindowEvents();
    void SetFullScren( const bool IsFullScreen)
    {
        m_IsFullScreen = IsFullScreen;
    }

    std::string getPrivateKey()
    {
        return m_PrivateKeyStr;
    }
    std::shared_ptr<NvComputer> getNvComputer()
    {
        qDebug() << "+++++++++++++++++++++++++++++++++getComputer  " << m_Computer.get();
        return m_Computer;
    }
    void SetComputer(NvComputer* c)
    {
        qDebug() << "---------------------------------setComputer old  " << m_Computer.get() << "___new____" << c;
        NvComputer* nc = new NvComputer(*c);
        m_Computer.reset(nc);
    }
    Session* getSession()
    {
        return this;
    }
    void SetIsFullScreen(bool flag)
    {
        m_IsFullScreen = flag;
    }
    bool GetIsFullScreen()
    {
        return m_IsFullScreen;
    }
    void toggleFullscreen(int index);

    void CheckDual();

    void SessionMain();
    void connectfailed();
    void InitMetricReport();
    void HideWindows2();
    void CheckDisplayStream(int nCount);
    void checkNetworkAndReconnect();

    void InitSdlWindow();

    void setCheckAutoCreateStreaming(){ m_bAutoStreaming = true;};
    void checkAutoCreateStreaming();
signals:
    void stageStarting(QString stage);

    void stageFailed(QString stage, int errorCode, QString failingPorts);

    void connectionStarted();

    void displayLaunchError(QString text);

    void displayLaunchWarning(QString text);

    void quitStarting();

    void sessionFinished(int portTestResult);

    // Emitted after sessionFinished() when the session is ready to be destroyed
    void readyForDeletion();


public:
    void toggleMinimized();
    void InitWebConfig();
    void QtWebViewClose();
    SDL_Window * getActiveSDLWindow();
    bool IsDualDisplay()
    {
        return m_isDualDisplay;
    }
    static Session* s_ActiveSession;
public:
    void execInternal();

    bool initialize();

    bool ReconnectInit();

    bool validateLaunch(SDL_Window* testWindow);

    void emitLaunchWarning(QString text);

    bool populateDecoderProperties(SDL_Window* window);
    void ResetDecoders();

    IAudioRenderer* createAudioRenderer(const POPUS_MULTISTREAM_CONFIGURATION opusConfig);

    bool testAudio(int audioConfiguration);

    int getAudioRendererCapabilities(int audioConfiguration);

    void getWindowDimensions(SDL_Window* window, int& x, int& y,
                             int& width, int& height, int& i);


    void mactoggleFullscreen();
    void notifyMouseEmulationMode(bool enabled);

    void updateOptimalWindowDisplayMode(SDL_Window* window);

    static
    bool isHardwareDecodeAvailable(SDL_Window* window,
                                   StreamingPreferences::VideoDecoderSelection vds,
                                   int videoFormat, int width, int height, int frameRate);

    static
    bool chooseDecoder(StreamingPreferences::VideoDecoderSelection vds,
                       SDL_Window* window, int videoFormat, int width, int height,
                       int frameRate, bool enableVsync, bool enableFramePacing,
                       bool testOnly,
                       IVideoDecoder*& chosenDecoder, DXGI_MODE_ROTATION rotation = DXGI_MODE_ROTATION_UNSPECIFIED, int streamid = -1);
    static
    int arInit(int audioConfiguration,
               const POPUS_MULTISTREAM_CONFIGURATION opusConfig,
               void* arContext, int arFlags);

    static
    void arCleanup();

    static
    void arDecodeAndPlaySample(char* sampleData, int sampleLength);

    static
    int drSetup(int videoFormat, int width, int height, int frameRate, void*, int);

    static
    void drCleanup();

    static
    int drSubmitDecodeUnit(PDECODE_UNIT du);

    void ReInitInfor();
    void SDLInitSubSystem();
    void DeleteGlobalSDLWindows();
    void ConnectStutasUpdate(int connectionStatus);

    bool ReseCthooseDecoder(StreamingPreferences::VideoDecoderSelection vds, SDL_Window *window, int videoFormat,
                            int width, int height, int frameRate, bool enableVsync, bool enableFramePacing,
                            bool testOnly, IVideoDecoder *&chosenDecoder);

    void ReFresh();
    void HaveFirstFrame(int i) {
        if (i == 1)
            m_HavefirstFrame1 = true;
        else if (i == 2)
            m_HavefirstFrame2 = true;
    }

    void ResetVideoDecoder();
    IVideoDecoder* GetVideoDecoder()
    {
        return m_VideoDecoder;
    }
    QMutex& GetSessionMutex()
    {
        return m_RunMutex;
    }
    void PauseVideoDecoders()
    {
        SDL_AtomicLock(&m_DecoderLock);
        if (m_VideoDecoder)
        {
            m_VideoDecoder->Pause();
            LiWakeWaitForVideoFrame(1);
            m_VideoDecoder->WaitPaused();
        }
        if (m_VideoDecoder2)
        {
            m_VideoDecoder2->Pause();
            LiWakeWaitForVideoFrame(2);
            m_VideoDecoder2->WaitPaused();
        }
        SDL_AtomicUnlock(&m_DecoderLock);
    }
    void WakeupVideoDecoders()
    {
        SDL_AtomicLock(&m_DecoderLock);
        if (m_VideoDecoder)
        {
            m_VideoDecoder->Wakeup();
        }
        if (m_VideoDecoder2)
        {
            m_VideoDecoder2->Wakeup();
        }
        SDL_AtomicUnlock(&m_DecoderLock);
    }
    void SendSessionInfoPortal(QString message = "");
    void SetRequestComputerInforData(const QString &szCSessionID,const std::string &Curl,const  std::string &arch);
    STREAM_CONFIGURATION getStreamConfig() { return m_StreamConfig; }
    StreamingPreferences* getStreamingPreferences() { return m_Preferences; }

    StreamingPreferences* m_Preferences;
    bool m_IsFullScreen;
    STREAM_CONFIGURATION m_StreamConfig;
    DECODER_RENDERER_CALLBACKS m_VideoCallbacks;
    AUDIO_RENDERER_CALLBACKS m_AudioCallbacks;
    std::shared_ptr<NvComputer> m_Computer = nullptr;
    NvApp m_App;
    SDL_Window* m_Window = nullptr;
    SDL_Window* m_Window2 = nullptr;    //Add support for second window
    DXGI_MODE_ROTATION m_WindowRotation = static_cast<DXGI_MODE_ROTATION>(-1);
    DXGI_MODE_ROTATION m_Window2Rotation = static_cast<DXGI_MODE_ROTATION>(-1);    //Add support for second window
    IVideoDecoder* m_VideoDecoder;
    IVideoDecoder* m_VideoDecoder2; //Add decoder for second window
    SDL_SpinLock m_DecoderLock;
    bool m_AudioDisabled;
    bool m_AudioMuted;
    Uint32 m_FullScreenFlag;
    int m_DisplayOriginX;
    int m_DisplayOriginY;
    bool m_ThreadedExec;
    bool m_UnexpectedTermination;
    SdlInputHandler* m_InputHandler;
    int m_MouseEmulationRefCount;
    int m_FlushingWindowEventsRef;
    int m_PortTestResults;

    bool m_VideoStreamInitialized = false;
    int m_ActiveVideoFormat =0;
    int m_ActiveVideoWidth = 0;
    int m_ActiveVideoHeight =0;
    int m_ActiveVideoFrameRate =0;

    OpusMSDecoder* m_OpusDecoder;
    IAudioRenderer* m_AudioRenderer;
    OPUS_MULTISTREAM_CONFIGURATION m_AudioConfig;
    int m_AudioSampleCount;
    Uint32 m_DropAudioEndTime;

    Overlay::OverlayManager m_OverlayManager;
    Overlay::OverlayManager m_OverlayManager2;

    //static CONNECTION_LISTENER_CALLBACKS k_ConnCallbacks;

    static QSemaphore s_ActiveSessionSemaphore;
    MetricsReportTask* m_metricsReportTask;
    bool m_isDualDisplay = false;

    QString m_ClientOS;
    int n_RtspTryAgain;
    bool m_needsFirstEnterCapture = false;
    bool m_needsPostDecoderCreationCapture = false;

    int m_currentDisplayIndex = -1;
    int m_currentDisplayIndex2 = -1;
    SDL_Surface* m_iconSurface = nullptr;
    SDL_Surface* m_iconSurface2 = nullptr;
    uint32_t m_winID1 = -1;
    uint32_t m_winID2 = -1;
private:
    void CheckAndCloseWindow(uint32_t winId);
    void CloseVideoStreamAndWindow(int streamId);
    bool SendStopVideoMessage(int streamId);
    void UpdateConfigSize(const GleamCore::DisplayInfo disp);
    void OnDisplayConfigChanged();
    void KeepWindowAspectRatio(SDL_Event *event);
public:
    QMutex m_RunMutex;
    int m_ConnectSucess = false;
    static std::string m_ClientUUid;
    static std::string m_PrivateKeyStr;
    static int m_DisConnectErrorCode;
    static SDL_Window* g_Window ;
    static SDL_Window* g_Window2;    //Add support for second window
private:
    QString m_szCSessionID;
    QString m_ClientArch;
    std::string m_HttpJsonStr;
    bool m_bAutoStreaming = false;
    bool m_HavefirstFrame1 = false;
    bool m_HavefirstFrame2 = false;
};

using SessionSingleton = PtrSingleton<Session>;

class DeferredSessionCleanupTask : public QObject, public QRunnable
{
    Q_OBJECT
public:
    DeferredSessionCleanupTask();
public:
    virtual ~DeferredSessionCleanupTask();
public:
    void run() override;
};

qreal GetPrimaryDevicePixelRatio();
