#include "sessionui.h"
#include "transparentWebView.h"
#include "displayInfo.h"

SessionUI::SessionUI()
{

}
bool SessionUI::Init()
{
    m_pSession = new Session();
    bool bOK = m_pSession->initialize();
    if(!bOK) {
        return bOK;
    }
    m_pSession->InitSdlWindow();
    m_pInfo_ComfirmView = new TransparentWebView(m_pSession->m_Window);
    m_pInfo_ComfirmView->SetSession(m_pSession);
    return bOK;
}

bool SessionUI::EnterLoop()
{
    m_pSession->SessionMain();

    return true;
}
void SessionUI::Close()
{
    if(m_pInfo_ComfirmView)
    {
        delete m_pInfo_ComfirmView;
        m_pInfo_ComfirmView = nullptr;
    }
}
