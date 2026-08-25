// Quick GET_INFO test on already-flashed device
#include <iostream>
#include <string>
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QCoreApplication>
#include <QThread>

static quint8 calculateChecksum(const QByteArray &data) {
    quint32 sum = 0;
    for (auto byte : data)
        sum += static_cast<quint8>(byte);
    return sum % 256;
}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    std::string portName = "ttyACM0";
    if (argc > 1) portName = argv[1];

    std::cout << "=== GET_INFO Test ===" << std::endl;
    std::cout << "Port: /dev/" << portName << std::endl;

    QSerialPort serial;
    serial.setPortName(QString::fromStdString(portName));
    serial.setBaudRate(115200);
    serial.setDataBits(QSerialPort::Data8);
    serial.setParity(QSerialPort::NoParity);
    serial.setStopBits(QSerialPort::OneStop);
    serial.setFlowControl(QSerialPort::NoFlowControl);

    if (!serial.open(QIODevice::ReadWrite)) {
        std::cerr << "Cannot open: " << serial.errorString().toStdString() << std::endl;
        return 1;
    }
    std::cout << "Serial opened at 115200" << std::endl;
    QThread::msleep(500);

    // Test 1: GET_INFO WITHOUT checksum (original)
    {
        serial.clear();
        QByteArray cmd = QByteArray::fromHex("57 AB 00 01 00");
        std::cout << "\n[Test 1] GET_INFO without checksum: " << cmd.toHex(' ').toStdString() << std::endl;
        serial.write(cmd);
        serial.waitForBytesWritten(1000);
        if (serial.waitForReadyRead(2000)) {
            QByteArray resp = serial.readAll();
            std::cout << "  Response (" << resp.size() << "B): " << resp.toHex(' ').toStdString() << std::endl;
        } else {
            std::cout << "  TIMEOUT" << std::endl;
        }
    }
    QThread::msleep(200);

    // Test 2: GET_INFO WITH checksum
    {
        serial.clear();
        QByteArray cmd = QByteArray::fromHex("57 AB 00 01 00");
        cmd.append(calculateChecksum(cmd));
        std::cout << "\n[Test 2] GET_INFO with checksum: " << cmd.toHex(' ').toStdString() << std::endl;
        serial.write(cmd);
        serial.waitForBytesWritten(1000);
        if (serial.waitForReadyRead(2000)) {
            QByteArray resp = serial.readAll();
            std::cout << "  Response (" << resp.size() << "B): " << resp.toHex(' ').toStdString() << std::endl;
        } else {
            std::cout << "  TIMEOUT" << std::endl;
        }
    }
    QThread::msleep(200);

    // Test 3: GET_PARA_CFG with checksum
    {
        serial.clear();
        QByteArray cmd = QByteArray::fromHex("57 AB 00 08 00");
        cmd.append(calculateChecksum(cmd));
        std::cout << "\n[Test 3] GET_PARA_CFG with checksum: " << cmd.toHex(' ').toStdString() << std::endl;
        serial.write(cmd);
        serial.waitForBytesWritten(1000);
        if (serial.waitForReadyRead(2000)) {
            QByteArray resp = serial.readAll();
            std::cout << "  Response (" << resp.size() << "B): " << resp.toHex(' ').toStdString() << std::endl;
        } else {
            std::cout << "  TIMEOUT" << std::endl;
        }
    }
    QThread::msleep(200);

    // Test 4: Just send some raw bytes and see if anything comes back
    {
        serial.clear();
        QByteArray cmd = QByteArray::fromHex("57 AB 00 01 00 01");
        std::cout << "\n[Test 4] Raw 6 bytes: " << cmd.toHex(' ').toStdString() << std::endl;
        serial.write(cmd);
        serial.waitForBytesWritten(1000);
        if (serial.waitForReadyRead(2000)) {
            QByteArray resp = serial.readAll();
            std::cout << "  Response (" << resp.size() << "B): " << resp.toHex(' ').toStdString() << std::endl;
        } else {
            std::cout << "  TIMEOUT" << std::endl;
        }
    }

    serial.close();
    std::cout << "\nDone." << std::endl;
    return 0;
}
