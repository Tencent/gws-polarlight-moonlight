#pragma once

#include <QObject>
#include <QVariant>

class ComputerManager;
class NvComputer;
class Session;
class StreamingPreferences;


namespace CliPair
{

class Event;
class LauncherPrivate;

class Launcher : public QObject
{
    Q_OBJECT
    Q_DECLARE_PRIVATE_D(m_DPtr, Launcher)

public:
    explicit Launcher(QString computer, QString predefinedPin, QString InstanceId,
                      QString Token,QString HttpJsonStr,QString ArchStr,
                      QString host,QString ClientUUid,QString privateKeyStr,bool FullScreen,
                      QObject *parent = nullptr);
    ~Launcher();
    void SetSeekSelfOnly(bool bFlag);
    void Reset();
    Q_INVOKABLE void execute(ComputerManager *manager);
    Q_INVOKABLE bool isExecuted() const;

signals:
    void searchingComputer();
    void pairing(QString pcName, QString pin);
    void failed(QString text);
    void success();
    void sessionCreated(QString appName, Session *session);
private slots:
    void onComputerFound(NvComputer *computer);
    void onPairingCompleted(NvComputer *computer, QString error);
    void onTimeout();
    void onComputerUpdated(NvComputer *computer);
    void onQuitAppCompleted(QVariant error);

private:
    QScopedPointer<LauncherPrivate> m_DPtr;
};

}
