#include <QCoreApplication>
#include <QSerialPort>
#include <QThread>
#include <iostream>
#include <QByteArray>

void testCommand(QSerialPort& serial, const QByteArray& cmd, const QString& name) {
    serial.clear();
    std::cout << "\nTesting " << name.toStdString() << ": " << cmd.toHex(' ').toStdString() << std::endl;
    
    serial.write(cmd);
    serial.waitForBytesWritten(1000);
    
    if (serial.waitForReadyRead(2000)) {
        QByteArray resp = serial.readAll();
        std::cout << "  Response (" << resp.size() << " bytes): " << resp.toHex(' ').toStdString() << std::endl;
    } else {
        std::cout << "  ✗ No response" << std::endl;
    }
}

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    
    QSerialPort serial;
    serial.setPortName("/dev/ttyACM0");
    serial.setBaudRate(115200);
    serial.setDataBits(QSerialPort::Data8);
    serial.setParity(QSerialPort::NoParity);
    serial.setStopBits(QSerialPort::OneStop);
    serial.setFlowControl(QSerialPort::NoFlowControl);
    
    if (!serial.open(QIODevice::ReadWrite)) {
        std::cerr << "Cannot open port" << std::endl;
        return 1;
    }
    
    QThread::msleep(1000);
    serial.readAll();
    
    // Test various commands
    testCommand(serial, QByteArray::fromHex("57 AB 00 01 00 03"), "GET_INFO");
    QThread::msleep(500);
    
    testCommand(serial, QByteArray::fromHex("57 AB 00 08 00 0B"), "GET_PARA_CFG");
    QThread::msleep(500);
    
    testCommand(serial, QByteArray::fromHex("57 AB 00 0F 00 0C"), "RESET");
    
    serial.close();
    return 0;
}
