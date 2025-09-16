#include "uiStateMachine.h"
#include "sessionui.h"
#include "transparentWebView.h"
#include "../EventReporter.h"
#include <mutex>

UIStateMachine::UIStateMachine()
{
    moveToThread(SessionUI::I().m_pInfo_ComfirmView->thread());
    connect(this, &UIStateMachine::showInfoView, this, &UIStateMachine::onShowInfoView);
    msgThread = std::thread(&UIStateMachine::run, this);
}

UIStateMachine::~UIStateMachine()
{
    qInfo() << "UIStateMachine::~UIStateMachine()";
    Stop();
}

void UIStateMachine::Stop() {
    qInfo() << "UIStateMachine::Stop()";
    confirmCondition.notify_one();
    {
    std::lock_guard<std::mutex> lock(msgListMutex);
    running = false;
    }
    msgListCondition.notify_one();
    if (msgThread.joinable()) msgThread.join();
}

void UIStateMachine::onConfirm(bool bOk)
{
    std::lock_guard<std::mutex> lock(confirmMutex);
    bOkClicked = bOk;
    confirmCondition.notify_one();
}

void UIStateMachine::push(int iOpId, MsgCode msgCode, MsgType msgType /*= MsgType::Information*/,
    const std::string_view& msgStr /*= ""*/, std::function<void(bool)> callback /*= nullptr*/, int priority /*= 1000*/)
{
    if (SessionUI::I().m_pInfo_ComfirmView == nullptr) {
        return;
    }

    std::string msg;
    auto it = MsgMap.find(msgCode);
    if (!msgStr.empty())               // param first
        msg = msgStr;
    else if (it != MsgMap.end())       // MsgMap second
        msg = it->second;
    else                               // no error msg
        msg = "No message";

    msgListMutex.lock();
    msgList.push_back(MsgData(iOpId, msgCode, msgType, QString::fromLocal8Bit(msg.c_str()), callback, priority));
    msgListMutex.unlock();
    msgListCondition.notify_one();
}

void UIStateMachine::clear(int iOpId)
{
    std::lock_guard<std::mutex> guard(msgListMutex);
    msgList.remove_if([iOpId](const MsgData& msg) {
            return msg.msgOpId == iOpId;
        });
    if (m_iOpIdShowing == iOpId)
        QMetaObject::invokeMethod(this, "onCloseByOpId", Q_ARG(int, iOpId));
}

void UIStateMachine::onShowInfoView(const MsgData& data)
{
    if (!running) {
        return;
    }

    switch (data.msgCode)
    {
    case MsgCode::REMDER_HAVE_FIRSTFRAME:
        if (gConnectionReporter != nullptr) {
            gConnectionReporter->SetStateSuccess();
        }
    case MsgCode::CloseInfo:
        SessionUI::I().m_pInfo_ComfirmView->Close();
        return;
    }
    
    switch (data.msgType)
    {
    case MsgType::Information:
        SessionUI::I().m_pInfo_ComfirmView->ShowInfo(data.msgCode, data.msgInfo);
        break;
    case MsgType::Progress:
        SessionUI::I().m_pInfo_ComfirmView->ShowProgress(data.msgCode, data.msgInfo);
        break;
    case MsgType::Confirm:
        SessionUI::I().m_pInfo_ComfirmView->ShowConfirm(data.msgInfo);
    }
    std::lock_guard<std::mutex> guard(msgListMutex);
    m_iOpIdShowing = data.msgOpId;
}

void UIStateMachine::onCloseByOpId(int iOpId)
{
    QTimer::singleShot(500, [this, iOpId] {
            std::unique_lock<std::mutex> listLock(msgListMutex);
            if (iOpId > 0 && iOpId == m_iOpIdShowing)
            {
                SessionUI::I().m_pInfo_ComfirmView->Close();
                m_iOpIdShowing = -1;
            }
        });
}

void UIStateMachine::run()
{
    while (running)
    {
        std::unique_lock<std::mutex> listLock(msgListMutex);
        msgListCondition.wait(listLock, [this] { return !msgList.empty() || !running; });

        if (!running && msgList.empty())
            break;

        while (!msgList.empty()) 
        {
            MsgData data = msgList.front();
            msgList.pop_front();
            listLock.unlock();
            processMsg(data);
            listLock.lock();
        }
    }
}

void UIStateMachine::processMsg(const MsgData& data)
{
    emit showInfoView(data);

    if (data.msgType == MsgType::Confirm)
    {
        bOkClicked = false;
        std::unique_lock<std::mutex> confirmLock(confirmMutex);
        confirmCondition.wait(confirmLock);

        if (data.callback)
            data.callback(bOkClicked);
        confirmLock.unlock();

        msgListMutex.lock();
        msgList.push_front(MsgData(0, MsgCode::CloseInfo));
        msgListMutex.unlock();
    }
}
