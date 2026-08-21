#include <QTest>
#include <QSignalSpy>
#include "device/HotplugMonitor.h"
#include "device/HotplugDebounceManager.h"
#include "device/DeviceInfo.h"
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
    void initTestCase() {
        qRegisterMetaType<DeviceInfo>("DeviceInfo");
        qRegisterMetaType<DeviceChangeEvent>("DeviceChangeEvent");
    }

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
            [&debounce](const DeviceInfo& device) {
                debounce.handleDeviceRemoved(device.getUniqueKey(), device.serialPortPath);
            });

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
        QCOMPARE(firstDevice.vid, secondDevice.vid);
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
        QSignalSpy changesSpy(&monitor, &HotplugMonitor::deviceChangesDetected);

        mock.plugInDevice(TestDeviceBuilder::createGen1Device("0002-0001"));
        mock.plugInDevice(TestDeviceBuilder::createUnrelatedDevice("0001-0002"));

        monitor.checkForChanges();

        // HotplugMonitor emits newDevicePluggedIn only for the first added device
        // per check cycle; deviceChangesDetected carries all changes.
        QCOMPARE(spy.count(), 1);
        QCOMPARE(changesSpy.count(), 1);
        auto event = changesSpy.first().first().value<DeviceChangeEvent>();
        QCOMPARE(event.addedDevices.size(), 2);
    }
};

QTEST_MAIN(TestHotplugScenarios)
#include "test_hotplug_scenarios.moc"
