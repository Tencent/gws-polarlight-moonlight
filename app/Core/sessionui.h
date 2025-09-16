#pragma once
#include "soloInstance.h"
#include "streaming/session.h"

class TransparentWebView;
enum class InfoNotify;
class SessionUI:
      public SoloInstance<SessionUI>
{
    friend class UIStateMachine;
private:
    Session* m_pSession = nullptr;
    TransparentWebView* m_pInfo_ComfirmView = nullptr;
public:
    SessionUI();
    Session* getSession() { return m_pSession; }
    //Init Session and others
    bool Init();
    bool EnterLoop();
    void Close();
};

