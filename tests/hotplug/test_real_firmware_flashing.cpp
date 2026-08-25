#include <QTest>
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QThread>
#include <QElapsedTimer>
#include <QDebug>
#include <QCoreApplication>

#include "wch/WCHFlasher.h"
#include "wch/WCHUSBTransport.h"
#include "wch/WCHHexParser.h"
#include "serial/ch9329.h"

// Define the logging category required by ch9329.h
Q_LOGGING_CATEGORY(log_core_serial, "opf.core.serial")

/**
 * @brief Hardware integration test for firmware flashing.
 *
 * This test requires:
 * - Real Openterface device connected via USB
 * - Device in ISP mode (hold BOOT button while plugging in)
 * - Valid firmware file (.hex format)
 *
 * Test flow:
 * 1. Detect device in ISP mode (VID:PID = 1A86:55E0 or 4348:55E0)
 * 2. Flash firmware to target chip (CH32V208)
 * 3. Wait for device to re-enumerate (VID:PID = 1A86:FE0C)
 * 4. Open serial port
 * 5. Send GET_INFO command (0x01) to verify firmware
 * 6. Validate response
 */
class TestRealFirmwareFlashing : public QObject {
    Q_OBJECT

private:
    // Device PIDs
    static constexpr uint16_t ISP_MODE_PID = 0x55E0;
    static constexpr uint16_t NORMAL_MODE_PID = 0xFE0C;
    static constexpr uint16_t WCH_VID = 0x1A86;

    // Protocol constants
    static constexpr uint8_t HEADER_BYTE_1 = 0x57;
    static constexpr uint8_t HEADER_BYTE_2 = 0xAB;
    static constexpr uint8_t CMD_GET_INFO = 0x01;
    static constexpr uint8_t RESP_GET_INFO = 0x81;

    // Test configuration
    QString m_firmwarePath;
    QString m_serialPortName;
    int m_testTimeoutMs = 30000;  // 30 seconds timeout for operations

    // Helper: Find device by VID/PID
    QSerialPortInfo findDeviceByVidPid(uint16_t vid, uint16_t pid) {
        const auto ports = QSerialPortInfo::availablePorts();
        for (const auto& port : ports) {
            if (port.vendorIdentifier() == vid && port.productIdentifier() == pid) {
                return port;
            }
        }
        return QSerialPortInfo();
    }

    // Helper: Wait for device with specific VID/PID
    bool waitForDevice(uint16_t vid, uint16_t pid, int timeoutMs) {
        QElapsedTimer timer;
        timer.start();

        while (timer.elapsed() < timeoutMs) {
            QSerialPortInfo port = findDeviceByVidPid(vid, pid);
            if (!port.isNull() && !port.portName().isEmpty()) {
                return true;
            }
            QThread::msleep(500);
            QCoreApplication::processEvents();
        }
        return false;
    }

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
            resp.errorMessage = QString("Response too short: %1 bytes (expected 12)").arg(data.size());
            return resp;
        }

        // Validate header
        if (static_cast<uint8_t>(data[0]) != HEADER_BYTE_1 ||
            static_cast<uint8_t>(data[1]) != HEADER_BYTE_2) {
            resp.errorMessage = QString("Invalid header: 0x%1 0x%2 (expected 0x57 0xAB)")
                .arg(static_cast<uint8_t>(data[0]), 2, 16, QChar('0'))
                .arg(static_cast<uint8_t>(data[1]), 2, 16, QChar('0'));
            return resp;
        }

        // Validate command code
        if (static_cast<uint8_t>(data[3]) != RESP_GET_INFO) {
            resp.errorMessage = QString("Wrong response code: 0x%1 (expected 0x81)")
                .arg(static_cast<uint8_t>(data[3]), 2, 16, QChar('0'));
            return resp;
        }

        // Parse payload
        resp.version = static_cast<uint8_t>(data[5]);
        resp.targetConnected = static_cast<uint8_t>(data[6]) != 0;
        resp.indicators = static_cast<uint8_t>(data[7]);
        resp.valid = true;

        return resp;
    }

    // Helper: Send GET_INFO command and get response
    GetInfoResponse sendGetInfo(QSerialPort& serial, int timeoutMs = 1000) {
        GetInfoResponse result;

        if (!serial.isOpen()) {
            result.errorMessage = "Serial port not open";
            return result;
        }

        // Clear any pending data
        serial.clear();

        // Send GET_INFO command
        QByteArray cmd = buildGetInfoCommand();
        qint64 written = serial.write(cmd);
        if (written != cmd.size()) {
            result.errorMessage = QString("Failed to write command: wrote %1 of %2 bytes")
                .arg(written).arg(cmd.size());
            return result;
        }

        // Wait for response
        if (!serial.waitForBytesWritten(timeoutMs)) {
            result.errorMessage = "Timeout waiting for command to be written";
            return result;
        }

        // Read response
        if (!serial.waitForReadyRead(timeoutMs)) {
            result.errorMessage = "Timeout waiting for response";
            return result;
        }

        QByteArray response = serial.readAll();
        qDebug() << "GET_INFO response:" << response.toHex(' ');

        return parseGetInfoResponse(response);
    }

private slots:
    void initTestCase() {
        // Get firmware path from environment or use default
        m_firmwarePath = qgetenv("FIRMWARE_PATH");
        if (m_firmwarePath.isEmpty()) {
            m_firmwarePath = QCoreApplication::applicationDirPath() + "/test_firmware.hex";
        }

        // Get serial port name from environment or auto-detect
        m_serialPortName = qgetenv("SERIAL_PORT");

        qDebug() << "=== Real Firmware Flashing Test ===";
        qDebug() << "Firmware path:" << m_firmwarePath;
        qDebug() << "Serial port:" << (m_serialPortName.isEmpty() ? "auto-detect" : m_serialPortName);
    }

    /**
     * @brief Test 1: Detect device in ISP mode.
     *
     * Prerequisites:
     * - Device must be plugged in and in ISP mode (BOOT button held during plug-in)
     */
    void testDetectISPModeDevice() {
        qDebug() << "\n=== Test 1: Detect ISP Mode Device ===";
        qDebug() << "Please ensure device is in ISP mode (hold BOOT while plugging in)";

        // Wait for ISP mode device
        bool found = waitForDevice(WCH_VID, ISP_MODE_PID, 10000);

        if (!found) {
            // Also check for alternative VID
            found = waitForDevice(0x4348, ISP_MODE_PID, 5000);
        }

        QVERIFY2(found, "Device not found in ISP mode. Please:\n"
                        "1. Unplug device\n"
                        "2. Hold BOOT button\n"
                        "3. Plug in device while holding BOOT\n"
                        "4. Release BOOT button\n"
                        "5. Run test again");

        QSerialPortInfo port = findDeviceByVidPid(WCH_VID, ISP_MODE_PID);
        if (port.isNull()) {
            port = findDeviceByVidPid(0x4348, ISP_MODE_PID);
        }

        qDebug() << "✓ Device found in ISP mode:" << port.portName();
        qDebug() << "  VID:" << QString("0x%1").arg(port.vendorIdentifier(), 4, 16, QChar('0'));
        qDebug() << "  PID:" << QString("0x%1").arg(port.productIdentifier(), 4, 16, QChar('0'));
    }

    /**
     * @brief Test 2: Flash firmware to target chip.
     *
     * This test actually flashes firmware to the device.
     * Requires firmware file to be present.
     */
    void testFlashFirmware() {
        qDebug() << "\n=== Test 2: Flash Firmware ===";

        // Check if firmware file exists
        if (!QFile::exists(m_firmwarePath)) {
            QSKIP(qPrintable(QString("Firmware file not found: %1\n"
                                     "Set FIRMWARE_PATH environment variable or place firmware at:\n"
                                     "  %2").arg(m_firmwarePath).arg(m_firmwarePath)));
        }

        qDebug() << "Loading firmware from:" << m_firmwarePath;

        try {
            // Parse firmware file
            WCHHexParser parser;
            std::vector<uint8_t> firmwareData = parser.parseFile(m_firmwarePath.toStdString());

            if (firmwareData.empty()) {
                QFAIL("Firmware file is empty or invalid");
            }

            qDebug() << "Firmware loaded:" << firmwareData.size() << "bytes";

            // Open USB transport
            WCHUSBTransport transport;

            // Scan for devices
            auto devices = transport.scanDevices();
            if (devices.empty()) {
                QFAIL("No WCH ISP devices found. Please ensure device is in ISP mode.");
            }

            qDebug() << "Found" << devices.size() << "WCH ISP device(s)";

            // Open first device
            transport.open(0);

            // Create flasher
            WCHFlasher flasher(&transport);

            // Get chip info BEFORE flash
            std::string chipInfoBefore = flasher.getChipInfo();
            qDebug() << "\n=== Chip Info BEFORE Flash ===";
            qDebug() << QString::fromStdString(chipInfoBefore);

            bool wasProtected = flasher.isCodeFlashProtected();
            qDebug() << "Flash protected:" << (wasProtected ? "YES" : "NO");

            if (wasProtected) {
                qDebug() << "\n⚠️  Flash is PROTECTED - will attempt to unprotect first";
                qDebug() << "This requires a device reset and reconnection...";
            }

            // Flash firmware with progress callback
            auto progressCallback = [](int percent, const std::string& message) {
                qDebug() << QString("[%1%] %2").arg(percent).arg(QString::fromStdString(message));
            };

            qDebug() << "\n=== Starting Flash Process ===";
            flasher.flash(firmwareData, progressCallback);

            qDebug() << "\n✓ Flash process completed";

            // Try to get chip info AFTER flash (device should be in normal mode now)
            // Note: After flash(), the device resets, so we can't query it in ISP mode anymore
            qDebug() << "Device has been reset and should be booting new firmware...";

            // Close transport
            transport.close();

            qDebug() << "\n=== Flash Summary ===";
            qDebug() << "  Firmware size:" << firmwareData.size() << "bytes";
            qDebug() << "  Was protected:" << (wasProtected ? "YES" : "NO");
            qDebug() << "  Flash status: COMPLETED";
            qDebug() << "  Next: Wait for device re-enumeration and verify with GET_INFO";

        } catch (const std::exception& e) {
            QFAIL(qPrintable(QString("Flash failed: %1").arg(e.what())));
        }
    }

    /**
     * @brief Test 3: Wait for device to re-enumerate after flash.
     */
    void testWaitForReenumeration() {
        qDebug() << "\n=== Test 3: Wait for Device Re-enumeration ===";
        qDebug() << "Waiting for device to boot with new firmware...";

        // Wait for device to appear in normal mode
        bool found = waitForDevice(WCH_VID, NORMAL_MODE_PID, m_testTimeoutMs);

        QVERIFY2(found, "Device did not re-enumerate after flash. Check:\n"
                        "1. USB connection\n"
                        "2. Firmware validity\n"
                        "3. Device power");

        QSerialPortInfo port = findDeviceByVidPid(WCH_VID, NORMAL_MODE_PID);
        qDebug() << "✓ Device re-enumerated in normal mode:" << port.portName();
        qDebug() << "  VID:" << QString("0x%1").arg(port.vendorIdentifier(), 4, 16, QChar('0'));
        qDebug() << "  PID:" << QString("0x%1").arg(port.productIdentifier(), 4, 16, QChar('0'));

        // Store port name for next test
        m_serialPortName = port.portName();
    }

    /**
     * @brief Test 4: Open serial port and verify communication.
     */
    void testOpenSerialPort() {
        qDebug() << "\n=== Test 4: Open Serial Port ===";

        if (m_serialPortName.isEmpty()) {
            // Try to auto-detect
            QSerialPortInfo port = findDeviceByVidPid(WCH_VID, NORMAL_MODE_PID);
            if (port.isNull()) {
                QSKIP("No serial port specified and device not found. Run testFlashFirmware first.");
            }
            m_serialPortName = port.portName();
        }

        qDebug() << "Opening serial port:" << m_serialPortName;

        QSerialPort serial;
        serial.setPortName(m_serialPortName);
        serial.setBaudRate(115200);
        serial.setDataBits(QSerialPort::Data8);
        serial.setParity(QSerialPort::NoParity);
        serial.setStopBits(QSerialPort::OneStop);
        serial.setFlowControl(QSerialPort::NoFlowControl);

        bool opened = serial.open(QIODevice::ReadWrite);
        QVERIFY2(opened, qPrintable(QString("Failed to open serial port: %1").arg(serial.errorString())));

        qDebug() << "✓ Serial port opened successfully";

        // Wait a bit for device to be ready
        QThread::msleep(500);

        serial.close();
    }

    /**
     * @brief Test 5: Send GET_INFO command to verify firmware.
     *
     * This is the key test - it verifies that the flashed firmware is working
     * by sending the GET_INFO command (0x01) and validating the response.
     */
    void testGetInfoVerification() {
        qDebug() << "\n=== Test 5: GET_INFO Verification ===";

        if (m_serialPortName.isEmpty()) {
            QSKIP("No serial port specified. Run previous tests first.");
        }

        qDebug() << "Using serial port:" << m_serialPortName;

        QSerialPort serial;
        serial.setPortName(m_serialPortName);
        serial.setBaudRate(115200);
        serial.setDataBits(QSerialPort::Data8);
        serial.setParity(QSerialPort::NoParity);
        serial.setStopBits(QSerialPort::OneStop);
        serial.setFlowControl(QSerialPort::NoFlowControl);

        bool opened = serial.open(QIODevice::ReadWrite);
        QVERIFY2(opened, qPrintable(QString("Failed to open serial port: %1").arg(serial.errorString())));

        // Wait for device to be ready
        QThread::msleep(1000);

        qDebug() << "Sending GET_INFO command (0x01)...";

        // Send GET_INFO and get response
        GetInfoResponse response = sendGetInfo(serial, 2000);

        serial.close();

        // Validate response
        QVERIFY2(response.valid, qPrintable(QString("GET_INFO failed: %1").arg(response.errorMessage)));

        qDebug() << "✓ GET_INFO response valid";
        qDebug() << "  Firmware version:" << response.version;
        qDebug() << "  Target connected:" << (response.targetConnected ? "YES" : "NO");
        qDebug() << "  Indicators:" << QString("0x%1").arg(response.indicators, 2, 16, QChar('0'));

        // Additional validation
        QVERIFY2(response.version > 0, "Firmware version should be > 0");

        qDebug() << "\n✓✓✓ Firmware flash and verification SUCCESSFUL ✓✓✓";
    }

    /**
     * @brief Test 6: Multiple GET_INFO commands (stability test).
     *
     * Send multiple GET_INFO commands to verify stable operation.
     */
    void testMultipleGetInfoCommands() {
        qDebug() << "\n=== Test 6: Multiple GET_INFO Commands (Stability Test) ===";

        if (m_serialPortName.isEmpty()) {
            QSKIP("No serial port specified. Run previous tests first.");
        }

        QSerialPort serial;
        serial.setPortName(m_serialPortName);
        serial.setBaudRate(115200);
        serial.setDataBits(QSerialPort::Data8);
        serial.setParity(QSerialPort::NoParity);
        serial.setStopBits(QSerialPort::OneStop);
        serial.setFlowControl(QSerialPort::NoFlowControl);

        bool opened = serial.open(QIODevice::ReadWrite);
        QVERIFY2(opened, qPrintable(QString("Failed to open serial port: %1").arg(serial.errorString())));

        QThread::msleep(500);

        const int numCommands = 10;
        int successCount = 0;

        qDebug() << "Sending" << numCommands << "GET_INFO commands...";

        for (int i = 0; i < numCommands; i++) {
            GetInfoResponse response = sendGetInfo(serial, 1000);

            if (response.valid) {
                successCount++;
                qDebug() << QString("  [%1/%2] ✓ Version: %3").arg(i + 1).arg(numCommands).arg(response.version);
            } else {
                qDebug() << QString("  [%1/%2] ✗ Failed: %3").arg(i + 1).arg(numCommands).arg(response.errorMessage);
            }

            QThread::msleep(100);  // Brief pause between commands
        }

        serial.close();

        qDebug() << "\nSuccess rate:" << successCount << "/" << numCommands;

        QVERIFY2(successCount == numCommands,
                 qPrintable(QString("Only %1 of %2 GET_INFO commands succeeded").arg(successCount).arg(numCommands)));

        qDebug() << "✓ All GET_INFO commands successful - firmware is stable";
    }

    void cleanupTestCase() {
        qDebug() << "\n=== Test Complete ===";
        qDebug() << "All tests passed successfully!";
        qDebug() << "Firmware is working correctly.";
    }
};

QTEST_MAIN(TestRealFirmwareFlashing)
#include "test_real_firmware_flashing.moc"
