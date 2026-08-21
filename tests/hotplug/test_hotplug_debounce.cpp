#include <QTest>
#include <QSignalSpy>
#include "device/HotplugDebounceManager.h"

/**
 * @brief Unit tests for device::HotplugDebounceManager.
 *
 * Fully cross-platform, zero platform dependencies.
 * Tests the state machine, fast-scan mode, and rapid-reconnect detection.
 */
class TestHotplugDebounce : public QObject {
    Q_OBJECT

private slots:
    void testInitialState() {
        device::HotplugDebounceManager manager;
        QVERIFY(!manager.isFastScanning());
        QCOMPARE(manager.getCurrentPollInterval(),
                 static_cast<int>(device::HotplugDebounceManager::NORMAL_POLL_INTERVAL_MS));
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
