#include <QCoreApplication>
#include <QSerialPort>
#include <QThread>
#include <iostream>

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    
    QSerialPort serial;
    serial.setPortName("/dev/ttyACM0");
    serial.setBaudRate(9600);
    serial.setDataBits(QSerialPort::Data8);
    serial.setParity(QSerialPort::NoParity);
    serial.setStopBits(QSerialPort::OneStop);
    serial.setFlowControl(QSerialPort::NoFlowControl);
    
    if (!serial.open(QIODevice::ReadWrite)) {
        std::cerr << "Cannot open port" << std::endl;
        return 1;
    }
    
    std::cout << "Port opened at 9600" << std::endl;
    QThread::msleep(500);
    serial.readAll();
    
    QByteArray cmd = QByteArray::fromHex("57 AB 00 01 00 03");
    std::cout << "Sending: " << cmd.toHex(' ').toStdString() << std::endl;
    serial.write(cmd);
    serial.waitForBytesWritten(1000);
    
    if (serial.waitForReadyRead(3000)) {
        QByteArray resp = serial.readAll();
        std::cout << "Response: " << resp.toHex(' ').toStdString() << std::endl;
    } else {
        std::cout << "No response" << std::endl;
    }
    
    serial.close();
    return 0;
}
