#ifdef _WIN32
#ifndef WINDOWSDEVICEMANAGER_H
#define WINDOWSDEVICEMANAGER_H

#include "AbstractPlatformDeviceManager.h"
#include "windows/IDeviceEnumerator.h"
#include "windows/discoverers/DeviceDiscoveryManager.h"
#include <QLoggingCategory>
#include <QMap>
#include <memory>
#if defined(_WIN32) && !defined(Q_MOC_RUN)
#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#endif

Q_DECLARE_LOGGING_CATEGORY(log_device_windows)

class WindowsDeviceManager : public AbstractPlatformDeviceManager
{
    Q_OBJECT

public:
    explicit WindowsDeviceManager(QObject *parent = nullptr);
    explicit WindowsDeviceManager(std::unique_ptr<IDeviceEnumerator> enumerator, QObject *parent = nullptr);
    ~WindowsDeviceManager();

    QVector<DeviceInfo> discoverDevices() override;
    QString getPlatformName() const override { return "Windows"; }
    void clearCache() override;

    // Debug methods
    void debugListAllUSBDevices();

private:
    // Device enumerator (abstraction layer for Windows API)
    std::unique_ptr<IDeviceEnumerator> m_enumerator;

    // Device discovery manager (coordinates generation-specific discoverers)
    std::unique_ptr<DeviceDiscoveryManager> m_discoveryManager;

    // Cleanup
    void cleanup();

    // Discovery system initialization
    void initializeDiscoveryManager();

    // Member variables for caching
    QVector<DeviceInfo> m_cachedDevices;
    QDateTime m_lastCacheUpdate;
    static const int CACHE_TIMEOUT_MS = 5000; // 5 seconds cache
};

#endif // WINDOWSDEVICEMANAGER_H
#endif // _WIN32
