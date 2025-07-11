#ifdef __linux__
#ifndef LINUXDEVICEMANAGER_H
#define LINUXDEVICEMANAGER_H

#include "AbstractPlatformDeviceManager.h"
#include <QLoggingCategory>
#include <QDir>
#include <QProcess>
#include <QRegularExpression>

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
    struct USBDeviceData {
        QString portChain;
        QList<QVariantMap> siblings;
        QList<QVariantMap> children;
        QString devicePath;
        QVariantMap deviceInfo;
    };
    
    // USB device discovery
    QList<USBDeviceData> findUSBDevicesWithVidPid(const QString& vid, const QString& pid);
    QString buildLinuxPortChain(const QString& devpath);
    
    // Device path finding by port chain
    QList<QVariantMap> findSerialPortsByPortChain(const QString& serialVid, const QString& serialPid, const QString& targetPortChain);
    QList<QVariantMap> findHIDDevicesByPortChain(const QString& hidVid, const QString& hidPid, const QString& targetPortChain);
    QList<QVariantMap> findVideoDevicesByPortChain(const QString& targetPortChain);
    QList<QVariantMap> findAudioDevicesByPortChain(const QString& targetPortChain);
    
    // Utility methods
    QString extractMainPortFromChain(const QString& portChain);
    QVariantMap findChildDevices(const QString& devicePath);
    QVariantMap findSiblingDevices(const QString& devicePath);
    
    // sysfs helpers
    QString readSysfsAttribute(const QString& devicePath, const QString& attribute);
    QStringList listSysfsDirectory(const QString& path);
    bool deviceExists(const QString& devicePath);
    
    // udev helpers (if available)
    QList<QVariantMap> enumerateUdevDevices(const QString& subsystem, const QVariantMap& filters = QVariantMap());
    
    // Device matching helpers
    bool matchesVidPid(const QString& devicePath, const QString& vid, const QString& pid);
    bool matchesVidPidAdvanced(const QString& devicePath, const QString& vid, const QString& pid);
    QString getDeviceVendorId(const QString& devicePath);
    QString getDeviceProductId(const QString& devicePath);
    QString getDevicePortChain(const QString& devicePath);
    
    // Enhanced device collection
    QList<QVariantMap> collectAllDevicesByPortChain(const QString& targetPortChain);
    
    // Cache management
    QList<DeviceInfo> m_cachedDevices;
    QDateTime m_lastCacheUpdate;
    static const int CACHE_TIMEOUT_MS = 1000; // 1 second cache
};

#endif // LINUXDEVICEMANAGER_H
#endif // __linux__
