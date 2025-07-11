#ifndef DEVICEMANAGER_H
#define DEVICEMANAGER_H

#include <QObject>
#include <QTimer>
#include <QThread>
#include <QMutex>
#include <QLoggingCategory>
#include "DeviceInfo.h"

class AbstractPlatformDeviceManager;

Q_DECLARE_LOGGING_CATEGORY(log_device_manager)

class DeviceManager : public QObject
{
    Q_OBJECT
    
public:
    explicit DeviceManager(QObject *parent = nullptr);
    ~DeviceManager();
    
    // Device discovery
    QList<DeviceInfo> discoverDevices();
    QList<DeviceInfo> getDevicesByPortChain(const QString& portChain);
    QStringList getAvailablePortChains();
    
    // Device selection
    DeviceInfo selectDeviceByPortChain(const QString& portChain);
    DeviceInfo getFirstAvailableDevice();
    
    // Hotplug monitoring
    void startHotplugMonitoring(int intervalMs = 2000);
    void stopHotplugMonitoring();
    bool isMonitoring() const { return m_monitoring; }
    
    // Current state
    QList<DeviceInfo> getCurrentDevices() const;
    DeviceInfo getCurrentSelectedDevice() const { return m_selectedDevice; }
    
    // Platform manager access
    AbstractPlatformDeviceManager* getPlatformManager() const { return m_platformManager; }
    
signals:
    void deviceAdded(const DeviceInfo& device);
    void deviceRemoved(const DeviceInfo& device);
    void deviceModified(const DeviceInfo& oldDevice, const DeviceInfo& newDevice);
    void devicesChanged(const QList<DeviceInfo>& currentDevices);
    void monitoringStarted();
    void monitoringStopped();
    void errorOccurred(const QString& error);
    
private slots:
    void onHotplugTimerTimeout();
    
private:
    void initializePlatformManager();
    void compareDeviceSnapshots(const QList<DeviceInfo>& current, 
                               const QList<DeviceInfo>& previous);
    DeviceInfo findDeviceByKey(const QList<DeviceInfo>& devices, const QString& key);
    
    AbstractPlatformDeviceManager* m_platformManager;
    QTimer* m_hotplugTimer;
    QList<DeviceInfo> m_lastSnapshot;
    QList<DeviceInfo> m_currentDevices;
    DeviceInfo m_selectedDevice;
    mutable QMutex m_mutex;
    bool m_monitoring;
    QString m_platformName;
};

#endif // DEVICEMANAGER_H
