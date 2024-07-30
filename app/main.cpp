#include <QApplication>
#include <QCursor>
#include <QElapsedTimer>
#include <QFile>
#include <QFont>
#include <QGuiApplication>
#include <QIcon>
#include <QJsonDocument>
#include <QMutex>
#include <QNetworkProxyFactory>
#include <QPalette>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWidget>
#include <QtDebug>
#include <regex>
#include <sstream>

// Don't let SDL hook our main function, since Qt is already
// doing the same thing. This needs to be before any headers
// that might include SDL.h themselves.
#define SDL_MAIN_HANDLED
#include <SDL.h>
#ifdef HAVE_FFMPEG
#include "streaming/video/ffmpeg.h"
#endif

#if defined(Q_OS_WIN32)
#include "antihookingprotection.h"
#elif defined(Q_OS_LINUX)
#include <openssl/ssl.h>
#endif

#include "backend/autoupdatechecker.h"
#include "backend/computermanager.h"
#include "backend/nvpairingmanager.h"
#include "backend/systemproperties.h"
#include "cli/commandlineparser.h"
#include "cli/listapps.h"
#include "cli/pair.h"
#include "cli/quitstream.h"
#include "cli/startstream.h"
#include "gui/appmodel.h"
#include "gui/computermodel.h"
#include "gui/sdlgamepadkeynavigation.h"
#include "path.h"
#include "settings/streamingpreferences.h"
#include "streaming/encryption.h"
#include "streaming/gleamapi.h"
#include "streaming/input/input.h"
#include "streaming/session.h"
#include "utils.h"
#include <QStandardPaths>
#include <QWebChannel>
#include <QWebEngineView>

QQmlApplicationEngine *engine;
#ifdef Q_OS_WIN
#include <Windows.h>
#endif

extern PopupWindow *g_pConfigPopup;

extern CWebConfig *g_pConfig1;
extern CWebConfig *g_pConfig2;
extern CWebConfig *g_pConfig3;

void ShowSetting() {
  if (g_pConfigPopup) {
    g_pConfigPopup->hide();
    g_pConfigPopup->Reszie();
    g_pConfigPopup->Showpage();
    g_pConfigPopup->show();
  }
}
void ShowConfigSetting() {
  QObject *object = engine->rootObjects().first();
  if (object) {
    QMetaObject::invokeMethod(object, "settingshow");
    qDebug() << "ShowSetting ";

    QWindow *window = qobject_cast<QWindow *>(object);
    if (window) {
      qDebug() << "hide menu" << window->winId();
    }
  }
}
void CloseSetting() {
  if (g_pConfigPopup) {
    g_pConfigPopup->hide();
  }
}

void CloseConfigSetting() {
  QObject *object = engine->rootObjects().first();
  if (object) {
    QMetaObject::invokeMethod(object, "closeseting");
    qDebug() << "CloasSetting ";
  }
}

#if !defined(QT_DEBUG) && defined(Q_OS_WIN32)
// Log to file for release Windows builds
#define USE_CUSTOM_LOGGER
#define LOG_TO_FILE
#elif defined(Q_OS_UNIX) && !defined(Q_OS_DARWIN)
// Use stdout logger on all Linux/BSD builds
#define USE_CUSTOM_LOGGER
#elif !defined(QT_DEBUG) && defined(Q_OS_DARWIN)
// Log to file for release Mac builds
#define USE_CUSTOM_LOGGER
#define LOG_TO_FILE
#else
// For debug Windows and Mac builds, use default logger
#endif

#ifdef USE_CUSTOM_LOGGER
static QElapsedTimer s_LoggerTime;
static QTextStream s_LoggerStream(stdout);
static QMutex s_LoggerLock;
static bool s_SuppressVerboseOutput;
#ifdef LOG_TO_FILE
// Max log file size of 10 MB
#define MAX_LOG_SIZE_BYTES (10 * 1024 * 1024)
static int s_LogBytesWritten = 0;
static bool s_LogLimitReached = false;
static QFile *s_LoggerFile;
#endif

void logToLoggerStream(QString &message) {
  QMutexLocker lock(&s_LoggerLock);

#ifdef LOG_TO_FILE
  if (s_LogLimitReached) {
    return;
  } else if (s_LogBytesWritten >= MAX_LOG_SIZE_BYTES) {
    s_LoggerStream << "Log size limit reached!";
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    s_LoggerStream << Qt::endl;
#else
    s_LoggerStream << endl;
#endif
    s_LogLimitReached = true;
    return;
  } else {
    s_LogBytesWritten += message.size();
  }
#endif

  s_LoggerStream << message;
  s_LoggerStream.flush();
}

void sdlLogToDiskHandler(void *, int category, SDL_LogPriority priority, const char *message) {
  QString priorityTxt;

  switch (priority) {
  case SDL_LOG_PRIORITY_VERBOSE:
    if (s_SuppressVerboseOutput) {
      return;
    }
    priorityTxt = "Verbose";
    break;
  case SDL_LOG_PRIORITY_DEBUG:
    if (s_SuppressVerboseOutput) {
      return;
    }
    priorityTxt = "Debug";
    break;
  case SDL_LOG_PRIORITY_INFO:
    if (s_SuppressVerboseOutput) {
      return;
    }
    priorityTxt = "Info";
    break;
  case SDL_LOG_PRIORITY_WARN:
    if (s_SuppressVerboseOutput) {
      return;
    }
    priorityTxt = "Warn";
    break;
  case SDL_LOG_PRIORITY_ERROR:
    priorityTxt = "Error";
    break;
  case SDL_LOG_PRIORITY_CRITICAL:
    priorityTxt = "Critical";
    break;
  default:
    priorityTxt = "Unknown";
    break;
  }

  QTime logTime = QTime::fromMSecsSinceStartOfDay(s_LoggerTime.elapsed());
  QString txt = QString("%1 - SDL %2 (%3): %4\n").arg(logTime.toString()).arg(priorityTxt).arg(category).arg(message);

  logToLoggerStream(txt);
}

void qtLogToDiskHandler(QtMsgType type, const QMessageLogContext &, const QString &msg) {
  QString typeTxt;

  switch (type) {
  case QtDebugMsg:
    if (s_SuppressVerboseOutput) {
      return;
    }
    typeTxt = "Debug";
    break;
  case QtInfoMsg:
    if (s_SuppressVerboseOutput) {
      return;
    }
    typeTxt = "Info";
    break;
  case QtWarningMsg:
    if (s_SuppressVerboseOutput) {
      return;
    }
    typeTxt = "Warning";
    break;
  case QtCriticalMsg:
    typeTxt = "Critical";
    break;
  case QtFatalMsg:
    typeTxt = "Fatal";
    break;
  }

  QTime logTime = QTime::fromMSecsSinceStartOfDay(s_LoggerTime.elapsed());
  QString txt = QString("%1 - Qt %2: %3\n").arg(logTime.toString()).arg(typeTxt).arg(msg);

  logToLoggerStream(txt);
}

#ifdef HAVE_FFMPEG

void ffmpegLogToDiskHandler(void *ptr, int level, const char *fmt, va_list vl) {
  char lineBuffer[1024];
  static int printPrefix = 1;

  if ((level & 0xFF) > av_log_get_level()) {
    return;
  } else if ((level & 0xFF) > AV_LOG_WARNING && s_SuppressVerboseOutput) {
    return;
  }

  // We need to use the *previous* printPrefix value to determine whether to
  // print the prefix this time. av_log_format_line() will set the printPrefix
  // value to indicate whether the prefix should be printed *next time*.
  bool shouldPrefixThisMessage = printPrefix != 0;

  av_log_format_line(ptr, level, fmt, vl, lineBuffer, sizeof(lineBuffer), &printPrefix);

  if (shouldPrefixThisMessage) {
    QTime logTime = QTime::fromMSecsSinceStartOfDay(s_LoggerTime.elapsed());
    QString txt = QString("%1 - FFmpeg: %2").arg(logTime.toString()).arg(lineBuffer);
    logToLoggerStream(txt);
  } else {
    QString txt = QString(lineBuffer);
    logToLoggerStream(txt);
  }
}

#endif

#endif

#ifdef Q_OS_WIN32
#define WIN32_LEAN_AND_MEAN
#include <DbgHelp.h>
#include <Windows.h>

static UINT s_HitUnhandledException = 0;

LONG WINAPI UnhandledExceptionHandler(struct _EXCEPTION_POINTERS *ExceptionInfo) {
  // Only write a dump for the first unhandled exception
  if (InterlockedCompareExchange(&s_HitUnhandledException, 1, 0) != 0) {
    return EXCEPTION_CONTINUE_SEARCH;
  }

  WCHAR dmpFileName[MAX_PATH];
  swprintf_s(dmpFileName, L"%ls\\Gleam-%I64u.dmp", (PWCHAR)QDir::toNativeSeparators(Path::getLogDir()).utf16(),
             QDateTime::currentSecsSinceEpoch());
  QString qDmpFileName = QString::fromUtf16((const char16_t *)dmpFileName);
  HANDLE dumpHandle =
      CreateFileW(dmpFileName, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (dumpHandle != INVALID_HANDLE_VALUE) {
    MINIDUMP_EXCEPTION_INFORMATION info;

    info.ThreadId = GetCurrentThreadId();
    info.ExceptionPointers = ExceptionInfo;
    info.ClientPointers = FALSE;

    DWORD typeFlags = MiniDumpWithIndirectlyReferencedMemory | MiniDumpIgnoreInaccessibleMemory |
                      MiniDumpWithUnloadedModules | MiniDumpWithThreadInfo;

    if (MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), dumpHandle, (MINIDUMP_TYPE)typeFlags, &info,
                          nullptr, nullptr)) {
      qCritical() << "Unhandled exception! Minidump written to:" << qDmpFileName;
    } else {
      qCritical() << "Unhandled exception! Failed to write dump:" << GetLastError();
    }

    CloseHandle(dumpHandle);
  } else {
    qCritical() << "Unhandled exception! Failed to open dump file:" << qDmpFileName << "with error" << GetLastError();
  }

  // Let the program crash and WER collect a dump
  return EXCEPTION_CONTINUE_SEARCH;
}

#endif

int DEFAULT_HTTP_PORT = LiGetPortFromPortFlagIndex(ML_PORT_INDEX_TCP_47989);  // 47989
int DEFAULT_HTTPS_PORT = LiGetPortFromPortFlagIndex(ML_PORT_INDEX_TCP_47984); // 47984

std::string urlParse(QString &szUrl) {
  std::string szReqHostUrl = szUrl.toStdString();

  std::regex reg("https://");
  szReqHostUrl = std::regex_replace(szReqHostUrl, reg, "");
  std::string_view szView = szReqHostUrl;
  int npos = szView.find("/");
  if (npos != std::string::npos) {
    szReqHostUrl = std::string(szView, 0, npos);
  } else {
    return std::string();
  }

  return szReqHostUrl;
}

void EncyptCheck(bool bEncrypty, std::string &HttpJsonStr, std::string &client_UUid, std::string szPubPriKey[2]) {
  bool bNew = false;
  std::vector<std::string> vUrl;
  client_UUid = generateRSAKey(szPubPriKey, vUrl, bNew); // create public key private key...
  if (bEncrypty) {

    if (client_UUid.size() != 32) {
#ifdef Q_OS_WIN32
      char szExc[258] = {0};
      sprintf_s(szExc, "Tip.exe \"The file is damaged,Please reinstall!");
      WinExec(szExc, SW_SHOW);
#else
      std::string ExcuteStr = "open -a Tip.app --args \"The file is damaged,Please reinstall!\"";
      qDebug() << "ExcuteStr = " << ExcuteStr;
      FILE *handle = popen(ExcuteStr.c_str(), "w");
      if (handle) {
        pclose(handle);
      }
#endif
      exit(0);
    }

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "client_UUid : [%s]", client_UUid.c_str());

    QString szUrl;
    QString strtoken;
    QJsonDocument retJson;
    std::string body =
        KeyParse(HttpJsonStr.c_str(), client_UUid.c_str(), szPubPriKey[0].c_str(), szUrl, strtoken, retJson);

    std::string szReqHostUrl = urlParse(szUrl);
    bool bSave = true;
    if (szReqHostUrl.size() > 0) {
      for (int i = 0; i < vUrl.size(); ++i) {
        if (vUrl[i] == szReqHostUrl) {
          bSave = false;
          break;
        }
      }
    }
    if (true == bNew || true == bSave) {
      std::string requstStr = "v1/client/instance/secret";
      NvReqHTTPS m_ReqHttps;
      int nRet = 0;
      std::string szStrUrl = szUrl.toStdString();
      int npos = szStrUrl.find("/v1");
      if (npos == std::string::npos) {
#ifdef Q_OS_WIN32
        char szExc[258] = {0};
        sprintf_s(szExc, "Tip.exe \"Client loading error,try agin!");
        WinExec(szExc, SW_SHOW);
#else
        std::string ExcuteStr = "open -a Tip.app --args \"Client loading error,try agin!\"";
        qDebug() << "ExcuteStr = " << ExcuteStr;
        FILE *handle = popen(ExcuteStr.c_str(), "w");
        if (handle) {
          pclose(handle);
        }
#endif
        exit(0);
      } else {
        szStrUrl[npos] = 0;
      }
      char newStr[256] = {0};
      memset(newStr, 0, sizeof(newStr));
      sprintf(newStr, "%s/%s", szStrUrl.c_str(), requstStr.c_str());
      szUrl = newStr;

      if (client_UUid.size() > 0) {
        qDebug() << "szUrl " << szUrl;
        qDebug() << "retJson " << retJson;
        std::string str = m_ReqHttps.RequestByToken(szUrl, strtoken, retJson, 3000, nRet);
        if (nRet != 0) {
          qDebug() << "post uuid failed " << client_UUid;
          qDebug() << "request str =  " << str;
#ifdef Q_OS_WIN32
          char szExc[258] = {0};
          sprintf_s(szExc, "Tip.exe \"Client loading error,contact administrator.");
          if (201 == nRet) {
            sprintf_s(szExc, "Tip.exe \"Client loading error,try again later!");
          }
          WinExec(szExc, SW_SHOW);
          std::string homePath = QDir::homePath().toStdString();
          std::string cpath = "/AppData/Roaming/Gws/Gleam/config/";
          std::string uudidPath = homePath + cpath + UUID_FILE;
          QFile uuidfile(uudidPath.c_str());
          if (uuidfile.exists()) {
            uuidfile.remove();
          }
#else
          std::string ExcuteStr = "open -a Tip.app --args \"Client loading error,contact administrator.\"";
          if (201 == nRet) {
            ExcuteStr = "open -a Tip.app --args \"Client loading error,try again later!\"";
          }
          qDebug() << "ExcuteStr = " << ExcuteStr;
          FILE *handle = popen(ExcuteStr.c_str(), "w");
          if (handle) {
            pclose(handle);
          }
#endif
          exit(0);
        } else {
          vUrl.push_back(szReqHostUrl);
          qDebug() << "reqest sucess " << client_UUid;
          if (bSave) {
            WriteXml(client_UUid, vUrl);
            qDebug() << "xml sucess";
          }
        }
      }
    }
  }
}

std::string PostClientResolution(std::string &HttpJsonStr, std::string &guid, std::string &szFingerprint,
                                 std::string &szAgentid) {
  QString szUrl;
  QString strtoken;
  QJsonDocument retJson;
  std::string body = ClientResolutionParse(HttpJsonStr.c_str(), guid.c_str(), szFingerprint.c_str(), szAgentid.c_str(),
                                           szUrl, strtoken, retJson);
  SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "body : [%s]", body.c_str());
  std::string szReqHostUrl = urlParse(szUrl);

  std::string requstStr = "v1/client/instance";
  NvReqHTTPS m_ReqHttps;
  int nRet = 0;
  qDebug() << "parse begin szUrl = " << szUrl;
  std::string szStrUrl = szUrl.toStdString();
  int npos = szStrUrl.find("/v1");
  if (npos == std::string::npos) {
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "PostClientResolution error!  : [%s]", szStrUrl.c_str());
    return "";
  } else {
    szStrUrl[npos] = 0;
  }
  char newStr[256] = {0};
  memset(newStr, 0, sizeof(newStr));
  sprintf(newStr, "%s/%s", szStrUrl.c_str(), requstStr.c_str());
  szUrl = newStr;
  qDebug() << "parse end szUrl = " << szUrl;

  std::string str = m_ReqHttps.RequestByToken(szUrl, strtoken, retJson, 3000, nRet);
  if (nRet != 0) {
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "PostClientResolution error nRet = %d ", nRet);
    return "";
  }
  return str;
}
extern int nRtspStatus;
class MyApplication : public QApplication {
public:
  MyApplication(int &argc, char **argv) : QApplication(argc, argv) {}

protected:
  bool event(QEvent *event) override {
    if (event->type() == QEvent::Close || event->type() == QEvent::Quit) {
#ifdef TARGET_OS_MAC
      // SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "mac MyApplication ");
      SDL_Event event;
      event.type = SDL_QUIT;
      event.quit.timestamp = SDL_GetTicks();
      SDL_PushEvent(&event);
      std::string ExcuteStr;
      if (nRtspStatus == 2) {
        ExcuteStr = "open -a Tip.app ";
      } else if (nRtspStatus > 0) {
        ExcuteStr = "open -a Tip.app --args \"Close Gleam\"";
      }
      qDebug() << "ExcuteStr = " << ExcuteStr;
      FILE *handle = popen(ExcuteStr.c_str(), "w");
      if (handle) {
        pclose(handle);
      }
#else
      SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "windows MyApplication Close");
      // char szExc[258] = { 0 };
      // sprintf_s(szExc, "Tip.exe");
      // WinExec(szExc,SW_SHOW);
#endif
    }

    return QApplication::event(event);
  }
};
static std::string gClientUUid;
std::string getClientUUid() { return gClientUUid; }
int main(int argc, char *argv[]) {
  Restarter restarter(argc, argv);

#ifdef Q_OS_WIN32
  QProcess p1(NULL);
  p1.start("cmd", QStringList() << "/c"
                                << "taskkill /f /im gwscli.exe");
  p1.waitForStarted();
  p1.waitForFinished();
  p1.close();
#else
  QProcess p;
  p.start("pkill", QStringList() << "-f"
                                 << "gwscli");
  p.waitForFinished();
#endif
#if 0
#if defined(Q_OS_WIN32)
    HWND hWnd = ::FindWindow(NULL, TEXT("Gleam"));
    HWND hWnd_2 = ::FindWindow(NULL, TEXT("Gleam-2"));
    if (hWnd != NULL)
    {
        ShowWindow(hWnd, SW_NORMAL);
        SetForegroundWindow(hWnd); //置顶
        if (hWnd_2 != NULL)
        {
            ShowWindow(hWnd_2, SW_NORMAL);
            SetForegroundWindow(hWnd_2); //置顶
        }
        return 0;
    }
#endif
#endif
  bool bSupp = QSslSocket::supportsSsl();
  QString buildVersion = QSslSocket::sslLibraryBuildVersionString();
  QString version = QSslSocket::sslLibraryVersionString();
  QNetworkAccessManager *acce = new QNetworkAccessManager();
  qDebug() << acce->supportedSchemes();
  qDebug() << "SSH = " << bSupp << buildVersion << version;

  char **address = argv;
  // std::string tcpstr = "47984,51369|47989,51370|48010,51371|";
  // std::string udpstr = "47998,51364|47999,51365|48000,51366|48002,51367|48010,51368|";
  std::string tcpstr = "";
  std::string udpstr = "";
  bool bEncrypty = false;
  std::string HttpJsonStr;
  bool bFullscreenflag = false;
  std::string szFingerprint;
  std::string szAgentid;
  for (int i = 0; address && address[i]; i++) {
    if ("--tport1" == std::string(address[i])) {
      tcpstr += std::string(address[i + 1]);
      tcpstr += "|";
    } else if ("--tport2" == std::string(address[i])) {
      tcpstr += std::string(address[i + 1]);
      tcpstr += "|";
    } else if ("--tport3" == std::string(address[i])) {
      tcpstr += std::string(address[i + 1]);
      tcpstr += "|";
    } else if ("--uport1" == std::string(address[i])) {
      udpstr += std::string(address[i + 1]);
      udpstr += "|";
    } else if ("--uport2" == std::string(address[i])) {
      udpstr += std::string(address[i + 1]);
      udpstr += "|";
    } else if ("--uport3" == std::string(address[i])) {
      udpstr += std::string(address[i + 1]);
      udpstr += "|";
    } else if ("--uport4" == std::string(address[i])) {
      udpstr += std::string(address[i + 1]);
      udpstr += "|";
    } else if ("--uport5" == std::string(address[i])) {
      udpstr += std::string(address[i + 1]);
      udpstr += "|";
    } else if ("--uport6" == std::string(address[i])) {
      udpstr += std::string(address[i + 1]);
      udpstr += "|";
    } else if ("--agentid" == std::string(address[i])) {
      szAgentid = std::string(address[i + 1]);
    } else if ("--token" == std::string(address[i])) {
      bEncrypty = true;
    } else if ("--sjson" == std::string(address[i])) {
      HttpJsonStr = std::string(address[i + 1]);
    } else if ("--fingerprint" == std::string(address[i])) {
      szFingerprint = std::string(address[i + 1]);
    }
  }

  if (!tcpstr.empty()) {
    std::vector<std::string> xml_files;
    Stringsplit(tcpstr, '|', xml_files);
    for (int i = 0; i < xml_files.size(); i++) {
      std::vector<std::string> vStrKey;
      Stringsplit(xml_files[i], ',', vStrKey);
      if (vStrKey.size() == 2) {
        // cout << vStrKey[0] << " " << vStrKey[1] << endl;
        int key = getTcpPortIndex(atoi(vStrKey[0].c_str()));
        int value = atoi(vStrKey[1].c_str());
        SetLiGetPortFromPortFlagIndex(key, value);
      } else {
        return 0;
      }
    }
  }

  if (!udpstr.empty()) {
    std::vector<std::string> xml_files;
    Stringsplit(udpstr, '|', xml_files);
    for (int i = 0; i < xml_files.size(); i++) {
      std::vector<std::string> vStrKey;
      Stringsplit(xml_files[i], ',', vStrKey);
      if (vStrKey.size() == 2) {
        // cout << vStrKey[0] << " " << vStrKey[1] << endl;
        int key = getUdpPortIndex(atoi(vStrKey[0].c_str()));
        int value = atoi(vStrKey[1].c_str());
        SetLiGetPortFromPortFlagIndex(key, value);
      } else {
        return 0;
      }
    }
  }

  DEFAULT_HTTP_PORT = LiGetPortFromPortFlagIndex(ML_PORT_INDEX_TCP_47989);  // 47989
  DEFAULT_HTTPS_PORT = LiGetPortFromPortFlagIndex(ML_PORT_INDEX_TCP_47984); // 47984

  SDL_SetMainReady();

  // Set the app version for the QCommandLineParser's showVersion() command
  QApplication::setApplicationVersion(VERSION_STR);

  // Set these here to allow us to use the default QSettings constructor.
  // These also ensure that our cache directory is named correctly. As such,
  // it is critical that these be called before Path::initialize().
  QApplication::setOrganizationName("Gleam Game Streaming Project");
  QApplication::setOrganizationDomain("Gleam-stream.com");
  QApplication::setApplicationName("Gleam");

  if (QFile(QDir::currentPath() + "/portable.dat").exists()) {
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, QDir::currentPath());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, QDir::currentPath());

    // Initialize paths for portable mode
    Path::initialize(true);
  } else {
    // Initialize paths for standard installation
    Path::initialize(false);
  }

#ifdef USE_CUSTOM_LOGGER
#ifdef LOG_TO_FILE
  QDir tempDir(Path::getLogDir());
  s_LoggerFile = new QFile(tempDir.filePath(QString("Gleam-%1.log").arg(QDateTime::currentSecsSinceEpoch())));
  if (s_LoggerFile->open(QIODevice::WriteOnly | QIODevice::Text)) {
    QTextStream(stderr) << "Redirecting log output to " << s_LoggerFile->fileName();
    s_LoggerStream.setDevice(s_LoggerFile);
  }
#endif

  s_LoggerTime.start();
  qInstallMessageHandler(qtLogToDiskHandler);
  SDL_LogSetOutputFunction(sdlLogToDiskHandler, nullptr);

#ifdef HAVE_FFMPEG
  av_log_set_callback(ffmpegLogToDiskHandler);
#endif

#endif

#ifdef Q_OS_WIN32
  // Create a crash dump when we crash on Windows
  SetUnhandledExceptionFilter(UnhandledExceptionHandler);
#endif
  // for(int i = 0;i<argc ;i++)
  {
    //   SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "argv =  %s", argv[i]);
  }
#if defined(Q_OS_WIN32)
  // Force AntiHooking.dll to be statically imported and loaded
  // by ntdll on Win32 platforms by calling a dummy function.
  AntiHookingDummyImport();
#elif defined(Q_OS_LINUX)
  // Force libssl.so to be directly linked to our binary, so
  // linuxdeployqt can find it and include it in our AppImage.
  // QtNetwork will pull it in via dlopen().
  SSL_free(nullptr);
#endif

  // Avoid using High DPI on EGLFS. It breaks font rendering.
  // https://bugreports.qt.io/browse/QTBUG-64377
  //
  // NB: We can't use QGuiApplication::platformName() here because it is only
  // set once the QGuiApplication is created, which is too late to enable High DPI :(
  if (WMUtils::isRunningWindowManager()) {
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    // Enable High DPI support on Qt 5.x. It is always enabled on Qt 6.0
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
#endif

#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    // Enable fractional High DPI scaling on Qt 5.14 and later
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
#endif
  } else {
#ifndef STEAM_LINK
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) {
      qInfo() << "Unable to detect Wayland or X11, so EGLFS will be used by default. Set QT_QPA_PLATFORM to "
                 "override this.";
      qputenv("QT_QPA_PLATFORM", "eglfs");

      if (!qEnvironmentVariableIsSet("QT_QPA_EGLFS_ALWAYS_SET_MODE")) {
        qInfo() << "Setting display mode by default. Set QT_QPA_EGLFS_ALWAYS_SET_MODE=0 to override this.";

        // The UI doesn't appear on RetroPie without this option.
        qputenv("QT_QPA_EGLFS_ALWAYS_SET_MODE", "1");
      }

      if (!QFile("/dev/dri").exists()) {
        qWarning() << "Unable to find a KMSDRM display device!";
        qWarning() << "On the Raspberry Pi, you must enable the 'fake KMS' driver in raspi-config to use Gleam "
                      "outside of the GUI environment.";
      }
    }

    // EGLFS uses OpenGLES 2.0, so we will too. Some embedded platforms may not
    // even have working OpenGL implementations, so GLES is the only option.
    // See https://github.com/Gleam-stream/Gleam-qt/issues/868
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengles2");
#endif
  }

  // This avoids using the default keychain for SSL, which may cause
  // password prompts on macOS.
  qputenv("QT_SSL_USE_TEMPORARY_KEYCHAIN", "1");

#if defined(Q_OS_WIN32) && QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
  if (!qEnvironmentVariableIsSet("QT_OPENGL")) {
    // On Windows, use ANGLE so we don't have to load OpenGL
    // user-mode drivers into our app. OGL drivers (especially Intel)
    // seem to crash Gleam far more often than DirectX.
    qputenv("QT_OPENGL", "angle");
  }
#endif

#if !defined(Q_OS_WIN32) || QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
  // Gleam requires the non-threaded renderer because we depend
  // on being able to control the render thread by blocking in the
  // main thread (and pumping events from the main thread when needed).
  // That doesn't work with the threaded renderer which causes all
  // sorts of odd behavior depending on the platform.
  //
  // NB: Windows defaults to the "windows" non-threaded render loop on
  // Qt 5 and the threaded render loop on Qt 6.
  qputenv("QSG_RENDER_LOOP", "basic");
#endif

  // We don't want system proxies to apply to us
  QNetworkProxyFactory::setUseSystemConfiguration(false);

  // Clear any default application proxy
  QNetworkProxy noProxy(QNetworkProxy::NoProxy);
  QNetworkProxy::setApplicationProxy(noProxy);

  // Register custom metatypes for use in signals
  qRegisterMetaType<NvApp>("NvApp");

  // Allow the display to sleep by default. We will manually use SDL_DisableScreenSaver()
  // and SDL_EnableScreenSaver() when appropriate. This hint must be set before
  // initializing the SDL video subsystem to have any effect.
  SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "1");

  // For SDL backends that support it, use double buffering instead of triple buffering
  // to save a frame of latency. This doesn't matter for MMAL or DRM renderers since they
  // are drawing directly to the screen without involving SDL, but it may matter for other
  // future KMSDRM platforms that use SDL for rendering.
  SDL_SetHint(SDL_HINT_VIDEO_DOUBLE_BUFFER, "1");

  // We use MMAL to render on Raspberry Pi, so we do not require DRM master.
  SDL_SetHint("SDL_KMSDRM_REQUIRE_DRM_MASTER", "0");

  // Use Direct3D 9Ex to avoid a deadlock caused by the D3D device being reset when
  // the user triggers a UAC prompt. This option controls the software/SDL renderer.
  // The DXVA2 renderer uses Direct3D 9Ex itself directly.
  SDL_SetHint("SDL_WINDOWS_USE_D3D9EX", "1");

  if (SDL_InitSubSystem(SDL_INIT_TIMER) != 0) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_InitSubSystem(SDL_INIT_TIMER) failed: %s", SDL_GetError());
    return -1;
  }

#ifdef STEAM_LINK
  // Steam Link requires that we initialize video before creating our
  // QGuiApplication in order to configure the framebuffer correctly.
  if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_InitSubSystem(SDL_INIT_VIDEO) failed: %s", SDL_GetError());
    return -1;
  }
#endif

  // Use atexit() to ensure SDL_Quit() is called. This avoids
  // racing with object destruction where SDL may be used.
  atexit(SDL_Quit);

  // Avoid the default behavior of changing the timer resolution to 1 ms.
  // We don't want this all the time that Gleam is open. We will set
  // it manually when we start streaming.
  SDL_SetHint(SDL_HINT_TIMER_RESOLUTION, "0");

  // Disable minimize on focus loss by default. Users seem to want this off by default.
  SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0");

  // SDL 2.0.12 changes the default behavior to use the button label rather than the button
  // position as most other software does. Set this back to 0 to stay consistent with prior
  // releases of Gleam.
  SDL_SetHint("SDL_GAMECONTROLLER_USE_BUTTON_LABELS", "0");

  // Disable relative mouse scaling to renderer size or logical DPI. We want to send
  // the mouse motion exactly how it was given to us.
  SDL_SetHint("SDL_MOUSE_RELATIVE_SCALING", "0");

  // Set our app name for SDL to use with PulseAudio and PipeWire. This matches what we
  // provide as our app name to libsoundio too. On SDL 2.0.18+, SDL_APP_NAME is also used
  // for screensaver inhibitor reporting.
  SDL_SetHint("SDL_AUDIO_DEVICE_APP_NAME", "Gleam");
  SDL_SetHint("SDL_APP_NAME", "Gleam");

  // We handle capturing the mouse ourselves when it leaves the window, so we don't need
  // SDL doing it for us behind our backs.
  SDL_SetHint("SDL_MOUSE_AUTO_CAPTURE", "0");

#ifdef QT_DEBUG
  // Allow thread naming using exceptions on debug builds. SDL doesn't use SEH
  // when throwing the exceptions, so we don't enable it for release builds out
  // of caution.
  SDL_SetHint(SDL_HINT_WINDOWS_DISABLE_THREAD_NAMING, "0");
#endif

  MyApplication app(argc, argv);

  std::string client_UUid;
  std::string szPubPriKey[2];
  EncyptCheck(bEncrypty, HttpJsonStr, client_UUid, szPubPriKey);
  gClientUUid = client_UUid;
  qDebug() << "client_UUid = " << client_UUid;
  qDebug() << "szFingerprint = " << szFingerprint;
#if 0
    if(client_UUid.size() > 0)
    {
        qDebug()<<"PostClientResolution";
        std::string szRet = PostClientResolution(HttpJsonStr,client_UUid,szFingerprint,szAgentid);
        qDebug()<<"request config = "<<szRet;
        if(szRet.size() > 0)
        {
            int Choosecount = -1;
            int DisplayMode = -1;
            //std::string json = "{\"choosecount\":\"1\",\"displayMode\":\"0\"}";
            ClientConfigParse(szRet.c_str(),Choosecount,DisplayMode);
            qDebug() << "Choosecount = "<<Choosecount;
            qDebug() << "DisplayMode = "<<DisplayMode;
            if(DisplayMode == 1)
            {
                bFullscreenflag = true;
            }

            qDebug() << "bFullscreenflag = "<<bFullscreenflag;
        }
    }
#endif
  engine = new QQmlApplicationEngine();
  QApplication::exit(0);
  GlobalCommandLineParser parser;
  GlobalCommandLineParser::ParseResult commandLineParserResult = parser.parse(app.arguments());
  switch (commandLineParserResult) {
  case GlobalCommandLineParser::ListRequested:
#ifdef USE_CUSTOM_LOGGER
    // Don't log to the console since it will jumble the command output
    s_SuppressVerboseOutput = true;
#endif
#ifdef Q_OS_WIN32
    // Attach to the console to be able to print output.
    // Since we're a /SUBSYSTEM:WINDOWS app, we won't be attached by default.
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
      HANDLE conOut = GetStdHandle(STD_OUTPUT_HANDLE);
      if (conOut != INVALID_HANDLE_VALUE && conOut != NULL) {
        freopen("CONOUT$", "w", stdout);
        setvbuf(stdout, NULL, _IONBF, 0);
      }
      HANDLE conErr = GetStdHandle(STD_ERROR_HANDLE);
      if (conErr != INVALID_HANDLE_VALUE && conErr != NULL) {
        freopen("CONOUT$", "w", stderr);
        setvbuf(stderr, NULL, _IONBF, 0);
      }
    }
#endif
    break;
  default:
    break;
  }

  SDL_version compileVersion;
  SDL_VERSION(&compileVersion);
  SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Compiled with SDL %d.%d.%d", compileVersion.major, compileVersion.minor,
              compileVersion.patch);

  SDL_version runtimeVersion;
  SDL_GetVersion(&runtimeVersion);
  SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Running with SDL %d.%d.%d", runtimeVersion.major, runtimeVersion.minor,
              runtimeVersion.patch);

  // SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,"proxy tcp port %s",tcpstr.c_str());

  // SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,"proxy udp port %s",udpstr.c_str());

  for (int i = ML_PORT_INDEX_TCP_47984; i < ML_PORT_MAX; i++) {
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Gleam map port %d", LiGetPortFromPortFlagIndex(i));
  }
  // Apply the initial translation based on user preference
  StreamingPreferences prefs;
  prefs.retranslate();

  // Trickily declare the translation for dialog buttons
  QApplication::translate("QPlatformTheme", "&Yes");
  QApplication::translate("QPlatformTheme", "&No");
  QApplication::translate("QPlatformTheme", "OK");
  QApplication::translate("QPlatformTheme", "Help");
  QApplication::translate("QPlatformTheme", "Cancel");

  // After the QGuiApplication is created, the platform stuff will be initialized
  // and we can set the SDL video driver to match Qt.
  if (WMUtils::isRunningWayland() && QGuiApplication::platformName() == "xcb") {
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Detected XWayland. This will probably break hardware decoding! Try "
                                              "running with QT_QPA_PLATFORM=wayland or switch to X11.");
    qputenv("SDL_VIDEODRIVER", "x11");
  } else if (QGuiApplication::platformName().startsWith("wayland")) {
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Detected Wayland");
    qputenv("SDL_VIDEODRIVER", "wayland");
  }

#ifdef STEAM_LINK
  // Qt 5.9 from the Steam Link SDK is not able to load any fonts
  // since the Steam Link doesn't include any of the ones it looks
  // for. We know it has NotoSans so we will explicitly ask for that.
  if (app.font().family().isEmpty()) {
    qWarning() << "SL HACK: No default font - using NotoSans";

    QFont fon("NotoSans");
    app.setFont(fon);
  }

  // Move the mouse to the bottom right so it's invisible when using
  // gamepad-only navigation.
  QCursor().setPos(0xFFFF, 0xFFFF);
#elif !SDL_VERSION_ATLEAST(2, 0, 11) && defined(Q_OS_LINUX) && (defined(__arm__) || defined(__aarch64__))
  if (qgetenv("SDL_VIDEO_GL_DRIVER").isEmpty() && QGuiApplication::platformName() == "eglfs") {
    // Look for Raspberry Pi GLES libraries. SDL 2.0.10 and earlier needs some help finding
    // the correct libraries for the KMSDRM backend if not compiled with the RPI backend enabled.
    if (SDL_LoadObject("libbrcmGLESv2.so") != nullptr) {
      qputenv("SDL_VIDEO_GL_DRIVER", "libbrcmGLESv2.so");
    } else if (SDL_LoadObject("/opt/vc/lib/libbrcmGLESv2.so") != nullptr) {
      qputenv("SDL_VIDEO_GL_DRIVER", "/opt/vc/lib/libbrcmGLESv2.so");
    }
  }
#endif

#ifndef Q_OS_DARWIN
  // Set the window icon except on macOS where we want to keep the
  // modified macOS 11 style rounded corner icon.
  app.setWindowIcon(QIcon("Gleam.ico"));
#endif

  // This is necessary to show our icon correctly on Wayland
  app.setDesktopFileName("com.Gleam_stream.Gleam.desktop");
  qputenv("SDL_VIDEO_WAYLAND_WMCLASS", "com.Gleam_stream.Gleam");
  qputenv("SDL_VIDEO_X11_WMCLASS", "com.Gleam_stream.Gleam");

  // Register our C++ types for QML
  qmlRegisterType<ComputerModel>("ComputerModel", 1, 0, "ComputerModel");
  qmlRegisterType<AppModel>("AppModel", 1, 0, "AppModel");
  qmlRegisterUncreatableType<Session>("Session", 1, 0, "Session", "Session cannot be created from QML");
  qmlRegisterSingletonType<ComputerManager>(
      "ComputerManager", 1, 0, "ComputerManager",
      [](QQmlEngine *, QJSEngine *) -> QObject * { return new ComputerManager(); });
  qmlRegisterSingletonType<AutoUpdateChecker>(
      "AutoUpdateChecker", 1, 0, "AutoUpdateChecker",
      [](QQmlEngine *, QJSEngine *) -> QObject * { return new AutoUpdateChecker(); });
  qmlRegisterSingletonType<SystemProperties>(
      "SystemProperties", 1, 0, "SystemProperties",
      [](QQmlEngine *, QJSEngine *) -> QObject * { return new SystemProperties(); });
  qmlRegisterSingletonType<SdlGamepadKeyNavigation>(
      "SdlGamepadKeyNavigation", 1, 0, "SdlGamepadKeyNavigation",
      [](QQmlEngine *, QJSEngine *) -> QObject * { return new SdlGamepadKeyNavigation(); });
  qmlRegisterSingletonType<StreamingPreferences>(
      "StreamingPreferences", 1, 0, "StreamingPreferences",
      [](QQmlEngine *qmlEngine, QJSEngine *) -> QObject * { return new StreamingPreferences(qmlEngine); });

  // Create the identity manager on the main thread
  IdentityManager::get();

#ifndef Q_OS_WINRT
  // Use the dense material dark theme by default
  if (!qEnvironmentVariableIsSet("QT_QUICK_CONTROLS_STYLE")) {
    qputenv("QT_QUICK_CONTROLS_STYLE", "Material");
  }
#else
  // Use universal dark on WinRT
  if (!qEnvironmentVariableIsSet("QT_QUICK_CONTROLS_STYLE")) {
    qputenv("QT_QUICK_CONTROLS_STYLE", "Universal");
  }
#endif
  if (!qEnvironmentVariableIsSet("QT_QUICK_CONTROLS_MATERIAL_THEME")) {
    qputenv("QT_QUICK_CONTROLS_MATERIAL_THEME", "Dark");
  }
  if (!qEnvironmentVariableIsSet("QT_QUICK_CONTROLS_MATERIAL_ACCENT")) {
    qputenv("QT_QUICK_CONTROLS_MATERIAL_ACCENT", "Purple");
  }
  if (!qEnvironmentVariableIsSet("QT_QUICK_CONTROLS_MATERIAL_VARIANT")) {
    qputenv("QT_QUICK_CONTROLS_MATERIAL_VARIANT", "Dense");
  }
  if (!qEnvironmentVariableIsSet("QT_QUICK_CONTROLS_UNIVERSAL_THEME")) {
    qputenv("QT_QUICK_CONTROLS_UNIVERSAL_THEME", "Dark");
  }

  QString initialView;
  bool hasGUI = true;

  switch (commandLineParserResult) {
  case GlobalCommandLineParser::NormalStartRequested:
    initialView = "qrc:/gui/PcView.qml";
    break;
  case GlobalCommandLineParser::StreamRequested: {
    initialView = "qrc:/gui/CliStartStreamSegue.qml";
    StreamingPreferences *preferences = new StreamingPreferences(&app);
    StreamCommandLineParser streamParser;
    streamParser.parse(app.arguments(), preferences);
    QString host = streamParser.getHost();
    QString appName = streamParser.getAppName();
    auto launcher = new CliStartStream::Launcher(host, appName, preferences, &app);
    engine->rootContext()->setContextProperty("launcher", launcher);
    break;
  }
  case GlobalCommandLineParser::QuitRequested: {
    initialView = "qrc:/gui/CliQuitStreamSegue.qml";
    QuitCommandLineParser quitParser;
    quitParser.parse(app.arguments());
    auto launcher = new CliQuitStream::Launcher(quitParser.getHost(), &app);
    engine->rootContext()->setContextProperty("launcher", launcher);
    break;
  }
  case GlobalCommandLineParser::PairRequested: {
    initialView = "qrc:/gui/CliPair.qml";
    PairCommandLineParser pairParser;
    pairParser.parse(app.arguments());

    QString szUrl = pairParser.getHost();
    QString strtoken = pairParser.getToken();
    auto launcher = new CliPair::Launcher(pairParser.getHost(), pairParser.getPredefinedPin(),
                                          pairParser.getInstanceId(), pairParser.getToken(), pairParser.getHttpsJson(),
                                          pairParser.getArch(), pairParser.getPairHost(), client_UUid.c_str(),
                                          szPubPriKey[1].c_str(), bFullscreenflag, &app);

    engine->rootContext()->setContextProperty("launcher", launcher);
    break;
  }
  case GlobalCommandLineParser::ListRequested: {
    ListCommandLineParser listParser;
    listParser.parse(app.arguments());
    auto launcher = new CliListApps::Launcher(listParser.getHost(), listParser, &app);
    launcher->execute(new ComputerManager(&app));
    hasGUI = false;
    break;
  }
  }

  if (hasGUI) {
    engine->rootContext()->setContextProperty("initialView", initialView);

    // Load the main.qml file
    engine->load(QUrl(QStringLiteral("qrc:/gui/main.qml")));
    if (engine->rootObjects().isEmpty())
      return -1;
  }

  int err = app.exec();

  // Give worker tasks time to properly exit. Fixes PendingQuitTask
  // sometimes freezing and blocking process exit.
  QThreadPool::globalInstance()->waitForDone(1000);
  delete engine;
  qDebug() << "err =  " << err;
  if (err == emExitRestart::RETCODE_RESTART) {
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "main exit = %d", err);
    return restarter.restart(err);
  }
  return err;
}
