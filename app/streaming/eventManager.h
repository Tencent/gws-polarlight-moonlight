#ifndef EVENT_MANAGER_H
#define EVENT_MANAGER_H

#include <vector>
#include <chrono>
#include <memory>
#include <unordered_map>
#include <mutex>
#include <algorithm>
#include <iostream>
#include <QDebug>
enum emEventDataType
{
    emNoraml = 0,
};

class EventBase {
public:
    using TimePoint = std::chrono::system_clock::time_point;

private:
    TimePoint expires;

public:

    EventBase( int timeoutInSeconds): expires(std::chrono::system_clock::now() + std::chrono::seconds(timeoutInSeconds)) {
    }
    virtual ~EventBase()
    {
    }
    bool isExpired() const {
        return std::chrono::system_clock::now() > expires;
    }
};


class EventManager {
private:
    std::unordered_map<int, std::vector<std::shared_ptr<EventBase>>> events;
    mutable std::mutex mtx;

public:
    ~EventManager() {
        clearAllEvents();
        std::cout << "EventManager destroyed, all events cleared." << std::endl;
    }

    void pushEvent(int type, const std::shared_ptr<EventBase>& event) {
        std::lock_guard<std::mutex> lock(mtx);
        // Remove expired events of the same type before pushing new event
        auto& vec = events[type];
        vec.erase(std::remove_if(vec.begin(), vec.end(), [](const std::shared_ptr<EventBase>& ev) {
                      return ev->isExpired();
                  }), vec.end());

        vec.push_back(event);
    }

    std::shared_ptr<EventBase> pullEvent(int type) {
        std::lock_guard<std::mutex> lock(mtx);
        if (events.count(type) && !events[type].empty()) {
            auto event = events[type].front();
            events[type].erase(events[type].begin());
            return event;
        }
        return nullptr;
    }

    void clearAllEvents() {
        std::lock_guard<std::mutex> lock(mtx);
        events.clear();
    }
};

extern EventManager g_Manager;

#endif // EVENT_MANAGER_H
