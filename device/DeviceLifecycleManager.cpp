/*
* ========================================================================== *
*                                                                            *
*    This file is part of the Openterface Mini KVM App QT version            *
*                                                                            *
*    Copyright (C) 2024   <info@openterface.com>                             *
*                                                                            *
*    This program is free software: you can redistribute it and/or modify    *
*    it under the terms of the GNU General Public License as published by    *
*    the Free Software Foundation version 3.                                 *
*                                                                            *
*    This program is distributed in the hope that it will be useful, but     *
*    WITHOUT ANY WARRANTY; without even the implied warranty of              *
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU        *
*    General Public License for more details.                                *
*                                                                            *
*    You should have received a copy of the GNU General Public License       *
*    along with this program. If not, see <http://www.gnu.org/licenses/>.    *
*                                                                            *
* ========================================================================== *
*/

#include "DeviceLifecycleManager.h"
#include "DeviceManager.h"
#include "HotplugMonitor.h"
#include "DeviceInfo.h"
#include "host/UsbPortResetter.h"
#include "serial/SerialPortManager.h"

#include <QDateTime>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(log_lifecycle, "opf.device.lifecycle")

// ─────────────────────────────────────────────────────────────────────────────
// Construction / Singleton
// ─────────────────────────────────────────────────────────────────────────────

DeviceLifecycleManager::DeviceLifecycleManager(QObject* parent)
    : QObject(parent)
    , m_hotplugMonitor(nullptr)
{
    // Connect to HotplugMonitor's batch signal (avoids the break bug in per-device signals)
    HotplugMonitor* monitor = DeviceManager::getInstance().getHotplugMonitor();
    if (monitor) {
        m_hotplugMonitor = monitor;
        connect(m_hotplugMonitor, &HotplugMonitor::deviceChangesDetected,
                this, &DeviceLifecycleManager::onDeviceChangesDetected);
        qCInfo(log_lifecycle) << "DeviceLifecycleManager connected to HotplugMonitor";
    } else {
        qCWarning(log_lifecycle) << "HotplugMonitor not available — lifecycle manager will not receive hotplug events";
    }

    // Fast reconnect window timer: fires periodically during the 60s window after a removal
    m_fastReconnectTimer = new QTimer(this);
    m_fastReconnectTimer->setSingleShot(false);
    connect(m_fastReconnectTimer, &QTimer::timeout,
            this, &DeviceLifecycleManager::onFastReconnectTimerTimeout);

    // Stale session cleanup: runs every 30s, removes sessions disconnected for >5 minutes
    m_staleCleanupTimer = new QTimer(this);
    m_staleCleanupTimer->setSingleShot(false);
    m_staleCleanupTimer->setInterval(30000);
    connect(m_staleCleanupTimer, &QTimer::timeout,
            this, &DeviceLifecycleManager::cleanupStaleSessions);
    m_staleCleanupTimer->start();

    // USB port reset for serial recovery (Linux only)
    // When CH32V208 fails to enumerate after target restart, we can reset the parent
    // hub port to force re-enumeration. The timer fires after a delay to allow normal
    // hotplug detection to work first.
#ifdef __linux__
    m_usbPortResetter = new UsbPortResetter(this);
    connect(m_usbPortResetter, &UsbPortResetter::resetCompleted,
            this, &DeviceLifecycleManager::onUsbPortResetCompleted);

    m_usbPortResetTimer = new QTimer(this);
    m_usbPortResetTimer->setSingleShot(true);
    connect(m_usbPortResetTimer, &QTimer::timeout,
            this, &DeviceLifecycleManager::attemptUsbPortResetForSerialRecovery);

    // Connect SerialPortManager's serialRecoveryFailed signal.
    // This signal is emitted when SerialPortManager detects error code 6 on a CH32V208
    // device and either: (a) the device is present but RTS recovery failed, or
    // (b) the device disappeared from availablePorts() (sysfs stale/missing).
    // This bypasses the normal fast reconnect window requirement, since HotplugMonitor
    // may not detect the removal on Linux (sysfs entries persist as stale).
    //
    // IMPORTANT: Defer this connection via QTimer::singleShot(0, ...) to avoid
    // initializing SerialPortManager too early. Calling SerialPortManager::getInstance()
    // in the DeviceLifecycleManager constructor causes SerialPortManager to be created
    // before its dependencies (serial port, worker thread) are properly set up, which
    // blocks the application startup sequence.
    QTimer::singleShot(0, this, [this]() {
        connect(&SerialPortManager::getInstance(), &SerialPortManager::serialRecoveryFailed,
                this, &DeviceLifecycleManager::handleSerialRecoveryFailed);
        qCInfo(log_lifecycle) << "serialRecoveryFailed signal connected (deferred)";
    });
#endif

    qCInfo(log_lifecycle) << "DeviceLifecycleManager initialized";
}

DeviceLifecycleManager::~DeviceLifecycleManager()
{
    stopFastReconnectWindow();
}

DeviceLifecycleManager& DeviceLifecycleManager::getInstance()
{
    static DeviceLifecycleManager instance;
    return instance;
}

void DeviceLifecycleManager::performInitialDiscovery()
{
    qCInfo(log_lifecycle) << "Performing initial device discovery...";

    // Use DeviceManager to get currently connected devices
    QList<DeviceInfo> devices = DeviceManager::getInstance().discoverDevices();
    qCInfo(log_lifecycle) << "Found" << devices.size() << "devices during initial discovery";

    for (const auto& device : devices) {
        qCInfo(log_lifecycle) << "  Initial device:" << device.portChain
                              << "vid:" << device.vid << "pid:" << device.pid;
        onDeviceDetected(device);
    }

    if (devices.isEmpty()) {
        qCInfo(log_lifecycle) << "No devices found during initial discovery — waiting for hotplug events";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Query API
// ─────────────────────────────────────────────────────────────────────────────

const DeviceSession* DeviceLifecycleManager::getSession(const QString& sessionKey) const
{
    auto it = m_sessions.constFind(sessionKey);
    if (it != m_sessions.constEnd()) {
        return &it.value();
    }
    return nullptr;
}

DeviceSessionState DeviceLifecycleManager::getSessionState(const QString& sessionKey) const
{
    const DeviceSession* session = getSession(sessionKey);
    return session ? session->state : DeviceSessionState::Disconnected;
}

InterfaceState DeviceLifecycleManager::getInterfaceState(
    const QString& sessionKey, InterfaceType type) const
{
    const DeviceSession* session = getSession(sessionKey);
    if (!session) return InterfaceState::Absent;
    return getInterfaceInfo(*session, type).state;
}

QList<DeviceSession> DeviceLifecycleManager::getAllSessions() const
{
    return m_sessions.values();
}

bool DeviceLifecycleManager::hasActiveSession() const
{
    for (auto it = m_sessions.constBegin(); it != m_sessions.constEnd(); ++it) {
        if (it.value().state != DeviceSessionState::Disconnected) {
            return true;
        }
    }
    return false;
}

QString DeviceLifecycleManager::getActiveSessionKey() const
{
    for (auto it = m_sessions.constBegin(); it != m_sessions.constEnd(); ++it) {
        if (it.value().state != DeviceSessionState::Disconnected) {
            return it.key();
        }
    }
    return {};
}

// ─────────────────────────────────────────────────────────────────────────────
// Report API — called by subsystems after executing connect/disconnect commands
// ─────────────────────────────────────────────────────────────────────────────

void DeviceLifecycleManager::notifyInterfaceConnected(
    const QString& sessionKey, InterfaceType type)
{
    if (!m_sessions.contains(sessionKey)) {
        qCWarning(log_lifecycle) << "notifyInterfaceConnected: unknown session" << sessionKey;
        return;
    }

    auto& session = m_sessions[sessionKey];
    auto& iface = getInterfaceInfo(session, type);
    iface.retryCount = 0;
    setInterfaceState(session, type, InterfaceState::Connected);

    qCInfo(log_lifecycle) << sessionKey
                          << interfaceTypeToString(type)
                          << "interface connected";

    // Advance the state machine — try to connect next interface or transition to Ready
    tryAdvanceSession(sessionKey, type);
}

void DeviceLifecycleManager::notifyInterfaceFailed(
    const QString& sessionKey, InterfaceType type, const QString& error)
{
    if (!m_sessions.contains(sessionKey)) {
        qCWarning(log_lifecycle) << "notifyInterfaceFailed: unknown session" << sessionKey;
        return;
    }

    auto& session = m_sessions[sessionKey];
    auto& iface = getInterfaceInfo(session, type);

    qCWarning(log_lifecycle) << sessionKey
                             << interfaceTypeToString(type)
                             << "interface failed (attempt"
                             << (iface.retryCount + 1) << "):"
                             << error;

    iface.lastError = error;

    // Schedule retry if within budget
    int nextAttempt = iface.retryCount + 1;
    if (m_reconnectPolicy.shouldRetry(nextAttempt)) {
        iface.retryCount = nextAttempt;
        // Transition to Disconnected so connectNextInterface skips this interface
        // and advances to the next one. The reconnect timer will set it back to
        // Connecting when it fires.
        setInterfaceState(session, type, InterfaceState::Disconnected);
        scheduleReconnect(sessionKey, type, nextAttempt);
        // CRITICAL: Also advance to the next interface so HID/Camera/Audio can connect
        // while Serial retries in the background. Without this, the state machine would
        // block on Serial retries and never connect HID/Camera.
        tryAdvanceSession(sessionKey, type);
    } else {
        // Exhausted all retries — mark as Absent (optional) so the session can still
        // become Ready with the remaining interfaces. Using Error would block Ready
        // because readyInterfaceCount() != presentInterfaceCount() when an interface
        // is in Error state.
        setInterfaceState(session, type, InterfaceState::Absent);
        qCInfo(log_lifecycle) << sessionKey << interfaceTypeToString(type)
                              << "exhausted all retries, marking Absent (optional)";
        tryAdvanceSession(sessionKey, type);
    }
}

void DeviceLifecycleManager::notifyInterfaceDisconnected(
    const QString& sessionKey, InterfaceType type)
{
    if (!m_sessions.contains(sessionKey)) return;

    auto& session = m_sessions[sessionKey];
    setInterfaceState(session, type, InterfaceState::Disconnected);

    qCInfo(log_lifecycle) << sessionKey
                          << interfaceTypeToString(type)
                          << "interface disconnected";
}

// ─────────────────────────────────────────────────────────────────────────────
// Manual control
// ─────────────────────────────────────────────────────────────────────────────

void DeviceLifecycleManager::requestDisconnect(const QString& sessionKey)
{
    if (!m_sessions.contains(sessionKey)) return;

    auto& session = m_sessions[sessionKey];
    qCInfo(log_lifecycle) << "Manual disconnect requested for" << sessionKey;

    // Emit disconnect commands for each active interface
    emit shouldDisconnectSerial(sessionKey);
    emit shouldDisconnectHid(sessionKey);
    emit shouldDisconnectCamera(sessionKey);
    emit shouldDisconnectAudio(sessionKey);
    emit shouldReleaseHidState(sessionKey);

    // Mark all interfaces as Disconnected
    for (auto type : {InterfaceType::Serial, InterfaceType::Hid,
                      InterfaceType::Camera, InterfaceType::Audio}) {
        auto& iface = getInterfaceInfo(session, type);
        if (iface.state != InterfaceState::Absent) {
            setInterfaceState(session, type, InterfaceState::Disconnected);
            iface.retryCount = 0;
        }
    }

    session.state = DeviceSessionState::Disconnected;
    emit sessionStateChanged(sessionKey, DeviceSessionState::Disconnected);
}

void DeviceLifecycleManager::requestReconnect(const QString& sessionKey)
{
    if (!m_sessions.contains(sessionKey)) {
        qCWarning(log_lifecycle) << "requestReconnect: unknown session" << sessionKey;
        return;
    }

    auto& session = m_sessions[sessionKey];
    qCInfo(log_lifecycle) << "Manual reconnect requested for" << sessionKey;

    // Reset all interfaces to Disconnected (from Error or any other state)
    for (auto type : {InterfaceType::Serial, InterfaceType::Hid,
                      InterfaceType::Camera, InterfaceType::Audio}) {
        auto& iface = getInterfaceInfo(session, type);
        if (iface.state != InterfaceState::Absent) {
            setInterfaceState(session, type, InterfaceState::Disconnected);
            iface.retryCount = 0;
            iface.lastError.clear();
        }
    }

    session.state = DeviceSessionState::Connecting;
    emit sessionStateChanged(sessionKey, DeviceSessionState::Connecting);
    startConnectingInterfaces(sessionKey);
}

// ─────────────────────────────────────────────────────────────────────────────
// Hotplug event handling
// ─────────────────────────────────────────────────────────────────────────────

void DeviceLifecycleManager::onDeviceChangesDetected(const DeviceChangeEvent& event)
{
    // Process removals first (to handle rapid unplug→replug correctly)
    for (const auto& device : event.removedDevices) {
        onDeviceRemoved(device);
    }

    // Process additions
    for (const auto& device : event.addedDevices) {
        onDeviceDetected(device);
    }

    // Process modifications (interface paths may have changed)
    for (const auto& [oldDev, newDev] : event.modifiedDevices) {
        QString key = findSessionKeyForDevice(newDev);
        if (!key.isEmpty() && m_sessions.contains(key)) {
            auto& session = m_sessions[key];
            updateSessionFromDeviceInfo(session, newDev);
            qCInfo(log_lifecycle) << "Session" << key << "updated from device modification";
        }
    }
}

void DeviceLifecycleManager::onDeviceDetected(const DeviceInfo& device)
{
    // Find existing session or create a new one
    QString key = findSessionKeyForDevice(device);
    if (key.isEmpty()) {
        key = device.getUniqueKey();
    }

    if (!m_sessions.contains(key)) {
        // Before creating a new session, check if this device is a companion for an existing
        // incomplete session. This handles the USB 3.0 timing issue where:
        //   - Generation3Discoverer finds KVMGO composite (HID+Camera) but not CH32V208 serial
        //   - BotherDeviceDiscoverer finds CH32V208 serial but not KVMGO composite (different USB bus)
        // These would otherwise create two separate incomplete sessions. Merge them instead.
        QString companionKey = findCompanionSessionForDevice(device);
        if (!companionKey.isEmpty()) {
            qCInfo(log_lifecycle) << "Device" << device.portChain
                                  << "(vid:" << device.vid << "pid:" << device.pid
                                  << ") is a companion for existing session" << companionKey
                                  << "— merging";
            mergeCompanionDevice(companionKey, device);
            return;
        }

        // New device — create session
        auto& session = createSession(device);
        qCInfo(log_lifecycle) << "New session created:" << key
                              << "vid:" << device.vid << "pid:" << device.pid
                              << "interfaces:" << session.presentInterfaceCount();

        emit sessionAdded(key);

        // Transition to Enumerating
        session.state = DeviceSessionState::Enumerating;
        emit sessionStateChanged(key, DeviceSessionState::Enumerating);

        // Immediately try to advance (interfaces are present → start connecting)
        tryAdvanceSession(key, InterfaceType::Serial);
    } else {
        // Existing session — device reappeared (fast reconnect window)
        auto& session = m_sessions[key];
        updateSessionFromDeviceInfo(session, device);
        session.lastActivity = QDateTime::currentDateTime();

        if (session.state == DeviceSessionState::Recovering
            || session.state == DeviceSessionState::Disconnected) {
            qCInfo(log_lifecycle) << "Device reappeared in session" << key
                                  << "(state was" << sessionStateToString(session.state) << ")";

            // Reset interface states to Disconnected so they can be reconnected
            for (auto type : {InterfaceType::Serial, InterfaceType::Hid,
                              InterfaceType::Camera, InterfaceType::Audio}) {
                auto& iface = getInterfaceInfo(session, type);
                if (iface.state == InterfaceState::Disconnected
                    || iface.state == InterfaceState::Error) {
                    iface.retryCount = 0;
                    iface.lastError.clear();
                }
            }

            session.totalReconnects++;
            session.state = DeviceSessionState::Connecting;
            emit sessionStateChanged(key, DeviceSessionState::Connecting);

            stopFastReconnectWindow();
            startConnectingInterfaces(key);
        } else {
            qCInfo(log_lifecycle) << "Device" << key << "updated while in state"
                                  << sessionStateToString(session.state);
        }
    }
}

void DeviceLifecycleManager::onDeviceRemoved(const DeviceInfo& device)
{
    QString key = findSessionKeyForDevice(device);
    if (key.isEmpty()) return;
    if (!m_sessions.contains(key)) return;

    auto& session = m_sessions[key];
    qCInfo(log_lifecycle) << "Device removed from session" << key
                          << "(state was" << sessionStateToString(session.state) << ")";

    // 1. Release HID state first (stuck keys, mouse buttons)
    emit shouldReleaseHidState(key);

    // 2. Emit disconnect commands for all active interfaces
    // TARGET RESTART FIX: Always emit shouldDisconnectSerial for any non-Absent serial
    // state. Previously this only fired for Connected/Connecting, but if serial was in
    // Disconnected state (from a failed connect attempt), the port was never closed.
    // When the device reappeared, switchSerialPortByPortChain saw the port was still
    // open and skipped the reopen, leaving stale communication state.
    if (session.serial.state != InterfaceState::Absent) {
        emit shouldDisconnectSerial(key);
    }
    if (session.hid.state == InterfaceState::Connected
        || session.hid.state == InterfaceState::Connecting) {
        emit shouldDisconnectHid(key);
    }
    if (session.camera.state == InterfaceState::Connected
        || session.camera.state == InterfaceState::Connecting) {
        emit shouldDisconnectCamera(key);
    }
    if (session.audio.state == InterfaceState::Connected
        || session.audio.state == InterfaceState::Connecting) {
        emit shouldDisconnectAudio(key);
    }

    // 3. Transition to Recovering
    session.state = DeviceSessionState::Recovering;
    emit sessionStateChanged(key, DeviceSessionState::Recovering);

    // 4. Mark all present interfaces as Disconnected, reset retry counts
    for (auto type : {InterfaceType::Serial, InterfaceType::Hid,
                      InterfaceType::Camera, InterfaceType::Audio}) {
        auto& iface = getInterfaceInfo(session, type);
        if (iface.state != InterfaceState::Absent) {
            setInterfaceState(session, type, InterfaceState::Disconnected);
            iface.retryCount = 0;
            iface.lastError.clear();
        }
    }

    // 5. Start fast reconnect window — the device is expected to come back
    //    (target reboot scenario). Keep session alive for 60s.
    startFastReconnectWindow(key);
}

// ─────────────────────────────────────────────────────────────────────────────
// State machine
// ─────────────────────────────────────────────────────────────────────────────

void DeviceLifecycleManager::tryAdvanceSession(
    const QString& sessionKey, InterfaceType completedType)
{
    if (!m_sessions.contains(sessionKey)) return;
    auto& session = m_sessions[sessionKey];

    switch (session.state) {
    case DeviceSessionState::Enumerating:
        // All present interfaces are at least Disconnected → start connecting
        if (allPresentInterfacesAreAtLeastDisconnected(session)) {
            session.state = DeviceSessionState::Connecting;
            emit sessionStateChanged(sessionKey, DeviceSessionState::Connecting);
            startConnectingInterfaces(sessionKey);
        }
        break;

    case DeviceSessionState::Connecting: {
        // An interface finished (connected or failed)
        // Try to connect the next one in sequence
        int orderIndex = connectOrderIndex(completedType);
        if (orderIndex >= 0 && orderIndex < CONNECT_ORDER_COUNT - 1) {
            connectNextInterface(sessionKey, orderIndex + 1);
        }

        // Check overall result
        int ready = session.readyInterfaceCount();
        int present = session.presentInterfaceCount();
        if (ready == present && present > 0) {
            // All interfaces operational (or absent but not required)
            if (session.state != DeviceSessionState::Ready) {
                session.state = DeviceSessionState::Ready;
                emit sessionStateChanged(sessionKey, DeviceSessionState::Ready);
                qCInfo(log_lifecycle) << "Session" << sessionKey << "is Ready ("
                                      << ready << "/" << present << "interfaces)";
            }
        } else if (anyInterfaceInError(session) && allRetriesExhausted(session)) {
            // Some interfaces failed all retries
            session.state = DeviceSessionState::Degraded;
            emit sessionStateChanged(sessionKey, DeviceSessionState::Degraded);
            qCWarning(log_lifecycle) << "Session" << sessionKey << "is Degraded:"
                                     << ready << "ready," << present << "present";
        }
        break;
    }

    case DeviceSessionState::Recovering:
        // Same logic as Connecting — try to bring interfaces back
        {
            int ready = session.readyInterfaceCount();
            int present = session.presentInterfaceCount();
            if (ready == present && present > 0) {
                session.state = DeviceSessionState::Ready;
                emit sessionStateChanged(sessionKey, DeviceSessionState::Ready);
                qCInfo(log_lifecycle) << "Session" << sessionKey
                                      << "recovered → Ready";
            } else if (anyInterfaceInError(session) && allRetriesExhausted(session)) {
                session.state = DeviceSessionState::Degraded;
                emit sessionStateChanged(sessionKey, DeviceSessionState::Degraded);
            } else {
                // Still have retries — try next failed interface
                int idx = nextFailedInterfaceIndex(session);
                if (idx >= 0) {
                    connectNextInterface(sessionKey, idx);
                }
            }
        }
        break;

    default:
        break;
    }
}

void DeviceLifecycleManager::startConnectingInterfaces(const QString& sessionKey)
{
    connectNextInterface(sessionKey, 0);  // Start with Serial (index 0)
}

void DeviceLifecycleManager::connectNextInterface(const QString& sessionKey, int orderIndex)
{
    if (!m_sessions.contains(sessionKey)) return;
    auto& session = m_sessions[sessionKey];

    // Find next interface that needs connecting
    while (orderIndex < CONNECT_ORDER_COUNT) {
        InterfaceType type = CONNECT_ORDER[orderIndex];
        auto& iface = getInterfaceInfo(session, type);

        if (iface.state == InterfaceState::Absent) {
            orderIndex++;
            continue;  // Skip absent interfaces
        }

        if (iface.state == InterfaceState::Connected) {
            orderIndex++;
            continue;  // Already connected
        }

        if (iface.state == InterfaceState::Connecting) {
            // Already in progress — don't start another
            return;
        }

        // This interface needs connecting
        setInterfaceState(session, type, InterfaceState::Connecting);
        iface.retryCount = 0;

        // Use the session's port chain (or companion port chain for HID/Camera/Audio)
        // because switchTo*ByPortChain() functions expect port chain identifiers,
        // not device paths like "COM7" or "\\?\usb#...".
        QString portChain = session.portChain;
        if (type != InterfaceType::Serial && !session.companionPortChain.isEmpty()) {
            portChain = session.companionPortChain;
        }

        qCInfo(log_lifecycle) << "Starting connection for"
                              << interfaceTypeToString(type)
                              << "on session" << sessionKey
                              << "portChain:" << portChain;

        // CRITICAL: Defer the signal emission via QTimer::singleShot(0, ...) to:
        // 1. Prevent re-entrant calls (handler → notifyInterfaceConnected →
        //    tryAdvanceSession → connectNextInterface → emit signal again)
        // 2. Ensure the signal handler always runs on the main thread's event loop,
        //    even if connectNextInterface was called from a worker thread
        // 3. Avoid deadlocks if a handler (e.g. CameraManager::startCamera) needs
        //    the event loop to process events
        switch (type) {
        case InterfaceType::Serial:
            QTimer::singleShot(0, this, [this, sessionKey, portChain]() {
                emit shouldConnectSerial(sessionKey, portChain);
            });
            break;
        case InterfaceType::Hid:
            QTimer::singleShot(0, this, [this, sessionKey, portChain]() {
                emit shouldConnectHid(sessionKey, portChain);
            });
            break;
        case InterfaceType::Camera:
            QTimer::singleShot(0, this, [this, sessionKey, portChain]() {
                emit shouldConnectCamera(sessionKey, portChain);
            });
            break;
        case InterfaceType::Audio:
            QTimer::singleShot(0, this, [this, sessionKey, portChain]() {
                emit shouldConnectAudio(sessionKey, portChain);
            });
            break;
        }
        return;  // Wait for this interface to complete before starting next
    }
}

void DeviceLifecycleManager::scheduleReconnect(
    const QString& sessionKey, InterfaceType type, int attemptIndex)
{
    if (!m_sessions.contains(sessionKey)) return;
    auto& session = m_sessions[sessionKey];
    auto& iface = getInterfaceInfo(session, type);

    if (!m_reconnectPolicy.shouldRetry(attemptIndex)) {
        setInterfaceState(session, type, InterfaceState::Error);
        iface.lastError = "Max retries exhausted";
        tryAdvanceSession(sessionKey, type);
        return;
    }

    int delay = m_reconnectPolicy.getDelayMs(attemptIndex);

    qCInfo(log_lifecycle) << "Scheduling reconnect for"
                          << interfaceTypeToString(type)
                          << "on session" << sessionKey
                          << "attempt" << attemptIndex
                          << "in" << delay << "ms";

    QTimer::singleShot(delay, this, [this, sessionKey, type, attemptIndex]() {
        if (!m_sessions.contains(sessionKey)) return;

        auto& session = m_sessions[sessionKey];
        // Session was removed or fully disconnected — abandon this retry
        if (session.state == DeviceSessionState::Disconnected) return;

        auto& iface = getInterfaceInfo(session, type);
        // Interface already connected (by another path) — abandon
        if (iface.state == InterfaceState::Connected) return;

        setInterfaceState(session, type, InterfaceState::Connecting);

        // Use the session's port chain (or companion port chain for HID/Camera/Audio)
        // because switchTo*ByPortChain() functions expect port chain identifiers,
        // not device paths like "COM7" or "\\?\usb#...".
        QString portChain = session.portChain;
        if (type != InterfaceType::Serial && !session.companionPortChain.isEmpty()) {
            portChain = session.companionPortChain;
        }

        qCInfo(log_lifecycle) << "Executing reconnect for"
                              << interfaceTypeToString(type)
                              << "on session" << sessionKey
                              << "attempt" << attemptIndex
                              << "portChain:" << portChain;

        switch (type) {
        case InterfaceType::Serial:
            emit shouldConnectSerial(sessionKey, session.portChain);
            break;
        case InterfaceType::Hid:
            emit shouldConnectHid(sessionKey, portChain);
            break;
        case InterfaceType::Camera:
            emit shouldConnectCamera(sessionKey, portChain);
            break;
        case InterfaceType::Audio:
            emit shouldConnectAudio(sessionKey, portChain);
            break;
        }
    });
}

// ─────────────────────────────────────────────────────────────────────────────
// Fast reconnect window (60s after removal, session kept alive)
// ─────────────────────────────────────────────────────────────────────────────

void DeviceLifecycleManager::startFastReconnectWindow(const QString& sessionKey)
{
    m_fastReconnectSessionKey = sessionKey;
    m_fastReconnectTimer->start(300);  // Check every 300ms
    m_usbPortResetAttempted = false;   // Reset flag for this recovery window

    qCInfo(log_lifecycle) << "Fast reconnect window started for" << sessionKey
                          << "(60s, 300ms poll)";

    // Schedule USB port reset attempt after 5 seconds
    // This gives the normal hotplug detection a chance to work first.
    // If the serial device still hasn't appeared after 5s, the CH32V208 likely
    // failed to enumerate (USB error -71), and we need to reset the hub port.
#ifdef __linux__
    if (m_usbPortResetTimer) {
        m_usbPortResetTimer->start(5000);  // 5 second delay
        qCInfo(log_lifecycle) << "USB port reset scheduled in 5s if serial device doesn't appear";
    }
#endif

    // Schedule a one-shot to stop the window after 60s
    QTimer::singleShot(60000, this, [this, sessionKey]() {
        if (m_fastReconnectSessionKey == sessionKey) {
            stopFastReconnectWindow();
            // If still in Recovering after 60s, mark as Disconnected
            if (m_sessions.contains(sessionKey)) {
                auto& session = m_sessions[sessionKey];
                if (session.state == DeviceSessionState::Recovering) {
                    session.state = DeviceSessionState::Disconnected;
                    emit sessionStateChanged(sessionKey, DeviceSessionState::Disconnected);
                    qCWarning(log_lifecycle) << "Fast reconnect window expired for" << sessionKey
                                             << "→ session marked Disconnected";
                }
            }
        }
    });
}

void DeviceLifecycleManager::stopFastReconnectWindow()
{
    if (m_fastReconnectTimer->isActive()) {
        m_fastReconnectTimer->stop();
        qCInfo(log_lifecycle) << "Fast reconnect window stopped";
    }
#ifdef __linux__
    if (m_usbPortResetTimer && m_usbPortResetTimer->isActive()) {
        m_usbPortResetTimer->stop();
        qCInfo(log_lifecycle) << "USB port reset timer stopped (device recovered normally)";
    }
#endif
    m_fastReconnectSessionKey.clear();
    m_usbPortResetAttempted = false;
}

void DeviceLifecycleManager::onFastReconnectTimerTimeout()
{
    // The fast reconnect timer just ensures the event loop is spinning.
    // Actual device reappearance is detected by HotplugMonitor → onDeviceChangesDetected.
    // This timer exists to enable the 60s expiry one-shot to fire promptly.
}

// ─────────────────────────────────────────────────────────────────────────────
// USB port reset for serial recovery (Linux only)
// ─────────────────────────────────────────────────────────────────────────────

void DeviceLifecycleManager::attemptUsbPortResetForSerialRecovery()
{
#ifdef __linux__
    if (m_usbPortResetAttempted) {
        qCInfo(log_lifecycle) << "USB port reset already attempted - skipping";
        return;
    }

    // Check if we still have a session in Recovering state with serial missing
    if (m_fastReconnectSessionKey.isEmpty()) {
        qCInfo(log_lifecycle) << "No active fast reconnect window - skipping USB port reset";
        return;
    }

    if (!m_sessions.contains(m_fastReconnectSessionKey)) {
        qCInfo(log_lifecycle) << "Session no longer exists - skipping USB port reset";
        return;
    }

    const auto& session = m_sessions[m_fastReconnectSessionKey];

    // Only attempt reset if session is still in Recovering state
    if (session.state != DeviceSessionState::Recovering) {
        qCInfo(log_lifecycle) << "Session state is" << sessionStateToString(session.state)
                              << "- skipping USB port reset";
        return;
    }

    // Check if serial interface is missing (Absent or still Disconnected)
    bool serialMissing = (session.serial.state == InterfaceState::Absent
                          || session.serial.state == InterfaceState::Disconnected);

    if (!serialMissing) {
        qCInfo(log_lifecycle) << "Serial interface is present - skipping USB port reset";
        return;
    }

    // Check if composite device (HID/Camera) is still present - this indicates
    // the physical device is connected but the serial chip failed to enumerate
    bool compositePresent = (session.hid.state != InterfaceState::Absent
                             || session.camera.state != InterfaceState::Absent);

    if (!compositePresent) {
        qCInfo(log_lifecycle) << "No composite device present - skipping USB port reset"
                              << "(device fully disconnected, not enumeration failure)";
        return;
    }

    // All conditions met: serial missing but composite present → attempt port reset
    m_usbPortResetAttempted = true;
    qCInfo(log_lifecycle) << "Serial device missing but composite present - attempting USB hub port reset";

    if (m_usbPortResetter) {
        m_usbPortResetter->resetHubPortsForSerialRecovery();
    }
#else
    Q_UNUSED(m_usbPortResetAttempted);
#endif
}

void DeviceLifecycleManager::onUsbPortResetCompleted(bool success)
{
#ifdef __linux__
    if (success) {
        qCInfo(log_lifecycle) << "USB hub port reset completed - waiting for serial device to re-enumerate";
        // The HotplugMonitor will detect the serial device when it re-enumerates
        // and trigger the normal recovery flow via onDeviceDetected()
    } else {
        qCWarning(log_lifecycle) << "USB hub port reset failed - serial device may remain unavailable";
    }
#else
    Q_UNUSED(success);
#endif
}

void DeviceLifecycleManager::handleSerialRecoveryFailed()
{
#ifdef __linux__
    qCWarning(log_lifecycle) << "Serial recovery failed signal received —"
                              << "SerialPortManager reports CH32V208 is not truly present on USB bus";

    if (m_usbPortResetAttempted) {
        qCInfo(log_lifecycle) << "USB port reset already attempted in this recovery window — skipping";
        return;
    }

    // Direct trigger: bypass the fast reconnect window requirement.
    // This path is reached when SerialPortManager detects error code 6 but
    // HotplugMonitor hasn't detected device removal (sysfs stale entries on Linux).
    // The UsbPortResetter will check if the composite device (345F:2132) is still
    // present on the USB bus before attempting hub port reset.
    m_usbPortResetAttempted = true;
    qCInfo(log_lifecycle) << "Direct USB port reset trigger — composite device present,"
                          << "serial missing (detected by SerialPortManager error path)";

    if (m_usbPortResetter) {
        m_usbPortResetter->resetHubPortsForSerialRecovery();
    } else {
        qCWarning(log_lifecycle) << "UsbPortResetter not available";
    }
#else
    Q_UNUSED(m_usbPortResetAttempted);
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// Session management
// ─────────────────────────────────────────────────────────────────────────────

DeviceSession& DeviceLifecycleManager::createSession(const DeviceInfo& device)
{
    QString key = device.getUniqueKey();
    DeviceSession session;
    session.sessionKey = key;
    session.firstSeen = QDateTime::currentDateTime();
    session.lastActivity = session.firstSeen;
    updateSessionFromDeviceInfo(session, device);
    m_sessions[key] = session;
    return m_sessions[key];
}

void DeviceLifecycleManager::removeSession(const QString& sessionKey)
{
    m_sessions.remove(sessionKey);
    emit sessionRemoved(sessionKey);
    qCInfo(log_lifecycle) << "Session removed:" << sessionKey;
}

void DeviceLifecycleManager::updateSessionFromDeviceInfo(
    DeviceSession& session, const DeviceInfo& device)
{
    session.portChain = device.portChain;
    session.companionPortChain = device.companionPortChain;
    session.vid = device.vid;
    session.pid = device.pid;

    // Update interface paths and mark present interfaces as Disconnected (if not already Connected/Connecting)
    auto updateIface = [&](InterfaceType type, const QString& path) {
        auto& iface = getInterfaceInfo(session, type);
        if (!path.isEmpty()) {
            iface.path = path;
            if (iface.state == InterfaceState::Absent) {
                iface.state = InterfaceState::Disconnected;
                iface.lastStateChange = QDateTime::currentDateTime();
            }
        } else {
            // Interface not present on this device
            if (iface.state == InterfaceState::Absent) return;  // Already absent
            // If it was present before but path is now gone, mark absent
            iface.state = InterfaceState::Absent;
            iface.path.clear();
            iface.lastStateChange = QDateTime::currentDateTime();
        }
    };

    updateIface(InterfaceType::Serial,  device.serialPortPath);
    updateIface(InterfaceType::Hid,     device.hidDevicePath);
    updateIface(InterfaceType::Camera,  device.cameraDevicePath);
    updateIface(InterfaceType::Audio,   device.audioDevicePath);
}

void DeviceLifecycleManager::cleanupStaleSessions()
{
    QDateTime now = QDateTime::currentDateTime();
    QStringList staleKeys;

    for (auto it = m_sessions.constBegin(); it != m_sessions.constEnd(); ++it) {
        const auto& session = it.value();
        // Remove sessions that have been disconnected for more than 5 minutes
        if (session.state == DeviceSessionState::Disconnected
            && session.lastActivity.isValid()
            && session.lastActivity.msecsTo(now) > 300000) {
            staleKeys.append(it.key());
        }
    }

    for (const auto& key : staleKeys) {
        removeSession(key);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Interface helpers
// ─────────────────────────────────────────────────────────────────────────────

InterfaceInfo& DeviceLifecycleManager::getInterfaceInfo(
    DeviceSession& session, InterfaceType type)
{
    switch (type) {
    case InterfaceType::Serial:  return session.serial;
    case InterfaceType::Hid:     return session.hid;
    case InterfaceType::Camera:  return session.camera;
    case InterfaceType::Audio:   return session.audio;
    }
    Q_UNREACHABLE();
    return session.serial;
}

const InterfaceInfo& DeviceLifecycleManager::getInterfaceInfo(
    const DeviceSession& session, InterfaceType type) const
{
    switch (type) {
    case InterfaceType::Serial:  return session.serial;
    case InterfaceType::Hid:     return session.hid;
    case InterfaceType::Camera:  return session.camera;
    case InterfaceType::Audio:   return session.audio;
    }
    Q_UNREACHABLE();
    return session.serial;
}

int DeviceLifecycleManager::connectOrderIndex(InterfaceType type) const
{
    for (int i = 0; i < CONNECT_ORDER_COUNT; i++) {
        if (CONNECT_ORDER[i] == type) return i;
    }
    return -1;
}

bool DeviceLifecycleManager::allPresentInterfacesAreAtLeastDisconnected(
    const DeviceSession& session) const
{
    auto atLeastDisconnected = [](const InterfaceInfo& iface) {
        return iface.state == InterfaceState::Absent
            || iface.state == InterfaceState::Disconnected;
    };
    return atLeastDisconnected(session.serial)
        && atLeastDisconnected(session.hid)
        && atLeastDisconnected(session.camera)
        && atLeastDisconnected(session.audio);
}

bool DeviceLifecycleManager::anyInterfaceInError(const DeviceSession& session) const
{
    return session.serial.state == InterfaceState::Error
        || session.hid.state == InterfaceState::Error
        || session.camera.state == InterfaceState::Error
        || session.audio.state == InterfaceState::Error;
}

bool DeviceLifecycleManager::allRetriesExhausted(const DeviceSession& session) const
{
    return session.allRetriesExhausted();
}

int DeviceLifecycleManager::nextFailedInterfaceIndex(const DeviceSession& session) const
{
    for (int i = 0; i < CONNECT_ORDER_COUNT; i++) {
        InterfaceType type = CONNECT_ORDER[i];
        const auto& iface = getInterfaceInfo(session, type);
        if (iface.state == InterfaceState::Disconnected
            || (iface.state == InterfaceState::Error
                && iface.retryCount < iface.maxRetries)) {
            return i;
        }
    }
    return -1;
}

void DeviceLifecycleManager::setInterfaceState(
    DeviceSession& session, InterfaceType type, InterfaceState newState)
{
    auto& iface = getInterfaceInfo(session, type);
    InterfaceState oldState = iface.state;
    if (oldState == newState) return;

    iface.state = newState;
    iface.lastStateChange = QDateTime::currentDateTime();

    emit interfaceStateChanged(session.sessionKey, type, newState);

    qCDebug(log_lifecycle) << session.sessionKey
                           << interfaceTypeToString(type)
                           << interfaceStateToString(oldState)
                           << "→" << interfaceStateToString(newState);
}

QString DeviceLifecycleManager::findSessionKeyForDevice(const DeviceInfo& device) const
{
    // First: try the device's own unique key (portChain)
    if (m_sessions.contains(device.getUniqueKey())) {
        return device.getUniqueKey();
    }

    // Second: try companionPortChain (USB 3.0 scenario: camera/HID on different bus)
    if (device.hasCompanionPortChain()) {
        for (auto it = m_sessions.constBegin(); it != m_sessions.constEnd(); ++it) {
            if (it.value().companionPortChain == device.companionPortChain) {
                return it.key();
            }
        }
    }

    // Third: try matching by VID:PID (fallback for devices whose port chain changed)
    if (!device.vid.isEmpty() && !device.pid.isEmpty()) {
        for (auto it = m_sessions.constBegin(); it != m_sessions.constEnd(); ++it) {
            const auto& session = it.value();
            if (session.vid == device.vid && session.pid == device.pid
                && session.state == DeviceSessionState::Recovering) {
                return it.key();
            }
        }
    }

    return {};
}

// ─────────────────────────────────────────────────────────────────────────────
// Companion merge — USB 3.0 timing issue mitigation
// ─────────────────────────────────────────────────────────────────────────────
//
// On USB 3.0 systems the KVMGO composite device (345F:2132 → HID, Camera, Audio)
// and the CH32V208 serial chip (1A86:FE0C) enumerate on different USB ports and
// sometimes at different rates. The two discoverers may independently find only
// one half of the device pair:
//
//   Generation3Discoverer: finds 345F:2132 composite → session with HID+Camera, Serial=Absent
//   BotherDeviceDiscoverer: finds 1A86:FE0C serial   → session with Serial only, HID/Camera=Absent
//
// Without a merge step these become two separate half-sessions that can never
// become Ready. findCompanionSessionForDevice detects this situation and
// mergeCompanionDevice stitches the missing interfaces into the existing session
// so a single complete session is produced.

bool DeviceLifecycleManager::isSerialOnlyDevice(const DeviceInfo& device) const
{
    return !device.serialPortPath.isEmpty()
           && device.hidDevicePath.isEmpty()
           && device.cameraDevicePath.isEmpty();
}

bool DeviceLifecycleManager::isHidCameraOnlyDevice(const DeviceInfo& device) const
{
    return device.serialPortPath.isEmpty()
           && (!device.hidDevicePath.isEmpty() || !device.cameraDevicePath.isEmpty());
}

QString DeviceLifecycleManager::findCompanionSessionForDevice(const DeviceInfo& device) const
{
    bool newIsSerialOnly  = isSerialOnlyDevice(device);
    bool newIsHidCamOnly  = isHidCameraOnlyDevice(device);
    if (!newIsSerialOnly && !newIsHidCamOnly) return {};

    for (auto it = m_sessions.constBegin(); it != m_sessions.constEnd(); ++it) {
        const DeviceSession& session = it.value();

        // Only merge into sessions that are still assembling (not already Ready / Disconnected).
        if (session.state != DeviceSessionState::Enumerating
            && session.state != DeviceSessionState::Connecting
            && session.state != DeviceSessionState::Ready
            && session.state != DeviceSessionState::Degraded) {
            continue;
        }

        bool sessionMissingSerial = (session.serial.state == InterfaceState::Absent);
        bool sessionHasHidOrCam   = (session.hid.state  != InterfaceState::Absent
                                  || session.camera.state != InterfaceState::Absent);

        if (newIsSerialOnly && sessionMissingSerial && sessionHasHidOrCam) {
            qCInfo(log_lifecycle) << "Companion match: new serial-only device fits session"
                                  << it.key() << "which is missing Serial";
            return it.key();
        }

        bool sessionMissingHidAndCam = (session.hid.state    == InterfaceState::Absent
                                     && session.camera.state == InterfaceState::Absent);
        bool sessionHasSerial        = (session.serial.state != InterfaceState::Absent);

        if (newIsHidCamOnly && sessionMissingHidAndCam && sessionHasSerial) {
            qCInfo(log_lifecycle) << "Companion match: new HID/Camera device fits session"
                                  << it.key() << "which is missing HID+Camera";
            return it.key();
        }
    }
    return {};
}

void DeviceLifecycleManager::mergeCompanionDevice(
    const QString& sessionKey, const DeviceInfo& device)
{
    if (!m_sessions.contains(sessionKey)) return;
    DeviceSession& session = m_sessions[sessionKey];

    bool deviceIsSerialOnly = isSerialOnlyDevice(device);

    // Route port chains so connectNextInterface can address each interface correctly.
    // The existing session key is NOT changed — it stays whatever it was (typically
    // the composite device's port chain from Generation3Discoverer).
    if (deviceIsSerialOnly) {
        // New device provides the serial side.
        // If the session currently has portChain pointing at the composite device
        // (because Generation3Discoverer couldn't find serial), move the composite
        // chain to companionPortChain and put the serial chain in portChain.
        if (!session.serial.path.isEmpty()) {
            // Session already had a (wrong) serial path — discard it.
        }
        if (session.companionPortChain.isEmpty() && !session.portChain.isEmpty()) {
            // Preserve composite chain as companion so HID/Camera still work.
            session.companionPortChain = session.portChain;
        }
        session.portChain        = device.portChain;
        session.serial.path      = device.serialPortPath;
        session.serial.retryCount = 0;
        if (session.serial.state == InterfaceState::Absent) {
            setInterfaceState(session, InterfaceType::Serial, InterfaceState::Disconnected);
        }
    } else {
        // New device provides HID / Camera (and possibly Audio).
        // Store its chain as the companionPortChain for interface routing.
        if (session.companionPortChain.isEmpty()) {
            session.companionPortChain = device.portChain;
        }
        if (!device.hidDevicePath.isEmpty()) {
            session.hid.path = device.hidDevicePath;
            session.hid.retryCount = 0;
            if (session.hid.state == InterfaceState::Absent) {
                setInterfaceState(session, InterfaceType::Hid, InterfaceState::Disconnected);
            }
        }
        if (!device.cameraDevicePath.isEmpty()) {
            session.camera.path = device.cameraDevicePath;
            session.camera.retryCount = 0;
            if (session.camera.state == InterfaceState::Absent) {
                setInterfaceState(session, InterfaceType::Camera, InterfaceState::Disconnected);
            }
        }
        if (!device.audioDevicePath.isEmpty()) {
            session.audio.path = device.audioDevicePath;
            session.audio.retryCount = 0;
            if (session.audio.state == InterfaceState::Absent) {
                setInterfaceState(session, InterfaceType::Audio, InterfaceState::Disconnected);
            }
        }
    }

    session.lastActivity = QDateTime::currentDateTime();

    qCInfo(log_lifecycle) << "Companion merge complete for session" << sessionKey
                          << "— now has" << session.presentInterfaceCount() << "present interfaces"
                          << "portChain:" << session.portChain
                          << "companionPortChain:" << session.companionPortChain;

    // Kick the state machine so newly-present interfaces start connecting.
    if (session.state == DeviceSessionState::Enumerating
        || session.state == DeviceSessionState::Connecting
        || session.state == DeviceSessionState::Degraded) {
        // Reset to Enumerating so all present interfaces are (re)evaluated.
        session.state = DeviceSessionState::Enumerating;
        emit sessionStateChanged(sessionKey, DeviceSessionState::Enumerating);
        tryAdvanceSession(sessionKey, InterfaceType::Serial);
    }
}
