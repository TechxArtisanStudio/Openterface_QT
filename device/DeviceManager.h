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
class SerialPortManager;
class VideoHid;

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
    void discoverDevicesAsync(); // Non-blocking async discovery
    QList<DeviceInfo> getDevicesByPortChain(const QString& portChain);
    QStringList getAvailablePortChains();
    
    // Device selection
    DeviceInfo selectDeviceByPortChain(const QString& portChain);
    DeviceInfo getFirstAvailableDevice();
    
    // Device switching
    struct DeviceSwitchResult {
        bool success;
        bool cameraSuccess;
        bool hidSuccess; 
        bool serialSuccess;
        QString statusMessage;
        DeviceInfo selectedDevice;
    };
    DeviceSwitchResult switchToDeviceByPortChain(const QString& portChain);
    
    // Complete device switching (with all managers) - for use by UI components
    template<typename CameraManagerType>
    DeviceSwitchResult switchToDeviceByPortChainComplete(const QString& portChain, CameraManagerType* cameraManager) {
        DeviceSwitchResult result = switchToDeviceByPortChain(portChain);
        
        if (!result.selectedDevice.isValid()) {
            result.statusMessage = "Invalid device selected";
            return result;
        }
        
        // Handle serial port switching if device has serial component
        if (result.selectedDevice.hasSerialPort()) {
            // Get SerialPortManager singleton and switch to the device
            // Note: We'll use extern declaration or include in implementation
            result.serialSuccess = switchSerialPortByPortChain(portChain);
            if (result.serialSuccess) {
                qCInfo(log_device_manager) << "✓ Serial port switched to device at port:" << portChain;
            } else {
                qCWarning(log_device_manager) << "Failed to switch serial port to device at port:" << portChain;
                result.statusMessage += " (Serial port switch failed)";
            }
        } else {
            result.serialSuccess = true; // No serial device to switch
        }
        
        // Handle HID switching if device has HID component
        if (result.selectedDevice.hasHidDevice()) {
            result.hidSuccess = switchHIDDeviceByPortChain(portChain);
            if (result.hidSuccess) {
                qCInfo(log_device_manager) << "✓ HID device switched to device at port:" << portChain;
            } else {
                qCWarning(log_device_manager) << "Failed to switch HID device to device at port:" << portChain;
                result.statusMessage += " (HID switch failed)";
            }
        } else {
            result.hidSuccess = true; // No HID device to switch
        }
        
        // Handle camera switching if camera manager is provided and device has camera
        if (cameraManager && result.selectedDevice.hasCameraDevice()) {
            result.cameraSuccess = cameraManager->switchToCameraDeviceByPortChain(portChain);
            if (result.cameraSuccess) {
                qCInfo(log_device_manager) << "✓ Camera switched to device at port:" << portChain;
            } else {
                qCWarning(log_device_manager) << "Failed to switch camera to device at port:" << portChain;
                result.statusMessage += " (Camera switch failed)";
            }
        } else if (result.selectedDevice.hasCameraDevice()) {
            result.cameraSuccess = false; // Camera device exists but no manager provided
            result.statusMessage += " (Camera manager not provided)";
        } else {
            result.cameraSuccess = true; // No camera device to switch
        }
        
        // Update overall success based on component switches
        bool allComponentsSuccessful = result.serialSuccess && result.hidSuccess && result.cameraSuccess;
        if (result.success && !allComponentsSuccessful) {
            result.success = false; // Mark as failure if any component failed
        }
        
        // Update status message with overall result
        if (result.success && allComponentsSuccessful) {
            result.statusMessage = QString("Successfully switched all components to device at port %1").arg(portChain);
        } else if (!result.success) {
            if (result.statusMessage.isEmpty()) {
                result.statusMessage = QString("Failed to switch to device at port %1").arg(portChain);
            }
        }
        
        return result;
    }
    
    // Keep the original method for backward compatibility
    template<typename CameraManagerType>
    DeviceSwitchResult switchToDeviceByPortChainWithCamera(const QString& portChain, CameraManagerType* cameraManager) {
        return switchToDeviceByPortChainComplete(portChain, cameraManager);
    }
    
    // Helper methods for component switching
    bool switchSerialPortByPortChain(const QString& portChain);
    bool switchHIDDeviceByPortChain(const QString& portChain);
    
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
