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
    
    std::cout << "Opening port..." << std::endl;
    if (!serial.open(QIODevice::ReadWrite)) {
        std::cerr << "Cannot open: " << serial.errorString().toStdString() << std::endl;
        return 1;
    }
    
    std::cout << "Port opened" << std::endl;
    std::cout << "Baud: " << serial.baudRate() << std::endl;
    std::cout << "Data bits: " << serial.dataBits() << std::endl;
    std::cout << "Parity: " << serial.parity() << std::endl;
    std::cout << "Stop bits: " << serial.stopBits() << std::endl;
    std::cout << "Flow control: " << serial.flowControl() << std::endl;
    
    serial.clear();
    QThread::msleep(200);
    
    QByteArray cmd = QByteArray::fromHex("57 AB 00 01 00 03");
    std::cout << "\nSending: " << cmd.toHex(' ').toStdString() << std::endl;
    
    qint64 written = serial.write(cmd);
    std::cout << "Written: " << written << std::endl;
    
    bool ok = serial.waitForBytesWritten(1000);
    std::cout << "waitForBytesWritten: " << ok << std::endl;
    
    serial.flush();
    std::cout << "Flushed" << std::endl;
    
    QThread::msleep(100);
    
    std::cout << "bytesAvailable: " << serial.bytesAvailable() << std::endl;
    
    if (serial.waitForReadyRead(3000)) {
        QByteArray resp = serial.readAll();
        std::cout << "Response: " << resp.toHex(' ').toStdString() << std::endl;
    } else {
        std::cout << "No response, error: " << serial.errorString().toStdString() << std::endl;
    }
    
    serial.close();
    return 0;
}
