#include "gleamapi.h"
#include <QDebug>
#include <QDialog>
#include <QWebEngineView>
#include <QVBoxLayout>
#include <QWebChannel>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QUrlQuery>
#include <QPushButton>

#include <QThread>
#include <QThreadPool>
#include <QJsonObject>
#include <QDialog>
#include <QMouseEvent>
#include <QPoint>
#include <QApplication>
#include <QJsonArray>
#include <QWebEngineSettings>
#include "streaming/session.h"
extern std::string getClientUUid();

double MAIN_WINDOW_WIDTH = 820.00;
double MAIN_WINDOW_HIGHT = 1000.00;
QTextStream gOutStream;


#define DEF_WIDTH  1920.00
#define DEF_HEIGHT 1080.00

void ConfigTask::run()
{
    qDebug() << "ConfigTask::run: http.SendMsg = "<< m_szMsg;
    NvHTTP http(m_Session->getNvComputer());
    QString szRet = http.SendMsg(m_szMsg);
    emit SendMsgRet(szRet);
}

CGleamConfigPost::CGleamConfigPost(PopupWindow *parent) : QObject(parent) {
    m_pPopupWindow = parent;
    connect(this, &CGleamConfigPost::SendMsgRetMsg, parent, &PopupWindow::SendMsgRetMsg);
}

void CGleamConfigPost::SendMsgRet(QString text)
{
    emit SendMsgRetMsg(text);
}

void CGleamConfigPost::Close()
{

    m_pPopupWindow->hide();
}

void CGleamConfigPost::log(const QString &str)
{
    qDebug() << "log [" << str<<"]";
}
QString CGleamConfigPost::sendMsg(const QString type ,const QString &message)
{
    //https send to sunshine

    if(!m_pPopupWindow)
    {
        return "";
    }
    Session* pSession = m_pPopupWindow->getSession();
    if(!pSession)
    {
        return "";
    }
    QUrlQuery query;
    query.addQueryItem("message", message);
    query.addQueryItem("type", type);
    query.addQueryItem("client_uuid", getClientUUid().c_str());
    QString szMessage = query.toString(QUrl::FullyEncoded);


    QJsonParseError jsonerror;
    QJsonDocument doc = QJsonDocument::fromJson(message.toLatin1(), &jsonerror);

    qDebug() << "CGleamConfigPost sendMsg = "<< type<< " Message from sunshine:" << message;
    if (!doc.isNull() && jsonerror.error == QJsonParseError::NoError)
    {
        qDebug() << "jsonerror.error doc = "<<doc;
        if(doc.isObject())
        {
            QJsonObject object = doc.object();
            qDebug() << "CreateVirtualDisplay VirtualDisplay :" << object["VirtualDisplay"].toString();
            if(object.contains("ActionType") && object["ActionType"].toString() == "CreateVirtualDisplay")
            {
                if(object.contains("VirtualDisplay"))
                {
                    qDebug() << "CreateVirtualDisplay VirtualDisplay = " <<object["VirtualDisplay"].toInt();
                    if(object["VirtualDisplay"].toInt() == 0)
                    {
                        if(m_pPopupWindow->getSession())
                        {
                            qDebug() << "CreateVirtualDisplay VirtualDisplay 0 set restarting ";
                            m_pPopupWindow->getSession()->SetDisplayCount(1,1);
                            m_pPopupWindow->getSession()->CheckDual();
                        }
                    }
                }
            }
            if(object.contains("ActionType") && object["ActionType"].toString() == "PrivacyModeChange")
            {
                qDebug() << "CreateVirtualDisplay PrivacyMode = " <<object["PrivacyMode"].toString();
                if(object.contains("PrivacyMode")&& object["PrivacyMode"].toString() == "Disabled")
                {

                    if(m_pPopupWindow->getSession())
                    {
                        qDebug() << "PrivacyModeChange PrivacyMode set disable ";
                        m_pPopupWindow->getSession()->SetDisplayCount(1,1);
                        m_pPopupWindow->getSession()->CheckDual();
                    }

                }
            }
            if(object.contains("DisplayChoose"))
            {
                qDebug() << "DisplayChoose is save ";
                if(object["DisplayChoose"].isArray() )
                {
                    QJsonArray array = object["DisplayChoose"].toArray();
                    qDebug() << "array.size() = "<<array.size();

                    m_pPopupWindow->SetDisplayCout(array.size());
                    //if(array.size() >= 2)
                    {
                        if(m_pPopupWindow->getSession())
                        {
                            m_pPopupWindow->getSession()->CheckDual();
                        }
                    }
                }
            }

        }
    }

    ConfigTask* pTask = new ConfigTask(this, pSession,szMessage);
    QThreadPool::globalInstance()->start(pTask);
    return "";

}
void PopupWindow::SendMsgRetMsg(QString text)
{
    qDebug()<<"PopupWindow::SendMsgRetMsg text = "<<text;

    QJsonParseError jsonerror;
    QJsonDocument doc = QJsonDocument::fromJson(text.toLatin1(), &jsonerror);

    if (!doc.isNull() && jsonerror.error == QJsonParseError::NoError)
    {
        if(doc.isObject())
        {
            QJsonObject object = doc.object();
            if(object.contains("ret"))
            {
                if(object["ret"].isString() )
                {
                    QString Ret = object["ret"].toString();
                    if(Ret == "OK")
                    {
                        if(getSession() != nullptr)
                        {
                            qDebug()<<" Ret = OK";
                            getSession()->SetDisplayCount(GetDisplayCout());

                        }
                    }
                }
            }
        }
    }


    QJsonDocument jsonDoc = QJsonDocument::fromJson(text.toUtf8());
    QString jsonString = QString(jsonDoc.toJson(QJsonDocument::Compact));

    QJsonParseError jsonerrorEx;
    QJsonDocument docEx = QJsonDocument::fromJson(jsonString.toLatin1(), &jsonerrorEx);
    int nCount = 0;
    if (!docEx.isNull() && jsonerrorEx.error == QJsonParseError::NoError)
    {
        if(docEx.isArray())
        {
            qDebug() << "PopupWindow::SendMsgRetMsg tex is array";
            QJsonArray jsonArray = doc.array();
            // 遍历数组并打印每个元素
            for (int i = 0; i < jsonArray.size(); ++i)
            {
                QJsonValue locationArray = jsonArray.at(i);
                QJsonObject location = locationArray.toObject();
                if(location.contains("Width")&&location.contains("Height")&&location.contains("Name"))
                {
                    int nHeight = location["Height"].toInt();
                    int nWidth = location["Width"].toInt();
                    if(nHeight > 0 && nWidth > 0)
                    {
                        nCount++;
                    }

                    qDebug()<<nHeight<<" "<<nWidth<<" "<<location["Name"].toString();
                }
            }
            qDebug()<<"nCount = "<<nCount;
            if(getSession() != nullptr)
            {
                getSession()->SetDisplayCount(nCount);
            }
        }
    }

    QString js = QString("CallJs(%1)").arg(jsonString);
    m_WebView->page()->runJavaScript(js, [&](const QVariant &v)
                                    {
                                        QString retStr = v.toString();

                                    });

    qDebug()<<"jsonString = "<<jsonString;
}
void PopupWindow::setSession(Session* Session)
{
    m_Session = Session;
}
Session* PopupWindow::getSession()
{
    return m_Session;
}

PopupWindow::PopupWindow(bool move,QWidget *parent) : QInterfaceDialog(parent)
{
    m_Layout = nullptr;
    m_WebView = nullptr;
    m_webChannel = nullptr;
    gleamPost = nullptr;
    m_Move = move;
    if(move)
    {
        QApplication::instance()->installEventFilter(this);
    }
}

void PopupWindow::Reszie(double width,double height)
{
    int nlcd_width = 1920;
    int nlcd_height = 1080;
    if(width > 0)
    {
        MAIN_WINDOW_WIDTH = width;
    }
    if(height > 0)
    {
        MAIN_WINDOW_HIGHT = height;
    }
    QScreen* screen = QGuiApplication::primaryScreen();
    if (screen)
    {
        nlcd_width = screen->size().width();
        nlcd_height = screen->size().height();
    }
    if(nlcd_width > DEF_WIDTH && nlcd_height > DEF_HEIGHT)
    {
        nlcd_width = DEF_WIDTH;
        nlcd_height = DEF_HEIGHT;
    }
    double dwidth_rate = (MAIN_WINDOW_WIDTH / DEF_WIDTH) * (double)nlcd_width / MAIN_WINDOW_WIDTH;
    double dheight_rate = (MAIN_WINDOW_HIGHT / DEF_HEIGHT) * (double)nlcd_height / MAIN_WINDOW_HIGHT;
    qDebug()<<"nlcd_width=  "<<nlcd_width<<" nlcd_height =  "<<nlcd_height;
    qDebug()<<"MAIN_WINDOW_HIGHT = "<< MAIN_WINDOW_HIGHT * dheight_rate;
#ifdef TARGET_OS_MAC
    resize(MAIN_WINDOW_WIDTH, MAIN_WINDOW_HIGHT);
#endif

#ifdef _WIN64
    int nRealHeight = MAIN_WINDOW_HIGHT * dheight_rate;
    if(nRealHeight > 700)
    {
        resize(MAIN_WINDOW_WIDTH, MAIN_WINDOW_HIGHT * dheight_rate);
    }
    else
    {
        resize(MAIN_WINDOW_WIDTH,  700);
    }

#endif
}
void PopupWindow::init()
{
    Reszie();
    setWindowFlags(Qt::FramelessWindowHint); // Remove the title bar
    setAttribute(Qt::WA_TranslucentBackground); // Optional: Make the window background transparent
    if(!m_Layout)
    {
        m_Layout = new QVBoxLayout(this);
    }

    if(!m_WebView)
    {
        m_WebView = new WebEngineView(this);
    }
    if(!m_webChannel)
    {
        m_webChannel = new QWebChannel(this);
    }
    if(!gleamPost)
    {
        gleamPost = new CGleamConfigPost(this);
        m_webChannel->registerObject(QStringLiteral("gleamPost"), gleamPost);
    }
    m_WebView->page()->setWebChannel(m_webChannel);
    m_Layout->addWidget(m_WebView);

    setLayout(m_Layout);
    QString exeDir = QCoreApplication::applicationDirPath();
#ifdef Q_OS_WIN32
    exeDir.append("\\webpages\\config.html");
#else
    exeDir.append("//webpages//config.html");
#endif
    loadHtml(exeDir);

}
void PopupWindow::loadHtml(QString str)
{
    if(m_WebView)
    {
        m_WebView->load(QUrl::fromLocalFile(str));
        m_WebView->page()->settings()->setAttribute(QWebEngineSettings::JavascriptEnabled, true);
        m_WebView->page()->settings()->setAttribute(QWebEngineSettings::JavascriptCanOpenWindows, true);
        m_WebView->page()->settings()->setAttribute(QWebEngineSettings::PluginsEnabled, true);
        m_WebView->show();
    }
}
void PopupWindow::Showpage()
{
    QString szMonitorinfo;
    GetRelution(szMonitorinfo);
    QString js = QString("Setinfo(%1)").arg("'"+szMonitorinfo+"'");
    m_WebView->page()->runJavaScript(js, [&](const QVariant &v)
                                    {
                                        QString retStr = v.toString();
                                    });

    qDebug()<<"js = "<<js;

}

int PopupWindow::GetRelution(QString &info)
{

    QList<QScreen *> list_screen = QGuiApplication::screens();
    QString  szInfo;
    QString szStr;

    for (int i = 0; i < list_screen.size(); i++)
    {
        //QString szName = list_screen.at(i)->name();
        QRect  rect = list_screen.at(i)->geometry();
        QString rectStr = QString("(%1,%2,%3,%4)|").arg(rect.x()).arg(rect.y()).arg(rect.width()).arg(rect.height());
        szInfo = "display";
        szInfo.append(QString::number(i+1));
        szInfo.append(rectStr);
        szStr.append(szInfo);

    }


    info = szStr;
    //qDebug() << "info = " << info;
    return list_screen.size();
}

///////////////////////////////////
FloatingBall::FloatingBall(QWidget *parent) : QInterfaceDialog(parent)
{
    m_Layout = nullptr;
    m_WebView = nullptr;
    m_webChannel = nullptr;
    grefresh = nullptr;
    QApplication::instance()->installEventFilter(this);

}

void FloatingBall::Reszie()
{
    int nSize = 90;
    int nlcd_width = 1920;
    int nlcd_height = 1080;
    QScreen* screen = QGuiApplication::primaryScreen();
    if (screen)
    {
        nlcd_width = screen->size().width();
        nlcd_height = screen->size().height();
    }
    if(nlcd_width > DEF_WIDTH && nlcd_height > DEF_HEIGHT)
    {
        nlcd_width = DEF_WIDTH;
        nlcd_height = DEF_HEIGHT;
    }
    double dwidth_rate = (nSize / DEF_WIDTH) * (double)nlcd_width / nSize;
    double dheight_rate = (nSize / DEF_HEIGHT) * (double)nlcd_height / nSize;

#ifdef TARGET_OS_MAC
    resize(nSize, MAIN_WINDOW_HIGHT);
#endif

#ifdef _WIN64
    int nRealHeight = nSize * dheight_rate;
    if(nRealHeight > 700)
    {
        resize(nSize, nSize * dheight_rate);
    }
    else
    {
        resize(nSize,  nSize);
    }

#endif
}
void FloatingBall::init()
{
    Reszie();

    setWindowFlags(this->windowFlags()|Qt::FramelessWindowHint); // Remove the title bar
    setAttribute(Qt::WA_TranslucentBackground,true); // Optional: Make the window background transparent
    if(!m_Layout)
    {
        m_Layout = new QVBoxLayout(this);
    }

    if(!m_WebView)
    {
        m_WebView = new WebEngineView(this);
    }
    if(!m_webChannel)
    {
        m_webChannel = new QWebChannel(this);
    }
    if(!grefresh)
    {
        grefresh = new CRefresh(this);
        m_webChannel->registerObject(QStringLiteral("grefresh"), grefresh);
    }
    m_WebView->page()->setWebChannel(m_webChannel);
    m_Layout->addWidget(m_WebView);

    setLayout(m_Layout);

    if(m_WebView)
    {
        QString exeDir = QCoreApplication::applicationDirPath();
#ifdef Q_OS_WIN32
        exeDir.append("\\webpages\\menu.html");
#else
        exeDir.append("//webpages//menu.html");
#endif
        m_WebView->load(QUrl::fromLocalFile(exeDir));
        m_WebView->page()->settings()->setAttribute(QWebEngineSettings::JavascriptEnabled, true);
        m_WebView->page()->settings()->setAttribute(QWebEngineSettings::JavascriptCanOpenWindows, true);
        m_WebView->page()->settings()->setAttribute(QWebEngineSettings::PluginsEnabled, true);
        m_WebView->show();
    }


}

void FloatingBall::OpenOther()
{

    QString js = QString("OpenOther(%1)").arg("'""'");
    m_WebView->page()->runJavaScript(js, [&](const QVariant &v)
                                    {
                                        QString retStr = v.toString();
                                    });

    qDebug()<<"js = "<<js;

}

CRefresh::CRefresh(FloatingBall *parent) : QObject(parent) {
    m_pFloatingBall = parent;
}

extern CWebConfig *g_pConfig2 ;
void CRefresh::RefreshFun(QString type)
{
    if (!g_pConfig2)
    {
        return ;
    }

    g_pConfig2->Reszie(747,90);

    qDebug()<<"Refresh type = "<<type;
    if(g_pConfig2->isHidden())
    {
        g_pConfig2->show();
    }
    else
    {
        g_pConfig2->hide();
    }
    QPoint buttonPos = m_pFloatingBall->pos();
    QRect rect = m_pFloatingBall->rect();
    qDebug()<<" buttonPos = "<< buttonPos;

    QPoint globalMousePos = QCursor::pos();
    qDebug() << "Global Mouse Position: " << globalMousePos;
    g_pConfig2->move(globalMousePos.x(),globalMousePos.y());



}


CWebConfig::CWebConfig(bool move):PopupWindow(move)
{

}

void CWebConfig::init(QString html,double width,double height)
{
    setWindowFlags(Qt::Tool);
    Reszie(width,height);
    this->setContentsMargins(0, 0, 0, 0);
    this->setWindowFlags(Qt::CustomizeWindowHint | Qt::FramelessWindowHint ); // Remove the title bar
    this->setAttribute(Qt::WA_TranslucentBackground); // Optional: Make the window background transparent
    if(!m_Layout)
    {
        m_Layout = new QVBoxLayout(this);
    }
    if(!m_WebView)
    {
        m_WebView = new WebEngineView(this);
    }
    if(!m_webChannel)
    {
        m_webChannel = new QWebChannel(this);
    }
    if(!gleamPost)
    {
        gleamPost = new CGleamConfigPost(this);
        m_webChannel->registerObject(QStringLiteral("gleamPost"), gleamPost);
    }
    m_WebView->page()->setWebChannel(m_webChannel);
    m_Layout->addWidget(m_WebView);
    m_Layout->setContentsMargins(0, 0, 0, 0);
    setLayout(m_Layout);
    QString exeDir = QCoreApplication::applicationDirPath();
#ifdef Q_OS_WIN32
    exeDir.append("\\webpages\\");
    exeDir.append(html);
#else
    exeDir.append("//webpages//");
    exeDir.append(html);
#endif
    loadHtml(exeDir);

}
void CWebConfig::Reszie(double width,double height)
{
    if(width !=0 && height != 0)
    {
        resize(width, height);
    }
}

