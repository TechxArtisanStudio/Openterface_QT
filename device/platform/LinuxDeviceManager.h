#ifdef __linux__
#ifndef LINUXDEVICEMANAGER_H
#define LINUXDEVICEMANAGER_H

#include "AbstractPlatformDeviceManager.h"
#include <QLoggingCategory>
#include <QDateTime>
#include <QVariantMap>
#include <QFuture>
#include <QFutureWatcher>
#include <QtConcurrent>

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
    
    // Async device discovery methods
    void discoverDevicesAsync();
    bool isDiscoveryInProgress() const { return m_discoveryInProgress; }
    
signals:
    void devicesDiscovered(const QList<DeviceInfo>& devices);
    void discoveryError(const QString& error);
    
private slots:
    void onAsyncDiscoveryFinished();
    
private:
#ifdef HAVE_LIBUDEV
    struct UdevDeviceData {
        QString syspath;
        QString portChain;
        QString parentSyspath;
        QVariantMap properties;
    };
    
    // Blocking device discovery (for use in background thread)
    QList<DeviceInfo> discoverDevicesBlocking();
    
    // libudev-based device discovery
    QList<UdevDeviceData> findUdevDevicesByVidPid(const QString& subsystem, const QString& vid, const QString& pid);
    QList<UdevDeviceData> findUdevDevices(const QString& subsystem, const QVariantMap& filters = QVariantMap());
    
    // libudev helper functions
    struct udev_device* findUsbParentDevice(struct udev_device* device);
    QVariantMap collectDeviceProperties(struct udev_device* device);
    QString extractPortChainFromSyspath(const QString& syspath);
    QString getPortChainFromSyspath(const QString& syspath);
    QString extractHubPortFromDevicePort(const QString& devicePort);
    
    // Generation-specific device discovery methods
    QList<DeviceInfo> discoverGeneration1DevicesLinux();
    QList<DeviceInfo> discoverGeneration2DevicesLinux();
    QList<DeviceInfo> discoverGeneration3DevicesLinux();
    QList<DeviceInfo> processDeviceMap(const QList<UdevDeviceData>& serialDevices, 
                                     const QList<UdevDeviceData>& usbDevices, 
                                     QMap<QString, DeviceInfo>& deviceMap, 
                                     const QString& generation);
    
    // Generation 2 (Companion device) helper methods
    QString findSerialPortByCompanionDeviceLinux(const UdevDeviceData& companionDevice, const QList<UdevDeviceData>& serialDevices);
    void findAndAssociateInterfaceDevicesLinux(DeviceInfo& deviceInfo, const UdevDeviceData& companionDevice);
    QString calculateExpectedSerialHubPortLinux(const QString& companionHubPort);
    QString extractHubPortFromDevicePortLinux(const QString& devicePort);
    bool arePortChainsRelatedLinux(const QString& portChain1, const QString& portChain2);
    bool isSerialDeviceAssociatedWithCompanionLinux(const UdevDeviceData& serialDevice, const UdevDeviceData& companionDevice);
    
    // udev context
    struct udev* m_udev;
#endif
    
    // Cache management
    QList<DeviceInfo> m_cachedDevices;
    QDateTime m_lastCacheUpdate;
    static const int CACHE_TIMEOUT_MS = 5000; // 1 second cache
    
    // Async discovery
    QFutureWatcher<QList<DeviceInfo>>* m_futureWatcher;
    bool m_discoveryInProgress;
};

#endif // LINUXDEVICEMANAGER_H
#endif // __linux__
