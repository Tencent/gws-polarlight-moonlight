#ifndef _SETTINGWEBVIEW_H
#define _SETTINGWEBVIEW_H

#include <QWidget>
#include <QVBoxLayout>
#include <QWebEngineView>
#include <QWebChannel>
#include <QDir>
#include <QApplication>
#include <QObject>
#include "streaming/session.h"
#include "streaming/gleamapi.h"
#include "settings/streamingpreferences.h"
class SetWebViewBridge;
class CSettingWebView : public QInterfaceDialog {
    Q_OBJECT
public:
    explicit CSettingWebView(QWidget *parent = nullptr);
    void Hide();
    void Show();
    void CallHtmlFunction(QString &str);
    void Resize();
    bool Init();
    void GetSettingConfig();
    void SaveSettingConfig(QString str);
    void SetSession(Session *pSession)
    {
        m_pSession = pSession;
    }
    Session * GetSession()
    {
        return m_pSession;
    }

public slots:

private:
    QVBoxLayout* m_Layout = nullptr;
    QWebEngineView* m_WebView = nullptr;
    QWebChannel* m_webChannel= nullptr;
    SetWebViewBridge* gsetbridge= nullptr;

    Session *m_pSession= nullptr;
protected:
    virtual bool isDragElement() override;
};

class SetWebViewBridge : public QObject {
    Q_OBJECT
public:
    explicit SetWebViewBridge( QObject* parent = nullptr)
    {
        m_pSetting = dynamic_cast<CSettingWebView *>(parent);
    }

    Q_INVOKABLE QString sendMsg(const QString type, const QString &message);
    Q_INVOKABLE void openLogDir();
    Q_INVOKABLE void uploadLog();
    Q_INVOKABLE void openUrlInExternalBrowser(const QUrl &url);
    Q_INVOKABLE void Close();
    Q_INVOKABLE void mouseOnTitle(bool bTitle);
    Q_INVOKABLE QString getVersion();

    bool isTitle() { return m_bOnTitle; };
public slots:

signals:

private:
    CSettingWebView *m_pSetting;
    bool m_bOnTitle = false;
};


void InitGleamSettingWebView();
void ShowGleamWebViewSetting();
void HideGleamWebViewSetting();

void openLogFileDirectory();
void uploadLogFile();
#endif // _SETTINGWEBVIEW_H
    
