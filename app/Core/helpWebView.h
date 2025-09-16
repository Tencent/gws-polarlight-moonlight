#ifndef _HELPWEBVIEW_H_
#define _HELPWEBVIEW_H_

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
class HelpWebViewBridge;
class CHelpWebView : public QInterfaceDialog {
    Q_OBJECT
public:
    explicit CHelpWebView(QWidget *parent = nullptr);
    void Hide();
    void Show();
    void Resize();
    bool Init();


public slots:

private:
    QVBoxLayout* m_Layout = nullptr;
    QWebEngineView* m_WebView = nullptr;
    QWebChannel* m_webChannel= nullptr;
    HelpWebViewBridge* ghelpbridge= nullptr;
    StreamingPreferences m_prefs;
protected:
    virtual bool isDragElement() override;
};

class HelpWebViewBridge : public QObject {
    Q_OBJECT
public:
    explicit HelpWebViewBridge( QObject* parent = nullptr)
    {
        m_pSetting = dynamic_cast<CHelpWebView *>(parent);
    }
    Q_INVOKABLE void Close();
    Q_INVOKABLE void mouseOnTitle(bool bTitle);
        bool isTitle() { return m_bOnTitle; };
public slots:

signals:

private:
    CHelpWebView *m_pSetting;
    bool m_bOnTitle = false;
};


void InitHelpWebView();
void ShowHelpWebView();
void HideHelpWebView();

#endif // _SETTINGWEBVIEW_H
    
