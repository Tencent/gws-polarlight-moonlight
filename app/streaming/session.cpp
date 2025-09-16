#include <Limelight.h>
#include <SDL.h>
#include <utility>
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
#include <QTimer>
#include <QDateTime>
#include <shared_mutex>

#include "backend/richpresencemanager.h"
#include "session.h"
#include "settings/streamingpreferences.h"
#include "streaming/share_data.h"
#include "streaming/streamutils.h"

#include "connection.h"
#include "utils.h"
#include "EventReporter.h"
#include "CountdownWindow.h"
#include "Core/potalInteractionManager.h"
#include "Core/MsgInfo.h"
#ifdef Q_OS_WIN32
#  include <windows.h>
#endif
#ifdef HAVE_FFMPEG
#  include "video/ffmpeg.h"
#endif

#ifdef HAVE_SLVIDEO
#  include "video/slvid.h"
#endif

#ifdef Q_OS_WIN32
// Scaling the icon down on Win32 looks dreadful, so render at lower res
#  define ICON_SIZE 32
#else
#  define ICON_SIZE 64
#endif

// HACK: Remove once proper Dark Mode support lands in SDL
#ifdef Q_OS_WIN32
#  include <SDL_syswm.h>
#  include <dwmapi.h>
#  ifndef DWMWA_USE_IMMERSIVE_DARK_MODE_OLD
#    define DWMWA_USE_IMMERSIVE_DARK_MODE_OLD 19
#  endif
#  ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#    define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#  endif
#endif

#define SDL_CODE_FLUSH_WINDOW_EVENT_BARRIER 100
#define SDL_CODE_GAMECONTROLLER_RUMBLE 101
#include <openssl/rand.h>
#include <streaming/gleamapi.h>
#include "Core/httpmessagetask.h"
#include <QCoreApplication>
#include <QCursor>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QSvgRenderer>
#include <QThreadPool>
#include <QWindow>
#include <QtEndian>
#include <sstream>
#include "Core/connectionmanager.h"
#include "Core/settingWebView.h"
#include "Core/helpWebView.h"
#include "Core/sessionui.h"

extern CSettingWebView *g_pSettingWebView;
#define CONN_TEST_SERVER "qt.conntest.Gleam-stream.org"

PopupWindow *g_pConfigPopup = nullptr;

std::vector<char> g_Encrypted_AES_Key;


Session *Session::s_ActiveSession;
QSemaphore Session::s_ActiveSessionSemaphore(1);
int Session::m_DisConnectErrorCode = 0;


std::string Session::m_ClientUUid;
std::string Session::m_PrivateKeyStr;

SDL_Window* Session::g_Window = NULL;
SDL_Window* Session::g_Window2= NULL;    //Add support for second window



#ifdef _WIN64
HHOOK hHotKey = nullptr;

void RegisterHotkey() {
  HHOOK hHotKey = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, NULL, 0);
  if (hHotKey == NULL) {
    qDebug() << "not hook";
  }
}
void UnregisterHotkey() { UnhookWindowsHookEx(hHotKey); }
// 处理快捷键的回调函数
LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
  if (nCode == HC_ACTION) {
    KBDLLHOOKSTRUCT *kbs = (KBDLLHOOKSTRUCT *)lParam;
    if (wParam == WM_KEYDOWN && kbs->vkCode == 'D' && (GetAsyncKeyState(VK_SHIFT) & 0x8000) &&
        (GetAsyncKeyState(VK_CONTROL) & 0x8000) && (GetAsyncKeyState(VK_MENU) & 0x8000)) {
      Session::s_ActiveSession->toggleMinimized();
      UnregisterHotkey();
      return 1;
    }
  }
  return CallNextHookEx(NULL, nCode, wParam, lParam);
}
#endif


bool SetDisplayList(const char* list, std::vector<std::string> &output_list)
{
    std::string strList(list);

    std::string output;
    std::istringstream iss(strList);
    while (std::getline(iss, output, ','))
    {
        std::string outName(output,strlen(output.c_str()));
        output_list.push_back(output);
    }

    return true;
}
std::string parse(QString json, QString csessionid, QString &httpCurl, QString &token,
                  QJsonDocument &retJson, QString ClientOS, QString ClientArch) {
    QJsonParseError jsonerror;
    QJsonDocument doc = QJsonDocument::fromJson(json.toLatin1(), &jsonerror);
    QString clientVersion, clientIP;
    if (!doc.isNull() && jsonerror.error == QJsonParseError::NoError) {
        if (doc.isObject()) {
            QJsonObject object = doc.object();
            if (object.contains("endpoint") && object["endpoint"].isString()) {
                QJsonValue value = object.value("endpoint");
                qDebug() << value.toString();
                httpCurl = value.toString();
            }
            if (object.contains("token") && object["token"].isString()) {
                QJsonValue value = object.value("token");
                qDebug() << value.toString();
                token = value.toString();
            }
            if (object.contains("body")) {
                if (object["body"].isObject()) {
                    QJsonValue value = object.value("body");
                    QJsonObject chidren_obj = object.value("body").toObject();

                    if (chidren_obj.contains("attributes")) {
                        if (chidren_obj["attributes"].isObject()) {
                            QJsonValue chidren_value = chidren_obj.value("attributes");
                            QJsonObject obj = chidren_value.toObject();
                            if (obj.contains("clientVersion")) {
                                QJsonValue value = obj.value("clientVersion");
                                if (value.isString()) {
                                    clientVersion = value.toString();
                                }
                            }
                            if (obj.contains("clientIP")) {
                                QJsonValue value = obj.value("clientIP");
                                if (value.isString()) {
                                    clientIP = value.toString();
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    // QJsonObject body;
    QJsonObject attributes;
    QJsonObject subObj;
    subObj.insert("clientVersion", clientVersion);
    subObj.insert("clientIP", clientIP);

    subObj.insert("clientOS", ClientOS);
    subObj.insert("clientArch", ClientArch);

    QJsonObject sessionid;
    sessionid.insert("sessionId", csessionid);
    sessionid.insert("attributes", subObj);

    QJsonDocument newdoc(sessionid);
    retJson = newdoc;

    QByteArray newjson = newdoc.toJson();
    // qDebug() << "newjson = " << newdoc.toJson();
    return newjson.toStdString();
}

bool Session::chooseDecoder(StreamingPreferences::VideoDecoderSelection vds, SDL_Window *window, int videoFormat,
                            int width, int height, int frameRate, bool enableVsync, bool enableFramePacing,
                            bool testOnly, IVideoDecoder *&chosenDecoder, DXGI_MODE_ROTATION rotation, int streamid) {
  DECODER_PARAMETERS params;

  // We should never have vsync enabled for test-mode.
  // It introduces unnecessary delay for renderers that may
  // block while waiting for a backbuffer swap.
  SDL_assert(!enableVsync || !testOnly);
  params.width = width == 0 ? 1920 : width;
  params.height = height == 0 ? 1080 : height;
  params.rotation = rotation;
  params.frameRate = frameRate;
  params.videoFormat = videoFormat;
  params.window = window;
  params.enableVsync = enableVsync;
  params.enableFramePacing = enableFramePacing;
  params.testOnly = testOnly;
  params.vds = vds;

  SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "V-sync %s", enableVsync ? "enabled" : "disabled");

#ifdef HAVE_SLVIDEO
  chosenDecoder = new SLVideoDecoder(testOnly);
  if (chosenDecoder->initialize(&params)) {
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "SLVideo video decoder chosen");
    return true;
  } else {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Unable to load SLVideo decoder");
    delete chosenDecoder;
    chosenDecoder = nullptr;
  }
#endif

#ifdef HAVE_FFMPEG
  chosenDecoder = new FFmpegVideoDecoder(testOnly);
  chosenDecoder->SetStreamId(streamid);

  if (chosenDecoder->initialize(&params)) {
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "FFmpeg-based video decoder chosen");
    return true;
  } else {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Unable to load FFmpeg decoder");
    delete chosenDecoder;
    chosenDecoder = nullptr;
  }
#endif

#if !defined(HAVE_FFMPEG) && !defined(HAVE_SLVIDEO)
#  error No video decoding libraries available!
#endif

  // If we reach this, we didn't initialize any decoders successfully
  return false;
}

int Session::drSetup(int videoFormat, int width, int height, int frameRate, void *, int) {
    qDebug()<<"Session::drSetup s_ActiveSession = "<<s_ActiveSession;
  s_ActiveSession->m_ActiveVideoFormat = videoFormat;
  s_ActiveSession->m_ActiveVideoWidth = width;
  s_ActiveSession->m_ActiveVideoHeight = height;
  s_ActiveSession->m_ActiveVideoFrameRate = frameRate;

  // Defer decoder setup until we've started streaming so we
  // don't have to hide and show the SDL window (which seems to
  // cause pointer hiding to break on Windows).

  SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Video stream is %dx%dx%d (format 0x%x)", width, height, frameRate,
              videoFormat);
  //to let renders created from SessionLoop
  s_ActiveSession->m_VideoStreamInitialized = true;
  SDL_Event event;
  event.type = SDL_RENDER_TARGETS_RESET;
  SDL_PushEvent(&event);
  return 0;
}

int Session::drSubmitDecodeUnit(PDECODE_UNIT du) {
  // Use a lock since we'll be yanking this decoder out
  // from underneath the session when we initiate destruction.
  // We need to destroy the decoder on the main thread to satisfy
  // some API constraints (like DXVA2). If we can't acquire it,
  // that means the decoder is about to be destroyed, so we can
  // safely return DR_OK and wait for the IDR frame request by
  // the decoder reinitialization code.
    qDebug() << "drSubmitDecodeUnit";
  if (SDL_AtomicTryLock(&s_ActiveSession->m_DecoderLock)) {
    int ret1 = 0;
    int ret2 = 0;
    IVideoDecoder *decoder = s_ActiveSession->GetVideoDecoder();
    if (decoder != nullptr) {
      ret1 = decoder->submitDecodeUnit(du);
    }
    if (s_ActiveSession->m_isDualDisplay) {
      IVideoDecoder *decoder2 = SessionSingleton::getInstance()->m_VideoDecoder2;
      if (decoder2 != nullptr) {
        ret2 = decoder2->submitDecodeUnit(du);
      }
    }
    SDL_AtomicUnlock(&s_ActiveSession->m_DecoderLock);
    if (ret1 == 0 && ret2 == 0) {
      return DR_OK;
    } else if (ret1 != 0) {
      return ret1;
    } else {
      return ret2;
    }
  } else {
    // Decoder is going away. Ignore anything coming in until
    // the lock is released.
    return DR_OK;
  }
}

void Session::getDecoderInfo(SDL_Window *window, bool &isHardwareAccelerated, bool &isFullScreenOnly,
                             bool &isHdrSupported, QSize &maxResolution) {
  IVideoDecoder *decoder;

  // Try an HEVC Main10 decoder first to see if we have HDR support
  if (chooseDecoder(StreamingPreferences::VDS_FORCE_HARDWARE, window, VIDEO_FORMAT_H265_MAIN10, 1920, 1080, 60, false,
                    false, true, decoder)) {
    isHardwareAccelerated = decoder->isHardwareAccelerated();
    isFullScreenOnly = decoder->isAlwaysFullScreen();
    isHdrSupported = decoder->isHdrSupported();
    maxResolution = decoder->getDecoderMaxResolution();
    delete decoder;

    return;
  }

  // HDR can only be supported by a hardware codec that can handle HEVC Main10.
  // If we made it this far, we don't have one, so HDR will not be available.
  isHdrSupported = false;

  // Try a regular hardware accelerated HEVC decoder now
  if (chooseDecoder(StreamingPreferences::VDS_FORCE_HARDWARE, window, VIDEO_FORMAT_H265, 1920, 1080, 60, false, false,
                    true, decoder)) {
    isHardwareAccelerated = decoder->isHardwareAccelerated();
    isFullScreenOnly = decoder->isAlwaysFullScreen();
    maxResolution = decoder->getDecoderMaxResolution();
    delete decoder;

    return;
  }

  // If we still didn't find a hardware decoder, try H.264 now.
  // This will fall back to software decoding, so it should always work.
  if (chooseDecoder(StreamingPreferences::VDS_AUTO, window, VIDEO_FORMAT_H264, 1920, 1080, 60, false, false, true,
                    decoder)) {
    isHardwareAccelerated = decoder->isHardwareAccelerated();
    isFullScreenOnly = decoder->isAlwaysFullScreen();
    maxResolution = decoder->getDecoderMaxResolution();
    delete decoder;

    return;
  }

  SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to find ANY working H.264 or HEVC decoder!");
}

bool Session::isHardwareDecodeAvailable(SDL_Window *window, StreamingPreferences::VideoDecoderSelection vds,
                                        int videoFormat, int width, int height, int frameRate) {
  IVideoDecoder *decoder;

  if (!chooseDecoder(vds, window, videoFormat, width, height, frameRate, false, false, true, decoder)) {
    return false;
  }

  bool ret = decoder->isHardwareAccelerated();

  delete decoder;

  return ret;
}

void Session::ResetDecoders()
{
    if (m_VideoCallbacks.capabilities & CAPABILITY_PULL_RENDERER) {
        // It is an error to pass a push callback when in pull mode
        m_VideoCallbacks.submitDecodeUnit = nullptr;
    }
    else {
        m_VideoCallbacks.submitDecodeUnit = drSubmitDecodeUnit;
    }
}

bool Session::populateDecoderProperties(SDL_Window *window) {
  IVideoDecoder *decoder;

  if (!chooseDecoder(m_Preferences->videoDecoderSelection, window,
                      m_StreamConfig.enableHdr ? VIDEO_FORMAT_H265_MAIN10 :
                                                (m_StreamConfig.supportsHevc ? VIDEO_FORMAT_H265 : VIDEO_FORMAT_H264),
                      m_StreamConfig.width, m_StreamConfig.height, m_StreamConfig.fps, false, false, true, decoder)) {
    return false;
  }

  m_VideoCallbacks.capabilities = decoder->getDecoderCapabilities();
  if (m_VideoCallbacks.capabilities & CAPABILITY_PULL_RENDERER) {
    // It is an error to pass a push callback when in pull mode
    m_VideoCallbacks.submitDecodeUnit = nullptr;
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "m_VideoCallbacks.submitDecodeUnit NULL  ");
  } else {
      SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "m_VideoCallbacks.submitDecodeUnit Set CallBack");
    m_VideoCallbacks.submitDecodeUnit = drSubmitDecodeUnit;
  }

  {
    bool ok;

    m_StreamConfig.colorSpace = qEnvironmentVariableIntValue("COLOR_SPACE_OVERRIDE", &ok);
    if (ok) {
      SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Using colorspace override: %d", m_StreamConfig.colorSpace);
    } else {
      m_StreamConfig.colorSpace = decoder->getDecoderColorspace();
    }

    m_StreamConfig.colorRange = qEnvironmentVariableIntValue("COLOR_RANGE_OVERRIDE", &ok);
    if (ok) {
      SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Using color range override: %d", m_StreamConfig.colorRange);
    } else {
      m_StreamConfig.colorRange = decoder->getDecoderColorRange();
    }
  }

  if (decoder->isAlwaysFullScreen()) {
    m_IsFullScreen = true;
  }

  delete decoder;

  return true;
}

Session::Session(NvComputer *computer, NvApp &app, StreamingPreferences *preferences)
    : m_Preferences(preferences ? preferences : new StreamingPreferences(this)),
    m_IsFullScreen(m_Preferences->windowMode != StreamingPreferences::WM_WINDOWED ||
                   !WMUtils::isRunningDesktopEnvironment()),
    m_Computer(computer),
    m_App(app),
    m_Window(nullptr),
    m_Window2(nullptr),
    m_VideoDecoder(nullptr),
    m_VideoDecoder2(nullptr),
    m_DecoderLock(0),
    m_AudioDisabled(false),
    m_AudioMuted(false),
    m_DisplayOriginX(0),
    m_DisplayOriginY(0),
    m_UnexpectedTermination(true),  // Failure prior to streaming is unexpected
    m_InputHandler(nullptr),
    m_MouseEmulationRefCount(0),
    m_FlushingWindowEventsRef(0),
    m_PortTestResults(0),
    m_OpusDecoder(nullptr),
    m_AudioRenderer(nullptr),
    m_AudioSampleCount(0),
    m_DropAudioEndTime(0),
    m_metricsReportTask(nullptr),
    m_isDualDisplay(computer->isDualDisplay)
{
    InitWebConfig();
}

Session::Session() : m_Preferences(NULL),
      m_Window(nullptr),
      m_Window2(nullptr),
      m_VideoDecoder(nullptr),
      m_VideoDecoder2(nullptr),
      m_DecoderLock(0),
      m_AudioDisabled(false),
      m_AudioMuted(false),
      m_DisplayOriginX(0),
      m_DisplayOriginY(0),
      m_UnexpectedTermination(true),  // Failure prior to streaming is unexpected
      m_InputHandler(nullptr),
      m_MouseEmulationRefCount(0),
      m_FlushingWindowEventsRef(0),
      m_PortTestResults(0),
      m_OpusDecoder(nullptr),
      m_AudioRenderer(nullptr),
      m_AudioSampleCount(0),
      m_DropAudioEndTime(0),
      m_metricsReportTask(nullptr)
{
    InitWebConfig();
}

void Session::SetComputerInfo(NvComputer *computer, NvApp &app)
{
    qDebug()<<"Session::SetComputerInfo";
    SetComputer(computer);
    m_App = app;
}

bool Session::ReconnectInit()
{
  LiInitializeVideoCallbacks(&m_VideoCallbacks);
  m_VideoCallbacks.setup = drSetup;

  SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Video bitrate: %d kbps", m_StreamConfig.bitrate);
  LiInitializeAudioCallbacks(&m_AudioCallbacks);
  m_AudioCallbacks.init = arInit;
  m_AudioCallbacks.cleanup = arCleanup;
  m_AudioCallbacks.decodeAndPlaySample = arDecodeAndPlaySample;
  m_AudioCallbacks.capabilities = getAudioRendererCapabilities(m_StreamConfig.audioConfiguration);
    qDebug()<<"Session::ReconnectInit() befer m_VideoCallbacks.capabilities"<<m_VideoCallbacks.capabilities;
    qDebug()<<"Session::ReconnectInit() befer m_AudioCallbacks.capabilities"<<m_AudioCallbacks.capabilities;

  SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Audio channel count: %d",
              CHANNEL_COUNT_FROM_AUDIO_CONFIGURATION(m_StreamConfig.audioConfiguration));
  SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Audio channel mask: %X",
              CHANNEL_MASK_FROM_AUDIO_CONFIGURATION(m_StreamConfig.audioConfiguration));
  SDL_SetMainReady();
  SDL_Window *testWindow = SDL_CreateWindow("", 0, 0, 1280, 720,
                            SDL_WINDOW_HIDDEN | StreamUtils::getPlatformWindowFlags());
  if (!testWindow)
  {
      SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Failed to create test window with platform flags: %s", SDL_GetError());
      testWindow = SDL_CreateWindow("", 0, 0, 1280, 720, SDL_WINDOW_HIDDEN);
      if (!testWindow)
      {
          SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create window for hardware decode test: %s",
                       SDL_GetError());
          SDL_QuitSubSystem(SDL_INIT_VIDEO);
          return false;
      }
  }
  switch (m_Preferences->videoCodecConfig) {
  case StreamingPreferences::VCC_AUTO:
      // TODO: Determine if HEVC is better depending on the decoder
      m_StreamConfig.supportsHevc = isHardwareDecodeAvailable(testWindow, m_Preferences->videoDecoderSelection,
                                                              VIDEO_FORMAT_H265, m_StreamConfig.width,
                                                              m_StreamConfig.height, m_StreamConfig.fps);
#ifdef Q_OS_DARWIN
      {
          // Prior to GFE 3.11, GFE did not allow us to constrain
          // the number of reference frames, so we have to fixup the SPS
          // to allow decoding via VideoToolbox on macOS. Since we don't
          // have fixup code for HEVC, just avoid it if GFE is too old.
          if(m_Computer)
          {
              QVector<int> gfeVersion = NvHTTP::parseQuad(m_Computer->gfeVersion);
              if (gfeVersion.isEmpty() ||  // Very old versions don't have GfeVersion at all
                  gfeVersion[0] < 3 || (gfeVersion[0] == 3 && gfeVersion[1] < 11)) {
                  SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Disabling HEVC on macOS due to old GFE version");
                  m_StreamConfig.supportsHevc = false;
              }
          }
      }
#endif
      m_StreamConfig.enableHdr = false;
      break;
  case StreamingPreferences::VCC_FORCE_H264:
      m_StreamConfig.supportsHevc = false;
      m_StreamConfig.enableHdr = false;
      break;
  case StreamingPreferences::VCC_FORCE_HEVC:
      m_StreamConfig.supportsHevc = true;
      m_StreamConfig.enableHdr = false;
      break;
  case StreamingPreferences::VCC_FORCE_HEVC_HDR:
      m_StreamConfig.supportsHevc = true;
      m_StreamConfig.enableHdr = true;
      break;
  }

  switch (m_Preferences->windowMode) {
  default:
  case StreamingPreferences::WM_FULLSCREEN_DESKTOP:
      // Only use full-screen desktop mode if we're running a desktop environment
      if (WMUtils::isRunningDesktopEnvironment()) {
          m_FullScreenFlag = SDL_WINDOW_FULLSCREEN_DESKTOP;
          break;
      }
      // Fall-through
  case StreamingPreferences::WM_FULLSCREEN:
      m_FullScreenFlag = SDL_WINDOW_FULLSCREEN;
      break;
  }
#if !SDL_VERSION_ATLEAST(2, 0, 11)
    // HACK: Using a full-screen window breaks mouse capture on the Pi's LXDE
    // GUI environment. Force the session to use windowed mode (which won't
    // really matter anyway because the MMAL renderer always draws full-screen).
    if (qgetenv("DESKTOP_SESSION") == "LXDE-pi") {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Forcing windowed mode on LXDE-Pi");
        m_FullScreenFlag = 0;
    }
#endif

    // Check for validation errors/warnings and emit
    // signals for them, if appropriate
        bool ret = validateLaunch(testWindow);
        if (ret) {
            // Populate decoder-dependent properties.
            // Must be done after validateLaunch() since m_StreamConfig is finalized.
            ret = populateDecoderProperties(testWindow);
        }
        SDL_DestroyWindow(testWindow);
        if (!ret)
        {
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
            return false;
        }
    qDebug()<<"Session::ReconnectInit() after m_VideoCallbacks.capabilities"<<m_VideoCallbacks.capabilities;
    qDebug()<<"Session::ReconnectInit() after m_AudioCallbacks.capabilities"<<m_AudioCallbacks.capabilities;
    return true;
}
bool Session::initialize()
{
    qDebug()<<"Session::initialize";
#ifdef _WIN64
    QProcess p(NULL);
    p.start("cmd", QStringList() << "/c"
                                 << "wmic os get Caption");
    p.waitForStarted();
    p.waitForFinished();
    m_ClientOS = QString::fromLocal8Bit(p.readAllStandardOutput());
    p.close();
    m_ClientOS.remove(QRegularExpression("Caption"));
    m_ClientOS.remove(QRegularExpression("\\r"));
    m_ClientOS.remove(QRegularExpression("\\n"));
    m_ClientOS.remove(QRegularExpression("^ +\\s*"));
    m_ClientOS.remove(QRegularExpression("\\s* +$"));

    if (QSysInfo::currentCpuArchitecture() == "x86") {
        m_ClientArch = "x86";
    } else if (QSysInfo::currentCpuArchitecture() == "x86_64") {
        m_ClientArch = "x64";
    }
#endif
#ifdef TARGET_OS_MAC
    m_ClientOS = QSysInfo::prettyProductName();
#endif
    m_DecoderLock = 0;
    m_AudioDisabled = false;
    m_AudioMuted = false;
    m_DisplayOriginX = 0;
    m_DisplayOriginY = 0;
    m_UnexpectedTermination = true;  // Failure prior to streaming is unexpected
    m_InputHandler = nullptr;
    m_MouseEmulationRefCount = 0;
    m_FlushingWindowEventsRef = 0;
    m_PortTestResults = 0;
    m_OpusDecoder = nullptr;
    m_AudioRenderer = nullptr;
    m_AudioSampleCount = 0;
    m_DropAudioEndTime = 0;
    if (m_Preferences == nullptr)
    {
        m_Preferences = new StreamingPreferences(this);
        m_IsFullScreen = (m_Preferences->windowMode != StreamingPreferences::WM_WINDOWED ||
                        !WMUtils::isRunningDesktopEnvironment());
    }
    if (m_Computer)
    {
        m_isDualDisplay = m_Computer->isDualDisplay;
        m_Computer->currentGameId = 0;
    }
    if (!m_InputHandler)
    {
        qDebug() << "new SdlInputHandler";
        m_InputHandler = new SdlInputHandler(m_Preferences, &m_StreamConfig);
    }
    s_ActiveSession = this;
    LiInitializeStreamConfiguration(&m_StreamConfig);
    m_StreamConfig.width = m_Preferences->width;
    m_StreamConfig.height = m_Preferences->height;
    m_StreamConfig.fps = m_Preferences->fps;
    m_StreamConfig.width2 = m_Preferences->width2;
    m_StreamConfig.height2 = m_Preferences->height2;
    m_StreamConfig.fps2 = m_Preferences->fps2;
    m_StreamConfig.bitrate = m_Preferences->bitrateKbps;
    m_StreamConfig.hevcBitratePercentageMultiplier = 75;


#ifndef STEAM_LINK
    // Enable audio encryption as long as we're not on Steam Link.
    // That hardware can hardly handle Opus decoding at all.
    m_StreamConfig.encryptionFlags = ENCFLG_AUDIO;
#endif
    RAND_bytes(reinterpret_cast<unsigned char*>(m_StreamConfig.remoteInputAesKey),
               sizeof(m_StreamConfig.remoteInputAesKey));

    // Only the first 4 bytes are populated in the RI key IV
    RAND_bytes(reinterpret_cast<unsigned char*>(m_StreamConfig.remoteInputAesIv), 4);

    switch (m_Preferences->audioConfig) {
    case StreamingPreferences::AC_STEREO:
        m_StreamConfig.audioConfiguration = AUDIO_CONFIGURATION_STEREO;
        break;
    case StreamingPreferences::AC_51_SURROUND:
        m_StreamConfig.audioConfiguration = AUDIO_CONFIGURATION_51_SURROUND;
        break;
    case StreamingPreferences::AC_71_SURROUND:
        m_StreamConfig.audioConfiguration = AUDIO_CONFIGURATION_71_SURROUND;
        break;
    }
    int nCount = m_isDualDisplay ? 2 : 1;
    SetStremNum(nCount);
    SetConnectInfo(m_Preferences,m_StreamConfig,m_VideoCallbacks,m_AudioCallbacks,m_Computer.get(), m_App, m_ClientUUid,
                    m_PrivateKeyStr,m_InputHandler->getAttachedGamepadMask(),m_AudioDisabled,
                    m_HttpJsonStr, m_ClientOS, m_ClientArch);
    return ReconnectInit();

}

void Session::InitWebConfig() {
  InitGleamSettingWebView();
  g_pSettingWebView->SetSession(this);
  if (nullptr == g_pConfigPopup) {
    g_pConfigPopup = new PopupWindow();
    g_pConfigPopup->init();

    g_pConfigPopup->setSession(this);
  }
  InitHelpWebView();
}

void Session::QtWebViewClose() {
  if (g_pConfigPopup) {
    g_pConfigPopup->hide();
  }
}

void Session::emitLaunchWarning(QString text) {
    qDebug()<<" Session::emitLaunchWarning ";
    return ;
  // Emit the warning to the UI
  emit displayLaunchWarning(text);

  // Wait a little bit so the user can actually read what we just said.
  // This wait is a little longer than the actual toast timeout (3 seconds)
  // to allow it to transition off the screen before continuing.
  uint32_t start = SDL_GetTicks();
  while (!SDL_TICKS_PASSED(SDL_GetTicks(), start + 3500)) {
    SDL_Delay(5);

    if (!m_ThreadedExec) {
      // Pump the UI loop while we wait if we're on the main thread
      QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
      QCoreApplication::sendPostedEvents();
    }
  }
}

bool Session::validateLaunch(SDL_Window *testWindow) {
  if (m_Computer && !m_Computer->isSupportedServerVersion) {
    /*emit displayLaunchError(tr("The version of GeForce Experience on %1 is not supported by this build of Gleam. "
                            "You must update Gleam to stream from %1.")
                            .arg(m_Computer->name));*/
    return false;
  }

  if (m_Preferences->absoluteMouseMode && !m_App.isAppCollectorGame) {
    emitLaunchWarning(tr("Your selection to enable remote desktop mouse mode may cause problems in games."));
  }

  if (m_Preferences->videoDecoderSelection == StreamingPreferences::VDS_FORCE_SOFTWARE) {
    if (m_Preferences->videoCodecConfig == StreamingPreferences::VCC_FORCE_HEVC_HDR) {
      emitLaunchWarning(tr("HDR is not supported with software decoding."));
      m_StreamConfig.enableHdr = false;
    } else {
      emitLaunchWarning(tr("Your settings selection to force software decoding may cause poor streaming performance."));
    }
  }

  if (m_Preferences->unsupportedFps && m_StreamConfig.fps > 60) {
    emitLaunchWarning(tr("Using unsupported FPS options may cause stuttering or lag."));

    if (m_Preferences->enableVsync) {
      emitLaunchWarning(tr("V-sync will be disabled when streaming at a higher frame rate than the display."));
    }
  }

  if (m_StreamConfig.supportsHevc) {
    bool hevcForced = m_Preferences->videoCodecConfig == StreamingPreferences::VCC_FORCE_HEVC ||
                      m_Preferences->videoCodecConfig == StreamingPreferences::VCC_FORCE_HEVC_HDR;

    if (m_Computer && m_Computer->maxLumaPixelsHEVC == 0) {
      if (hevcForced) {
        emitLaunchWarning(tr("Your host PC doesn't support encoding HEVC."));
      }

      // Gleam-common-c will handle this case already, but we want
      // to set this explicitly here so we can do our hardware acceleration
      // check below.
      m_StreamConfig.supportsHevc = false;
    } else if (m_Preferences->videoDecoderSelection ==
                StreamingPreferences::VDS_AUTO &&  // Force hardware decoding checked below
                hevcForced &&                        // Auto VCC is already checked in initialize()
                !isHardwareDecodeAvailable(testWindow, m_Preferences->videoDecoderSelection, VIDEO_FORMAT_H265,
                                          m_StreamConfig.width, m_StreamConfig.height, m_StreamConfig.fps)) {
      emitLaunchWarning(
        tr("Using software decoding due to your selection to force HEVC without GPU support. "
           "This may cause poor streaming performance."));
    }
  }

  if (!m_StreamConfig.supportsHevc && m_Preferences->videoDecoderSelection == StreamingPreferences::VDS_AUTO &&
      !isHardwareDecodeAvailable(testWindow, m_Preferences->videoDecoderSelection, VIDEO_FORMAT_H264,
                                 m_StreamConfig.width, m_StreamConfig.height, m_StreamConfig.fps)) {
    if (m_Preferences->videoCodecConfig == StreamingPreferences::VCC_FORCE_H264) {
      emitLaunchWarning(
        tr("Using software decoding due to your selection to force H.264 without GPU support. "
           "This may cause poor streaming performance."));
    } else {
      if (m_Computer && m_Computer->maxLumaPixelsHEVC == 0 &&
          isHardwareDecodeAvailable(testWindow, m_Preferences->videoDecoderSelection, VIDEO_FORMAT_H265,
                                    m_StreamConfig.width, m_StreamConfig.height, m_StreamConfig.fps)) {
        emitLaunchWarning(
          tr("Your host PC and client PC don't support the same video codecs. This may cause "
             "poor streaming performance."));
      } else {
        // emitLaunchWarning(tr("Your client GPU doesn't support H.264 decoding. This may cause poor streaming
        // performance."));
      }
    }
  }

  if (m_StreamConfig.enableHdr) {
    // Turn HDR back off unless all criteria are met.
    m_StreamConfig.enableHdr = false;

    // Check that the server GPU supports HDR
    // if (!(m_Computer && m_Computer->serverCodecModeSupport & 0x200)) {
    if (m_Computer && !(m_Computer->serverCodecModeSupport & 0x200)) {
      emitLaunchWarning(tr("Your host PC doesn't support HDR streaming."));
    } else if (!isHardwareDecodeAvailable(testWindow, m_Preferences->videoDecoderSelection, VIDEO_FORMAT_H265_MAIN10,
                                          m_StreamConfig.width, m_StreamConfig.height, m_StreamConfig.fps)) {
      emitLaunchWarning(tr("This PC's GPU doesn't support HEVC Main10 decoding for HDR streaming."));
    } else {
      // TODO: Also validate display capabilites

      // Validation successful so HDR is good to go
      m_StreamConfig.enableHdr = true;
    }
  }

  if (m_StreamConfig.width >= 3840 && m_Computer != nullptr) {
    // Only allow 4K on GFE 3.x+
    if (m_Computer->gfeVersion.isEmpty() || m_Computer->gfeVersion.startsWith("2.")) {
      emitLaunchWarning(tr("GeForce Experience 3.0 or higher is required for 4K streaming."));

      m_StreamConfig.width = 1920;
      m_StreamConfig.height = 1080;
    }
  }

  // Test if audio works at the specified audio configuration
  bool audioTestPassed = testAudio(m_StreamConfig.audioConfiguration);

  // Gracefully degrade to stereo if surround sound doesn't work
  if (!audioTestPassed && CHANNEL_COUNT_FROM_AUDIO_CONFIGURATION(m_StreamConfig.audioConfiguration) > 2) {
    audioTestPassed = testAudio(AUDIO_CONFIGURATION_STEREO);
    if (audioTestPassed) {
      m_StreamConfig.audioConfiguration = AUDIO_CONFIGURATION_STEREO;
      emitLaunchWarning(tr("Your selected surround sound setting is not supported by the current audio device."));
    }
  }

  // If nothing worked, warn the user that audio will not work
  m_AudioDisabled = !audioTestPassed;
  if (m_AudioDisabled) {
    emitLaunchWarning(tr("Failed to open audio device. Audio will be unavailable during this session."));
  }

  // Check for unmapped gamepads
  if (!SdlInputHandler::getUnmappedGamepads().isEmpty()) {
    emitLaunchWarning(
      tr("An attached gamepad has no mapping and won't be usable. Visit the Gleam help to resolve this."));
  }

  // NVENC will fail to initialize when any dimension exceeds 4096 using:
  // - H.264 on all versions of NVENC
  // - HEVC prior to Pascal
  //
  // However, if we aren't using Nvidia hosting software, don't assume anything about
  // encoding capabilities by using HEVC Main 10 support. It will likely be wrong.

  if (m_Computer && (m_StreamConfig.width > 4096 || m_StreamConfig.height > 4096) && m_Computer->isNvidiaServerSoftware) {
    // Pascal added support for 8K HEVC encoding support. Maxwell 2 could encode HEVC but only up to 4K.
    // We can't directly identify Pascal, but we can look for HEVC Main10 which was added in the same generation.
    if (m_Computer->maxLumaPixelsHEVC == 0 || !(m_Computer->serverCodecModeSupport & 0x200)) {
      emit displayLaunchError(tr("Your host PC's GPU doesn't support streaming video resolutions over 4K."));
      return false;
    } else if (!m_StreamConfig.supportsHevc) {
      emit displayLaunchError(tr("Video resolutions over 4K are only supported by the HEVC codec."));
      return false;
    }
  }

  if (m_Preferences->videoDecoderSelection == StreamingPreferences::VDS_FORCE_HARDWARE &&
      !m_StreamConfig.enableHdr &&  // HEVC Main10 was already checked for hardware decode support above
      !isHardwareDecodeAvailable(testWindow, m_Preferences->videoDecoderSelection,
                                 m_StreamConfig.supportsHevc ? VIDEO_FORMAT_H265 : VIDEO_FORMAT_H264,
                                 m_StreamConfig.width, m_StreamConfig.height, m_StreamConfig.fps)) {
    if (m_Preferences->videoCodecConfig == StreamingPreferences::VCC_AUTO) {
      emit displayLaunchError(
        tr("Your selection to force hardware decoding cannot be satisfied due to missing "
           "hardware decoding support on this PC's GPU."));
    } else {
      emit displayLaunchError(
        tr("Your codec selection and force hardware decoding setting are not compatible. "
           "This PC's GPU lacks support for decoding your chosen codec."));
    }

    // Fail the launch, because we won't manage to get a decoder for the actual stream
    return false;
  }

  return true;
}
class MetricsReportTask : public QThread {
  public:
  MetricsReportTask(Session *session) : QThread(nullptr), m_Session(session) {
    setObjectName("MetricsReportTask");
    m_bRun = true;
  }
  void run() override {
    while (m_bRun) {
      QThread::msleep(10000);
      float metrics[32];  // first two means metrics count per stream,each stream now takes 11 items
      memset(metrics, 0, sizeof(float) * 30);
      int size1 = 0, size2 = 0;

      {
          if (m_Session->m_VideoDecoder) {
              size1 = m_Session->m_VideoDecoder->collectMetrics(&metrics[2], 15);
          }
          if (m_Session->m_VideoDecoder2) {
              size2 = m_Session->m_VideoDecoder2->collectMetrics(&metrics[2 + size1], 15);
          }
      }
      metrics[0] = (float)size1;
      metrics[1] = (float)size2;
      LiSendMetricsAndLogs((unsigned char *)metrics, (2 + size1 + size2) * sizeof(float));
    }
    qDebug() << "MetricsReportTask end";
  }
  void Stop() { m_bRun = false; }
  void Restart() { m_bRun = true; }

  private:
  bool m_bRun;
  Session *m_Session;
};




DeferredSessionCleanupTask::DeferredSessionCleanupTask()
{
    qDebug()<<"DeferredSessionCleanupTask::DeferredSessionCleanupTask";
}

DeferredSessionCleanupTask::~DeferredSessionCleanupTask()
{

    qDebug()<<"~DeferredSessionCleanupTask()";
    Session::s_ActiveSessionSemaphore.release();
}
void DeferredSessionCleanupTask::run()
{

    //
    // Only quit the running app if our session terminated gracefully

    {
        // The video decoder must already be destroyed, since it could
        // try to interact with APIs that can only be called between
        // LiStartConnection() and LiStopConnection().

        // Perform a best-effort app quit
        if (SessionSingleton::getInstance()->m_Computer != nullptr)
        {
            NvHTTP http(SessionSingleton::getInstance()->m_Computer.get());

            // Logging is already done inside NvHTTP
            try {
                http.quitApp();
                qInfo()<<"http.quitApp() ";
            }
            catch (const GfeHttpResponseException& e)
            {
                qWarning()<<"http.quitApp() GfeHttpResponseException"<<e.toQString();
            }
            catch (const QtNetworkReplyException& e)
            {
                qWarning()<<"http.quitApp() QtNetworkReplyException"<<e.toQString();
            }
        }
    }
    // Finish cleanup of the connection state
    int num = SessionSingleton::getInstance()->IsDualDisplay()?2 : 1;
    LiStopConnection(num);
    qDebug()<<" DeferredSessionCleanupTask num = "<<num;
}


void Session::getWindowDimensions(SDL_Window *window, int &x, int &y, int &width, int &height, int &displayToSkip) {
  int displayIndex = 0;

  if (window != nullptr) {
    displayIndex = SDL_GetWindowDisplayIndex(window);
      qDebug()<<"displayIndex = "<<displayIndex;
    //SDL_assert(displayIndex >= 0);
  }
  // Create our window on the same display that Qt's UI
  // was being displayed on.
  else {

    for (int i = 0; i < SDL_GetNumVideoDisplays(); i++) {
      SDL_Rect displayBounds;

      if (i == displayToSkip) continue;

      if (SDL_GetDisplayBounds(i, &displayBounds) == 0) {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "SDL found matching display %d", i);
        displayIndex = i;
        displayToSkip = i;
        break;
      } else {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SDL_GetDisplayBounds(%d) failed: %s", i, SDL_GetError());
      }
    }
  }

  SDL_Rect usableBounds;
  if (SDL_GetDisplayUsableBounds(displayIndex, &usableBounds) == 0) {
    // Don't use more than 80% of the display to leave room for system UI
    // and ensure the target size is not odd (otherwise one of the sides
    // of the image will have a one-pixel black bar next to it).
    SDL_Rect src, dst;
    src.x = src.y = dst.x = dst.y = 0;
    src.w = m_StreamConfig.width == 0? 1920 : m_StreamConfig.width;
    src.h = m_StreamConfig.height == 0? 1080 : m_StreamConfig.height;
    dst.w = ((int)SDL_ceilf((std::min)(usableBounds.w, usableBounds.h) * 0.95f) & ~0x1);
    dst.h = ((int)SDL_ceilf(dst.w * 9 / 16.0f) & ~0x1);

    // Scale the window size while preserving aspect ratio
    StreamUtils::scaleSourceToDestinationSurface(&src, &dst);

    // If the stream window can fit within the usable drawing area with 1:1
    // scaling, do that rather than filling the screen.
    if (src.w < dst.w && src.h < dst.h) {
      width = src.w;
      height = src.h;
    } else {
      width = dst.w;
      height = dst.h;
    }
  } else {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_GetDisplayUsableBounds() failed: %s", SDL_GetError());

    width = m_StreamConfig.width == 0? 1920 : m_StreamConfig.width;
    height = m_StreamConfig.height == 0? 1080 : m_StreamConfig.height;
  }

  x = y = SDL_WINDOWPOS_CENTERED_DISPLAY(displayIndex);
}

void Session::updateOptimalWindowDisplayMode(SDL_Window *window) {
  SDL_DisplayMode desktopMode, bestMode, mode;
  // int displayIndex = SDL_GetWindowDisplayIndex(m_Window);
  int displayIndex = SDL_GetWindowDisplayIndex(window);

  // Try the current display mode first. On macOS, this will be the normal
  // scaled desktop resolution setting.
  if (SDL_GetDesktopDisplayMode(displayIndex, &desktopMode) == 0) {
    // If this doesn't fit the selected resolution, use the native
    // resolution of the panel (unscaled).
    if (desktopMode.w < m_ActiveVideoWidth || desktopMode.h < m_ActiveVideoHeight) {
      if (!StreamUtils::getNativeDesktopMode(displayIndex, &desktopMode)) {
        return;
      }
    }
  } else {
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SDL_GetDesktopDisplayMode() failed: %s", SDL_GetError());
    return;
  }

  // Start with the native desktop resolution and try to find
  // the highest refresh rate that our stream FPS evenly divides.
  bestMode = desktopMode;
  bestMode.refresh_rate = 0;
  for (int i = 0; i < SDL_GetNumDisplayModes(displayIndex); i++) {
    if (SDL_GetDisplayMode(displayIndex, i, &mode) == 0) {
      if (mode.w == desktopMode.w && mode.h == desktopMode.h && mode.refresh_rate % m_StreamConfig.fps == 0) {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Found display mode with desktop resolution: %dx%dx%d", mode.w,
                    mode.h, mode.refresh_rate);
        if (mode.refresh_rate > bestMode.refresh_rate) {
          bestMode = mode;
        }
      }
    }
  }

  // If we didn't find a mode that matched the current resolution and
  // had a high enough refresh rate, start looking for lower resolution
  // modes that can meet the required refresh rate and minimum video
  // resolution. We will also try to pick a display mode that matches
  // aspect ratio closest to the video stream.
  if (bestMode.refresh_rate == 0) {
    float bestModeAspectRatio = 0;
    float videoAspectRatio = (float)m_ActiveVideoWidth / (float)m_ActiveVideoHeight;
    for (int i = 0; i < SDL_GetNumDisplayModes(displayIndex); i++) {
      if (SDL_GetDisplayMode(displayIndex, i, &mode) == 0) {
        float modeAspectRatio = (float)mode.w / (float)mode.h;
        if (mode.w >= m_ActiveVideoWidth && mode.h >= m_ActiveVideoHeight &&
            mode.refresh_rate % m_StreamConfig.fps == 0) {
          SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Found display mode with video resolution: %dx%dx%d", mode.w,
                      mode.h, mode.refresh_rate);
          if (mode.refresh_rate >= bestMode.refresh_rate &&
              (bestModeAspectRatio == 0 ||
               fabs(videoAspectRatio - modeAspectRatio) <= fabs(videoAspectRatio - bestModeAspectRatio))) {
            bestMode = mode;
            bestModeAspectRatio = modeAspectRatio;
          }
        }
      }
    }
  }

  if (bestMode.refresh_rate == 0) {
    // We may find no match if the user has moved a 120 FPS
    // stream onto a 60 Hz monitor (since no refresh rate can
    // divide our FPS setting). We'll stick to the default in
    // this case.
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "No matching display mode found; using desktop mode");
    bestMode = desktopMode;
  }

  // if ((SDL_GetWindowFlags(m_Window) & SDL_WINDOW_FULLSCREEN_DESKTOP) == SDL_WINDOW_FULLSCREEN) {
  if ((SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN_DESKTOP) == SDL_WINDOW_FULLSCREEN) {
    // Only print when the window is actually in full-screen exclusive mode,
    // otherwise we're not actually using the mode we've set here
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Chosen best display mode: %dx%dx%d", bestMode.w, bestMode.h,
                bestMode.refresh_rate);
  }

  // SDL_SetWindowDisplayMode(m_Window, &bestMode);
  SDL_SetWindowDisplayMode(window, &bestMode);
}

SDL_Window * Session::getActiveSDLWindow() {
    return m_InputHandler->getActiveSDLWindow();
}

void Session::toggleFullscreen(int index) {
    SDL_Window *targetWindow = index == 1 ? m_Window : m_Window2;
    if (targetWindow == nullptr) {
        return;
    }


  bool fullScreen = !(SDL_GetWindowFlags(targetWindow) & m_FullScreenFlag);
  // #ifdef Q_OS_WIN32 Mac has the same issues.
  //  Destroy the video decoder before toggling full-screen because D3D9 can try
  //  to put the window back into full-screen before we've managed to destroy
  //  the renderer. This leads to excessive flickering and can cause the window
  //  decorations to get messed up as SDL and D3D9 fight over the window style.  
  SDL_AtomicLock(&m_DecoderLock);
  if (targetWindow == m_Window) {
    delete m_VideoDecoder;
    m_VideoDecoder = nullptr;
  } else if (targetWindow == m_Window2){
    delete m_VideoDecoder2;
    m_VideoDecoder2 = nullptr;
  }
  SDL_AtomicUnlock(&m_DecoderLock);
  // #endif

  // Actually enter/leave fullscreen
  SDL_SetWindowFullscreen(targetWindow, fullScreen ? m_FullScreenFlag : 0);
#ifdef Q_OS_DARWIN
  // SDL on macOS has a bug that causes the window size to be reset to crazy
  // large dimensions when exiting out of true fullscreen mode. We can work
  // around the issue by manually resetting the position and size here.
  if (!fullScreen && m_FullScreenFlag == SDL_WINDOW_FULLSCREEN) {
    int x, y, defaultWidth, defaultHeight;
    int dp = -1;
    getWindowDimensions(targetWindow, x, y, defaultWidth, defaultHeight, dp);
    SDL_SetWindowSize(targetWindow, defaultWidth, defaultHeight);
    SDL_SetWindowPosition(targetWindow, x, y);
  }
#endif
  int width, height, minWidth, minHeight;
  if (!fullScreen) {
    SDL_GetWindowSize(targetWindow, &width, &height);
    SDL_GetWindowMinimumSize(targetWindow, &minWidth, &minHeight);
    SDL_SetWindowMinimumSize(targetWindow, minWidth, minHeight);
    SDL_SetWindowSize(targetWindow, width, height);
  }
  SDL_RaiseWindow(targetWindow);
  // Input handler might need to start/stop keyboard grab after changing modes
  m_InputHandler->updateKeyboardGrabState();

  // Input handler might need stop/stop mouse grab after changing modes
  m_InputHandler->updatePointerRegionLock();
}

void Session::toggleMinimized() {
  bool minimized = (SDL_GetWindowFlags(m_Window) & SDL_WINDOW_MINIMIZED);
  qDebug() << "minimized = " << minimized;
  if (minimized == false) {
#ifdef _WIN64
    RegisterHotkey();
#endif
  }

  if (minimized) {
    SDL_RestoreWindow(m_Window);
    if (m_isDualDisplay) {
      SDL_RestoreWindow(m_Window2);
    }
  } else {
    SDL_SetWindowFullscreen(m_Window, SDL_FALSE);
    SDL_MinimizeWindow(m_Window);
    if (m_isDualDisplay) {
      SDL_SetWindowFullscreen(m_Window2, SDL_FALSE);
      SDL_MinimizeWindow(m_Window2);
    }
  }
#ifdef Q_OS_DARWIN
  // SDL on macOS has a bug that causes the window size to be reset to crazy
  // large dimensions when exiting out of true fullscreen mode. We can work
  // around the issue by manually resetting the position and size here.
//    if (!fullScreen && m_FullScreenFlag == SDL_WINDOW_FULLSCREEN) {
//        int x, y, width, height;
//        int dp = -1;
//        getWindowDimensions(m_Window, x, y, width, height, dp);
//        SDL_SetWindowSize(m_Window, width, height);
//        SDL_SetWindowPosition(m_Window, x, y);
//    }
#endif

  // Input handler might need to start/stop keyboard grab after changing modes
  m_InputHandler->updateKeyboardGrabState();

  // Input handler might need stop/stop mouse grab after changing modes
  m_InputHandler->updatePointerRegionLock();
}

void Session::notifyMouseEmulationMode(bool enabled) {
  m_MouseEmulationRefCount += enabled ? 1 : -1;
  SDL_assert(m_MouseEmulationRefCount >= 0);

  // We re-use the status update overlay for mouse mode notification
  if (m_MouseEmulationRefCount > 0) {
    strcpy(m_OverlayManager.getOverlayText(Overlay::OverlayStatusUpdate),
           "Gamepad mouse mode active\nLong press Start to deactivate");
    m_OverlayManager.setOverlayTextUpdated(Overlay::OverlayStatusUpdate);
    m_OverlayManager.setOverlayState(Overlay::OverlayStatusUpdate, true);
  } else {
    m_OverlayManager.setOverlayState(Overlay::OverlayStatusUpdate, false);
  }
}

// Called in a non-main thread

void Session::flushWindowEvents() {
  // Pump events to ensure all pending OS events are posted
  SDL_PumpEvents();

  // Insert a barrier to discard any additional window events.
  // We don't use SDL_FlushEvent() here because it could cause
  // important events to be lost.
  m_FlushingWindowEventsRef++;

  // This event will cause us to set m_FlushingWindowEvents back to false.
  SDL_Event flushEvent = {};
  flushEvent.type = SDL_USEREVENT;
  flushEvent.user.code = SDL_CODE_FLUSH_WINDOW_EVENT_BARRIER;
  SDL_PushEvent(&flushEvent);
}

class ExecThread : public QThread {
  public:
  ExecThread(Session *session) : QThread(nullptr), m_Session(session) { setObjectName("Session Exec"); }

  void run() override { m_Session->execInternal(); }

  Session *m_Session;
};

void Session::exec(int displayOriginX, int displayOriginY) {
  m_DisplayOriginX = displayOriginX;
  m_DisplayOriginY = displayOriginY;

  // Use a separate thread for the streaming session on X11 or Wayland
  // to ensure we don't stomp on Qt's GL context. This breaks when using
  // the Qt EGLFS backend, so we will restrict this to X11
  m_ThreadedExec = WMUtils::isRunningX11() || WMUtils::isRunningWayland();      //true  not windows mac system

  if (m_ThreadedExec) {
    // Run the streaming session on a separate thread for Linux/BSD
    ExecThread execThread(this);
    execThread.start();

    // Until the SDL streaming window is created, we should continue
    // to update the Qt UI to allow warning messages to display and
    // make sure that the Qt window can hide itself.
    while (!execThread.wait(10) && m_Window == nullptr) {
      QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
      QCoreApplication::sendPostedEvents();
    }
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    QCoreApplication::sendPostedEvents();

    // SDL is in charge now. Wait until the streaming thread exits
    // to further update the Qt window.
    execThread.wait();
  } else {
    // Run the streaming session on the main thread for Windows and macOS
    execInternal();
  }
}



void Session::DeleteGlobalSDLWindows()
{
    if (m_Window)
    {
        SDL_DestroyWindow(m_Window);
        m_Window = nullptr;
        qDebug()<<g_Window<<"DeleteGlobalSDLWindows "<<m_Window;
    }

    if (m_Window2) {
        SDL_DestroyWindow(m_Window2);
        m_Window2 = nullptr;
    }

    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

void Session::InitMetricReport()
{
    if (m_metricsReportTask == nullptr)
    {
        qDebug() << " new MetricsReportTask ";
        m_metricsReportTask = new MetricsReportTask(this);
        m_metricsReportTask->start();

    }
    else
    {
        m_metricsReportTask->Restart();
        m_metricsReportTask->start();
    }
}
void Session::ReInitInfor()
{
    if(m_InputHandler)
    {
        m_InputHandler->raiseAllKeys();
    }
    m_InputHandler->setCaptureActive(false);
}
void Session::SDLInitSubSystem()
{
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "SDL_InitSubSystem(SDL_INIT_VIDEO) failed: %s",
                     SDL_GetError());
        delete m_InputHandler;
        m_InputHandler = nullptr;
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        QThreadPool::globalInstance()->start(new DeferredSessionCleanupTask());
        return ;
    }
}
void Session::connectfailed()
{
    delete m_InputHandler;
    m_InputHandler = nullptr;
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    QThreadPool::globalInstance()->start(new DeferredSessionCleanupTask());
}


void renderBlackBackground(SDL_Window *win) {
    SDL_Renderer* renderer = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    // Clear screen with black color
    SDL_RenderClear(renderer);
    // Update screen
    SDL_RenderPresent(renderer);
    SDL_DestroyRenderer(renderer);
}

/*
typedef enum DXGI_MODE_ROTATION
{
    DXGI_MODE_ROTATION_UNSPECIFIED  = 0,
    DXGI_MODE_ROTATION_IDENTITY     = 1,
    DXGI_MODE_ROTATION_ROTATE90     = 2,
    DXGI_MODE_ROTATION_ROTATE180    = 3,
    DXGI_MODE_ROTATION_ROTATE270    = 4
} DXGI_MODE_ROTATION;
*/
void Session::InitSdlWindow()
{
    qDebug() << "InitSdlWindow" << m_metricsReportTask;
    // Complete initialization in this deferred context to avoid
    // calling expensive functions in the constructor (during the
    // process of loading the StreamSegue).
    //
    // NB: This initializes the SDL video subsystem, so it must be
    // called on the main thread.
    // Start rich presence to indicate we're in game

    int x, y, width, height;
    int display_index = -1;
    qDebug() << "m_Window = " << m_Window;
    getWindowDimensions(m_Window, x, y, width, height, display_index);


#ifdef STEAM_LINK
    // We need a little delay before creating the window or we will trigger some kind
    // of graphics driver bug on Steam Link that causes a jagged overlay to appear in
    // the top right corner randomly.
    SDL_Delay(500);
#endif

    // Request at least 8 bits per color for GL
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);

    // We always want a resizable window with High DPI enabled
    Uint32 defaultWindowFlags = SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE;

    double r = (double)height / (double)width;

    const int margin_x = 20;
    int margin_y = 200;
    const int inter_margin = 2;
    if (m_isDualDisplay) {
        int scr_w = 1920;
        int scr_h = 1080;
        {
            SDL_Rect rect;
            SDL_GetDisplayBounds(0, &rect);
            scr_w = rect.w;
            scr_h = rect.h;
        }
        x = margin_x;
        width = (scr_w - 2 * margin_x- inter_margin) / 2;
        //old code use: height = scr_h - 2 * margin_y;
        height = int(width * r+0.5);
        y = margin_y = (scr_h - height) / 2;
    }

    if (!m_Window)
    {
        qDebug() << "create m_Windows";
        const char* title1 = "GWS Client-1 (Ctrl+Alt+Shift+? to open the help page)";
        m_Window = SDL_CreateWindow(title1,
            x,
            y,
            width,
            height,
            defaultWindowFlags | StreamUtils::getPlatformWindowFlags());
        qInfo()<<"create m_Window = " <<m_Window;
        if (!m_Window) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "SDL_CreateWindow() failed with platform flags: %s",
                SDL_GetError());

            m_Window = SDL_CreateWindow(title1,
                x,
                y,
                width,
                height,
                defaultWindowFlags);
        }
        SDL_SetWindowMinimumSize(m_Window, width, height);
        renderBlackBackground(m_Window);
    }

    if (m_isDualDisplay)
    {
        //move first window
        if (m_Window && !m_IsFullScreen)
        {
            SDL_SetWindowPosition(m_Window, x, margin_y);
            if (GleamCore::IsVerticalRotation(m_WindowRotation)) {
                std::swap(width, height);
            }
            SDL_SetWindowMinimumSize(m_Window, width, height);
            SDL_SetWindowSize(m_Window, width, height);
        }

        if (!m_Window2)
        {
            double r2 = (double)m_StreamConfig.height2 / (double)m_StreamConfig.width2;
            if (GleamCore::IsVerticalRotation(m_Window2Rotation)) {
                r2 = 1 / r2;
            }
            if (GleamCore::IsVerticalRotation(m_WindowRotation) != GleamCore::IsVerticalRotation(m_Window2Rotation)) {
                std::swap(width, height);
            }
            height = width * r2;

            const char* title2 = "GWS Client-2 (Ctrl+Alt+Shift+? to open the help page)";
            if (m_IsFullScreen) {
                getWindowDimensions(m_Window2, x, y, width, height, display_index);
            }
            else {
                x = margin_x + (std::max)(width, height) + inter_margin;
                y = margin_y;
            }

            m_Window2 = SDL_CreateWindow(title2,
                x,
                y,
                width,
                height,
                defaultWindowFlags | StreamUtils::getPlatformWindowFlags());
            qInfo()<<"create m_Window2 = " <<m_Window2;

            if (!m_Window2) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "SDL_CreateWindow() failed with platform flags: %s",
                    SDL_GetError());

                m_Window2 = SDL_CreateWindow(title2,
                    x,
                    y,
                    width,
                    height,
                    defaultWindowFlags);
            }
#ifdef Q_OS_DARWIN
            SDL_SetWindowPosition(m_Window2, x, y);
            SDL_SetWindowMinimumSize(m_Window2, width, height);
            SDL_SetWindowSize(m_Window2, width, height);
#else
            SDL_SetWindowMinimumSize(m_Window2, width, height);
#endif
            renderBlackBackground(m_Window2);
        }

        m_OverlayManager2.setOverlayState(Overlay::OverlayDebug,
            Session::get()->getOverlayManager(m_Window).isOverlayEnabled(Overlay::OverlayDebug));
    }

    if (!m_Window || (m_isDualDisplay && (!m_Window2))) {
        if (m_Window)
        {
            SDL_DestroyWindow(m_Window);
        }
        if (m_Window2)
        {
            SDL_DestroyWindow(m_Window2);
        }
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "SDL_CreateWindow() failed: %s",
            SDL_GetError());

        delete m_InputHandler;
        m_InputHandler = nullptr;
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        QThreadPool::globalInstance()->start(new DeferredSessionCleanupTask());
        return;
    }

    m_winID1 = SDL_GetWindowID(m_Window);
    m_winID2 = -1;
    if (m_isDualDisplay)
        m_winID2 = SDL_GetWindowID(m_Window2);

    m_InputHandler->setWindow(m_Window, m_Window2);
#ifndef Q_OS_DARWIN
    QSvgRenderer svgIconRenderer(QString(":/res/Gleam.svg"));
    QImage svgImage(ICON_SIZE, ICON_SIZE, QImage::Format_RGBA8888);
    svgImage.fill(0);

    QPainter svgPainter(&svgImage);
    svgIconRenderer.render(&svgPainter);
    m_iconSurface = SDL_CreateRGBSurfaceWithFormatFrom((void*)svgImage.constBits(),
        svgImage.width(),
        svgImage.height(),
        32,
        4 * svgImage.width(),
        SDL_PIXELFORMAT_RGBA32);
    m_iconSurface2 = nullptr;
    if (m_isDualDisplay)
    {
        m_iconSurface2 = SDL_CreateRGBSurfaceWithFormatFrom((void*)svgImage.constBits(),
            svgImage.width(),
            svgImage.height(),
            32,
            4 * svgImage.width(),
            SDL_PIXELFORMAT_RGBA32);
    }

    // Other platforms seem to preserve our Qt icon when creating a new window.
    //if (m_iconSurface != nullptr)
    {
        // This must be called before entering full-screen mode on Windows
        // or our icon will not persist when toggling to windowed mode
        SDL_Surface* icon = SDL_LoadBMP("Gleam_wix.png");
        if (icon)
        {
            SDL_SetWindowIcon(m_Window, icon);
        }
    }
    if (m_isDualDisplay /* && m_iconSurface2 != nullptr*/)
    {
        SDL_Surface* icon = SDL_LoadBMP("Gleam_wix.png");
        if (icon)
        {
            SDL_SetWindowIcon(m_Window2, icon);
        }
    }
#endif

    // Update the window display mode based on our current monitor
    // for if/when we enter full-screen mode.
    updateOptimalWindowDisplayMode(m_Window);
    if (m_isDualDisplay)
        updateOptimalWindowDisplayMode(m_Window2);

    // Enter full screen if requested
    if (m_IsFullScreen) {
        SDL_SetWindowFullscreen(m_Window, m_FullScreenFlag);
        if (m_isDualDisplay)
            SDL_SetWindowFullscreen(m_Window2, m_FullScreenFlag);

#ifdef Q_OS_WIN32
        SDL_SysWMinfo info;
        SDL_VERSION(&info.version);

        // Draw a black background again after entering full-screen to avoid visual artifacts
        // where the old window decorations were before.
        //
        // TODO: Remove when SDL does this itself
        if (SDL_GetWindowWMInfo(m_Window, &info) && info.subsystem == SDL_SYSWM_WINDOWS) {
            RECT clientRect;
            HBRUSH blackBrush;

            blackBrush = CreateSolidBrush(0);
            GetClientRect(info.info.win.window, &clientRect);
            FillRect(info.info.win.hdc, &clientRect, blackBrush);
            DeleteObject(blackBrush);
        }

        if (m_isDualDisplay) {
            if (SDL_GetWindowWMInfo(m_Window2, &info) && info.subsystem == SDL_SYSWM_WINDOWS) {
                RECT clientRect;
                HBRUSH blackBrush;

                blackBrush = CreateSolidBrush(0);
                GetClientRect(info.info.win.window, &clientRect);
                FillRect(info.info.win.hdc, &clientRect, blackBrush);
                DeleteObject(blackBrush);
            }
        }
#endif
    }

    // HACK: For Wayland, we wait until we get the first SDL_WINDOWEVENT_ENTER
    // event where it seems to work consistently on GNOME. For other platforms,
    // especially where SDL may call SDL_RecreateWindow(), we must only capture
    // after the decoder is created.
    if (strcmp(SDL_GetCurrentVideoDriver(), "wayland") == 0) {
        // Native Wayland: Capture on SDL_WINDOWEVENT_ENTER
        m_needsFirstEnterCapture = true;
    }
    else {
        // X11/XWayland: Capture after decoder creation
        m_needsPostDecoderCreationCapture = true;
    }

    // Stop text input. SDL enables it by default
    // when we initialize the video subsystem, but this
    // causes an IME popup when certain keys are held down
    // on macOS.
    SDL_StopTextInput();

    // Disable the screen saver if requested
    if (m_Preferences->keepAwake) {
        SDL_DisableScreenSaver();
    }

    // Hide Qt's fake mouse cursor on EGLFS systems
    if (QGuiApplication::platformName() == "eglfs") {
        QGuiApplication::setOverrideCursor(QCursor(Qt::BlankCursor));
    }

    // Set timer resolution to 1 ms on Windows for greater
    // sleep precision and more accurate callback timing.
    SDL_SetHint(SDL_HINT_TIMER_RESOLUTION, "1");

    m_currentDisplayIndex = SDL_GetWindowDisplayIndex(m_Window);
    m_currentDisplayIndex2 = -1;
    if (m_isDualDisplay)
        m_currentDisplayIndex2 = SDL_GetWindowDisplayIndex(m_Window2);
}

void Session::checkAutoCreateStreaming()
{
    ConnectionManager::I().lockStatus();
    const Status& status = ConnectionManager::I().getStatus();
    if (status.displayChosen.size() == 0 && status.HostDisplayList.size() == 1)
    {
        //UIStateMachine::I().push(MsgCode::NO_CODE, MsgType::Progress, "Starting streaming ...");
        ConnectionManager::I().createMessageTask(
            QString(
                "{\"ActionType\":\"ConfigChange\",\"DisplayChoose\":[{\"Name\":\""
                ).append(status.HostDisplayList[0].name).append("\"}]}"), "config", -3);
    }
    ConnectionManager::I().unlockStatus();
}

void Session::HideWindows2()
{
    SDL_AtomicLock(&m_DecoderLock);
    if (m_VideoDecoder2)
    {
        delete m_VideoDecoder2;
        m_VideoDecoder2 = nullptr;
    }
    SDL_AtomicUnlock(&m_DecoderLock);
    SDL_HideWindow(m_Window2);
    if (m_iconSurface2 != nullptr)
    {
        qInfo()<<"free m_iconSurface2";
        SDL_FreeSurface(m_iconSurface2);
        m_iconSurface2 = NULL;
    }
    if (m_Window2 != nullptr)
    {
        qInfo()<<"free m_Window2";
        SDL_DestroyWindow(m_Window2);
        m_Window2 = nullptr;
        m_Window2Rotation = static_cast<DXGI_MODE_ROTATION>(-1);
    }
}

void Session::UpdateConfigSize(const GleamCore::DisplayInfo disp) {
    bool resolutionChanged = false, rotationWindow = false, rotationChange = false;

    bool isMainScreen = (disp.x == 0 && disp.y == 0);
    int &configWidth = isMainScreen ? m_StreamConfig.width : m_StreamConfig.width2;
    int &configHeight = isMainScreen ? m_StreamConfig.height : m_StreamConfig.height2;

    int &preferencesWidth = isMainScreen ? m_Preferences->width : m_Preferences->width2;
    int &preferencesHeight = isMainScreen ? m_Preferences->height : m_Preferences->height2;

    DXGI_MODE_ROTATION &windowRotation = isMainScreen ? m_WindowRotation : m_Window2Rotation;

    int dispWidth = disp.width;
    int dispHeight = disp.height;

    if (GleamCore::IsVerticalRotation(disp.rotation)) {
        std::swap(dispWidth, dispHeight);
    }


    if (preferencesWidth == 0 && preferencesHeight == 0) {
        if (configWidth != dispWidth || configHeight != dispHeight) {
            resolutionChanged = true;
            configWidth = dispWidth;
            configHeight = dispHeight;
        }
    } else {
        int scaleWidth = dispWidth * configHeight / dispHeight;
        if (scaleWidth != configWidth) {
            resolutionChanged = true;
            configWidth = scaleWidth;
        }
    }

    if (windowRotation != disp.rotation) {
            if (GleamCore::IsVerticalRotation(windowRotation) != GleamCore::IsVerticalRotation(disp.rotation)) {
                rotationWindow = true;
            }
            rotationChange = true;
        windowRotation = disp.rotation;
    }

    SDL_Window* window = isMainScreen ? m_Window : m_Window2;
    int width, height;
    int minWidth;
    int minHeight;
    int streamWidth;
    int streamHeight;

    if (window != nullptr) {
        if (rotationWindow) {
            SDL_GetWindowSize(window, &width, &height);
            SDL_GetWindowMinimumSize(window, &minWidth, &minHeight);

            SDL_SetWindowMinimumSize(window, minHeight, minWidth);
            SDL_SetWindowSize(window, height, width);

            renderBlackBackground(window);
        }

        if (rotationChange || resolutionChanged) {
            streamWidth = configWidth;
            streamHeight = configHeight;
            SDL_GetWindowSize(window, &width, &height);
            SDL_GetWindowMinimumSize(window, &minWidth, &minHeight);

            if (streamWidth * minHeight != streamHeight * minWidth) {
                if (GleamCore::IsVerticalRotation(windowRotation)) {
                    minWidth = streamHeight * minHeight / (double)streamWidth;
                    width = streamHeight * height / (double)streamWidth;
                } else {
                    minHeight = streamHeight * minWidth / (double)streamWidth;
                    height = streamHeight * width / (double)streamWidth;
                }
            }

            SDL_SetWindowMinimumSize(window, minWidth, minHeight);
            SDL_SetWindowSize(window, width, height);
            renderBlackBackground(window);
        }
    }

    if (rotationChange || resolutionChanged) {
        SDL_Event event;
        event.type = SDL_RENDER_DEVICE_RESET;
        event.window.windowID = SDL_GetWindowID(window);
        SDL_PushEvent(&event);
    }
}

void SwapWindowPositions(SDL_Window* window1, SDL_Window* window2) {
    // 获取窗口1和窗口2的位置
    int x1, y1, x2, y2;
    SDL_GetWindowPosition(window1, &x1, &y1);
    SDL_GetWindowPosition(window2, &x2, &y2);

    // 交换窗口位置
    SDL_SetWindowPosition(window1, x2, y2);
    SDL_SetWindowPosition(window2, x1, y1);
}

void Session::OnDisplayConfigChanged()
{
    auto hostDispList = ConnectionManager::I().getHostDisplayList();
    std::pair<int,int> minXY = {0, 0};
    for (auto disp : hostDispList) {
        minXY.first = (std::min)(disp.x, minXY.first);
        minXY.second = (std::min)(disp.y, minXY.second);
    }
    auto dispConfigList = ConnectionManager::I().getChosenList();
    m_InputHandler->setDispConfigList(dispConfigList, minXY);

    QList<StreamInfoReporter::DisplayConfig> displayConfig;
    for (auto& d : dispConfigList)
    {
        qDebug() << "OnDisplayConfigChanged dispConfigList " << d.shortName;
        StreamInfoReporter::DisplayConfig cfg;
        cfg.name = d.name;
        cfg.width = d.width;
        cfg.height = d.height;
        if (d.x != 0 || d.y != 0) {
            cfg.streamWidth = m_StreamConfig.width2;
            cfg.streamHeight = m_StreamConfig.height2;
            cfg.fps = m_StreamConfig.fps2;
        } else {
            cfg.streamWidth = m_StreamConfig.width;
            cfg.streamHeight = m_StreamConfig.height;
            cfg.fps = m_StreamConfig.fps;
        }

        if (cfg.streamWidth == 0 && cfg.streamHeight == 0) {
            cfg.streamWidth = cfg.width;
            cfg.streamHeight = cfg.height;
        } else {
            cfg.streamWidth = cfg.width * cfg.streamHeight / cfg.height;
        }

        cfg.bitrate = m_StreamConfig.bitrate;
        cfg.rotation = d.rotation;
        displayConfig.append(cfg);
    }
    gStreamInfoReporter.SendConfig(displayConfig);


    // cnt / rotation / resolution may not change together
    bool isDualDisplay = (dispConfigList.size() >= 2);
    bool cntChange = false;

    bool swapWindow = false;
    bool fullscreen = SDL_GetWindowFlags(m_Window) & m_FullScreenFlag;
    for (auto &disp : dispConfigList) {
        UpdateConfigSize(disp);
        if (m_Window2 == nullptr && (disp.x != 0 || disp.y != 0) && disp.x < 0 && isDualDisplay && !fullscreen) {
            swapWindow = true;
        } else if (m_Window2 != nullptr) {
            int x1, y1, x2, y2;
            SDL_GetWindowPosition(m_Window, &x1, &y1);
            SDL_GetWindowPosition(m_Window2, &x2, &y2);
            if ((disp.x != 0 || disp.y != 0) && (disp.x < 0 == x2 > x1)) {
                swapWindow = true;
            }
        }
    }

    if (m_isDualDisplay != isDualDisplay)
    {
        m_isDualDisplay = isDualDisplay;
        cntChange = true;
    }

    if (cntChange) {
        if (isDualDisplay)
        {
            LiStartSecondVideoStream();
            InitSdlWindow();
            if (swapWindow) {
                SwapWindowPositions(m_Window, m_Window2);
            }
        }
        else
        {
            CloseVideoStreamAndWindow(2);
        }
    } else if (swapWindow) {
        SwapWindowPositions(m_Window, m_Window2);
    }
}

void Session::CheckDisplayStream(int nCount)
{
    SetStremNum(nCount);
    if(m_isDualDisplay == false && 2 == nCount )
    {
        m_ConnectSucess = false;
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,"Check Dua display");
        m_isDualDisplay = true;
        ResetVideoDecoder();
        NotifyDisConnect();
    }
    else if(m_isDualDisplay == true && 1 == nCount )
    {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,"Check Single Display");
        m_InputHandler->raiseAllKeys();
        m_isDualDisplay = false;
        StopStreamByNumber(2);
    }
}

void Session::checkNetworkAndReconnect()
{
    // This network connectivity check is not effective
    QNetworkRequest request(QUrl(QString("https://www.cloudstudio.games")));
    request.setTransferTimeout(5000);
    QNetworkAccessManager manager;
    QNetworkReply* reply = manager.get(request);

    std::mutex waitMutex;
    std::condition_variable waitCondition;
    std::unique_lock<std::mutex> lock(waitMutex);
    waitCondition.wait_for(lock, std::chrono::seconds(1));

    static int s_iOpId = UIStateMachine::I().genOpId();
    if (reply->error() == QNetworkReply::NoError)
    {
        ConnectionManager::I().OnReconnect();//ConnectionManager::I().Reconnect();// 
    }
    else
    {
        UIStateMachine::I().push(s_iOpId, MsgCode::Client_Network_Error);
        QTimer::singleShot(1000, this, &Session::checkNetworkAndReconnect);
    }
}


void Session::KeepWindowAspectRatio(SDL_Event *event) {
    if (event == nullptr) {
        return;
    }

    int dispWith = 0;
    int dispHeight = 0;
    DXGI_MODE_ROTATION rotation = DXGI_MODE_ROTATION_UNSPECIFIED;
    SDL_Window *window = nullptr;

    if (event->window.windowID == SDL_GetWindowID(m_Window)) {
        dispWith = m_StreamConfig.width;
        dispHeight = m_StreamConfig.height;
        rotation = m_WindowRotation;
        window = m_Window;
    } else {
        dispWith = m_StreamConfig.width2;
        dispHeight = m_StreamConfig.height2;
        rotation = m_Window2Rotation;
        window = m_Window2;
    }

    // mac windows full screen can't catched by this
    auto flag = SDL_GetWindowFlags(window);
    if (flag & SDL_WINDOW_FULLSCREEN_DESKTOP) {
        return;
    }

#ifdef Q_OS_DARWIN
    int displayIndex = SDL_GetWindowDisplayIndex(window);
    SDL_DisplayMode displayMode;
    SDL_Rect winRect;
    SDL_GetWindowSize(window, &winRect.w, &winRect.h);
    if (SDL_GetCurrentDisplayMode(displayIndex, &displayMode) != 0) {
        qWarning() << "get display mode error";
    }
    if (winRect.w == displayMode.w && winRect.h == displayMode.h) {
        qDebug() << "mac windows full screen mode can't change size";
        return;
    }
#endif

    if (GleamCore::IsVerticalRotation(rotation)) {
        std::swap(dispWith, dispHeight);
    }
    int newWidth = event->window.data1;
    int newHeight = event->window.data2;

    int ratioWidth = dispWith * newHeight / dispHeight;
    int ratioHeight = dispHeight * newWidth / dispWith;

    if (ratioWidth == newWidth && ratioHeight == newHeight) {
        return;
    }

    if (ratioHeight <= newHeight) {
        SDL_SetWindowSize(window, newWidth, ratioHeight);
    } else {
        SDL_SetWindowSize(window, ratioWidth, newHeight);
    }
}
void Session::SessionMain()
{
    qDebug() << "RunSDL" << m_metricsReportTask;
    for (int i = SDL_NORMAL_EVENT; i <= SDL_CONNECT_ERROR_ALL; i++)
    {
        auto nRet = SDL_RegisterEvents(i);
        if (nRet == ((uint32_t)-1)) {
            qInfo() << "create sdl event failue i = "<<i;
        }
    }

    RichPresenceManager presence(*m_Preferences, m_App.name);
    bool bOnceRun = false;
    //Evetloop()
    SDL_Event event;
    // Uint32 lastUserEventTime = 0;
    QDateTime lastUserEventTime;
    CountdownWindow timeoutWindow;
    qint64 timeoutCnt = 0;
    static qint64 repeatSec = 300;
    for (;;) {
        if (gTimeoutAction.timeoutSec != 0 && lastUserEventTime.isValid()) {
            QDateTime currentTime = QDateTime::currentDateTime();
            qint64 diffSec = lastUserEventTime.secsTo(currentTime);
            if (gTimeoutAction.action == "disconnect") {
                if (diffSec >= gTimeoutAction.timeoutSec) {
                    if (!timeoutWindow.isActive()) {
                        timeoutWindow.show();
                        timeoutWindow.startCountdown();
                    } else {
                        lastUserEventTime = currentTime;
                    }
                }
            } else {
                if (diffSec >= gTimeoutAction.timeoutSec &&
                    (timeoutCnt == 0 || (diffSec - gTimeoutAction.timeoutSec)/repeatSec > timeoutCnt - 1)) {
                    timeoutCnt++;
                    IdleTimeoutReporter::TimoutInfo timeoutInfo {
                        .startTime = lastUserEventTime.toString("yyyy-MM-dd hh:mm:ss.zzz"),
                        .currentTime = currentTime.toString("yyyy-MM-dd hh:mm:ss.zzz")
                    };
                    gIdleTimeoutReporter.SendTimeoutInfo(timeoutInfo);
                }
            }
        }
#if SDL_VERSION_ATLEAST(2, 0, 18) && !defined(STEAM_LINK)
        // SDL 2.0.18 has a proper wait event implementation that uses platform
        // support to block on events rather than polling on Windows, macOS, X11,
        // and Wayland. It will fall back to 1 ms polling if a joystick is
        // connected, so we don't use it for STEAM_LINK to ensure we only poll
        // every 10 ms.
        //
        // NB: This behavior was introduced in SDL 2.0.16, but had a few critical
        // issues that could cause indefinite timeouts, delayed joystick detection,
        // and other problems.

        //if (!SDL_WaitEventTimeout(&event, 1000)) {
        if (!SDL_WaitEventTimeout(&event, 1000)) {  //Victor
            presence.runCallbacks();
            continue;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents);
        QCoreApplication::sendPostedEvents();

#else \
        // We explicitly use SDL_PollEvent() and SDL_Delay() because \
        // SDL_WaitEvent() has an internal SDL_Delay(10) inside which \
        // blocks this thread too long for high polling rate mice and high \
        // refresh rate displays.
        if (!SDL_PollEvent(&event)) {
#ifndef STEAM_LINK
            SDL_Delay(1);
#else \
            // Waking every 1 ms to process input is too much for the low performance \
            // ARM core in the Steam Link, so we will wait 10 ms instead.
            SDL_Delay(10);
#endif
            presence.runCallbacks();
            continue;
        }
#endif
        switch (event.type) {
        case SDL_RENDER_HAVE_FIRSTFRAME:
        {
            if (SDL_GetWindowID(m_Window) == event.window.windowID)
                SessionUI::I().getSession()->HaveFirstFrame(1);
            else if (SDL_GetWindowID(m_Window2) == event.window.windowID)
                SessionUI::I().getSession()->HaveFirstFrame(2);
            UIStateMachine::I().push(0, MsgCode::REMDER_HAVE_FIRSTFRAME);

            if (!lastUserEventTime.isValid()) {
                lastUserEventTime = QDateTime::currentDateTime();
            }
        }
        break;
        case SDL_STATUS_CHANGED:
        {
            if (g_pConfigPopup /*&& g_pConfigPopup->isVisible()*/)
            {
                g_pConfigPopup->onStatusChanged();
            }

            ConnectionManager::I().lockStatus();
            auto status = ConnectionManager::I().getStatus();
            ConnectionManager::I().unlockStatus();
            if (status.Reconnect)
            {
                SDL_Event event;
                event.type = SDL_DISCONNECTION_EVENT;
                SDL_PushEvent(&event);
            }

            if (m_bAutoStreaming)
            {
                m_bAutoStreaming = false;
                checkAutoCreateStreaming();
            }
        }
        break;
        case SDL_DISPLAY_CHANGED:
        {
                OnDisplayConfigChanged();
        }
        break;
        case SDL_SERVER_DATA_UPDATE_EVENT:
        {
            //SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,"SDL_STOP_SERVER_DATA");
            //SServerInfoData *pInfor = (SServerInfoData*)event.user.data1;
            auto EventData = g_Manager.pullEvent(emEventDataType::emNoraml);
            if(EventData != nullptr)
            {
                SServerInfoData *p = dynamic_cast<SServerInfoData*>(EventData.get());
                CheckDisplayStream( p->nDisplayCount);
            }
        }
        break;
        case SDL_DISCONNECTION_EVENT:           //reconnect
        {
            m_HavefirstFrame1 = false;
            m_HavefirstFrame2 = false;
            checkNetworkAndReconnect();
        }
        break;
        case SDL_RESET_EVENT:
        {
            ResetVideoDecoder();
            NotifyReconnect();
        }
        break;
        case SDL_RECONNECT_SUCESS_EVENT:
        {
            int nDisplayCount = (int)(uintptr_t)event.user.data1;
            if(nDisplayCount > 1)
            {
                m_isDualDisplay = true;
            }
            else
            {
                m_isDualDisplay = false;
            }
            qDebug()<<"SDL_RECONNECT_SUCESS_EVENT DisplayCount =  "<<nDisplayCount;
            ReInitInfor();
            SDLInitSubSystem();
            InitSdlWindow();
            ReFresh();
            m_ConnectSucess = true;
        }
        break;
        case SDL_CLSET_HDRMODE_EVENT:
        {
            qDebug()<<"SDL_CLSET_HDRMODE_EVENT = "<< event.user.code;
            SDL_AtomicLock(&m_DecoderLock);
            {
                if (event.user.data1 == (void*)1)
                {
                    IVideoDecoder *decoder = GetVideoDecoder();
                    if (decoder != nullptr) {
                        decoder->setHdrMode(event.user.code);
                    }
                } else if (m_isDualDisplay && event.user.data1 == (void*)2) {
                    IVideoDecoder *decoder2 = m_VideoDecoder2;
                    if (decoder2 != nullptr) {
                        decoder2->setHdrMode(event.user.code);
                    }
                }
            }
            SDL_AtomicUnlock(&m_DecoderLock);
        }
        break;
        case SDL_CLIENT_STATUS_EVENT:
        {
            ConnectStutasUpdate((uintptr_t)event.user.code);
        }
        break;
        case SDL_CLOSE_WINDOW2_EVENT:
        {
            qDebug()<<"SDL_CLOSE_WINDOW2_EVENT";
            HideWindows2();
        }
        break;
        case SDL_QUIT:
        {
            CloseVideoStreamAndWindow(1);
            qInfo()<<"SDL_QUIT DispatchDeferredCleanup";
            goto DispatchDeferredCleanup;
        }
        break;
        case SDL_USEREVENT:
            switch (event.user.code) {
            case SDL_CODE_FRAME_READY:
                qDebug() << "SDL_USEREVENT SDL_CODE_FRAME_READY";
                if (m_VideoDecoder != nullptr)
                {
                    m_VideoDecoder->renderFrameOnMainThread();
                }
                if (m_isDualDisplay)
                {
                    if (m_VideoDecoder2 != nullptr)
                    {
                        m_VideoDecoder2->renderFrameOnMainThread();
                    }
                }
                break;
            case SDL_CODE_FLUSH_WINDOW_EVENT_BARRIER:
                m_FlushingWindowEventsRef--;
                break;
            case SDL_CODE_GAMECONTROLLER_RUMBLE:
                m_InputHandler->rumble((unsigned short)(uintptr_t)event.user.data1,
                                       (unsigned short)((uintptr_t)event.user.data2 >> 16),
                                       (unsigned short)((uintptr_t)event.user.data2 & 0xFFFF));
                break;
            default:
                SDL_assert(false);
            }
            break;
        case SDL_WINDOWEVENT:
            // Early handling of some events
            //qDebug()<<"SDL_WINDOWEVENT event.window.event= "<< event.window.event;
            switch (event.window.event) {
            case SDL_WINDOWEVENT_RESIZED:
            {
                KeepWindowAspectRatio(&event);
                break;
            }
            case SDL_WINDOWEVENT_FOCUS_LOST:
            {
                if (m_Preferences->muteOnFocusLoss) {
                    m_AudioMuted = true;
                }
                m_InputHandler->notifyFocusLost();
                break;
            }
            case SDL_WINDOWEVENT_FOCUS_GAINED:
                if (m_Preferences->muteOnFocusLoss) {
                    m_AudioMuted = false;
                }
                break;
            case SDL_WINDOWEVENT_LEAVE:
                m_InputHandler->notifyMouseLeave();
                break;
            case SDL_WINDOWEVENT_CLOSE:
                auto id = SDL_GetWindowID(m_Window);
                if (event.window.windowID == id) {
                    CheckAndCloseWindow(event.window.windowID);
                    qInfo()<<"SDL_WINDOWEVENT_CLOSE DispatchDeferredCleanup";
                    goto DispatchDeferredCleanup;
                }
                else
                    CheckAndCloseWindow(event.window.windowID);
                break;
            }

            presence.runCallbacks();

            // Capture the mouse on SDL_WINDOWEVENT_ENTER if needed
            if (m_needsFirstEnterCapture && event.window.event == SDL_WINDOWEVENT_ENTER) {
                m_InputHandler->setCaptureActive(true);
                m_needsFirstEnterCapture = false;
            }

            // We want to recreate the decoder for resizes (full-screen toggles) and the initial shown event.
            // We use SDL_WINDOWEVENT_SIZE_CHANGED rather than SDL_WINDOWEVENT_RESIZED because the latter doesn't
            // seem to fire when switching from windowed to full-screen on X11.
            if (event.window.event != SDL_WINDOWEVENT_SIZE_CHANGED && event.window.event != SDL_WINDOWEVENT_SHOWN) {
                // Check that the window display hasn't changed. If it has, we want
                // to recreate the decoder to allow it to adapt to the new display.
                // This will allow Pacer to pull the new display refresh rate.
#if SDL_VERSION_ATLEAST(2, 0, 18)
                // On SDL 2.0.18+, there's an event for this specific situation
                if (event.window.event != SDL_WINDOWEVENT_DISPLAY_CHANGED) {
                    break;
                }
#else \
                // Prior to SDL 2.0.18, we must check the display index for each window event
                if (SDL_GetWindowDisplayIndex(m_Window) == m_currentDisplayIndex) {
                    break;
                }
                if (m_isDualDisplay) {
                    if (SDL_GetWindowDisplayIndex(m_Window2) == m_currentDisplayIndex2) {
                        break;
                    }
                }
#endif
            }
#ifdef Q_OS_WIN32
            // We can get a resize event after being minimized. Recreating the renderer at that time can cause
            // us to start drawing on the screen even while our window is minimized. Minimizing on Windows also
            // moves the window to -32000, -32000 which can cause a false window display index change. Avoid
            // that whole mess by never recreating the decoder if we're minimized.
            else if (SDL_GetWindowFlags(m_Window) & SDL_WINDOW_MINIMIZED) {
                break;
            }
#endif

            if (event.window.data1 == 0 && event.window.data2 == 0 && m_FlushingWindowEventsRef > 0) {
                // Ignore window events for renderer reset if flushing
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                            "Dropping window event during flush: %d (%d %d)",
                            event.window.event,
                            event.window.data1,
                            event.window.data2);
                break;
            }
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "Recreating renderer for window event: %d (%d %d)",
                        event.window.event,
                        event.window.data1,
                        event.window.data2);
        case SDL_RENDER_DEVICE_RESET:
        case SDL_RENDER_TARGETS_RESET:
            if (event.type != SDL_WINDOWEVENT) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                            "Recreating renderer by internal request: %d",
                            event.type);
            }
            qDebug() << "SDL_RENDER_DEVICE_RESET SDL_RENDER_TARGETS_RESET SDL_AtomicLock"<<m_VideoDecoder
                    <<"<<event.type = "<<event.type;
            SDL_AtomicLock(&m_DecoderLock);

            // Destroy the old decoder
            if (event.window.windowID == m_winID1) {
                if(m_VideoDecoder)
                {
                    delete m_VideoDecoder;
                }
            }
            if (m_isDualDisplay) {
                if (event.window.windowID == m_winID2) {
                    if(m_VideoDecoder2)
                    {
                        delete m_VideoDecoder2;
                    }
                }
            }
            // Insert a barrier to discard any additional window events
            // that could cause the renderer to be and recreated again.
            // We don't use SDL_FlushEvent() here because it could cause
            // important events to be lost.
            flushWindowEvents();

            // Update the window display mode based on our current monitor
            // NB: Avoid a useless modeset by only doing this if it changed.
            if (event.window.windowID == m_winID1) {
                if (m_currentDisplayIndex != SDL_GetWindowDisplayIndex(m_Window)) {
                    m_currentDisplayIndex = SDL_GetWindowDisplayIndex(m_Window);
                    updateOptimalWindowDisplayMode(m_Window);
                }
            }
            if (m_isDualDisplay) {
                if (event.window.windowID == m_winID2) {
                    if (m_currentDisplayIndex2 != SDL_GetWindowDisplayIndex(m_Window2)) {
                        m_currentDisplayIndex2= SDL_GetWindowDisplayIndex(m_Window2);
                        updateOptimalWindowDisplayMode(m_Window2);
                    }
                }
            }

            // Now that the old decoder is dead, flush any events it may
            // have queued to reset itself (if this reset was the result
            // of state loss).
            SDL_PumpEvents();
            // SDL_FlushEvent(SDL_RENDER_DEVICE_RESET);
            // SDL_FlushEvent(SDL_RENDER_TARGETS_RESET);

            if(m_VideoStreamInitialized)
            {
                // If the stream exceeds the display refresh rate (plus some slack),
                // forcefully disable V-sync to allow the stream to render faster
                // than the display.
                int displayHz = StreamUtils::getDisplayRefreshRate(m_Window);
                bool enableVsync = m_Preferences->enableVsync;
                if (displayHz + 5 < m_StreamConfig.fps) {
                    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                                "Disabling V-sync because refresh rate limit exceeded");
                    enableVsync = false;
                }

                // Choose a new decoder (hopefully the same one, but possibly
                // not if a GPU was removed or something).
                if (GetVideoDecoder() == nullptr  ||
                    event.window.windowID == m_winID1) {
                    if(!chooseDecoder(m_Preferences->videoDecoderSelection,
                                    m_Window, m_ActiveVideoFormat, m_StreamConfig.width,
                                    m_StreamConfig.height, m_StreamConfig.fps,
                                    enableVsync,
                                    enableVsync && m_Preferences->framePacing,
                                    false,
                                    m_VideoDecoder, m_WindowRotation, 1)) {

                        //qDebug() << "SDL_RENDER_DEVICE_RESET SDL_RENDER_TARGETS_RESET_1 SDL_AtomicUnlock";
                        //SDL_AtomicUnlock(&m_DecoderLock);
                        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"Failed to recreate decoder after reset");
                    }
                }
                if ((m_isDualDisplay && m_VideoDecoder2 == nullptr) ||event.window.windowID == m_winID2) {
                    if(m_isDualDisplay)
                    {
                        if (!chooseDecoder(m_Preferences->videoDecoderSelection,
                                           m_Window2, m_ActiveVideoFormat, m_StreamConfig.width2,
                                           m_StreamConfig.height2, m_StreamConfig.fps2,
                                           enableVsync,
                                           enableVsync && m_Preferences->framePacing,
                                           false,
                                           m_VideoDecoder2, m_Window2Rotation,2))
                        {
                            //qDebug() << "SDL_RENDER_DEVICE_RESET SDL_RENDER_TARGETS_RESET_2 SDL_AtomicUnlock";
                            //SDL_AtomicUnlock(&m_DecoderLock);
                            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                                         "Failed to recreate decoder2 after reset");
                            //emit displayLaunchError(tr("Unable to initialize video decoder. Please check your streaming settings and again."));
                        }
                    }
                }

                // As of SDL 2.0.12, SDL_RecreateWindow() doesn't carry over mouse capture
                // or mouse hiding state to the new window. By capturing after the decoder
                // is set up, this ensures the window re-creation is already done.
                if (m_needsPostDecoderCreationCapture) {
#ifdef Q_OS_WIN32
                    m_InputHandler->setCaptureActive(true);
#endif
#ifdef TARGET_OS_MAC
                    m_InputHandler->setCaptureActive(false);
#endif
                    m_needsPostDecoderCreationCapture = false;
                }
            }

            // Request an IDR frame to complete the reset
            LiRequestIdrFrame(1);
            if (m_isDualDisplay)
            {
                LiRequestIdrFrame(2);
            }

            // Set HDR mode. We may miss the callback if we're in the middle
            // of recreating our decoder at the time the HDR transition happens.
            if (event.window.windowID == m_winID1 && GetVideoDecoder() != nullptr) {
                GetVideoDecoder()->setHdrMode(LiGetCurrentHostDisplayHdrMode(1));
            }
            if (m_isDualDisplay) {
                if (event.window.windowID == m_winID2 && m_VideoDecoder2 != nullptr) {
                    m_VideoDecoder2->setHdrMode(LiGetCurrentHostDisplayHdrMode(2));
                }
            }
            // After a window resize, we need to reset the pointer lock region
            m_InputHandler->updatePointerRegionLock();
            qDebug() << "SDL_RENDER_DEVICE_RESET SDL_RENDER_TARGETS_RESET_3 SDL_AtomicUnlock";
            SDL_AtomicUnlock(&m_DecoderLock);
            if (m_OverlayManager.isOverlayEnabled(Overlay::OverlayStatusUpdate)) {
                ConnectStutasUpdate(0);
                ConnectStutasUpdate(1);
            }
            break;

        case SDL_KEYUP:
        case SDL_KEYDOWN:
            presence.runCallbacks();
            m_InputHandler->handleKeyEvent(&event.key);
            lastUserEventTime = QDateTime::currentDateTime();
            timeoutCnt = 0;
            break;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
            presence.runCallbacks();
            m_InputHandler->handleMouseButtonEvent(&event.button);
            lastUserEventTime = QDateTime::currentDateTime();
            timeoutCnt = 0;
            break;
        case SDL_MOUSEMOTION:
            m_InputHandler->handleMouseMotionEvent(&event.motion);
            lastUserEventTime = QDateTime::currentDateTime();
            timeoutCnt = 0;
            break;
        case SDL_MOUSEWHEEL:
            m_InputHandler->handleMouseWheelEvent(&event.wheel);
            lastUserEventTime = QDateTime::currentDateTime();
            timeoutCnt = 0;
            break;
        case SDL_CONTROLLERAXISMOTION:
            m_InputHandler->handleControllerAxisEvent(&event.caxis);
            lastUserEventTime = QDateTime::currentDateTime();
            timeoutCnt = 0;
            break;
        case SDL_CONTROLLERBUTTONDOWN:
        case SDL_CONTROLLERBUTTONUP:
            presence.runCallbacks();
            m_InputHandler->handleControllerButtonEvent(&event.cbutton);
            lastUserEventTime = QDateTime::currentDateTime();
            timeoutCnt = 0;
            break;
        case SDL_CONTROLLERDEVICEADDED:
        case SDL_CONTROLLERDEVICEREMOVED:
            m_InputHandler->handleControllerDeviceEvent(&event.cdevice);
            lastUserEventTime = QDateTime::currentDateTime();
            timeoutCnt = 0;
            break;
        case SDL_JOYDEVICEADDED:
            m_InputHandler->handleJoystickArrivalEvent(&event.jdevice);
            lastUserEventTime = QDateTime::currentDateTime();
            timeoutCnt = 0;
            break;
        case SDL_FINGERDOWN:
        case SDL_FINGERMOTION:
        case SDL_FINGERUP:
            m_InputHandler->handleTouchFingerEvent(&event.tfinger);
            lastUserEventTime = QDateTime::currentDateTime();
            timeoutCnt = 0;
            break;
        }
        if(false == bOnceRun)
        {
            NotifyReconnect();
            InitMetricReport();
            bOnceRun = true;
        }
    }

DispatchDeferredCleanup:
    // SendSessionInfoPortal("Normal End");
    qInfo()<<"Enter DispatchDeferredCleanup";
#ifdef _WIN64
    UnregisterHotkey();
#endif
    // Uncapture the mouse and hide the window immediately,
    // so we can return to the Qt GUI ASAP.
        m_InputHandler->setCaptureActive(false);
        // Raise any keys that are still down
        m_InputHandler->raiseAllKeys();

        m_metricsReportTask->Stop();
        m_metricsReportTask->quit();
        m_metricsReportTask->wait();

        StopSerStatusTask();
        CoonetThreadStop();
    {
        SDL_AtomicLock(&m_DecoderLock);
        if(m_VideoDecoder)
        {
            qDebug()<<"sdl_quit delete m_VideoDecoder ";
            delete m_VideoDecoder;
            m_VideoDecoder = nullptr;
        }

        if(m_VideoDecoder2)
        {
            qDebug()<<"sdl_quit  delete m_VideoDecoder2 ";
            delete m_VideoDecoder2;
            m_VideoDecoder2 = nullptr;
        }
        SDL_AtomicUnlock(&m_DecoderLock);
        if (m_iconSurface != nullptr)
        {
            SDL_FreeSurface(m_iconSurface);
            m_iconSurface = nullptr;
        }
        if (m_iconSurface2 != nullptr)
        {
            SDL_FreeSurface(m_iconSurface2);
            m_iconSurface2 = nullptr;
        }
    }

    SDL_EnableScreenSaver();
    SDL_SetHint(SDL_HINT_TIMER_RESOLUTION, "0");
    if (QGuiApplication::platformName() == "eglfs")
    {
        QGuiApplication::restoreOverrideCursor();
    }
    qDebug()<<"new DeferredSessionCleanupTask";
    QThreadPool::globalInstance()->start(new DeferredSessionCleanupTask());

}

void Session::execInternal()
{
    s_ActiveSessionSemaphore.acquire();
    if (!initialize())
    {
        qDebug() << "initialize  error ,exit client." ;
        return;
    }
    SetConnectInfo(m_Preferences,m_StreamConfig,m_VideoCallbacks,m_AudioCallbacks,m_Computer.get(), m_App, m_ClientUUid,
                    m_PrivateKeyStr,m_InputHandler->getAttachedGamepadMask(),m_AudioDisabled,
                    m_HttpJsonStr, m_ClientOS, m_ClientArch);
    //qDebug()<<"read create sdl window";
    SessionMain();

}

void Session::CheckDual() {
    ImmediatelyNotity();
}

void Session::ConnectStutasUpdate(int connectionStatus)
{
    qDebug()<<"ConnectStutasUpdate =  "<<connectionStatus;
    switch (connectionStatus) {
    case CONN_STATUS_POOR:
        if (AsyncConnect::getInstance()->m_StreamConfig.bitrate > 50000)
        {
            strcpy(m_OverlayManager.getOverlayText(Overlay::OverlayStatusUpdate), "Slow connection to PC\nReduce your bitrate");
        } else
        {
            strcpy(m_OverlayManager.getOverlayText(Overlay::OverlayStatusUpdate), "Poor connection to PC");
        }
        m_OverlayManager.setOverlayTextUpdated(Overlay::OverlayStatusUpdate);
        m_OverlayManager.setOverlayState(Overlay::OverlayStatusUpdate, true);
        break;
    case CONN_STATUS_OKAY:
        m_OverlayManager.setOverlayState(Overlay::OverlayStatusUpdate, false);
        break;
    }
}


bool Session::ReseCthooseDecoder(StreamingPreferences::VideoDecoderSelection vds, SDL_Window *window, int videoFormat,
                                 int width, int height, int frameRate, bool enableVsync, bool enableFramePacing,
                                 bool testOnly, IVideoDecoder *&chosenDecoder) {
    DECODER_PARAMETERS params;

    // We should never have vsync enabled for test-mode.
    // It introduces unnecessary delay for renderers that may
    // block while waiting for a backbuffer swap.
    SDL_assert(!enableVsync || !testOnly);

    params.width = width;
    params.height = height;
    params.frameRate = frameRate;
    params.videoFormat = videoFormat;
    params.window = window;
    params.enableVsync = enableVsync;
    params.enableFramePacing = enableFramePacing;
    params.testOnly = testOnly;
    params.vds = vds;

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "ReseCthooseDecoder V-sync %s", enableVsync ? "enabled" : "disabled");

#ifdef HAVE_SLVIDEO
    if(nullptr == chosenDecoder)
    {
        chosenDecoder = new SLVideoDecoder(testOnly);
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "new SLVideoDecoder");
    }
    if (chosenDecoder && chosenDecoder->initialize(&params)) {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "SLVideo video decoder chosen");
        return true;
    }
    else
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Unable to load SLVideo decoder");
        delete chosenDecoder;
        chosenDecoder = nullptr;
    }
#endif
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "ReseCthooseDecoder chosenDecoder->initialize %p",chosenDecoder);
#ifdef HAVE_FFMPEG
    if(nullptr == chosenDecoder)
    {
        chosenDecoder = new FFmpegVideoDecoder(testOnly);
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "new FFmpegVideoDecoder");
    }
    if (chosenDecoder && chosenDecoder->initialize(&params)) {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "ReseCthooseDecoder FFmpeg-based video decoder chosen");
        return true;
    }
    else
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "ReseCthooseDecoder Unable to load FFmpeg decoder");
        delete chosenDecoder;
        chosenDecoder = nullptr;
    }
#endif

#if !defined(HAVE_FFMPEG) && !defined(HAVE_SLVIDEO)
#  error No video decoding libraries available!
#endif

    // If we reach this, we didn't initialize any decoders successfully
    return false;
}

void Session::ReFresh()
{
    SDL_AtomicLock(&m_DecoderLock);

    flushWindowEvents();
    // Update the window display mode based on our current monitor
    // NB: Avoid a useless modeset by only doing this if it changed.
    if (m_currentDisplayIndex != SDL_GetWindowDisplayIndex(m_Window)) {
        m_currentDisplayIndex = SDL_GetWindowDisplayIndex(m_Window);
        updateOptimalWindowDisplayMode(m_Window);
    }

    if (m_isDualDisplay)
    {
        if (m_currentDisplayIndex2 != SDL_GetWindowDisplayIndex(m_Window2)) {
            m_currentDisplayIndex2= SDL_GetWindowDisplayIndex(m_Window2);
            updateOptimalWindowDisplayMode(m_Window2);
        }
    }

    // Now that the old decoder is dead, flush any events it may
    // have queued to reset itself (if this reset was the result
    // of state loss).
    SDL_PumpEvents();
    SDL_FlushEvent(SDL_RENDER_DEVICE_RESET);
    SDL_FlushEvent(SDL_RENDER_TARGETS_RESET);

    {
        // If the stream exceeds the display refresh rate (plus some slack),
        // forcefully disable V-sync to allow the stream to render faster
        // than the display.
        int displayHz = StreamUtils::getDisplayRefreshRate(m_Window);
        bool enableVsync = m_Preferences->enableVsync;
        if (displayHz + 5 < m_StreamConfig.fps) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "Disabling V-sync because refresh rate limit exceeded");
            enableVsync = false;
        }

        // Choose a new decoder (hopefully the same one, but possibly
        // not if a GPU was removed or something).

        if(!ReseCthooseDecoder(m_Preferences->videoDecoderSelection,
                                m_Window, m_ActiveVideoFormat, m_ActiveVideoWidth,
                                m_ActiveVideoHeight, m_ActiveVideoFrameRate,
                                enableVsync,
                                enableVsync && m_Preferences->framePacing,
                                false,
                                m_VideoDecoder)) {

            qDebug() << "SDL_RENDER_DEVICE_RESET SDL_RENDER_TARGETS_RESET_1 SDL_AtomicUnlock";
            SDL_AtomicUnlock(&m_DecoderLock);
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"ReseCthooseDecoder Failed to recreate decoder after reset");
        }
        else
        {
            s_ActiveSession->GetVideoDecoder()->SetStreamId(1);
        }


        if(m_isDualDisplay)
        {
            if (!ReseCthooseDecoder(m_Preferences->videoDecoderSelection,
                                    m_Window2, m_ActiveVideoFormat, m_ActiveVideoWidth,
                                    m_ActiveVideoHeight, m_ActiveVideoFrameRate,
                                    enableVsync,
                                    enableVsync && m_Preferences->framePacing,
                                    false,
                                    m_VideoDecoder2))
            {
                qDebug() << "SDL_RENDER_DEVICE_RESET SDL_RENDER_TARGETS_RESET_2 SDL_AtomicUnlock";
                SDL_AtomicUnlock(&m_DecoderLock);
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"ReseCthooseDecoder Failed to recreate decoder2 after reset");
                //emit displayLaunchError(tr("Unable to initialize video decoder. Please check your streaming settings and again."));
            }
            else {
                s_ActiveSession->m_VideoDecoder2->SetStreamId(2);
            }
        }


        // As of SDL 2.0.12, SDL_RecreateWindow() doesn't carry over mouse capture
        // or mouse hiding state to the new window. By capturing after the decoder
        // is set up, this ensures the window re-creation is already done.
    }
    // Set HDR mode. We may miss the callback if we're in the middle
    // of recreating our decoder at the time the HDR transition happens.
    if (GetVideoDecoder() != nullptr)
    {
        GetVideoDecoder()->setHdrMode(LiGetCurrentHostDisplayHdrMode(1));
    }
    if (m_isDualDisplay)
    {
        if (m_VideoDecoder2 != nullptr) {
            m_VideoDecoder2->setHdrMode(LiGetCurrentHostDisplayHdrMode(2));
        }
    }
    // Request an IDR frame to complete the reset
    LiRequestIdrFrame(1);
    if (m_isDualDisplay)
    {
        LiRequestIdrFrame(2);
    }

    qDebug() << "SDL_RENDER_DEVICE_RESET LiRequestIdrFrame";
    SDL_AtomicUnlock(&m_DecoderLock);
    if (m_needsPostDecoderCreationCapture) {
#ifdef Q_OS_WIN32
        m_InputHandler->setCaptureActive(true);
#endif
#ifdef TARGET_OS_MAC
        m_InputHandler->setCaptureActive(false);
#endif
        m_needsPostDecoderCreationCapture = false;
    }
    // After a window resize, we need to reset the pointer lock region
    m_InputHandler->updatePointerRegionLock();
}

extern std::string getClientUUid();
bool Session::SendStopVideoMessage(int streamId)
{
    // not connected successfully yet,no need send
    if (m_Computer == nullptr) {
        return true;
    }

    // Create the JSON object
    QJsonObject jsonObj;
    jsonObj["ActionType"] = "StopVideo";
    jsonObj["StreamId"] = streamId;

    // Convert JSON object to string
    QJsonDocument jsonDoc(jsonObj);
    QString jsonString = jsonDoc.toJson(QJsonDocument::Compact);

    QUrlQuery query;
    query.addQueryItem("message", jsonString);
    query.addQueryItem("type", "config");
    query.addQueryItem("client_uuid", getClientUUid().c_str());
    QString szMessage = query.toString(QUrl::FullyEncoded);

    HttpMessageTask* pTask = new HttpMessageTask(this, szMessage,0);
    QThreadPool::globalInstance()->start(pTask);
    return true;
}
void Session::CheckAndCloseWindow(uint32_t winId)
{
    if (m_Window)
    {
        if (winId == SDL_GetWindowID(m_Window)) 
        {
            CloseVideoStreamAndWindow(1);
        }
    }
    if (m_Window2)
    {
        if (winId == SDL_GetWindowID(m_Window2)) 
        {
            CloseVideoStreamAndWindow(2);
        }
    }
}
void Session::CloseVideoStreamAndWindow(int streamId)
{
    switch (streamId)
    {
    case 1:
        {
            if (m_HavefirstFrame1)
            {
                m_HavefirstFrame1 = false;
                SendStopVideoMessage(1);
            }
            SDL_AtomicLock(&m_DecoderLock);
            if (m_VideoDecoder) // should release before VideoStream
            {
                delete m_VideoDecoder;
                m_VideoDecoder = nullptr;
            }
            LiStopStream();
            ResetControlInfoPerStream(1);
            SDL_DestroyWindow(m_Window);
            m_Window = nullptr;
            m_WindowRotation = static_cast<DXGI_MODE_ROTATION>(-1);
            if (m_Window2 != nullptr)
            {
                // also close 2 when close 1, but not send stop video
                if (m_VideoDecoder2)
                {
                    delete m_VideoDecoder2;
                    m_VideoDecoder2 = nullptr;
                }
                SDL_AtomicUnlock(&m_DecoderLock);
                LiStopStream2();
                ResetControlInfoPerStream(2);
                SDL_DestroyWindow(m_Window2);
                m_Window2 = nullptr;
                m_Window2Rotation = static_cast<DXGI_MODE_ROTATION>(-1);
            }
            else {
                SDL_AtomicUnlock(&m_DecoderLock);
            }
            ConnectionManager::I().sendQuitMessage();
        }
        break;
    case 2:
        {
            if (m_HavefirstFrame2)
            {
                m_HavefirstFrame2 = false;
                SendStopVideoMessage(2);
            }
            SDL_AtomicLock(&m_DecoderLock);
            if (m_VideoDecoder2) // should release before VideoStream
            {
                delete m_VideoDecoder2;
                m_VideoDecoder2 = nullptr;
            }
            SDL_AtomicUnlock(&m_DecoderLock);
            LiStopStream2();
            ResetControlInfoPerStream(2);
            SDL_DestroyWindow(m_Window2);
            m_Window2 = nullptr;
            m_Window2Rotation = static_cast<DXGI_MODE_ROTATION>(-1);
            m_InputHandler->setWindow(m_Window, m_Window2);
        }
        break;
    default:
        break;
    }
}
void Session::ResetVideoDecoder()
{
    SDL_AtomicLock(&m_DecoderLock);
#ifdef HAVE_SLVIDEO
    SLVideoDecoder *pSLVideoDecoder = dynamic_cast<SLVideoDecoder*>(m_VideoDecoder);
    if(pSLVideoDecoder)
    {
        qDebug()<<"pSLVideoDecoder->reset() ";
        pSLVideoDecoder->reset();
    }
    SLVideoDecoder *pSLVideoDecoder2 = dynamic_cast<SLVideoDecoder*>(m_VideoDecoder2);
    if(pSLVideoDecoder2)
    {
        qDebug()<<"pSLVideoDecoder2->reset() ";
        pSLVideoDecoder2->reset();
    }
#endif
#ifdef HAVE_FFMPEG
    FFmpegVideoDecoder *pFFmpgVideDecode = dynamic_cast<FFmpegVideoDecoder*>(m_VideoDecoder);
    if(pFFmpgVideDecode)
    {
        qDebug()<<"pFFmpgVideDecode->reset() ";
        pFFmpgVideDecode->reset();
    }
    FFmpegVideoDecoder *pFFmpgVideDecode2 = dynamic_cast<FFmpegVideoDecoder*>(m_VideoDecoder2);
    if(pFFmpgVideDecode2)
    {
        qDebug()<<"pFFmpgVideDecode2->reset() ";
        pFFmpgVideDecode2->reset();
    }
#endif
    SDL_AtomicUnlock(&m_DecoderLock);
}

void Session::SetRequestComputerInforData(const QString &szCSessionID,const std::string &Curl,const  std::string &arch)
{
    m_szCSessionID = szCSessionID;
    m_HttpJsonStr = Curl;
    if(m_ClientArch.isEmpty())
    {
        m_ClientArch = arch.c_str();
    }
}
void Session::SendSessionInfoPortal(QString message)
{
    qDebug()<<"SendSessionInfoPortal";
    CPortalInteractionManager::I().pushEvent(new CPortalSessionEvent(getClientUUid(),m_HttpJsonStr,
    m_ClientOS.toStdString(),m_ClientArch.toStdString(),message.toStdString()));
}

qreal GetPrimaryDevicePixelRatio() {
#ifdef Q_OS_WIN32
    // 获取缩放比例
    static qreal qtScaleFactor = QGuiApplication::primaryScreen()->devicePixelRatio();
    if (qtScaleFactor > 2.0) {
        qDebug() << "qtScaleFactor too large : " << qtScaleFactor;
        qtScaleFactor = 2.0;
    }
#else
    static qreal qtScaleFactor = 1.0;
#endif
    return qtScaleFactor;
}








