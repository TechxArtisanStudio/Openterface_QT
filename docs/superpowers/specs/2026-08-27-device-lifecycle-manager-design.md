# DeviceLifecycleManager & Hotplug Test Framework Design

**Date:** 2026-08-27
**Status:** Draft
**Author:** Claude + User

## 1. Problem Statement

When the target computer reboots during use, the KVMGO device (VID 345F, PID 2132, CH32V208 chip) fully disconnects its USB connection. The Host-side OpenterfaceQT app fails to recover — it freezes or becomes unusable. Manually unplugging and replugging the USB on the Host side fixes it.

Root cause analysis reveals a systemic architecture problem: three subsystems (SerialPortManager, CameraManager, VideoHid) each independently connect to HotplugMonitor signals and maintain their own device state. They don't coordinate with each other, leading to:

| Bug | Location | Impact |
|-----|----------|--------|
| `break` after first added device in HotplugMonitor | `HotplugMonitor.cpp` | Only one interface (serial OR HID OR camera) receives `newDevicePluggedIn` per poll cycle |
| Only 2 serial auto-connect attempts | `SerialHotplugHandler.cpp` | If USB enumeration is slow (>1s), both attempts fail with no further retries |
| `ERROR_STATE` blocks `openPort()` when port not yet present | `SerialPortManager.cpp` | If port hasn't re-enumerated, open is rejected with no retry |
| Camera `hasActiveCameraDevice()` blocks auto-switch | `cameramanager.cpp` | If deactivation fails, camera never reconnects |
| Camera retry window too short (11.5s) | `cameramanager.h` | May not cover slow target boot + USB enumeration |
| `deviceRapidlyReconnected` emits empty `DeviceInfo()` | `DeviceManager.cpp` | Downstream handlers can't identify which device reconnected |
| `switchSerialPortByPortChain()` early-returns when path matches but port is closed | `SerialPortManager.cpp` | After USB reconnect, auto-connect sees matching path and returns without reopening |
| FFmpeg backend bypasses CameraManager hotplug | `cameramanager.cpp` | Camera lifecycle not centrally managed |

## 2. Design Goals

1. **Single source of truth** for device state — one manager owns the lifecycle
2. **Sequenced reconnection** — Serial → HID → Camera → Audio, with proper ordering
3. **Robust retry** — exponential backoff with sufficient time window for target reboots
4. **No stuck states** — every error state has a clear recovery path
5. **FFmpeg integration** — all camera backends go through the same lifecycle flow
6. **Testable** — a guided test wizard validates hotplug recovery end-to-end

## 3. Architecture Overview

### 3.1 Current Architecture (Problematic)

```
HotplugMonitor ──emit──→ SerialPortManager  (owns: m_portState, m_deviceUnpluggedDetected, ...)
                   ├──→ CameraManager      (owns: m_currentCameraDevice, hasActiveCameraDevice(), ...)
                   └──→ VideoHid           (owns: m_currentHIDDevicePath, isInTransaction(), ...)
```

Three subsystems independently react to the same signals, each maintaining their own state. No coordination.

### 3.2 New Architecture

```
HotplugMonitor
      │
      ▼
DeviceLifecycleManager  ←── Single source of truth
      │
      ├──→ SerialPortManager    (executes connect/disconnect commands only)
      ├──→ VideoHid             (executes connect/disconnect commands only)
      ├──→ CameraManager        (executes connect/disconnect commands only)
      │      ├── FFmpegBackendHandler
      │      └── GStreamerBackendHandler
      └──→ KeyboardManager / MouseManager  (release keys/buttons on disconnect)

HotplugTestWizard  ←── Reads state from DeviceLifecycleManager, verifies via subsystem APIs
```

**Key principle:** Subsystems no longer decide *when* to reconnect. They only execute instructions from DeviceLifecycleManager and report results back.

## 4. Core Data Structures

### 4.1 InterfaceType Enum

```cpp
enum class InterfaceType : uint8_t {
    Serial,
    Hid,
    Camera,
    Audio
};
```

### 4.2 InterfaceState Enum

```cpp
enum class InterfaceState : uint8_t {
    Absent,          // Interface not present on this device
    Disconnected,    // Interface present but not connected
    Connecting,      // Connection in progress
    Connected,       // Connected and operational
    Error            // Connection failed (retries exhausted)
};
```

### 4.3 DeviceSessionState Enum

```cpp
enum class DeviceSessionState : uint8_t {
    Disconnected,    // Device not present
    Enumerating,     // Device detected, waiting for interfaces to appear
    Connecting,      // Interfaces being opened in sequence
    Ready,           // All present interfaces operational
    Degraded,        // Some interfaces operational, others failed
    Recovering       // Device was disconnected, attempting recovery
};
```

State machine:
```
                 ┌──────────────────────────────────────┐
                 │                                      │
Disconnected ──→ Enumerating ──→ Connecting ──→ Ready ──┤
                                     │            │     │
                                     │            ▼     │
                                     │       Degraded   │
                                     │            │     │
                                     └────────────┼─────┘
                                                  │
                         (any interface disconnects)
                                                  │
                                                  ▼
                                            Recovering
                                                  │
                          (all interfaces back)   │ (retries exhausted)
                              ┌───────────────────┤
                              │                   │
                              ▼                   ▼
                           Ready/Connecting    Degraded
```

### 4.4 InterfaceInfo Struct

```cpp
struct InterfaceInfo {
    InterfaceState state = InterfaceState::Absent;
    QString path;                    // e.g. "COM3", "\\.\hid#...", camera device id
    int retryCount = 0;
    int maxRetries = 7;
    QDateTime lastStateChange;
    QString lastError;
};
```

### 4.5 DeviceSession Struct

The central data structure — represents one physical KVM device and its complete lifecycle.

```cpp
struct DeviceSession {
    // Identity
    QString sessionKey;              // = portChain (or composite key for USB 3.0)
    QString portChain;               // Serial port chain
    QString companionPortChain;      // USB 3.0 companion (camera/HID may be on different chain)
    QString vid;
    QString pid;

    // Overall state
    DeviceSessionState state = DeviceSessionState::Disconnected;

    // Four interfaces
    InterfaceInfo serial;
    InterfaceInfo hid;
    InterfaceInfo camera;
    InterfaceInfo audio;

    // Lifecycle tracking
    QDateTime firstSeen;
    QDateTime lastActivity;
    int totalReconnects = 0;

    // Computed helpers
    int presentInterfaceCount() const {
        int count = 0;
        if (serial.state != InterfaceState::Absent) count++;
        if (hid.state != InterfaceState::Absent) count++;
        if (camera.state != InterfaceState::Absent) count++;
        if (audio.state != InterfaceState::Absent) count++;
        return count;
    }

    int readyInterfaceCount() const {
        int count = 0;
        if (serial.state == InterfaceState::Connected) count++;
        if (hid.state == InterfaceState::Connected) count++;
        if (camera.state == InterfaceState::Connected
            || camera.state == InterfaceState::Absent) count++;
        if (audio.state == InterfaceState::Connected
            || audio.state == InterfaceState::Absent) count++;
        return count;
    }

    bool isFullyReady() const {
        return state == DeviceSessionState::Ready;
    }
};
```

### 4.6 ReconnectPolicy

```cpp
struct ReconnectPolicy {
    // Exponential backoff: 0.5s, 1s, 2s, 3s, 5s, 5s, 10s
    // Total window: ~26.5s — covers most target reboot scenarios
    QList<int> intervals = {500, 1000, 2000, 3000, 5000, 5000, 10000};
    int maxAttempts = 7;
};
```

## 5. DeviceLifecycleManager Class

### 5.1 Class Declaration

```cpp
class DeviceLifecycleManager : public QObject {
    Q_OBJECT

public:
    static DeviceLifecycleManager& getInstance();

    // ── Query API (subsystems read state) ──
    const DeviceSession* getSession(const QString& sessionKey) const;
    DeviceSessionState getSessionState(const QString& sessionKey) const;
    InterfaceState getInterfaceState(const QString& sessionKey, InterfaceType type) const;
    QList<DeviceSession> getAllSessions() const;
    bool hasActiveSession() const;

    // ── Report API (subsystems report results) ──
    void notifyInterfaceConnected(const QString& sessionKey, InterfaceType type);
    void notifyInterfaceFailed(const QString& sessionKey, InterfaceType type,
                                const QString& error);
    void notifyInterfaceDisconnected(const QString& sessionKey, InterfaceType type);

    // ── Manual control (user actions) ──
    void requestDisconnect(const QString& sessionKey);
    void requestReconnect(const QString& sessionKey);

signals:
    // ── Command signals (subsystems listen and execute) ──
    void shouldConnectSerial(const QString& sessionKey, const QString& portPath);
    void shouldConnectHid(const QString& sessionKey, const QString& hidPath);
    void shouldConnectCamera(const QString& sessionKey, const QString& cameraDeviceId);
    void shouldConnectAudio(const QString& sessionKey, const QString& audioDeviceId);

    void shouldDisconnectSerial(const QString& sessionKey);
    void shouldDisconnectHid(const QString& sessionKey);
    void shouldDisconnectCamera(const QString& sessionKey);
    void shouldDisconnectAudio(const QString& sessionKey);

    void shouldReleaseHidState(const QString& sessionKey);  // Release stuck keys/buttons

    // ── State change signals (UI and test framework listen) ──
    void sessionStateChanged(const QString& sessionKey, DeviceSessionState newState);
    void sessionAdded(const QString& sessionKey);
    void sessionRemoved(const QString& sessionKey);
    void interfaceStateChanged(const QString& sessionKey, InterfaceType type,
                                InterfaceState newState);

private:
    explicit DeviceLifecycleManager(QObject* parent = nullptr);

    QMap<QString, DeviceSession> m_sessions;
    HotplugMonitor* m_hotplugMonitor;
    ReconnectPolicy m_reconnectPolicy;

    // Connection sequence order
    static constexpr InterfaceType CONNECT_ORDER[] = {
        InterfaceType::Serial,
        InterfaceType::Hid,
        InterfaceType::Camera,
        InterfaceType::Audio
    };

    // ── Internal logic ──
    void onDeviceDetected(const DeviceInfo& device);
    void onDeviceRemoved(const DeviceInfo& device);
    void tryAdvanceSession(const QString& sessionKey, InterfaceType completedType);
    void startConnectingInterfaces(const QString& sessionKey);
    void connectNextInterface(const QString& sessionKey, int orderIndex);
    void scheduleReconnect(const QString& sessionKey, InterfaceType type, int attemptIndex);
    void handleFullDisconnect(const QString& sessionKey);
    void startFastReconnectWindow(const QString& sessionKey);
    void cleanupStaleSessions();

    InterfaceInfo& getInterfaceInfo(DeviceSession& session, InterfaceType type);
    const InterfaceInfo& getInterfaceInfo(const DeviceSession& session, InterfaceType type) const;
};
```

### 5.2 Core Logic: `tryAdvanceSession()`

```cpp
void DeviceLifecycleManager::tryAdvanceSession(
    const QString& sessionKey, InterfaceType completedType)
{
    auto& session = m_sessions[sessionKey];

    switch (session.state) {
    case DeviceSessionState::Enumerating:
        // Wait until all present interfaces are at least Disconnected
        if (allPresentInterfacesAreAtLeastDisconnected(session)) {
            session.state = DeviceSessionState::Connecting;
            emit sessionStateChanged(sessionKey, DeviceSessionState::Connecting);
            startConnectingInterfaces(sessionKey);
        }
        break;

    case DeviceSessionState::Connecting:
        // An interface finished (connected or failed)
        // Try to connect the next one in sequence
        {
            int orderIndex = connectOrderIndex(completedType);
            if (orderIndex < 3) {  // Not the last interface
                connectNextInterface(sessionKey, orderIndex + 1);
            }
        }
        // Check if all interfaces are done
        if (session.readyInterfaceCount() == session.presentInterfaceCount()) {
            session.state = DeviceSessionState::Ready;
            emit sessionStateChanged(sessionKey, DeviceSessionState::Ready);
        } else if (anyInterfaceInError(session) && allRetriesExhausted(session)) {
            session.state = DeviceSessionState::Degraded;
            emit sessionStateChanged(sessionKey, DeviceSessionState::Degraded);
        }
        break;

    case DeviceSessionState::Recovering:
        // Same logic as Connecting — try to bring interfaces back
        if (session.readyInterfaceCount() == session.presentInterfaceCount()) {
            session.state = DeviceSessionState::Ready;
            emit sessionStateChanged(sessionKey, DeviceSessionState::Ready);
        } else {
            // Continue trying
            connectNextInterface(sessionKey, nextFailedInterfaceIndex(session));
        }
        break;

    default:
        break;
    }
}
```

### 5.3 Connection Sequence

```cpp
void DeviceLifecycleManager::startConnectingInterfaces(const QString& sessionKey) {
    connectNextInterface(sessionKey, 0);  // Start with Serial (index 0)
}

void DeviceLifecycleManager::connectNextInterface(const QString& sessionKey, int orderIndex) {
    auto& session = m_sessions[sessionKey];

    // Find next interface that needs connecting
    while (orderIndex < 4) {
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

        // This interface needs connecting
        iface.state = InterfaceState::Connecting;
        emit interfaceStateChanged(sessionKey, type, InterfaceState::Connecting);

        switch (type) {
        case InterfaceType::Serial:
            emit shouldConnectSerial(sessionKey, iface.path);
            break;
        case InterfaceType::Hid:
            emit shouldConnectHid(sessionKey, iface.path);
            break;
        case InterfaceType::Camera:
            emit shouldConnectCamera(sessionKey, iface.path);
            break;
        case InterfaceType::Audio:
            emit shouldConnectAudio(sessionKey, iface.path);
            break;
        }
        return;  // Wait for this interface to complete before starting next
    }
}
```

### 5.4 Disconnect Handling

```cpp
void DeviceLifecycleManager::onDeviceRemoved(const DeviceInfo& device) {
    QString key = device.getUniqueKey();

    if (!m_sessions.contains(key)) return;

    auto& session = m_sessions[key];

    // 1. Release HID state first (stuck keys, mouse buttons)
    emit shouldReleaseHidState(key);

    // 2. Transition to Recovering
    session.state = DeviceSessionState::Recovering;
    emit sessionStateChanged(key, DeviceSessionState::Recovering);

    // 3. Mark all interfaces as Disconnected
    for (auto type : {InterfaceType::Serial, InterfaceType::Hid,
                      InterfaceType::Camera, InterfaceType::Audio}) {
        auto& iface = getInterfaceInfo(session, type);
        if (iface.state != InterfaceState::Absent) {
            iface.state = InterfaceState::Disconnected;
            iface.retryCount = 0;
            emit interfaceStateChanged(key, type, InterfaceState::Disconnected);
        }
    }

    // 4. Don't delete session — keep for fast reconnect
    // 5. Start fast reconnect window (60s, 300ms poll)
    startFastReconnectWindow(key);
}
```

### 5.5 Reconnect Strategy

```cpp
void DeviceLifecycleManager::scheduleReconnect(
    const QString& sessionKey, InterfaceType type, int attemptIndex)
{
    auto& session = m_sessions[sessionKey];
    auto& iface = getInterfaceInfo(session, type);

    if (attemptIndex >= m_reconnectPolicy.maxAttempts) {
        iface.state = InterfaceState::Error;
        iface.lastError = "Max retries exhausted";
        emit interfaceStateChanged(sessionKey, type, InterfaceState::Error);
        tryAdvanceSession(sessionKey, type);
        return;
    }

    int delay = m_reconnectPolicy.intervals.value(attemptIndex, 10000);
    QTimer::singleShot(delay, this, [this, sessionKey, type, attemptIndex]() {
        if (!m_sessions.contains(sessionKey)) return;

        auto& session = m_sessions[sessionKey];
        if (session.state == DeviceSessionState::Disconnected) return;  // Session removed

        auto& iface = getInterfaceInfo(session, type);
        iface.state = InterfaceState::Connecting;
        iface.retryCount = attemptIndex + 1;
        emit interfaceStateChanged(sessionKey, type, InterfaceState::Connecting);

        switch (type) {
        case InterfaceType::Serial:
            emit shouldConnectSerial(sessionKey, iface.path);
            break;
        case InterfaceType::Hid:
            emit shouldConnectHid(sessionKey, iface.path);
            break;
        case InterfaceType::Camera:
            emit shouldConnectCamera(sessionKey, iface.path);
            break;
        case InterfaceType::Audio:
            emit shouldConnectAudio(sessionKey, iface.path);
            break;
        }
    });
}
```

### 5.6 HotplugMonitor Bug Fix

The `break` in `HotplugMonitor::checkForChangesSlot()` that skips all but the first added device must be removed:

```cpp
// In HotplugMonitor.cpp, checkForChangesSlot():
// BEFORE (buggy):
for (const auto& device : event.addedDevices) {
    emit newDevicePluggedIn(device);
    break;  // ← BUG: only first device
}

// AFTER (fixed):
for (const auto& device : event.addedDevices) {
    emit newDevicePluggedIn(device);
    // No break — all added devices get their signal
}
```

However, with DeviceLifecycleManager as the single consumer, this is less critical — the manager receives `deviceChangesDetected(DeviceChangeEvent)` which contains the full list. The per-device signals can be removed or kept for backward compatibility.

**Recommended approach:** DeviceLifecycleManager connects to `deviceChangesDetected` (the batch signal) instead of individual `newDevicePluggedIn`/`deviceUnplugged`. This avoids the `break` bug entirely.

## 6. Subsystem Migration

### 6.1 SerialPortManager Migration

**Remove:**
- Connection to `HotplugMonitor::newDevicePluggedIn` / `deviceUnplugged`
- `SerialHotplugHandler` (its responsibilities move to DeviceLifecycleManager)
- Internal debounce logic (`m_debounceActive`, `m_lastUnplugTime`)
- Internal retry logic (moved to DeviceLifecycleManager's ReconnectPolicy)

**Add:**
```cpp
// In constructor, connect to DeviceLifecycleManager:
connect(&DeviceLifecycleManager::getInstance(),
    &DeviceLifecycleManager::shouldConnectSerial,
    this, [this](const QString& sessionKey, const QString& portPath) {
        bool success = switchSerialPortByPortChain(portPath);
        if (success) {
            DeviceLifecycleManager::getInstance().notifyInterfaceConnected(
                sessionKey, InterfaceType::Serial);
        } else {
            DeviceLifecycleManager::getInstance().notifyInterfaceFailed(
                sessionKey, InterfaceType::Serial, "port open failed");
        }
    });

connect(&DeviceLifecycleManager::getInstance(),
    &DeviceLifecycleManager::shouldDisconnectSerial,
    this, [this](const QString& sessionKey) {
        closePort();
        m_currentSerialPortPath.clear();
        m_currentSerialPortChain.clear();
        DeviceLifecycleManager::getInstance().notifyInterfaceDisconnected(
            sessionKey, InterfaceType::Serial);
    });
```

**Keep:**
- `m_portState` state machine (internal to serial port operations)
- `switchSerialPortByPortChain()` — but remove the buggy early-return when path matches but port is closed (the fix already applied in previous session)
- `openPort()` — but simplify ERROR_STATE recovery (DeviceLifecycleManager handles retry timing)

### 6.2 VideoHid Migration

**Remove:**
- Direct connection to HotplugMonitor
- `isInTransaction()` as a gate for auto-switch

**Add:**
```cpp
connect(&DeviceLifecycleManager::getInstance(),
    &DeviceLifecycleManager::shouldConnectHid,
    this, [this](const QString& sessionKey, const QString& hidPath) {
        bool success = switchToHIDDeviceByPortChain(hidPath);
        if (success) {
            DeviceLifecycleManager::getInstance().notifyInterfaceConnected(
                sessionKey, InterfaceType::Hid);
        } else {
            DeviceLifecycleManager::getInstance().notifyInterfaceFailed(
                sessionKey, InterfaceType::Hid, "HID open failed");
        }
    });

connect(&DeviceLifecycleManager::getInstance(),
    &DeviceLifecycleManager::shouldDisconnectHid,
    this, [this](const QString& sessionKey) {
        stop();  // Existing disconnect method
        DeviceLifecycleManager::getInstance().notifyInterfaceDisconnected(
            sessionKey, InterfaceType::Hid);
    });

connect(&DeviceLifecycleManager::getInstance(),
    &DeviceLifecycleManager::shouldReleaseHidState,
    this, [this](const QString& sessionKey) {
        HostManager::getInstance().getKeyboardManager().releaseAllKeys();
        HostManager::getInstance().getMouseManager().releaseAllButtons();
    });
```

### 6.3 CameraManager Migration + FFmpeg Integration

**Remove:**
- Direct connection to HotplugMonitor
- `hasActiveCameraDevice()` as a gate for auto-switch (state managed by DeviceLifecycleManager)
- FFmpeg backend's independent device detection

**Add:**
```cpp
// DeviceLifecycleManager → CameraManager
connect(&DeviceLifecycleManager::getInstance(),
    &DeviceLifecycleManager::shouldConnectCamera,
    this, [this](const QString& sessionKey, const QString& cameraDeviceId) {
        bool success = switchToCameraDeviceByPortChain(cameraDeviceId);
        if (success) {
            startCamera();
            DeviceLifecycleManager::getInstance().notifyInterfaceConnected(
                sessionKey, InterfaceType::Camera);
        } else {
            DeviceLifecycleManager::getInstance().notifyInterfaceFailed(
                sessionKey, InterfaceType::Camera, "camera open failed");
        }
    });

// FFmpeg backend → CameraManager (unified reporting)
connect(m_ffmpegBackend, &FFmpegBackendHandler::backendDeviceChanged,
    this, [this](const QString& deviceId, BackendDeviceEvent event) {
        if (event == BackendDeviceEvent::Disconnected) {
            DeviceLifecycleManager::getInstance().notifyInterfaceDisconnected(
                m_currentSessionKey, InterfaceType::Camera);
        }
        // Note: Backend "connected" events are handled by DeviceLifecycleManager
        // through the normal hotplug flow, not by the backend directly.
    });
```

**FFmpeg Backend Changes:**
- Remove FFmpeg's independent hotplug detection thread
- Add `backendDeviceChanged` signal to `FFmpegBackendHandler`
- FFmpeg backend still handles the low-level device enumeration, but reports changes up through CameraManager instead of acting on them independently

### 6.4 CameraManager Frame Timestamp Tracking

For the test framework's image verification:

```cpp
class CameraManager : public QObject {
    // ...
    qint64 m_lastFrameTimestamp = 0;

    void onNewFrame(const VideoFrame& frame) {
        m_lastFrameTimestamp = QDateTime::currentMSecsSinceEpoch();
        // ... existing frame handling
    }

public:
    qint64 getLastFrameTimestamp() const { return m_lastFrameTimestamp; }
    bool isCameraActive() const {
        return !m_currentCameraDevice.isNull()
            && !m_currentCameraDeviceId.isEmpty()
            && m_lastFrameTimestamp > 0
            && (QDateTime::currentMSecsSinceEpoch() - m_lastFrameTimestamp) < 5000;
    }
};
```

### 6.5 KeyboardManager / MouseManager

These already have `releaseAllKeys()` and `releaseAllButtons()` from the previous session's work. They connect to `shouldReleaseHidState`:

```cpp
// In HostManager or wherever these are initialized:
connect(&DeviceLifecycleManager::getInstance(),
    &DeviceLifecycleManager::shouldReleaseHidState,
    this, [this](const QString& sessionKey) {
        getKeyboardManager().releaseAllKeys();
        getMouseManager().releaseAllButtons();
    });
```

## 7. Thread Model

```
[HotplugMonitor background thread]
    │
    │  (Qt signal, queued connection)
    ▼
[Main thread] DeviceLifecycleManager
    │
    ├── shouldConnectSerial  →  [SerialWorker thread] SerialPortManager::openPort()
    ├── shouldConnectHid     →  [Main thread] VideoHid::switchToHIDDeviceByPortChain()
    ├── shouldConnectCamera  →  [Main thread] CameraManager::startCamera()
    └── notify* callbacks    →  [Main thread] DeviceLifecycleManager state updates
```

DeviceLifecycleManager runs entirely on the main thread. All state mutations happen on the main thread — no mutex needed for `m_sessions`. Serial port open/close still happens on the serial worker thread.

## 8. Hotplug Test Framework

### 8.1 Overview

A guided test wizard integrated into the app. The user performs physical operations (unplug, replug, reboot target), and the system automatically verifies that all interfaces recover correctly.

### 8.2 Test Step Data Structure

```cpp
struct TestVerification {
    QString name;                       // "Serial GET_INFO"
    std::function<bool()> check;        // Returns true if verification passes
    int timeoutMs;                      // Max time to wait
    bool required;                      // Must pass for step to succeed

    // Runtime state
    bool passed = false;
    bool timedOut = false;
    QString errorMessage;
    qint64 elapsedMs = 0;
};

struct HotplugTestStep {
    QString id;
    QString title;
    QString instruction;                // What the user should do
    QString autoVerifyHint;             // Shown while waiting for auto-verification

    enum class TriggerType {
        Manual,          // User clicks "Start Verification"
        AutoOnState      // Triggers when DeviceLifecycleManager state changes
    };
    TriggerType trigger = TriggerType::AutoOnState;

    QList<TestVerification> verifications;

    // Runtime state
    bool completed = false;
    bool passed = false;
    int durationMs = 0;
};
```

### 8.3 Test Plan

| Step | Title | Instruction | Trigger | Verifications |
|------|-------|-------------|---------|---------------|
| 0 | Initial State | Confirm device connected and working | Manual | Serial GET_INFO (5s), Image frames (5s), HID resolution (3s) |
| 1 | Target USB Unplug | Unplug USB on Target side | Auto | Serial disconnected (5s), Camera stopped (3s), HID closed (3s) |
| 2 | Target USB Replug | Replug USB on Target side | Auto | Serial GET_INFO (15s), Image frames (15s), HID resolution (10s) |
| 3 | Target Reboot | Power off target, wait 5s, power on | Auto | Serial GET_INFO (30s), Image frames (30s), HID resolution (20s) |
| 4 | Host USB Unplug | Unplug USB on Host side | Auto | All interfaces disconnected (5s) |
| 5 | Host USB Replug | Replug USB on Host side | Auto | Serial GET_INFO (15s), Image frames (15s), HID resolution (10s) |
| 6 | Rapid Plug Stress | Rapidly unplug and replug (within 3s) | Manual | All interfaces recovered (20s), GET_INFO (5s), Resolution correct (5s) |

### 8.4 Verification Implementations

**Serial GET_INFO:**
```cpp
bool verifySerialGetInfo(int timeoutMs = 5000) {
    auto& serial = SerialPortManager::getInstance();
    if (!serial.isPortReady()) return false;

    QByteArray cmd = CMD_GET_INFO;  // 0x57 0xAB 0x00 0x01
    QEventLoop loop;
    bool gotResponse = false;
    QByteArray responseData;

    auto conn = QObject::connect(&serial, &SerialPortManager::dataReceived,
        [&gotResponse, &responseData](const QByteArray& data) {
            if (data.size() >= 2 && (uint8_t)data[0] == 0x57 && (uint8_t)data[1] == 0xAB) {
                responseData = data;
                gotResponse = true;
            }
        });

    serial.sendCommandAsync(cmd, false);
    QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
    // Also quit on response
    auto conn2 = QObject::connect(&serial, &SerialPortManager::dataReceived,
        [&loop, &gotResponse]() { if (gotResponse) loop.quit(); });
    loop.exec();

    QObject::disconnect(conn);
    QObject::disconnect(conn2);
    return gotResponse && responseData.size() >= 4;
}
```

**Image Transmission:**
```cpp
bool verifyImageFrames(int timeoutMs = 5000) {
    auto& cam = CameraManager::getInstance();
    if (!cam.isCameraActive()) return false;

    qint64 deadline = QDateTime::currentMSecsSinceEpoch() + timeoutMs;
    qint64 lastFrameBefore = cam.getLastFrameTimestamp();

    while (QDateTime::currentMSecsSinceEpoch() < deadline) {
        if (cam.getLastFrameTimestamp() > lastFrameBefore) {
            return true;  // New frame received
        }
        QThread::msleep(100);
    }
    return false;
}
```

**HID Resolution:**
```cpp
bool verifyHidResolution(int timeoutMs = 3000) {
    qint64 deadline = QDateTime::currentMSecsSinceEpoch() + timeoutMs;

    while (QDateTime::currentMSecsSinceEpoch() < deadline) {
        auto res = VideoHid::getInstance().getResolution();
        if (res.width > 0 && res.height > 0) {
            return true;
        }
        QThread::msleep(200);
    }
    return false;
}
```

### 8.5 HotplugTestWizard Class

```cpp
class HotplugTestWizard : public QObject {
    Q_OBJECT

public:
    static HotplugTestWizard& getInstance();

    void startTest();
    void stopTest();
    void nextStep();
    void retryStep();
    void skipStep();
    void startVerification();  // For Manual trigger steps

    int currentStepIndex() const;
    const HotplugTestStep& currentStep() const;
    TestReport generateReport() const;

    struct StepResult {
        QString stepId;
        QString stepTitle;
        QList<TestVerification> verifications;
        bool passed;
        int durationMs;
    };

    struct TestReport {
        QDateTime startTime;
        QDateTime endTime;
        QList<StepResult> steps;

        bool allPassed() const {
            return std::all_of(steps.begin(), steps.end(),
                [](const StepResult& s) { return s.passed; });
        }

        QString summary() const;
    };

signals:
    void stepChanged(int index, const HotplugTestStep& step);
    void verificationUpdated(const QString& name, bool passed, bool timedOut);
    void stepCompleted(int index, bool passed);
    void testCompleted(const TestReport& report);

private:
    QList<HotplugTestStep> m_steps;
    int m_currentStep = -1;
    QTimer* m_verificationTimer = nullptr;
    QDateTime m_testStartTime;

    void runVerifications();
    void checkVerificationTimeout();
    void advanceIfReady();

    // Listen to DeviceLifecycleManager for auto-trigger
    void onSessionStateChanged(const QString& key, DeviceSessionState state);
};
```

### 8.6 UI: Test Wizard Dialog

```
┌──────────────────────────────────────────────────────┐
│  Openterface 热插拔测试向导                            │
│                                                      │
│  ┌──────────────┐  ┌──────────────────────────────┐  │
│  │ ○ 步骤 0     │  │  步骤 3: Target 重启测试      │  │
│  │ ✓ 步骤 1     │  │                              │  │
│  │ ✓ 步骤 2     │  │  操作:                       │  │
│  │ ● 步骤 3     │  │  请关闭 Target 电源，          │  │
│  │ ○ 步骤 4     │  │  等待 5 秒后重新开机           │  │
│  │ ○ 步骤 5     │  │                              │  │
│  │ ○ 步骤 6     │  │  验证结果:                    │  │
│  │              │  │  ✅ 串口重连 + GET_INFO (3.2s) │  │
│  │              │  │  ⏳ 等待图像恢复...            │  │
│  │              │  │  ○ HID 恢复 + 分辨率           │  │
│  │              │  │                              │  │
│  │              │  │  [跳过]  [重试]  [下一步]      │  │
│  └──────────────┘  └──────────────────────────────┘  │
│                                                      │
│  ████████████████████░░░░░░░░  3/7                   │
└──────────────────────────────────────────────────────┘
```

### 8.7 Integration Point

Add entry point in the settings or diagnostics page:

```cpp
// In SettingsDialog or MainWindow
QPushButton* testBtn = new QPushButton(tr("热插拔测试向导"));
connect(testBtn, &QPushButton::clicked, []() {
    HotplugTestDialog dialog;
    dialog.exec();
});
```

## 9. Migration Strategy

The migration is done in 6 phases. Each phase is independently compilable and testable.

### Phase 1: Core Infrastructure (1-2 days)
- Create `DeviceLifecycleManager` class with data structures
- Create `DeviceSession`, `InterfaceInfo`, `InterfaceState`, `DeviceSessionState`
- Implement state machine (`tryAdvanceSession`, `startConnectingInterfaces`, `connectNextInterface`)
- Implement reconnect policy
- Connect to `HotplugMonitor::deviceChangesDetected` (batch signal, not per-device)
- Fix the `break` bug in HotplugMonitor (or bypass it by using batch signal)

### Phase 2: SerialPortManager Migration (1 day)
- Connect SerialPortManager to DeviceLifecycleManager command signals
- Remove SerialHotplugHandler's connection to HotplugMonitor
- Remove SerialPortManager's internal debounce logic
- Keep SerialHotplugHandler as a thin adapter (or remove entirely)
- Verify: target reboot → serial auto-reconnects

### Phase 3: VideoHid Migration (0.5 days)
- Connect VideoHid to DeviceLifecycleManager command signals
- Remove VideoHid's direct HotplugMonitor connection
- Add `shouldReleaseHidState` handler
- Verify: target reboot → HID auto-reconnects

### Phase 4: CameraManager + FFmpeg Integration (1 day)
- Connect CameraManager to DeviceLifecycleManager command signals
- Remove CameraManager's direct HotplugMonitor connection
- Integrate FFmpeg backend: remove independent hotplug, add `backendDeviceChanged` signal
- Add `m_lastFrameTimestamp` tracking
- Verify: target reboot → camera auto-reconnects

### Phase 5: Hotplug Test Framework (1 day)
- Implement `HotplugTestWizard` class
- Implement `HotplugTestDialog` UI
- Implement verification functions (GET_INFO, image frames, HID resolution)
- Wire up to DeviceLifecycleManager state signals

### Phase 6: Integration Testing (1 day)
- Run the full test wizard on real KVMGO hardware
- Fix bugs discovered during testing
- Verify all 7 test steps pass consistently
- Test edge cases: rapid plug, USB 3.0 companion devices

**Total estimated: 5-6 days**

## 10. File Changes Summary

### New Files
| File | Description |
|------|-------------|
| `device/DeviceLifecycleManager.h` | Central lifecycle manager |
| `device/DeviceLifecycleManager.cpp` | Implementation |
| `device/DeviceSession.h` | DeviceSession + InterfaceInfo + enums |
| `ui/hotplug/HotplugTestWizard.h` | Test framework core |
| `ui/hotplug/HotplugTestWizard.cpp` | Test framework implementation |
| `ui/hotplug/HotplugTestDialog.h` | Test wizard UI dialog |
| `ui/hotplug/HotplugTestDialog.cpp` | Test wizard UI implementation |

### Modified Files
| File | Changes |
|------|---------|
| `device/HotplugMonitor.cpp` | Remove `break` after `newDevicePluggedIn` (or bypass via batch signal) |
| `serial/SerialPortManager.cpp` | Replace HotplugMonitor connection with DeviceLifecycleManager connection; simplify error recovery |
| `serial/SerialPortManager.h` | Remove hotplug-related members that moved to DeviceLifecycleManager |
| `serial/serial_hotplug_handler.h` | May be removed or reduced to thin adapter |
| `serial/serial_hotplug_handler.cpp` | Same |
| `video/videohid.cpp` | Replace HotplugMonitor connection with DeviceLifecycleManager connection |
| `video/videohid.h` | Remove hotplug-related members |
| `host/cameramanager.cpp` | Replace HotplugMonitor connection; integrate FFmpeg backend events; add frame timestamp tracking |
| `host/cameramanager.h` | Add `m_lastFrameTimestamp`, `isCameraActive()`, `getLastFrameTimestamp()` |
| `host/backend/ffmpegbackendhandler.h` | Add `backendDeviceChanged` signal; remove independent hotplug |
| `host/backend/ffmpegbackendhandler.cpp` | Same |
| `target/KeyboardManager.cpp` | Connect to `shouldReleaseHidState` (minor) |
| `target/MouseManager.cpp` | Connect to `shouldReleaseHidState` (minor) |
| `CMakeLists.txt` | Add new files |

### Removed/Deprecated
| File/Class | Reason |
|-----------|--------|
| `SerialHotplugHandler` (or significant portions) | Responsibilities absorbed by DeviceLifecycleManager |
| CameraManager's internal auto-switch retry logic | Replaced by DeviceLifecycleManager's ReconnectPolicy |

## 11. Backward Compatibility

- DeviceManager, HotplugMonitor, and HotplugDebounceManager continue to exist — they handle low-level device discovery and debounce
- DeviceLifecycleManager sits *above* them, consuming their output
- Old signals (`deviceAdded`, `deviceRemoved`) on DeviceManager still work for any code that hasn't migrated yet
- Migration is incremental — subsystems can be migrated one at a time

## 12. Risk Assessment

| Risk | Mitigation |
|------|------------|
| Single point of failure (DeviceLifecycleManager) | Comprehensive unit tests; fallback to direct HotplugMonitor connection if manager fails |
| Thread safety | DeviceLifecycleManager runs on main thread only; all state mutations serialized |
| FFmpeg backend regression | Test FFmpeg camera discovery independently before integration |
| Migration breaks existing functionality | Phase-by-phase migration with manual testing between each phase |
| Test framework itself has bugs | Test framework verifies against known-good states; manual override available |
