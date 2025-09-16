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
#include "Core/displayInfo.h"
#include "Core/connectionmanager.h"

class PopupWindow;
class WebEngineView;
class HttpMessageTask;

class DataProvider : public QObject {
    Q_OBJECT
public:
    explicit DataProvider(QObject *parent = nullptr) : QObject(parent) {}

    Q_INVOKABLE QByteArray getData() {
        // Return some data as QByteArray
        return QByteArray("Hello from C++!");
    }
};

class QInterfaceDialog: public QDialog {
public:
    QInterfaceDialog(QWidget *parent = nullptr):QDialog(parent){}
    virtual void mousePressEvent(QMouseEvent *event) override 
    {
        if (!m_Move)
            return;
        if (isDragElement() && event->button() == Qt::LeftButton) {
            m_isDragging = true;
            m_dragStartPos = event->globalPos() - frameGeometry().topLeft();
        }
    }

    virtual void mouseMoveEvent(QMouseEvent *event) override
    {
        if (!m_Move || !m_isDragging)
            return;
        if (event->buttons() & Qt::LeftButton) {
            move(event->globalPos() - m_dragStartPos);
        }
    }

    virtual void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (!m_Move)
            return;
        if (event->button() == Qt::LeftButton) {
            m_isDragging = false;
        }
    }

protected:
    QPoint m_dragStartPos;
    bool m_Move = true;
    bool m_isDragging = false;

    virtual bool isDragElement() { return true; };
};

class CGleamConfigPost : public QObject {
    Q_OBJECT
  public:
    explicit CGleamConfigPost(PopupWindow *parent = nullptr);

    Q_INVOKABLE QString sendMsg(const QString type, const QString &message,int nReq);
    Q_INVOKABLE void log(const QString &str);
    Q_INVOKABLE void Close();
    Q_INVOKABLE QString QueryData(int Index);
    Q_INVOKABLE void mouseOnTitle(bool bTitle);

  public:
    void SetQueryData(QByteArray &data,int &nIndex);
    bool isTitle() { return m_bOnTitle; };
  private:
    PopupWindow *m_pPopupWindow;
    std::map<uint64_t,QByteArray> m_QueryData;
    uint64_t m_nIndex;
    QMutex m_Mutex;
    bool m_bOnTitle = false;
};
class PopupWindow : public QInterfaceDialog {

    Q_OBJECT
  public:
    explicit PopupWindow(bool move = true, QWidget *parent = nullptr);
    void setSession(Session *Session);
    Session *getSession();
    QList<GleamCore::DisplayInfo> getChosenDisplayInfo(QJsonValue &displayChosen);
    void Showpage();
    void Resize(double width = 0, double height = 0);
    void init();
    int GetRelution(QString &info);
    void loadHtml(QString str);
    void SetDisplayCout(int n) { m_nDisplayCount = n; }
    int GetDisplayCout() { return m_nDisplayCount; }

    QString getXmlString(QString xml, QString tagName);

    void onStatusChanged();

  public slots:
    void SendMsgRetMsg(int flag,int nReq,QByteArray data);
  signals:
    void statusChange(const Status& s);
  private slots:
    void onLoadFinished(bool ok);
  private:
    QWebChannel *m_webChannel;
    Session *m_Session;
    QVBoxLayout *m_Layout;

    QWebEngineView *m_WebView = nullptr;;
    int m_nDisplayCount;
    CGleamConfigPost *gleamPost = NULL;
    QJsonValue m_hostDisplayList;

    void addMouseOnTitleCheck();
protected:
    virtual bool isDragElement() override 
    {
        return gleamPost && gleamPost->isTitle();
    }
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


#endif // GLEAMAPI_H
