#ifndef _PotalInteractionManage_H_
#define _PotalInteractionManage_H_

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

enum emPortalType
{
    emSessionInfoToPoral = 1,
};
class CPortalEvent {
public:
    virtual ~CPortalEvent() {}
    virtual emPortalType getType() const = 0;
};

class CPortalSessionEvent : public CPortalEvent {
public:
    CPortalSessionEvent(std::string clientId,std::string HttpJsonStr,std::string ClientOS,
        std::string ClientArch,std::string message)
    {
        m_ClientId= clientId;
        m_HttpJsonStr= HttpJsonStr;
        m_ClientOS = ClientOS;
        m_ClientArch= ClientArch;
        m_message= message;
    }
    emPortalType getType() const override { return emPortalType::emSessionInfoToPoral; }
    std::string m_ClientId;
    std::string m_HttpJsonStr;
    std::string m_ClientOS;
    std::string m_ClientArch;
    std::string m_message;
    int m_Count = 0;
};


class CPortalInteractionManager;
class CPortalInteractionManager : public SoloInstance<CPortalInteractionManager> {
private:
    SafeQueue<CPortalEvent*> eventQueue;
    std::atomic<bool> thread_running;
    std::thread* conn_thread;

    void processEvent(CPortalEvent* event) {
        if (event == nullptr) {
            return;
        }
        switch(event->getType())
        {
        case  emPortalType::emSessionInfoToPoral:
        {
            auto* dataEvent = dynamic_cast<CPortalSessionEvent*>(event);
            PortalSessionInfo(dataEvent->m_ClientId,dataEvent->m_HttpJsonStr,
                              dataEvent->m_ClientOS,dataEvent->m_ClientArch,dataEvent->m_message);
        }break;
        default:break;
        }
        delete event;
    }
    void PortalSessionInfo(std::string & clientId,std::string &HttpJsonStr,
                        std::string & ClientOS,std::string &ClientArch,std::string & message);

public:
    CPortalInteractionManager() : thread_running(false), conn_thread(nullptr) {}

    ~CPortalInteractionManager() {
        if (thread_running.load()) {
            Stop();
        }
    }
    void OnDisconnected(int errorCode);
    void Reconnect();
    void Start() {
        if (!thread_running.load()) {
            thread_running.store(true);
            conn_thread = new std::thread(&CPortalInteractionManager::Run, this);
        }
    }

    void Stop() {
        qInfo()<<"CPortalInteractionManager Stop Begin";
        if (thread_running.load()) {
            thread_running.store(false);
            eventQueue.stopWait();
            if (conn_thread->joinable()) {
                conn_thread->join();
            }
            qInfo()<<"CPortalInteractionManage Stop End";
            delete conn_thread;
            conn_thread = nullptr;
        }
    }

    void pushEvent(CPortalEvent* event) {
        eventQueue.push(event);
    }

private:
    void Run() {
        while (thread_running.load()) {
            CPortalEvent* event = eventQueue.pop();
            processEvent(event);
        }
        std::cout << "Exiting Run function..." << std::endl;
    }
};


std::string ParseJson(QString json, std::string & clientId, QString &httpCurl, QString &token,
          QJsonDocument &retJson, std::string &ClientOS, std::string & ClientArch,std::string & message);
#endif
    
