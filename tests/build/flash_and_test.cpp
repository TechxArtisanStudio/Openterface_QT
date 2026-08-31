// Standalone flash + GET_INFO test
// Usage: ./flash_and_test <firmware.hex>
#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <cstring>

#include <QSerialPort>
#include <QSerialPortInfo>
#include <QCoreApplication>
#include <QThread>

#include "wch/WCHFlasher.h"
#include "wch/WCHUSBTransport.h"
#include "wch/WCHHexParser.h"

static quint8 calculateChecksum(const QByteArray &data) {
    quint32 sum = 0;
    for (auto byte : data)
        sum += static_cast<quint8>(byte);
    return sum % 256;
}

static QByteArray buildGetInfoWithChecksum() {
    QByteArray cmd = QByteArray::fromHex("57 AB 00 01 00");
    cmd.append(calculateChecksum(cmd));
    return cmd;
}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);

    std::string firmwarePath = "/home/bot/project/06(1).hex";
    if (argc > 1) firmwarePath = argv[1];

    std::cout << "=== Flash + GET_INFO Test ===" << std::endl;
    std::cout << "Firmware: " << firmwarePath << std::endl;

    // === STEP 1: Flash firmware ===
    try {
        WCHHexParser parser;
        auto firmware = parser.parseFile(firmwarePath);
        std::cout << "Firmware loaded: " << firmware.size() << " bytes" << std::endl;

        WCHUSBTransport transport;
        auto devices = transport.scanDevices();
        if (devices.empty()) {
            std::cerr << "ERROR: No ISP device found. Put device in ISP mode first!" << std::endl;
            return 1;
        }
        std::cout << "Found " << devices.size() << " ISP device(s)" << std::endl;

        transport.open(0);
        WCHFlasher flasher(&transport);

        std::cout << "\n" << flasher.getChipInfo() << std::endl;
        std::cout << "Protected: " << (flasher.isCodeFlashProtected() ? "YES" : "NO") << std::endl;

        auto progress = [](int pct, const std::string& msg) {
            std::cout << "[" << pct << "%] " << msg << std::endl;
        };

        std::cout << "\n=== Flashing ===" << std::endl;
        flasher.flash(firmware, progress);
        std::cout << "\n✓ Flash completed!" << std::endl;

        transport.close();
    } catch (const std::exception& e) {
        std::cerr << "Flash FAILED: " << e.what() << std::endl;
        return 1;
    }

    // === STEP 2: Wait for re-enumeration ===
    std::cout << "\n=== Waiting for device re-enumeration ===" << std::endl;
    std::string serialPort;

    for (int i = 0; i < 30; i++) {
        QThread::msleep(1000);
        auto ports = QSerialPortInfo::availablePorts();
        for (const auto& p : ports) {
            if (p.vendorIdentifier() == 0x1A86 && p.productIdentifier() == 0xFE0C) {
                serialPort = p.portName().toStdString();
                break;
            }
        }
        if (!serialPort.empty()) break;
        std::cout << "  Waiting... " << (i+1) << "s\r" << std::flush;
    }

    if (serialPort.empty()) {
        std::cerr << "ERROR: Device did not re-enumerate!" << std::endl;
        return 1;
    }
    std::cout << "✓ Device found on /dev/" << serialPort << std::endl;

    // === STEP 3: Test GET_INFO ===
    std::cout << "\n=== Testing GET_INFO ===" << std::endl;
    QThread::msleep(2000);  // Wait for firmware to initialize

    QSerialPort serial;
    serial.setPortName(QString::fromStdString(serialPort));
    serial.setBaudRate(115200);
    serial.setDataBits(QSerialPort::Data8);
    serial.setParity(QSerialPort::NoParity);
    serial.setStopBits(QSerialPort::OneStop);
    serial.setFlowControl(QSerialPort::NoFlowControl);

    if (!serial.open(QIODevice::ReadWrite)) {
        std::cerr << "ERROR: Cannot open serial port: " << serial.errorString().toStdString() << std::endl;
        return 1;
    }
    std::cout << "Serial port opened at 115200" << std::endl;

    // Clear buffers after open
    serial.clear();
    QThread::msleep(100);

    // Try GET_INFO with checksum (3 times)
    bool success = false;
    for (int attempt = 0; attempt < 3; attempt++) {
        serial.clear();
        QByteArray cmd = buildGetInfoWithChecksum();
        std::cout << "\nAttempt " << (attempt+1) << ": sending GET_INFO ("
                  << cmd.toHex(' ').toStdString() << ")" << std::endl;

        serial.write(cmd);
        serial.waitForBytesWritten(1000);
        serial.flush();

        if (serial.waitForReadyRead(2000)) {
            QByteArray resp = serial.readAll();
            std::cout << "Response (" << resp.size() << " bytes): "
                      << resp.toHex(' ').toStdString() << std::endl;

            if (resp.size() >= 8 &&
                (uint8_t)resp[0] == 0x57 && (uint8_t)resp[1] == 0xAB) {
                std::cout << "✓ GET_INFO responded successfully!" << std::endl;
                std::cout << "  Header: 57 AB" << std::endl;
                std::cout << "  Cmd: 0x" << std::hex << ((uint8_t)resp[3]) << std::dec << std::endl;
                success = true;
                break;
            }
        } else {
            std::cout << "  Timeout - no response" << std::endl;
        }
        QThread::msleep(500);
    }

    serial.close();

    if (success) {
        std::cout << "\n✓✓✓ ALL TESTS PASSED ✓✓✓" << std::endl;
        return 0;
    } else {
        std::cout << "\n✗✗✗ GET_INFO FAILED ✗✗✗" << std::endl;
        return 1;
    }
}
