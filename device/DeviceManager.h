#ifndef DEVICEMANAGER_H
#define DEVICEMANAGER_H

#include <QObject>
#include <QTimer>
#include <QThread>
#include <QMutex>
#include <QLoggingCategory>
#include "DeviceInfo.h"
#include "HotplugMonitor.h"

class AbstractPlatformDeviceManager;

Q_DECLARE_LOGGING_CATEGORY(log_device_manager)

class DeviceManager : public QObject
{
    Q_OBJECT
    
public:
    // Singleton pattern
    static DeviceManager& getInstance() {
        static DeviceManager instance;
        return instance;
    }
    
    // Delete copy constructor and assignment operator
    DeviceManager(const DeviceManager&) = delete;
    DeviceManager& operator=(const DeviceManager&) = delete;
    
    ~DeviceManager();
    
    // Force device checking (replaces SerialPortManager functionality)
    void checkForChanges();
    void forceRefresh();
    
    // Device discovery
    QList<DeviceInfo> discoverDevices();
    QList<DeviceInfo> getDevicesByPortChain(const QString& portChain);
    QStringList getAvailablePortChains();
    
    // Device selection
    DeviceInfo selectDeviceByPortChain(const QString& portChain);
    DeviceInfo getFirstAvailableDevice();
    
    // Hotplug monitoring
    void startHotplugMonitoring(int intervalMs = 5000);
    void stopHotplugMonitoring();
    bool isMonitoring() const { return m_monitoring; }
    
    // Current state
    QList<DeviceInfo> getCurrentDevices() const;
    DeviceInfo getCurrentSelectedDevice() const { return m_selectedDevice; }
    void setCurrentSelectedDevice(const DeviceInfo& device) { m_selectedDevice = device; }
    
    // Platform manager access
    AbstractPlatformDeviceManager* getPlatformManager() const { return m_platformManager; }
    
    // HotplugMonitor-like functionality (replaces SerialPortManager hotplug)
    HotplugMonitor* getHotplugMonitor() const { return m_hotplugMonitor; }
    
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
    // Private constructor for singleton
    explicit DeviceManager();
    
    void initializePlatformManager();
    void compareDeviceSnapshots(const QList<DeviceInfo>& current, 
                               const QList<DeviceInfo>& previous);
    DeviceInfo findDeviceByKey(const QList<DeviceInfo>& devices, const QString& key);
    
    AbstractPlatformDeviceManager* m_platformManager;
    QTimer* m_hotplugTimer;
    HotplugMonitor* m_hotplugMonitor;
    QList<DeviceInfo> m_lastSnapshot;
    QList<DeviceInfo> m_currentDevices;
    DeviceInfo m_selectedDevice;
    mutable QMutex m_mutex;
    bool m_monitoring;
    QString m_platformName;
};

#endif // DEVICEMANAGER_H
