#pragma once

#include "nvhttp.h"
#include "nvaddress.h"

#include <QThread>
#include <QReadWriteLock>
#include <QSettings>
#include <QRunnable>

class NvComputer
{
    friend class PcMonitorThread;
    friend class ComputerManager;
    friend class PendingQuitTask;

private:
    void sortAppList();
    bool pendingQuit;

public:
    explicit NvComputer(NvHTTP& http, QString serverInfo);

    explicit NvComputer(QSettings& settings);

    NvComputer(const NvComputer& other)
        : pendingQuit(other.pendingQuit),
        state(other.state),
        pairState(other.pairState),
        activeAddress(other.activeAddress),
        activeHttpsPort(other.activeHttpsPort),
        currentGameId(other.currentGameId),
        gfeVersion(other.gfeVersion),
        appVersion(other.appVersion),
        displayModes(other.displayModes),
        maxLumaPixelsHEVC(other.maxLumaPixelsHEVC),
        serverCodecModeSupport(other.serverCodecModeSupport),
        gpuModel(other.gpuModel),
        isSupportedServerVersion(other.isSupportedServerVersion),
        isDualDisplay(other.isDualDisplay),
        localAddress(other.localAddress),
        remoteAddress(other.remoteAddress),
        ipv6Address(other.ipv6Address),
        manualAddress(other.manualAddress),
        macAddress(other.macAddress),
        name(other.name),
        hasCustomName(other.hasCustomName),
        uuid(other.uuid),
        serverCert(other.serverCert),
        appList(other.appList),
        isNvidiaServerSoftware(other.isNvidiaServerSoftware),
        externalPort(other.externalPort)
    {}

    void
    setRemoteAddress(QHostAddress);

    bool updateAppList(QVector<NvApp> newAppList);

    bool
    update(const NvComputer& that);

    bool
    wake() const;

    enum ReachabilityType {
        RI_UNKNOWN,
        RI_LAN,
        RI_VPN,
    };

    ReachabilityType
    getActiveAddressReachability() const;

    QVector<NvAddress>
    uniqueAddresses() const;

    void
    serialize(QSettings& settings) const;

    enum PairState {
        PS_UNKNOWN,
        PS_PAIRED,
        PS_NOT_PAIRED
    };

    enum ComputerState {
        CS_UNKNOWN,
        CS_ONLINE,
        CS_OFFLINE
    };

    // Ephemeral traits
    ComputerState state;
    PairState pairState;
    NvAddress activeAddress;
    uint16_t activeHttpsPort;
    int currentGameId;
    QString gfeVersion;
    QString appVersion;
    QVector<NvDisplayMode> displayModes;
    int maxLumaPixelsHEVC;
    int serverCodecModeSupport;
    QString gpuModel;
    bool isSupportedServerVersion;
    bool isDualDisplay;

    // Persisted traits
    NvAddress localAddress;
    NvAddress remoteAddress;
    NvAddress ipv6Address;
    NvAddress manualAddress;
    QByteArray macAddress;
    QString name;
    bool hasCustomName;
    QString uuid;
    QSslCertificate serverCert;
    QVector<NvApp> appList;
    bool isNvidiaServerSoftware;

    // Synchronization
    mutable QReadWriteLock lock;

private:
    uint16_t externalPort;
};
