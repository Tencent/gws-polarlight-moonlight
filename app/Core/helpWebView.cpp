#include "helpWebView.h"
#include <QDebug>
#include <QUrlQuery>
#include <QWebEngineSettings>
#include "QThreadPool"
#include "sessionui.h"
#define WIDTH 850
#define HEIGH 600

CHelpWebView *g_pHelpWebView = nullptr;

void InitHelpWebView()
{
    qDebug()<<"InitHelpWebView";
    if(g_pHelpWebView == nullptr)
    {
        g_pHelpWebView = new CHelpWebView();

        if(false == g_pHelpWebView->Init())
        {
            qWarning()<<"init HelpWebView failed";
        }
    }
}
void ShowHelpWebView()
{
    if(g_pHelpWebView)
    {
        qDebug()<<"ShowHelpWebView()";
        g_pHelpWebView->Resize();
        g_pHelpWebView->Show();
    }
}

void HideHelpWebView()
{
    if(g_pHelpWebView)
    {
        g_pHelpWebView->Hide();
    }
}
CHelpWebView::CHelpWebView(QWidget* parent) : QInterfaceDialog(parent)
{
    QApplication::instance()->installEventFilter(this);

}

bool CHelpWebView::isDragElement()
{
    return ghelpbridge && ghelpbridge->isTitle();
}

bool CHelpWebView::Init()
{
    setFixedSize(WIDTH, HEIGH);
    setAttribute(Qt::WA_TranslucentBackground);
    setWindowFlags(windowFlags() | Qt::WindowStaysOnTopHint | Qt::FramelessWindowHint);
    QString exePath = QCoreApplication::applicationDirPath();
    QString htmlFilePath;
    htmlFilePath = QDir(exePath).filePath("webpages/keyboard.html");
#ifdef Q_OS_DARWIN
    htmlFilePath = QDir(exePath).filePath("../Resources/webpages/keyboard.html");
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
    if(ghelpbridge == nullptr)
    {
        ghelpbridge = new HelpWebViewBridge(this);
        if(!ghelpbridge)
        {
            return false;
        }
    }

    m_webChannel->registerObject(QStringLiteral("ghelpbridge"), ghelpbridge);
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
                    ghelpbridge.mouseOnTitle(element && element.classList.contains('title-bars'));
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
void CHelpWebView::Resize()
{
    setFixedSize(WIDTH, HEIGH);
}
void CHelpWebView::Hide()
{
    hide();
#ifdef Q_OS_DARWIN
    SessionUI::I().getSession()->m_InputHandler->raiseLastWindow();
#endif
}

void CHelpWebView::Show()
{
    show();
    showNormal();
    raise();
}

void HelpWebViewBridge::Close()
{
    m_pSetting->Hide();
}

void HelpWebViewBridge::mouseOnTitle(bool bTitle)
{
    m_bOnTitle = bTitle;
}







