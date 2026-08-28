# DeviceLifecycleManager Architecture Diagram

## 1. System Overview

```
┌──────────────────────────────────────────────────────────────────────┐
│                         Main Thread                                  │
│                                                                      │
│  ┌────────────────┐    ┌──────────────────────────────────────────┐  │
│  │   UI Layer     │    │     DeviceLifecycleManager (Singleton)   │  │
│  │                │◄──►│                                          │  │
│  │ DeviceSelector │    │  ┌─────────┐  ┌──────────────────────┐  │  │
│  │ StatusBar      │    │  │ Sessions │  │    State Machine     │  │  │
│  │ VideoPane      │    │  │  QMap    │  │                      │  │  │
│  │ Toolbar        │    │  └─────────┘  │ Enumerating           │  │  │
│  └────────────────┘    │               │ → Connecting           │  │  │
│         │              │               │ → Ready / Degraded     │  │  │
│         │ state signals│               │ → Recovering           │  │  │
│         │              │               └──────────────────────┘  │  │
│         │              │                                          │  │
│         │              │  ┌─────────────────┐  ┌──────────────┐  │  │
│         │              │  │ ReconnectPolicy │  │ Fast Reconnect│  │  │
│         │              │  │ Exponential     │  │ Window (60s)  │  │  │
│         │              │  │ backoff:        │  │              │  │  │
│         │              │  │ 500→1k→2k→...  │  │              │  │  │
│         │              │  └─────────────────┘  └──────────────┘  │  │
│         │              └──────────────────┬───────────────────────┘  │
│         │                                 │                          │
│  ┌──────▼─────────────────────────────────▼───────────────────────┐  │
│  │                    HotplugMonitor                               │  │
│  │  Polling interval: 2-3s | Device discovery: VID/PID matching    │  │
│  │  Emits: deviceChangesDetected(DeviceChangeEvent)                │  │
│  └────────────────────────────────────────────────────────────────┘  │
└──────────────────────────────────────────────────────────────────────┘
         │                                    │
    command signals                     report API
    (shouldConnect*)                   (notifyInterface*)
         │                                    │
    ┌────▼────────────────────────────────────▼──────────────────────┐
    │                                                                │
    │  ┌───────────────┐  ┌──────────────┐  ┌────────────────────┐  │
    │  │ SerialPort    │  │  VideoHid    │  │  CameraManager     │  │
    │  │ Manager       │  │  (HID)       │  │                    │  │
    │  │               │  │              │  │                    │  │
    │  │ Worker Thread │  │ HID Thread   │  │  Main Thread       │  │
    │  └───────────────┘  └──────────────┘  └────────────────────┘  │
    │                                                                │
    └────────────────────────────────────────────────────────────────┘
```

## 2. Command / Report Separation Architecture

Core design principle: **Subsystems do not decide when to reconnect — they only execute commands and report results.**

```
                    DeviceLifecycleManager
                    ┌─────────────────────────┐
                    │                         │
    Command signals (downward)  │  Central Decision Maker  │   Report API (upward)
    ──────────────►│                         │◄────────────────
                    │  "When to connect/       │
                    │   disconnect/retry"      │
                    └────────────┬────────────┘
                                 │
              ┌──────────────────┼──────────────────┐
              │                  │                  │
     shouldConnectSerial  shouldConnectHid  shouldConnectCamera
              │                  │                  │
              ▼                  ▼                  ▼
    ┌────────────────┐ ┌────────────────┐ ┌────────────────┐
    │ SerialPort     │ │   VideoHid     │ │ CameraManager  │
    │ Manager        │ │                │ │                │
    │                │ │                │ │                │
    │ Execute        │ │ Execute        │ │ Execute        │
    │ connect/disconn│ │ connect/disconn│ │ connect/disconn│
    │ Report success/│ │ Report success/│ │ Report success/│
    │        failure │ │        failure │ │        failure │
    └────────────────┘ └────────────────┘ └────────────────┘
```

### Command Signals

| Signal | Target | Description |
|--------|--------|-------------|
| `shouldConnectSerial(key, path)` | SerialPortManager | Connect serial port by portChain |
| `shouldConnectHid(key, path)` | VideoHid | Connect HID device |
| `shouldConnectCamera(key, id)` | CameraManager | Connect camera |
| `shouldConnectAudio(key, id)` | AudioManager | Connect audio device |
| `shouldDisconnectSerial(key)` | SerialPortManager | Disconnect serial port |
| `shouldDisconnectHid(key)` | VideoHid | Disconnect HID |
| `shouldDisconnectCamera(key)` | CameraManager | Disconnect camera |
| `shouldReleaseHidState(key)` | VideoHid | Release stuck keys/mouse |

### Report API

| Method | Caller | Description |
|--------|--------|-------------|
| `notifyInterfaceConnected(key, type)` | All subsystems | Interface connected successfully |
| `notifyInterfaceFailed(key, type, error)` | All subsystems | Interface connection failed |
| `notifyInterfaceDisconnected(key, type)` | All subsystems | Interface disconnected |

## 3. State Machine

### 3.1 Session State (DeviceSessionState)

```
                    ┌──────────────┐
                    │ Disconnected │◄──── Initial state / 60s timeout
                    └──────┬───────┘
                           │
                           │ onDeviceDetected()
                           │ createSession()
                           ▼
                    ┌──────────────┐
                    │  Enumerating │     Device detected, waiting for interfaces
                    └──────┬───────┘
                           │
                           │ allPresentInterfaces ≥ Disconnected
                           ▼
                    ┌──────────────┐
            ┌──────│  Connecting   │──────┐
            │      └───────────────┘      │
            │              │              │
            │              │ ready==present│ all retries
            │              │ && present>0  │ exhausted
            │              ▼              │
            │       ┌──────────────┐      │
            │       │    Ready     │      │
            │       └──────┬───────┘      │
            │              │              │
            │              │ some iface   │
            │              │ failed       │
            │              ▼              ▼
            │       ┌──────────────────────┐
            │       │      Degraded        │
            │       └──────────────────────┘
            │
            │ onDeviceRemoved()
            ▼
    ┌──────────────┐
    │  Recovering  │──── Device reappears within 60s ────► Connecting
    └──────────────┘
            │
            │ 60s timeout
            ▼
    Disconnected
```

### 3.2 Interface State (InterfaceState)

```
    ┌──────────┐
    │  Absent   │  Interface does not exist on this device
    └──────────┘

    ┌──────────────┐
    │ Disconnected  │◄──── Initial / During retry / Device reconnect
    └──────┬───────┘
           │
           │ connectNextInterface()
           │ setInterfaceState(Connecting)
           ▼
    ┌──────────────┐
    │  Connecting   │     Connection in progress
    └──────┬───────┘
           │
     ┌─────┴─────┐
     │           │
     ▼           ▼
┌──────────┐ ┌──────────┐
│ Connected │ │  Error   │ (retries exhausted)
└──────────┘ └──────────┘
```

### 3.3 Interface Connection Order

```
    Serial ──────► HID ──────► Camera ──────► Audio
     [0]           [1]          [2]           [3]

    Each interface starts connecting only after the previous
    one completes (driven by tryAdvanceSession).
    Failed interfaces retry independently in the background,
    without blocking subsequent interfaces.
```

## 4. Device Disconnect and Recovery Flow

```
    Normal operation (Ready)
         │
         │ Device disconnects (target restart / USB unplug)
         │ Detected by HotplugMonitor
         ▼
    onDeviceRemoved()
         │
         ├── 1. emit shouldReleaseHidState()     ← Release stuck keys/mouse
         │
         ├── 2. emit shouldDisconnectSerial/Hid/Camera/Audio()
         │
         ├── 3. session.state → Recovering
         │
         ├── 4. All interfaces → Disconnected, retryCount = 0
         │
         └── 5. startFastReconnectWindow(60s)
                   │
                   ├── 300ms timer keeps event loop active
                   │
                   ├── 60s timeout → session.state → Disconnected
                   │
                   └── Device reappears → onDeviceDetected()
                          │
                          ├── Find session in Recovering state
                          ├── Reset retryCount
                          ├── session.state → Connecting
                          ├── stopFastReconnectWindow()
                          └── startConnectingInterfaces()
```

## 5. Retry Policy

```
    Interface connection fails
         │
         │ notifyInterfaceFailed(key, type, error)
         ▼
    retryCount < maxRetries (7)?
         │
    ┌────┴────┐
    │         │
    ▼         ▼
   Yes       No
    │         │
    │         └──► InterfaceState = Error
    │              All retries exhausted → Degraded
    │
    └──► scheduleReconnect()
          │
          │ Exponential backoff delays:
          │  Attempt 0:  500ms
          │  Attempt 1:  1000ms
          │  Attempt 2:  2000ms
          │  Attempt 3:  3000ms
          │  Attempt 4:  5000ms
          │  Attempt 5:  5000ms
          │  Attempt 6:  10000ms
          │  ──────────────
          │  Total:  ~26.5s
          │
          └──► QTimer::singleShot(delay, ...)
                    │
                    │ After delay expires
                    ▼
               emit shouldConnect*(key, portChain)
               Retry connection
```

**Key**: Serial retry does not block HID/Camera. When Serial fails, the state machine continues connecting the next interface.

## 6. USB 3.0 Companion Device Merging

The KVMGO device's Serial (CH32V208) and HID+Camera (KVMGO composite) may appear on different USB buses:

```
    USB Topology:

    USB Hub Port 2 ──── CH32V208 (Serial)
        │               VID: 1A86, PID: FE0C
        │               portChain: "0004-0003-0002"
        │
    USB Hub Port 4 ──── KVMGO Composite (HID + Camera)
                        VID: 345F, PID: 2132
                        portChain: "0004-0003-0004"


    DeviceLifecycleManager merge logic:

    ┌──────────────────────────────────────────────┐
    │  DeviceSession                                │
    │                                               │
    │  sessionKey:       "0004-0003-0002"           │
    │  portChain:        "0004-0003-0002"  ← Serial │
    │  companionPortChain: "0004-0003-0004"← HID/Cam│
    │                                               │
    │  serial:  Connected  (COM7)                   │
    │  hid:     Connected  (\\.\hid#...)            │
    │  camera:  Connected  (camera device id)       │
    │  audio:   Absent                               │
    │                                               │
    │  state: Ready (3/3 interfaces)                │
    └──────────────────────────────────────────────┘
```

### Merge Flow

```
    Step 1: Generation3Discoverer finds KVMGO composite
    → Create Session (HID + Camera, no Serial)
    → State: Enumerating

    Step 2: BotherDeviceDiscoverer finds CH32V208 serial
    → findCompanionSessionForDevice() detects existing session
      missing Serial but has HID/Camera
    → mergeCompanionDevice() merges them
    → Session now includes Serial + HID + Camera
    → State: Connecting → begin sequential interface connection
```

## 7. Thread Model

```
    ┌─────────────────────────────────────────────────────────┐
    │                     Main Thread                          │
    │                                                          │
    │  ┌──────────────────────┐  ┌──────────────┐             │
    │  │ DeviceLifecycleMgr   │  │ HotplugMon   │             │
    │  │ (all state changes   │  │ (polling     │             │
    │  │  on this thread)     │  │  detection)  │             │
    │  │ No mutexes needed    │  │              │             │
    │  └──────────┬───────────┘  └──────┬───────┘             │
    │             │                     │                      │
    │  ┌──────────┴───────────┐                                │
    │  │ CameraManager        │                                │
    │  └──────────────────────┘                                │
    └──────────┬──────────────────────────────┬────────────────┘
               │                              │
       QueuedConnection               QueuedConnection
               │                              │
    ┌──────────▼───────────┐    ┌─────────────▼───────────────┐
    │  SerialWorkerThread   │    │     VideoHidThread           │
    │                       │    │                              │
    │  SerialPortManager    │    │  HID keyboard/mouse          │
    │  ConnectionWatchdog   │    │  event handling              │
    │  FactoryResetManager  │    │                              │
    │                       │    │                              │
    │  All serial ops on    │    │                              │
    │  this thread          │    │                              │
    └───────────────────────┘    └─────────────────────────────┘
```

## 8. Signal Flow Sequence Diagrams

### Normal Connection Flow

```
  HotplugMonitor    DeviceLifecycleManager    SerialPortManager    VideoHid           CameraManager
       │                    │                       │                   │                   │
       │ deviceChanges      │                       │                   │                   │
       │ ──────────────────►│                       │                   │                   │
       │                    │                       │                   │                   │
       │                    │ shouldConnectSerial   │                   │                   │
       │                    │ ─────────────────────►│                   │                   │
       │                    │                       │ open port         │                   │
       │                    │                       │ init CH32V208     │                   │
       │                    │  notifyConnected(Serial)                  │                   │
       │                    │ ◄─────────────────────│                   │                   │
       │                    │                       │                   │                   │
       │                    │ shouldConnectHid      │                   │                   │
       │                    │ ─────────────────────────────────────────►│                   │
       │                    │                       │                   │ open HID          │
       │                    │  notifyConnected(Hid) │                   │                   │
       │                    │ ◄────────────────────────────────────────│                   │
       │                    │                       │                   │                   │
       │                    │ shouldConnectCamera   │                   │                   │
       │                    │ ─────────────────────────────────────────────────────────────►│
       │                    │                       │                   │                   │ startCamera
       │                    │  notifyConnected(Camera)                  │                   │
       │                    │ ◄───────────────────────────────────────────────────────────│
       │                    │                       │                   │                   │
       │                    │ state → Ready ✓       │                   │                   │
       │                    │                       │                   │                   │
```

### Device Disconnect Recovery Flow

```
  HotplugMonitor    DeviceLifecycleManager    SerialPortManager    VideoHid           CameraManager
       │                    │                       │                   │                   │
       │ deviceRemoved      │                       │                   │                   │
       │ ──────────────────►│                       │                   │                   │
       │                    │                       │                   │                   │
       │                    │ shouldReleaseHid      │                   │                   │
       │                    │ ─────────────────────────────────────────►│                   │
       │                    │                       │                   │                   │
       │                    │ shouldDisconnectSerial│                   │                   │
       │                    │ ─────────────────────►│                   │                   │
       │                    │ shouldDisconnectCamera│                   │                   │
       │                    │ ─────────────────────────────────────────────────────────────►│
       │                    │                       │                   │                   │
       │                    │ state → Recovering    │                   │                   │
       │                    │ start 60s window      │                   │                   │
       │                    │                       │                   │                   │
       │          ... waiting for device to reappear ...                │                   │
       │                    │                       │                   │                   │
       │ deviceAdded        │                       │                   │                   │
       │ ──────────────────►│                       │                   │                   │
       │                    │                       │                   │                   │
       │                    │ state → Connecting    │                   │                   │
       │                    │ stop 60s window       │                   │                   │
       │                    │ shouldConnectSerial   │                   │                   │
       │                    │ ─────────────────────►│                   │                   │
       │                    │         ...           │                   │                   │
       │                    │ state → Ready ✓       │                   │                   │
       │                    │                       │                   │                   │
```

## 9. Key Design Decisions

| Decision | Rationale |
|----------|-----------|
| Centralized decisions, distributed execution | Prevents subsystems from racing over reconnection timing; unified state view |
| Sequential interface connection | Serial→HID→Camera→Audio avoids dependency conflicts (e.g. HID requires serial ready first) |
| Failed interface retries in background | Serial retry does not block HID/Camera connection |
| 60-second fast reconnect window | Covers target restart scenarios (device re-enumerates within seconds) |
| USB 3.0 companion merge | Handles KVMGO's Serial and HID/Camera appearing on different USB buses |
| Main-thread singleton, no mutexes | All state changes on main thread; inherently thread-safe |
| Exponential backoff retry | 500ms→10s, total ~26.5s window covers most restart durations |
| QueuedConnection signals | Safe cross-thread communication; prevents reentrant calls |
