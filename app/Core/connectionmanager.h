#pragma once
#include "soloInstance.h"
#include <iostream>
#include <thread>
#include <Limelight.h>
#include <opus_multistream.h>
#include <atomic>
#include "settings/streamingpreferences.h"
#include "streaming/encryption.h"
#include "streaming/share_data.h"
#include "streaming/session.h"
#include "streaming/eventManager.h"
#include "safequeue.h"
#include "uiStateMachine.h"

struct Status
{
    bool Reconnect = false;
    bool PrivacyState = false;
    bool ForcePrivacyMode = false;
    bool Headless = false;
    QList<GleamCore::DisplayInfo> HostDisplayList;
    std::vector<int> displayChosen;
    QString jsonString;

    bool operator==(const Status& other) const {
        return PrivacyState == other.PrivacyState && Reconnect == other.Reconnect &&
            HostDisplayList == other.HostDisplayList &&
            displayChosen == other.displayChosen && Headless == other.Headless &&
            ForcePrivacyMode == other.ForcePrivacyMode;
    }
    bool operator!=(const Status& other) const {
        return PrivacyState != other.PrivacyState || Reconnect != other.Reconnect ||
            Headless != other.Headless ||
            HostDisplayList != other.HostDisplayList ||
            displayChosen != other.displayChosen ||
            ForcePrivacyMode != other.ForcePrivacyMode;
    }

    bool hasVirtualDisplay() const
    {
        for (auto& item : HostDisplayList)
        {
            if (!item.isPhysical)
                return true;
        }
        return false;
    }

    QList<GleamCore::DisplayInfo> getChosenList() const
    {
        QList<GleamCore::DisplayInfo> dispChosenList;
        for (int index : displayChosen)
            dispChosenList.push_back(HostDisplayList[index]);
        return dispChosenList;
    }

    QList<GleamCore::DisplayInfo> getHostDisplayList() const
    {
        return HostDisplayList;
    }

    void setChosenDisplayInfo(QJsonValue& displayChosenJson) {
        QSet<QString> chosenSet;
        QJsonArray jsonArray = displayChosenJson.toArray();
        for (const QJsonValue& value : jsonArray) {
            if (value.isObject()) {
                QJsonObject v = value.toObject();
                if (v.contains("Name")) {
                    chosenSet.insert(v.value("Name").toString());
                }
            }
        }

        displayChosen.clear();
        for (int i = 0; i < HostDisplayList.size(); ++i)
        {
            if (chosenSet.contains(HostDisplayList[i].shortName))
                displayChosen.push_back(i);
        }
    }
};

Q_DECLARE_METATYPE(Status)
Q_DECLARE_METATYPE(GleamCore::DisplayInfo)

extern QString gQuitMessage;
extern QString gQReq;
extern QString gQToken;

class CMEvent {
public:
    virtual ~CMEvent() {}
    virtual std::string getType() const = 0;
};

class CMReconnectEvent : public CMEvent {
public:
    std::string getType() const override { return "ReconnectEvent"; }
};

class CMDataEvent : public CMEvent {
private:
    std::string data;
public:
    CMDataEvent(const std::string& data) : data(data) {}
    std::string getData() const { return data; }
    std::string getType() const override { return "DataEvent"; }
};

class NvComputer;
class CMConnectEvent : public CMEvent {
protected:
    NvComputer* computer = nullptr;
public:
    CMConnectEvent(NvComputer* c) : computer(c) {}
    NvComputer* getComputer() { return computer; }
    std::string getType() const override { return "ConnectEvent"; }
};

class ComputerManager;
class HttpMessageTask;
namespace CliPair
{
    class Launcher;
}
class ConnectionManager : public SoloInstance<ConnectionManager> {
private:
    SafeQueue<CMEvent*> eventQueue;
    std::atomic<bool> thread_running;
    std::thread* conn_thread;
    std::thread* status_thread;
    std::string m_clientId;
    std::string m_username;
    std::string m_watermark;
    std::string m_userID;
    std::string m_privateKey;

    std::string m_HttpJsonStr;
    std::string m_Arch;
    Status m_Status;
    //for connection info
    QStringList m_args;
    void ParseArgs(const char* clientUUid, const char* privateKeyStr, const QStringList& args);

    void processEvent(CMEvent* event) {
        if (event == nullptr) {
            return;
        }

        if (event->getType() == "DataEvent") {
            auto* dataEvent = dynamic_cast<CMDataEvent*>(event);
            std::cout << "Processing Data Event with data: " << dataEvent->getData() << std::endl;
        }
        else if (event->getType() == "ConnectEvent") {
            auto* pEvent = dynamic_cast<CMConnectEvent*>(event);
            static int s_iOpId = UIStateMachine::I().genOpId();
            UIStateMachine::I().push(s_iOpId, MsgCode::StartConnect, MsgType::Progress);
            OnConnect(pEvent->getComputer());
        }
        else if (event->getType() == "ReconnectEvent") {
            OnReconnect();
        }
        // Handle other event types accordingly
        delete event;
    }
    int OnConnect(NvComputer* computer);
public:
    void OnReconnect();
    ConnectionManager() : thread_running(false), conn_thread(nullptr) {
    }

    ~ConnectionManager() {
        if (thread_running.load()) {
            Stop();
        }
    }
    void OnDisconnected();
    void Reconnect();
    void Start(bool startWithPairAndConnect,
        const char* clientUUid,
        const std::string &username,
        const std::string &watermark,
        const std::string &userID,
        const char* privateKeyStr,
        const QStringList& args) {
        if (!thread_running.load()) {
            thread_running.store(true);
            conn_thread = new std::thread(&ConnectionManager::Run, this);
            status_thread = new std::thread(&ConnectionManager::RunQueryStatus, this);
        }
        m_clientId = clientUUid;
        m_username = username;
        m_watermark = watermark;
        m_userID = userID;
        m_privateKey = privateKeyStr;
        m_args = args;
        if (startWithPairAndConnect) {
            ParseArgs(clientUUid, privateKeyStr, args);
        }
    }

    void Stop() {
        qInfo()<<"ConnectionManager Stop Begin";
        m_StatusCondition.notify_all();
        if (thread_running.load()) {
            thread_running.store(false);
            eventQueue.stopWait();
            if (conn_thread->joinable()) {
                conn_thread->join();
            }
            delete conn_thread;
            conn_thread = nullptr;
            if (status_thread->joinable()) {
                status_thread->join();
            }
            qInfo()<<"ConnectionManager Stop End";
            delete status_thread;
            status_thread = nullptr;
        }
    }

    void pushEvent(CMEvent* event) {
        eventQueue.push(event);
    }

    HttpMessageTask* createMessageTask(const QString& msg, const QString& type, int nReq,  bool start = true);
    void sendQuitMessage();
    std::mutex m_StatusMutex;
    void lockStatus() { m_StatusMutex.lock(); }
    void unlockStatus() { m_StatusMutex.unlock(); }
    const Status& getStatus() { return m_Status; }
    void setChosenDisplayInfo(QJsonValue& displayChosenJson) {
        m_StatusMutex.lock();
        m_Status.setChosenDisplayInfo(displayChosenJson);
        m_StatusMutex.unlock();
        SDL_Event event;
        event.type = SDL_STATUS_CHANGED;
        SDL_PushEvent(&event);
    }
    QList<GleamCore::DisplayInfo> getChosenList() 
    { 
        std::lock_guard<std::mutex> lock(m_StatusMutex); 
        return m_Status.getChosenList(); 
    }

    QList<GleamCore::DisplayInfo> getHostDisplayList()
    {
        std::lock_guard<std::mutex> lock(m_StatusMutex);
        return m_Status.getHostDisplayList();
    }
    std::atomic_bool m_bLiStartConnection = false;
private:
    void Run() {
        while (thread_running.load()) {
            CMEvent* event = eventQueue.pop();
            processEvent(event);
        }
        std::cout << "Exiting Run function..." << std::endl;
    }

    std::condition_variable m_StatusCondition;
    void RunQueryStatus();
    void PaserQueryTaskXml(int flag, const QByteArray& data);

    void SetStatusByXml(const QString& text);
    

};


