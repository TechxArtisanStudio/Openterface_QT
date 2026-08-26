#include <QCoreApplication>
#include <QSerialPort>
#include <QThread>
#include <iostream>

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
    
    std::cout << "Port opened" << std::endl;
    
    // Clear buffers
    serial.clear();
    QThread::msleep(200);
    
    // Send GET_INFO
    QByteArray cmd = QByteArray::fromHex("57 AB 00 01 00 03");
    std::cout << "Sending: " << cmd.toHex(' ').toStdString() << std::endl;
    
    qint64 written = serial.write(cmd);
    std::cout << "Written: " << written << " bytes" << std::endl;
    
    serial.waitForBytesWritten(1000);
    serial.flush();
    
    // Wait for response
    if (serial.waitForReadyRead(3000)) {
        QByteArray resp = serial.readAll();
        std::cout << "Response: " << resp.toHex(' ').toStdString() << std::endl;
        
        if (resp.size() >= 14 && resp[0] == 0x57 && resp[1] == 0xAB && resp[3] == 0x81) {
            std::cout << "✓✓✓ GET_INFO SUCCESS ✓✓✓" << std::endl;
        }
    } else {
        std::cout << "✗ No response" << std::endl;
    }
    
    serial.close();
    return 0;
}
