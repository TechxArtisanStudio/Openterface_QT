#include "DeviceManager.h"
#include "platform/DeviceFactory.h"
#include "platform/AbstractPlatformDeviceManager.h"
#ifdef __linux__
#include "platform/LinuxDeviceManager.h"
#endif
#include "HotplugMonitor.h"
#include "../ui/globalsetting.h"
#include "../video/videohid.h"
#include "../serial/SerialPortManager.h"
#include "../host/audiomanager.h"
#include <QMutexLocker>
#include <QtConcurrent>

Q_LOGGING_CATEGORY(log_device_manager, "opf.device.manager")

DeviceManager::DeviceManager()
    : QObject(nullptr)
    , m_platformManager(nullptr)
    , m_hotplugTimer(new QTimer(this))
    , m_hotplugMonitor(nullptr)
    , m_monitoring(false)
{
    initializePlatformManager();
    
    // Create HotplugMonitor instance
    m_hotplugMonitor = new HotplugMonitor(this, this);
    
    // Setup hotplug timer
    m_hotplugTimer->setSingleShot(false);
    connect(m_hotplugTimer, &QTimer::timeout, this, &DeviceManager::onHotplugTimerTimeout);
    
    // Start the HotplugMonitor to enable signal emission
    if (m_hotplugMonitor) {
        m_hotplugMonitor->start(2000); // Start with 2 second interval
        qCDebug(log_device_manager) << "HotplugMonitor started";
    }
    
    // Auto-start hotplug monitoring
    startHotplugMonitoring();
    
    qCDebug(log_device_manager) << "Device Manager singleton initialized for platform:" << m_platformName;
}

DeviceManager::~DeviceManager()
{
    stopHotplugMonitoring();
    if (m_hotplugMonitor) {
        m_hotplugMonitor->stop();
        delete m_hotplugMonitor;
    }
    if (m_platformManager) {
        delete m_platformManager;
    }
}

void DeviceManager::initializePlatformManager()
{
    m_platformManager = DeviceFactory::createDeviceManager(this);
    if (m_platformManager) {
        m_platformName = m_platformManager->getPlatformName();
        qCDebug(log_device_manager) << "Platform manager created:" << m_platformName;
    } else {
        qCWarning(log_device_manager) << "Failed to create platform manager";
        m_platformName = "Unknown";
    }
}

QList<DeviceInfo> DeviceManager::discoverDevices()
{
    QMutexLocker locker(&m_mutex);
    
    if (!m_platformManager) {
        qCWarning(log_device_manager) << "No platform manager available";
        return QList<DeviceInfo>();
    }
    
    try {
        QList<DeviceInfo> devices = m_platformManager->discoverDevices();
        m_currentDevices = devices;
        qCDebug(log_device_manager) << "Discovered" << devices.size() << "devices";
        return devices;
    } catch (const std::exception& e) {
        qCWarning(log_device_manager) << "Error discovering devices:" << e.what();
        emit errorOccurred(QString("Device discovery failed: %1").arg(e.what()));
        return QList<DeviceInfo>();
    }
}

void DeviceManager::discoverDevicesAsync()
{
    if (!m_platformManager) {
        qCWarning(log_device_manager) << "No platform manager available for async discovery";
        return;
    }
    
#ifdef __linux__
    // Check if the platform manager supports async discovery
    if (auto* linuxManager = qobject_cast<LinuxDeviceManager*>(m_platformManager)) {
        // Connect to Linux-specific async discovery signals
        connect(linuxManager, &LinuxDeviceManager::devicesDiscovered,
                this, [this](const QList<DeviceInfo>& devices) {
                    QMutexLocker locker(&m_mutex);
                    m_currentDevices = devices;
                    qCDebug(log_device_manager) << "Async discovery completed with" << devices.size() << "devices";
                    emit devicesChanged(devices);
                }, Qt::UniqueConnection);
        
        connect(linuxManager, &LinuxDeviceManager::discoveryError,
                this, [this](const QString& error) {
                    qCWarning(log_device_manager) << "Async discovery error:" << error;
                    emit errorOccurred(error);
                }, Qt::UniqueConnection);
        
        linuxManager->discoverDevicesAsync();
    } else
#endif
    {
        // For other platforms, fall back to synchronous discovery
        // but run it in a background thread to avoid blocking
        auto future = QtConcurrent::run([this]() {
            QList<DeviceInfo> devices = discoverDevices();
            emit devicesChanged(devices);
        });
    }
}

QList<DeviceInfo> DeviceManager::getDevicesByPortChain(const QString& portChain)
{
    if (!m_platformManager) {
        return QList<DeviceInfo>();
    }
    
    // First try exact PortChain match, then try companion PortChain match for USB 3.0 devices
    return m_platformManager->getDevicesByAnyPortChain(portChain);
}

QStringList DeviceManager::getAvailablePortChains()
{
    if (!m_platformManager) {
        return QStringList();
    }
    
    return m_platformManager->getAvailablePortChains();
}

QString DeviceManager::getCompositePortChain(const QString& requestedPortChain)
{
    if (!m_platformManager || requestedPortChain.isEmpty()) {
        return requestedPortChain;
    }
    
    QList<DeviceInfo> devices = m_platformManager->getDevicesByAnyPortChain(requestedPortChain);
    if (devices.isEmpty()) {
        return requestedPortChain;
    }
    
    DeviceInfo device = devices.first();
    return device.getCompositePortChain();
}

QString DeviceManager::getSerialPortChain(const QString& requestedPortChain)
{
    if (!m_platformManager || requestedPortChain.isEmpty()) {
        return requestedPortChain;
    }
    
    QList<DeviceInfo> devices = m_platformManager->getDevicesByAnyPortChain(requestedPortChain);
    if (devices.isEmpty()) {
        return requestedPortChain;
    }
    
    DeviceInfo device = devices.first();
    return device.hasCompanionPortChain() ? device.companionPortChain : device.portChain;
}

QString DeviceManager::getCompanionPortChain(const QString& portChain)
{
    if (!m_platformManager || portChain.isEmpty()) {
        return QString();
    }
    
    QList<DeviceInfo> devices = m_platformManager->getDevicesByAnyPortChain(portChain);
    if (devices.isEmpty()) {
        return QString();
    }
    
    DeviceInfo device = devices.first();
    return device.companionPortChain;
}

DeviceInfo DeviceManager::selectDeviceByPortChain(const QString& portChain)
{
    QList<DeviceInfo> devices = getDevicesByPortChain(portChain);
    if (!devices.isEmpty()) {
        m_selectedDevice = devices.first();
        qCDebug(log_device_manager) << "Selected device:" << m_selectedDevice.portChain;
        return m_selectedDevice;
    }
    
    qCWarning(log_device_manager) << "No device found for port chain:" << portChain;
    return DeviceInfo();
}

DeviceInfo DeviceManager::getFirstAvailableDevice()
{
    QList<DeviceInfo> devices = discoverDevices();
    if (!devices.isEmpty()) {
        m_selectedDevice = devices.first();
        qCDebug(log_device_manager) << "Selected first available device:" << m_selectedDevice.portChain;
        return m_selectedDevice;
    }
    
    qCWarning(log_device_manager) << "No devices available";
    return DeviceInfo();
}

DeviceManager::DeviceSwitchResult DeviceManager::switchToDeviceByPortChain(const QString& portChain)
{
    DeviceSwitchResult result = {false, false, false, false, false, "", DeviceInfo()};
    
    if (portChain.isEmpty()) {
        result.statusMessage = "Cannot switch to device with empty port chain";
        qCWarning(log_device_manager) << result.statusMessage;
        return result;
    }
    
    qCDebug(log_device_manager) << "Switching to device by port chain:" << portChain;
    
    // Get the selected device info from DeviceManager
    QList<DeviceInfo> devices = getDevicesByPortChain(portChain);
    if (devices.isEmpty()) {
        result.statusMessage = QString("No device found for port chain: %1").arg(portChain);
        qCWarning(log_device_manager) << result.statusMessage;
        return result;
    }
    
    DeviceInfo selectedDevice = devices.first();
    result.selectedDevice = selectedDevice;
    
    // Update global settings first
    GlobalSetting::instance().setOpenterfacePortChain(portChain);
    
    QStringList successMessages;
    QStringList failureMessages;
    
    // Switch camera device
    if (selectedDevice.hasCameraDevice()) {
        // Note: Camera switching needs to be handled by the caller since
        // DeviceManager shouldn't have direct dependency on CameraManager
        qCDebug(log_device_manager) << "Camera switching needs to be handled by caller";
    }
    
    // Switch HID device
    if (selectedDevice.hasHidDevice()) {
        QString hidPortChain = selectedDevice.getCompositePortChain();
        result.hidSuccess = VideoHid::getInstance().switchToHIDDeviceByPortChain(hidPortChain);
        if (result.hidSuccess) {
            successMessages << "HID device switched";
            qCInfo(log_device_manager) << "✓ HID device switched to device at port:" << hidPortChain;
        } else {
            failureMessages << "HID device switch failed";
            qCWarning(log_device_manager) << "Failed to switch HID device to port:" << hidPortChain;
        }
    }
    
    // Switch audio device
    if (selectedDevice.hasAudioDevice()) {
        QString audioPortChain = selectedDevice.getCompositePortChain();
        result.audioSuccess = AudioManager::getInstance().switchToAudioDeviceByPortChain(audioPortChain);
        if (result.audioSuccess) {
            successMessages << "Audio device switched";
            qCInfo(log_device_manager) << "✓ Audio device switched to device at port:" << audioPortChain;
        } else {
            failureMessages << "Audio device switch failed";
            qCWarning(log_device_manager) << "Failed to switch audio device to port:" << audioPortChain;
        }
    }
    
    // Switch serial port device
    if (selectedDevice.hasSerialPort()) {
        QString serialPortChain = selectedDevice.getSerialPortChain();
        result.serialSuccess = SerialPortManager::getInstance().switchSerialPortByPortChain(serialPortChain);
        
        if (!result.serialSuccess && !selectedDevice.companionPortChain.isEmpty()) {
            // For USB 3.0 devices like KVMGO, try companion port chain for serial
            qCDebug(log_device_manager) << "Serial switch failed, trying companion port chain:" << selectedDevice.companionPortChain;
            result.serialSuccess = SerialPortManager::getInstance().switchSerialPortByPortChain(selectedDevice.companionPortChain);
            if (result.serialSuccess) {
                qCInfo(log_device_manager) << "✓ Serial port switched using companion port chain:" << selectedDevice.companionPortChain;
            }
        }
        
        if (result.serialSuccess) {
            successMessages << "Serial port switched";
            qCInfo(log_device_manager) << "✓ Serial port switched to device at port:" << (selectedDevice.companionPortChain.isEmpty() ? serialPortChain : selectedDevice.companionPortChain);
        } else {
            failureMessages << "Serial port switch failed";
            qCWarning(log_device_manager) << "Failed to switch serial port to device at port:" << serialPortChain << " or companion:" << selectedDevice.companionPortChain;
        }
    }
    
    // Update device manager selection
    setCurrentSelectedDevice(selectedDevice);
    
    // Determine overall success and create status message
    bool hasSuccess = result.hidSuccess || result.serialSuccess || result.audioSuccess;
    bool hasFailure = (!result.hidSuccess && selectedDevice.hasHidDevice()) || 
                      (!result.serialSuccess && selectedDevice.hasSerialPort()) ||
                      (!result.audioSuccess && selectedDevice.hasAudioDevice());
    
    if (hasSuccess && !hasFailure) {
        result.success = true;
        result.statusMessage = QString("Successfully switched to device at port %1. %2")
                              .arg(portChain)
                              .arg(successMessages.join(", "));
    } else if (hasSuccess && hasFailure) {
        result.success = false;  // Partial success is treated as failure
        result.statusMessage = QString("Partially switched to device at port %1. Success: %2. Failed: %3")
                              .arg(portChain)
                              .arg(successMessages.join(", "))
                              .arg(failureMessages.join(", "));
    } else {
        result.success = false;
        result.statusMessage = QString("Failed to switch to device at port %1. %2")
                              .arg(portChain)
                              .arg(failureMessages.join(", "));
    }
    
    qCDebug(log_device_manager) << result.statusMessage;
    return result;
}

void DeviceManager::startHotplugMonitoring(int intervalMs)
{
    if (m_monitoring) {
        qCDebug(log_device_manager) << "Hotplug monitoring already started";
        return;
    }
    
    qCDebug(log_device_manager) << "Starting hotplug monitoring with interval:" << intervalMs << "ms";
    
    // Take initial snapshot
    m_lastSnapshot = discoverDevices();
    
    // Start monitoring
    m_hotplugTimer->setInterval(intervalMs);
    m_hotplugTimer->start();
    m_monitoring = true;
    
    emit monitoringStarted();
}

void DeviceManager::stopHotplugMonitoring()
{
    if (!m_monitoring) {
        return;
    }
    
    qCDebug(log_device_manager) << "Stopping hotplug monitoring";
    
    m_hotplugTimer->stop();
    m_monitoring = false;
    
    emit monitoringStopped();
}

QList<DeviceInfo> DeviceManager::getCurrentDevices() const
{
    QMutexLocker locker(&m_mutex);
    return m_currentDevices;
}

void DeviceManager::onHotplugTimerTimeout()
{
    if (!m_monitoring) {
        return;
    }
    
    QList<DeviceInfo> currentDevices = discoverDevices();
    compareDeviceSnapshots(currentDevices, m_lastSnapshot);
    m_lastSnapshot = currentDevices;
    
    emit devicesChanged(currentDevices);
}

void DeviceManager::compareDeviceSnapshots(const QList<DeviceInfo>& current, 
                                          const QList<DeviceInfo>& previous)
{
    // Find added devices
    for (const DeviceInfo& currentDevice : current) {
        QString key = currentDevice.getUniqueKey();
        DeviceInfo previousDevice = findDeviceByKey(previous, key);
        
        if (!previousDevice.isValid()) {
            // Device was added
            qCDebug(log_device_manager) << "Device added:" << currentDevice.portChain;
            emit deviceAdded(currentDevice);
        } else if (!(currentDevice == previousDevice)) {
            // Device was modified
            qCDebug(log_device_manager) << "Device modified:" << currentDevice.portChain;
            emit deviceModified(previousDevice, currentDevice);
        }
    }
    
    // Find removed devices
    for (const DeviceInfo& previousDevice : previous) {
        QString key = previousDevice.getUniqueKey();
        DeviceInfo currentDevice = findDeviceByKey(current, key);
        
        if (!currentDevice.isValid()) {
            // Device was removed
            qCDebug(log_device_manager) << "Device removed:" << previousDevice.portChain;
            emit deviceRemoved(previousDevice);
        }
    }
}

DeviceInfo DeviceManager::findDeviceByKey(const QList<DeviceInfo>& devices, const QString& key)
{
    for (const DeviceInfo& device : devices) {
        if (device.getUniqueKey() == key) {
            return device;
        }
    }
    return DeviceInfo(); // Invalid device
}

void DeviceManager::checkForChanges()
{
    if (m_hotplugMonitor) {
        m_hotplugMonitor->checkForChanges();
    } else {
        // Fallback to manual check
        onHotplugTimerTimeout();
    }
}

void DeviceManager::forceRefresh()
{
    qCDebug(log_device_manager) << "Force refreshing device list";
    
    // Clear platform manager cache if available
    if (m_platformManager) {
        m_platformManager->clearCache();
    }
    
    // Trigger device discovery and notify of changes
    QList<DeviceInfo> currentDevices = discoverDevices();
    compareDeviceSnapshots(currentDevices, m_lastSnapshot);
    m_lastSnapshot = currentDevices;
    
    emit devicesChanged(currentDevices);
}

bool DeviceManager::switchHIDDeviceByPortChain(const QString& portChain)
{
    qCDebug(log_device_manager) << "Attempting to switch HID device to device at port chain:" << portChain;
    
    try {
        QString hidPortChain = getCompositePortChain(portChain);
        VideoHid& videoHid = VideoHid::getInstance();
        bool success = videoHid.switchToHIDDeviceByPortChain(hidPortChain);
        
        if (success) {
            qCInfo(log_device_manager) << "✓ Successfully switched HID device to port chain:" << hidPortChain;
        } else {
            qCWarning(log_device_manager) << "Failed to switch HID device to port chain:" << hidPortChain;
        }
        
        return success;
    } catch (const std::exception& e) {
        qCCritical(log_device_manager) << "Exception while switching HID device:" << e.what();
        return false;
    }
}

bool DeviceManager::switchAudioDeviceByPortChain(const QString& portChain)
{
    qCDebug(log_device_manager) << "Attempting to switch audio device to device at port chain:" << portChain;
    
    try {
        QString audioPortChain = getCompositePortChain(portChain);
        AudioManager& audioManager = AudioManager::getInstance();
        bool success = audioManager.switchToAudioDeviceByPortChain(audioPortChain);
        
        if (success) {
            qCInfo(log_device_manager) << "✓ Successfully switched audio device to port chain:" << audioPortChain;
        } else {
            qCWarning(log_device_manager) << "Failed to switch audio device to port chain:" << audioPortChain;
        }
        
        return success;
    } catch (const std::exception& e) {
        qCCritical(log_device_manager) << "Exception while switching audio device:" << e.what();
        return false;
    }
}

bool DeviceManager::switchSerialPortByPortChain(const QString& portChain)
{
    qCDebug(log_device_manager) << "Attempting to switch serial port to device at port chain:" << portChain;
    
    try {
        QString serialPortChain = getSerialPortChain(portChain);
        SerialPortManager& serialPortManager = SerialPortManager::getInstance();
        bool success = serialPortManager.switchSerialPortByPortChain(serialPortChain);
        
        if (success) {
            qCInfo(log_device_manager) << "✓ Successfully switched serial port to port chain:" << serialPortChain;
        } else {
            qCWarning(log_device_manager) << "Failed to switch serial port to port chain:" << serialPortChain;
        }
        
        return success;
    } catch (const std::exception& e) {
        qCCritical(log_device_manager) << "Exception while switching serial port:" << e.what();
        return false;
    }
}
