# VideoHID Refactoring Plan

## Current State Analysis

### The God Class Problem

`VideoHid` is a single class spanning **~4300 lines** that handles all of the following:

| Responsibility | Lines (approx) |
|---|---|
| HID device lifecycle (open/close/transaction) | ~200 |
| Polling thread management | ~100 |
| Chip type detection (VID/PID parsing) | ~150 |
| Resolution / FPS / pixel clock reads | ~200 |
| GPIO / SPDIFOUT (USB switch) | ~100 |
| EEPROM read/write | ~200 |
| MS2130S flash operations (erase/burst write/read/GPIO) | ~900 |
| Firmware network fetch & version check | ~300 |
| Platform HID I/O (Windows + Linux `#ifdef`) | ~800 |
| Chip-specific protocol variants (`usbXdataRead4ByteMS*`) | ~600 |
| Hotplug integration | ~120 |

### Architecture Flaws

#### 1. Defeated Chip Abstraction (Circular Dependency)

`videohidchip.h` defines `VideoChip` / `Ms2109Chip` / `Ms2130sChip` as an abstraction, but the chip classes only delegate straight back to `VideoHid`:

```cpp
// videohidchip.cpp — today
QPair<QByteArray, bool> Ms2109Chip::read4Byte(quint16 address) {
    return m_owner->usbXdataRead4ByteMS2109(address); // calls back into VideoHid
}
```

The actual chip-specific protocol logic (`usbXdataRead4ByteMS2109`, `usbXdataRead4ByteMS2109S`, `usbXdataRead4ByteMS2130S`) remains inside **VideoHid**, making the abstraction purely cosmetic.

#### 2. Incomplete Platform Adapter

`PlatformHidAdapter` exists but VideoHid still owns platform-specific state directly:

```cpp
// videohid.h — today
#ifdef _WIN32
    HANDLE deviceHandle = INVALID_HANDLE_VALUE;
    std::wstring m_cachedDevicePath;
#elif __linux__
    int hidFd = -1;
    QString m_cachedDevicePath;
#endif
```

Platform `#ifdef` blocks also appear inside method bodies throughout `videohid.cpp`.

#### 3. Firmware Operations Leak into VideoHid

While `FirmwareWriter` / `FirmwareReader` / `FirmwareOperationManager` exist, VideoHid still directly owns:
- `loadFirmwareToEeprom()` — orchestration logic
- `fetchBinFileToString()` — network I/O with embedded `QEventLoop`
- `getLatestFirmwareFilenName()` — network I/O + filename parsing
- `isLatestFirmware()` — version comparison logic
- `pickFirmwareFileNameFromIndex()` — index parsing

#### 4. MS2130S Flash Driver Inside VideoHid

A complete SPI flash driver is embedded inside VideoHid:
- `ms2130sEraseSector` / `ms2130sFlashEraseDone`
- `ms2130sFlashBurstWrite` / `ms2130sFlashBurstRead`
- `ms2130sWriteFirmware`
- `ms2130sInitializeGPIO` / `ms2130sRestoreGPIO`
- `ms2130sDetectConnectMode`

These ~900 lines belong inside `Ms2130sChip`.

#### 5. Mixed Signals

`VideoHid` exposes 14+ signals mixing device lifecycle, firmware progress, and USB switch state — consumers must subscribe to a monolithic event source.

---

## Target Architecture

```
video/
├── transport/
│   ├── IHIDTransport.h          (interface: open/close/send/get feature report/path)
│   ├── WindowsHIDTransport.h/.cpp
│   └── LinuxHIDTransport.h/.cpp
│
├── chip/
│   ├── VideoChip.h              (abstract base, owns IHIDTransport*)
│   ├── Ms2109Chip.h/.cpp        (self-contained protocol + register reads)
│   ├── Ms2109sChip.h/.cpp       (self-contained protocol + register reads)
│   └── Ms2130sChip.h/.cpp       (self-contained protocol + flash driver)
│
├── detection/
│   └── ChipDetector.h/.cpp      (VID/PID path parsing → VideoChipType + create chip)
│
├── firmware/
│   ├── FirmwareNetworkClient.h/.cpp   (network fetch, version comparison, index parsing)
│   ├── FirmwareOperationManager.h/.cpp (existing, unchanged public API)
│   ├── FirmwareWriter.h/.cpp    (existing)
│   └── FirmwareReader.h/.cpp    (existing)
│
└── VideoHid.h/.cpp              (facade: lifecycle, USB switch, resolution query)
```

### Dependency Graph (After)

```
VideoHid
 ├─ IHIDTransport          ← owns/creates, passed to chip
 ├─ VideoChip              ← receives IHIDTransport ref, no VideoHid* back-ref
 ├─ ChipDetector           ← stateless utility
 ├─ FirmwareNetworkClient  ← async, emits signals
 └─ FirmwareOperationManager
```

No circular dependencies. `VideoChip` no longer holds a pointer to `VideoHid`.

---

## Refactoring Phases

---

### Phase 1 — Complete the Chip Abstraction

**Goal**: Move all chip-specific protocol logic INTO the chip classes. Eliminate `usbXdataRead4ByteMS2109/S/2130S` from VideoHid.

#### Changes

**`IHIDTransport` (new interface)**

```cpp
// video/transport/IHIDTransport.h
class IHIDTransport {
public:
    virtual ~IHIDTransport() = default;
    virtual bool isOpen() const = 0;
    virtual bool sendFeatureReport(uint8_t* buf, size_t len) = 0;
    virtual bool getFeatureReport(uint8_t* buf, size_t len) = 0;
};
```

**`VideoChip` base (modified)**

```cpp
// Remove: VideoHid* m_owner
// Add:    IHIDTransport* m_transport
class VideoChip {
protected:
    IHIDTransport* m_transport{nullptr};
public:
    explicit VideoChip(IHIDTransport* transport) : m_transport(transport) {}
    ...
    virtual QPair<QByteArray, bool> read4Byte(quint16 address) = 0;
    virtual bool write4Byte(quint16 address, const QByteArray& data) = 0;
};
```

**`Ms2109Chip` (modified)**

Move the full body of `VideoHid::usbXdataRead4ByteMS2109` into `Ms2109Chip::read4Byte`. The method uses only `sendFeatureReport` / `getFeatureReport` — both available via `m_transport`.

**`Ms2109sChip`** — same treatment for the multi-strategy read logic.

**`Ms2130sChip`** — same treatment PLUS move all `ms2130sXxx` flash methods:
- `eraseSector(quint32 addr)`
- `flashEraseDone(bool& done)`
- `flashBurstWrite(quint32 addr, const QByteArray& data)`
- `flashBurstRead(quint32 addr, quint32 len, QByteArray& out)`
- `writeFirmware(quint16 addr, const QByteArray& data)`
- `initializeGPIO()` / `restoreGPIO()`
- `detectConnectMode()`

**`VideoHid::usbXdataRead4Byte` (simplified)**

```cpp
QPair<QByteArray, bool> VideoHid::usbXdataRead4Byte(quint16 addr) {
    if (!m_chipImpl) return {QByteArray(4, 0), false};
    return m_chipImpl->read4Byte(addr);
}
```

**Files to create**: `video/transport/IHIDTransport.h`  
**Files to modify**: `videohidchip.h`, `videohidchip.cpp`, `videohid.h`, `videohid.cpp`  
**Lines removed from VideoHid**: ~600 (chip-specific read methods) + ~900 (flash methods) = **~1500 lines**

---

### Phase 2 — Complete the Platform Transport Extraction

**Goal**: Eliminate all platform `#ifdef` branches from `VideoHid`. All device-handle state moves behind `IHIDTransport`.

#### Changes

**Rename `PlatformHidAdapter` → `IHIDTransport`** (or make it implement the interface)

Move ALL platform-specific state into the concrete transport:

```cpp
// video/transport/WindowsHIDTransport.h
class WindowsHIDTransport : public IHIDTransport {
    HANDLE m_handle{INVALID_HANDLE_VALUE};
    std::wstring m_cachedPath;
public:
    bool open() override;
    void close() override;
    bool sendFeatureReport(uint8_t* buf, size_t len) override;
    bool getFeatureReport(uint8_t* buf, size_t len) override;
    QString getHIDDevicePath();
    // Windows-specific: getProperDevicePath, enumeration logic
};
```

Remove from `VideoHid`:
- `#ifdef _WIN32 HANDLE deviceHandle` / `#elif __linux__ int hidFd`
- `openHIDDeviceHandle()` / `closeHIDDeviceHandle()`
- `platform_openDevice()` / `platform_closeDevice()`
- `platform_sendFeatureReport()` / `platform_getFeatureReport()`
- `sendFeatureReportWindows()` / `getFeatureReportWindows()`
- `sendFeatureReportLinux()` / `getFeatureReportLinux()`
- `getHIDDevicePath()` (platform-branched)
- The ~780-line Windows HID enumeration block (`#ifdef _WIN32 ... #elif __linux__`)

**Files to create**: `video/transport/WindowsHIDTransport.h/.cpp`, `video/transport/LinuxHIDTransport.h/.cpp`  
**Lines removed from VideoHid**: ~800

---

### Phase 3 — Extract ChipDetector

**Goal**: `detectChipType()` becomes a standalone stateless utility class.

#### Changes

```cpp
// video/detection/ChipDetector.h
class ChipDetector {
public:
    // Returns the chip type and creates the appropriate VideoChip instance
    static VideoChipType detect(const QString& devicePath, const QString& portChain);
    static std::unique_ptr<VideoChip> createChip(VideoChipType type, IHIDTransport* transport);
};
```

The VID/PID parsing logic (`#ifdef _WIN32` / `#elif __linux__` detection branches) moves entirely into `ChipDetector`. `VideoHid::detectChipType()` becomes a 5-line call:

```cpp
void VideoHid::detectChipType() {
    m_chipType = ChipDetector::detect(m_currentHIDDevicePath, m_currentHIDPortChain);
    m_chipImpl = ChipDetector::createChip(m_chipType, m_transport.get());
}
```

**Files to create**: `video/detection/ChipDetector.h/.cpp`  
**Lines removed from VideoHid**: ~150

---

### Phase 4 — Extract FirmwareNetworkClient

**Goal**: All network I/O and firmware version logic leaves `VideoHid`.

#### Changes

```cpp
// video/firmware/FirmwareNetworkClient.h
class FirmwareNetworkClient : public QObject {
    Q_OBJECT
public:
    void fetchLatestFirmware(const QString& indexUrl, VideoChipType chip, int timeoutMs = 5000);
    QString pickFirmwareFileNameFromIndex(const QString& content, VideoChipType chip); // moved from VideoHid

signals:
    void firmwareInfoReady(const QString& filename, const std::string& version);
    void firmwareDataReady(const QByteArray& data, const std::string& version);
    void fetchFailed(FirmwareResult result);
};
```

Move from `VideoHid`:
- `fetchBinFileToString()` → `FirmwareNetworkClient::fetchData()`
- `getLatestFirmwareFilenName()` → `FirmwareNetworkClient::fetchFilename()`
- `pickFirmwareFileNameFromIndex()` → `FirmwareNetworkClient::pickFirmwareFileNameFromIndex()`
- `isLatestFirmware()` → `FirmwareNetworkClient::compareVersions()`
- `m_firmwareVersion` / `m_currentfirmwareVersion` → moved to manager

**Files to create**: `video/firmware/FirmwareNetworkClient.h/.cpp`  
**Lines removed from VideoHid**: ~300

---

### Phase 5 — Shrink VideoHid to a Facade

After the above phases, `VideoHid` retains only:

| Responsibility | Target lines |
|---|---|
| Singleton + construction/destruction | 30 |
| `start()` / `stop()` / `stopPollingOnly()` | 60 |
| `beginTransaction()` / `endTransaction()` | 50 |
| `pollDeviceStatus()` + `PollingThread` | 80 |
| `switchToHost()` / `switchToTarget()` / USB GPIO | 60 |
| `getResolution()` / `getFps()` / `getPixelclk()` | 40 |
| `isHdmiConnected()` | 20 |
| `getFirmwareVersion()` | 30 |
| Hotplug connection/disconnection handlers | 80 |
| `findMatchingHIDDevice()` / `switchToHIDDeviceByPortChain()` | 100 |
| **Total** | **~550** |

**Target reduction: ~4300 → ~550 lines** (87% smaller).

---

### Phase 6 — Clean Up Signals and Public API

Split the monolithic `VideoHid` signal set into per-component signals emitted locally and aggregated at the top level only when needed:

| Signal group | Owner |
|---|---|
| `firmwareWriteProgress`, `firmwareWriteComplete` | `FirmwareOperationManager` |
| `firmwareReadProgress`, `firmwareReadComplete` | `FirmwareOperationManager` |
| `firmwareWriteError`, `firmwareReadError` | `FirmwareOperationManager` |
| `hidDeviceConnected`, `hidDeviceDisconnected` | `VideoHid` (from hotplug) |
| `inputResolutionChanged`, `resolutionChangeUpdate` | `VideoHid` (from poller) |
| `gpio0StatusChanged` | `VideoHid` (from poller) |

Remove `firmwareWriteChunkComplete` / `firmwareReadChunkComplete` internal signals (implementation detail, not public API).

---

## Implementation Order & Risk

| Phase | Est. Scope | Risk | Breaks ABI? |
|---|---|---|---|
| 1 — Chip abstraction | Large | Medium | No — same public API |
| 2 — Transport extraction | Large | Low | No |
| 3 — ChipDetector | Small | Low | No |
| 4 — FirmwareNetworkClient | Medium | Low | No |
| 5 — Shrink VideoHid | — | Low | No |
| 6 — Signal cleanup | Small | Medium | Yes — callers updated |

**Phase 1 must come before Phase 2** because Phase 1 defines `IHIDTransport` which Phase 2 implements.  
Phases 3 and 4 are independent and can run in parallel.  
Phase 5 is a cleanup pass after the previous phases.

---

## Files Overview

### New Files

| File | Purpose |
|---|---|
| `video/transport/IHIDTransport.h` | Pure abstract interface for HID I/O |
| `video/transport/WindowsHIDTransport.h/.cpp` | Windows HANDLE-based implementation |
| `video/transport/LinuxHIDTransport.h/.cpp` | Linux fd-based implementation |
| `video/detection/ChipDetector.h/.cpp` | VID/PID parsing and chip factory |
| `video/firmware/FirmwareNetworkClient.h/.cpp` | Network fetch + version logic |

### Modified Files

| File | Change |
|---|---|
| `video/videohid.h` | Remove platform members, ms2130s methods, chip-specific read decls |
| `video/videohid.cpp` | Remove ~3700 lines, delegate to components |
| `video/videohidchip.h` | Change `VideoChip(VideoHid*)` → `VideoChip(IHIDTransport*)` |
| `video/videohidchip.cpp` | Add full read/write implementations per chip |
| `video/platformhidadapter.h/.cpp` | Merge into `IHIDTransport` or delete |
| `video/ms2130s.h` | Add flash command constants if missing |
| `cmake/SourceFiles.cmake` | Register new source files |

### Deleted Files (after extraction)

| File | Reason |
|---|---|
| `video/platformhidadapter.h/.cpp` | Superseded by `IHIDTransport` + concrete transports |

---

## Chip Method Mapping (Before → After)

| Current location | Method | Target location |
|---|---|---|
| `VideoHid` | `usbXdataRead4ByteMS2109` | `Ms2109Chip::read4Byte` |
| `VideoHid` | `usbXdataRead4ByteMS2109S` | `Ms2109sChip::read4Byte` |
| `VideoHid` | `usbXdataRead4ByteMS2130S` | `Ms2130sChip::read4Byte` |
| `VideoHid` | `usbXdataWrite4Byte` | `VideoChip::write4Byte` (base impl) |
| `VideoHid` | `ms2130sEraseSector` | `Ms2130sChip::eraseSector` |
| `VideoHid` | `ms2130sFlashEraseDone` | `Ms2130sChip::flashEraseDone` |
| `VideoHid` | `ms2130sFlashBurstWrite` | `Ms2130sChip::flashBurstWrite` |
| `VideoHid` | `ms2130sFlashBurstRead` | `Ms2130sChip::flashBurstRead` |
| `VideoHid` | `ms2130sWriteFirmware` | `Ms2130sChip::writeFirmware` |
| `VideoHid` | `ms2130sInitializeGPIO` | `Ms2130sChip::initializeGPIO` |
| `VideoHid` | `ms2130sRestoreGPIO` | `Ms2130sChip::restoreGPIO` |
| `VideoHid` | `ms2130sDetectConnectMode` | `Ms2130sChip::detectConnectMode` |
| `VideoHid` | `detectChipType` | `ChipDetector::detect` |
| `VideoHid` | `fetchBinFileToString` | `FirmwareNetworkClient::fetchData` |
| `VideoHid` | `getLatestFirmwareFilenName` | `FirmwareNetworkClient::fetchFilename` |
| `VideoHid` | `pickFirmwareFileNameFromIndex` | `FirmwareNetworkClient::pickFilename` |
| `VideoHid` | `isLatestFirmware` | `FirmwareNetworkClient::compareVersions` |
| `VideoHid` | Windows/Linux `#ifdef` blocks | `WindowsHIDTransport` / `LinuxHIDTransport` |

---

## Testing Strategy

1. **Unit tests per chip class**: Each `Ms*Chip::read4Byte` can be tested with a mock `IHIDTransport` that returns pre-recorded byte sequences.
2. **`ChipDetector` unit tests**: Feed device path strings from Windows and Linux and assert the correct `VideoChipType`.
3. **`FirmwareNetworkClient` unit tests**: Use mock HTTP responses to verify `pickFirmwareFileNameFromIndex` fallback behaviour (already partially tested in existing code).
4. **Integration test via `VideoHid`**: Connect a real device and verify resolution, FPS, and switch state are reported correctly — identical behaviour before and after refactoring.

---

## Notes

- The public API of `VideoHid` (as seen by `ui/` and other callers) should **not change** during this refactoring, except for Phase 6 signal consolidation.
- `FirmwareWriter` and `FirmwareReader` currently call into `VideoHid` via the `friend` mechanism. After Phase 1 they should receive an `Ms2130sChip*` or `VideoChip*` directly, removing the need for `friend class FirmwareWriter`.
- The `m_flashInProgress` atomic flag that guards register reads during flashing should move into `Ms2130sChip` and be checked inside the chip's `read4Byte`, not in `VideoHid::usbXdataRead4Byte`.
