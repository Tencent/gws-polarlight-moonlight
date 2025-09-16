#include "transparentWebView.h"
#include <QScreen>
#include <QDebug>
#include <QTimer>
#include <QScreen>

TransparentWebView::TransparentWebView(SDL_Window *window, QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_TranslucentBackground);
    auto flag = Qt::FramelessWindowHint| Qt::WindowStaysOnTopHint |
                Qt::MSWindowsFixedSizeDialogHint | Qt::CustomizeWindowHint;
#ifdef Q_OS_WIN32
    flag = flag | Qt::Tool;
#endif
    setWindowFlags(flag);

    QVBoxLayout* layout = new QVBoxLayout(this);
    webView = new QWebEngineView(this);
    // Get the path of the executable and locate the HTML file
    QString exePath = QCoreApplication::applicationDirPath();
    QString htmlFilePath;
    htmlFilePath = QDir(exePath).filePath("webpages/info_confirm.html");
#ifdef Q_OS_DARWIN
    htmlFilePath = QDir(exePath).filePath("../Resources/webpages/info_confirm.html");
#endif

    webView->setUrl(QUrl::fromLocalFile(htmlFilePath));
    webView->page()->setBackgroundColor(Qt::transparent);

    // Set up the web channel
    bridge = new WebViewBridge(this);
    channel = new QWebChannel(this);
    channel->registerObject(QStringLiteral("bridge"), bridge);
    webView->page()->setWebChannel(channel);

    layout->addWidget(webView);
    this->setLayout(layout);
    int w, h;
    SDL_GetWindowSize(window, &w, &h);
    qreal qtScaleFactor = this->devicePixelRatio();
    setFixedSize(w / qtScaleFactor, h / qtScaleFactor); // Set the size of the WebView
    connect(webView, &QWebEngineView::loadFinished, this, &TransparentWebView::handlePageLoaded);


    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &TransparentWebView::keepOnTop);
    m_timer->start(15); // 每15毫秒检查一次
}

TransparentWebView::~TransparentWebView() {
    if (m_timer != nullptr) {
        m_timer->stop();
        delete m_timer;
    }
}

void TransparentWebView::SetSession(Session* Session)
{
    m_Session = Session;
}

void TransparentWebView::callJsFunction(const QString& functionName, const QVariantList& args) {
    if (isDocumentReady) {
        QString jsCall = QString("%1(").arg(functionName);
        for (int i = 0; i < args.size(); ++i) {
            jsCall += QString::fromLatin1("\"%1\"").arg(args[i].toString());
            if (i < args.size() - 1) {
                jsCall += QString::fromLatin1(",");
            }
        }
        jsCall += QString::fromLatin1(");");
        qDebug() << "Calling JS function:" << jsCall; // Debug log
        webView->page()->runJavaScript(jsCall);
    }
    else {
        cachedCalls.push({ functionName, args });
    }
}
void TransparentWebView::handlePageLoaded(bool success) {
    if (success) {
        qDebug() << "Page loaded successfully.";
        isDocumentReady = true;
        executeCachedCalls();
        emit PageLoaded(); // Emit the PageLoaded signal
    }
    else {
        qWarning() << "Failed to load page.";
    }
}

QPoint getScreenOffset(QWidget *widget) {
    QList<QScreen*> screens = QGuiApplication::screens();
    QRect widgetGeometry = widget->geometry();
    for (QScreen *screen : screens) {
        if (screen->geometry().intersects(widgetGeometry)) {
            return screen->geometry().topLeft();
        }
    }

    return {0,0}; // 如果窗口不在任何屏幕上，返回 0
}

void TransparentWebView::keepOnTop() {
    if (!this->isVisible() || m_Session == nullptr) {
        return;
    }

    Uint32 window1Flags = SDL_GetWindowFlags(m_Session->m_Window);
    Uint32 window2Flags = SDL_WINDOW_MINIMIZED;
    if (m_Session->m_Window2 != nullptr) {
        window2Flags = SDL_GetWindowFlags(m_Session->m_Window2);
    }

    // 判断窗口是否最小化
    if (window1Flags & window2Flags & SDL_WINDOW_MINIMIZED) {
        qDebug() << "all window minimized";
        if (!this->isMinimized()) {
            this->showMinimized();
        }
        return;
    } else {
        if (this->isMinimized()) {
            this->showNormal();
        }
    }

    static SDL_Window *LastActiveWin = m_Session->getActiveSDLWindow();
    SDL_Window *activeWin = m_Session->getActiveSDLWindow();
    if (activeWin != nullptr) {
        if (!(windowFlags() & Qt::WindowStaysOnTopHint)) {
            setWindowFlags(windowFlags() | Qt::WindowStaysOnTopHint);
            show();
        }
    } else if (m_Session->m_Window != nullptr && !(window1Flags & SDL_WINDOW_MINIMIZED)) {
        activeWin = m_Session->m_Window;
        if (windowFlags() & Qt::WindowStaysOnTopHint) {
            setWindowFlags(windowFlags() & ~Qt::WindowStaysOnTopHint);
            show();
        }
    } else if (m_Session->m_Window2 != nullptr && !(window2Flags & SDL_WINDOW_MINIMIZED)) {
        activeWin = m_Session->m_Window2;
        if (windowFlags() & Qt::WindowStaysOnTopHint) {
            setWindowFlags(windowFlags() & ~Qt::WindowStaysOnTopHint);
            show();
        }
    }

    if (activeWin == nullptr) {
        qWarning() << "can't get any SDL window";
        return;
    }


    int x, y, width, height;
    SDL_GetWindowPosition(activeWin, &x, &y);
    SDL_GetWindowSize(activeWin, &width, &height);

    qreal qtScaleFactor  = 1;
#ifdef Q_OS_WIN32
    qtScaleFactor = this->devicePixelRatio();
#endif
    QPoint qtPosition = this->pos();
    int qtWidth = this->width() * qtScaleFactor;
    int qtHeight = this->height() * qtScaleFactor;

    static unsigned int resizeCnt = 0;
    if (resizeCnt % 10 == 0 && (abs(qtWidth - width) > 10 || abs(qtHeight - height) > 10)) {
        qInfo() << "not equal";
        setFixedSize(width / qtScaleFactor, height / qtScaleFactor); // Set the size of the WebView
    }

    QPoint offset = getScreenOffset(this);
    x -= offset.x();
    y -= offset.y();
    QPoint targetPosition(
        offset.x() + (x + (width-qtWidth)/2)/qtScaleFactor,
        offset.y() + (y + (height-qtHeight)/2)/qtScaleFactor);
    if (targetPosition != qtPosition) {
        this->move(targetPosition);
    }
    if (LastActiveWin != activeWin) {
        LastActiveWin = activeWin;
        SDL_RaiseWindow(activeWin);
    }

}

void TransparentWebView::executeCachedCalls() {
    while (!cachedCalls.empty()) {
        auto call = cachedCalls.front();
        cachedCalls.pop();
        callJsFunction(call.first, call.second);
    }
}

void TransparentWebView::ShowInfo(MsgCode errorCode, const QString& strInfo) {
    if (isVisible())
    {
        callJsFunction("setInfo", { "", strInfo});
    }
    else
    {
        m_CurMsgType = MsgType::Information;
        show();
        keepOnTop();
        callJsFunction("showError", { "", strInfo });
    }
}

void TransparentWebView::ShowProgress(MsgCode progressCode, const QString& info) {
    m_CurMsgType = MsgType::Progress;
    QString title;
    switch (progressCode) {
        case MsgCode::StartConnect: {
            title = "Connecting";
        }break;
        case MsgCode::StartPair: {
            title = "Pairing";
        }break;
    }

    show();
    keepOnTop();
    callJsFunction("showProgress", { info, "", title });
}

void TransparentWebView::ShowConfirm(const QString& info) {
    m_CurMsgType = MsgType::Confirm;
    show();
    keepOnTop();
    callJsFunction("showConfirm", { info });
}
void TransparentWebView::waitForSectionReady() {
    QEventLoop loop;
    connect(bridge, &WebViewBridge::SectionReady, &loop, &QEventLoop::quit);
    loop.exec();
}
void TransparentWebView::Close() {
    m_CurMsgType = MsgType::Information;
    hide();
    callJsFunction("closeView", {});
}
void TransparentWebView::SetInfo(const QString& strInfo, int errorCode) {
    callJsFunction("setInfo", { errorCode, strInfo });
}
void TransparentWebView::handleSectionReady() {
    setStyleSheet("background: transparent;");
    show(); // Show the webview when the section is ready
}
