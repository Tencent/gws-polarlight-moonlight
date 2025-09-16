#include "gleamapi.h"
#include <QSet>
#include <QDebug>
#include <QDialog>
#include <QWebEngineView>
#include <QVBoxLayout>
#include <QWebChannel>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QUrlQuery>
#include <QPushButton>
#include <QXmlStreamReader>
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
#include "Core/displayInfo.h"
#include "Core/httpmessagetask.h"
#include "Core/sessionui.h"

extern std::string getClientUUid();

double MAIN_WINDOW_WIDTH = 820.00;
double MAIN_WINDOW_HIGHT = 800.00;
QTextStream gOutStream;


#define DEF_WIDTH  1920.00
#define DEF_HEIGHT 1080.00

CGleamConfigPost::CGleamConfigPost(PopupWindow* parent) : QObject(parent) {
    m_pPopupWindow = parent;
    m_nIndex = 0;
}

QString CGleamConfigPost::QueryData(int Index)
{
    qDebug() << "CGleamConfigPost::QueryData Index = " << Index << "m_QueryData.size()" << m_QueryData.size();
    QMutexLocker locker(&m_Mutex);
    QByteArray temp = m_QueryData[Index];
    m_QueryData.erase(Index);
    QString szRet = QString("data:image/jpeg;base64,") + temp.toBase64();

    return szRet;
}

void CGleamConfigPost::mouseOnTitle(bool bTitle)
{
    m_bOnTitle = bTitle;
}

void CGleamConfigPost::SetQueryData(QByteArray& data, int& nIndex)
{
    m_Mutex.lock();
    m_QueryData[m_nIndex] = data;
    nIndex = m_nIndex;
    m_nIndex++;
    m_Mutex.unlock();
}

void CGleamConfigPost::Close()
{

    m_pPopupWindow->hide();
#ifdef Q_OS_DARWIN
    SessionUI::I().getSession()->m_InputHandler->raiseLastWindow();
#endif

}

void CGleamConfigPost::log(const QString& str)
{
    qDebug() << "log [" << str << "]";
}

QString CGleamConfigPost::sendMsg(const QString type, const QString& message, int nReq)
{
    qDebug() << "sendMsg type =  " << type;
    qDebug() << "sendMsg message begin =  " << message;
    qDebug() << "sendMsg nReq = " << nReq;

    if (!m_pPopupWindow)
    {
        return "";
    }
    if (m_pPopupWindow->isVisible() == false)
    {
        return "";
    }


    QString szTemp = message;
    szTemp.replace("\\\\\\\\.\\\\", "\\\\.\\");
    qDebug() << "sendMsg message after =  " << szTemp;
    qDebug() << "client_uuid =  " << getClientUUid().c_str();
    Session* pSession = m_pPopupWindow->getSession();
    if (!pSession)
    {
        return "";
    }

    if (type == "config") {
        /*
        "{\"ActionType\":\"ConfigChange\","
        "\"PrivacyMode\":\"Disabled\","
        "\"DisplayChoose\":[
            {
            \"Name\":\"\\\\\\\\.\\\\DISPLAY6[0|0|3840|2160]P\",
            \"Stream\":true,\"FPS\":60
            },{
            \"Name\":\"\\\\\\\\.\\\\DISPLAY5[0|-2160|3840|0]P\",
            \"FPS\":60,\"Stream\":true
            }],"
        "\"VirtualDisplay\":0,"
        "\"Channels\":\"channel1\","
        "\"Profile\":\"profile1\","
        "\"Quality\":\"high\","
        "\"Encoder\":\"H.264\","
        "\"RateControl\":\"\","
        "\"VideoBitrate\":\"10\"}"
        */
        QJsonDocument jsonDoc = QJsonDocument::fromJson(message.toUtf8());
        if (jsonDoc.isObject())
        {
            QJsonObject jsonObj = jsonDoc.object();
            if (jsonObj.contains("DisplayChoose")) {
                auto displayChosen = jsonObj.value("DisplayChoose");
            }
        }

    }

    HttpMessageTask* pTask = ConnectionManager::I().createMessageTask(szTemp, type, nReq);
    pTask->onRet(m_pPopupWindow, SLOT(SendMsgRetMsg(int, int, QByteArray)));
    return "";

}


QString PopupWindow::getXmlString(QString xml, QString tagName)
{
    QXmlStreamReader xmlReader(xml);

    while (!xmlReader.atEnd()) {
        if (xmlReader.readNext() != QXmlStreamReader::StartElement)
        {
            continue;
        }

        if (xmlReader.name() == tagName) {
            return xmlReader.readElementText();
        }
    }

    return nullptr;
}

QList<GleamCore::DisplayInfo> PopupWindow::getChosenDisplayInfo(QJsonValue& displayChosen) {
    QSet<QString> chosenSet;
    QJsonArray jsonArray = displayChosen.toArray();
    for (const QJsonValue& value : jsonArray) {
        if (value.isObject()) {
            QJsonObject v = value.toObject();
            if (v.contains("Name")) {
                chosenSet.insert(v.value("Name").toString());
            }
        }
    }
    auto dispInfo = GleamCore::parseDisplayInfo(m_hostDisplayList);
    auto iter = dispInfo.begin();
    while (iter != dispInfo.end()) {
        if (!chosenSet.contains(iter->name)) {
            iter = dispInfo.erase(iter);
        }
        else {
            ++iter;
        }
    }
    return dispInfo;
}

void PopupWindow::SendMsgRetMsg(int flag, int nReq, QByteArray data)
{
    qDebug() << "PopupWindow::SendMsgRetMsg flag = " << flag;
    qDebug() << "PopupWindow::SendMsgRetMsg nReq = " << nReq;
    qDebug() << "PopupWindow::SendMsgRetMsg data.size() = " << data.size();


    QString jsonString;
    QString js;
    if (!data.isEmpty()) {
        if (flag)
        {
            int nIndex = 0;
            gleamPost->SetQueryData(data, nIndex);
            QJsonObject jsonObj;
            jsonObj["image"] = nIndex;
            QJsonDocument jsonDoc(jsonObj);
            jsonString = QString(jsonDoc.toJson(QJsonDocument::Compact));
            qDebug() << "save image data nIndex= " << nIndex;
            if (!jsonString.isEmpty())
            {
                js = QString("CallSnapShotJs(%1,%2)").arg(jsonString).arg(nReq);
                qDebug() << "jsonStrings = " << js;

            }

            if (!js.isEmpty()) {
                qDebug() << "jsonStrings = " << js;
                m_WebView->page()->runJavaScript(js, [&](const QVariant& v)
                    {
                        QString retStr = v.toString();
                    });
            }
        }
    }
}

void PopupWindow::setSession(Session* Session)
{
    m_Session = Session;
}
Session* PopupWindow::getSession()
{
    return m_Session;
}

PopupWindow::PopupWindow(bool move, QWidget* parent) : QInterfaceDialog(parent)
{
    m_Layout = nullptr;
    m_WebView = nullptr;
    m_webChannel = nullptr;
    gleamPost = nullptr;
    m_Move = move;
    if (move)
    {
        QApplication::instance()->installEventFilter(this);
    }
}

void PopupWindow::Resize(double width, double height)
{
    int nlcd_width = 1920;
    int nlcd_height = 1080;
    if (width > 0)
    {
        MAIN_WINDOW_WIDTH = width;
    }
    if (height > 0)
    {
        MAIN_WINDOW_HIGHT = height;
    }
    QScreen* screen = QGuiApplication::primaryScreen();
    if (screen)
    {
        nlcd_width = screen->size().width();
        nlcd_height = screen->size().height();
    }
    if (nlcd_width > DEF_WIDTH && nlcd_height > DEF_HEIGHT)
    {
        nlcd_width = DEF_WIDTH;
        nlcd_height = DEF_HEIGHT;
    }

    double dheight_rate = (MAIN_WINDOW_HIGHT / DEF_HEIGHT) * (double)nlcd_height / MAIN_WINDOW_HIGHT;
    qDebug() << "nlcd_width=  " << nlcd_width << " nlcd_height =  " << nlcd_height;
    qDebug() << "MAIN_WINDOW_HIGHT = " << MAIN_WINDOW_HIGHT * dheight_rate;
#ifdef TARGET_OS_MAC
    resize(MAIN_WINDOW_WIDTH, MAIN_WINDOW_HIGHT);
#endif

#ifdef _WIN64
    int nRealHeight = MAIN_WINDOW_HIGHT * dheight_rate;
    if (nRealHeight > 700)
    {
        setFixedSize(MAIN_WINDOW_WIDTH, MAIN_WINDOW_HIGHT * dheight_rate);
    }
    else
    {
        setFixedSize(MAIN_WINDOW_WIDTH, 700);
    }

#endif
}
void PopupWindow::init()
{
    Resize();
    setWindowFlags(windowFlags() | Qt::WindowStaysOnTopHint | Qt::FramelessWindowHint); // Remove the title bar
    setAttribute(Qt::WA_TranslucentBackground); // Optional: Make the window background transparent
    if (!m_Layout)
    {
        m_Layout = new QVBoxLayout(this);
    }

    if (!m_WebView)
    {
        m_WebView = new WebEngineView(this);
    }
    if (!m_webChannel)
    {
        m_webChannel = new QWebChannel(this);
    }
    if (!gleamPost)
    {
        gleamPost = new CGleamConfigPost(this);
        m_webChannel->registerObject(QStringLiteral("gleamPost"), gleamPost);

    }
    m_WebView->page()->setWebChannel(m_webChannel);
    m_Layout->addWidget(m_WebView);

    setLayout(m_Layout);
    QString exePath = QCoreApplication::applicationDirPath();
    QString htmlFilePath;
#ifdef Q_OS_WIN32
    htmlFilePath = QDir(exePath).filePath("webpages/config.html");
#endif
#ifdef Q_OS_DARWIN
    htmlFilePath = QDir(exePath).filePath("../Resources/webpages/config.html");
#endif
    loadHtml(htmlFilePath);

}
void PopupWindow::loadHtml(QString str)
{
    if (m_WebView)
    {
        connect(m_WebView, &QWebEngineView::loadFinished, this, &PopupWindow::onLoadFinished);
        m_WebView->load(QUrl::fromLocalFile(str));
        m_WebView->page()->settings()->setAttribute(QWebEngineSettings::JavascriptEnabled, true);
        m_WebView->page()->settings()->setAttribute(QWebEngineSettings::JavascriptCanOpenWindows, true);
        m_WebView->page()->settings()->setAttribute(QWebEngineSettings::PluginsEnabled, true);
        m_WebView->show();
    }
}

void PopupWindow::onLoadFinished(bool ok) {
    if (ok) {
        qDebug() << "Page loaded successfully";
        addMouseOnTitleCheck();
        onStatusChanged();
    }
    else {
        qWarning() << "PopupWindow Page failed to load";
    }
}

void PopupWindow::onStatusChanged()
{
    QString js;
    ConnectionManager::I().lockStatus();
    QString jsonString = ConnectionManager::I().getStatus().jsonString;
    ConnectionManager::I().unlockStatus();
    if (!jsonString.isEmpty())
    {
        QJsonDocument jsonDoc = QJsonDocument::fromJson(jsonString.toUtf8());
        if (jsonDoc.isObject())
        {
            QJsonObject jsonObj = jsonDoc.object();
            if (jsonObj.contains("HostDisplayList")) {
                m_hostDisplayList = jsonObj.value("HostDisplayList");
            }
        }
        js = QString("CallStatusJs(%1,%2)").arg(jsonString).arg(0);
    }

    if (!js.isEmpty()) {
        qDebug() << "onStatusChanged jsonStrings = " << js;
        m_WebView->page()->runJavaScript(js, [&](const QVariant& v)
        {
            QString retStr = v.toString();
        });
    }
}

void PopupWindow::addMouseOnTitleCheck()
{
    m_WebView->page()->runJavaScript(R"(
            document.addEventListener('mousemove', function(event) 
            {
                var element = document.elementFromPoint(event.clientX, event.clientY);
                gleam.mouseOnTitle(element && element.classList.contains('title-bars'));
            });
        )");
}

void PopupWindow::Showpage()
{
    QString szMonitorinfo;
    GetRelution(szMonitorinfo);
    QString js = QString("Setinfo(%1)").arg("'" + szMonitorinfo + "'");
    m_WebView->page()->runJavaScript(js, [&](const QVariant& v)
        {
            QString retStr = v.toString();
        });

    qDebug() << "js = " << js;

}

int PopupWindow::GetRelution(QString& info)
{

    QList<QScreen*> list_screen = QGuiApplication::screens();
    QString  szInfo;
    QString szStr;

    for (int i = 0; i < list_screen.size(); i++)
    {
        QRect  rect = list_screen.at(i)->geometry();
        QString rectStr = QString("(%1,%2,%3,%4)|").arg(rect.x()).arg(rect.y()).arg(rect.width()).arg(rect.height());
        szInfo = "display";
        szInfo.append(QString::number(i + 1));
        szInfo.append(rectStr);
        szStr.append(szInfo);

    }
    info = szStr;

    return list_screen.size();
}
