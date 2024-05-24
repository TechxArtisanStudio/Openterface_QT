#include "SerialPortManager.h"
#include <QSerialPortInfo>
#include <QTimer>
#include <QtConcurrent>
#include <QFuture>

Q_LOGGING_CATEGORY(log_core_serial, "opf.core.serial")

const QByteArray SerialPortManager::MOUSE_ABS_ACTION_PREFIX = QByteArray::fromHex("57 AB 00 04 07 02");
const QByteArray SerialPortManager::MOUSE_REL_ACTION_PREFIX = QByteArray::fromHex("57 AB 00 05 05 01");
const QByteArray SerialPortManager::CMD_GET_PARA_CFG = QByteArray::fromHex("57 AB 00 08 00");
const QByteArray SerialPortManager::CMD_RESET = QByteArray::fromHex("57 AB 00 0F 00");

SerialPortManager::SerialPortManager(QObject *parent) : QObject(parent), serialThread(new QThread(this)) {
    //qCDebug(log_core_serial) << "Initialize serial port.";

    initializeSerialPort();

    observerSerialPortNotification();
}

void SerialPortManager::checkSerialPort(){
    if (serialPort != nullptr && serialPort->isOpen()) {
        if (!serialPort->setDataTerminalReady(true)) {
            qCDebug(log_core_serial) << "Checking port, disconnected...";
            if(ready){
                closePort();
            }
        } else {
            //qCDebug(log_core_serial) << "Checking port, opened...";
            if(!ready){
                qCDebug(log_core_serial) << "Port opened, but the port is not ready, reset now...";
                resetSerialPort();
            }
        }
    } else {
        qCDebug(log_core_serial) << "Checking port, closed...";
        // check the port name available
        if(!ready){
            QString availablePort = getPortName();
            if(availablePort != nullptr)
                initializeSerialPort();
        }
    }
}


void SerialPortManager::setEventCallback(SerialPortEventCallback* callback) {
    eventCallback = callback;
}

QString SerialPortManager::getPortName(){
#ifdef __linux__
    QString desiredPortName = "USB Serial";
#elif _WIN32
    QString desiredPortName = "USB-SERIAL CH340";
#endif

    foreach(const QSerialPortInfo &info, QSerialPortInfo::availablePorts()) {
        qCDebug(log_core_serial) << "Found port: " << info.portName() << "port description: " << info.description() ;

        if (info.description() == desiredPortName) {
            qCDebug(log_core_serial) << "Found desired port: " << info.portName();
            return info.portName();
        }
    }
    return nullptr;
}


void SerialPortManager::initializeSerialPort(){
    qCDebug(log_core_serial) << "Initialize port... ";
    if (serialPort != nullptr && serialPort->isOpen()) {
        closePort();
    }

    QString availablePort = getPortName();
    if (availablePort == nullptr) {
        qCDebug(log_core_serial) << "No port available.";
        QThread::sleep(1);
        return;
    }

    serialPort = new QSerialPort();
    if(openPort(availablePort, SerialPortManager::DEFAULT_BAUDRATE)){
        connect(serialPort, &QSerialPort::readyRead, this, &SerialPortManager::readData);
        connect(serialPort, &QSerialPort::bytesWritten, this, &SerialPortManager::bytesWritten);
        qCDebug(log_core_serial) << "Open port " << availablePort << " with baudrate " << SerialPortManager::DEFAULT_BAUDRATE << "success.";
    }else{
        qCDebug(log_core_serial) << "Open port " << availablePort << " with baudrate " << SerialPortManager::DEFAULT_BAUDRATE << "fail.";
    }
    bool ret = sendCommand(SerialPortManager::CMD_GET_PARA_CFG, true);

    if(!ret) {
        qCDebug(log_core_serial) << "Send command failure. ";
    }else {
        // In order to receive the response, we need to exit the event loop
        QEventLoop loop;
        connect(serialPort, &QSerialPort::readyRead, &loop, &QEventLoop::quit);
        loop.exec();
        QThread::msleep(100);
        if(ret && ready) {
            return;
        }
    }
    QThread::sleep(1);
    closePort();

    if(openPort(availablePort, SerialPortManager::ORIGINAL_BAUDRATE)){
        connect(serialPort, &QSerialPort::readyRead, this, &SerialPortManager::readData);
        connect(serialPort, &QSerialPort::bytesWritten, this, &SerialPortManager::bytesWritten);
        qCDebug(log_core_serial) << "Open port " << availablePort << " with baudrate " << SerialPortManager::ORIGINAL_BAUDRATE << "success.";
    } else {
        qCDebug(log_core_serial) << "Open port " << availablePort << " with baudrate " << SerialPortManager::ORIGINAL_BAUDRATE << "fail.";
    }
    ret = sendCommand(SerialPortManager::CMD_GET_PARA_CFG, true);
    if(!ret) {
        qCDebug(log_core_serial) << "Send command failure. ";
    }else {
        // In order to receive the response, we need to exit the event loop
        QEventLoop loop;
        connect(serialPort, &QSerialPort::readyRead, &loop, &QEventLoop::quit);
        loop.exec();
        QThread::msleep(100);
        if(ret && ready) {
            return;
        }
    }

}

void SerialPortManager::resetSerialPort(){
    qCDebug(log_core_serial) << "resetSerialPort: " << serialPort;
    closePort();
    ready=false;
}

void SerialPortManager::observerSerialPortNotification(){
    qCDebug(log_core_serial) << "Created a timer to observer SerialPort...";
    // create a timer to check the serial port every 1s
    QTimer *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &SerialPortManager::checkSerialPort);
    timer->start(1000);
}

bool arePortListsEqual(const QList<QSerialPortInfo>& list1, const QList<QSerialPortInfo>& list2)
{
    if (list1.size() != list2.size())
        return false;

    for (int i = 0; i < list1.size(); ++i) {
        if (list1[i].portName() != list2[i].portName())
            return false;
    }

    return true;
}


SerialPortManager::~SerialPortManager() {
    serialThread->quit();
    serialThread->wait();
    closePort();
    delete serialThread;
    delete serialPort;
}

bool SerialPortManager::openPort(const QString &portName, int baudRate) {
    if (serialPort != nullptr && serialPort->isOpen()) {
        qCDebug(log_core_serial) << "Serial port is already opened.";
        return false;
    }
    serialPort->setPortName(portName);
    serialPort->setBaudRate(baudRate);
    if (serialPort->open(QIODevice::ReadWrite)) {
        qCDebug(log_core_serial) << "Open port " << portName + ", baudrate: " << baudRate;
        return true;
    } else {
        return false;
    }
}

void SerialPortManager::closePort() {
    if (serialPort != nullptr && serialPort->isOpen()) {
        qCDebug(log_core_serial) << "Close serial port";
        serialPort->flush();
        serialPort->clear();
        serialPort->clearError();
        disconnect(serialPort, &QSerialPort::readyRead, this, &SerialPortManager::readData);
        disconnect(serialPort, &QSerialPort::bytesWritten, this, &SerialPortManager::bytesWritten);
        serialPort->close();
        delete serialPort;
        serialPort = nullptr;
        ready=false;
        if(eventCallback!=nullptr) eventCallback->onPortConnected("NA");
    }
}

void SerialPortManager::readData() {
    QByteArray data = serialPort->readAll();
    if (data.size() >= 4) {
        unsigned char fourthByte = data[3];

        if ((fourthByte & 0xF0) == 0xC0) {
            unsigned char code = fourthByte | 0xC0;
            switch (code)
            {
            case 0xC1:
                qCDebug(log_core_serial) << "Error(" + QString::number(code, 16) + "), Serial response timeout, data: " + data.toHex(' ');
                break;
            case 0xC2:
                qCDebug(log_core_serial) << "Error(" + QString::number(code, 16) + "),  Packet header error, data: " + data.toHex(' ');
                break;
            case 0xC3:
                qCDebug(log_core_serial) << "Error(" + QString::number(code, 16) + "),  Command error, data: " + data.toHex(' ');
                break;
            case 0xC4:
                qCDebug(log_core_serial) << "Error(" + QString::number(code, 16) + "),  Checksum error, data: " + data.toHex(' ');
                break;
            case 0xC5:
                qCDebug(log_core_serial) << "Error(" + QString::number(code, 16) + "),  Argument error, data: " + data.toHex(' ');
                break;
            case 0xC6:
                qCDebug(log_core_serial) << "Error(" + QString::number(code, 16) + "),  Execution error, data: " + data.toHex(' ');
                break;
            default:
                qCDebug(log_core_serial) << "Error(" + QString::number(code, 16) + "),  Unknown error, data: " + data.toHex(' ');
                break;
            }
        }else{
            qCDebug(log_core_serial) << "Data read from serial port: " << data.toHex(' ');

            unsigned char code = fourthByte | 0x80;
            int baudrate = 0;
            switch (code)
            {
            case 0x88:
                // get parameter configuration
                // baud rate 8...11 bytes
                baudrate = ((unsigned char)data[8] << 24) | ((unsigned char)data[9] << 16) | ((unsigned char)data[10] << 8) | (unsigned char)data[11];
                qCDebug(log_core_serial) << "Receive baudrate update: " << baudrate;
                if (baudrate == SerialPortManager::DEFAULT_BAUDRATE) {
                    ready = true;
                    if(eventCallback!=nullptr)
                        eventCallback->onPortConnected(serialPort->portName());
                }
                break;
            default:
                break;
            }
        }
    }
    emit dataReceived(data);
}

void SerialPortManager::aboutToClose()
{
    qCDebug(log_core_serial) << "aboutToClose";
}

void SerialPortManager::bytesWritten(qint64 bytes){
    //qCDebug(log_core_serial) << "bytesWritten";
}

bool SerialPortManager::writeData(const QByteArray &data) {
    if (serialPort->isOpen()) {
        serialPort->write(data);
        qCDebug(log_core_serial) << "Data written to serial port: " << data.toHex(' ');
        return true;
    }

    qCDebug(log_core_serial) << "Serial is not opened, " << serialPort->portName();
    ready = false;
    return false;
}

bool SerialPortManager::sendCommand(const QByteArray &data, bool force) {
    if(!force && !ready) return false;
    QByteArray command = data;
    command.append(calculateChecksum(command));
    return writeData(command);
}

quint8 SerialPortManager::calculateChecksum(const QByteArray &data) {
    quint32 sum = 0;
    for (auto byte : data) {
        sum += static_cast<unsigned char>(byte);
    }
    return sum % 256;
}
