#pragma once
#include "soloInstance.h"
#include "MsgInfo.h"
#include <string_view>
#include <QObject>
#include <thread>
#include <list>
#include <mutex>
#include <condition_variable>
#include <functional>

struct MsgData {
    int     msgOpId;
    MsgCode msgCode;
    MsgType msgType;
    QString msgInfo;
    std::function<void(bool)> callback = nullptr;
    int priority;

    MsgData(int opId, MsgCode code, MsgType type = MsgType::Information, QString info = "",
    std::function<void(bool)> callback = nullptr, int priority = 1000)
        : msgOpId(opId), msgCode(code), msgType(type), msgInfo(info), callback(callback), priority(priority) {}
};


class UIStateMachine : public QObject, public SoloInstance<UIStateMachine>
{
    Q_OBJECT
public:
    UIStateMachine();
    ~UIStateMachine();
    void Stop();

    void onConfirm(bool bOk);
    void push(int iOpId, MsgCode msgCode, MsgType msgType = MsgType::Information, const std::string_view& msgStr = "",
                std::function<void(bool)> callback = nullptr, int priority = 1000);
    void clear(int iOpId);
    int genOpId()
    {
        static int opId = 1;
        std::lock_guard<std::mutex> guard(opMutex);
        return opId++;
    }
signals:
    void showInfoView(const MsgData& data);
private slots:
    void onShowInfoView(const MsgData& data);
    void onCloseByOpId(int iOpId);
private:
    std::thread msgThread;
    std::list<MsgData> msgList;
    std::mutex msgListMutex;
    std::condition_variable msgListCondition;
    bool running = true;

    std::condition_variable confirmCondition;
    bool bOkClicked;
    std::mutex confirmMutex;
    std::mutex opMutex;

    void run();
    void processMsg(const MsgData& event);
    int m_iOpIdShowing = -1;
    int m_iOpIdToClose = -1;
};

