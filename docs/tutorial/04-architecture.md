# Tutorial 04 — Architecture Deep Dive

This guide is aimed at contributors who want to understand the codebase structure, design patterns, and major subsystems of the Openterface Mini-KVM application.

## Table of Contents

| Section | Covers |
|---------|--------|
| [Project Structure](#project-structure) | Directory layout, key classes per module |
| [Application Bootstrap](#application-bootstrap) | Two-phase init, deferred startup |
| [Global State Management](#global-state-management) | `GlobalVar`, `GlobalSetting`, shutdown flag |
| [Coordinator Pattern](#coordinator-pattern) | DeviceCoordinator, MenuCoordinator, WindowLayoutCoordinator, SerialCommandCoordinator |
| [Video Backend System](#video-backend-system) | FFmpeg, GStreamer, Qt backends, hardware acceleration |
| [Serial Communication System](#serial-communication-system) | Protocol framing, chip strategies, watchdog |
| [HID & USB Transport](#hid--usb-transport) | Platform transports, chip variants, libusb usage |
| [TCP Server](#tcp-server) | Command protocol, socket lifecycle |
| [Script Interpreter](#script-interpreter) | Lexer → Parser → Executor pipeline |
| [Firmware Update System](#firmware-update-system) | WCH flash pipeline, network client |
| [Settings & Persistence](#settings--persistence) | QSettings, preference pages |
| [Build System](#build-system) | CMake options, architecture detection |

---

## Project Structure

The codebase follows a modular directory layout where each directory owns a distinct subsystem:

| Directory | Purpose | Key Classes |
|-----------|---------|-------------|
| [`ui/`](ui/) | GUI widgets, dialogs, preferences, toolbars, coordinators | `MainWindow`, `VideoPane`, `DeviceCoordinator`, `MenuCoordinator`, `WindowLayoutCoordinator`, `GlobalSetting` |
| [`host/`](host/) | Video/audio capture backends, camera management, USB control | `CameraManager`, `MultimediaBackendHandler`, `FFmpegBackendHandler`, `GStreamerBackendHandler`, `QtBackendHandler`, `QtMultimediaBackendHandler`, `AudioManager` |
| [`serial/`](serial/) | Serial port communication, protocol parsing, chip strategies | `SerialPortManager`, `SerialProtocol`, `IChipStrategy`, `CH9329Strategy`, `CH32V208Strategy`, `ChipStrategyFactory`, `ConnectionWatchdog`, `SerialCommandCoordinator` |
| [`video/`](video/) | HID transport for video capture chips, firmware operations | `VideoHid`, `IHIDTransport`, `LinuxHIDTransport`, `WindowsHIDTransport`, `VideoChip`, `Ms2109Chip`, `Ms2109sChip`, `Ms2130sChip`, `FirmwareOperationManager` |
| [`device/`](device/) | Platform-agnostic device enumeration, hotplug monitoring | `DeviceManager`, `DeviceInfo`, `HotplugMonitor`, `AbstractPlatformDeviceManager` |
| [`server/`](server/) | TCP server for remote command/image access | `TcpServer`, `TcpResponse` |
| [`scripts/`](scripts/) | Embedded scripting language for automation | `Lexer`, `Parser`, `ASTNode`, `SemanticAnalyzer`, `ScriptExecutor`, `ScriptRunner` |
| [`wch/`](wch/) | WCH chip ISP firmware flashing utilities | `WCHFlasher`, `WCHUSBTransport`, `WCHProtocol`, `WCHHexParser`, `WCHDevice` |
| [`target/`](target/) | Keyboard/mouse input forwarding to target (HID emulation) | `KeyboardManager`, `MouseManager`, `Keymapping` |
| [`config/`](config/) | JSON keyboard layouts + Qt translation files | 11 keyboard layouts, 12 language translations |
| [`cmake/`](cmake/) | CMake module configuration files | `FFmpeg.cmake`, `Configuration.cmake`, `SourceFiles.cmake`, `Resources.cmake` |

---

## Application Bootstrap

The application follows a **two-phase initialization** pattern to keep startup responsive. Entry point is [`main.cpp`](main.cpp):

```
main()
  ├── setupEnv()                    // Set QT_QPA_PLATFORM (xcb/wayland)
  ├── QApplication creation         // Fusion style, app name/version
  ├── SplashScreen                  // SVG splash with loading animation
  ├── GlobalSetting::loadLogSettings()
  ├── GlobalSetting::loadVideoSettings()
  ├── applyMediaBackendSetting()    // Configure GStreamer env vars or QT_MEDIA_BACKEND
  ├── LogHandler::instance().enableLogStore()
  ├── KeyboardLayoutManager::loadLayouts()
  ├── LanguageManager created
  ├── MainWindow constructed
  │     ├── WindowLayoutCoordinator  // Created FIRST (needs early resize)
  │     └── MainWindowInitializer created + initialize()
  │           ├── setupCentralWidget()
  │           ├── setupCoordinators()        // DeviceCoordinator, MenuCoordinator
  │           ├── connectCornerWidgetSignals()
  │           ├── connectDeviceManagerSignals()
  │           ├── connectActionSignals()     // Menu actions -> coordinators
  │           ├── setupToolbar()
  │           ├── connectCameraSignals()
  │           ├── connectVideoHidSignals()
  │           ├── setupScriptComponents()
  │           ├── setupEventCallbacks()
  │           ├── setupKeyboardShortcuts()
  │           └── finalize()
  ├── window->show()
  ├── QTimer(5ms):   deferredSetupCoordinators()  // Device enumeration, hotplug
  ├── QTimer(150ms): deferredInitializeCamera()   // Camera/audio init + VideoHid start
  ├── QTimer(500ms): EnvironmentSetupDialog        // Check drivers/udev rules
  └── app.exec()                                    // Enter event loop
```

The key insight: lightweight coordinator creation and signal wiring happens synchronously, while expensive operations (device enumeration, camera initialization) are deferred via `QTimer::singleShot` after the window is shown.

---

## Global State Management

### Atomic Shutdown Flag

```cpp
// Declared in global.h, defined in main.cpp
extern QAtomicInteger<int> g_applicationShuttingDown;
```

Set to `1` during `closeEvent()` and destructor. Used across the codebase to signal all threads to stop and prevent Qt Multimedia operations during shutdown.

### `GlobalVar` Singleton ([`global.h`](global.h))

Thread-unsafe Meyers singleton holding **runtime** application state:

- Input/capture resolution (`input_width`, `capture_width`, etc.)
- Window geometry (`win_width`, `menu_height`, `statusbar_height`)
- Mouse mode (`absolute_mouse_mode`)
- Firmware version (`captureCardFirmwareVersion`)
- USB switch state (`_isSwitchOnTarget`)
- Toolbar visibility (`toolbarVisible`)

Copy and move constructors are explicitly deleted. Default values are 1920x1080 at 30fps.

### `GlobalSetting` Singleton ([`ui/globalsetting.h`](ui/globalsetting.h))

QSettings-backed **persistent** settings manager for:

- Log filters, video settings, media backend selection
- Hardware acceleration preference, scaling quality
- Keyboard layout, language, operating mode
- Recording codec/bitrate/pixel format
- Screen ratio, port chain, serial baudrate
- Update reminder throttling

---

## Coordinator Pattern

The codebase uses coordinators to extract UI-domain mediation logic out of `MainWindow`, reducing its responsibilities. Four coordinator classes exist:

### DeviceCoordinator

**Files:** [`ui/coordinator/devicecoordinator.h`](ui/coordinator/devicecoordinator.h), [`ui/coordinator/devicecoordinator.cpp`](ui/coordinator/devicecoordinator.cpp)

Mediates between device hardware detection, the device selection menu, and camera switching. Handles device enumeration (via `DeviceManager`), hotplug event integration, menu population with deduplicated devices (by port chain), and auto-selection of the first available device.

**Signals:** `deviceSelected()`, `deviceMenuUpdateRequested()`, `deviceSwitchCompleted()`

**Mediates between:** `QMenu` (device menu) ↔ `DeviceManager` (singleton) ↔ `CameraManager` ↔ `HotplugMonitor`

**Instantiation:** `MainWindowInitializer::setupCoordinators()` — parented to `MainWindow`, destroyed with it.

### MenuCoordinator

**Files:** [`ui/coordinator/menucoordinator.h`](ui/coordinator/menucoordinator.h), [`ui/coordinator/menucoordinator.cpp`](ui/coordinator/menucoordinator.cpp)

Manages language selection and baudrate selection menus, delegating actual changes to `LanguageManager` and `SerialPortManager`.

**Signals:** `languageChanged()`, `baudrateChanged()`

**Mediates between:** `QMenu` (language/baudrate menus) ↔ `LanguageManager` ↔ `SerialPortManager`

### WindowLayoutCoordinator

**Files:** [`ui/coordinator/windowlayoutcoordinator.h`](ui/coordinator/windowlayoutcoordinator.h), [`ui/coordinator/windowlayoutcoordinator.cpp`](ui/coordinator/windowlayoutcoordinator.cpp)

All window geometry, resize, fullscreen, and zoom operations. Calculates aspect-ratio-aware window dimensions, handles screen bounds clamping, manages fullscreen entry/exit, and animates the video pane when toolbar visibility changes.

**Signals:** `layoutChanged()`, `fullscreenChanged()`, `zoomChanged()`

**Mediates between:** `QMainWindow` ↔ `VideoPane` ↔ `QMenuBar`/`QStatusBar` ↔ `ToolbarManager` ↔ `GlobalVar`/`GlobalSetting`

**Instantiation:** Created **earliest** — directly in `MainWindow` constructor before `MainWindowInitializer`, because `doResize()` is called from `resizeEvent()`.

### SerialCommandCoordinator

**Files:** [`serial/SerialCommandCoordinator.h`](serial/SerialCommandCoordinator.h), [`serial/SerialCommandCoordinator.cpp`](serial/SerialCommandCoordinator.cpp)

Manages serial command queuing, sync/async execution, checksum calculation, and statistics tracking. Used compositionally within `SerialPortManager` — not wired through the `MainWindow` layer.

**Signals:** `dataSent()`, `dataReceived()`, `commandExecuted()`, `statisticsUpdated()`

---

## Video Backend System

### Architecture Overview

The video capture system uses a **pluggable backend** pattern with a factory selecting the handler at runtime based on user preference stored in `GlobalSetting`.

```
CameraManager
  └── MultimediaBackendHandler (abstract base)
        ├── FFmpegBackendHandler     (default, most feature-complete)
        ├── GStreamerBackendHandler  (Linux only)
        ├── QtBackendHandler         (Windows-specific)
        └── QtMultimediaBackendHandler (minimal fallback)
```

### Base Class: `MultimediaBackendHandler`

**File:** [`host/multimediabackend.h`](host/multimediabackend.h)

Abstract base defining the contract for all backends:

- Camera lifecycle: `prepareCameraCreation()`, `startCamera()`, `stopCamera()`, `cleanupCamera()`
- Format selection: `selectOptimalFormat()`, `getSupportedFrameRates()`
- Hardware acceleration: `getAvailableHardwareAccelerations()`
- Recording interface: `startRecording()`, `stopRecording()`, `pauseRecording()`, `resumeRecording()`
- Configuration via `MultimediaBackendConfig` struct

### Factory: `MultimediaBackendFactory`

**File:** [`host/multimediabackend.cpp`](host/multimediabackend.cpp)

`detectBackendType()` reads the user preference from `GlobalSetting::instance().getMediaBackend()`. If unset/unknown, defaults to **FFmpeg**.

### FFmpegBackendHandler

**File:** [`host/backend/ffmpegbackendhandler.h`](host/backend/ffmpegbackendhandler.h)

The most feature-complete backend. Uses **delegation/composition** rather than inheritance — delegates to specialized single-responsibility managers:

| Component | File | Responsibility |
|-----------|------|---------------|
| `CaptureThread` | [`host/backend/ffmpeg/capturethread.h`](host/backend/ffmpeg/capturethread.h) | QThread running the capture loop |
| `FFmpegDeviceManager` | [`host/backend/ffmpeg/ffmpeg_device_manager.h`](host/backend/ffmpeg/ffmpeg_device_manager.h) | Opens/closes V4L2 devices, sets up decoders |
| `FFmpegHardwareAccelerator` | [`host/backend/ffmpeg/ffmpeg_hardware_accelerator.h`](host/backend/ffmpeg/ffmpeg_hardware_accelerator.h) | VAAPI, V4L2-M2M decoder acceleration |
| `FFmpegFrameProcessor` | [`host/backend/ffmpeg/ffmpeg_frame_processor.h`](host/backend/ffmpeg/ffmpeg_frame_processor.h) | Decoding, scaling, frame dropping, TurboJPEG |
| `FFmpegRecorder` | [`host/backend/ffmpeg/ffmpeg_recorder.h`](host/backend/ffmpeg/ffmpeg_recorder.h) | Video recording to file (mp4/avi/mov/mkv) |
| `FFmpegDeviceValidator` | [`host/backend/ffmpeg/ffmpeg_device_validator.h`](host/backend/ffmpeg/ffmpeg_device_validator.h) | OS-specific device path validation |
| `FFmpegHotplugHandler` | [`host/backend/ffmpeg/ffmpeg_hotplug_handler.h`](host/backend/ffmpeg/ffmpeg_hotplug_handler.h) | USB hotplug monitoring for capture devices |
| `FFmpegCaptureManager` | [`host/backend/ffmpeg/ffmpeg_capture_manager.h`](host/backend/ffmpeg/ffmpeg_capture_manager.h) | Coordinates capture lifecycle |

Supports direct V4L2 capture, hardware-accelerated decoding (VAAPI, V4L2-M2M), TurboJPEG fast MJPEG decoding, and full recording with configurable codec/bitrate.

### GStreamerBackendHandler

**File:** [`host/backend/gstreamerbackendhandler.h`](host/backend/gstreamerbackendhandler.h)

Linux-only backend. Runs GStreamer pipelines either in-process (`InProcessGstRunner`) or externally via `gst-launch-1.0` subprocess (`ExternalGstRunner`). Supports multiple recording strategies (valve-based tee, frame-based appsink, direct filesink).

### QtBackendHandler / QtMultimediaBackendHandler

**Files:** [`host/backend/qtbackendhandler.h`](host/backend/qtbackendhandler.h), [`host/backend/qtmultimediabackendhandler.h`](host/backend/qtmultimediabackendhandler.h)

- `QtBackendHandler`: Windows-specific, wraps `QMediaRecorder` and `QMediaCaptureSession`.
- `QtMultimediaBackendHandler`: Minimal fallback for platforms where FFmpeg/GStreamer are unavailable. No recording support.

### Hardware Acceleration

**File:** [`host/backend/ffmpeg/ffmpeg_hardware_accelerator.h`](host/backend/ffmpeg/ffmpeg_hardware_accelerator.h)

`FFmpegHardwareAccelerator` manages hardware-accelerated decoding. Tries hardware decoders (VAAPI, V4L2-M2M) based on user preference from `GlobalSetting`, falls back to software. Preference stored via `GlobalSetting::setHardwareAcceleration()`.

---

## Serial Communication System

### Architecture

```
SerialPortManager (singleton)
  ├── IChipStrategy (pluggable via ChipStrategyFactory)
  │     ├── CH9329Strategy     (VID:PID 1A86:7523, dual baudrate 9600/115200)
  │     └── CH32V208Strategy   (VID:PID 1A86:FE0C, fixed 115200)
  ├── SerialProtocol            (packet building/parsing)
  ├── SerialCommandCoordinator  (command queuing, sync/async execution)
  ├── ConnectionWatchdog        (connection health monitoring, auto-recovery)
  ├── FactoryResetManager       (chip-specific factory reset logic)
  ├── SerialStatistics          (performance metrics)
  └── SerialHotplugHandler      (auto-connect retry scheduling)
```

### Serial Protocol

**File:** [`serial/protocol/SerialProtocol.h`](serial/protocol/SerialProtocol.h)

Fixed frame format:

| Field | Size | Value |
|-------|------|-------|
| Header | 2 bytes | `0x57 0xAB` |
| Address | 1 byte | `0x00` |
| Command | 1 byte | e.g., `0x01` GET_INFO, `0x02` SEND_KB, `0x0F` RESET |
| Length | 1 byte | Payload size |
| Payload | variable | Command-specific data |
| Checksum | 1 byte | Sum of all preceding bytes |

Response codes have the high bit set (`RESPONSE_BIT = 0x80`): `0x81`, `0x82`, `0x88`, `0x8F`, `0x97`.

`SerialProtocol::parsePacket()` validates header, extracts packet size, verifies checksum. `processResponse()` dispatches to the appropriate handler based on response code.

### Chip Strategy Pattern

**Interface:** [`serial/chipstrategy/IChipStrategy.h`](serial/chipstrategy/IChipStrategy.h)

`ChipStrategyFactory::detectChipType()` examines the serial port's VID/PID to select the strategy:

| Chip | VID:PID | Baudrate | Config | USB Switch |
|------|---------|----------|--------|------------|
| CH9329 | `1A86:7523` | 9600 or 115200 | Command-based | No |
| CH32V208 | `1A86:FE0C` | 115200 only | Close/reopen | Yes (`0x17`) |

### Connection Watchdog

**File:** [`serial/watchdog/ConnectionWatchdog.h`](serial/watchdog/ConnectionWatchdog.h)

Tracks connection state (`Disconnected`, `Connecting`, `Connected`, `Unstable`, `Recovering`, `Failed`), with configurable thresholds (30s watchdog interval, 10 max consecutive errors, 5 max retries with exponential backoff). Implements `IRecoveryHandler` interface with `SerialPortManager` as the handler.

---

## HID & USB Transport

### Transport Abstraction

`VideoHid` holds `std::unique_ptr<IHIDTransport>` — either `LinuxHIDTransport` or `WindowsHIDTransport` based on platform.

**Interface:** [`video/transport/IHIDTransport.h`](video/transport/IHIDTransport.h)

Two tiers of I/O:
- **High-level:** `sendFeatureReport()` / `getFeatureReport()` — with retry logic
- **Low-level:** `sendDirect()` / `getDirect()` — single attempt, used by chip protocol engines

| Platform | Transport | API |
|----------|-----------|-----|
| Linux | `LinuxHIDTransport` | `/dev/hidrawX` + `ioctl(HIDIOCSFEATURE/HIDIOCGFEATURE)` |
| Windows | `WindowsHIDTransport` | Win32 `HidD_SetFeature/HidD_GetFeature` with overlapped I/O |

### Video Chip Variants

Three HDMI capture chip variants with different register maps:

| Chip | Registers | EEPROM | Notes |
|------|-----------|--------|-------|
| MS2109 | `0xCxxx` range | `0xE5`/`0xE6` | GPIO, SPDIF |
| MS2109S | `0xC7xx`, `0xC6xx`, `0xC8xx` | — | No GPIO/SPDIF, timing registers |
| MS2130S | `0x1Cxx`/`0x1Fxx` range | SPI flash | GPIO save state, firmware flash support |

### libusb Usage

libusb-1.0 is used **only in the WCH system** ([`wch/WCHUSBTransport.h`](wch/WCHUSBTransport.h)) on Linux/macOS for WCH ISP flashing (VID `0x4348`/`0x1A86`, PID `0x55E0`, bulk EP `0x02`/`0x82`). The HID video transport does NOT use libusb — it uses OS-native HID APIs.

---

## TCP Server

**Files:** [`server/tcpServer.h`](server/tcpServer.h), [`server/tcpResponse.h`](server/tcpResponse.h)

`TcpServer` inherits `QTcpServer`. On `startServer(port)`, it begins listening. `onReadyRead()` processes incoming data through `parseCommand()`:

| Command | Description |
|---------|-------------|
| `CmdGetLastImage` | Send the last captured image |
| `CmdGetTargetScreen` | Capture and send the target screen |
| `CheckStatus` | Return status |
| `ScriptCommand` | Compile and execute an AHK-style script |

Responses are JSON built by `TcpResponse` factory methods with `ResponseType` (Image, Screen, Status, Error) and `ResponseStatus` (Success, Error, Warning, Pending).

**Security note:** The server has no authentication, encryption, rate limiting, or connection limits. Any TCP connection can execute commands including script execution.

---

## Script Interpreter

**Directory:** [`scripts/`](scripts/)

AHK (AutoHotkey)-inspired custom language. Compilation/execution pipeline:

```
Source text
  ├── Lexer          → std::vector<Token>
  ├── Parser         → ASTNode tree (StatementList of CommandStatementNode)
  ├── ScriptRunner   → triggers SemanticAnalyzer::analyzeTree()
  ├── SemanticAnalyzer → walks AST, dispatches command handlers
  └── KeyboardMouse  → builds HID packets, sends via SerialPortManager
```

Supported commands: `If`, `Else`, `Loop`, `While`, `For`, `Send`, `Click`, `MouseMove`, `Sleep`, `FullScreenCapture`, `AreaScreenCapture`.

Modifier prefixes: `^` → Ctrl, `+` → Shift, `!` → Alt, `#` → Win.

Escape sequences: `` `n `` → Enter, `` `t `` → Tab.

---

## Firmware Update System

### WCH Flash Pipeline

**Directory:** [`wch/`](wch/)

1. **Transport:** `WCHUSBTransport::open()` connects to WCH ISP device
2. **Identify:** `WCHFlasher::identify()` sends signature, receives chipID, looks up in `WCHChipDB`
3. **Read config:** Reads RDPR, UserData, WPR, BTVER, UID registers
4. **Derive key:** 8-byte XOR encryption key from chip UID
5. **Parse firmware:** `WCHHexParser::parseFile()` converts .hex/.bin to bytes
6. **Flash sequence:**
   - `unprotect()` — clears code flash protection
   - `erase()` — calculates sectors (1KB each)
   - `program()` — encrypts chunks with XOR key, 56-byte chunks with progress callbacks
   - `verify()` — reads back and compares
   - `protect()` — re-enables protection
   - `reset()` — exits bootloader

### Video Firmware Operations

**Directory:** [`video/`](video/)

`FirmwareNetworkClient` downloads firmware index from `assets.openterface.com`, selects correct binary per chip type. `FirmwareOperationManager` orchestrates read/write on a worker thread using `FirmwareReader`/`FirmwareWriter`.

---

## Settings & Persistence

Uses Qt's `QSettings` through the `GlobalSetting` singleton. Settings are organized into categories:

- **Video:** resolution, FPS, backend selection, hardware acceleration, scaling quality
- **Audio:** codec, bitrate, sample rate, mute state
- **Recording:** codec, bitrate, pixel format, keyframe interval
- **Target Control:** mouse mode, keyboard layout, operating mode
- **Log:** log level, file output, filters
- **System:** language, screen ratio, port chain, baudrate

Preferences pages ([`ui/preferences/`](ui/preferences/)) wire directly to `GlobalSetting` getter/setter methods.

---

## Build System

### CMake Configuration

**File:** [`CMakeLists.txt`](CMakeLists.txt)

Key features:
- **Architecture detection:** Automatically detects arm64/amd64 via `CMAKE_SYSTEM_PROCESSOR`, overridable with `-DOPENTERFACE_ARCH`
- **Static vs dynamic:** `OPENTERFACE_BUILD_STATIC` (default ON). Static builds link compression libraries, XCB/Wayland plugins, and FFmpeg statically.
- **FFmpeg integration:** Via [`cmake/FFmpeg.cmake`](cmake/FFmpeg.cmake) — supports both static and shared linking (`USE_SHARED_FFMPEG`)
- **GStreamer:** Linux-only, linked via [`cmake/GStreamer.cmake`](cmake/GStreamer.cmake)
- **Translations:** Handled via [`cmake/Internationalization.cmake`](cmake/Internationalization.cmake)
- **Source file management:** Via [`cmake/SourceFiles.cmake`](cmake/SourceFiles.cmake)
- **Optimization:** LTO and `-Os` for amd64 Release builds (disabled for arm64 to avoid segfaults)

### CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `OPENTERFACE_BUILD_STATIC` | `ON` | Static linking where possible |
| `USE_SHARED_FFMPEG` | `OFF` | Use shared FFmpeg libraries |
| `FFMPEG_PREFIX` | `/opt/ffmpeg` | Path to FFmpeg installation |
| `CMAKE_PREFIX_PATH` | auto | Path to Qt6 CMake config |

### qmake Alternative

**File:** [`openterfaceQT.pro`](openterfaceQT.pro)

Legacy qmake build file still maintained as a fallback. Generates translation files via `lrelease`.
