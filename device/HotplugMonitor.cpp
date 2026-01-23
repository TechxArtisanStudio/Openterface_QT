#include "HotplugMonitor.h"
#include "DeviceManager.h"
#include <QLoggingCategory>
#include <QMutexLocker>
#include <QtConcurrent>

Q_LOGGING_CATEGORY(log_hotplug_monitor, "opf.device.hotplug")

HotplugMonitor::HotplugMonitor(DeviceManager* deviceManager, QObject *parent)
    : QObject(parent)
    , m_deviceManager(deviceManager)
    , m_timer(new QTimer(this))
    , m_running(false)
    , m_pollInterval(2000)
    , m_changeEventCount(0)
{
    if (!m_deviceManager) {
        qCWarning(log_hotplug_monitor) << "Invalid device manager provided";
        return;
    }
    
    m_timer->setSingleShot(false);
    connect(m_timer, &QTimer::timeout, this, &HotplugMonitor::checkForChangesSlot);
    
    qCDebug(log_hotplug_monitor) << "Hotplug monitor created";
}

HotplugMonitor::~HotplugMonitor()
{
    stop();
    clearCallbacks();
    qCDebug(log_hotplug_monitor) << "Hotplug monitor destroyed";
}

void HotplugMonitor::addCallback(ChangeCallback callback)
{
    m_callbacks.append(callback);
    qCDebug(log_hotplug_monitor) << "Added callback, total callbacks:" << m_callbacks.size();
}

void HotplugMonitor::removeCallback(ChangeCallback callback)
{
    // Note: This is a simplified removal - in practice you might want to use 
    // a more sophisticated callback management system
    qCDebug(log_hotplug_monitor) << "Callback removal requested (simplified implementation)";
}

void HotplugMonitor::clearCallbacks()
{
    m_callbacks.clear();
    qCDebug(log_hotplug_monitor) << "All callbacks cleared";
}

void HotplugMonitor::start(int pollIntervalMs)
{
    if (m_running) {
        qCDebug(log_hotplug_monitor) << "Hotplug monitor already running";
        return;
    }
    
    if (!m_deviceManager) {
        qCWarning(log_hotplug_monitor) << "Cannot start - no device manager";
        return;
    }
    
    qCDebug(log_hotplug_monitor) << "Starting hotplug monitor with interval:" << pollIntervalMs << "ms";
    m_pollInterval = pollIntervalMs;
    m_timer->setInterval(m_pollInterval);
    
    // Take initial snapshot
    {
        QMutexLocker locker(&m_mutex);
        m_lastSnapshot = m_deviceManager->discoverDevices();
        m_initialSnapshot = m_lastSnapshot;
    }
    
    qCDebug(log_hotplug_monitor) << "Initial snapshot contains" << m_lastSnapshot.size() << "devices";
    for (const auto& device : m_lastSnapshot) {
        qCDebug(log_hotplug_monitor) << "  - Device:" << device.portChain;
    }
    
    m_running = true;
    m_timer->start();
    
    emit monitoringStarted();
    qCInfo(log_hotplug_monitor) << "Hotplug monitoring started with interval" << m_pollInterval << "ms";
}

void HotplugMonitor::stop()
{
    if (!m_running) {
        return;
    }
    
    m_timer->stop();
    m_running = false;
    
    emit monitoringStopped();
    qCInfo(log_hotplug_monitor) << "Hotplug monitoring stopped";
}

void HotplugMonitor::updateInterval(int newIntervalMs)
{
    if (newIntervalMs <= 0) {
        qCWarning(log_hotplug_monitor) << "Invalid interval:" << newIntervalMs << "ms, ignoring";
        return;
    }
    
    if (m_pollInterval == newIntervalMs) {
        return; // No change needed
    }
    
    qCDebug(log_hotplug_monitor) << "Updating monitoring interval from" << m_pollInterval << "ms to" << newIntervalMs << "ms";
    m_pollInterval = newIntervalMs;
    
    if (m_running && m_timer) {
        m_timer->setInterval(m_pollInterval);
        qCInfo(log_hotplug_monitor) << "Hotplug monitoring interval updated to" << m_pollInterval << "ms";
    }
}

QList<DeviceInfo> HotplugMonitor::getLastSnapshot() const
{
    QMutexLocker locker(&m_mutex);
    return m_lastSnapshot;
}

void HotplugMonitor::checkForChanges()
{
    if (!m_deviceManager) {
        qCWarning(log_hotplug_monitor) << "No device manager available for change check";
        return;
    }
    
    qCDebug(log_hotplug_monitor) << "Checking for device changes...";
    QList<DeviceInfo> currentDevices = m_deviceManager->discoverDevices();
    
    // Get previous snapshot with mutex protection
    QList<DeviceInfo> previousSnapshot;
    {
        QMutexLocker locker(&m_mutex);
        previousSnapshot = m_lastSnapshot;
    }
    
    qCDebug(log_hotplug_monitor) << "Checking for changes. Current devices:" << currentDevices.size() 
                                << "Previous devices:" << previousSnapshot.size();
    
    // Create change event
    DeviceChangeEvent event = createChangeEvent(currentDevices, previousSnapshot);
    
    if (event.hasChanges()) {
        m_changeEventCount++;
        m_lastChangeTime = QDateTime::currentDateTime();
        
        qCInfo(log_hotplug_monitor) << "Device changes detected:";
        qCInfo(log_hotplug_monitor) << "  Added:" << event.addedDevices.size();
        qCInfo(log_hotplug_monitor) << "  Removed:" << event.removedDevices.size();
        qCInfo(log_hotplug_monitor) << "  Modified:" << event.modifiedDevices.size();
        
        // Log details and emit specific signals
        for (const auto& device : event.addedDevices) {
            qCDebug(log_hotplug_monitor) << "  + Added device:" << device.portChain << ", pid:" << device.pid << "vid:" << device.vid;
            emit newDevicePluggedIn(device);
            break;
        }
        for (const auto& device : event.removedDevices) {
            qCDebug(log_hotplug_monitor) << "  - Removed device:" << device.portChain << ", pid:" << device.pid << "vid:" << device.vid;
            emit deviceUnplugged(device);
        }
        for (const auto& pair : event.modifiedDevices) {
            qCDebug(log_hotplug_monitor) << "  * Modified device:" << pair.second.portChain << ", pid:" << pair.second.pid << "vid:" << pair.second.vid;
        }
        
        // Notify callbacks
        notifyCallbacks(event);
        
        // Emit signal
        qCDebug(log_hotplug_monitor) << "Emitting deviceChangesDetected signal";
        emit deviceChangesDetected(event);
        
        // Update last snapshot with mutex protection
        {
            QMutexLocker locker(&m_mutex);
            m_lastSnapshot = currentDevices;
        }
    } else {
        // Only log this occasionally to avoid spam
        static int noChangeCount = 0;
        noChangeCount++;
        if (noChangeCount % 10 == 0) {
            qCDebug(log_hotplug_monitor) << "No device changes detected after" << noChangeCount << "checks";
        }
    }
}

DeviceChangeEvent HotplugMonitor::createChangeEvent(const QList<DeviceInfo>& current, 
                                                  const QList<DeviceInfo>& previous)
{
    DeviceChangeEvent event;
    event.timestamp = QDateTime::currentDateTime();
    event.currentDevices = current;
    event.initialDevices = m_initialSnapshot;
    
    // Create maps for efficient lookup
    QMap<QString, DeviceInfo> currentMap;
    QMap<QString, DeviceInfo> previousMap;
    
    for (const auto& device : current) {
        currentMap[device.getUniqueKey()] = device;
    }
    
    for (const auto& device : previous) {
        previousMap[device.getUniqueKey()] = device;
    }
    
    // Find added devices
    for (const auto& device : current) {
        QString key = device.getUniqueKey();
        if (!previousMap.contains(key)) {
            event.addedDevices.append(device);
        }
    }
    
    // Find removed devices
    for (const auto& device : previous) {
        QString key = device.getUniqueKey();
        if (!currentMap.contains(key)) {
            event.removedDevices.append(device);
        }
    }
    
    // Find modified devices
    for (const auto& device : current) {
        QString key = device.getUniqueKey();
        if (previousMap.contains(key)) {
            const DeviceInfo& oldDevice = previousMap[key];
            if (device != oldDevice) {
                event.modifiedDevices.append(qMakePair(oldDevice, device));
            }
        }
    }
    
    return event;
}

void HotplugMonitor::notifyCallbacks(const DeviceChangeEvent& event)
{
    for (const auto& callback : m_callbacks) {
        try {
            callback(event);
        } catch (const std::exception& e) {
            qCWarning(log_hotplug_monitor) << "Callback exception:" << e.what();
        } catch (...) {
            qCWarning(log_hotplug_monitor) << "Unknown callback exception";
        }
    }
}

DeviceChangeEvent HotplugMonitor::getCurrentState() const
{
    DeviceChangeEvent event;
    event.timestamp = QDateTime::currentDateTime();
    event.currentDevices = m_lastSnapshot;
    event.initialDevices = m_initialSnapshot;
    return event;
}

DeviceChangeEvent HotplugMonitor::getInitialState() const
{
    DeviceChangeEvent event;
    event.timestamp = QDateTime::currentDateTime();
    event.currentDevices = m_initialSnapshot;
    event.initialDevices = m_initialSnapshot;
    return event;
}

void HotplugMonitor::checkForChangesSlot()
{
    // Run device discovery in background thread to avoid blocking UI
    QtConcurrent::run([this]() {
        checkForChanges();
    });
}
