# Tutorial 03 — Advanced Features

**Audience:** Intermediate to Expert — power user features and configuration

## Table of Contents

| # | Feature | Description |
|---|---------|-------------|
| 1 | [Preferences System](#1-preferences-system) | Video, audio, target control, logging settings |
| 2 | [EDID Management](#2-edid-management) | What EDID is, custom resolutions, flashing |
| 3 | [Firmware Updates](#3-firmware-updates) | Capture chip and WCH serial chip firmware |
| 4 | [Device Diagnostics](#4-device-diagnostics) | Hardware test suite, support email export |
| 5 | [Device Selector](#5-device-selector) | Multi-device management, port chains |
| 6 | [Environment Setup Dialog](#6-environment-setup-dialog) | Driver checks, BrlTTY conflicts, udev rules |
| 7 | [TCP Server](#7-tcp-server-remote-control) | Remote control protocol, Python example |
| 8 | [Corner Widget Manager](#8-corner-widget-manager) | Status overlays on the video area |
| 9 | [Window Control Manager](#9-window-control-manager) | Fullscreen, always-on-top, geometry persistence |
| 10 | [Screen Scale Configuration](#10-screen-scale-configuration) | HiDPI scaling override |
| 11 | [Task Manager](#11-task-manager) | Deferred startup task queue |

---

## 1. Preferences System

The Settings dialog (accessible via **Settings → Preferences** or the gear icon) uses a tree-based navigation with a stacked widget for different configuration pages.

### Architecture

The settings dialog (`SettingDialog`) uses a `QTreeWidget` on the left and `QStackedWidget` on the right, with four pages:

### Video Page

Controls video capture behavior:
- **Resolution** — Preferred capture resolution
- **Frame rate** — Target FPS
- **Media backend** — Choose between FFmpeg, GStreamer, or Qt Multimedia
  - **FFmpeg** — Direct FFmpeg-based capture (recommended for most users)
  - **GStreamer** — GStreamer pipeline-based (available on Linux)
  - **Qt Multimedia** — Qt's native multimedia (Windows fallback)
- **Hardware acceleration** — VAAPI (Linux Intel), V4L2 (Linux) options when using FFmpeg
- **Custom input resolution** — Override the auto-detected target resolution

The settings are persisted via `GlobalSetting` (QSettings-based singleton) and loaded at application startup.

### Audio Page

Controls audio capture:
- **Input device** — Select the audio input source
- **Volume** — Capture volume level
- **Mute** — Toggle audio mute

### Target Control Page

Controls keyboard/mouse behavior toward the target:
- **Mouse mode** — Absolute or relative
- **Mouse auto-hide** — Automatically hide cursor in relative mode
- **Keyboard layout** — Physical keyboard mapping
- **Repeating keystroke interval** — How fast held keys repeat

### Log Page

Controls application logging:
- **Log level** — Debug, Info, Warning, Error
- **Log file path** — Where to write log files
- **Serial logging** — Separate serial communication log for diagnostics

---

## 2. EDID Management

### What is EDID?

EDID (Extended Display Identification Data) is a data structure that a display (or in this case, the capture device) sends to the source (target computer) to describe its capabilities — supported resolutions, refresh rates, vendor info, etc.

The Mini-KVM acts as a "fake monitor" to the target, so the EDID data determines what resolutions the target will output.

### EDID Features

Access: **Device → Update Display Settings**

The EDID system includes:

**EDID Processor** (`edidprocessor.cpp`):
- Parse and construct EDID data blocks
- Modify resolution descriptors
- Update checksums

**EDID Resolution Parser** (`edidresolutionparser.cpp`):
- Parse DDC/EDID timing descriptors
- Convert between human-readable resolutions and EDID timing codes

**EDID Utils** (`edidutils.cpp`):
- Validate EDID structures
- Calculate checksums
- Convert between format representations

**Firmware Utils** (`firmwareutils.cpp`):
- Read/write EDID from/to the capture chip's EEPROM
- Flash updated EDID to the device

### Custom Resolution Editing

You can:
1. View the current EDID data
2. Edit supported resolutions
3. Add custom resolution entries
4. Flash the updated EDID to the device's firmware

### Resolution Model

The `ResolutionModel` provides a Qt model for displaying and editing resolution entries in the UI dialog.

---

## 3. Firmware Updates

The application can update the firmware on the HDMI capture chip (MS2109/MS2109S/MS2130S).

### Firmware Check

The app periodically checks for firmware updates by:
1. Reading the current firmware version from the chip's EEPROM
2. Downloading the firmware index from the network (`FirmwareNetworkClient`)
3. Comparing versions to determine if an update is available

### Firmware Update Dialog

Access: **Device → Update Firmware**

The dialog shows:
- Current firmware version
- Latest available version
- Update progress bar
- Update status messages

### WCH Flash Tool

For the CH340/CH9329 serial chip firmware, there's a separate flashing tool:

Access: **Device → WCH Flash**

The `WCHFlashDialog` and `WCHFlashWorker` handle:
- Flashing firmware to the WCH serial chip
- Progress reporting from the worker thread
- Error handling and recovery

### Firmware Architecture

- **`FirmwareReader`** — Read firmware from EEPROM via HID commands
- **`FirmwareWriter`** — Write firmware to EEPROM in chunks with progress callbacks
- **`FirmwareNetworkClient`** — Download firmware index and firmware files from the network
- **`FirmwareOperationManager`** — Coordinates read/write operations

### EEPROM Operations

The VideoHid class provides direct EEPROM access:
- `readEeprom(address, size, progressCallback)` — Read raw bytes
- `writeEeprom(address, data, chunkCallback)` — Write raw bytes in chunks
- `loadFirmwareToEeprom()` — Flash firmware binary
- `loadEepromToFile(filePath)` — Backup EEPROM to file

---

## 4. Device Diagnostics

The diagnostics tool runs a suite of hardware tests to verify the Mini-KVM is functioning correctly.

### Access

Menu: **Device → Device Diagnostics**

### Test Suite

The `DiagnosticsManager` runs these tests sequentially:

1. **Serial Connection Test** — Verifies the serial port is accessible at various baudrates
2. **Target USB Status** — Checks if the target-side USB is connected and responsive
3. **Factory Reset Test** — Tests the factory reset functionality
4. **High Baudrate Test** — Tests communication at 115200 baud
5. **Low Baudrate Test** — Tests communication at 9600 baud
6. **Stress Test** — Sends rapid mouse/keyboard commands and measures success rate
7. **Target Plug & Play Test** — Detects unplugging and replugging of the target
8. **Host Plug & Play Test** — Detects host-side USB disconnect/reconnect

### Test Results

Each test shows:
- Status indicator (pending / running / passed / failed)
- Test log with detailed output
- Overall pass/fail summary

### Support Email Export

After running diagnostics, you can export the results via the **Support Email Dialog**:
- Collects all test logs
- Includes system information
- Opens your email client with a pre-filled support request

### Log Writer

The `LogWriter` runs on a dedicated thread and writes diagnostic logs to temporary files for export.

---

## 5. Device Selector

When multiple Mini-KVM devices are connected, the **Device Selector Dialog** helps you manage them.

### Features

- Lists all detected Openterface devices
- Shows device type (Mini-KVM, KVMGO, KVMVGA) based on VID/PID
- Allows manual device selection by port chain
- Shows connection status for each device

### Port Chains

A **port chain** is a string that uniquely identifies a device's physical USB topology position, e.g.:
```
usb-0000:00:14.0-1.2
```

This remains consistent across reboots (as long as the device is plugged into the same physical port), making it a reliable identifier.

---

## 6. Environment Setup Dialog

On first launch (or when drivers are missing), the application may show the **Environment Setup Dialog**.

This dialog checks for:
- Serial driver availability (CH340 on Linux/Windows)
- BrlTTY conflicts (Linux — BrlTTY can grab serial devices)
- USB permissions (Linux udev rules)
- Qt multimedia backend availability

You can skip this check with the `--skip-env-check` command-line flag.

---

## 7. TCP Server (Remote Control)

The application includes a built-in TCP server that allows remote control via a simple protocol.

### Starting the Server

Menu: **File → Start Server** (or configured to auto-start)

The server listens on port **12345** (defined in `mainwindow.h`).

### Protocol

See [tcp_protocol.md](../tcp_protocol.md) for the full protocol specification. Briefly:

- Send commands as text to the TCP port
- Commands include: screen capture, script execution, status check
- Responses include the requested data (images, status codes)

### Use Cases

- **Automated testing** — Scripts can capture screenshots and send keypresses
- **Remote management** — Control the KVM from another machine
- **CI/CD integration** — Use in automated test pipelines

### Python Example

```python
import socket
sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.connect(("localhost", 12345))
sock.send(b"CHECK_STATUS\n")
response = sock.recv(4096)
print(response.decode())
sock.close()
```

See `docs/socket_example.ipynb` for more examples.

---

## 8. Corner Widget Manager

The corner widget system overlays small informational widgets on the video area:
- Connection status indicator
- Recording indicator
- Device type badge

Managed by `CornerWidgetManager`, these widgets float above the video pane and reposition when the window is resized.

---

## 9. Window Control Manager

The `WindowControlManager` handles window-level operations:
- **Fullscreen toggle** — Maximize the video area, hide UI chrome
- **Always on top** — Keep the window above others
- **Window geometry persistence** — Remember window position and size between sessions

---

## 10. Screen Scale Configuration

For HiDPI displays, the **Screen Scale** dialog lets you adjust the application's scaling factor independently of the system DPI.

Access: **Settings → Screen Scale**

This is useful when the automatic DPI detection doesn't produce the desired result on mixed-DPI multi-monitor setups.

---

## 11. Task Manager

The `TaskManager` provides a deferred task execution system used during startup:
- Schedules non-blocking operations on a background queue
- Prevents UI freezes during device enumeration and initialization
- Used by the `MainWindowInitializer` for deferred camera setup and device scanning

---

## Next Steps

- **[Architecture Deep Dive →](04-architecture.md)** — How the code is organized, design patterns, data flow
- **[Development Guide →](05-development-guide.md)** — Building, testing, contributing
