#ifndef GLEAMAPI_H
#define GLEAMAPI_H

#include "session.h"
#include <QApplication>
#include <QDialog>
#include <QFormLayout>
#include <QMouseEvent>
#include <QObject>
#include <QPoint>
#include <QVBoxLayout>
#include <QWebChannel>
#include <QWebEngineView>

class PopupWindow;
class FloatingBall;
class WebEngineView;
class CWebConfig;

class QInterfaceDialog: public QDialog {
public:
    QInterfaceDialog(QWidget *parent = nullptr):QDialog(parent){}
    void mousePressEvent(QMouseEvent *event) override {}

    void mouseMoveEvent(QMouseEvent *event) override {}

    void mouseReleaseEvent(QMouseEvent *event) override {}
};

class CGleamConfigPost : public QObject {
    Q_OBJECT
  public:
    explicit CGleamConfigPost(PopupWindow *parent = nullptr);

    Q_INVOKABLE QString sendMsg(const QString type, const QString &message);
    Q_INVOKABLE void log(const QString &str);

    Q_INVOKABLE void Close();
  public slots:
    void SendMsgRet(QString text);

  signals:
    void SendMsgRetMsg(QString text);

  private:
    PopupWindow *m_pPopupWindow;
};
class PopupWindow : public QInterfaceDialog {
    friend class CWebConfig;
    Q_OBJECT
  public:
    explicit PopupWindow(bool move = true, QWidget *parent = nullptr);
    void setSession(Session *Session);
    Session *getSession();
    void Showpage();
    void Reszie(double width = 0, double height = 0);
    void init();
    int GetRelution(QString &info);
    void loadHtml(QString str);
    void mousePressEvent(QMouseEvent *event) override {
        if (!m_Move)
            return;
        if (event->button() == Qt::LeftButton) {
            isDragging = true;
            dragStartPos = event->globalPos() - frameGeometry().topLeft();
        }
    }

    void mouseMoveEvent(QMouseEvent *event) override {
        if (!m_Move)
            return;
        if (event->buttons() & Qt::LeftButton) {
            move(event->globalPos() - dragStartPos);
        }
    }

    void mouseReleaseEvent(QMouseEvent *event) override {
        if (!m_Move)
            return;
        if (event->button() == Qt::LeftButton) {
            isDragging = false;
        }
    }
    void SetDisplayCout(int n) { m_nDisplayCount = n; }
    int GetDisplayCout() { return m_nDisplayCount; }
  public slots:
    void SendMsgRetMsg(QString text);

  private:
    QWebChannel *m_webChannel;
    Session *m_Session;
    QVBoxLayout *m_Layout;
    CGleamConfigPost *gleamPost;
    QWebEngineView *m_WebView = nullptr;
    QPoint dragPosition;

    bool isDragging = false;
    QPoint dragStartPos;
    int m_nDisplayCount;
    bool m_Move;
};

class WebEngineView : public QWebEngineView {
    Q_OBJECT

  protected:
    bool eventFilter(QObject *object, QEvent *event) {
        if (object->parent() == this) {
            QEvent::Type et = event->type();
            switch (et) {
            case QEvent::MouseButtonPress:
                m_Parent->mousePressEvent(static_cast<QMouseEvent *>(event));
                break;
            case QEvent::MouseButtonRelease:
                m_Parent->mouseReleaseEvent(static_cast<QMouseEvent *>(event));
                break;
            case QEvent::MouseMove:
                m_Parent->mouseMoveEvent(static_cast<QMouseEvent *>(event));
                break;
            default:
                break;
            }
        }
        return false;
    }

  public:
    WebEngineView(QInterfaceDialog *wind) : QWebEngineView(wind) {
        QApplication::instance()->installEventFilter(this);
        m_Parent = wind;
    }
private:
    QInterfaceDialog *m_Parent;
};

class ConfigTask : public QObject, public QRunnable {
    Q_OBJECT
  public:
    ConfigTask(CGleamConfigPost *GleamConfig, Session *session, QString szMsg)
        : m_GleamConfig(GleamConfig), m_Session(session), m_szMsg(szMsg) {
        connect(this, &ConfigTask::SendMsgRet, GleamConfig, &CGleamConfigPost::SendMsgRet);
    }

  signals:
    void SendMsgRet(QString text);

  private:
    void run();

  private:
    CGleamConfigPost *m_GleamConfig;
    Session *m_Session;
    QString m_szMsg;
};

class CRefresh : public QObject {
    Q_OBJECT
  public:
    explicit CRefresh(FloatingBall *parent);

    Q_INVOKABLE void RefreshFun(QString type);

  private:
    FloatingBall *m_pFloatingBall;
};
class FloatingBall : public QInterfaceDialog {
    Q_OBJECT
  public:
    explicit FloatingBall(QWidget *parent = nullptr);
    void OpenOther();
    void Reszie();
    void init();
    void mousePressEvent(QMouseEvent *event) override {
        if (event->button() == Qt::LeftButton) {
            isDragging = true;
            dragStartPos = event->globalPos() - frameGeometry().topLeft();
        }
    }

    void mouseMoveEvent(QMouseEvent *event) override {
        if (event->buttons() & Qt::LeftButton) {
            move(event->globalPos() - dragStartPos);
        }
    }

    void mouseReleaseEvent(QMouseEvent *event) override {
        if (event->button() == Qt::LeftButton) {
            isDragging = false;
        }
    }

  private:
    QWebChannel *m_webChannel;
    Session *m_Session;
    QVBoxLayout *m_Layout;
    QWebEngineView *m_WebView;
    QPoint dragPosition;
    bool isDragging = false;
    QPoint dragStartPos;
    int m_nDisplayCount;
    CRefresh *grefresh;
};

class CWebConfig : public PopupWindow {
  public:
    explicit CWebConfig(bool move = true);
    void init(QString html, double width, double height);
    void Reszie(double width = 0, double height = 0);
};

#endif // GLEAMAPI_H
