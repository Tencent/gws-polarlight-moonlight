#ifndef TRANSPARENTWEBVIEW_H
#define TRANSPARENTWEBVIEW_H

#include "streaming/session.h"
#include "MsgInfo.h"
#include <QWidget>
#include <QVBoxLayout>
#include <QWebEngineView>
#include <QWebChannel>
#include <QDir>
#include <QApplication>
#include <QObject>
#include <QPainter>
#include <QTimer>
#include <queue>
#include <SDL.h>

class WebViewBridge : public QObject {
    Q_OBJECT
public:
    explicit WebViewBridge(QObject* parent = nullptr) : QObject(parent) {}

public slots:
    void receiveMessageFromJS(const QString& message) {
        qDebug() << "Message from JS:" << message;
        if (message == "OK")
            UIStateMachine::I().onConfirm(true);
        else if (message == "Cancel")
            UIStateMachine::I().onConfirm(false);
    }
    void sectionReady() {
        emit SectionReady();
    }
signals:
    void SectionReady();
};

class TransparentWebView : public QWidget {
    Q_OBJECT
public:
    TransparentWebView(SDL_Window *window, QWidget* parent = nullptr);
    ~TransparentWebView();
    void SetSession(Session* Session);
public slots:
    void callJsFunction(const QString& functionName, const QVariantList& args);

    // New functions to show different types of information
    void ShowInfo(MsgCode errorCode, const QString& strInfo);
    void ShowProgress(MsgCode progressCode, const QString& info);
    void ShowConfirm(const QString& info);
    void Close();
    void SetInfo(const QString& strInfo, int errorCode =0);
signals:
    void PageLoaded(); // Signal emitted when the page is fully loaded
private slots:
    void handlePageLoaded(bool success);
    void handleSectionReady();
    void keepOnTop();
private:
    QWebEngineView* webView;
    WebViewBridge* bridge;
    QWebChannel* channel;
    bool isDocumentReady = false;
    std::queue<std::pair<QString, QVariantList>> cachedCalls;
    QTimer *m_timer = nullptr;
    Session *m_Session = nullptr;
    MsgType m_CurMsgType = MsgType::Information;
    void executeCachedCalls();
    void waitForSectionReady();
};

#endif // TRANSPARENTWEBVIEW_H
