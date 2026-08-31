# Fix Summary

This document summarizes two key fixes: the CH32V208 device recovery mechanism after target restart, and the DeviceLifecycleManager state refactoring.

---

## Part 1: Keyboard/Mouse and Video Failure After Target Restart — Root Cause Analysis and Fix

### Problem Description

The KVMGO device (CH32V208 chip) becomes completely unresponsive (keyboard, mouse, and video) after the target computer restarts. The host machine must physically unplug and replug the USB cable to recover.

### Device Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    Host Computer                             │
│                                                              │
│  ┌──────────────┐     USB      ┌────────────────────────┐   │
│  │  Openterface  │◄───────────►│    CH32V208 Chip        │   │
│  │   QT App      │   (host     │                        │   │
│  │               │    side)    │  ┌──────────────────┐  │   │
│  └──────────────┘              │  │  Host-side USB    │  │   │
│                                 │  │  (always powered, │  │   │
│                                 │  │   functions)      │  │   │
│                                 │  ├──────────────────┤  │   │
│                                 │  │  Target-side USB  │  │   │
│                                 │  │  (HID + Video)    │  │   │
│                                 │  └────────┬─────────┘  │   │
│                                 └───────────┼────────────┘   │
└─────────────────────────────────────────────┼────────────────┘
                                              │ USB
                                    ┌─────────┴─────────┐
                                    │  Target Computer   │
                                    │ (disrupted during   │
                                    │   restart)          │
                                    └───────────────────┘
```

### Root Cause Analysis

Log analysis revealed two distinct failure modes:

#### Failure Mode A: Target-Side USB Failure (Host Side Normal)

| Time | Symptom | Explanation |
|------|---------|-------------|
| Normal | GET_INFO response byte 6 = `0x01` | Target-side USB connected |
| Target restart | GET_INFO response byte 6 ≠ `0x01` | Target-side USB disconnected |
| Ongoing | `recordSuccess()` still called | CH32V208 remains active on host side |
| Result | Watchdog never triggers | Serial communication appears "normal", but data never reaches target |

**Key Finding**: The protocol layer already parses `targetUSBStatus` signal from GET_INFO response byte 6, but it was only used to update the status bar icon — no code used it to trigger recovery.

#### Failure Mode B: CH32V208 Chip Enters Abnormal State on Host USB Bus

Actual failure sequence observed in logs:

```
22:14:53.829  Error code 8: "A device attached to the system is not functioning"
              → CH32V208 still on USB bus, but cannot communicate properly
              → Old code: treated as "transient error", no action taken

22:14:54~56   Error code 9: "The wait operation timed out" (3 consecutive)
              → Serial write succeeds (Success:true), but no response returned

22:14:57.745  Error code 6: "The I/O operation has been aborted"
              → Old code: recognized as ResourceError, set ERROR_STATE
              → Also set m_deviceUnpluggedDetected = true ← THE PROBLEM!
              → Device is actually still on USB bus; this flag blocks recovery

22:15:08.876  restartPort() called
              → Only closes/reopens the serial port, cannot fix CH32V208
              → Requires RTS hardware reset to recover
```

### Fix

#### Fix 1: Target-Side Disconnect Detection and RTS Recovery

**Files**: `serial/SerialPortManager.h`, `serial/SerialPortManager.cpp`

```
targetUSBStatus(false) signal
        │
        ▼
handleTargetUsbStatusChanged(false)
        │
        ├── 2-second debounce timer
        │      │
        │      ├── Target reconnects → cancel recovery
        │      │
        │      └── Still disconnected after 2s →
        │           1. Stop watchdog + timer
        │           2. Set ready = false
        │           3. Call factoryResetHipChip()
        │              → RTS low 4s → RTS high → close port → 2s wait → reopen
        │              → CH32V208 fully reset
        │
        └── 20-second safety timeout (if target doesn't reconnect, restart watchdog to allow retry)
```

**New code added**:
- `handleTargetUsbStatusChanged(bool connected)` — detects target USB disconnect and triggers RTS recovery
- `m_targetRecoveryInProgress` — flag to prevent duplicate triggers
- `m_targetDisconnectRecoveryTimer` — 2-second debounce timer

#### Fix 2: Host-Side CH32V208 Abnormal State Recovery

**Files**: `serial/SerialPortManager.h`, `serial/SerialPortManager.cpp`

Core change: distinguish "device physically disconnected" from "device present but malfunctioning".

```
handleSerialError error classification:
        │
        ├── Error code 8 (UnknownError, "not functioning")
        │      → Mark as "device present but malfunctioning"
        │      → Don't trigger recovery yet, wait for error code 6
        │
        ├── Error code 6 (ResourceError)
        │      │
        │      ├── Check if device is still on USB bus
        │      │      │
        │      │      ├── Device gone → original logic (physical disconnect)
        │      │      │      Set ERROR_STATE
        │      │      │      Set m_deviceUnpluggedDetected = true
        │      │      │
        │      │      └── Device present + CH32V208 → RTS hardware reset recovery
        │      │             Set ERROR_STATE
        │      │             Do NOT set m_deviceUnpluggedDetected
        │      │             Call triggerRtsRecoveryForUnresponsiveDevice()
        │      │
        │      └── triggerRtsRecoveryForUnresponsiveDevice():
        │             1. Stop watchdog (prevent competing recovery)
        │             2. Call factoryResetHipChip()
        │                → RTS low 4s → RTS high → close port → 2s wait → reopen
        │                → CH32V208 fully reset and re-initialized
        │             3. 20-second safety timeout
        │
        └── Other errors → transient, log only
```

**New code added**:
- `triggerRtsRecoveryForUnresponsiveDevice()` — RTS hardware reset recovery method
- `m_rtsRecoveryInProgress` — flag to prevent concurrent recovery
- Device presence check logic added to `handleSerialError`
- RTS recovery mutex check added to `performRecovery`
- Clear `m_rtsRecoveryInProgress` in `onSerialPortConnectionSuccess`

### Complete Recovery Flow

```
Target restarts
    │
    ├── Failure Mode A: CH32V208 normal, target-side USB disrupted
    │      │
    │      ├── GET_INFO response byte 6 ≠ 0x01
    │      ├── targetUSBStatus(false) emitted
    │      ├── handleTargetUsbStatusChanged(false)
    │      ├── 2-second debounce → factoryResetHipChip()
    │      ├── RTS reset → CH32V208 re-initialized
    │      └── Target re-enumerates → targetUSBStatus(true) → recovery complete
    │
    └── Failure Mode B: CH32V208 abnormal on host USB bus
           │
           ├── Error code 8 → mark device as malfunctioning
           ├── Error code 6 → detect device still on USB bus
           ├── triggerRtsRecoveryForUnresponsiveDevice()
           ├── factoryResetHipChip() → RTS hardware reset
           ├── Serial port reopened → CH32V208 re-initialized
           └── onSerialPortConnectionSuccess → clear recovery flag → normal operation
```

### Modified Files

| File | Changes |
|------|---------|
| `serial/SerialPortManager.h` | Added `handleTargetUsbStatusChanged` slot, `m_targetRecoveryInProgress`, `m_targetDisconnectRecoveryTimer`, `triggerRtsRecoveryForUnresponsiveDevice()` method, `m_rtsRecoveryInProgress` |
| `serial/SerialPortManager.cpp` | Connected `targetUSBStatus` signal, implemented target disconnect recovery, modified `handleSerialError` error classification, implemented RTS recovery method, clear recovery flag, watchdog mutex |

---

## Part 2: DeviceLifecycleManager State Refactoring Summary

### Design Goals

DeviceLifecycleManager is a centralized device lifecycle manager that addresses:

1. **Distributed Decision Problem**: Previously each subsystem (SerialPortManager, CameraManager, VideoHid) independently decided when to reconnect, causing races and conflicts
2. **Inconsistent State**: No unified device state view, making it hard for the UI to display accurate status
3. **Complex Recovery Logic**: Recovery logic for hotplug, target restart, and device malfunction scenarios was scattered across the codebase

### Core Design Principle

> **Subsystems no longer decide *when* to reconnect — they only execute the manager's commands and report results.**

```
┌────────────────────────────────────────────────────────────────┐
│                  DeviceLifecycleManager                        │
│                  (Central Decision Maker, Main Thread)         │
│                                                                │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐      │
│  │ Session  │  │  State   │  │ Reconnect│  │  Fast    │      │
│  │ Manager  │  │ Machine  │  │ Policy   │  │ Reconnect│      │
│  └──────────┘  └──────────┘  └──────────┘  └──────────┘      │
│                                                                │
│  Command Signals (downward)       Report API (upward)          │
│  shouldConnectSerial()    ←──    notifyInterfaceConnected()    │
│  shouldConnectCamera()    ←──    notifyInterfaceFailed()       │
│  shouldDisconnectSerial() ←──    notifyInterfaceDisconnected() │
│  ...                        ...                                │
└────────┬──────────────────────────────┬────────────────────────┘
         │ Commands                     │ Reports
         ▼                              │
┌─────────────────┐  ┌─────────────┐  ┌──────────────┐
│ SerialPortManager│  │ CameraManager│  │   VideoHid   │
│ (Executor,       │  │ (Executor,   │  │ (Executor,   │
│  Worker Thread)  │  │  Main Thread)│  │  HID Thread) │
└─────────────────┘  └─────────────┘  └──────────────┘
```

### Data Model

#### DeviceSession — Complete Lifecycle of One Physical KVM Device

```cpp
struct DeviceSession {
    // Identity
    QString sessionKey;           // = portChain (USB topology path)
    QString portChain;            // Serial device chain "0004-0003-0002"
    QString companionPortChain;   // USB 3.0 companion (camera/HID may be on different bus)
    QString vid, pid;

    // Overall state
    DeviceSessionState state;

    // Four interfaces
    InterfaceInfo serial;
    InterfaceInfo hid;
    InterfaceInfo camera;
    InterfaceInfo audio;

    // Lifecycle tracking
    QDateTime firstSeen;
    QDateTime lastActivity;
    int totalReconnects;
};
```

#### State Enums

```
Interface State (InterfaceState):
  Absent        Interface does not exist on this device
  Disconnected  Interface exists but not connected
  Connecting    Connection in progress
  Connected     Connected and operating normally
  Error         Connection failed (retries exhausted)

Session State (DeviceSessionState):
  Disconnected  Device does not exist
  Enumerating   Device detected, waiting for interfaces to appear
  Connecting    Interfaces opening in sequence
  Ready         All present interfaces are ready
  Degraded      Some interfaces ready, some failed
  Recovering    Device disconnected, attempting recovery
```

### State Machine

#### Normal Connection Flow

```
  Disconnected
       │
       │ onDeviceDetected()
       ▼
  Enumerating
       │
       │ allPresentInterfaces ≥ Disconnected
       ▼
  Connecting ──────── Serial Connecting
       │                    │
       │                    │ notifyInterfaceConnected(Serial)
       │                    ▼
       │              Serial Connected
       │              HID Connecting
       │                    │
       │                    │ notifyInterfaceConnected(HID)
       │                    ▼
       │              HID Connected
       │              Camera Connecting
       │                    │
       │                    │ notifyInterfaceConnected(Camera)
       │                    ▼
       │              Camera Connected
       │              all ready?
       ▼
    Ready ✓
```

#### Connection Order

Interfaces connect in fixed order: **Serial → HID → Camera → Audio**

Each interface connects only after the previous one completes, driven by `tryAdvanceSession()`.

#### Device Disconnect and Recovery

```
  Ready / Connecting / ...
       │
       │ onDeviceRemoved()
       ▼
  Recovering
       │
       ├── Release HID state (stuck keys / mouse buttons)
       ├── Disconnect all active interfaces
       ├── All interface states → Disconnected, retry count reset to zero
       ├── Start 60-second fast reconnect window
       │
       │  (Device reappears)
       ▼
  Connecting ──→ Reconnect interfaces in sequence ──→ Ready
       │
       │  (60-second timeout)
       ▼
  Disconnected
```

#### Interface Failure and Retry

```
  Connecting (Serial)
       │
       │ notifyInterfaceFailed(Serial, "error")
       ▼
  retryCount < maxRetries (7)?
       │
       ├── Yes → scheduleReconnect()
       │         Exponential backoff: 500ms → 1s → 2s → 3s → 5s → 5s → 10s
       │         Total: ~26.5 seconds retry window
       │         Meanwhile, continue connecting next interface (HID/Camera)
       │
       └── No → InterfaceState = Error
                 tryAdvanceSession() continues
                 If all retries exhausted → Degraded
```

**Key Design**: Serial retry does not block other interfaces. HID and Camera can connect in parallel while Serial is retrying.

### Fast Reconnect Window

After device disconnect, the session is preserved for 60 seconds (not immediately deleted). If the device reappears during this window:
- Reset interface retry counts
- Immediately restore to Connecting state
- Covers target restart scenarios (target typically re-enumerates within seconds)

```
Timeline:
  0s ─────── Device disconnects ──────── 60s
  │                                     │
  │  300ms polling detection            │  Timeout → Disconnected
  │  Device reconnects → Connecting     │
  │  → Ready                            │
  │                                     │
  └── startFastReconnectWindow() ───────┘
```

### Reconnect Policy

```cpp
struct ReconnectPolicy {
    QList<int> intervals = {500, 1000, 2000, 3000, 5000, 5000, 10000};
    int maxAttempts = 7;
    // Total window: ~26.5 seconds
};
```

Exponential backoff design covers most target restart durations.

### Signal Interface

#### Command Signals (Manager → Subsystems)

| Signal | Description |
|--------|-------------|
| `shouldConnectSerial(sessionKey, portPath)` | Connect serial port |
| `shouldConnectHid(sessionKey, hidPath)` | Connect HID device |
| `shouldConnectCamera(sessionKey, cameraDeviceId)` | Connect camera |
| `shouldConnectAudio(sessionKey, audioDeviceId)` | Connect audio device |
| `shouldDisconnectSerial/Hid/Camera/Audio(sessionKey)` | Disconnect interface |
| `shouldReleaseHidState(sessionKey)` | Release stuck keys/mouse |

#### State Signals (Manager → UI/Tests)

| Signal | Description |
|--------|-------------|
| `sessionStateChanged(key, newState)` | Session state changed |
| `sessionAdded/Removed(key)` | Session created/removed |
| `interfaceStateChanged(key, type, newState)` | Interface state changed |

### Thread Model

```
┌──────────────────────────────────────────────────┐
│                   Main Thread                     │
│                                                   │
│  DeviceLifecycleManager (Singleton, Main Thread) │
│  ├─ All state changes on this thread             │
│  ├─ No mutexes needed                            │
│  └─ Communicates with subsystems via Qt signals  │
│                                                   │
│  HotplugMonitor                                   │
│  CameraManager                                    │
│  UI Components                                    │
└──────────────────────────────────────────────────┘
         │                    │
    QueuedConnection    QueuedConnection
         │                    │
┌────────▼──────┐    ┌────────▼──────┐
│ SerialWorker   │    │  VideoHid     │
│ Thread         │    │  Thread       │
│                │    │               │
│ SerialPortMgr  │    │  HID handling │
│ ConnectionWdog │    │               │
└────────────────┘    └───────────────┘
```

### USB 3.0 Companion Device Merging

The KVMGO's Serial (CH32V208) and HID+Camera (KVMGO composite) may appear on different USB buses. DeviceLifecycleManager merges these into a single session via `companionPortChain`:

```
CH32V208 Serial:      portChain = "0004-0003-0002"
KVMGO Composite:      portChain = "0004-0003-0004"
                            │
                            ▼
                DeviceLifecycleManager
                Merges into single session:
                sessionKey = "0004-0003-0002"
                portChain = "0004-0003-0002"
                companionPortChain = "0004-0003-0004"
```

### Key Design Decisions

1. **Centralized decisions, distributed execution**: Manager decides when to connect/disconnect; subsystems only execute and report
2. **Sequential connection, parallel retry**: Interfaces start connecting in order, but failed interfaces retry independently in the background
3. **Session preservation, fast reconnect**: Session is kept for 60 seconds after device disconnect, covering target restart scenarios
4. **Thread safety**: Manager runs on main thread, communicates with worker threads via QueuedConnection signals
5. **No mutexes**: All state changes happen on main thread, inherently thread-safe
