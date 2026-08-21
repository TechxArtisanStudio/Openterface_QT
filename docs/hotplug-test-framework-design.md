# Hotplug Test Framework Design

## Background

The Openterface application crashed on Windows 11 (issue introduced between v0.5.26 and v0.5.29). Analysis of the 24,555-line crash log revealed a USB device transient disconnect/reconnect cycle that the app failed to recover from cleanly. The root cause was traced to a serial port deletion race condition in the refactored `SerialPortManager.cpp` (801 lines changed) and a stalled FFmpeg DirectShow capture thread.

This design proposes a **cross-platform hotplug test framework** to prevent regressions and validate device hotplug behavior without requiring physical hardware.

## Current Architecture

### Hotplug Detection Flow

All hotplug detection is **timer-driven polling** (not OS push-based events):

```
HotplugMonitor (timer)
    └── DeviceManager (singleton)
            └── AbstractPlatformDeviceManager
                    ├── WindowsDeviceManager (SetupAPI)
                    ├── LinuxDeviceManager (sysfs)
                    └── (macOS placeholder)
```

### Key Components

| Component | File | Responsibility |
|-----------|------|----------------|
| `HotplugMonitor` | `device/HotplugMonitor.{h,cpp}` | Timer-driven polling, diff detection, emits `newDevicePluggedIn` / `deviceUnplugged` |
| `HotplugDebounceManager` | `device/HotplugDebounceManager.{h,cpp}` | State machine (Stable -> Removing -> Removed -> Inserting -> Inserted -> Stable), fast-scan mode (300ms for 60s after removal), rapid-reconnect detection |
| `DeviceManager` | `device/DeviceManager.{h,cpp}` | Singleton, owns `HotplugMonitor` and `HotplugDebounceManager`, delegates discovery to `AbstractPlatformDeviceManager` |
| `SerialHotplugHandler` | `serial/serial_hotplug_handler.{h,cpp}` | Serial-specific debounce (1000ms window + 1500ms stabilization), emits `SerialPortUnplugged` and `AutoConnectRequested` |
| `FFmpegHotplugHandler` | `host/backend/ffmpeg/ffmpeg_hotplug_handler.{h,cpp}` | Video capture hotplug, dependency injection via `FFmpegDeviceValidator*`, emits `DeviceActivated` / `RequestStartCapture` |
| `AbstractPlatformDeviceManager` | `device/platform/AbstractPlatformDeviceManager.{h,cpp}` | Pure virtual `discoverDevices()` + `getPlatformName()`, holds shared VID/PID constants |

### Existing Test Infrastructure

**None.** The project has no C++ unit test framework. The `tests/` directory contains only shell scripts, Python scripts, and markdown documentation.

### Testability Assessment

**Good seams that exist:**
1. `HotplugMonitor::checkForChanges()` - public method, documented as "Manual trigger for testing"
2. `HotplugMonitor::addCallback(std::function<...>)` - easy synchronous observation
3. `HotplugDebounceManager` - fully self-contained QObject, no external dependencies, exposes `getDeviceState()`, `isFastScanning()`, `getCurrentPollInterval()`, `resetAllStates()`
4. `FFmpegHotplugHandler` - takes `FFmpegDeviceValidator*` (dependency injection)
5. `AbstractPlatformDeviceManager` - clean interface for faking device discovery

**Gaps to address:**
1. `DeviceManager` is a hard singleton (`getInstance()` returns static local) - tests need a workaround
2. `HotplugMonitor` constructor takes `DeviceManager*` directly instead of an abstract interface
3. No test framework is set up in the build system

## Design

### Core Strategy: Mock the Platform Layer

Since all hotplug detection flows through `AbstractPlatformDeviceManager::discoverDevices()`, we can inject a mock that returns configurable device lists. This mock works on **any platform** (Windows, macOS, Linux) without calling real OS APIs.

```
Test Code
    └── MockDeviceDiscovery (returns fake device lists)
            └── HotplugMonitor (unchanged logic, just different discovery source)
                    └── Signals (newDevicePluggedIn, deviceUnplugged, etc.)
                            └── Test assertions (QVERIFY, QCOMPARE, QSignalSpy)
```

### Minimal Refactoring for Testability

Two small changes are required:

**Change 1: Extract device discovery interface**

Create a new file `device/IDeviceDiscovery.h`:

```cpp
#ifndef IDEVICEDISCOVERY_H
#define IDEVICEDISCOVERY_H

#include <QList>
#include "DeviceInfo.h"

/**
 * @brief Abstract interface for device discovery.
 *
 * Decouples HotplugMonitor from the concrete DeviceManager implementation,
 * enabling cross-platform unit testing with mock device lists.
 */
class IDeviceDiscovery {
public:
    virtual ~IDeviceDiscovery() = default;
    virtual QList<DeviceInfo> discoverDevices() = 0;
};

#endif // IDEVICEDISCOVERY_H
```

`DeviceManager` already implements `discoverDevices()`, so it just needs to inherit this interface:

```cpp
// DeviceManager.h
class DeviceManager : public QObject, public IDeviceDiscovery {
    // ... existing code unchanged
};
```

**Change 2: Update HotplugMonitor constructor**

```cpp
// HotplugMonitor.h - change parameter type
explicit HotplugMonitor(IDeviceDiscovery* deviceDiscovery, QObject *parent = nullptr);

// HotplugMonitor.cpp - change member type
HotplugMonitor::HotplugMonitor(IDeviceDiscovery* deviceDiscovery, QObject *parent)
    : QObject(parent)
    , m_deviceDiscovery(deviceDiscovery)  // type changed to IDeviceDiscovery*
```

All existing call sites remain compatible because `DeviceManager` implements `IDeviceDiscovery`.

### Mock Device Discovery (Cross-Platform Core)

```cpp
// tests/hotplug/mock/MockDeviceDiscovery.h
#ifndef MOCK_DEVICE_DISCOVERY_H
#define MOCK_DEVICE_DISCOVERY_H

#include "device/IDeviceDiscovery.h"
#include <QList>

/**
 * @brief Mock device discovery for cross-platform testing.
 *
 * Returns configurable device lists on any platform (Windows/macOS/Linux),
 * simulating USB device plug/unplug without real hardware.
 */
class MockDeviceDiscovery : public IDeviceDiscovery {
public:
    QList<DeviceInfo> discoverDevices() override {
        m_discoveryCallCount++;
        return m_devices;
    }

    // Test helper methods

    void setDevices(const QList<DeviceInfo>& devices) {
        m_devices = devices;
    }

    void plugInDevice(const DeviceInfo& device) {
        m_devices.append(device);
    }

    void unplugDevice(const QString& portChain) {
        m_devices.erase(
            std::remove_if(m_devices.begin(), m_devices.end(),
                [&](const DeviceInfo& d) { return d.portChain == portChain; }),
            m_devices.end()
        );
    }

    void unplugAll() {
        m_devices.clear();
    }

    int discoveryCallCount() const { return m_discoveryCallCount; }
    void resetCallCount() { m_discoveryCallCount = 0; }

private:
    QList<DeviceInfo> m_devices;
    int m_discoveryCallCount = 0;
};

#endif // MOCK_DEVICE_DISCOVERY_H
```

### Test Device Builder

```cpp
// tests/hotplug/mock/TestDeviceBuilder.h
#ifndef TEST_DEVICE_BUILDER_H
#define TEST_DEVICE_BUILDER_H

#include "device/DeviceInfo.h"

/**
 * @brief Factory methods for creating test DeviceInfo objects with sensible defaults.
 */
class TestDeviceBuilder {
public:
    static DeviceInfo createGen1Device(const QString& portChain = "0002-0001") {
        DeviceInfo d;
        d.portChain = portChain;
        d.serialPortName = "COM5";
        d.serialVid = "534D";
        d.serialPid = "2109";
        d.hasSerialDevice = true;
        d.hasCameraDevice = true;
        d.hasHidDevice = true;
        d.cameraDeviceId = "usb_openterface_" + portChain;
        d.hidDevicePath = "\\\\.\\hid#" + portChain;
        return d;
    }

    static DeviceInfo createGen2Device(const QString& portChain = "0002-0001") {
        DeviceInfo d = createGen1Device(portChain);
        d.serialVid = "1A86";
        d.serialPid = "FE0C";
        return d;
    }

    static DeviceInfo createGen3Device(const QString& portChain = "0002-0001") {
        DeviceInfo d = createGen1Device(portChain);
        d.serialVid = "345F";
        d.serialPid = "2132";
        return d;
    }

    static DeviceInfo createUnrelatedDevice(const QString& portChain = "0001-0002") {
        DeviceInfo d;
        d.portChain = portChain;
        d.serialVid = "046D";  // Logitech
        d.serialPid = "C52B";
        d.hasSerialDevice = true;
        return d;
    }
};

#endif // TEST_DEVICE_BUILDER_H
```

## Test Cases

### Layer 1: HotplugDebounceManager Unit Tests

Fully cross-platform, zero platform dependencies. Tests the state machine, fast-scan mode, and rapid-reconnect detection.

```cpp
// tests/hotplug/test_hotplug_debounce.cpp
#include <QTest>
#include <QSignalSpy>
#include "device/HotplugDebounceManager.h"

class TestHotplugDebounce : public QObject {
    Q_OBJECT

private slots:
    void testInitialState() {
        device::HotplugDebounceManager manager;
        QVERIFY(!manager.isFastScanning());
        QCOMPARE(manager.getCurrentPollInterval(),
                 device::HotplugDebounceManager::NORMAL_POLL_INTERVAL_MS);
    }

    void testDeviceRemovedTransitionsState() {
        device::HotplugDebounceManager manager;

        manager.handleDeviceRemoved("dev1", "/dev/ttyUSB0");

        auto state = manager.getDeviceState("dev1");
        QVERIFY(state == device::DeviceState::Removing ||
                state == device::DeviceState::Removed);
    }

    void testRapidReconnect() {
        device::HotplugDebounceManager manager;
        QSignalSpy reconnectSpy(&manager,
            &device::HotplugDebounceManager::deviceRapidlyReconnected);

        // 1. Device connected
        manager.handleDeviceAdded("dev1", "/dev/ttyUSB0");

        // 2. Device disconnected -> triggers fast scan
        manager.handleDeviceRemoved("dev1", "/dev/ttyUSB0");
        QVERIFY(manager.isFastScanning());

        // 3. Re-inserted within fast-scan window -> rapid reconnect
        bool isRapidReconnect = manager.handleDeviceAdded("dev1", "/dev/ttyUSB0");
        QVERIFY(isRapidReconnect);
        QVERIFY(reconnectSpy.count() > 0);
    }

    void testNewDeviceNotRapidReconnect() {
        device::HotplugDebounceManager manager;

        // Disconnect a device to trigger fast scan
        manager.handleDeviceRemoved("dev1", "/dev/ttyUSB0");

        // Insert a different device -> should be "new device", not "rapid reconnect"
        bool isRapidReconnect = manager.handleDeviceAdded("dev2", "/dev/ttyUSB1");
        QVERIFY(!isRapidReconnect);
    }

    void testResetAllStates() {
        device::HotplugDebounceManager manager;

        manager.handleDeviceRemoved("dev1", "/dev/ttyUSB0");
        QVERIFY(manager.isFastScanning());

        manager.resetAllStates();
        QVERIFY(!manager.isFastScanning());
        QCOMPARE(manager.getDeviceState("dev1"), device::DeviceState::Stable);
    }
};

QTEST_MAIN(TestHotplugDebounce)
#include "test_hotplug_debounce.moc"
```

### Layer 2: HotplugMonitor Unit Tests

Cross-platform via `MockDeviceDiscovery`. Tests event emission, deduplication, and change detection.

```cpp
// tests/hotplug/test_hotplug_monitor.cpp
#include <QTest>
#include <QSignalSpy>
#include "device/HotplugMonitor.h"
#include "mock/MockDeviceDiscovery.h"
#include "mock/TestDeviceBuilder.h"

class TestHotplugMonitor : public QObject {
    Q_OBJECT

private slots:
    void testNoDevicesNoEvents() {
        MockDeviceDiscovery mock;
        HotplugMonitor monitor(&mock);

        QSignalSpy addedSpy(&monitor, &HotplugMonitor::newDevicePluggedIn);
        QSignalSpy removedSpy(&monitor, &HotplugMonitor::deviceUnplugged);

        monitor.checkForChanges();

        QCOMPARE(addedSpy.count(), 0);
        QCOMPARE(removedSpy.count(), 0);
    }

    void testDevicePluggedIn() {
        MockDeviceDiscovery mock;
        HotplugMonitor monitor(&mock);
        QSignalSpy spy(&monitor, &HotplugMonitor::newDevicePluggedIn);

        mock.plugInDevice(TestDeviceBuilder::createGen1Device("0002-0001"));
        monitor.checkForChanges();

        QCOMPARE(spy.count(), 1);
        auto device = spy.first().first().value<DeviceInfo>();
        QCOMPARE(device.portChain, QString("0002-0001"));
    }

    void testDeviceUnplugged() {
        MockDeviceDiscovery mock;
        HotplugMonitor monitor(&mock);

        // Plug in first
        mock.plugInDevice(TestDeviceBuilder::createGen1Device("0002-0001"));
        monitor.checkForChanges();

        QSignalSpy spy(&monitor, &HotplugMonitor::deviceUnplugged);

        // Unplug
        mock.unplugDevice("0002-0001");
        monitor.checkForChanges();

        QCOMPARE(spy.count(), 1);
    }

    void testNoDuplicateEvents() {
        MockDeviceDiscovery mock;
        HotplugMonitor monitor(&mock);
        QSignalSpy spy(&monitor, &HotplugMonitor::newDevicePluggedIn);

        mock.plugInDevice(TestDeviceBuilder::createGen1Device());

        monitor.checkForChanges();
        monitor.checkForChanges();  // Second call should not re-trigger
        monitor.checkForChanges();

        QCOMPARE(spy.count(), 1);
    }

    /**
     * @brief Simulates the crash scenario: USB transient disconnect then reconnect.
     *
     * Timeline: device online -> disconnect -> reconnect -> app should recover cleanly.
     */
    void testUsbTransientDisconnect() {
        MockDeviceDiscovery mock;
        HotplugMonitor monitor(&mock);

        QSignalSpy addedSpy(&monitor, &HotplugMonitor::newDevicePluggedIn);
        QSignalSpy removedSpy(&monitor, &HotplugMonitor::deviceUnplugged);

        // 1. Device online
        mock.plugInDevice(TestDeviceBuilder::createGen1Device("0002-0001"));
        monitor.checkForChanges();
        QCOMPARE(addedSpy.count(), 1);
        QCOMPARE(removedSpy.count(), 0);

        // 2. USB transient disconnect
        mock.unplugAll();
        monitor.checkForChanges();
        QCOMPARE(removedSpy.count(), 1);

        // 3. Device reconnects
        mock.plugInDevice(TestDeviceBuilder::createGen1Device("0002-0001"));
        monitor.checkForChanges();
        QCOMPARE(addedSpy.count(), 2);  // Second plug-in
    }

    void testMultipleDevices() {
        MockDeviceDiscovery mock;
        HotplugMonitor monitor(&mock);
        QSignalSpy addedSpy(&monitor, &HotplugMonitor::newDevicePluggedIn);

        mock.plugInDevice(TestDeviceBuilder::createGen1Device("0002-0001"));
        mock.plugInDevice(TestDeviceBuilder::createGen2Device("0003-0001"));
        mock.plugInDevice(TestDeviceBuilder::createGen3Device("0004-0001"));

        monitor.checkForChanges();
        QCOMPARE(addedSpy.count(), 3);
    }
};

QTEST_MAIN(TestHotplugMonitor)
#include "test_hotplug_monitor.moc"
```

### Layer 3: End-to-End Scenario Tests

Simulates complete hotplug lifecycles and stress conditions. These tests replicate the crash scenario from the log analysis.

```cpp
// tests/hotplug/test_hotplug_scenarios.cpp
#include <QTest>
#include <QSignalSpy>
#include "device/HotplugMonitor.h"
#include "device/HotplugDebounceManager.h"
#include "mock/MockDeviceDiscovery.h"
#include "mock/TestDeviceBuilder.h"

/**
 * @brief End-to-end hotplug scenario tests.
 *
 * Validates the complete hotplug lifecycle across components.
 * All tests run on any platform (Windows/macOS/Linux).
 */
class TestHotplugScenarios : public QObject {
    Q_OBJECT

private slots:
    /**
     * @brief Stress test: repeated plug/unplug cycles.
     *
     * Validates: no crash, no memory leak, correct state reset.
     */
    void testRepeatedPlugUnplug() {
        MockDeviceDiscovery mock;
        HotplugMonitor monitor(&mock);
        device::HotplugDebounceManager debounce;

        QObject::connect(&monitor, &HotplugMonitor::deviceUnplugged,
            &debounce, &device::HotplugDebounceManager::handleDeviceRemoved);

        int addCount = 0, removeCount = 0;
        QObject::connect(&monitor, &HotplugMonitor::newDevicePluggedIn,
            [&addCount](const DeviceInfo&) { addCount++; });
        QObject::connect(&monitor, &HotplugMonitor::deviceUnplugged,
            [&removeCount](const DeviceInfo&) { removeCount++; });

        // Repeated plug/unplug 50 times
        for (int i = 0; i < 50; i++) {
            mock.plugInDevice(TestDeviceBuilder::createGen1Device("0002-0001"));
            monitor.checkForChanges();

            mock.unplugAll();
            monitor.checkForChanges();
        }

        QCOMPARE(addCount, 50);
        QCOMPARE(removeCount, 50);

        debounce.resetAllStates();
        QVERIFY(!debounce.isFastScanning());
    }

    /**
     * @brief Reproduces the crash log timeline.
     *
     * Log timeline:
     *   09:54:07 - device disconnect -> serial error -> serial port deleted
     *   09:54:09 - device reconnect
     *   09:54:22 - keyboard layouts reloaded
     */
    void testCrashScenario_TransientDisconnect() {
        MockDeviceDiscovery mock;
        HotplugMonitor monitor(&mock);

        QSignalSpy addedSpy(&monitor, &HotplugMonitor::newDevicePluggedIn);
        QSignalSpy removedSpy(&monitor, &HotplugMonitor::deviceUnplugged);

        // Initial: device online
        mock.plugInDevice(TestDeviceBuilder::createGen1Device("0002-0001"));
        monitor.checkForChanges();

        // T=0s: USB disconnect (log: 09:54:07)
        mock.unplugAll();
        monitor.checkForChanges();
        QCOMPARE(removedSpy.count(), 1);

        // T=2s: device reconnect (log: 09:54:09)
        mock.plugInDevice(TestDeviceBuilder::createGen1Device("0002-0001"));
        monitor.checkForChanges();
        QCOMPARE(addedSpy.count(), 2);

        // Verify device info consistency
        auto firstDevice = addedSpy.at(0).first().value<DeviceInfo>();
        auto secondDevice = addedSpy.at(1).first().value<DeviceInfo>();
        QCOMPARE(firstDevice.portChain, secondDevice.portChain);
        QCOMPARE(firstDevice.serialVid, secondDevice.serialVid);
    }

    void testEmptyDeviceList() {
        MockDeviceDiscovery mock;
        HotplugMonitor monitor(&mock);

        for (int i = 0; i < 100; i++) {
            monitor.checkForChanges();
        }

        QVERIFY(true);  // Pass if no crash
    }

    void testMixedDevices() {
        MockDeviceDiscovery mock;
        HotplugMonitor monitor(&mock);
        QSignalSpy spy(&monitor, &HotplugMonitor::newDevicePluggedIn);

        mock.plugInDevice(TestDeviceBuilder::createGen1Device("0002-0001"));
        mock.plugInDevice(TestDeviceBuilder::createUnrelatedDevice("0001-0002"));

        monitor.checkForChanges();

        // HotplugMonitor does not filter by VID/PID (filtering is in discoverer layer)
        QCOMPARE(spy.count(), 2);
    }
};

QTEST_MAIN(TestHotplugScenarios)
#include "test_hotplug_scenarios.moc"
```

## Build System Integration

```cmake
# tests/CMakeLists.txt
cmake_minimum_required(VERSION 3.16)
project(openterface-tests LANGUAGES CXX)

find_package(Qt6 REQUIRED COMPONENTS Core Test Concurrent)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

set(PROJECT_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

include_directories(
    ${PROJECT_ROOT}
    ${PROJECT_ROOT}/device
    ${PROJECT_ROOT}/device/platform
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Shared sources under test
set(HOTPLUG_SOURCES
    ${PROJECT_ROOT}/device/HotplugMonitor.cpp
    ${PROJECT_ROOT}/device/HotplugDebounceManager.cpp
    ${PROJECT_ROOT}/device/DeviceInfo.cpp
)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/hotplug/mock)

# Test 1: HotplugDebounceManager
add_executable(test_hotplug_debounce
    hotplug/test_hotplug_debounce.cpp
    ${PROJECT_ROOT}/device/HotplugDebounceManager.cpp
)
target_link_libraries(test_hotplug_debounce PRIVATE Qt6::Core Qt6::Test)
add_test(NAME HotplugDebounce COMMAND test_hotplug_debounce)

# Test 2: HotplugMonitor
add_executable(test_hotplug_monitor
    hotplug/test_hotplug_monitor.cpp
    ${HOTPLUG_SOURCES}
)
target_link_libraries(test_hotplug_monitor PRIVATE Qt6::Core Qt6::Test Qt6::Concurrent)
add_test(NAME HotplugMonitor COMMAND test_hotplug_monitor)

# Test 3: End-to-end scenarios
add_executable(test_hotplug_scenarios
    hotplug/test_hotplug_scenarios.cpp
    ${HOTPLUG_SOURCES}
)
target_link_libraries(test_hotplug_scenarios PRIVATE Qt6::Core Qt6::Test Qt6::Concurrent)
add_test(NAME HotplugScenarios COMMAND test_hotplug_scenarios)

enable_testing()
```

## Directory Structure

```
tests/
├── CMakeLists.txt
└── hotplug/
    ├── mock/
    │   ├── MockDeviceDiscovery.h      # Mock device discovery (cross-platform core)
    │   └── TestDeviceBuilder.h        # Test device factory methods
    ├── test_hotplug_debounce.cpp      # Debounce manager unit tests
    ├── test_hotplug_monitor.cpp       # Monitor unit tests
    └── test_hotplug_scenarios.cpp     # End-to-end scenario tests
```

## Cross-Platform Compatibility

| Component | Windows | macOS | Linux |
|-----------|---------|-------|-------|
| HotplugDebounceManager tests | Yes | Yes | Yes |
| HotplugMonitor tests (Mock) | Yes | Yes | Yes |
| Scenario tests (crash simulation) | Yes | Yes | Yes |
| Real Windows device enumeration tests | Yes | No | No |

The core tests (Mock layer) run on all platforms. Platform-specific tests (real Windows SetupAPI) can be added later in Windows CI.

## Usage

```bash
# Build tests
mkdir build-tests && cd build-tests
cmake ../tests
cmake --build .

# Run all tests
ctest --output-on-failure

# Run individual test
./test_hotplug_monitor
./test_hotplug_debounce
./test_hotplug_scenarios

# Verbose output
./test_hotplug_monitor -v2
```

## Implementation Steps

1. Create `device/IDeviceDiscovery.h` interface (new file, ~15 lines)
2. Update `DeviceManager` to inherit `IDeviceDiscovery` (add `: public IDeviceDiscovery`)
3. Change `HotplugMonitor` constructor parameter from `DeviceManager*` to `IDeviceDiscovery*`
4. Create `tests/CMakeLists.txt` with QTest integration
5. Create `tests/hotplug/mock/MockDeviceDiscovery.h` and `TestDeviceBuilder.h`
6. Write `HotplugDebounceManager` tests
7. Write `HotplugMonitor` tests
8. Write end-to-end scenario tests
9. Verify all tests pass on Windows, macOS, and Linux CI
10. (Optional) Refactor `SerialHotplugHandler` for dependency injection to make it testable

## Estimated Effort

| Task | Time |
|------|------|
| Add `IDeviceDiscovery.h` + update `HotplugMonitor` constructor | 30 min |
| Write Mock and TestDeviceBuilder | 30 min |
| Write `HotplugDebounceManager` tests | 1 hour |
| Write `HotplugMonitor` tests | 1 hour |
| Write scenario tests | 1 hour |
| CMake integration + verify passing | 1 hour |
| **Total** | **~5 hours** |

## Future Enhancements

- **SerialHotplugHandler tests**: Refactor to accept `HotplugMonitor*` via constructor injection instead of accessing `DeviceManager::getInstance()` singleton
- **FFmpegHotplugHandler tests**: Leverage existing `FFmpegDeviceValidator*` dependency injection
- **Windows-specific integration tests**: Run on Windows CI with real USB device enumeration
- **Memory leak detection**: Add AddressSanitizer/LeakSanitizer to CI test runs
- **Fuzz testing**: Randomize device plug/unplug sequences and timing to find race conditions
