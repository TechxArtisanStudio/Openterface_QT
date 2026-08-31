#include <QCoreApplication>
#include <QSerialPort>
#include <QThread>
#include <cstdio>

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
    printf("串口已打开\n");
    
    // Try toggling DTR to reset device
    printf("DTR toggle...\n");
    serial.setDataTerminalReady(true);
    QThread::msleep(100);
    serial.setDataTerminalReady(false);
    QThread::msleep(100);
    serial.setDataTerminalReady(true);
    QThread::msleep(2000);
    
    serial.clear();
    
    // Read any spontaneous data
    printf("读取自发数据 (2秒)...\n");
    if (serial.waitForReadyRead(2000)) {
        QByteArray data = serial.readAll();
        printf("  收到: %d 字节: %s\n", data.size(), data.toHex(' ').toUpper().constData());
    } else {
        printf("  无数据\n");
    }
    
    // Send GET_INFO
    printf("\n发送 GET_INFO...\n");
    QByteArray cmd = QByteArray::fromHex("57 AB 00 01 00");
    serial.write(cmd);
    serial.waitForBytesWritten(2000);
    
    if (serial.waitForReadyRead(3000)) {
        QByteArray response = serial.readAll();
        printf("  ✓ 响应: %s\n", response.toHex(' ').toUpper().constData());
    } else {
        printf("  ✗ 无响应\n");
    }
    
    // Try 9600 baud
    serial.close();
    QThread::msleep(500);
    
    serial.setBaudRate(9600);
    if (!serial.open(QIODevice::ReadWrite)) {
        fprintf(stderr, "无法以9600打开\n");
        return 1;
    }
    printf("\n尝试 9600 波特率...\n");
    QThread::msleep(1000);
    serial.clear();
    serial.write(cmd);
    serial.waitForBytesWritten(2000);
    if (serial.waitForReadyRead(3000)) {
        QByteArray response = serial.readAll();
        printf("  ✓ 响应: %s\n", response.toHex(' ').toUpper().constData());
    } else {
        printf("  ✗ 无响应\n");
    }
    
    serial.close();
    return 0;
}
