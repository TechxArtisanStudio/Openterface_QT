#include <QTest>
#include <QSignalSpy>
#include "device/HotplugMonitor.h"
#include "device/DeviceInfo.h"
#include "mock/MockDeviceDiscovery.h"
#include "mock/TestDeviceBuilder.h"

/**
 * @brief Unit tests for HotplugMonitor.
 *
 * Cross-platform via MockDeviceDiscovery. Tests event emission,
 * deduplication, and change detection.
 */
class TestHotplugMonitor : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        qRegisterMetaType<DeviceInfo>("DeviceInfo");
        qRegisterMetaType<DeviceChangeEvent>("DeviceChangeEvent");
    }

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
        QSignalSpy changesSpy(&monitor, &HotplugMonitor::deviceChangesDetected);

        mock.plugInDevice(TestDeviceBuilder::createGen1Device("0002-0001"));
        mock.plugInDevice(TestDeviceBuilder::createGen2Device("0003-0001"));
        mock.plugInDevice(TestDeviceBuilder::createGen3Device("0004-0001"));

        monitor.checkForChanges();

        // Note: HotplugMonitor emits newDevicePluggedIn only for the first added
        // device per check cycle (known behavior — see break in checkForChanges loop).
        // The aggregate deviceChangesDetected signal carries all added devices.
        QCOMPARE(addedSpy.count(), 1);
        QCOMPARE(changesSpy.count(), 1);
        auto event = changesSpy.first().first().value<DeviceChangeEvent>();
        QCOMPARE(event.addedDevices.size(), 3);
    }
};

QTEST_MAIN(TestHotplugMonitor)
#include "test_hotplug_monitor.moc"
