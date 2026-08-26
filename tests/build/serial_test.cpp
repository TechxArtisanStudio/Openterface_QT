#include <QCoreApplication>
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QDebug>
#include <QThread>

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);

    QString portName = "/dev/ttyACM0";
    if (argc > 1) {
        portName = argv[1];
    }

    qDebug() << "=== Serial Port Test ===";
    qDebug() << "Port:" << portName;

    QSerialPort serial;
    serial.setPortName(portName);
    serial.setBaudRate(115200);
    serial.setDataBits(QSerialPort::Data8);
    serial.setParity(QSerialPort::NoParity);
    serial.setStopBits(QSerialPort::OneStop);
    serial.setFlowControl(QSerialPort::NoFlowControl);

    if (!serial.open(QIODevice::ReadWrite)) {
        qCritical() << "Failed to open serial port:" << serial.errorString();
        return 1;
    }

    qDebug() << "✓ Serial port opened";

    // Wait for device to be ready
    QThread::msleep(1000);

    // Send GET_INFO command
    QByteArray cmd = QByteArray::fromHex("57 AB 00 01 00");
    qDebug() << "\nSending GET_INFO command:" << cmd.toHex(' ').toUpper();

    serial.clear();
    qint64 written = serial.write(cmd);
    qDebug() << "Written" << written << "bytes";

    if (!serial.waitForBytesWritten(1000)) {
        qCritical() << "Timeout waiting for write";
        serial.close();
        return 1;
    }

    qDebug() << "Waiting for response...";

    if (serial.waitForReadyRead(2000)) {
        QByteArray response = serial.readAll();
        qDebug() << "Response received:" << response.size() << "bytes";
        qDebug() << "Response:" << response.toHex(' ').toUpper();

        if (response.size() >= 12) {
            qDebug() << "\n=== Parsed Response ===";
            qDebug() << "Header:" << QString("0x%1 0x%2").arg((uint8_t)response[0], 2, 16, QChar('0')).arg((uint8_t)response[1], 2, 16, QChar('0'));
            qDebug() << "Address:" << QString("0x%1").arg((uint8_t)response[2], 2, 16, QChar('0'));
            qDebug() << "Command:" << QString("0x%1").arg((uint8_t)response[3], 2, 16, QChar('0'));
            qDebug() << "Length:" << (uint8_t)response[4];
            qDebug() << "Version:" << (uint8_t)response[5];
            qDebug() << "Target Connected:" << ((uint8_t)response[6] != 0 ? "YES" : "NO");
            qDebug() << "Indicators:" << QString("0x%1").arg((uint8_t)response[7], 2, 16, QChar('0'));

            if ((uint8_t)response[0] == 0x57 && (uint8_t)response[1] == 0xAB && (uint8_t)response[3] == 0x81) {
                qDebug() << "\n✓✓✓ Firmware is responding! Flash successful! ✓✓✓";
            }
        }
    } else {
        qWarning() << "No response received";
        qDebug() << "Trying to read anyway...";
        QByteArray response = serial.readAll();
        if (!response.isEmpty()) {
            qDebug() << "Got data:" << response.toHex(' ').toUpper();
        }
    }

    serial.close();
    qDebug() << "\nTest complete";

    return 0;
}
