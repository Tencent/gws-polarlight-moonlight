#include "settingWebView.h"
#include <QDebug>
#include <QUrlQuery>
#include <QWebEngineSettings>
#include <QDesktopServices>
#include "QThreadPool"
#include "sessionui.h"
#define WIDTH 900
#define HEIGH 600
extern std::string getClientUUid();

CSettingWebView *g_pSettingWebView = nullptr;

void InitGleamSettingWebView()
{
    qDebug()<<"InitGleamSettingWebView";
    if(g_pSettingWebView == nullptr)
    {
        g_pSettingWebView = new CSettingWebView();

        if(false == g_pSettingWebView->Init())
        {
            qWarning()<<"init SettingWebView failed";
        }
    }
}
void ShowGleamWebViewSetting()
{
    if(g_pSettingWebView)
    {
        qDebug()<<"ShowConfigSetting()";
        g_pSettingWebView->Resize();
        g_pSettingWebView->Show();
    }
}

void HideGleamWebViewSetting()
{
    if(g_pSettingWebView)
    {
        g_pSettingWebView->Hide();
    }
}
CSettingWebView::CSettingWebView(QWidget* parent) : QInterfaceDialog(parent)
{
    QApplication::instance()->installEventFilter(this);
}

void CSettingWebView::CallHtmlFunction(QString &str)
{
    if(!str.isEmpty())
    {
        QString js = QString("CallFunc(%1)").arg(str);
        qDebug()<<"CallHtmlFunction js = "<<js;
        m_WebView->page()->runJavaScript(js, [&](const QVariant &v)
                                        {
                                            QString retStr = v.toString();
                                        });
    }
}
bool CSettingWebView::Init()
{
    resize(WIDTH, HEIGH);
    setAttribute(Qt::WA_TranslucentBackground);
    setWindowFlags(windowFlags() | Qt::WindowStaysOnTopHint | Qt::FramelessWindowHint);
    QString exePath = QCoreApplication::applicationDirPath();
    QString htmlFilePath;
    htmlFilePath = QDir(exePath).filePath("webpages/setting.html");
#ifdef Q_OS_DARWIN
    htmlFilePath = QDir(exePath).filePath("../Resources/webpages/setting.html");
#endif
    if(m_Layout == nullptr)
    {
        m_Layout = new QVBoxLayout(this);
        if(!m_Layout)
        {
            return false;
        }
    }
    this->setLayout(m_Layout);
    if(m_webChannel == nullptr)
    {
        m_webChannel = new QWebChannel(this);
        if(!m_webChannel)
        {
            return false;
        }
    }
    if(gsetbridge == nullptr)
    {
        gsetbridge = new SetWebViewBridge(this);
        if(!gsetbridge)
        {
            return false;
        }
    }

    m_webChannel->registerObject(QStringLiteral("gsetbridge"), gsetbridge);
    if(m_WebView == nullptr)
    {
        m_WebView = new WebEngineView(this);
        if(!m_WebView)
        {
            return false;
        }
    }
    if(m_WebView)
    {
        m_WebView->page()->setWebChannel(m_webChannel);
        m_Layout->addWidget(m_WebView);
        setLayout(m_Layout);
        connect(m_WebView, &QWebEngineView::loadFinished, [&] {
            m_WebView->page()->runJavaScript(R"(
                document.addEventListener('mousemove', function(event) 
                {
                    var element = document.elementFromPoint(event.clientX, event.clientY);
                    gset.mouseOnTitle(element && element.classList.contains('title-bars'));
                });
            )");
        });
        m_WebView->load(QUrl::fromLocalFile(htmlFilePath));
        m_WebView->page()->settings()->setAttribute(QWebEngineSettings::JavascriptEnabled, true);
        m_WebView->page()->settings()->setAttribute(QWebEngineSettings::JavascriptCanOpenWindows, true);
        m_WebView->page()->settings()->setAttribute(QWebEngineSettings::PluginsEnabled, true);
        m_WebView->show();
    }
    return true;
}
void CSettingWebView::Resize()
{
    setFixedSize(WIDTH, HEIGH);
}
void CSettingWebView::Hide()
{
    hide();
#ifdef Q_OS_DARWIN
    SessionUI::I().getSession()->m_InputHandler->raiseLastWindow();
#endif
}

void CSettingWebView::Show()
{
    GetSettingConfig();
    show();
    showNormal();
    raise();
}
void CSettingWebView::GetSettingConfig()
{
    QString szSetting = m_pSession->getStreamingPreferences()->WebViewSettingToJson();
    CallHtmlFunction(szSetting);
}

void CSettingWebView::SaveSettingConfig(QString str)
{

    m_pSession->getStreamingPreferences()->SaveWebViewSetting(str);
}

bool CSettingWebView::isDragElement()
{
    return gsetbridge && gsetbridge->isTitle();
}

QString SetWebViewBridge::sendMsg(const QString type, const QString &message)
{
    qDebug()<<"SetWebViewBridge::sendMsg";
    qDebug()<<"type = "<<type;
    qDebug()<<"message = "<<message;
    if(!m_pSetting)
    {
        return "";
    }
    //sendMsg type =   "config"
    //sendMsg message =   "{\"Type\":\"Set\"},{\"Type\":\"Get\"}"
    QJsonDocument jsonDoc = QJsonDocument::fromJson(message.toUtf8());
    if (jsonDoc.isObject()) {
        QJsonObject jsonObj = jsonDoc.object();
        if(jsonObj.value("Type").toString() == "Set")
        {
            m_pSetting->SaveSettingConfig(message);
        }
        else  if(jsonObj.value("Type").toString() == "Get")
        {
            m_pSetting->GetSettingConfig();
        }
    }
    return "";
}

void SetWebViewBridge::Close()
{
    m_pSetting->Hide();
}

void SetWebViewBridge::openLogDir()
{
    openLogFileDirectory();
}

void SetWebViewBridge::uploadLog()
{
    uploadLogFile();
}

void SetWebViewBridge::mouseOnTitle(bool bTitle)
{
    m_bOnTitle = bTitle;
}

void SetWebViewBridge::openUrlInExternalBrowser(const QUrl &url)
{
    QDesktopServices::openUrl(url);
}


QString SetWebViewBridge::getVersion() {
    return VERSION_STR;
}



