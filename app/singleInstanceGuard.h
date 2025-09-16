#ifndef SINGLEINSTANCEGUARD_H
#define SINGLEINSTANCEGUARD_H
#include <QCoreApplication>
#include <QSharedMemory>
#include <QSystemSemaphore>
#include <QDebug>
#ifdef Q_OS_WIN32
#include <windows.h>
#else
#include <sys/types.h>
#include <signal.h>
#endif
class SingleInstanceGuard {
public:
    SingleInstanceGuard(const QString& key)
        : key(key),
        sharedMemory(key),
        systemSemaphore(key + "_sem", 1)
    {
        systemSemaphore.acquire();

        // 尝试附加到现有的共享内存段
        if (sharedMemory.attach()) {
            // 检查共享内存中的 PID 是否仍在运行
            qint64* pid = static_cast<qint64*>(sharedMemory.data());
            if (isProcessRunning(*pid)) {
                isAnotherInstanceRunning = true;
            } else {
                // 清理残留的共享内存
                sharedMemory.detach();
                sharedMemory.create(sizeof(qint64));
                *pid = QCoreApplication::applicationPid();
                isAnotherInstanceRunning = false;
            }
        } else {
            // 创建新的共享内存段
            if (sharedMemory.create(sizeof(qint64))) {
                qint64* pid = static_cast<qint64*>(sharedMemory.data());
                *pid = QCoreApplication::applicationPid();
                isAnotherInstanceRunning = false;
            } else {
                // 创建失败，可能是其他实例正在运行
                isAnotherInstanceRunning = true;
            }
        }

        systemSemaphore.release();
    }
    bool isProcessRunning(qint64 pid) {
#ifdef Q_OS_WIN32
        HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
        if (hProcess == NULL) {
            return false; // 进程不存在或无法访问
        }

        DWORD exitCode;
        if (GetExitCodeProcess(hProcess, &exitCode)) {
            CloseHandle(hProcess);
            return (exitCode == STILL_ACTIVE); // 如果进程仍在运行，返回 true
        }

        CloseHandle(hProcess);
        return false;
#else
        // 发送信号 0 检查进程是否存在
        return (kill(pid, 0) == 0);
#endif
    }
    ~SingleInstanceGuard() {
        //qInfo() << "~SingleInstanceGuard()... isAnotherInstanceRunning :" << isAnotherInstanceRunning;
        if (!isAnotherInstanceRunning) {
            sharedMemory.detach();
        }
    }

    bool anotherInstanceRunning() const {
        return isAnotherInstanceRunning;
    }

private:
    QString key;
    QSharedMemory sharedMemory;
    QSystemSemaphore systemSemaphore;
    bool isAnotherInstanceRunning;
};
#endif // SINGLEINSTANCEGUARD_H
