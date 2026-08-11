#include "HotplugDebounceManager.h"
#include <QDebug>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(log_debounce, "opf.device.debounce")

namespace device {

HotplugDebounceManager::HotplugDebounceManager(QObject *parent)
    : QObject(parent)
    , m_fastScanTimer(new QTimer(this))
    , m_fastScanDurationTimer(new QTimer(this))
    , m_fastScanning(false)
    , m_fastScanRemainingCount(0)
{
    // Setup fast scan timer (300ms interval)
    m_fastScanTimer->setSingleShot(false);
    connect(m_fastScanTimer, &QTimer::timeout,
            this, &HotplugDebounceManager::onFastScanTimeout);

    // Setup duration timer (fires once after 60 seconds)
    m_fastScanDurationTimer->setSingleShot(true);
    connect(m_fastScanDurationTimer, &QTimer::timeout,
            this, [this]() {
                qCInfo(log_debounce) << "Fast scan window expired (60s)";
                stopFastScan();
            });

    qCDebug(log_debounce) << "HotplugDebounceManager created";
}

HotplugDebounceManager::~HotplugDebounceManager()
{
    stopFastScan();

    // Clean up all debounce timers
    for (auto timer : m_debounceTimers) {
        timer->deleteLater();
    }
    m_debounceTimers.clear();

    qCDebug(log_debounce) << "HotplugDebounceManager destroyed";
}

void HotplugDebounceManager::handleDeviceRemoved(const QString &deviceId, const QString &devicePath)
{
    Q_UNUSED(devicePath);
    qCInfo(log_debounce) << "Device removal handled - ID:" << deviceId;

    // Check if this device is already tracked
    if (m_devices.contains(deviceId)) {
        DeviceInfo& info = m_devices[deviceId];

        // If already in Removing state, debounce - skip
        if (info.state == DeviceState::Removing || info.state == DeviceState::Removed) {
            qCDebug(log_debounce) << "Device already being removed, debouncing";
            emit debounceTriggered(deviceId, "removed_duplicate");
            return;
        }

        // Check debounce window
        if (m_debounceTimers.contains(deviceId)) {
            QTimer* timer = m_debounceTimers[deviceId];
            if (timer->isActive()) {
                qCDebug(log_debounce) << "Device removed within debounce window, debouncing";
                emit debounceTriggered(deviceId, "removed_within_debounce");
                return;
            }
        }

        // Start debounce timer for this removal
        startDebounceTimer(deviceId);

        // Update state
        setDeviceState(deviceId, DeviceState::Removing);

        // Add to removed set for rapid reconnect detection
        m_removedDevices.insert(deviceId);

        // Update full state
        info.state = DeviceState::Removed;
        info.lastStateChange = QDateTime::currentDateTime();
    } else {
        // New device that we haven't seen before being removed
        DeviceInfo info;
        info.deviceId = deviceId;
        info.devicePath = devicePath;
        info.state = DeviceState::Removed;
        info.lastStateChange = QDateTime::currentDateTime();
        m_devices[deviceId] = info;
        m_removedDevices.insert(deviceId);
        startDebounceTimer(deviceId);
    }

    // Start fast scan window on device removal
    startFastScan();
}

bool HotplugDebounceManager::handleDeviceAdded(const QString &deviceId, const QString &devicePath)
{
    bool isRapidReconnect = false;

    // Check if this is a rapid reconnect (device was recently removed)
    if (m_removedDevices.contains(deviceId)) {
        isRapidReconnect = true;
        qCInfo(log_debounce) << "Rapid reconnect detected for device:" << deviceId
                            << "path:" << devicePath;

        // Remove from removed set
        m_removedDevices.remove(deviceId);

        // Cancel any pending debounce for this device
        if (m_debounceTimers.contains(deviceId)) {
            QTimer* timer = m_debounceTimers[deviceId];
            if (timer->isActive()) {
                timer->stop();
            }
        }

        // Emit rapid reconnect signal
        emit deviceRapidlyReconnected(deviceId, devicePath);
    }

    // Update device info
    if (m_devices.contains(deviceId)) {
        DeviceInfo& info = m_devices[deviceId];
        info.devicePath = devicePath;
        info.state = DeviceState::Stable;
        info.lastStateChange = QDateTime::currentDateTime();
    } else {
        DeviceInfo info;
        info.deviceId = deviceId;
        info.devicePath = devicePath;
        info.state = DeviceState::Stable;
        info.lastStateChange = QDateTime::currentDateTime();
        m_devices[deviceId] = info;

        // This is a genuinely new device (not a reconnect)
        if (!isRapidReconnect && m_fastScanning) {
            qCInfo(log_debounce) << "New device detected during fast scan:" << deviceId;
            emit newDeviceDetected(deviceId, devicePath);
        }
    }

    return isRapidReconnect;
}

bool HotplugDebounceManager::isFastScanning() const
{
    return m_fastScanning;
}

int HotplugDebounceManager::getCurrentPollInterval() const
{
    return m_fastScanning ? FAST_SCAN_INTERVAL_MS : NORMAL_POLL_INTERVAL_MS;
}

void HotplugDebounceManager::stopFastScan()
{
    if (!m_fastScanning) return;

    m_fastScanning = false;
    m_fastScanTimer->stop();
    m_fastScanDurationTimer->stop();

    qCInfo(log_debounce) << "Fast scan stopped - returning to normal polling";
    emit fastScanEnded();
}

DeviceState HotplugDebounceManager::getDeviceState(const QString &deviceId) const
{
    if (m_devices.contains(deviceId)) {
        return m_devices[deviceId].state;
    }
    return DeviceState::Stable;
}

void HotplugDebounceManager::resetAllStates()
{
    stopFastScan();

    for (auto timer : m_debounceTimers) {
        timer->deleteLater();
    }
    m_debounceTimers.clear();

    m_devices.clear();
    m_removedDevices.clear();
    m_fastScanning = false;
    m_fastScanRemainingCount = 0;

    qCInfo(log_debounce) << "All states reset";
}

void HotplugDebounceManager::startFastScan()
{
    if (m_fastScanning) {
        // Already running, just refresh the duration timer
        m_fastScanDurationTimer->start(FAST_SCAN_DURATION_MS);
        return;
    }

    m_fastScanning = true;
    m_fastScanDurationTimer->start(FAST_SCAN_DURATION_MS);
    m_fastScanTimer->start(FAST_SCAN_INTERVAL_MS);

    qCInfo(log_debounce) << "Fast scan started - interval:" << FAST_SCAN_INTERVAL_MS
                        << "ms, duration:" << FAST_SCAN_DURATION_MS << "ms";
    emit fastScanStarted();
}

void HotplugDebounceManager::onFastScanTimeout()
{
    if (!m_fastScanning) return;

    m_fastScanRemainingCount++;

    // Scan for devices in the removed set to see if they've reappeared
    // This is a polling check - the actual detection happens in handleDeviceAdded
    // when the caller reports a device addition

    qCDebug(log_debounce) << "Fast scan check #" << m_fastScanRemainingCount;
}

void HotplugDebounceManager::onDebounceTimeout(const QString &deviceId)
{
    Q_UNUSED(deviceId);
    // Debounce period expired - the state change is now confirmed
    qCDebug(log_debounce) << "Debounce timer expired for device:" << deviceId;

    // Clean up the timer
    if (m_debounceTimers.contains(deviceId)) {
        QTimer* timer = m_debounceTimers.take(deviceId);
        timer->deleteLater();
    }
}

void HotplugDebounceManager::setDeviceState(const QString &deviceId, DeviceState newState)
{
    if (m_devices.contains(deviceId)) {
        DeviceState oldState = m_devices[deviceId].state;
        m_devices[deviceId].state = newState;
        m_devices[deviceId].lastStateChange = QDateTime::currentDateTime();
        emit deviceStateChanged(deviceId, oldState, newState);
    }
}

void HotplugDebounceManager::cleanupRemovedDevices()
{
    // Remove devices that have been in Stable state for more than 5 minutes
    const int MAX_AGE_MS = 5 * 60 * 1000;
    const QDateTime now = QDateTime::currentDateTime();

    auto it = m_devices.begin();
    while (it != m_devices.end()) {
        if (it.value().state == DeviceState::Stable &&
            it.value().lastStateChange.msecsTo(now) > MAX_AGE_MS) {
            it = m_devices.erase(it);
        } else {
            ++it;
        }
    }
}

bool HotplugDebounceManager::isSameDevice(const QString &deviceId1, const QString &deviceId2) const
{
    // Simple string comparison for device ID matching
    return deviceId1 == deviceId2;
}

void HotplugDebounceManager::startDebounceTimer(const QString &deviceId)
{
    // Create or reset debounce timer
    if (m_debounceTimers.contains(deviceId)) {
        QTimer* timer = m_debounceTimers[deviceId];
        if (timer->isActive()) {
            timer->stop();
        }
    } else {
        QTimer* timer = new QTimer(this);
        timer->setSingleShot(true);
        m_debounceTimers[deviceId] = timer;

        connect(timer, &QTimer::timeout, this, [this, deviceId]() {
            onDebounceTimeout(deviceId);
        });
    }

    m_debounceTimers[deviceId]->start(DEBOUNCE_INTERVAL_MS);
}

} // namespace device
