#include <QCoreApplication>
#include <QSerialPort>
#include <QThread>
#include <cstdio>

void sendCommand(QSerialPort& serial, const QByteArray& cmd, const char* name) {
    printf("\n=== 发送 %s ===\n", name);
    printf("  命令: %s\n", cmd.toHex(' ').toUpper().constData());
    
    serial.clear();
    qint64 written = serial.write(cmd);
    printf("  已写入: %lld 字节\n", written);
    
    if (!serial.waitForBytesWritten(2000)) {
        printf("  ✗ 写入超时\n");
        return;
    }
    
    if (serial.waitForReadyRead(3000)) {
        QByteArray response = serial.readAll();
        printf("  ✓ 收到响应! %d 字节\n", response.size());
        printf("  数据: %s\n", response.toHex(' ').toUpper().constData());
    } else {
        printf("  ✗ 无响应\n");
    }
}

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    
    QSerialPort serial;
    serial.setPortName("/dev/ttyACM0");
    serial.setBaudRate(115200);
    serial.setDataBits(QSerialPort::Data8);
    serial.setParity(QSerialPort::NoParity);
    serial.setStopBits(QSerialPort::OneStop);
    serial.setFlowControl(QSerialPort::NoFlowControl);
    
    if (!serial.open(QIODevice::ReadWrite)) {
        fprintf(stderr, "无法打开串口\n");
        return 1;
    }
    
    printf("串口已打开，等待设备初始化 (3秒)...\n");
    QThread::msleep(3000);
    serial.clear();
    
    // Try different commands
    sendCommand(serial, QByteArray::fromHex("57 AB 00 01 00"), "GET_INFO");
    sendCommand(serial, QByteArray::fromHex("57 AB 00 08 00"), "GET_PARA_CFG");
    sendCommand(serial, QByteArray::fromHex("57 AB 00 0F 00"), "RESET");
    sendCommand(serial, QByteArray::fromHex("57 AB 00 0C 00"), "SET_DEFAULT_CFG");
    
    // Try reading without sending anything
    printf("\n=== 直接读取 (不发送命令) ===\n");
    serial.clear();
    if (serial.waitForReadyRead(2000)) {
        QByteArray data = serial.readAll();
        printf("  收到数据: %d 字节\n", data.size());
        printf("  数据: %s\n", data.toHex(' ').toUpper().constData());
    } else {
        printf("  无数据\n");
    }
    
    serial.close();
    return 0;
}
