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
#include "../ui/globalsetting.h"

#include <QSerialPortInfo>
#include <QTimer>
#include <QtConcurrent>
#include <QFuture>
#include <QtSerialPort>
#include <QElapsedTimer>


Q_LOGGING_CATEGORY(log_core_serial, "opf.core.serial")

// Define static constants
const int SerialPortManager::ORIGINAL_BAUDRATE;
const int SerialPortManager::DEFAULT_BAUDRATE;
const int SerialPortManager::SERIAL_TIMER_INTERVAL;

SerialPortManager::SerialPortManager(QObject *parent) : QObject(parent), serialPort(nullptr), serialThread(new QThread(nullptr)), serialTimer(new QTimer(nullptr)){
    qCDebug(log_core_serial) << "Initialize serial port.";

    // Initialize device manager
    m_deviceManager = new DeviceManager(this);
    connect(m_deviceManager, &DeviceManager::deviceAdded, this, &SerialPortManager::onDeviceAdded);
    connect(m_deviceManager, &DeviceManager::deviceRemoved, this, &SerialPortManager::onDeviceRemoved);
    connect(m_deviceManager, &DeviceManager::deviceModified, this, &SerialPortManager::onDeviceModified);

    // Initialize hotplug monitor with callbacks
    m_hotplugMonitor = new HotplugMonitor(m_deviceManager, this);
    
    // Add callback for detailed device change handling
    m_hotplugMonitor->addCallback([this](const DeviceChangeEvent& event) {
        onHotplugDeviceChangeEvent(event);
    });
    
    connect(this, &SerialPortManager::serialPortConnected, this, &SerialPortManager::onSerialPortConnected);
    connect(this, &SerialPortManager::serialPortDisconnected, this, &SerialPortManager::onSerialPortDisconnected);
    connect(this, &SerialPortManager::serialPortConnectionSuccess, this, &SerialPortManager::onSerialPortConnectionSuccess);

    observeSerialPortNotification();
    m_lastCommandTime.start();
    m_commandDelayMs = 0;  // Default no delay
    lastSerialPortCheckTime = QDateTime::currentDateTime().addMSecs(-SERIAL_TIMER_INTERVAL);  // Initialize check time in the past 
    
    // Start enhanced hotplug monitoring with both legacy and new systems
    m_deviceManager->startHotplugMonitoring();
    m_hotplugMonitor->start(2000); // Check every 2 seconds
    
    qCDebug(log_core_serial) << "SerialPortManager initialized with enhanced hotplug detection";
}

void SerialPortManager::observeSerialPortNotification(){
    qCDebug(log_core_serial) << "Created a timer to observer SerialPort...";

    serialTimer->moveToThread(serialThread);

    connect(serialThread, &QThread::started, serialTimer, [this]() {
        connect(serialTimer, &QTimer::timeout, this, &SerialPortManager::checkSerialPort);
        checkSerialPort();
        serialTimer->start(SERIAL_TIMER_INTERVAL);
    });

    connect(serialThread, &QThread::finished, serialTimer, &QObject::deleteLater);
    connect(serialThread, &QThread::finished, serialThread, &QObject::deleteLater);
    connect(this, &SerialPortManager::sendCommandAsync, this, &SerialPortManager::sendCommand);
    
    serialThread->start();
}

void SerialPortManager::stop() {
    qCDebug(log_core_serial) << "Stopping serial port manager thread...";
    
    // Stop enhanced hotplug monitoring
    if (m_hotplugMonitor) {
        m_hotplugMonitor->stop();
    }
    
    // Stop device monitoring
    if (m_deviceManager) {
        m_deviceManager->stopHotplugMonitoring();
    }
    
    if (serialTimer) {
        serialTimer->stop();
    }

    if (serialThread && serialThread->isRunning()) {
        closePort();
        
        serialThread->quit();

        const int maxWaitTime = 10000; 
        if (!serialThread->wait(maxWaitTime)) {
            qCWarning(log_core_serial) << "Thread did not terminate within" << maxWaitTime << "ms - thread may be blocked";
        }
    }

    qCDebug(log_core_serial) << "Serial port manager thread stopped";
}

void SerialPortManager::checkSerialPorts() {
    if(ready) return;
    
    QSet<QString> currentPorts;
    const auto ports = QSerialPortInfo::availablePorts();
    for (const QSerialPortInfo &port : ports) {
        QString vidHex = port.hasVendorIdentifier() ? 
            QString("0x%1").arg(port.vendorIdentifier(), 4, 16, QChar('0')).toUpper() : 
            "N/A";
        QString pidHex = port.hasProductIdentifier() ? 
            QString("0x%1").arg(port.productIdentifier(), 4, 16, QChar('0')).toUpper() : 
            "N/A";
            
        qCDebug(log_core_serial) << "Search port name" << port.portName() << "Manufacturer:" << port.manufacturer() 
                 << "VID:" << vidHex << "PID:" << pidHex;
        
        // Match specific VID:PID {0x1A86, 0x7523}
        if (port.hasVendorIdentifier() && port.hasProductIdentifier() &&
            port.vendorIdentifier() == 0x1A86 && port.productIdentifier() == 0x7523) {
            currentPorts.insert(port.portName());
            qCDebug(log_core_serial) << "Matched port name" << port.portName() << "with VID:PID 0x1A86:0x7523";

            if(serialPort == nullptr) {
                qCDebug(log_core_serial) << "The serial port is nullptr create a new serial port instance";
                emit serialPortConnected(port.portName());
            }else if (!serialPort->isOpen()){
                qCDebug(log_core_serial) << "The serial port is not open create a new serial port instance";
                emit serialPortConnected(port.portName());
            }
        }
    }

    // Detect newly connected ports
    for (const QString &portName : currentPorts) {
        qCDebug(log_core_serial) << "Current port name:" << portName;
        if (!availablePorts.contains(portName)) {
            qCDebug(log_core_serial) << "New port connected: " << portName;
            emit serialPortConnected(portName);
        }
    }

    // Detect disconnected ports
    for (auto it = availablePorts.constBegin(); it != availablePorts.constEnd(); ++it) {
        const QString &portName = *it;
        if (!currentPorts.contains(portName)) {
            qCDebug(log_core_serial) << "Disconnect port: " << portName;
            emit serialPortDisconnected(portName);
        }
    }

    // Update the availablePorts set
    availablePorts = currentPorts;
}

// void SerialPortManager::checkSwitchableUSB(){
//     // For hardware 1.9, using MS2109 GPIO 0 to read the hard USB toggle switch state
//     // TODO skip this checking for 1.9
//     if(serialPort){
//         bool newIsSwitchToHost = serialPort->pinoutSignals() & QSerialPort::DataSetReadySignal;
//         if(newIsSwitchToHost){
//             qCDebug(log_core_serial) << "USB switch is connecting to host, original connected to" << (isSwitchToHost?"host":"target");
//         }else{
//             qCDebug(log_core_serial) << "USB switch is connecting to target, original connected to" << (isSwitchToHost?"host":"target");
//         }

//         if(isSwitchToHost!=newIsSwitchToHost){
//             qCDebug(log_core_serial) << "USB switch status changed, toggle the switch";
//             if (isSwitchToHost) {
//                 // Set the RTS pin to high(pin inverted) to connect to the target
//                 serialPort->setRequestToSend(false);
//             }else{
//                 // Set the RTS pin to low(pin inverted) to connect to the host
//                 serialPort->setRequestToSend(true);
//             }
//             isSwitchToHost = newIsSwitchToHost;
//             restartSwitchableUSB();
//         }
//     }
// }

/*
 * Check the serial port connection status
 * This method now works alongside the enhanced hotplug detection system
 */
void SerialPortManager::checkSerialPort() {
    QDateTime currentTime = QDateTime::currentDateTime();
    if (lastSerialPortCheckTime.isValid() && lastSerialPortCheckTime.msecsTo(currentTime) < SERIAL_TIMER_INTERVAL) {
        return;
    }
    lastSerialPortCheckTime = currentTime;
    qCDebug(log_core_serial) << "Check serial port (legacy method alongside enhanced hotplug)";

    // Only use legacy detection if enhanced system is not running
    if (!m_hotplugMonitor || !m_hotplugMonitor->isRunning()) {
        qCDebug(log_core_serial) << "Using legacy port detection as fallback";
        checkSerialPorts();
    } else {
        qCDebug(log_core_serial) << "Enhanced hotplug detection is active - skipping legacy port scan";
    }

    if(ready){
        if (isTargetUsbConnected){
            // Check target connection status when no data received in 3 seconds
            if (latestUpdateTime.secsTo(QDateTime::currentDateTime()) > 3) {
                ready = sendAsyncCommand(CMD_GET_INFO, false);
            }
        }else {
            sendAsyncCommand(CMD_GET_INFO, false);
            // ready = false;
        }
    }

    // If no data received in 5 seconds, check if any port disconnected
    // Because the connection will regularily check every 3 seconds, if not data received
    // is received, consider the port is disconnected or not working
    sendSyncCommand(CMD_GET_INFO, false);
    if (latestUpdateTime.secsTo(QDateTime::currentDateTime()) > 5) {
        ready = false;
    }
}

/*
 * Open the serial port and check the baudrate and mode
 */
void SerialPortManager::onSerialPortConnected(const QString &portName){
    // Use synchronous method to check the serial port
    qCDebug(log_core_serial) << "Serial port connected: " << portName << "baudrate:" << DEFAULT_BAUDRATE;
    // Check if the port was successfully opened
    const int maxRetries = 2;
    int retryCount = 0;
    bool openSuccess = openPort(portName, DEFAULT_BAUDRATE);
    while (retryCount < maxRetries && !openSuccess) {
        qCWarning(log_core_serial) << "Failed to open serial port: " << portName;
        // Check if the port is still open (in case of partial failure)
        if (serialPort->isOpen()) {
            qCDebug(log_core_serial) << "Port is still open, closing it before retry";
            closePort();
        }
        QThread::msleep(500 * (retryCount + 1));
        retryCount++;
        // Retry opening the port
        qCDebug(log_core_serial) << "Retrying to open serial port: " << portName << "baudrate:" << DEFAULT_BAUDRATE;
        openSuccess = openPort(portName, DEFAULT_BAUDRATE);
    }
    if (!openSuccess) {
        qCWarning(log_core_serial) << "Retry failed to open serial port: " << portName;
        return; // Exit if retry also fails
    }

    // send a command to get the parameter configuration with 115200 baudrate
    QByteArray retBtye = sendSyncCommand(CMD_GET_PARA_CFG, true);
    CmdDataParamConfig config;
    static QSettings settings("Techxartisan", "Openterface");
    uint8_t mode = (settings.value("hardware/operatingMode", 0x02).toUInt());
    if(retBtye.size() > 0){
        qDebug() << "Data read from serial port: " << retBtye.toHex(' ');
        config = CmdDataParamConfig::fromByteArray(retBtye);
        if(config.mode == mode){ 
            ready = true;
            qCDebug(log_core_serial) << "Connect success with baudrate: " << DEFAULT_BAUDRATE << ready;
        } else { // the mode is not correct, need to re-config the chip
            qCWarning(log_core_serial) << "The mode is incorrect, mode:" << config.mode;
            resetHipChip();
        }
    } else { // try 9600 baudrate
        qCDebug(log_core_serial) << "No data with 115200 baudrate, try to connect: " << portName << "with baudrate:" << ORIGINAL_BAUDRATE;
        closePort();
        openPort(portName, ORIGINAL_BAUDRATE);
        QByteArray retBtye = sendSyncCommand(CMD_GET_PARA_CFG, true);
        qDebug() << "Data read from serial port with 9600: " << retBtye.toHex(' ');
        if(retBtye.size() > 0){
            config = CmdDataParamConfig::fromByteArray(retBtye);
            qCDebug(log_core_serial) << "Connect success with baudrate: " << ORIGINAL_BAUDRATE;
            qCDebug(log_core_serial) << "Current working mode is:" << "0x" + QString::number(config.mode, 16);

            resetHipChip();
        }
    }

    qCDebug(log_core_serial) << "Check serial port completed.";
    emit serialPortConnectionSuccess(portName); 
}

/*
 * Close the serial port
 */
void SerialPortManager::onSerialPortDisconnected(const QString &portName){
    qCDebug(log_core_serial) << "Serial port disconnected:" << portName;
    if (serialPort) {
        qCDebug(log_core_serial) << "Last error:" << serialPort->errorString();
        qCDebug(log_core_serial) << "Port state:" << (serialPort->isOpen() ? "Open" : "Closed");
    }
    if (ready) {
        closePort();
        availablePorts.remove(portName);
        QThread::msleep(500);
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

    if(eventCallback!=nullptr) eventCallback->onPortConnected(portName, serialPort->baudRate());

    qCDebug(log_core_serial) << "Enable the switchable USB now...";
    // serialPort->setDataTerminalReady(false);

    sendSyncCommand(CMD_GET_INFO, true);
}

void SerialPortManager::setEventCallback(StatusEventCallback* callback) {
    eventCallback = callback;
}

/* 
 * Reset the hid chip, set the baudrate to 115200 and mode to 0x82 and reset the chip
 */
bool SerialPortManager::resetHipChip(){
    QString portName = serialPort->portName();
    if(reconfigureHidChip()) {
        if(sendResetCommand()){
            qCDebug(log_core_serial) << "Reopen the serial port with baudrate: " << DEFAULT_BAUDRATE;
            setBaudRate(DEFAULT_BAUDRATE);
            restartPort();
            return true;
        }else{
            qCWarning(log_core_serial) << "Reset the hid chip fail...";
            return false;
        }
    }else{
        qCWarning(log_core_serial) << "Set data config fail, reset the serial port now...";
        restartPort();
        ready = false;
        qCDebug(log_core_serial) << "Reopen the serial port with baudrate: " << DEFAULT_BAUDRATE;
        return false;
    }
}



/*
 * Send the reset command to the hid chip
 */
bool SerialPortManager::sendResetCommand(){
    QByteArray retByte = sendSyncCommand(CMD_RESET, true);
    if(retByte.size() > 0){
        qCDebug(log_core_serial) << "Reset the hid chip success.";
        return true;
    } else{
        qCDebug(log_core_serial) << "Reset the hid chip fail.";
        return false;
    }
}

/*
 * Supported hardware 1.9 and > 1.9.1
 * Factory reset the hid chip by holding the RTS pin to low for 4 seconds
 */
bool SerialPortManager::factoryResetHipChip(){
    qCDebug(log_core_serial) << "Factory reset Hid chip now...";

    if(serialPort->setRequestToSend(true)){
        eventCallback->factoryReset(true);
        qCDebug(log_core_serial) << "Set RTS to low";
        QTimer::singleShot(4000, this, [this]() {
            if (serialPort->setRequestToSend(false)) {
                qCDebug(log_core_serial) << "Set RTS to high";
                eventCallback->factoryReset(false);
                restartPort();
            }
        });
    }
    return false;
}

/*
 * Supported hardware == 1.9.1
 * Factory reset the hid chip by sending set default cfg command
 */
bool SerialPortManager::factoryResetHipChipV191(){
    qCDebug(log_core_serial) << "Factory reset Hid chip for 1.9.1 now...";
    if(eventCallback) eventCallback->onStatusUpdate("Factory reset Hid chip now.");

    QByteArray retByte = sendSyncCommand(CMD_SET_DEFAULT_CFG, true);
    if (retByte.size() > 0) {
        qCDebug(log_core_serial) << "Factory reset the hid chip success.";
        if(eventCallback) eventCallback->onStatusUpdate("Factory reset the hid chip success.");
        return true;
    } else{
        qCDebug(log_core_serial) << "Factory reset the hid chip fail.";
        // toggle to another baudrate
        serialPort->close();
        setBaudRate(ORIGINAL_BAUDRATE);
        if(eventCallback) eventCallback->onStatusUpdate("Factory reset the hid chip@9600.");
        if(serialPort->open(QIODevice::ReadWrite)){
            QByteArray retByte = sendSyncCommand(CMD_SET_DEFAULT_CFG, true);
            if (retByte.size() > 0) {
                qCDebug(log_core_serial) << "Factory reset the hid chip success.";
                if(eventCallback) eventCallback->onStatusUpdate("Factory reset the hid chip success@9600.");
                return true;
            }
        }
    }
    if(eventCallback) eventCallback->onStatusUpdate("Factory reset the hid chip failure.");
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
    if(eventCallback!=nullptr) eventCallback->onStatusUpdate("Going to open the port");
    if(serialPort == nullptr){
        serialPort = new QSerialPort();
    }
    serialPort->setPortName(portName);
    serialPort->setBaudRate(baudRate);
    if (serialPort->open(QIODevice::ReadWrite)) {
        qCDebug(log_core_serial) << "Open port" << portName + ", baudrate: " << baudRate;
        serialPort->setRequestToSend(false);

        if(eventCallback!=nullptr) eventCallback->onStatusUpdate("");
        if(eventCallback!=nullptr) eventCallback->onPortConnected(portName, baudRate);
        return true;
    } else {
        if(eventCallback!=nullptr) eventCallback->onStatusUpdate("Open port failure");
        return false;
    }
}

/*
 * Close the serial port
 */
void SerialPortManager::closePort() {
    qCDebug(log_core_serial) << "Close serial port";
    if (serialPort != nullptr) {
        if (serialPort->isOpen()) {
            disconnect(serialPort, &QSerialPort::readyRead, this, &SerialPortManager::readData);
            disconnect(serialPort, &QSerialPort::bytesWritten, this, &SerialPortManager::bytesWritten);
            serialPort->flush();
            serialPort->clear();
            serialPort->clearError();
            serialPort->close();
        }
        delete serialPort;
        serialPort = nullptr;
    } else {
        qCDebug(log_core_serial) << "Serial port is not opened.";
    }
    ready = false;
    if (eventCallback != nullptr) {
        eventCallback->onPortConnected("NA", 0);
    }
    QThread::msleep(300);
}

bool SerialPortManager::restartPort() {
    QString portName = serialPort->portName();
    qint32 baudRate = serialPort->baudRate();
    qDebug() << "Restart port" << portName << "baudrate:" << baudRate;
    eventCallback->serialPortReset(true);
    closePort();
    QThread::msleep(100);
    openPort(portName, baudRate);
    onSerialPortConnected(portName);
    eventCallback->serialPortReset(false);
    return ready;
}


void SerialPortManager::updateSpecialKeyState(uint8_t data){

    qCDebug(log_core_serial) << "Data received: " << data;
    NumLockState = (data & 0b00000001) != 0; // NumLockState bit
    CapsLockState = (data & 0b00000010) != 0; // CapsLockState bit
    ScrollLockState = (data & 0b00000100) != 0; // ScrollLockState bit
    
}
/*
 * Read the data from the serial port
 */
void SerialPortManager::readData() {
    QByteArray data = serialPort->readAll();
    if (data.size() >= 6) {

        unsigned char status = data[5];
        unsigned char cmdCode = data[3];

        if(status != DEF_CMD_SUCCESS && (cmdCode >= 0xC0 && cmdCode <= 0xCF)){
            dumpError(status, data);
        }
        else{
            qCDebug(log_core_serial) << "Receive from serial port @" << serialPort->baudRate() << ":" << data.toHex(' ');
            static QSettings settings("Techxartisan", "Openterface");
            latestUpdateTime = QDateTime::currentDateTime();
            ready = true;
            unsigned char code = cmdCode | 0x80;
            int checkedBaudrate = 0;
            uint8_t mode = (settings.value("hardware/operatingMode", 0x02).toUInt());
            uint8_t chip_mode = data[5];
            switch (code)
            {
            case 0x81:
                isTargetUsbConnected = CmdGetInfoResult::fromByteArray(data).targetConnected == 0x01;
                eventCallback->onTargetUsbConnected(isTargetUsbConnected);
                updateSpecialKeyState(CmdGetInfoResult::fromByteArray(data).indicators);
                break;
            case 0x82:
                qCDebug(log_core_serial) << "Keyboard event sent, status" << statusCodeToString(data[5]);
                break;
            case 0x84:
                qCDebug(log_core_serial) << "Absolute mouse event sent, status" << statusCodeToString(data[5]);
                break;
            case 0x85:
                qCDebug(log_core_serial) << "Relative mouse event sent, status" << statusCodeToString(data[5]);
                break;
            case 0x88:
                // get parameter configuration
                // baud rate 8...11 bytes
                checkedBaudrate = ((unsigned char)data[8] << 24) | ((unsigned char)data[9] << 16) | ((unsigned char)data[10] << 8) | (unsigned char)data[11];

                qCDebug(log_core_serial) << "Current serial port baudrate rate:" << checkedBaudrate << ", Mode:" << "0x" + QString::number(mode, 16);
                if (checkedBaudrate == SerialPortManager::DEFAULT_BAUDRATE && chip_mode == mode) {
                    qCDebug(log_core_serial) << "Serial is ready for communication.";
                    setBaudRate(checkedBaudrate);
                }else{
                    qCDebug(log_core_serial) << "Serial is not ready for communication.";
                    //reconfigureHidChip();
                    QThread::sleep(1);
                    resetHipChip();
                    ready=false;
                }
                //baudrate = checkedBaudrate;
                break;
            default:
                qCDebug(log_core_serial) << "Unknown command: " << data.toHex(' ');
                break;
            }
        }
    }
    // qDebug() << "Recv read" << data;
    emit dataReceived(data);
}

QString SerialPortManager::statusCodeToString(uint8_t status) {
    switch (status) {
        case 0x00:
            return "Success"; 
        case 0xE1:
            return "Serial port recived one byte timeout";
        case 0xE2:
            return "Serial port recived package frist byte error";
        case 0xE3:
            return "Serial port recived command code error";
        case 0xE4:
            return "Serial port recived package checksum error";
        case 0xE5:
            return "Command parameter error";
        case 0xE6:
            return "The data frame failed to execute properly";
    } 
}

/*
 * Reconfigure the HID chip to the default baudrate and mode
 */
bool SerialPortManager::reconfigureHidChip()
{
    static QSettings settings("Techxartisan", "Openterface");
    uint8_t mode = (settings.value("hardware/operatingMode", 0x02).toUInt());
    qCDebug(log_core_serial) << "Reconfigure to baudrate to 115200 and mode 0x" << QString::number(mode, 16);
    // replace the data with set parameter configuration prefix
    QByteArray command = CMD_SET_PARA_CFG_PREFIX;
    command[5] = mode;  // Set mode byte at index 5 (6th byte)

    //append from date 12...31
    command.append(CMD_SET_PARA_CFG_MID);
    QByteArray retBtyes = sendSyncCommand(command, true);
    if(retBtyes.size() > 0){
        CmdDataResult dataResult = fromByteArray<CmdDataResult>(retBtyes);
        if(dataResult.data == DEF_CMD_SUCCESS){
            qCDebug(log_core_serial) << "Set data config success, reconfig to 115200 baudrate and mode 0x" << QString::number(mode, 16);
            return true;
        }else{
            qWarning() << "Set data config fail.";
            dumpError(dataResult.data, retBtyes);
        } 
    }else{
        qWarning() << "Set data config response empty, response:" << retBtyes.toHex(' ');
    }

    return false;
}

/*
 * Bytes written to the serial port
 */
void SerialPortManager::bytesWritten(qint64 nBytes){
    // qCDebug(log_core_serial) << nBytes << "bytesWritten";
    Q_UNUSED(nBytes);
}

/*
 * Write the data to the serial port
 */
bool SerialPortManager::writeData(const QByteArray &data) {
    if (serialPort->isOpen()) {
        serialPort->write(data);
        qCDebug(log_core_serial) << "Data written to serial port: @" + serialPort->portName() << ":" << data.toHex(' ');
        return true;
    }

    qCDebug(log_core_serial) << "Serial is not opened, cannot write data" << serialPort->portName();
    ready = false;
    return false;
}

/*
 * Send the async command to the serial port
 */
bool SerialPortManager::sendAsyncCommand(const QByteArray &data, bool force) {
    if(!force && !ready) return false;
    QByteArray command = data;
    emit dataSent(data);
    command.append(calculateChecksum(command));

    // Check if less than the configured delay has passed since the last command
    if (m_lastCommandTime.isValid() && m_lastCommandTime.elapsed() < m_commandDelayMs) {
        // Calculate remaining delay time
        int remainingDelay = m_commandDelayMs - m_lastCommandTime.elapsed();
        
        // Use QTimer::singleShot for non-blocking delay
        QTimer::singleShot(remainingDelay, this, [this, command]() {
            writeData(command);
            m_lastCommandTime.start();
        });
        
        return true;
    }

    bool result = writeData(command);
    m_lastCommandTime.start();
    return result;
}

/*
 * Send the sync command to the serial port
 */
QByteArray SerialPortManager::sendSyncCommand(const QByteArray &data, bool force) {
    if(!force && !ready) return QByteArray();
    // qDebug() << "Data received signal emitted";
    emit dataSent(data);
    QByteArray command = data;
    
    command.append(calculateChecksum(command));
    qDebug() <<  "Check sum" << command.toHex(' ');
    writeData(command);
    if (serialPort->waitForReadyRead(100)) {
        QByteArray responseData = serialPort->readAll();
        while (serialPort->waitForReadyRead(100))
            responseData += serialPort->readAll();
        emit dataReceived(responseData);
        return responseData;
        
    }
    return QByteArray();
}


quint8 SerialPortManager::calculateChecksum(const QByteArray &data) {
    quint32 sum = 0;
    for (auto byte : data) {
        sum += static_cast<unsigned char>(byte);
    }
    return sum % 256;
}

/*
 * Restart the switchable USB port
 * Set the DTR to high for 0.5s to restart the USB port
 */
void SerialPortManager::restartSwitchableUSB(){
    if(serialPort){
        qCDebug(log_core_serial) << "Restart the USB port now...";
        serialPort->setDataTerminalReady(true);
        QThread::msleep(500);
        serialPort->setDataTerminalReady(false);
    }
}

/*
* Set the USB configuration
*/
void SerialPortManager::setUSBconfiguration(){
    QSettings settings("Techxartisan", "Openterface");
    QByteArray command = CMD_SET_PARA_CFG_PREFIX;

    QString VID = settings.value("serial/vid", "86 1A").toString();
    QString PID = settings.value("serial/pid", "29 E1").toString();
    QString enable = settings.value("serial/enableflag", "00").toString();

    QByteArray VIDbyte = GlobalSetting::instance().convertStringToByteArray(VID);
    QByteArray PIDbyte = GlobalSetting::instance().convertStringToByteArray(PID);
    QByteArray enableByte =  GlobalSetting::instance().convertStringToByteArray(enable);

    command.append(RESERVED_2BYTES);
    command.append(PACKAGE_INTERVAL);

    command.append(VIDbyte);
    command.append(PIDbyte);
    command.append(KEYBOARD_UPLOAD_INTERVAL);
    command.append(KEYBOARD_RELEASE_TIMEOUT);
    command.append(KEYBOARD_AUTO_ENTER);
    command.append(KEYBOARD_ENTER);
    command.append(FILTER);

    command.append(enableByte);

    command.append(SPEED_MODE);
    command.append(RESERVED_4BYTES);
    command.append(RESERVED_4BYTES);
    command.append(RESERVED_4BYTES);
    
    qDebug(log_core_serial) <<  " no checksum" << command.toHex(' ');
    if (serialPort != nullptr && serialPort->isOpen()){
        QByteArray respon = sendSyncCommand(command, true); 
        qDebug(log_core_serial) << respon;
        qDebug(log_core_serial) << " After sending command";
    } 
}

/*
 * change USB Descriptor of the device
 */
void SerialPortManager::changeUSBDescriptor() {
    QSettings settings("Techxartisan", "Openterface");
    
    QString USBDescriptors[3];
    USBDescriptors[0] = settings.value("serial/customVIDDescriptor", "www.openterface.com").toString(); // 00
    USBDescriptors[1] = settings.value("serial/customPIDDescriptor", "test").toString(); // 01
    USBDescriptors[2] = settings.value("serial/serialnumber", "1").toString(); //02
    QString enableflag = settings.value("serial/enableflag", "00").toString();
    bool bits[4];

    bool ok;    
    int hexValue = enableflag.toInt(&ok, 16);

    qDebug(log_core_serial) << "extractBits: " << hexValue;

    if (!ok) {
        qDebug(log_core_serial) << "Convert failed";
        return ; // return empty array
    }
    
    bits[0] = (hexValue >> 0) & 1;
    bits[1] = (hexValue >> 1) & 1;
    bits[2] = (hexValue >> 2) & 1;
    bits[3] = (hexValue >> 7) & 1;
    
    if (bits[3]){
        for(uint i=0; i < sizeof(bits)/ sizeof(bits[0]) -1; i++){
            if (bits[i]){
                QByteArray command = CMD_SET_USB_STRING_PREFIX;
                QByteArray tmp = USBDescriptors[i].toUtf8();
                // qDebug() << "USB descriptor:" << tmp;
                int descriptor_size = tmp.length();
                QByteArray hexLength = QByteArray::number(descriptor_size, 16).rightJustified(2, '0').toUpper();
                QByteArray hexLength_2 = QByteArray::number(descriptor_size + 2, 16).rightJustified(2, '0').toUpper();
                QByteArray descriptor_type = QByteArray::number(0, 16).rightJustified(1, '0').toUpper() + QByteArray::number(i, 16).rightJustified(1, '0').toUpper();
                
                // convert hex to binary bytes
                QByteArray hexLength_2_bin = QByteArray::fromHex(hexLength_2);
                QByteArray descriptor_type_bin = QByteArray::fromHex(descriptor_type);
                QByteArray hexLength_bin = QByteArray::fromHex(hexLength);
                
                command.append(hexLength_2_bin);
                command.append(descriptor_type_bin);
                command.append(hexLength_bin);
                command.append(tmp);

                // qDebug() <<  "usb descriptor" << command.toHex(' ');
                if (serialPort != nullptr && serialPort->isOpen()){
                    QByteArray respon = sendSyncCommand(command, true);
                    qDebug(log_core_serial) << respon;
                    qDebug(log_core_serial) << " After sending command";
                }
                qDebug() <<  "usb descriptor" << command.toHex(' ');
            }
            QThread::msleep(10);
        }
    }
}

void SerialPortManager::sendCommand(const QByteArray &command, bool waitForAck) {
    Q_UNUSED(waitForAck);
    // qCDebug(log_core_serial)  << "sendCommand:" << command.toHex(' ');
    sendAsyncCommand(command, false);

}

bool SerialPortManager::setBaudRate(int baudRate) {
    if (serialPort->baudRate() == baudRate) {
        qCDebug(log_core_serial) << "Baud rate is already set to" << baudRate;
        return true;
    }

    qCDebug(log_core_serial) << "Setting baud rate to" << baudRate;
    
    if (serialPort->setBaudRate(baudRate)) {
        qCDebug(log_core_serial) << "Baud rate successfully set to" << baudRate;
        emit connectedPortChanged(serialPort->portName(), baudRate);
        return true;
    } else {
        qCWarning(log_core_serial) << "Failed to set baud rate to" << baudRate << ": " << serialPort->errorString();
        return false;
    }
}

void SerialPortManager::setCommandDelay(int delayMs) {
    m_commandDelayMs = delayMs;
}

// Device management methods
QList<DeviceInfo> SerialPortManager::getAvailableDevices()
{
    if (m_deviceManager) {
        return m_deviceManager->discoverDevices();
    }
    return QList<DeviceInfo>();
}

QStringList SerialPortManager::getAvailablePortChains()
{
    if (m_deviceManager) {
        return m_deviceManager->getAvailablePortChains();
    }
    return QStringList();
}

bool SerialPortManager::selectDeviceByPortChain(const QString& portChain)
{
    if (!m_deviceManager) {
        return false;
    }
    
    DeviceInfo device = m_deviceManager->selectDeviceByPortChain(portChain);
    if (device.isValid()) {
        m_selectedDevice = device;
        m_selectedPortChain = portChain;
        
        // If device has a serial port, try to connect
        if (device.hasSerialPort()) {
            emit serialPortConnected(device.serialPortPath);
        }
        
        qCDebug(log_core_serial) << "Selected device by port chain:" << portChain;
        return true;
    }
    
    qCWarning(log_core_serial) << "Failed to select device by port chain:" << portChain;
    return false;
}

DeviceInfo SerialPortManager::getCurrentSelectedDevice() const
{
    return m_selectedDevice;
}

// Device hotplug event handlers
void SerialPortManager::onDeviceAdded(const DeviceInfo& device)
{
    qCDebug(log_core_serial) << "Device added:" << device.portChain;
    
    // If no device is currently selected, auto-select the first available
    if (!m_selectedDevice.isValid() || m_selectedPortChain.isEmpty()) {
        if (device.hasSerialPort()) {
            m_selectedDevice = device;
            m_selectedPortChain = device.portChain;
            emit serialPortConnected(device.serialPortPath);
            qCDebug(log_core_serial) << "Auto-selected device:" << device.portChain;
        }
    }
    // If this is the previously selected device, reconnect
    else if (device.portChain == m_selectedPortChain) {
        m_selectedDevice = device;
        if (device.hasSerialPort()) {
            emit serialPortConnected(device.serialPortPath);
            qCDebug(log_core_serial) << "Reconnected to device:" << device.portChain;
        }
    }
}

void SerialPortManager::onDeviceRemoved(const DeviceInfo& device)
{
    qCDebug(log_core_serial) << "Device removed:" << device.portChain;
    
    // If this was our selected device, disconnect
    if (device.portChain == m_selectedPortChain) {
        if (device.hasSerialPort()) {
            emit serialPortDisconnected(device.serialPortPath);
        }
        // Don't clear the selected device info yet - we might reconnect
        qCDebug(log_core_serial) << "Selected device disconnected:" << device.portChain;
    }
}

void SerialPortManager::onDeviceModified(const DeviceInfo& oldDevice, const DeviceInfo& newDevice)
{
    qCDebug(log_core_serial) << "Device modified:" << newDevice.portChain;
    
    // Update our selected device if it was modified
    if (newDevice.portChain == m_selectedPortChain) {
        m_selectedDevice = newDevice;
        qCDebug(log_core_serial) << "Updated selected device info:" << newDevice.portChain;
    }
}

void SerialPortManager::onHotplugDeviceChangeEvent(const DeviceChangeEvent& event)
{
    qCDebug(log_core_serial) << "Enhanced hotplug event detected at" << event.timestamp.toString();
    qCDebug(log_core_serial) << "  Added devices:" << event.addedDevices.size();
    qCDebug(log_core_serial) << "  Removed devices:" << event.removedDevices.size();
    qCDebug(log_core_serial) << "  Modified devices:" << event.modifiedDevices.size();
    qCDebug(log_core_serial) << "  Total current devices:" << event.currentDevices.size();
    
    // Handle device additions with detailed port chain info
    for (const auto& device : event.addedDevices) {
        qCInfo(log_core_serial) << "New Openterface device detected:";
        qCInfo(log_core_serial) << "  Port Chain:" << device.portChain;
        qCInfo(log_core_serial) << "  Serial Port:" << device.serialPortPath;
        qCInfo(log_core_serial) << "  HID Device:" << device.hidDevicePath;
        qCInfo(log_core_serial) << "  Camera Device:" << device.cameraDevicePath;
        qCInfo(log_core_serial) << "  Audio Device:" << device.audioDevicePath;
        
        // Auto-select if no device currently selected
        if (!m_selectedDevice.isValid() && device.hasSerialPort()) {
            selectDeviceByPortChain(device.portChain);
        }
    }
    
    // Handle device removals
    for (const auto& device : event.removedDevices) {
        qCInfo(log_core_serial) << "Openterface device removed:";
        qCInfo(log_core_serial) << "  Port Chain:" << device.portChain;
        
        // If this was our selected device, handle disconnection
        if (device.portChain == m_selectedPortChain) {
            qCWarning(log_core_serial) << "Currently selected device was removed!";
            handleSelectedDeviceRemoval(device);
        }
    }
    
    // Handle device modifications (e.g., device enumeration changes)
    for (const auto& pair : event.modifiedDevices) {
        const DeviceInfo& oldDevice = pair.first;
        const DeviceInfo& newDevice = pair.second;
        
        qCDebug(log_core_serial) << "Device modified:" << newDevice.portChain;
        
        // Update selected device if it was modified
        if (newDevice.portChain == m_selectedPortChain) {
            m_selectedDevice = newDevice;
            qCDebug(log_core_serial) << "Updated selected device info";
        }
    }
    
    // Emit summary signal for UI updates
    emit deviceInventoryChanged(event.currentDevices.size(), m_selectedDevice.isValid());
}

void SerialPortManager::handleSelectedDeviceRemoval(const DeviceInfo& removedDevice)
{
    qCWarning(log_core_serial) << "Handling removal of selected device:" << removedDevice.portChain;
    
    // Close current connection if open
    if (serialPort && serialPort->isOpen()) {
        closePort();
    }
    
    // Clear selection
    m_selectedDevice = DeviceInfo();
    m_selectedPortChain.clear();
    
    // Try to auto-select another available device
    auto availableDevices = getAvailableDevices();
    for (const auto& device : availableDevices) {
        if (device.hasSerialPort() && device.portChain != removedDevice.portChain) {
            qCInfo(log_core_serial) << "Auto-selecting alternative device:" << device.portChain;
            selectDeviceByPortChain(device.portChain);
            break;
        }
    }
    
    if (!m_selectedDevice.isValid()) {
        qCWarning(log_core_serial) << "No alternative Openterface devices available";
        emit noDevicesAvailable();
    }
}

QStringList SerialPortManager::getDevicePortChains() const
{
    QStringList portChains;
    if (m_deviceManager) {
        auto devices = m_deviceManager->discoverDevices();
        for (const auto& device : devices) {
            portChains.append(device.portChain);
        }
    }
    return portChains;
}

QString SerialPortManager::formatDeviceInfo(const DeviceInfo& device) const
{
    QStringList info;
    info << QString("Port Chain: %1").arg(device.portChain);
    
    if (device.hasSerialPort()) {
        info << QString("Serial: %1").arg(device.serialPortPath);
    }
    if (device.hasHidDevice()) {
        info << QString("HID: %1").arg(device.hidDevicePath);
    }
    if (device.hasCameraDevice()) {
        info << QString("Camera: %1").arg(device.cameraDevicePath);
    }
    if (device.hasAudioDevice()) {
        info << QString("Audio: %1").arg(device.audioDevicePath);
    }
    
    return info.join(", ");
}

void SerialPortManager::debugDeviceStatus() const
{
    qCInfo(log_core_serial) << "=== Enhanced Device Status Debug ===";
    
    if (!m_deviceManager) {
        qCWarning(log_core_serial) << "No device manager available";
        return;
    }
    
    auto devices = m_deviceManager->discoverDevices();
    qCInfo(log_core_serial) << "Total Openterface devices found:" << devices.size();
    
    for (int i = 0; i < devices.size(); ++i) {
        const auto& device = devices[i];
        qCInfo(log_core_serial) << QString("Device %1:").arg(i + 1);
        qCInfo(log_core_serial) << "  Port Chain:" << device.portChain;
        qCInfo(log_core_serial) << "  Instance ID:" << device.deviceInstanceId;
        qCInfo(log_core_serial) << "  Serial Port:" << (device.hasSerialPort() ? device.serialPortPath : "None");
        qCInfo(log_core_serial) << "  HID Device:" << (device.hasHidDevice() ? device.hidDevicePath : "None");
        qCInfo(log_core_serial) << "  Camera:" << (device.hasCameraDevice() ? device.cameraDevicePath : "None");
        qCInfo(log_core_serial) << "  Audio:" << (device.hasAudioDevice() ? device.audioDevicePath : "None");
        qCInfo(log_core_serial) << "  Last Seen:" << device.lastSeen.toString("yyyy-MM-dd hh:mm:ss");
        
        if (device.getUniqueKey() == m_selectedDevice.getUniqueKey()) {
            qCInfo(log_core_serial) << "  >>> CURRENTLY SELECTED <<<";
        }
    }
    
    if (m_hotplugMonitor) {
        qCInfo(log_core_serial) << "Hotplug Monitor Status:";
        qCInfo(log_core_serial) << "  Running:" << m_hotplugMonitor->isRunning();
        qCInfo(log_core_serial) << "  Poll Interval:" << m_hotplugMonitor->getPollInterval() << "ms";
        qCInfo(log_core_serial) << "  Total Events:" << m_hotplugMonitor->getChangeEventCount();
        if (m_hotplugMonitor->getLastChangeTime().isValid()) {
            qCInfo(log_core_serial) << "  Last Change:" << m_hotplugMonitor->getLastChangeTime().toString("hh:mm:ss");
        }
    }
    
    qCInfo(log_core_serial) << "Current Serial Port Status:";
    qCInfo(log_core_serial) << "  Ready:" << ready;
    if (serialPort) {
        qCInfo(log_core_serial) << "  Port Name:" << serialPort->portName();
        qCInfo(log_core_serial) << "  Baud Rate:" << serialPort->baudRate();
        qCInfo(log_core_serial) << "  Is Open:" << serialPort->isOpen();
    } else {
        qCInfo(log_core_serial) << "  Serial Port: Not initialized";
    }
    
    qCInfo(log_core_serial) << "================================";
}

// Enhanced complete device management methods
bool SerialPortManager::selectCompleteDevice(const QString& portChain)
{
    if (!m_deviceManager) {
        qCWarning(log_core_serial) << "Device manager not available";
        return false;
    }
    
    QList<DeviceInfo> devices = m_deviceManager->getDevicesByPortChain(portChain);
    if (devices.isEmpty()) {
        qCWarning(log_core_serial) << "No devices found for port chain:" << portChain;
        return false;
    }
    
    // Find the best device (most complete interface set)
    DeviceInfo bestDevice;
    int maxInterfaces = 0;
    for (const DeviceInfo& device : devices) {
        int interfaceCount = device.getInterfaceCount();
        if (interfaceCount > maxInterfaces) {
            maxInterfaces = interfaceCount;
            bestDevice = device;
        }
    }
    
    if (!bestDevice.isValid()) {
        qCWarning(log_core_serial) << "No valid device found for port chain:" << portChain;
        return false;
    }
    
    qCInfo(log_core_serial) << "Selecting complete physical device:" << portChain;
    qCInfo(log_core_serial) << "Device interfaces:" << bestDevice.getInterfaceSummary();
    
    // Deactivate current device first if any
    if (m_selectedDevice.isValid()) {
        deactivateCurrentDevice();
    }
    
    // Set the new device
    m_selectedDevice = bestDevice;
    m_selectedPortChain = portChain;
    
    // Activate all available interfaces
    activateDeviceInterfaces(bestDevice);
    
    // Emit signals for UI updates
    emit deviceInventoryChanged(getAvailableDevices().size(), true);
    emit completeDeviceSelected(bestDevice);
    
    if (bestDevice.hasSerialPort()) {
        emit connectedPortChanged(bestDevice.serialPortPath, DEFAULT_BAUDRATE);
    }
    
    qCInfo(log_core_serial) << "Complete device selection successful for:" << portChain;
    return true;
}

bool SerialPortManager::switchToDevice(const QString& portChain)
{
    if (m_selectedPortChain == portChain) {
        qCInfo(log_core_serial) << "Device already selected:" << portChain;
        return true;
    }
    
    return selectCompleteDevice(portChain);
}

bool SerialPortManager::switchPhysicalDevice(const DeviceInfo& fromDevice, const DeviceInfo& toDevice)
{
    qCInfo(log_core_serial) << "Switching physical device from" << fromDevice.portChain << "to" << toDevice.portChain;
    
    if (fromDevice.getUniqueKey() == toDevice.getUniqueKey()) {
        qCWarning(log_core_serial) << "Attempting to switch to the same device";
        return false;
    }
    
    // Perform graceful switch
    deactivateCurrentDevice();
    
    // Brief delay to ensure clean disconnection
    QThread::msleep(100);
    
    // Activate new device
    bool success = selectCompleteDevice(toDevice.portChain);
    
    if (success) {
        qCInfo(log_core_serial) << "Physical device switch successful";
        emit physicalDeviceSwitched(fromDevice.portChain, toDevice.portChain);
    } else {
        qCWarning(log_core_serial) << "Physical device switch failed";
    }
    
    return success;
}

DeviceInfo SerialPortManager::getCurrentCompleteDevice() const
{
    return m_selectedDevice;
}

QStringList SerialPortManager::getActiveDeviceInterfaces() const
{
    QStringList interfaces;
    
    if (!m_selectedDevice.isValid()) {
        return interfaces;
    }
    
    if (m_selectedDevice.hasSerialPort()) {
        interfaces << QString("Serial: %1").arg(m_selectedDevice.serialPortPath);
    }
    if (m_selectedDevice.hasHidDevice()) {
        interfaces << "HID: Available for keyboard/mouse control";
    }
    if (m_selectedDevice.hasCameraDevice()) {
        interfaces << "Camera: Available for video capture";
    }
    if (m_selectedDevice.hasAudioDevice()) {
        interfaces << "Audio: Available for audio capture/playback";
    }
    
    return interfaces;
}

bool SerialPortManager::isDeviceCompletelyAvailable(const QString& portChain) const
{
    if (!m_deviceManager) {
        return false;
    }
    
    QList<DeviceInfo> devices = m_deviceManager->getDevicesByPortChain(portChain);
    for (const DeviceInfo& device : devices) {
        if (device.isCompleteDevice()) {
            return true;
        }
    }
    
    return false;
}

void SerialPortManager::activateDeviceInterfaces(const DeviceInfo& device)
{
    qCDebug(log_core_serial) << "Activating interfaces for device:" << device.portChain;
    
    // Activate serial interface
    if (device.hasSerialPort()) {
        emit serialPortConnected(device.serialPortPath);
        qCDebug(log_core_serial) << "✓ Serial interface activated:" << device.serialPortPath;
    }
    
    // Notify other components about HID interface
    if (device.hasHidDevice()) {
        emit hidDeviceAvailable(device.hidDevicePath);
        qCDebug(log_core_serial) << "✓ HID interface activated:" << device.hidDevicePath;
    }
    
    // Notify about camera interface
    if (device.hasCameraDevice()) {
        emit cameraDeviceAvailable(device.cameraDevicePath);
        qCDebug(log_core_serial) << "✓ Camera interface activated:" << device.cameraDevicePath;
    }
    
    // Notify about audio interface
    if (device.hasAudioDevice()) {
        emit audioDeviceAvailable(device.audioDevicePath);
        qCDebug(log_core_serial) << "✓ Audio interface activated:" << device.audioDevicePath;
    }
    
    emit deviceInterfacesActivated(device);
}

void SerialPortManager::deactivateCurrentDevice()
{
    if (!m_selectedDevice.isValid()) {
        return;
    }
    
    qCInfo(log_core_serial) << "Deactivating current device:" << m_selectedDevice.portChain;
    
    // Close serial port
    if (serialPort && serialPort->isOpen()) {
        closePort();
    }
    
    // Notify components to release interfaces
    emit hidDeviceDisconnected();
    emit cameraDeviceDisconnected();
    emit audioDeviceDisconnected();
    emit deviceInterfacesDeactivated(m_selectedDevice);
    
    qCDebug(log_core_serial) << "Device deactivated successfully";
}

bool SerialPortManager::isDeviceCurrentlyActive(const QString& portChain) const
{
    return m_selectedDevice.isValid() && m_selectedDevice.portChain == portChain;
}
