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
#include "../host/cameramanager.h"
#include "../device/DeviceManager.h"
#include "../device/HotplugMonitor.h"

#include <QSerialPortInfo>
#include <QTimer>
#include <QtConcurrent>
#include <QFuture>
#include <QtSerialPort>
#include <QElapsedTimer>
#include <QSysInfo>


Q_LOGGING_CATEGORY(log_core_serial, "opf.core.serial")

// Define static constants
const int SerialPortManager::ORIGINAL_BAUDRATE;
const int SerialPortManager::DEFAULT_BAUDRATE;
const int SerialPortManager::SERIAL_TIMER_INTERVAL;

SerialPortManager::SerialPortManager(QObject *parent) : QObject(parent), serialPort(nullptr), serialThread(new QThread(nullptr)), serialTimer(new QTimer(nullptr)){
    qCDebug(log_core_serial) << "Initialize serial port.";

    // Initialize port chain tracking member variables
    m_currentSerialPortPath = QString();
    m_currentSerialPortChain = QString();
    
    // Initialize enhanced stability members
    m_connectionWatchdog = new QTimer(this);
    m_errorRecoveryTimer = new QTimer(this);
    m_connectionWatchdog->setSingleShot(true);
    m_errorRecoveryTimer->setSingleShot(true);
    m_lastSuccessfulCommand.start();
    
    setupConnectionWatchdog();

    connect(this, &SerialPortManager::serialPortConnected, this, &SerialPortManager::onSerialPortConnected);
    connect(this, &SerialPortManager::serialPortDisconnected, this, &SerialPortManager::onSerialPortDisconnected);
    connect(this, &SerialPortManager::serialPortConnectionSuccess, this, &SerialPortManager::onSerialPortConnectionSuccess);

    // Connect to DeviceManager for device monitoring instead of running own timer
    DeviceManager& deviceManager = DeviceManager::getInstance();
    // connect(&deviceManager, &DeviceManager::devicesChanged,
    //         this, [this](const QList<DeviceInfo>& devices) {
    //             qCDebug(log_core_serial) << "DeviceManager detected" << devices.size() << "devices";
    //             // Check if we need to connect to a device
    //             checkDeviceConnections(devices);
    //         });

    observeSerialPortNotification();
    m_lastCommandTime.start();
    m_commandDelayMs = 0;  // Default no delay
    lastSerialPortCheckTime = QDateTime::currentDateTime().addMSecs(-SERIAL_TIMER_INTERVAL);  // Initialize check time in the past 
    
    // Connect to hotplug monitor for automatic device management
    connectToHotplugMonitor();
    
    qCDebug(log_core_serial) << "SerialPortManager initialized with DeviceManager integration and enhanced stability features";
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
    qCDebug(log_core_serial) << "Stopping serial port manager...";
    
    // Set shutdown flag to prevent new operations
    m_isShuttingDown = true;
    
    // Stop watchdog timers
    stopConnectionWatchdog();
    
    // Prevent callback access during shutdown
    eventCallback = nullptr;
    
    if (serialThread && serialThread->isRunning()) {
        serialThread->quit();
        serialThread->wait(3000); // Wait up to 3 seconds
    }
    
    if (serialPort && serialPort->isOpen()) {
        closePort();
    }
    
    // Clear command queue
    QMutexLocker locker(&m_commandQueueMutex);
    m_commandQueue.clear();
    
    qCDebug(log_core_serial) << "Serial port manager stopped";
}

// DeviceManager integration methods
void SerialPortManager::checkDeviceConnections(const QList<DeviceInfo>& devices)
{
    qCDebug(log_core_serial) << "Checking device connections for" << devices.size() << "devices";
    
    // Look for available serial ports in the devices
    for (const DeviceInfo& device : devices) {
        if (!device.serialPortPath.isEmpty()) {
            qCDebug(log_core_serial) << "Found device with serial port:" << device.serialPortPath;
            
            // If we don't have a port open, or the current port is different, connect to this one
            if (!serialPort || !serialPort->isOpen() || serialPort->portName() != device.serialPortPath) {
                qCDebug(log_core_serial) << "Attempting to connect to serial port:" << device.serialPortPath;
                emit serialPortConnected(device.serialPortPath);
                
                // Update DeviceManager with the selected device
                DeviceManager& deviceManager = DeviceManager::getInstance();
                deviceManager.setCurrentSelectedDevice(device);
                break; // Connect to the first available device
            }
        }
    }
    
    // If no devices have serial ports and we have a port open, disconnect
    if (devices.isEmpty() && serialPort && serialPort->isOpen()) {
        qCDebug(log_core_serial) << "No devices available, disconnecting serial port";
        emit serialPortDisconnected(serialPort->portName());
    }
}


// New serial port initialization logic using port chain and DeviceInfo
void SerialPortManager::initializeSerialPortFromPortChain() {
    // Get port chain from global settings
    QString portChain = GlobalSetting::instance().getOpenterfacePortChain();
    qCDebug(log_core_serial) << "Initializing serial port using port chain:" << portChain;
    if (portChain.isEmpty()) {
        qCWarning(log_core_serial) << "No port chain found in global settings.";
        return;
    }

    // Use DeviceManager to look up device information by port chain
    DeviceManager& deviceManager = DeviceManager::getInstance();
    QList<DeviceInfo> devices = deviceManager.getDevicesByPortChain(portChain);
    if (devices.isEmpty()) {
        qCWarning(log_core_serial) << "No device found for port chain:" << portChain;
        return;
    }

    // Find a device with a valid serial port path
    DeviceInfo selectedDevice;
    for (const DeviceInfo& device : devices) {
        if (!device.serialPortPath.isEmpty()) {
            selectedDevice = device;
            qCDebug(log_core_serial) << "Found device with serial port:" << device.serialPortPath;
            break;
        }
    }

    if (!selectedDevice.isValid() || selectedDevice.serialPortPath.isEmpty()) {
        qCWarning(log_core_serial) << "No valid device with serial port found for port chain:" << portChain;
        return;
    }
    
    // Open the serial port using the serialPortPath from DeviceInfo
    onSerialPortConnected(selectedDevice.serialPortPath);
    // Optionally, set the selected device in DeviceManager
    deviceManager.setCurrentSelectedDevice(selectedDevice);
    m_currentSerialPortChain = portChain;
}

QString SerialPortManager::getCurrentSerialPortPath() const
{
    return m_currentSerialPortPath;
}

QString SerialPortManager::getCurrentSerialPortChain() const
{
    return m_currentSerialPortChain;
}

bool SerialPortManager::switchSerialPortByPortChain(const QString& portChain)
{
    if (portChain.isEmpty()) {
        qCWarning(log_core_serial) << "Cannot switch to serial port with empty port chain";
        return false;
    }

    qCDebug(log_core_serial) << "Attempting to switch to serial port by port chain:" << portChain;

    try {
        // Use DeviceManager to look up device information by port chain
        DeviceManager& deviceManager = DeviceManager::getInstance();
        QList<DeviceInfo> devices = deviceManager.getDevicesByPortChain(portChain);
        
        if (devices.isEmpty()) {
            qCWarning(log_core_serial) << "No devices found for port chain:" << portChain;
            return false;
        }

        qCDebug(log_core_serial) << "Found" << devices.size() << "device(s) for port chain:" << portChain;

        // Find a device with a valid serial port path
        DeviceInfo selectedDevice;
        for (const DeviceInfo& device : devices) {
            if (!device.serialPortPath.isEmpty()) {
                selectedDevice = device;
                qCDebug(log_core_serial) << "Found device with serial port:" << device.serialPortPath;
                break;
            }
        }

        if (!selectedDevice.isValid() || selectedDevice.serialPortPath.isEmpty()) {
            qCWarning(log_core_serial) << "No valid device with serial port found for port chain:" << portChain;
            return false;
        }

        // Check if we're already using this port - avoid unnecessary switching
        if (!m_currentSerialPortPath.isEmpty() && m_currentSerialPortPath == selectedDevice.serialPortPath) {
            qCDebug(log_core_serial) << "Already using serial port:" << selectedDevice.serialPortPath << "- skipping switch";
            return true;
        }

        QString previousPortPath = m_currentSerialPortPath;
        QString previousPortChain = m_currentSerialPortChain;
        
        qCDebug(log_core_serial) << "Switching serial port from" << previousPortPath 
                                << "to" << selectedDevice.serialPortPath;

        // Close current serial port if open
        if (serialPort && serialPort->isOpen()) {
            qCDebug(log_core_serial) << "Closing current serial port before switch";
            closePort();
        }

        // Update current device tracking
        m_currentSerialPortPath = selectedDevice.serialPortPath;
        m_currentSerialPortChain = portChain;

        // Use onSerialPortConnected to properly initialize the HID chip
        // This ensures the same initialization process as during normal connection
        qCDebug(log_core_serial) << "Initializing serial port with HID chip configuration";
        onSerialPortConnected(selectedDevice.serialPortPath);
        
        if (!ready) {
            qCWarning(log_core_serial) << "Serial port initialization failed after switch";
            // Revert to previous device info on failure
            m_currentSerialPortPath = previousPortPath;
            m_currentSerialPortChain = previousPortChain;
            return false;
        }

        // Update global settings and device manager
        GlobalSetting::instance().setOpenterfacePortChain(portChain);
        deviceManager.setCurrentSelectedDevice(selectedDevice);
        
        // Emit signals for serial port switching
        emit serialPortDeviceChanged(previousPortPath, selectedDevice.serialPortPath);
        emit serialPortSwitched(previousPortChain, portChain);
        
        qCDebug(log_core_serial) << "Serial port switch successful to:" << selectedDevice.serialPortPath 
                                << "Ready state:" << ready;
        return true;

    } catch (const std::exception& e) {
        qCritical() << "Exception in switchSerialPortByPortChain:" << e.what();
        return false;
    } catch (...) {
        qCritical() << "Unknown exception in switchSerialPortByPortChain";
        return false;
    }
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
    qCDebug(log_core_serial) << "Check serial port";

    // Use new initialization logic
    if (!serialPort || !serialPort->isOpen()) {
        qCDebug(log_core_serial) << "Serial port not open, will initialize from port chain after 300ms delay";
        QTimer::singleShot(300, this, [this]() {
            initializeSerialPortFromPortChain();
        });
        return;
    }

    if (ready) {
        if (isTargetUsbConnected) {
            // Check target connection status when no data received in 3 seconds
            if (latestUpdateTime.secsTo(QDateTime::currentDateTime()) > 3) {
                ready = sendAsyncCommand(CMD_GET_INFO, false);
            }
        } else {
            sendAsyncCommand(CMD_GET_INFO, false);
        }
    }

    // If no data received in 5 seconds, consider the port is disconnected or not working
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
    qCDebug(log_core_serial) << "Serial port connected: " << portName;
    
    // First, check if we have a stored baudrate setting
    int storedBaudrate = GlobalSetting::instance().getSerialPortBaudrate();
    int initialBaudrate = DEFAULT_BAUDRATE; // Default fallback
    
    if (storedBaudrate > 0) {
        // Use stored baudrate if available
        initialBaudrate = storedBaudrate;
        qCDebug(log_core_serial) << "Using stored baudrate: " << storedBaudrate;
    } else {
        qCDebug(log_core_serial) << "No stored baudrate found, using default: " << DEFAULT_BAUDRATE;
    }
    
    // Check if the port was successfully opened
    const int maxRetries = 2;
    int retryCount = 0;
    bool openSuccess = openPort(portName, initialBaudrate);
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
        qCDebug(log_core_serial) << "Retrying to open serial port: " << portName << "baudrate:" << initialBaudrate;
        openSuccess = openPort(portName, initialBaudrate);
    }
    if (!openSuccess) {
        qCWarning(log_core_serial) << "Retry failed to open serial port: " << portName;
        return; // Exit if retry also fails
    }

    // send a command to get the parameter configuration with initial baudrate
    QByteArray retBtye = sendSyncCommand(CMD_GET_PARA_CFG, true);
    CmdDataParamConfig config;
    static QSettings settings("Techxartisan", "Openterface");
    uint8_t mode = (settings.value("hardware/operatingMode", 0x02).toUInt());
    
    bool connectionSuccessful = false;
    int workingBaudrate = initialBaudrate;
    
    if(retBtye.size() > 0){
        qCDebug(log_core_serial) << "Data read from serial port: " << retBtye.toHex(' ');
        config = CmdDataParamConfig::fromByteArray(retBtye);
        if(config.mode == mode){ 
            ready = true;
            connectionSuccessful = true;
            qCDebug(log_core_serial) << "Connect success with baudrate: " << initialBaudrate << ready;
        } else { // the mode is not correct, need to re-config the chip
            qCWarning(log_core_serial) << "The mode is incorrect, mode:" << config.mode;
            if(resetHipChip()) {
                connectionSuccessful = true;
                workingBaudrate = DEFAULT_BAUDRATE; // resetHipChip sets to DEFAULT_BAUDRATE
            }
        }
    } else { 
        // If initial baudrate failed, try the baudrate detection process
        qCDebug(log_core_serial) << "No data with initial baudrate, starting baudrate detection process";
        
        // Only try 9600 baudrate for CH341 serial chip (VID:PID 1A86:7523)
        bool isCH341SerialChip = false;
        
        // Check if this port has VID/PID 1A86:7523
        QList<QSerialPortInfo> availablePorts = QSerialPortInfo::availablePorts();
        for (const QSerialPortInfo &portInfo : availablePorts) {
            if (portInfo.portName() == portName) {
                QString vid = QString("%1").arg(portInfo.vendorIdentifier(), 4, 16, QChar('0')).toUpper();
                QString pid = QString("%1").arg(portInfo.productIdentifier(), 4, 16, QChar('0')).toUpper();
                if (vid == "1A86" && pid == "7523") {
                    isCH341SerialChip = true;
                    qCDebug(log_core_serial) << "Detected CH341 serial chip (VID:PID 1A86:7523), will try 9600 baudrate fallback";
                }
                break;
            }
        }
        
        if (isCH341SerialChip) {
            // try 9600 baudrate only for CH341 chip
            qCDebug(log_core_serial) << "No data with" << initialBaudrate << "baudrate, try to connect: " << portName << "with baudrate:" << ORIGINAL_BAUDRATE;
            closePort();
            openPort(portName, ORIGINAL_BAUDRATE);
            QByteArray retBtye = sendSyncCommand(CMD_GET_PARA_CFG, true);
            qCDebug(log_core_serial) << "Data read from serial port with 9600: " << retBtye.toHex(' ');
            if(retBtye.size() > 0){
                config = CmdDataParamConfig::fromByteArray(retBtye);
                qCDebug(log_core_serial) << "Connect success with baudrate: " << ORIGINAL_BAUDRATE;
                qCDebug(log_core_serial) << "Current working mode is:" << "0x" + QString::number(config.mode, 16);

                if(resetHipChip()) {
                    connectionSuccessful = true;
                    workingBaudrate = DEFAULT_BAUDRATE; // resetHipChip sets to DEFAULT_BAUDRATE
                }
            }
        } else {
            qCDebug(log_core_serial) << "No data received and not a CH341 serial chip, skipping 9600 baudrate fallback";
        }
    }

    // Store the working baudrate if connection was successful and it's different from stored
    if (connectionSuccessful && (storedBaudrate != workingBaudrate)) {
        qCDebug(log_core_serial) << "Storing working baudrate:" << workingBaudrate;
        GlobalSetting::instance().setSerialPortBaudrate(workingBaudrate);
    }

    // Check for ARM architecture performance recommendation on successful connection
    if (connectionSuccessful) {
        // Use QTimer::singleShot to show the prompt after the connection is fully established
        QTimer::singleShot(1000, this, [this, workingBaudrate]() {
            checkArmBaudratePerformance(workingBaudrate);
        });
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
    
    // Connect error signal for enhanced error handling
    connect(serialPort, QOverload<QSerialPort::SerialPortError>::of(&QSerialPort::errorOccurred),
            this, &SerialPortManager::handleSerialError);
    
    ready = true;
    resetErrorCounters();
    m_lastSuccessfulCommand.restart();

    if(eventCallback!=nullptr) eventCallback->onPortConnected(portName, serialPort->baudRate());

    qCDebug(log_core_serial) << "Enable the switchable USB now...";
    // serialPort->setDataTerminalReady(false);

    // Start connection watchdog
    setupConnectionWatchdog();

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

    // Clear stored baudrate on factory reset
    clearStoredBaudrate();

    if(serialPort->setRequestToSend(true)){
        if (eventCallback != nullptr) {
            eventCallback->factoryReset(true);
        }
        qCDebug(log_core_serial) << "Set RTS to low";
        QTimer::singleShot(4000, this, [this]() {
            if (serialPort->setRequestToSend(false)) {
                qCDebug(log_core_serial) << "Set RTS to high";
                if (eventCallback != nullptr) {
                    eventCallback->factoryReset(false);
                }
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

    // Clear stored baudrate on factory reset
    clearStoredBaudrate();

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
    
    // Prevent further callback access during destruction
    eventCallback = nullptr;
    
    // Set shutdown flag
    m_isShuttingDown = true;
    
    // Properly stop the manager first
    stop();
    
    // Disconnect from hotplug monitor
    disconnectFromHotplugMonitor();
    
    // Clean up timers
    if (m_connectionWatchdog) {
        m_connectionWatchdog->stop();
        m_connectionWatchdog->deleteLater();
        m_connectionWatchdog = nullptr;
    }
    
    if (m_errorRecoveryTimer) {
        m_errorRecoveryTimer->stop();
        m_errorRecoveryTimer->deleteLater();
        m_errorRecoveryTimer = nullptr;
    }
    
    // Final cleanup
    if (serialPort) {
        delete serialPort;
        serialPort = nullptr;
    }
    
    qCDebug(log_core_serial) << "Serial port manager destroyed";
}

/*
 * Open the serial port
 */
bool SerialPortManager::openPort(const QString &portName, int baudRate) {
    if (m_isShuttingDown) {
        qCDebug(log_core_serial) << "Cannot open port during shutdown";
        return false;
    }
    
    QMutexLocker locker(&m_serialPortMutex);
    
    if (serialPort != nullptr && serialPort->isOpen()) {
        qCDebug(log_core_serial) << "Serial port is already opened.";
        return false;
    }
    
    if(eventCallback!=nullptr) eventCallback->onStatusUpdate("Going to open the port");
    
    if(serialPort == nullptr){
        serialPort = new QSerialPort();
        
        // Connect error signal for enhanced error handling
        connect(serialPort, QOverload<QSerialPort::SerialPortError>::of(&QSerialPort::errorOccurred),
                this, &SerialPortManager::handleSerialError);
    }
    
    serialPort->setPortName(portName);
    serialPort->setBaudRate(baudRate);
    
    // Enhanced port opening with better error handling
    bool openResult = false;
    QSerialPort::SerialPortError lastError = QSerialPort::NoError;
    
    for (int attempt = 0; attempt < 3; ++attempt) {
        openResult = serialPort->open(QIODevice::ReadWrite);
        if (openResult) {
            break;
        }
        
        lastError = serialPort->error();
        qCWarning(log_core_serial) << "Failed to open port on attempt" << (attempt + 1) 
                                   << "Error:" << serialPort->errorString();
        
        // Wait progressively longer between attempts
        QThread::msleep(100 * (attempt + 1));
        
        // Clear error before retry
        serialPort->clearError();
    }
    
    if (openResult) {
        qCDebug(log_core_serial) << "Open port" << portName + ", baudrate: " << baudRate;
        serialPort->setRequestToSend(false);
        
        // Reset error counters on successful connection
        resetErrorCounters();

        if(eventCallback!=nullptr) eventCallback->onStatusUpdate("");
        if(eventCallback!=nullptr) eventCallback->onPortConnected(portName, baudRate);
        return true;
    } else {
        QString errorMsg = QString("Open port failure: %1 (Error: %2)")
                          .arg(serialPort->errorString())
                          .arg(static_cast<int>(lastError));
        qCWarning(log_core_serial) << errorMsg;
        
        m_consecutiveErrors++;
        if(eventCallback!=nullptr) eventCallback->onStatusUpdate(errorMsg);
        return false;
    }
}

/*
 * Close the serial port
 */
void SerialPortManager::closePort() {
    qCDebug(log_core_serial) << "Close serial port";
    
    QMutexLocker locker(&m_serialPortMutex);
    
    if (serialPort != nullptr) {
        if (serialPort->isOpen()) {
            // Disconnect all signals first
            disconnect(serialPort, &QSerialPort::readyRead, this, &SerialPortManager::readData);
            disconnect(serialPort, &QSerialPort::bytesWritten, this, &SerialPortManager::bytesWritten);
            disconnect(serialPort, QOverload<QSerialPort::SerialPortError>::of(&QSerialPort::errorOccurred),
                      this, &SerialPortManager::handleSerialError);
            
            // Attempt graceful flush and close
            try {
                serialPort->flush();
                serialPort->clear();
                serialPort->clearError();
                serialPort->close();
                qCDebug(log_core_serial) << "Serial port closed successfully";
            } catch (...) {
                qCWarning(log_core_serial) << "Exception during port closure";
            }
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
    
    // Stop watchdog while port is closed
    stopConnectionWatchdog();
    
    QThread::msleep(300);
}

bool SerialPortManager::restartPort() {
    QString portName = serialPort->portName();
    qint32 baudRate = serialPort->baudRate();
    qCDebug(log_core_serial) << "Restart port" << portName << "baudrate:" << baudRate;
    if (eventCallback != nullptr) {
        eventCallback->serialPortReset(true);
    }
    closePort();
    QThread::msleep(100);
    openPort(portName, baudRate);
    onSerialPortConnected(portName);
    if (eventCallback != nullptr) {
        eventCallback->serialPortReset(false);
    }
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
    if (m_isShuttingDown || !serialPort || !serialPort->isOpen()) {
        return;
    }
    
    QByteArray data;
    try {
        data = serialPort->readAll();
    } catch (...) {
        qCWarning(log_core_serial) << "Exception occurred while reading serial data";
        m_consecutiveErrors++;
        if (isRecoveryNeeded()) {
            attemptRecovery();
        }
        return;
    }
    
    if (data.isEmpty()) {
        qCDebug(log_core_serial) << "Received empty data from serial port";
        return;
    }
    
    if (data.size() >= 6) {
        // Reset consecutive errors on successful data read
        resetErrorCounters();
        m_lastSuccessfulCommand.restart();

        unsigned char status = data[5];
        unsigned char cmdCode = data[3];

        if(status != DEF_CMD_SUCCESS && (cmdCode >= 0xC0 && cmdCode <= 0xCF)){
            dumpError(status, data);
            m_consecutiveErrors++;
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
                if (eventCallback != nullptr) {
                    eventCallback->onTargetUsbConnected(isTargetUsbConnected);
                }
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
    } else {
        qCWarning(log_core_serial) << "Received incomplete data packet of size:" << data.size();
        m_consecutiveErrors++;
    }
    
    // Check if recovery is needed based on error count
    if (isRecoveryNeeded()) {
        attemptRecovery();
    }
    
    // qCDebug(log_core_serial) << "Recv read" << data;
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
    if (m_isShuttingDown) {
        qCDebug(log_core_serial) << "Cannot write data during shutdown";
        return false;
    }
    
    QMutexLocker locker(&m_serialPortMutex);
    
    if (!serialPort || !serialPort->isOpen()) {
        qCWarning(log_core_serial) << "Serial port not open, cannot write data";
        ready = false;
        m_consecutiveErrors++;
        return false;
    }

    try {
        qint64 bytesWritten = serialPort->write(data);
        if (bytesWritten == -1) {
            qCWarning(log_core_serial) << "Failed to write data to serial port:" << serialPort->errorString();
            m_consecutiveErrors++;
            return false;
        } else if (bytesWritten != data.size()) {
            qCWarning(log_core_serial) << "Partial write: expected" << data.size() << "bytes, wrote" << bytesWritten;
            m_consecutiveErrors++;
            return false;
        }
        
        // Wait for write to complete with timeout
        if (!serialPort->waitForBytesWritten(1000)) {
            qCWarning(log_core_serial) << "Write timeout occurred";
            m_consecutiveErrors++;
            return false;
        }
        
        qCDebug(log_core_serial) << "Data written to serial port: @" + serialPort->portName() << ":" << data.toHex(' ');
        
        // Reset error count on successful write
        if (m_consecutiveErrors > 0) {
            m_consecutiveErrors = qMax(0, m_consecutiveErrors - 1);
        }
        
        return true;
        
    } catch (...) {
        qCCritical(log_core_serial) << "Exception occurred while writing to serial port";
        m_consecutiveErrors++;
        ready = false;
        return false;
    }
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
    // qCDebug(log_core_serial) << "Data received signal emitted";
    emit dataSent(data);
    QByteArray command = data;
    
    command.append(calculateChecksum(command));
    qCDebug(log_core_serial) <<  "Check sum" << command.toHex(' ');
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
                // qCDebug(log_core_serial) << "USB descriptor:" << tmp;
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

                // qCDebug(log_core_serial) <<  "usb descriptor" << command.toHex(' ');
                if (serialPort != nullptr && serialPort->isOpen()){
                    QByteArray respon = sendSyncCommand(command, true);
                    qDebug(log_core_serial) << respon;
                    qDebug(log_core_serial) << " After sending command";
                }
                qCDebug(log_core_serial) <<  "usb descriptor" << command.toHex(' ');
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

void SerialPortManager::setUserSelectedBaudrate(int baudRate) {
    qCDebug(log_core_serial) << "User manually selected baudrate:" << baudRate;
    
    // Check for ARM architecture performance recommendation
    checkArmBaudratePerformance(baudRate);
    
    // Store the user selection immediately
    GlobalSetting::instance().setSerialPortBaudrate(baudRate);
    
    // Apply the baudrate change
    bool success = setBaudRate(baudRate);
    if (success) {
        qCInfo(log_core_serial) << "User selected baudrate applied successfully:" << baudRate;
    } else {
        qCWarning(log_core_serial) << "Failed to apply user selected baudrate:" << baudRate;
    }
}

void SerialPortManager::clearStoredBaudrate() {
    qCDebug(log_core_serial) << "Clearing stored baudrate setting";
    GlobalSetting::instance().clearSerialPortBaudrate();
}

// ARM architecture detection and performance prompt
bool SerialPortManager::isArmArchitecture() {
    QString architecture = QSysInfo::currentCpuArchitecture();
    qCDebug(log_core_serial) << "Current CPU architecture:" << architecture;
    
    // Check for ARM architectures (arm, arm64, aarch64)
    return architecture.contains("arm", Qt::CaseInsensitive) || 
           architecture.contains("aarch64", Qt::CaseInsensitive);
}

void SerialPortManager::checkArmBaudratePerformance(int baudrate) {
    // Only check for 115200 baudrate on ARM architecture
    if (baudrate != DEFAULT_BAUDRATE || !isArmArchitecture()) {
        return;
    }
    
    // Check if user has disabled this prompt
    if (GlobalSetting::instance().getArmBaudratePromptDisabled()) {
        qCDebug(log_core_serial) << "ARM baudrate performance prompt is disabled by user";
        return;
    }
    
    qCInfo(log_core_serial) << "ARM architecture detected with 115200 baudrate - emitting performance recommendation signal";
    
    // Emit signal to notify UI layer
    emit armBaudratePerformanceRecommendation(baudrate);
}

void SerialPortManager::handleArmBaudrateRecommendationResponse(bool switchTo9600, bool dontShowAgain) {
    if (switchTo9600) {
        qCInfo(log_core_serial) << "User chose to switch to 9600 baudrate for ARM performance";
        setUserSelectedBaudrate(ORIGINAL_BAUDRATE);
    } else {
        qCInfo(log_core_serial) << "User chose to keep 115200 baudrate despite ARM performance recommendation";
    }
    
    // Remember user's choice if requested
    if (dontShowAgain) {
        qCInfo(log_core_serial) << "User chose to disable ARM baudrate performance prompts";
        GlobalSetting::instance().setArmBaudratePromptDisabled(true);
    }
}

void SerialPortManager::setCommandDelay(int delayMs) {
    m_commandDelayMs = delayMs;
}

void SerialPortManager::connectToHotplugMonitor()
{
    qCDebug(log_core_serial) << "Connecting SerialPortManager to hotplug monitor";
    
    // Get the hotplug monitor from DeviceManager
    DeviceManager& deviceManager = DeviceManager::getInstance();
    HotplugMonitor* hotplugMonitor = deviceManager.getHotplugMonitor();
    
    if (!hotplugMonitor) {
        qCWarning(log_core_serial) << "Failed to get hotplug monitor from device manager";
        return;
    }
    
    // Connect to device unplugging signal
    connect(hotplugMonitor, &HotplugMonitor::deviceUnplugged,
            this, [this](const DeviceInfo& device) {
                qCDebug(log_core_serial) << "Device unplugged detected:" << device.portChain << "Port chain:" << m_currentSerialPortChain;
                
                // Check if this device has the same port chain as our current serial port
                if (!m_currentSerialPortChain.isEmpty() && 
                    m_currentSerialPortChain == device.portChain) {
                    qCInfo(log_core_serial) << "Serial port device unplugged, closing connection:" << device.portChain;
                    
                    // Close the serial port connection
                    if (serialPort && serialPort->isOpen()) {
                        closePort();
                        emit serialPortDisconnected(m_currentSerialPortPath);
                    }
                    
                    // Clear current device tracking
                    m_currentSerialPortPath.clear();
                    m_currentSerialPortChain.clear();
                }
            });
            
    // Connect to new device plugged in signal
    connect(hotplugMonitor, &HotplugMonitor::newDevicePluggedIn,
            this, [this](const DeviceInfo& device) {
                qCDebug(log_core_serial) << "New device plugged in:" << device.portChain;
                
                // Check if we don't have an active serial port and if this device has a serial port
                if ((!serialPort || !serialPort->isOpen()) && !device.serialPortPath.isEmpty()) {
                    qCInfo(log_core_serial) << "Auto-connecting to new serial device:" << device.serialPortPath;
                    
                    // Try to switch to this new serial port
                    bool switchSuccess = switchSerialPortByPortChain(device.portChain);
                    if (switchSuccess) {
                        qCInfo(log_core_serial) << "✓ Serial port auto-switched to new device at port:" << device.portChain;
                        emit serialPortConnected(device.serialPortPath);
                    } else {
                        qCDebug(log_core_serial) << "Serial port auto-switch failed for port:" << device.portChain;
                    }
                }
            });
            
    qCDebug(log_core_serial) << "SerialPortManager successfully connected to hotplug monitor";
}

void SerialPortManager::disconnectFromHotplugMonitor()
{
    qCDebug(log_core_serial) << "Disconnecting SerialPortManager from hotplug monitor";
    
    // Get the hotplug monitor from DeviceManager
    DeviceManager& deviceManager = DeviceManager::getInstance();
    HotplugMonitor* hotplugMonitor = deviceManager.getHotplugMonitor();
    
    if (hotplugMonitor) {
        // Disconnect all signals from hotplug monitor
        disconnect(hotplugMonitor, nullptr, this, nullptr);
        qCDebug(log_core_serial) << "SerialPortManager disconnected from hotplug monitor";
    }
}

// Enhanced stability implementation

void SerialPortManager::enableAutoRecovery(bool enable)
{
    m_autoRecoveryEnabled = enable;
    qCDebug(log_core_serial) << "Auto recovery" << (enable ? "enabled" : "disabled");
}

void SerialPortManager::setMaxRetryAttempts(int maxRetries)
{
    m_maxRetryAttempts = qMax(1, maxRetries);
    qCDebug(log_core_serial) << "Max retry attempts set to:" << m_maxRetryAttempts;
}

void SerialPortManager::setMaxConsecutiveErrors(int maxErrors)
{
    m_maxConsecutiveErrors = qMax(1, maxErrors);
    qCDebug(log_core_serial) << "Max consecutive errors set to:" << m_maxConsecutiveErrors;
}

bool SerialPortManager::isConnectionStable() const
{
    return m_consecutiveErrors < (m_maxConsecutiveErrors / 2) && 
           m_lastSuccessfulCommand.elapsed() < 10000; // 10 seconds
}

int SerialPortManager::getConsecutiveErrorCount() const
{
    return m_consecutiveErrors;
}

int SerialPortManager::getConnectionRetryCount() const
{
    return m_connectionRetryCount;
}

void SerialPortManager::forceRecovery()
{
    qCInfo(log_core_serial) << "Force recovery requested";
    attemptRecovery();
}

void SerialPortManager::handleSerialError(QSerialPort::SerialPortError error)
{
    if (error == QSerialPort::NoError || m_isShuttingDown) {
        return;
    }
    
    QString errorString = serialPort ? serialPort->errorString() : "Unknown error";
    qCWarning(log_core_serial) << "Serial port error occurred:" << errorString << "Error code:" << static_cast<int>(error);
    
    m_consecutiveErrors++;
    
    switch (error) {
        case QSerialPort::DeviceNotFoundError:
        case QSerialPort::PermissionError:
        case QSerialPort::OpenError:
            qCCritical(log_core_serial) << "Critical serial port error, immediate recovery needed";
            ready = false;
            attemptRecovery();
            break;
            
        case QSerialPort::WriteError:
        case QSerialPort::ReadError:
        case QSerialPort::ResourceError:
        case QSerialPort::UnsupportedOperationError:
        case QSerialPort::TimeoutError:
            if (isRecoveryNeeded()) {
                attemptRecovery();
            }
            break;
            
        default:
            qCDebug(log_core_serial) << "Unhandled serial port error:" << static_cast<int>(error);
            break;
    }
}

void SerialPortManager::attemptRecovery()
{
    if (m_isShuttingDown || !m_autoRecoveryEnabled) {
        return;
    }
    
    qCInfo(log_core_serial) << "Attempting serial port recovery. Consecutive errors:" << m_consecutiveErrors 
                           << "Retry count:" << m_connectionRetryCount;
    
    if (m_connectionRetryCount >= m_maxRetryAttempts) {
        qCCritical(log_core_serial) << "Maximum retry attempts reached. Giving up recovery.";
        ready = false;
        if (eventCallback) {
            eventCallback->onStatusUpdate("Serial port recovery failed - max retries exceeded");
        }
        return;
    }
    
    m_connectionRetryCount++;
    
    // Schedule recovery attempt with exponential backoff
    int delay = qMin(1000 * (1 << (m_connectionRetryCount - 1)), 10000); // Max 10 seconds
    
    m_errorRecoveryTimer->stop();
    QTimer::singleShot(delay, this, [this]() {
        if (m_isShuttingDown) {
            return;
        }
        
        qCInfo(log_core_serial) << "Executing recovery attempt" << m_connectionRetryCount;
        
        QString currentPortPath = m_currentSerialPortPath;
        QString currentPortChain = m_currentSerialPortChain;
        
        // Try to restart the current port
        if (!currentPortPath.isEmpty() && !currentPortChain.isEmpty()) {
            bool recoverySuccess = switchSerialPortByPortChain(currentPortChain);
            
            if (recoverySuccess && ready) {
                qCInfo(log_core_serial) << "✓ Serial port recovery successful";
                resetErrorCounters();
                if (eventCallback) {
                    eventCallback->onStatusUpdate("Serial port recovered successfully");
                }
            } else {
                qCWarning(log_core_serial) << "Serial port recovery attempt failed";
                if (eventCallback) {
                    eventCallback->onStatusUpdate(QString("Recovery attempt %1 failed").arg(m_connectionRetryCount));
                }
                
                // Try again if we haven't exceeded max attempts
                if (m_connectionRetryCount < m_maxRetryAttempts) {
                    attemptRecovery();
                }
            }
        } else {
            qCWarning(log_core_serial) << "Cannot recover - no port chain information available";
        }
    });
}

void SerialPortManager::resetErrorCounters()
{
    m_consecutiveErrors = 0;
    m_connectionRetryCount = 0;
}

bool SerialPortManager::isRecoveryNeeded() const
{
    return m_autoRecoveryEnabled && 
           m_consecutiveErrors >= m_maxConsecutiveErrors &&
           m_connectionRetryCount < m_maxRetryAttempts;
}

void SerialPortManager::setupConnectionWatchdog()
{
    m_connectionWatchdog->setInterval(30000); // 30 seconds
    connect(m_connectionWatchdog, &QTimer::timeout, this, [this]() {
        if (m_isShuttingDown) {
            return;
        }
        
        // Check if we haven't had successful communication in a while
        if (m_lastSuccessfulCommand.elapsed() > 30000) { // 30 seconds
            qCWarning(log_core_serial) << "Connection watchdog triggered - no successful communication for 30s";
            
            if (m_autoRecoveryEnabled && !isRecoveryNeeded()) {
                m_consecutiveErrors = m_maxConsecutiveErrors; // Force recovery
                attemptRecovery();
            }
        }
        
        // Restart watchdog
        m_connectionWatchdog->start();
    });
    
    if (!m_isShuttingDown) {
        m_connectionWatchdog->start();
    }
}

void SerialPortManager::stopConnectionWatchdog()
{
    if (m_connectionWatchdog) {
        m_connectionWatchdog->stop();
    }
    if (m_errorRecoveryTimer) {
        m_errorRecoveryTimer->stop();
    }
}
