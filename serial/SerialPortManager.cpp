/*
* ========================================================================== *
*                                                                            *
*    This file is part of the Openterface Mini KVM App QT version            *
*                                                                            *
*    Copyright (C) 2024   <info@openterface.com>                             *
*                                                                            *
*    This program is free software: you can redistribute it and/or modify    *
*    it under the terms of the GNU General Public License as published by    *
*    the Free Software Foundation version 3.                                 *
*                                                                            *
*    This program is distributed in the hope that it will be useful, but     *
*    WITHOUT ANY WARRANTY; without even the implied warranty of              *
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU        *
*    General Public License for more details.                                *
*                                                                            *
*    You should have received a copy of the GNU General Public License       *
*    along with this program. If not, see <http://www.gnu.org/licenses/>.    *
*                                                                            *
* ========================================================================== *
*/

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
const QByteArray SerialPortManager::CMD_SET_PARA_CFG_PREFIX = QByteArray::fromHex("57 AB 00 09 32 82 80 00 00 01 C2 00");
const QByteArray SerialPortManager::CMD_SET_PARA_CFG_MID = QByteArray::fromHex("08 00 00 03 86 1a 29 e1 00 00 00 01 00 0d 00 00 00 00 00 00") + QByteArray(23, 0x00) ;

SerialPortManager::SerialPortManager(QObject *parent) : QObject(parent), serialThread(new QThread(this)) {
    //qCDebug(log_core_serial) << "Initialize serial port.";

    //initializeSerialPort();

    observerSerialPortNotification();
}

void SerialPortManager::checkSerialPort() {
    qCDebug(log_core_serial) << "Check serial port.";

    if(!ready){
        baudrate = baudrate == SerialPortManager::DEFAULT_BAUDRATE ? SerialPortManager::ORIGINAL_BAUDRATE : SerialPortManager::DEFAULT_BAUDRATE;
        qCDebug(log_core_serial) << "Try baudrate"<< baudrate;
    }

    try {
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
                    closePort();
                    ready=false;
                    QString availablePort = getPortName();
                    if(availablePort != nullptr){
                        initializeSerialPort(availablePort);
                    }
                }
            }
        } else {
            qCDebug(log_core_serial) << "Checking port, closed...";
            // check the port name available
            if(!ready){
                QString availablePort = getPortName();
                if(availablePort != nullptr){
                    initializeSerialPort(availablePort);
                }
            }
        }
    } catch (const std::exception& e) {
        qCDebug(log_core_serial) << "Exception caught in checkSerialPort: " << e.what();
    }
    qCDebug(log_core_serial) << "Check serial port completed.";
}

bool SerialPortManager::prepareSerialPort(const QString& availablePort) {
    qCDebug(log_core_serial) << "Prepare for port" << availablePort << "with baudrate" << baudrate;
    if(!openPort(availablePort, baudrate)){
        qCDebug(log_core_serial) << "Open port" << availablePort << "with baudrate" << baudrate << "fail.";
        return false;
    }

    QThread *serialThread = new QThread(this);
    SerialPortManager& manager = SerialPortManager::getInstance();

    // Move the manager to the new thread
    manager.moveToThread(serialThread);

    // Connect the readyRead signal to the readData slot
    qCDebug(log_core_serial) << "Observe" << availablePort << "with baudrate" << baudrate << "data ready.";
    connect(serialPort, &QSerialPort::readyRead, this, &SerialPortManager::readData);
    connect(serialPort, &QSerialPort::bytesWritten, this, &SerialPortManager::bytesWritten);
    qCDebug(log_core_serial) << "Open port" << availablePort << "with baudrate" << baudrate << "success.";

    // Start the thread
    serialThread->start();

    return sendCommand(SerialPortManager::CMD_GET_PARA_CFG, true);
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


void SerialPortManager::initializeSerialPort(const QString& availablePort){
    if(ready) return;
    qCDebug(log_core_serial) << "Initialize port... ";

    if (serialPort != nullptr && serialPort->isOpen()) {
        closePort();
    }

    if (availablePort == nullptr) {
        qCDebug(log_core_serial) << "No port available.";
        QThread::sleep(1);
        return;
    }

    prepareSerialPort(availablePort);
}

void SerialPortManager::resetSerialPort(){
    qCDebug(log_core_serial) << "resetSerialPort: " << serialPort;

    QThread::sleep(1);
    resetSerialPort();
    QThread::sleep(1);
    closePort();
    ready=false;
}

void SerialPortManager::observerSerialPortNotification(){
    qCDebug(log_core_serial) << "Created a timer to observer SerialPort...";

    QThread *thread = new QThread;
    QTimer *timer = new QTimer(0); // parent must be nullptr
    timer->moveToThread(thread);

    connect(thread, &QThread::started, timer, [this, timer]() {
        connect(timer, &QTimer::timeout, this, &SerialPortManager::checkSerialPort);
        timer->start(2000); // 5000 ms = 5 s
    });

    connect(thread, &QThread::finished, timer, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);

    thread->start();
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
    qCDebug(log_core_serial) << "Destroy serial port manager.";
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
    if(serialPort == nullptr){
        serialPort = new QSerialPort();
    }
    serialPort->setPortName(portName);
    serialPort->setBaudRate(baudRate);
    if (serialPort->open(QIODevice::ReadWrite)) {
        qCDebug(log_core_serial) << "Open port" << portName + ", baudrate: " << baudRate;
        if(eventCallback!=nullptr) eventCallback->onPortConnected(portName);
        return true;
    } else {
        return false;
    }
}

void SerialPortManager::closePort() {
    qCDebug(log_core_serial) << "Close serial port";
    if (serialPort != nullptr && serialPort->isOpen()) {
        serialPort->flush();
        serialPort->clear();
        serialPort->clearError();
        qCDebug(log_core_serial) << "Unregister obseration of the data ready";
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
            int checkedBaudrate = 0;
            uint8_t mode = 0;
            switch (code)
            {
            case 0x88:
                // get parameter configuration
                // baud rate 8...11 bytes
                checkedBaudrate = ((unsigned char)data[8] << 24) | ((unsigned char)data[9] << 16) | ((unsigned char)data[10] << 8) | (unsigned char)data[11];
                mode = data[5];

                qCDebug(log_core_serial) << "Current serial port baudrate rate:" << checkedBaudrate << ", Mode:" << "0x" + QString::number(mode, 16);
                if (checkedBaudrate == SerialPortManager::DEFAULT_BAUDRATE && mode == 0x82) {
                    qCDebug(log_core_serial) << "Serial is ready for communication.";
                    ready = true;
                    if(eventCallback!=nullptr){
                        eventCallback->onPortConnected(serialPort->portName());
                    }
                }else{
                    resetHidChip();
                    QThread::sleep(1);
                    sendResetCommand();
                    closePort();
                    ready=false;
                }
                baudrate = checkedBaudrate;
                break;
            case 0x84:
                qCDebug(log_core_serial) << "Absolute mouse event sent, status" << data[5];
                break;
            case 0x85:
                qCDebug(log_core_serial) << "Relative mouse event sent, status" << data[5];
                break;
            default:
                qCDebug(log_core_serial) << "Unknown command: " << data.toHex(' ');
                break;
            }
        }
    }
    emit dataReceived(data);
}

void SerialPortManager::resetHidChip()
{
    qCDebug(log_core_serial) << "Reset to baudrate to 115200 and mode 0x82";
    // replace the data with set parameter configuration prefix
    QByteArray command = SerialPortManager::CMD_SET_PARA_CFG_PREFIX;
    //append from date 12...31
    command.append(SerialPortManager::CMD_SET_PARA_CFG_MID);
    sendCommand(command, true);
}

void SerialPortManager::aboutToClose()
{
    qCDebug(log_core_serial) << "aboutToClose";
}

void SerialPortManager::bytesWritten(qint64 nBytes){
    qCDebug(log_core_serial) << nBytes << "bytesWritten";
}

bool SerialPortManager::writeData(const QByteArray &data) {
    if (serialPort->isOpen()) {
        serialPort->write(data);
        qCDebug(log_core_serial) << "Data written to serial port:" << data.toHex(' ')
                                 << ", Baud rate: " << serialPort->baudRate();
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

bool SerialPortManager::sendResetCommand(){
    return sendCommand(SerialPortManager::CMD_RESET, true);
}

quint8 SerialPortManager::calculateChecksum(const QByteArray &data) {
    quint32 sum = 0;
    for (auto byte : data) {
        sum += static_cast<unsigned char>(byte);
    }
    return sum % 256;
}
