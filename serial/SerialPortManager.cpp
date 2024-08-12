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

SerialPortManager::SerialPortManager(QObject *parent) : QObject(parent), serialThread(new QThread(nullptr)), serialTimer(new QTimer(nullptr)){
    qCDebug(log_core_serial) << "Initialize serial port.";

    connect(this, &SerialPortManager::serialPortConnected, this, &SerialPortManager::onSerialPortConnected);
    connect(this, &SerialPortManager::serialPortDisconnected, this, &SerialPortManager::onSerialPortDisconnected);
    connect(this, &SerialPortManager::serialPortConnectionSuccess, this, &SerialPortManager::onSerialPortConnectionSuccess);

    observeSerialPortNotification();
}

void SerialPortManager::observeSerialPortNotification(){
    qCDebug(log_core_serial) << "Created a timer to observer SerialPort...";

    serialTimer->moveToThread(serialThread);

    connect(serialThread, &QThread::started, serialTimer, [this]() {
        connect(serialTimer, &QTimer::timeout, this, &SerialPortManager::checkSerialPort);
        serialTimer->start(2000); // 2000 ms = 2 s
    });

    connect(serialThread, &QThread::finished, serialTimer, &QObject::deleteLater);
    connect(serialThread, &QThread::finished, serialThread, &QObject::deleteLater);

    serialThread->start();
}

void SerialPortManager::checkSerialPorts() {
#ifdef __linux__
    QList<QString> acceptedPorts = {"USB Serial", "USB2.0-Serial"};
#elif _WIN32
    QList<QString> acceptedPorts = {"USB-SERIAL CH340"};
#endif

    QSet<QString> currentPorts;
    const auto ports = QSerialPortInfo::availablePorts();
    for (const QSerialPortInfo &port : ports) {
        if (acceptedPorts.contains(port.description())) {
            currentPorts.insert(port.portName());
        }
    }

    // Detect newly connected ports
    for (const QString &portName : currentPorts) {
        if (!availablePorts.contains(portName)) {
            emit serialPortConnected(portName);
        }
    }

    // Detect disconnected ports
    for (const QString &portName : availablePorts) {
        if (!currentPorts.contains(portName)) {
            emit serialPortDisconnected(portName);
        }
    }

    // Update the availablePorts set
    availablePorts = currentPorts;
}

/*
 * Check the serial port connection status
 */
void SerialPortManager::checkSerialPort() {
    qCDebug(log_core_serial) << "Check serial port.";

    // Check if any new ports is connected, compare to the last port list
    checkSerialPorts();
}
/*
 * Open the serial port and check the baudrate and mode
 */
void SerialPortManager::onSerialPortConnected(const QString &portName){
    // Use synchronous method to check the serial port
    qCDebug(log_core_serial) << "Serial port connected: " << portName << "baudrate:" << DEFAULT_BAUDRATE;
    openPort(portName, DEFAULT_BAUDRATE);
    // send a command to get the parameter configuration with 115200 baudrate
    QByteArray retBtye = sendSyncCommand(CMD_GET_PARA_CFG, true);
    CmdDataParamConfig config;
    if(retBtye.size() > 0){
        qCDebug(log_core_serial) << "Data read from serial port: " << retBtye.toHex(' ');
        config = CmdDataParamConfig::fromByteArray(retBtye);
        if(config.mode == 0x82){ // the default mode is correct, TODO store the default mode to config in future
            ready = true;
        } else { // the mode is not correct, need to re-config the chip
            //TODO
            qCWarning(log_core_serial) << "The mode is incorrect, mode:" << config.mode;
        }
    } else { // try 9600 baudrate
        qCDebug(log_core_serial) << "No data with 115200 baudrate, try to connect: " << portName << "with baudrate:" << ORIGINAL_BAUDRATE;
        closePort();
        openPort(portName, ORIGINAL_BAUDRATE);
        QByteArray retBtye = sendSyncCommand(CMD_GET_PARA_CFG, true);

        if(retBtye.size() > 0){
            config = CmdDataParamConfig::fromByteArray(retBtye);
            qCDebug(log_core_serial) << "Connect success with baudrate: " << ORIGINAL_BAUDRATE;
            qCDebug(log_core_serial) << "Current working mode is:" << "0x" + QString::number(config.mode, 16);

            qCDebug(log_core_serial) << "Reconfigure to baudrate to 115200 and mode 0x82";

            if(reconfigureHidChip()) {
                if(resetHipChip()){
                    QThread::sleep(1);
                    closePort();
                    openPort(portName, DEFAULT_BAUDRATE);
                    qCDebug(log_core_serial) << "Reopen the serial port with baudrate: " << DEFAULT_BAUDRATE;
                }else{
                    qCWarning(log_core_serial) << "Reset the hid chip fail...";
                }
            }else{
                qCWarning(log_core_serial) << "Set data config fail, reset the serial port now...";
                QThread::sleep(1);
                closePort();
                openPort(portName, DEFAULT_BAUDRATE);
                ready = false;
                qCDebug(log_core_serial) << "Reopen the serial port with baudrate: " << DEFAULT_BAUDRATE;
            }
        }
    }

    qCDebug(log_core_serial) << "Check serial port completed.";
    emit serialPortConnectionSuccess(portName);
}

/*
 * Close the serial port
 */
void SerialPortManager::onSerialPortDisconnected(const QString &portName){
    qCDebug(log_core_serial) << "Serial port disconnected: " << portName;
    if(ready){
        closePort();
    }
}

/*
 * Serial port connection success, connect the data ready and bytes written signal
 */
void SerialPortManager::onSerialPortConnectionSuccess(const QString &portName){
    qCDebug(log_core_serial) << "Serial port connection success: " << portName;

    // Async handle the keyboard and mouse events
    qCDebug(log_core_serial) << "Observe" << portName << "data ready and bytes written.";
    connect(serialPort, &QSerialPort::readyRead, this, &SerialPortManager::readData);
    connect(serialPort, &QSerialPort::bytesWritten, this, &SerialPortManager::bytesWritten);
    ready = true;
}

void SerialPortManager::setEventCallback(StatusEventCallback* callback) {
    eventCallback = callback;
}

/* 
 * Reset the hid chip
 */
bool SerialPortManager::resetHipChip(){
    qCDebug(log_core_serial) << "Reset Hid chip now...";
    eventCallback->onPortConnected("Reset Hid chip now...");

    QByteArray retByte = sendSyncCommand(CMD_RESET, true);
    if (retByte.size() > 0) {
        qCDebug(log_core_serial) << "Reset the hid chip success.";
        eventCallback->onPortConnected("Reset Hid chip success.");
        return true;
    }
    return false;
}

/*
 * Factory reset the hid chip by holding the RTS pin to low for 4 seconds
 */
bool SerialPortManager::factoryResetHipChip(){
    qCDebug(log_core_serial) << "Factory reset Hid chip now...";

    if(serialPort->setRequestToSend(true)){
        qCDebug(log_core_serial) << "Set RTS to low";
        QThread::sleep(4);
        if(serialPort->setRequestToSend(false)){
            qCDebug(log_core_serial) << "Set RTS to high";

            QString portName = serialPort->portName();
            restartPort();
            emit serialPortConnectionSuccess(portName);
        }
    }
    return false;
}

/*
 * Destructor
 */
SerialPortManager::~SerialPortManager() {
    qCDebug(log_core_serial) << "Destroy serial port manager.";
    closePort();

    if (serialThread->isRunning()) {
        serialThread->quit();
        serialThread->wait();
    }
    delete serialTimer;
    delete serialThread;
    delete serialPort;
}

/*
 * Open the serial port
 */
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
        serialPort->setRequestToSend(false);
        qCDebug(log_core_serial) << "RTS:" << serialPort->isRequestToSend();

        if(eventCallback!=nullptr) eventCallback->onPortConnected(portName);
        return true;
    } else {
        return false;
    }
}

/*
 * Close the serial port
 */
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
        if(eventCallback!=nullptr) eventCallback->onPortConnected("Going to close the port");
    } else {
        qCDebug(log_core_serial) << "Serial port is not opened.";
    }
}

bool SerialPortManager::restartPort() {
    QString portName = serialPort->portName();
    qint32 baudRate = serialPort->baudRate();
    closePort();
    openPort(portName, baudRate);
    onSerialPortConnected(portName);
    return ready;
}

/*
 * Read the data from the serial port
 */
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
                    serialPort->setBaudRate(checkedBaudrate);
                    if(eventCallback!=nullptr){
                        eventCallback->onPortConnected(serialPort->portName());
                    }
                }else{
                    reconfigureHidChip();
                    QThread::sleep(1);
                    sendResetCommand();
                    ready=false;
                }
                //baudrate = checkedBaudrate;
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

/*
 * Reconfigure the HID chip to the default baudrate and mode
 */
bool SerialPortManager::reconfigureHidChip()
{
    qCDebug(log_core_serial) << "Reset to baudrate to 115200 and mode 0x82";
    // replace the data with set parameter configuration prefix
    QByteArray command = CMD_SET_PARA_CFG_PREFIX;
    //append from date 12...31
    command.append(CMD_SET_PARA_CFG_MID);
    QByteArray retBtyes = sendSyncCommand(command, true);
    if(retBtyes.size() > 0){
        CmdDataResult dataResult = fromByteArray<CmdDataResult>(retBtyes);
        qCDebug(log_core_serial) << "Set data config result: " << dataResult.data;
        if(dataResult.data == DEF_CMD_SUCCESS){
            qCDebug(log_core_serial) << "Set data config success, reconfig to 115200 baudrate and mode 0x82";
            return true;
        } 
    }

    return false;
}

/*
 * About to close the serial port
 */
void SerialPortManager::aboutToClose()
{
    qCDebug(log_core_serial) << "aboutToClose";
}

/*
 * Bytes written to the serial port
 */
void SerialPortManager::bytesWritten(qint64 nBytes){
    qCDebug(log_core_serial) << nBytes << "bytesWritten";
}

/*
 * Write the data to the serial port
 */
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

/*
 * Send the async command to the serial port
 */
bool SerialPortManager::sendAsyncCommand(const QByteArray &data, bool force) {
    if(!force && !ready) return false;
    QByteArray command = data;
    command.append(calculateChecksum(command));
    return writeData(command);
}

/*
 * Send the sync command to the serial port
 */
QByteArray SerialPortManager::sendSyncCommand(const QByteArray &data, bool force) {
    if(!force && !ready) return QByteArray();
    QByteArray command = data;
    command.append(calculateChecksum(command));
    writeData(command);
    if (serialPort->waitForReadyRead(1000)) {
        QByteArray responseData = serialPort->readAll();
        while (serialPort->waitForReadyRead(100))
            responseData += serialPort->readAll();
        return responseData;
    }
    return QByteArray();;
}

/*
 * Send the reset command to the serial port
 */
bool SerialPortManager::sendResetCommand(){
    return sendAsyncCommand(CMD_RESET, true);
}

quint8 SerialPortManager::calculateChecksum(const QByteArray &data) {
    quint32 sum = 0;
    for (auto byte : data) {
        sum += static_cast<unsigned char>(byte);
    }
    return sum % 256;
}
