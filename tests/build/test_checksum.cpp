#include <QCoreApplication>
#include <QSerialPort>
#include <QThread>
#include <iostream>

quint8 checksum_mod256(const QByteArray &data) {
    quint32 sum = 0;
    for (auto byte : data) sum += static_cast<quint8>(byte);
    return sum % 256;
}

quint8 checksum_xor(const QByteArray &data) {
    quint8 xor_val = 0;
    for (auto byte : data) xor_val ^= static_cast<quint8>(byte);
    return xor_val;
}

quint8 checksum_negate(const QByteArray &data) {
    quint32 sum = 0;
    for (auto byte : data) sum += static_cast<quint8>(byte);
    return static_cast<quint8>((~sum) + 1);  // Two's complement
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
    
    std::cout << "Port opened" << std::endl;
    QThread::msleep(500);
    serial.readAll();
    
    QByteArray base = QByteArray::fromHex("57 AB 00 01 00");
    
    // Test different checksums
    auto testCmd = [&](const QByteArray& cmd, const QString& name) {
        serial.clear();
        std::cout << "\nTesting " << name.toStdString() << ": " << cmd.toHex(' ').toStdString() << std::endl;
        serial.write(cmd);
        serial.waitForBytesWritten(1000);
        
        if (serial.waitForReadyRead(2000)) {
            QByteArray resp = serial.readAll();
            std::cout << "  Response: " << resp.toHex(' ').toStdString() << std::endl;
        } else {
            std::cout << "  No response" << std::endl;
        }
    };
    
    // Mod 256
    QByteArray cmd1 = base;
    cmd1.append(checksum_mod256(base));
    testCmd(cmd1, "mod256");
    
    // XOR
    QByteArray cmd2 = base;
    cmd2.append(checksum_xor(base));
    testCmd(cmd2, "XOR");
    
    // Two's complement
    QByteArray cmd3 = base;
    cmd3.append(checksum_negate(base));
    testCmd(cmd3, "negate");
    
    // No checksum
    testCmd(base, "no checksum");
    
    serial.close();
    return 0;
}
