#pragma once
#include <queue>
#include <mutex>
#include <condition_variable>

template<typename T>
class SafeQueue {
private:
    std::queue<T> queue;
    std::mutex mutex;
    std::condition_variable cond;
    std::atomic<bool> stopFlag = false;
public:
    void push(T value) {
        std::lock_guard<std::mutex> lock(mutex);
        queue.push(value);
        cond.notify_one();
    }

    T pop() {
        std::unique_lock<std::mutex> lock(mutex);
        cond.wait(lock, [this] { return !queue.empty() || stopFlag.load(); });
        if (stopFlag.load()) {
            return nullptr;
        }
        T value = queue.front();
        queue.pop();
        return value;
    }

    void stopWait() {
        stopFlag.store(true);
        cond.notify_one();
    }
};
