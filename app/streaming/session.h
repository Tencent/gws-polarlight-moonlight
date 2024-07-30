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
#ifdef _WIN64
#include <windows.h>

void RegisterHotkey();
void UnregisterHotkey();
LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);
#endif
class CheckDisplayDualTask;
class MetricsReportTask;

enum emExitRestart {
    RETCODE_RESTART = 1000,
};

class Restarter
{
public:
    Restarter(int argc, char *argv[])
    {
        Q_UNUSED(argc)
        m_executable = QString::fromLocal8Bit(argv[0]);
        m_workingPath = QDir::currentPath();

        QStringList argsList;
        for(int i = 1; i < argc ; i++) {
            argsList.append(QString::fromUtf8(argv[i]));
        }
        setArguments(argsList);
    }

    void setArguments(const QStringList &args)
    {
        m_args = args;
    }

    QString executable() const
    {
        return m_executable;
    }
    QStringList arguments() const
    {
        return m_args;
    }
    QString workingPath() const
    {
        return m_workingPath;
    }

    int restart(int exitCode)
    {
        QProcess::startDetached(m_executable, m_args, m_workingPath);
        return 0;
    }

private:
    QString m_executable;
    QStringList m_args;
    QString m_workingPath;
};
class Session : public QObject
{
    Q_OBJECT

    friend class SdlInputHandler;
    friend class DeferredSessionCleanupTask;
    friend class DeferredSessionCleanupDisplay2Task;
    friend class MetricsReportTask;
    friend class AsyncConnectionStartThread;
    friend class ExecThread;
    friend class CheckDisplayDualTask;
public:
    explicit Session(NvComputer* computer, NvApp& app, StreamingPreferences *preferences = nullptr);

    // NB: This may not get destroyed for a long time! Don't put any cleanup here.
    // Use Session::exec() or DeferredSessionCleanupTask instead.
    virtual ~Session()
    {

        delete m_metricsReportTask;
        m_metricsReportTask = NULL;
    };

    Q_INVOKABLE void exec(int displayOriginX, int displayOriginY);

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
        return (id == 1) ? m_OverlayManager : m_OverlayManager2;
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
    void SetComputerInfor(const std::string &Curl,const std::string &Token,const std::string &ClientArch,
                          const std::string &ClientUUid,const std::string &privateKeyStr, const bool IsFullScreen);
    std::string parse(QString json,QString csessionid,QString &httpCurl,QString &token,QJsonDocument &retJson);

    std::string getPrivateKey()
    {
        return m_PrivateKeyStr;
    }
    NvComputer* getNvComputer()
    {
        return m_Computer;
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
    void toggleFullscreen();
    void SetDisplayCount(int n,int status = 0);
    void CheckDual();
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
    static Session* s_ActiveSession;
private:
    void execInternal();

    bool initialize();

    bool startConnectionAsync();

    bool validateLaunch(SDL_Window* testWindow);

    void emitLaunchWarning(QString text);

    bool populateDecoderProperties(SDL_Window* window);

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
                       IVideoDecoder*& chosenDecoder);

    static
    void clStageStarting(int stage);

    static
    void clStageFailed(int stage, int errorCode);

    static
    void clConnectionTerminated(int errorCode);

    static
    void clLogMessage(const char* format, ...);

    static
    void clRumble(unsigned short controllerNumber, unsigned short lowFreqMotor, unsigned short highFreqMotor);

    static
    void clConnectionStatusUpdate(int connectionStatus);

    static
    void clSetHdrMode(bool enabled);

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

    void RequestComputerInfor(QString Csessionid);

    StreamingPreferences* m_Preferences;
    bool m_IsFullScreen;
    STREAM_CONFIGURATION m_StreamConfig;
    DECODER_RENDERER_CALLBACKS m_VideoCallbacks;
    AUDIO_RENDERER_CALLBACKS m_AudioCallbacks;
    NvComputer* m_Computer;
    NvApp m_App;
    SDL_Window* m_Window = nullptr;
    SDL_Window* m_Window2 = nullptr;    //Add support for second window
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

    bool m_AsyncConnectionSuccess;
    int m_PortTestResults;

    int m_ActiveVideoFormat;
    int m_ActiveVideoWidth;
    int m_ActiveVideoHeight;
    int m_ActiveVideoFrameRate;

    OpusMSDecoder* m_OpusDecoder;
    IAudioRenderer* m_AudioRenderer;
    OPUS_MULTISTREAM_CONFIGURATION m_AudioConfig;
    int m_AudioSampleCount;
    Uint32 m_DropAudioEndTime;

    Overlay::OverlayManager m_OverlayManager;
    Overlay::OverlayManager m_OverlayManager2;

    static CONNECTION_LISTENER_CALLBACKS k_ConnCallbacks;

    static QSemaphore s_ActiveSessionSemaphore;
    bool m_isDualDisplay;

    std::string m_ClientUUid;
    std::string m_PrivateKeyStr;
    //std::string m_session_Client_UUid;
    QString m_ClientArch;
    QString m_ClientOS;


    MetricsReportTask *m_metricsReportTask;
    CheckDisplayDualTask *m_checkDisplayDual;
    int n_RtspTryAgain = 0;
public:
    std::string m_HttpJsonStr;
    std::string m_Token;
    static int m_DisConnectErrorCode;
};
