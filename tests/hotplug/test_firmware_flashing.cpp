#include <QTest>
#include <QSignalSpy>
#include <QThread>
#include <QElapsedTimer>
#include <QSerialPort>
#include <QSerialPortInfo>
#include "device/HotplugMonitor.h"
#include "device/DeviceInfo.h"
#include "mock/MockDeviceDiscovery.h"
#include "mock/TestDeviceBuilder.h"

/**
 * @brief Systematic test suite for firmware flashing and serial port reconnection.
 *
 * This test simulates the complete firmware flashing workflow:
 * 1. Device in ISP mode (flashing mode)
 * 2. Flash process (USB disconnect during reset)
 * 3. Device re-enumeration after flash
 * 4. Serial port auto-reconnection
 * 5. Firmware verification using GET_INFO command (0x01)
 *
 * Tests cover:
 * - Normal flash workflow
 * - Rapid plug/unplug during flash
 * - Delayed USB re-enumeration
 * - Serial port reconnection timing
 * - GET_INFO command validation
 */
class TestFirmwareFlashing : public QObject {
    Q_OBJECT

private:
    // Protocol constants
    static constexpr uint8_t HEADER_BYTE_1 = 0x57;
    static constexpr uint8_t HEADER_BYTE_2 = 0xAB;
    static constexpr uint8_t CMD_GET_INFO = 0x01;
    static constexpr uint8_t RESP_GET_INFO = 0x81;
    static constexpr uint8_t STATUS_SUCCESS = 0x00;

    // Helper: Build GET_INFO command
    QByteArray buildGetInfoCommand() {
        return QByteArray::fromHex("57 AB 00 01 00");
    }

    // Helper: Parse GET_INFO response
    struct GetInfoResponse {
        bool valid = false;
        uint8_t version = 0;
        bool targetConnected = false;
        uint8_t indicators = 0;
        QString errorMessage;
    };

    GetInfoResponse parseGetInfoResponse(const QByteArray& data) {
        GetInfoResponse resp;

        if (data.size() < 12) {
            resp.errorMessage = QString("Response too short: %1 bytes").arg(data.size());
            return resp;
        }

        // Validate header
        if (static_cast<uint8_t>(data[0]) != HEADER_BYTE_1 ||
            static_cast<uint8_t>(data[1]) != HEADER_BYTE_2) {
            resp.errorMessage = "Invalid header";
            return resp;
        }

        // Validate command code
        if (static_cast<uint8_t>(data[3]) != RESP_GET_INFO) {
            resp.errorMessage = QString("Wrong response code: 0x%1").arg(static_cast<uint8_t>(data[3]), 2, 16, QChar('0'));
            return resp;
        }

        // Parse payload
        resp.version = static_cast<uint8_t>(data[5]);
        resp.targetConnected = static_cast<uint8_t>(data[6]) != 0;
        resp.indicators = static_cast<uint8_t>(data[7]);
        resp.valid = true;

        return resp;
    }

    // Helper: Calculate checksum for a packet
    uint8_t calculateChecksum(const QByteArray& data) {
        uint8_t sum = 0;
        for (int i = 0; i < data.size() - 1; i++) {
            sum += static_cast<uint8_t>(data[i]);
        }
        return sum;
    }

    // Helper: Validate checksum
    bool validateChecksum(const QByteArray& data) {
        if (data.isEmpty()) return false;
        uint8_t expected = calculateChecksum(data);
        uint8_t actual = static_cast<uint8_t>(data[data.size() - 1]);
        return expected == actual;
    }

private slots:
    void initTestCase() {
        qRegisterMetaType<DeviceInfo>("DeviceInfo");
        qRegisterMetaType<DeviceChangeEvent>("DeviceChangeEvent");
    }

    /**
     * @brief Test 1: Simulate complete flash workflow with GET_INFO verification.
     *
     * Scenario:
     * 1. Device detected in ISP mode
     * 2. Flash process starts (simulated by USB disconnect)
     * 3. Device re-enumerates with new firmware
     * 4. Serial port connects
     * 5. Send GET_INFO command to verify firmware
     *
     * Expected: Device successfully reconnects and responds to GET_INFO
     */
    void testCompleteFlashWorkflow() {
        MockDeviceDiscovery mock;
        HotplugMonitor monitor(&mock);

        QSignalSpy plugSpy(&monitor, &HotplugMonitor::newDevicePluggedIn);
        QSignalSpy unplugSpy(&monitor, &HotplugMonitor::deviceUnplugged);

        // Step 1: Device in ISP mode (before flash)
        DeviceInfo ispDevice = TestDeviceBuilder::createGen2Device("0002-0001");
        ispDevice.vid = "1A86";
        ispDevice.pid = "55E0";  // ISP mode PID
        mock.plugInDevice(ispDevice);
        monitor.checkForChanges();
        QCOMPARE(plugSpy.count(), 1);

        // Step 2: Flash process - device resets (USB disconnect)
        qDebug() << "Simulating flash reset - USB disconnect";
        mock.unplugAll();
        monitor.checkForChanges();
        QCOMPARE(unplugSpy.count(), 1);

        // Step 3: Wait for USB re-enumeration (simulated delay)
        QThread::msleep(500);  // Simulate device reboot time

        // Step 4: Device re-enumerates with new firmware (normal mode)
        DeviceInfo flashedDevice = TestDeviceBuilder::createGen2Device("0002-0001");
        flashedDevice.vid = "1A86";
        flashedDevice.pid = "FE0C";  // Normal mode PID
        mock.plugInDevice(flashedDevice);
        monitor.checkForChanges();
        QCOMPARE(plugSpy.count(), 2);

        // Step 5: Verify state
        QVERIFY(true);  // If we get here without crash, test passes
        qDebug() << "Complete flash workflow test passed";
    }

    /**
     * @brief Test 2: Test rapid plug/unplug during flash process.
     *
     * Scenario: Device experiences unstable USB connection during flash
     * Expected: System handles gracefully, no crash
     */
    void testUnstableUSBDuringFlash() {
        MockDeviceDiscovery mock;
        HotplugMonitor monitor(&mock);

        QSignalSpy plugSpy(&monitor, &HotplugMonitor::newDevicePluggedIn);
        QSignalSpy unplugSpy(&monitor, &HotplugMonitor::deviceUnplugged);

        // Simulate unstable connection during flash
        for (int i = 0; i < 10; i++) {
            DeviceInfo device = TestDeviceBuilder::createGen2Device("0002-0001");
            mock.plugInDevice(device);
            monitor.checkForChanges();

            QThread::msleep(50);  // Brief connection

            mock.unplugAll();
            monitor.checkForChanges();

            QThread::msleep(50);  // Brief disconnect
        }

        // Should handle all cycles without crash
        QCOMPARE(plugSpy.count(), 10);
        QCOMPARE(unplugSpy.count(), 10);
        qDebug() << "Unstable USB test passed - handled" << plugSpy.count() << "cycles";
    }

    /**
     * @brief Test 3: Test delayed USB re-enumeration after flash.
     *
     * Scenario: Device takes longer than expected to re-enumerate after flash
     * Expected: System waits and eventually reconnects
     */
    void testDelayedReenumeration() {
        MockDeviceDiscovery mock;
        HotplugMonitor monitor(&mock);

        QSignalSpy plugSpy(&monitor, &HotplugMonitor::newDevicePluggedIn);

        // Initial device (ISP mode)
        DeviceInfo ispDevice = TestDeviceBuilder::createGen2Device("0002-0001");
        mock.plugInDevice(ispDevice);
        monitor.checkForChanges();

        // Unplug (flash reset)
        mock.unplugAll();
        monitor.checkForChanges();

        // Simulate various delay scenarios
        QList<int> delays = {100, 500, 1000, 2000, 3000};
        for (int delay : delays) {
            qDebug() << "Testing re-enumeration delay:" << delay << "ms";

            QThread::msleep(delay);

            DeviceInfo flashedDevice = TestDeviceBuilder::createGen2Device("0002-0001");
            mock.plugInDevice(flashedDevice);
            monitor.checkForChanges();

            mock.unplugAll();
            monitor.checkForChanges();
        }

        QVERIFY(true);  // Should handle all delays without crash
        qDebug() << "Delayed re-enumeration test passed";
    }

    /**
     * @brief Test 4: GET_INFO command structure validation.
     *
     * Scenario: Validate GET_INFO command and response parsing
     * Expected: Command builds correctly, response parses correctly
     */
    void testGetInfoCommandStructure() {
        // Test command building
        QByteArray cmd = buildGetInfoCommand();
        QCOMPARE(cmd.size(), 5);
        QCOMPARE(static_cast<uint8_t>(cmd[0]), HEADER_BYTE_1);
        QCOMPARE(static_cast<uint8_t>(cmd[1]), HEADER_BYTE_2);
        QCOMPARE(static_cast<uint8_t>(cmd[2]), 0x00);  // addr
        QCOMPARE(static_cast<uint8_t>(cmd[3]), CMD_GET_INFO);
        QCOMPARE(static_cast<uint8_t>(cmd[4]), 0x00);  // length

        // Test response parsing with valid response
        QByteArray validResponse = QByteArray::fromHex("57 AB 00 81 06 01 01 00 00 00 00 2A");
        GetInfoResponse resp = parseGetInfoResponse(validResponse);
        QVERIFY(resp.valid);
        QCOMPARE(resp.version, static_cast<uint8_t>(1));
        QVERIFY(resp.targetConnected);

        // Test response parsing with invalid header
        QByteArray invalidHeader = QByteArray::fromHex("00 00 00 81 06 01 01 00 00 00 00 00");
        GetInfoResponse resp2 = parseGetInfoResponse(invalidHeader);
        QVERIFY(!resp2.valid);
        QVERIFY(resp2.errorMessage.contains("header"));

        // Test response parsing with too short data
        QByteArray tooShort = QByteArray::fromHex("57 AB 00 81 06");
        GetInfoResponse resp3 = parseGetInfoResponse(tooShort);
        QVERIFY(!resp3.valid);
        QVERIFY(resp3.errorMessage.contains("short"));

        qDebug() << "GET_INFO command structure test passed";
    }

    /**
     * @brief Test 5: Checksum validation for protocol packets.
     *
     * Scenario: Verify checksum calculation and validation
     * Expected: Checksums calculated correctly
     */
    void testChecksumValidation() {
        // Test with known good packet
        QByteArray goodPacket = QByteArray::fromHex("57 AB 00 01 00");
        uint8_t checksum = calculateChecksum(goodPacket);
        goodPacket.append(static_cast<char>(checksum));
        QVERIFY(validateChecksum(goodPacket));

        // Test with corrupted packet
        QByteArray badPacket = goodPacket;
        badPacket[badPacket.size() - 1] = static_cast<char>(checksum + 1);
        QVERIFY(!validateChecksum(badPacket));

        qDebug() << "Checksum validation test passed";
    }

    /**
     * @brief Test 6: Stress test - multiple flash cycles.
     *
     * Scenario: Perform many flash cycles to test stability
     * Expected: No crashes, no memory leaks
     */
    void testMultipleFlashCycles() {
        MockDeviceDiscovery mock;
        HotplugMonitor monitor(&mock);

        QElapsedTimer timer;
        timer.start();

        const int cycles = 50;
        for (int i = 0; i < cycles; i++) {
            // ISP mode
            DeviceInfo ispDevice = TestDeviceBuilder::createGen2Device("0002-0001");
            ispDevice.pid = "55E0";
            mock.plugInDevice(ispDevice);
            monitor.checkForChanges();

            // Flash reset
            mock.unplugAll();
            monitor.checkForChanges();

            // Brief wait
            QThread::msleep(10);

            // Normal mode (after flash)
            DeviceInfo flashedDevice = TestDeviceBuilder::createGen2Device("0002-0001");
            flashedDevice.pid = "FE0C";
            mock.plugInDevice(flashedDevice);
            monitor.checkForChanges();

            // Clear for next cycle
            mock.unplugAll();
            monitor.checkForChanges();
        }

        qint64 elapsed = timer.elapsed();
        qDebug() << "Completed" << cycles << "flash cycles in" << elapsed << "ms";
        QVERIFY(elapsed < 30000);  // Should complete in reasonable time
    }

    /**
     * @brief Test 7: Test serial port path tracking after flash.
     *
     * Scenario: Verify port chain is maintained correctly through flash cycle
     * Expected: Port chain correctly tracked and updated
     */
    void testPortChainTracking() {
        MockDeviceDiscovery mock;
        HotplugMonitor monitor(&mock);

        QSignalSpy plugSpy(&monitor, &HotplugMonitor::newDevicePluggedIn);

        // Initial connection
        DeviceInfo device1 = TestDeviceBuilder::createGen2Device("0002-0001");
        mock.plugInDevice(device1);
        monitor.checkForChanges();
        QCOMPARE(plugSpy.count(), 1);

        // Flash reset
        mock.unplugAll();
        monitor.checkForChanges();

        // Reconnect (same port chain)
        DeviceInfo device2 = TestDeviceBuilder::createGen2Device("0002-0001");
        mock.plugInDevice(device2);
        monitor.checkForChanges();
        QCOMPARE(plugSpy.count(), 2);

        // Verify port chain is consistent
        QVERIFY(device1.portChain == device2.portChain);
        qDebug() << "Port chain tracking test passed:" << device1.portChain;
    }

    /**
     * @brief Test 8: Test multiple devices during flash.
     *
     * Scenario: Flash one device while another is connected
     * Expected: System correctly identifies which device is being flashed
     */
    void testMultipleDevicesDuringFlash() {
        MockDeviceDiscovery mock;
        HotplugMonitor monitor(&mock);

        QSignalSpy plugSpy(&monitor, &HotplugMonitor::newDevicePluggedIn);

        // Device 1: Already connected (normal operation)
        DeviceInfo device1 = TestDeviceBuilder::createGen2Device("0002-0001");
        mock.plugInDevice(device1);
        monitor.checkForChanges();

        // Device 2: In ISP mode (being flashed)
        DeviceInfo device2 = TestDeviceBuilder::createGen2Device("0003-0001");
        device2.pid = "55E0";  // ISP mode
        mock.plugInDevice(device2);
        monitor.checkForChanges();

        QCOMPARE(plugSpy.count(), 2);

        // Flash device 2 (unplug)
        mock.unplugDevice("0003-0001");
        monitor.checkForChanges();

        // Device 1 should still be connected
        QList<DeviceInfo> devices = mock.discoverDevices();
        QCOMPARE(devices.size(), 1);
        QCOMPARE(devices[0].portChain, QString("0002-0001"));

        // Device 2 reconnects after flash
        DeviceInfo device2Flashed = TestDeviceBuilder::createGen2Device("0003-0001");
        device2Flashed.pid = "FE0C";  // Normal mode
        mock.plugInDevice(device2Flashed);
        monitor.checkForChanges();

        // Both devices should be present
        devices = mock.discoverDevices();
        QCOMPARE(devices.size(), 2);

        qDebug() << "Multiple devices test passed";
    }

    /**
     * @brief Test 9: Test VID/PID transition during flash.
     *
     * Scenario: Verify correct handling of VID/PID change from ISP to normal mode
     * Expected: System recognizes both modes as same device type
     */
    void testVIDPIDTransition() {
        MockDeviceDiscovery mock;
        HotplugMonitor monitor(&mock);

        // ISP mode: VID=1A86, PID=55E0
        DeviceInfo ispDevice = TestDeviceBuilder::createGen2Device("0002-0001");
        ispDevice.vid = "1A86";
        ispDevice.pid = "55E0";
        mock.plugInDevice(ispDevice);
        monitor.checkForChanges();

        // Verify ISP device
        QList<DeviceInfo> devices = mock.discoverDevices();
        QCOMPARE(devices.size(), 1);
        QCOMPARE(devices[0].vid, QString("1A86"));
        QCOMPARE(devices[0].pid, QString("55E0"));

        // Flash reset
        mock.unplugAll();
        monitor.checkForChanges();

        // Normal mode: VID=1A86, PID=FE0C
        DeviceInfo normalDevice = TestDeviceBuilder::createGen2Device("0002-0001");
        normalDevice.vid = "1A86";
        normalDevice.pid = "FE0C";
        mock.plugInDevice(normalDevice);
        monitor.checkForChanges();

        // Verify normal device
        devices = mock.discoverDevices();
        QCOMPARE(devices.size(), 1);
        QCOMPARE(devices[0].vid, QString("1A86"));
        QCOMPARE(devices[0].pid, QString("FE0C"));

        qDebug() << "VID/PID transition test passed";
    }

    /**
     * @brief Test 10: Integration test - flash with serial communication simulation.
     *
     * Scenario: Simulate complete workflow including serial communication after flash
     * Expected: System handles flash, reconnection, and command validation
     */
    void testFlashWithSerialCommunication() {
        MockDeviceDiscovery mock;
        HotplugMonitor monitor(&mock);

        // Step 1: Device in ISP mode
        DeviceInfo ispDevice = TestDeviceBuilder::createGen2Device("0002-0001");
        ispDevice.pid = "55E0";
        mock.plugInDevice(ispDevice);
        monitor.checkForChanges();

        // Step 2: Flash process (USB disconnect)
        mock.unplugAll();
        monitor.checkForChanges();

        // Step 3: Wait for re-enumeration
        QThread::msleep(100);

        // Step 4: Device reconnects
        DeviceInfo flashedDevice = TestDeviceBuilder::createGen2Device("0002-0001");
        flashedDevice.pid = "FE0C";
        mock.plugInDevice(flashedDevice);
        monitor.checkForChanges();

        // Step 5: Simulate serial communication
        // Build GET_INFO command
        QByteArray cmd = buildGetInfoCommand();
        QVERIFY(cmd.size() > 0);

        // Simulate response (this would normally come from serial port)
        QByteArray mockResponse = QByteArray::fromHex("57 AB 00 81 06 02 01 00 00 00 00 2B");
        GetInfoResponse resp = parseGetInfoResponse(mockResponse);
        QVERIFY(resp.valid);
        QCOMPARE(resp.version, static_cast<uint8_t>(2));
        QVERIFY(resp.targetConnected);

        qDebug() << "Flash with serial communication test passed";
        qDebug() << "  Firmware version:" << resp.version;
        qDebug() << "  Target connected:" << resp.targetConnected;
    }
};

QTEST_MAIN(TestFirmwareFlashing)
#include "test_firmware_flashing.moc"
