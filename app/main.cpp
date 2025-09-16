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
#include <QQuickStyle>
#include <QQuickWidget>
#include <QtDebug>
#include <QStandardPaths>
#include <QFileDialog>
#include <regex>
#include <sstream>
#include <QString>
#include <QWebEngineProfile>
#include <QMessageBox>
#include <QThreadPool>
#include <cstdlib>
#include "gwscli.h"
#include "singleInstanceGuard.h"
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

#include "backend/computermanager.h"
#include "backend/nvpairingmanager.h"
#include "backend/systemproperties.h"
#include "cli/commandlineparser.h"
#include "cli/listapps.h"
#include "cli/pair.h"
#include "cli/quitstream.h"
#include "cli/startstream.h"

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
#include <QQuickView>
#include <QString>
#include <QDesktopServices>
#include "streaming/connection.h"

#include "Core/sessionui.h"
#include "Core/connectionmanager.h"
#include "Core/settingWebView.h"
#include "Core/helpWebView.h"
#include "Core/potalInteractionManager.h"
#include "Core/IpFetcher.h"
#include "fileuploader.h"
#include "EventReporter.h"

#ifdef Q_OS_WIN
#include <Windows.h>
#endif

#include <csignal>



void ErrorMessageBox(QString str)
{
    QMessageBox::critical(nullptr, // 父窗口，这里使用nullptr表示没有父窗口
                          "Error", // 对话框标题
                          str, // 显示的文本
                          QMessageBox::Ok, // 按钮，这里是只有一个OK按钮
                          QMessageBox::NoButton // 默认按钮设置为无
                          );

}
//CTipsWebView *g_TipView = nullptr;
bool bCmdEnter = false;
template<typename T>
class Singleton {
public:
    static T *getInstance() {
        static T instance;
        return &instance;
    }

    Singleton(const Singleton&) = delete;
    Singleton& operator=(const Singleton&) = delete;

protected:
    Singleton() {}
    ~Singleton() {}
};

using ComputerSingleton = Singleton<ComputerManager>;
extern PopupWindow *g_pConfigPopup;

void ShowSetting() {
  if (g_pConfigPopup) {
    g_pConfigPopup->hide();
    g_pConfigPopup->Resize();
    g_pConfigPopup->Showpage();
    g_pConfigPopup->show();
    g_pConfigPopup->showNormal();
    g_pConfigPopup->raise();
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
static bool gDebugOutLog = false;
#ifdef LOG_TO_FILE
// Max log file size of 10 MB
#define MAX_LOG_SIZE_BYTES (10 * 1024 * 1024)
static int s_LogBytesWritten = 0;
static bool s_LogLimitReached = false;
static QFile *s_LoggerFile;
#endif
QString getLogDirectoryPath() {
    QString directoryPath;
        if (!s_LoggerFile) {
            qWarning() << "Logger is not initialized.";
            return directoryPath;
        }

        // 获取日志文件的路径
        QString filePath = s_LoggerFile->fileName();

        // 获取文件信息
        QFileInfo fileInfo(filePath);
        if (!fileInfo.exists()) {
            qWarning() << "Log file does not exist:" << filePath;
            return directoryPath;
        }

        // 获取文件所在目录
        directoryPath = fileInfo.absoluteDir().absolutePath();
        return directoryPath;
}

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
    if(!gDebugOutLog)
    {
        return ;
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
    if(!gDebugOutLog)
    {
        return ;
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

#else
QString getLogDirectoryPath() {
    return QDir::currentPath();
}

#endif

// 打开日志文件所在的文件夹
void openLogFileDirectory() {
    // 获取文件所在目录
    QString directoryPath = getLogDirectoryPath();

    // 打开文件夹
    QUrl url = QUrl::fromLocalFile(directoryPath);
    if (!QDesktopServices::openUrl(url)) {
        qWarning() << "Failed to open directory:" << directoryPath;
    }
}

QStringList selectLogFile() {
    QStringList selectedFiles;

    // 获取文件所在目录
    QString directoryPath = getLogDirectoryPath();

    QFileDialog dialog(nullptr, "Select Log Files", directoryPath, "Log Files (*.log)");
    dialog.setFileMode(QFileDialog::ExistingFiles);
    // 自定义按钮文案
    dialog.setLabelText(QFileDialog::Accept, "Upload");
    dialog.setLabelText(QFileDialog::Reject, "Close");


    if(dialog.exec()) {
        selectedFiles = dialog.selectedFiles();
        qDebug() << "select File : " << selectedFiles;
    } else {
        qWarning() << "select no File ";
    }
    return selectedFiles;

}

QString gQToken;
QString gHost;
void uploadLogMessageBox(const QString &title, const QString &text, QMessageBox::Icon &&icon) {
    QWidget *parent = QApplication::activeWindow();

    QMessageBox msgBox(parent);
    msgBox.setWindowTitle(title);
    msgBox.setText(text);
    msgBox.setIcon(icon);

    // 强制置顶窗口
    msgBox.setWindowFlags(msgBox.windowFlags() | Qt::WindowStaysOnTopHint);

    msgBox.exec();
}

void uploadLogFile() {
    QStringList uploadFiles = selectLogFile();
    if (uploadFiles.isEmpty()) {
        return;
    }

        // 后续操作（上传临时目录中的压缩包）
        FileUploader uploader;

        QObject::connect(&uploader, &FileUploader::uploadFinished, [](bool success, const QString &response){
          if (success) {
            uploadLogMessageBox("Success", "File uploaded successfully!", QMessageBox::Information);
          } else {
            uploadLogMessageBox("Error", "Upload failed!", QMessageBox::Critical);
          }
        });

        QObject::connect(&uploader, &FileUploader::uploadError,  [](const QString &error){
            uploadLogMessageBox("Error", error, QMessageBox::Critical);
        });
        static const QString uploadUrl = "https://"+ gHost +"/gws/v1/users/client-log/upload";
        uploader.uploadFiles(uploadFiles, uploadUrl, gQToken);
}

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
  auto npos = szView.find("/");
  if (npos != std::string::npos) {
    szReqHostUrl = std::string(szView, 0, npos);
  } else {
    return std::string();
  }

  return szReqHostUrl;
}

bool EncyptCheck(bool bEncrypty, std::string &HttpJsonStr, std::string &client_UUid, std::string szPubPriKey[2]) {
  bool bNew = false;
  std::vector<std::string> vUrl;
  client_UUid = generateRSAKey(szPubPriKey, vUrl, bNew); // create public key private key...
  qInfo() << "EncyptCheck = " << bEncrypty;
  if (bEncrypty) {
    if (client_UUid.size() != 32) {
        ErrorMessageBox("The file is damaged,Please reinstall!");
        return false;
    }
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "client_UUid : [%s]", client_UUid.c_str());
    QString szUrl;
    QString strtoken;
    QJsonDocument retJson;
    std::string body = KeyParse(HttpJsonStr.c_str(), client_UUid.c_str(), szPubPriKey[0].c_str(), szUrl, strtoken, retJson);
    std::string szReqHostUrl = urlParse(szUrl);
    bool bSave = true;
    if (szReqHostUrl.size() > 0) {
        for (std::vector<std::string>::size_type i = 0; i < vUrl.size(); ++i) {
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
      auto npos = szStrUrl.find("/v1");
      if (npos == std::string::npos) {
        ErrorMessageBox("Portal access denied, please refresh the page and login again!");
          return false;
      } else {
        szStrUrl[npos] = 0;
      }
      char newStr[256] = {0};
      memset(newStr, 0, sizeof(newStr));
      sprintf(newStr, "%s/%s", szStrUrl.c_str(), requstStr.c_str());
      szUrl = newStr;
      if (client_UUid.size() > 0) {
        qInfo() << "szUrl = " << szUrl;
        qInfo() << "strtoken =" << strtoken;
        std::string str = m_ReqHttps.RequestByToken(szUrl, strtoken, retJson, 3000, nRet);
        qInfo() << "request str =  " << str <<"nRet = "<< nRet;
        if (nRet != 0) {

            std::string szTips =  "Client loading error,contact administrator.";
            if (201 == nRet) {
               szTips = "Client loading error,try again later!";
            }
            ErrorMessageBox(szTips.c_str());
            return false;
        } else {
          vUrl.push_back(szReqHostUrl);
          if (bSave) {
            WriteXml(client_UUid, vUrl);
          }
        }
      }
    }
  }
  return true;
}


static std::string gClientUUid;
std::string getClientUUid() { return gClientUUid; }

SingleInstanceGuard guard("Gleam");
void handleAbortSignal(int signal) {
    if (signal == SIGABRT) {
        qWarning() << "Caught SIGABRT signal. Performing cleanup...";
        guard.~SingleInstanceGuard();
        std::exit(EXIT_FAILURE);
    }
}
std::string URL_SCHEME;
#ifdef Q_OS_DARWIN

#include <QFileOpenEvent>

class OpenEventFilter : public QObject
{
public:
    OpenEventFilter(QObject *parent = nullptr) : QObject(parent) {}
protected:
    bool eventFilter(QObject* obj, QEvent* event)
    {
        bool result=false;
        if (event->type() == QEvent::FileOpen)
        {
            QFileOpenEvent* fileEvent = static_cast<QFileOpenEvent*>(event);
            if (!fileEvent->url().isEmpty())
            {
                QString url = fileEvent->url().toString();
                URL_SCHEME = url.toStdString();
                // QMessageBox::information(nullptr, "完整url scheme", url,"确定");
            }
            result=true;
        }
        else
        {
            result = QObject::eventFilter(obj, event);
        }
        return result;
    }
};
#include <sys/types.h>
#include <sys/sysctl.h>
#include <unistd.h>
std::string getArchitecture() {
    int mib[2];
    size_t len;
    char *machine;

    mib[0] = CTL_HW;
    mib[1] = HW_MACHINE;

    // 获取机器名称的长度
    sysctl(mib, 2, NULL, &len, NULL, 0);
    machine = new char[len];

    // 获取机器名称
    sysctl(mib, 2, machine, &len, NULL, 0);
    std::string arch(machine);
    delete[] machine;

    return arch;
}

#endif // Q_OS_DARWIN


QString gQuitMessage;
QString gQReq;
int gBitrateKbpsLimit = 0;
ConnectionReporter *gConnectionReporter = nullptr;
StreamInfoReporter gStreamInfoReporter;
IdleTimeoutReporter gIdleTimeoutReporter;

TimeoutAction gTimeoutAction;

int main(int argc, char *argv[]) {
#ifdef Q_OS_DARWIN
    if (argc == 1) {
        QApplication app(argc, argv);
        OpenEventFilter *openEventFilter= new OpenEventFilter;
        app.installEventFilter(openEventFilter);//这个变量a是默认生成的QApplication a(argc, argv)
        QTimer::singleShot(3000, &app, &QApplication::quit);
        app.exec();
    }
#endif

  QApplication app(argc, argv);

  std::signal(SIGABRT, handleAbortSignal);
  SDL_SetMainReady();
  // Set the app version for the QCommandLineParser's showVersion() command
  QApplication::setApplicationVersion(VERSION_STR);
  // Set these here to allow us to use the default QSettings constructor.
  // These also ensure that our cache directory is named correctly. As such,
  // it is critical that these be called before Path::initialize().
  QApplication::setOrganizationName("Gleam Game Streaming Project");
  QApplication::setOrganizationDomain("Gleam-stream.com");
  QApplication::setApplicationName("Gleam");

  if (guard.anotherInstanceRunning()) {
      qInfo() << "Another instance is already running.";
      ErrorMessageBox("Another Client is already running.");
      return 0;
  }

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
#ifdef Q_OS_WIN32
    // Create a crash dump when we crash on Windows
    SetUnhandledExceptionFilter(UnhandledExceptionHandler);
#endif
#ifdef USE_CUSTOM_LOGGER
#ifdef LOG_TO_FILE
  StreamingPreferences Prefs;
  gDebugOutLog = Prefs.DebugOutLogFlag();

  QString homeDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
  homeDir.append("/GWS/Client/logs");

  QDir folder;
  bool exist = folder.exists(homeDir);
  if(!exist)
  {
      bool ok = folder.mkpath(homeDir);
      if(!ok)
      {
          QTextStream(stderr)  << "create log path " << homeDir << "  error! \n";
      }
      else
      {
          QTextStream(stderr)  << "create  log path" << homeDir << "  sucess! \n";
      }
  }
  QTextStream(stderr) << "log directory: " << homeDir<<"\n" ;
  QDir tempDir(homeDir);
  s_LoggerFile = new QFile(tempDir.filePath(QString("Gleam-%1.log").arg(QDateTime::currentSecsSinceEpoch())));
  if (s_LoggerFile->open(QIODevice::WriteOnly | QIODevice::Text)) {
      QTextStream(stderr) << "Redirecting log output to " << s_LoggerFile->fileName() ;
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
  qRegisterMetaType<Status>("Status");
  qRegisterMetaType<GleamCore::DisplayInfo>("GleamCore::DisplayInfo");
  bool bSupp = QSslSocket::supportsSsl();
  QString buildVersion = QSslSocket::sslLibraryBuildVersionString();
  QString version = QSslSocket::sslLibraryVersionString();
  QNetworkAccessManager *acce = new QNetworkAccessManager();
  qInfo() << acce->supportedSchemes();
  qInfo() << "SSH = " << bSupp << buildVersion << version;

  char **address = argv;
  std::string szRetStr ;
  bool bEncrypty = false;
  std::string HttpJsonStr;
  std::string szFingerprint;
  std::string szAgentid;
  std::string szUrl ;
  std::string szToken;
  QStringList paramList;
  std::string username = "Anonymous";
  std::string watermark = "Disable";
  std::string userID;
// URL_SCHEME = "cesgws:%5C?";
#ifdef Q_OS_DARWIN
  if (argc == 1)
  {
      qInfo()<<"argv[0] = "<<argv[0];
      qInfo() << "URL_SCHEME : " << URL_SCHEME;
      if(URL_SCHEME.empty()) {
          ErrorMessageBox("No parameters were obtained!");
          return 0;
      }
  }
#endif
#ifdef Q_OS_WIN32
  if(address && address[1])
  {
      URL_SCHEME = address[1];
  }
#endif
  ConnectionReporter cr;
  gConnectionReporter = &cr;
  if(!URL_SCHEME.empty())
  {
      auto [param, ret] = ParseParam(URL_SCHEME);
      // mac and win portal mode cesgws:\?
      if (ret&& param.Token.size() > 0 && param.Host.size() > 0)
      {
          bEncrypty = true;
          szToken = param.Token;
          szFingerprint = param.Finger;
          szAgentid = param.InstanceId;
          szUrl = param.Host.c_str();
          char szReq[256] = {0};
          std::sprintf(
            szReq,
            "https://%s/gws/v1/agent/%s/connection?client=%s&version=%s",
            param.Host.c_str(),
            param.InstanceId.c_str(),
            param.Client.c_str(),
            VERSION_STR);
          gQReq = szReq;
          gQToken = param.Token.c_str();
          gHost = param.Host.c_str();
          qInfo()<<"QReq = "<<gQReq;
          QJsonDocument retJson;
          NvReqHTTPS m_ReqHttps;
          int nGetAgentDataRet = 0;
          szRetStr = m_ReqHttps.RequestByToken(gQReq, gQToken, retJson, 3000, nGetAgentDataRet,false);
          if (nGetAgentDataRet != 0) {
              QString szError = "Can not connect to ";
              szError = szError.append(param.Host.c_str());
              ErrorMessageBox(szError);
              return 0;
          }
          cr.Init(gHost, param.InstanceId.c_str(), gQToken);
          gStreamInfoReporter.Init(gHost, param.InstanceId.c_str(), gQToken);
          gIdleTimeoutReporter.Init(gHost, param.InstanceId.c_str(), gQToken);

          // QString szRetStr = "{\"ip\":\"192.168.4.100\",\"user\":\"ivenxiao\","
          // "\"password\":null,\"desktop\":null,\"minRequiredClientVersion\":\"3.5.2\","
          // "\"maxRequiredClientVersion\":\"3.5.2\",\"proxies\":[{\"port\":\"47998\",\"protocol\":"
          // "\"UDP\",\"mapping_port\":\"47998\"},{\"port\":\"47999\",\"protocol\":\"UDP\",\"mapping_port\":"
          // "\"47999\"},{\"port\":\"48000\",\"protocol\":\"UDP\",\"mapping_port\":\"48000\"},{\"port\":\"48002\","
          // "\"protocol\":\"UDP\",\"mapping_port\":\"48002\"},{\"port\":\"48001\",\"protocol\":\"UDP\","
          // "\"mapping_port\":\"48001\"},{\"port\":\"48010\",\"protocol\":\"UDP\",\"mapping_port\":\"48010\"},"
          // "{\"port\":\"47984\",\"protocol\":\"TCP\",\"mapping_port\":\"47984\"},{\"port\":\"47989\","
          // "\"protocol\":\"TCP\",\"mapping_port\":\"47989\"},{\"port\":\"48010\",\"protocol\":\"TCP\""
          // ",\"mapping_port\":\"48010\"}],\"username\":\"ivenxiao\"}";
          std::map<std::string,std::string>vTcp;
          std::map<std::string,std::string>vUdp;
          std::string szIp;
          std::pair<std::string, std::string> requiredClientVersion;
          qInfo()<<"szRetStr = "<<szRetStr;
          JsonParse(szRetStr.c_str(),szIp,
            username, watermark,
            userID, requiredClientVersion,
            vTcp, vUdp,
            gBitrateKbpsLimit, gTimeoutAction);

          qInfo()<<"szIp = "<<szIp;
          qInfo() << "gleam version is " << VERSION_STR;
          bool result = checkVersions(requiredClientVersion, VERSION_STR);
          if (!result) {
              QString szErr = "Client version mismatch. Please update to a version between " +
              QString::fromStdString(requiredClientVersion.first) +
                " and " + QString::fromStdString(requiredClientVersion.second);

              QString failedReason = "required " + QString::fromStdString(requiredClientVersion.first) + " and "
                                     + QString::fromStdString(requiredClientVersion.second) + "gleam version is " + VERSION_STR;
              cr.SetStateFailed(ConnectionReporter::VERSION_MISMATCH, failedReason);
              ErrorMessageBox(szErr);
              return 0;
          }

          qInfo() << "Gleam proxy tcp port count: " << vTcp.size() << ", upd port count: " << vUdp.size();

          int key = 0;
          int value = 0;
          for(auto v:vTcp)
          {
              qInfo()<<"Tcp ="<<v.first<< " : "<<v.second;
              if(!isPortOpen(szIp.c_str(),atoi(v.second.c_str())))
              {
                  QString msg = "Can not connect to host tcp port: ";
                  msg.append(v.second.c_str());
                  cr.SetStateFailed(ConnectionReporter::TCP_PORT_CONNECT_FAILD, msg);
                  ErrorMessageBox(msg);
                  return 0;
              }

              if(v.first.size() > 0 && v.second.size()>0)
              {
                  key = getTcpPortIndex(atoi(v.first.c_str()));
                  value = atoi(v.second.c_str());
                  SetLiGetPortFromPortFlagIndex(key, value);
              }
              else
              {
                  QString msg = "Gleam proxy mapping empty tcp port!";
                  cr.SetStateFailed(ConnectionReporter::TCP_PORT_EMPTY_MAPPING, msg);
                  ErrorMessageBox(msg);
                  return 0;
              }

          }
          for(auto v:vUdp)
          {
              qInfo()<<"Udp ="<<v.first<< " : "<<v.second;
              if(v.first.size() > 0 && v.second.size()>0)
              {
                  key = getUdpPortIndex(atoi(v.first.c_str()));
                  value = atoi(v.second.c_str());
                  SetLiGetPortFromPortFlagIndex(key, value);
              }
              else
              {
                  QString msg = "Gleam proxy mapping empty udp port!";
                  cr.SetStateFailed(ConnectionReporter::UDP_PORT_EMPTY_MAPPING, msg);
                  ErrorMessageBox(msg);
                  return 0;
              }
          }

          QString ip = "https://api.ipify.org?format=json";
          PublicIpFetcher fetcher;
          QString publicIp = fetcher.getPublicIp(1000); // Timeout set to 5000 ms
          if (publicIp.isEmpty()) {
              qWarning() << "Failed to fetch public IP.";
          }
          char szbuf[256]= {0};
          std::sprintf(szbuf, "https://%s/gws/v1/agent/%s/client/info", szUrl.c_str(), szAgentid.c_str());
          SessionParam sessionParam;
          sessionParam.endpoint = szbuf;
          sessionParam.token = szToken.c_str();
          sessionParam.clientVersion = VERSION_STR;
          sessionParam.clientIP = publicIp;
          HttpJsonStr = sessionParam.toJson().toStdString();
          QString clientArch;
#ifdef Q_OS_DARWIN
          clientArch =  getArchitecture().c_str();
#endif
          paramList <<"Gleam.exe"
                    << "pair" << szIp.c_str()
                    << "--pin" << GeneratePinString().c_str()
                    << "--sjson" << HttpJsonStr.c_str()
                    << "--agentid" << szAgentid.c_str()
                    << "--token" << szToken.c_str()
                    << "--phost" << szUrl.c_str()
                    << "--fingerprint" << szFingerprint.c_str()
                    << "--arch"<<clientArch;
      }
      else {
          // win command line arguments mode
          paramList = app.arguments();
      }
  }  else {
      // mac command line arguments mode
      paramList = app.arguments();
  }
  DEFAULT_HTTP_PORT = LiGetPortFromPortFlagIndex(ML_PORT_INDEX_TCP_47989);  // 47989
  DEFAULT_HTTPS_PORT = LiGetPortFromPortFlagIndex(ML_PORT_INDEX_TCP_47984); // 47984
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
    QString msg = QString("SDL_InitSubSystem(SDL_INIT_TIMER) failed:") + SDL_GetError();
    cr.SetStateFailed(ConnectionReporter::SDL_INIT_SUBSYSTEM_FAILED, msg);
    ErrorMessageBox("SDL_InitSubSystem(SDL_INIT_TIMER) failed");
    return -1;
  }

#ifdef STEAM_LINK
  // Steam Link requires that we initialize video before creating our
  // QGuiApplication in order to configure the framebuffer correctly.
  if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_InitSubSystem(SDL_INIT_VIDEO) failed: %s", SDL_GetError());
    ErrorMessageBox("SDL_InitSubSystem(SDL_INIT_VIDEO) failed");
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

  std::string client_UUid;
  std::string szPubPriKey[2];
  bool result = EncyptCheck(bEncrypty, HttpJsonStr, client_UUid, szPubPriKey);
  if (!result) {
      QString msg = "EncyptCheck failed";
      cr.SetStateFailed(ConnectionReporter::ENCYPT_CHECK_FAILED, msg);
      ErrorMessageBox(msg);
      return 0;
  }
  gClientUUid = client_UUid;
  qInfo() << "client_UUid = " << client_UUid;

  // Create the identity manager on the main thread
  IdentityManager::get();
  GlobalCommandLineParser parser;
  GlobalCommandLineParser::ParseResult commandLineParserResult = parser.parse(paramList);
  bool bStartPairAndConnect = (commandLineParserResult == GlobalCommandLineParser::PairRequested);
  if (bStartPairAndConnect && true) {
      //We do Session Init first, then ConnectionManager
      //can use Streaming Config and other info from Session
      //Pass 1
      bool bInitOK = SessionUI::I().Init();
      if (!bInitOK)
      {
          QString msg = "init SessionUI failed";
          qInfo() << msg;
          cr.SetStateFailed(ConnectionReporter::INIT_SESSION_UI_FAILED, msg);
          ErrorMessageBox(msg);
          return 0;
      }
      ////Pass 2
      CPortalInteractionManager::I().Start();
      ConnectionManager::I().Start(
          bStartPairAndConnect,
          client_UUid.c_str(),
          username,
          watermark,
          userID,
          szPubPriKey[1].c_str(),
          paramList);

      ////Pass 3 enter Session Main Loop
      SessionUI::I().EnterLoop();
  }
  ConnectionManager::I().Stop();
  QThreadPool::globalInstance()->clear();
  bool allTasksDone = QThreadPool::globalInstance()->waitForDone(1000);
  app.sendPostedEvents(nullptr, QEvent::DeferredDelete);
  app.quit();

  CPortalInteractionManager::I().Stop();
  UIStateMachine::I().Stop();
  SessionUI::I().Close();


  if (gQuitMessage.size() != 0) {
      ErrorMessageBox(gQuitMessage);
  }

  //Todo: avoid quit crash, try to find root cause
  {
    qInfo()<<"quick exit in any case";
    guard.~SingleInstanceGuard();
    cr.~ConnectionReporter();
#if defined(Q_OS_WIN32)
    std::quick_exit(0);
#else
    _exit(0);
#endif
  }
  // Give worker tasks time to properly exit. Fixes PendingQuitTask
  // sometimes freezing and blocking process exit.
  if (!allTasksDone) {
      qInfo()<<"QThreadPool not all task done";
      exit(0);
  }
  qInfo()<<"main end";

  return  0;
}
