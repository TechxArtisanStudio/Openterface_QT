#include <QCoreApplication>
#include <QSerialPort>
#include <QThread>
#include <iostream>
#include <QByteArray>

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
        std::cerr << "Cannot open port: " << serial.errorString().toStdString() << std::endl;
        return 1;
    }
    
    std::cout << "Port opened at 115200" << std::endl;
    
    // Wait for device to initialize
    QThread::msleep(2000);
    
    // Clear buffer
    serial.readAll();
    
    // GET_INFO command with checksum
    QByteArray cmd = QByteArray::fromHex("57 AB 00 01 00 03");
    std::cout << "Sending GET_INFO: " << cmd.toHex(' ').toStdString() << std::endl;
    
    serial.write(cmd);
    serial.waitForBytesWritten(1000);
    
    if (serial.waitForReadyRead(3000)) {
        QByteArray resp = serial.readAll();
        std::cout << "Response (" << resp.size() << " bytes): " << resp.toHex(' ').toStdString() << std::endl;
        
        if (resp.size() >= 8 && (uint8_t)resp[0] == 0x57 && (uint8_t)resp[1] == 0xAB) {
            std::cout << "✓✓✓ GET_INFO SUCCESS ✓✓✓" << std::endl;
        } else {
            std::cout << "Response received but header mismatch" << std::endl;
        }
    } else {
        std::cout << "✗ No response (timeout)" << std::endl;
    }
    
    serial.close();
    return 0;
}
