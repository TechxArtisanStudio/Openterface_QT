#include <QTest>
#include <QSignalSpy>
#include "device/HotplugMonitor.h"
#include "device/DeviceInfo.h"
#include "mock/MockDeviceDiscovery.h"
#include "mock/TestDeviceBuilder.h"
#include "mock/SubsystemHandlerSimulator.h"

/**
 * @brief Integration tests for four-subsystem hotplug coordination.
 *
 * Verifies that HotplugMonitor correctly dispatches plug/unplug events
 * to all four subsystem handlers (serial, camera, HID, audio), and that
 * each handler's portChain matching logic works correctly.
 *
 * This replicates the crash scenario: a USB transient disconnect/reconnect
 * that affects all four subsystems simultaneously.
 */
class TestSubsystemIntegration : public QObject {
    Q_OBJECT

private:
    // Helper: create all 4 handler simulators
    struct FourSubsystemHandlers {
        SubsystemHandlerSimulator serial{SubsystemHandlerSimulator::SubsystemType::Serial};
        SubsystemHandlerSimulator camera{SubsystemHandlerSimulator::SubsystemType::Camera};
        SubsystemHandlerSimulator hid{SubsystemHandlerSimulator::SubsystemType::Hid};
        SubsystemHandlerSimulator audio{SubsystemHandlerSimulator::SubsystemType::Audio};

        void connectAll(HotplugMonitor* monitor) {
            serial.connectToMonitor(monitor);
            camera.connectToMonitor(monitor);
            hid.connectToMonitor(monitor);
            audio.connectToMonitor(monitor);
        }

        void resetAll() {
            serial.reset();
            camera.reset();
            hid.reset();
            audio.reset();
        }

        bool allActive() const {
            return serial.isActive() && camera.isActive()
                && hid.isActive() && audio.isActive();
        }

        bool allInactive() const {
            return !serial.isActive() && !camera.isActive()
                && !hid.isActive() && !audio.isActive();
        }
    };

private slots:
    void initTestCase() {
        qRegisterMetaType<DeviceInfo>("DeviceInfo");
        qRegisterMetaType<DeviceChangeEvent>("DeviceChangeEvent");
    }

    /**
     * @brief Full device activates all 4 subsystems.
     */
    void testFullDeviceActivatesAllSubsystems() {
        MockDeviceDiscovery mock;
        HotplugMonitor monitor(&mock);
        FourSubsystemHandlers handlers;
        handlers.connectAll(&monitor);

        mock.plugInDevice(TestDeviceBuilder::createFullDevice("0002-0001"));
        monitor.checkForChanges();

        QVERIFY(handlers.allActive());
        QCOMPARE(handlers.serial.currentPortChain(), QString("0002-0001"));
        QCOMPARE(handlers.camera.currentPortChain(), QString("0002-0001"));
        QCOMPARE(handlers.hid.currentPortChain(), QString("0002-0001"));
        QCOMPARE(handlers.audio.currentPortChain(), QString("0002-0001"));
    }

    /**
     * @brief Unplugging full device deactivates all 4 subsystems.
     */
    void testUnplugDeactivatesAllSubsystems() {
        MockDeviceDiscovery mock;
        HotplugMonitor monitor(&mock);
        FourSubsystemHandlers handlers;
        handlers.connectAll(&monitor);

        // Plug in
        mock.plugInDevice(TestDeviceBuilder::createFullDevice("0002-0001"));
        monitor.checkForChanges();
        QVERIFY(handlers.allActive());

        // Unplug
        mock.unplugAll();
        monitor.checkForChanges();
        QVERIFY(handlers.allInactive());

        QCOMPARE(handlers.serial.deactivateCount(), 1);
        QCOMPARE(handlers.camera.deactivateCount(), 1);
        QCOMPARE(handlers.hid.deactivateCount(), 1);
        QCOMPARE(handlers.audio.deactivateCount(), 1);
    }

    /**
     * @brief Partial device only activates matching subsystems.
     */
    void testPartialDeviceOnlyActivatesMatching() {
        // Test 1: Serial-only device
        {
            MockDeviceDiscovery mock;
            HotplugMonitor monitor(&mock);
            FourSubsystemHandlers handlers;
            handlers.connectAll(&monitor);

            mock.plugInDevice(TestDeviceBuilder::createSerialOnlyDevice("0002-0001"));
            monitor.checkForChanges();

            QVERIFY(handlers.serial.isActive());
            QVERIFY(!handlers.camera.isActive());
            QVERIFY(!handlers.hid.isActive());
            QVERIFY(!handlers.audio.isActive());
        }

        // Test 2: Camera-only device
        {
            MockDeviceDiscovery mock;
            HotplugMonitor monitor(&mock);
            FourSubsystemHandlers handlers;
            handlers.connectAll(&monitor);

            mock.plugInDevice(TestDeviceBuilder::createCameraOnlyDevice("0003-0001"));
            monitor.checkForChanges();

            QVERIFY(!handlers.serial.isActive());
            QVERIFY(handlers.camera.isActive());
            QVERIFY(!handlers.hid.isActive());
            QVERIFY(!handlers.audio.isActive());
        }

        // Test 3: Audio-only device (Openterface branded)
        {
            MockDeviceDiscovery mock;
            HotplugMonitor monitor(&mock);
            FourSubsystemHandlers handlers;
            handlers.connectAll(&monitor);

            mock.plugInDevice(TestDeviceBuilder::createAudioOnlyDevice("0004-0001"));
            monitor.checkForChanges();

            QVERIFY(!handlers.serial.isActive());
            QVERIFY(!handlers.camera.isActive());
            QVERIFY(!handlers.hid.isActive());
            QVERIFY(handlers.audio.isActive());
        }
    }

    /**
     * @brief USB transient disconnect: all 4 subsystems deactivate then reactivate.
     */
    void testUsbTransientDisconnectAllSubsystems() {
        MockDeviceDiscovery mock;
        HotplugMonitor monitor(&mock);
        FourSubsystemHandlers handlers;
        handlers.connectAll(&monitor);

        // 1. Device online
        mock.plugInDevice(TestDeviceBuilder::createFullDevice("0002-0001"));
        monitor.checkForChanges();
        QVERIFY(handlers.allActive());

        // 2. USB disconnect
        mock.unplugAll();
        monitor.checkForChanges();
        QVERIFY(handlers.allInactive());
        QCOMPARE(handlers.serial.deactivateCount(), 1);

        // 3. Device reconnects
        mock.plugInDevice(TestDeviceBuilder::createFullDevice("0002-0001"));
        monitor.checkForChanges();
        QVERIFY(handlers.allActive());
        QCOMPARE(handlers.serial.activateCount(), 2);
        QCOMPARE(handlers.serial.currentPortChain(), QString("0002-0001"));
    }

    /**
     * @brief Two devices with partial overlap: unplugging one doesn't affect the other.
     *
     * Note: HotplugMonitor only emits newDevicePluggedIn for the first added
     * device per check cycle, so we plug in devices in separate check cycles.
     */
    void testTwoDevicesPartialOverlap() {
        MockDeviceDiscovery mock;
        HotplugMonitor monitor(&mock);
        FourSubsystemHandlers handlers;
        handlers.connectAll(&monitor);

        // Device A: serial + camera on port "0002-0001"
        DeviceInfo deviceA = TestDeviceBuilder::createSerialOnlyDevice("0002-0001");
        deviceA.cameraDeviceId = "usb_openterface_0002-0001";
        deviceA.cameraDevicePath = "video=Openterface_0002-0001";

        // Device B: HID + audio on port "0003-0001"
        DeviceInfo deviceB = TestDeviceBuilder::createHidOnlyDevice("0003-0001");
        deviceB.audioDeviceId = "Openterface Audio 0003-0001";
        deviceB.audioDevicePath = "\\\\.\\audio#0003-0001";

        // Plug in device A first, then B (separate check cycles
        // because HotplugMonitor emits per-device signal only for first added)
        mock.plugInDevice(deviceA);
        monitor.checkForChanges();
        mock.plugInDevice(deviceB);
        monitor.checkForChanges();

        // All 4 should be active (2 devices cover all interfaces)
        QVERIFY(handlers.allActive());

        // Unplug device A only
        mock.unplugDevice("0002-0001");
        monitor.checkForChanges();

        // Serial + Camera should deactivate, HID + Audio should remain active
        QVERIFY(!handlers.serial.isActive());
        QVERIFY(!handlers.camera.isActive());
        QVERIFY(handlers.hid.isActive());
        QVERIFY(handlers.audio.isActive());
        QCOMPARE(handlers.hid.currentPortChain(), QString("0003-0001"));
        QCOMPARE(handlers.audio.currentPortChain(), QString("0003-0001"));
    }

    /**
     * @brief Reproduces the crash scenario with 4-subsystem coordination.
     *
     * Timeline from crash log:
     *   09:54:07 - device disconnect -> all subsystems should clean up
     *   09:54:09 - device reconnect -> all subsystems should reactivate
     *
     * Verifies no subsystem retains stale state after the cycle.
     */
    void testCrashScenarioFourSubsystemCoordination() {
        MockDeviceDiscovery mock;
        HotplugMonitor monitor(&mock);
        FourSubsystemHandlers handlers;
        handlers.connectAll(&monitor);

        // Phase 1: All subsystems online
        mock.plugInDevice(TestDeviceBuilder::createFullDevice("0002-0001"));
        monitor.checkForChanges();
        QVERIFY(handlers.allActive());

        // Phase 2: USB transient disconnect (09:54:07)
        mock.unplugAll();
        monitor.checkForChanges();

        // All subsystems must clean up
        QVERIFY(handlers.allInactive());
        QVERIFY(handlers.serial.currentPortChain().isEmpty());
        QVERIFY(handlers.camera.currentPortChain().isEmpty());
        QVERIFY(handlers.hid.currentPortChain().isEmpty());
        QVERIFY(handlers.audio.currentPortChain().isEmpty());

        // Phase 3: Device reconnects (09:54:09)
        mock.plugInDevice(TestDeviceBuilder::createFullDevice("0002-0001"));
        monitor.checkForChanges();

        // All subsystems must reactivate cleanly
        QVERIFY(handlers.allActive());
        QCOMPARE(handlers.serial.activateCount(), 2);
        QCOMPARE(handlers.camera.activateCount(), 2);
        QCOMPARE(handlers.hid.activateCount(), 2);
        QCOMPARE(handlers.audio.activateCount(), 2);

        // Port chain consistency
        QCOMPARE(handlers.serial.currentPortChain(), handlers.camera.currentPortChain());
        QCOMPARE(handlers.hid.currentPortChain(), handlers.audio.currentPortChain());
    }

    /**
     * @brief Stress test: 50 plug/unplug cycles across all 4 subsystems.
     */
    void testRepeatedPlugUnplugFourSubsystems() {
        MockDeviceDiscovery mock;
        HotplugMonitor monitor(&mock);
        FourSubsystemHandlers handlers;
        handlers.connectAll(&monitor);

        for (int i = 0; i < 50; i++) {
            mock.plugInDevice(TestDeviceBuilder::createFullDevice("0002-0001"));
            monitor.checkForChanges();

            mock.unplugAll();
            monitor.checkForChanges();
        }

        // Each subsystem should have been activated and deactivated 50 times
        QCOMPARE(handlers.serial.activateCount(), 50);
        QCOMPARE(handlers.serial.deactivateCount(), 50);
        QCOMPARE(handlers.camera.activateCount(), 50);
        QCOMPARE(handlers.camera.deactivateCount(), 50);
        QCOMPARE(handlers.hid.activateCount(), 50);
        QCOMPARE(handlers.hid.deactivateCount(), 50);
        QCOMPARE(handlers.audio.activateCount(), 50);
        QCOMPARE(handlers.audio.deactivateCount(), 50);

        // Final state: all inactive
        QVERIFY(handlers.allInactive());
    }

    /**
     * @brief Unrelated device (non-Openterface) is filtered by audio handler.
     */
    void testUnrelatedDeviceFiltering() {
        MockDeviceDiscovery mock;
        HotplugMonitor monitor(&mock);
        FourSubsystemHandlers handlers;
        handlers.connectAll(&monitor);

        // Unrelated device has serial port but non-Openterface audio
        mock.plugInDevice(TestDeviceBuilder::createUnrelatedDevice("0001-0002"));
        monitor.checkForChanges();

        // Serial activates (it has a serial port)
        QVERIFY(handlers.serial.isActive());
        // Camera, HID do not activate (unrelated device has no camera/HID)
        QVERIFY(!handlers.camera.isActive());
        QVERIFY(!handlers.hid.isActive());
        // Audio does NOT activate (audioDeviceId doesn't contain "Openterface")
        QVERIFY(!handlers.audio.isActive());
    }
};

QTEST_MAIN(TestSubsystemIntegration)
#include "test_subsystem_integration.moc"
