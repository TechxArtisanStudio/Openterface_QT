#ifdef __linux__
#ifndef LINUXDEVICEMANAGER_H
#define LINUXDEVICEMANAGER_H

#include "AbstractPlatformDeviceManager.h"
#include <QLoggingCategory>
#include <QDateTime>
#include <QVariantMap>

#ifdef HAVE_LIBUDEV
struct udev;
struct udev_device;
#endif

Q_DECLARE_LOGGING_CATEGORY(log_device_linux)

class LinuxDeviceManager : public AbstractPlatformDeviceManager
{
    Q_OBJECT
    
public:
    explicit LinuxDeviceManager(QObject *parent = nullptr);
    ~LinuxDeviceManager();
    
    QList<DeviceInfo> discoverDevices() override;
    QString getPlatformName() const override { return "Linux"; }
    void clearCache() override;
    
private:
#ifdef HAVE_LIBUDEV
    struct UdevDeviceData {
        QString syspath;
        QString portChain;
        QString parentSyspath;
        QVariantMap properties;
    };
    
    // libudev-based device discovery
    QList<UdevDeviceData> findUdevDevicesByVidPid(const QString& subsystem, const QString& vid, const QString& pid);
    QList<UdevDeviceData> findUdevDevices(const QString& subsystem, const QVariantMap& filters = QVariantMap());
    
    // libudev helper functions
    struct udev_device* findUsbParentDevice(struct udev_device* device);
    QVariantMap collectDeviceProperties(struct udev_device* device);
    QString extractPortChainFromSyspath(const QString& syspath);
    QString getPortChainFromSyspath(const QString& syspath);
    QString extractHubPortFromDevicePort(const QString& devicePort);
    
    // udev context
    struct udev* m_udev;
#endif
    
    // Cache management
    QList<DeviceInfo> m_cachedDevices;
    QDateTime m_lastCacheUpdate;
    static const int CACHE_TIMEOUT_MS = 1000; // 1 second cache
};

#endif // LINUXDEVICEMANAGER_H
#endif // __linux__
