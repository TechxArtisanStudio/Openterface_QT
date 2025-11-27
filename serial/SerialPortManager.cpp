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
const int SerialPortManager::BAUDRATE_HIGHSPEED;
const int SerialPortManager::BAUDRATE_LOWSPEED;
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
    
    // Initialize error frequency tracking
    m_errorTrackingTimer.start();
    
    setupConnectionWatchdog();

    connect(this, &SerialPortManager::serialPortConnected, this, &SerialPortManager::onSerialPortConnected);
    connect(this, &SerialPortManager::serialPortDisconnected, this, &SerialPortManager::onSerialPortDisconnected);
    connect(this, &SerialPortManager::serialPortConnectionSuccess, this, &SerialPortManager::onSerialPortConnectionSuccess);
    
    // Connect parameter configuration success signal to automatically send reset command
    connect(this, &SerialPortManager::parameterConfigurationSuccess, this, [this]() {
        qCDebug(log_core_serial) << "Parameter configuration successful, sending reset command automatically";
        sendResetCommand();
        int storedBaudrate = GlobalSetting::instance().getSerialPortBaudrate();
        qCDebug(log_core_serial) << "Reopen the serial port with baudrate: " << storedBaudrate;
        setBaudRate(storedBaudrate);
        restartPort();
    });

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
        // checkSerialPort();
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
// void SerialPortManager::checkDeviceConnections(const QList<DeviceInfo>& devices)
// {
//     qCDebug(log_core_serial) << "Checking device connections for" << devices.size() << "devices";
    
//     // Look for available serial ports in the devices
//     for (const DeviceInfo& device : devices) {
//         if (!device.serialPortPath.isEmpty()) {
//             qCDebug(log_core_serial) << "Found device with serial port:" << device.serialPortPath;
            
//             // If we don't have a port open, or the current port is different, connect to this one
//             if (!serialPort || !serialPort->isOpen() || serialPort->portName() != device.serialPortPath) {
//                 qCDebug(log_core_serial) << "Attempting to connect to serial port:" << device.serialPortPath;
//                 emit serialPortConnected(device.serialPortPath);
                
//                 // Update DeviceManager with the selected device
//                 DeviceManager& deviceManager = DeviceManager::getInstance();
//                 deviceManager.setCurrentSelectedDevice(device);
//                 break; // Connect to the first available device
//             }
//         }
//     }
    
//     // If no devices have serial ports and we have a port open, disconnect
//     if (devices.isEmpty() && serialPort && serialPort->isOpen()) {
//         qCDebug(log_core_serial) << "No devices available, disconnecting serial port";
//         emit serialPortDisconnected(serialPort->portName());
//     }
// }


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
    QString usedPortChain = portChain;
    for (const DeviceInfo& device : devices) {
        if (!device.serialPortPath.isEmpty()) {
            selectedDevice = device;
            qCDebug(log_core_serial) << "Found device with serial port:" << device.serialPortPath;
            break;
        }
    }

    // If no device with serial found on main port chain, try companion port chain
    if (!selectedDevice.isValid() || selectedDevice.serialPortPath.isEmpty()) {
        QString companionPortChain = deviceManager.getCompanionPortChain(portChain);
        if (!companionPortChain.isEmpty()) {
            QList<DeviceInfo> companionDevices = deviceManager.getDevicesByPortChain(companionPortChain);
            qCDebug(log_core_serial) << "Checking companion port chain:" << companionPortChain << "with" << companionDevices.size() << "devices";
            for (const DeviceInfo& companionDevice : companionDevices) {
                if (!companionDevice.serialPortPath.isEmpty()) {
                    selectedDevice = companionDevice;
                    usedPortChain = companionPortChain;
                    qCDebug(log_core_serial) << "Found device with serial port on companion chain:" << companionDevice.serialPortPath;
                    break;
                }
            }
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
    m_currentSerialPortChain = usedPortChain;
}

QString SerialPortManager::getCurrentSerialPortPath() const
{
    return m_currentSerialPortPath;
}

QString SerialPortManager::getCurrentSerialPortChain() const
{
    return m_currentSerialPortChain;
}

int SerialPortManager::getCurrentBaudrate() const
{
    if (serialPort && serialPort->isOpen()) {
        return serialPort->baudRate();
    }
    return 9600;
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
    if(isChipTypeCH32V208()){
        ready = true; // CH32V208 chip is always ready
    }else{
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

        // Always send CMD_GET_INFO with force=true to ensure key state update every check,
        // even if !ready (bypasses the ready guard in sendAsyncCommand)
        sendAsyncCommand(CMD_GET_INFO, true);

        if (latestUpdateTime.secsTo(QDateTime::currentDateTime()) > 5) {
            ready = false;
        }
    }
}


/*
 * Open the serial port and check the baudrate and mode
 */
void SerialPortManager::onSerialPortConnected(const QString &portName){
    qCDebug(log_core_serial) << "Serial port connected: " << portName;
    
    // Detect chip type FIRST
    m_currentChipType = detectChipType(portName);
    
    int tryBaudrate = determineBaudrate();
    
    if (!openPortWithRetries(portName, tryBaudrate)) {
        qCWarning(log_core_serial) << "Failed to open port after retries";
        return;
    }
    
    ConfigResult config = sendAndProcessConfigCommand();
    if (!config.success) {
        config = attemptBaudrateDetection();
    }
    
    if (config.success) {
        handleChipSpecificLogic(config);
        storeBaudrateIfNeeded(config.workingBaudrate);
        emit serialPortConnectionSuccess(portName);
    }
}

int SerialPortManager::determineBaudrate() const {
    int stored = GlobalSetting::instance().getSerialPortBaudrate();
    if (isChipTypeCH32V208()) {
        return BAUDRATE_HIGHSPEED;  // Always 115200
    }
    return stored > 0 ? stored : DEFAULT_BAUDRATE;
}

bool SerialPortManager::openPortWithRetries(const QString &portName, int tryBaudrate) {
    const int maxRetries = 2;
    int retryCount = 0;
    bool openSuccess = openPort(portName, tryBaudrate);
    while (retryCount < maxRetries && !openSuccess) {
        qCWarning(log_core_serial) << "Failed to open serial port: " << portName;
        if (serialPort->isOpen()) {
            qCDebug(log_core_serial) << "Port is still open, closing it before retry";
            closePort();
        }
        QEventLoop loop;
        QTimer::singleShot(500 * (retryCount + 1), &loop, &QEventLoop::quit);
        loop.exec();
        retryCount++;
        qCDebug(log_core_serial) << "Retrying to open serial port: " << portName << "baudrate:" << tryBaudrate;
        openSuccess = openPort(portName, tryBaudrate);
    }
    if (!openSuccess) {
        qCWarning(log_core_serial) << "Retry failed to open serial port: " << portName;
    }
    return openSuccess;
}

/*
* Send configuration command and process the response
*/
ConfigResult SerialPortManager::sendAndProcessConfigCommand() {
    ConfigResult result;
    QByteArray retByte = sendSyncCommand(CMD_GET_PARA_CFG, true);
    if (retByte.isEmpty()) return result;
    
    qCDebug(log_core_serial) << "Data read from serial port: " << retByte.toHex(' ');
    CmdDataParamConfig config = CmdDataParamConfig::fromByteArray(retByte);
    
    static QSettings settings("Techxartisan", "Openterface");
    uint8_t hostConfigMode = (settings.value("hardware/operatingMode", 0x02).toUInt());
    
    if (config.mode == hostConfigMode) {
        if (isChipTypeCH32V208() && serialPort->baudRate() != BAUDRATE_HIGHSPEED) {
            qCWarning(log_core_serial) << "CH32V208 chip detected at wrong baudrate" << serialPort->baudRate() << "- switching to 115200";
            closePort();
            openPort(serialPort->portName(), BAUDRATE_HIGHSPEED);
            QByteArray verifyByte = sendSyncCommand(CMD_GET_PARA_CFG, true);
            if (verifyByte.size() > 0) {
                result.success = true;
                result.workingBaudrate = BAUDRATE_HIGHSPEED;
                qCInfo(log_core_serial) << "CH32V208 chip successfully switched to 115200 baudrate";
            } else {
                qCWarning(log_core_serial) << "Failed to verify CH32V208 chip at 115200";
            }
        } else {
            result.success = true;
            result.workingBaudrate = serialPort->baudRate();
            qCDebug(log_core_serial) << "Connect success with baudrate: " << serialPort->baudRate();
            checkArmBaudratePerformance(serialPort->baudRate());
        }
    } else {
        qCWarning(log_core_serial) << "The mode is incorrect, mode:" << config.mode << "expected:" << hostConfigMode;
        if (isChipTypeCH32V208()) {
            qCWarning(log_core_serial) << "CH32V208 chip does not support mode reconfiguration via commands";
            result.success = true;  // Still successful, just mode is different
            result.workingBaudrate = BAUDRATE_HIGHSPEED;
        } else {
            if (reconfigureHidChip(serialPort->baudRate())) {
                qCDebug(log_core_serial) << "Reconfigured HID chip successfully, sending reset command";
                if (sendResetCommand()) {
                    result.success = true;
                    result.workingBaudrate = serialPort->baudRate();
                } else {
                    qCWarning(log_core_serial) << "Failed to send reset command after reconfiguration";
                }
            } else {
                qCWarning(log_core_serial) << "Failed to reconfigure HID chip with mode:" << hostConfigMode;
            }
        }
    }
    return result;
}

/*
 * Attempt baudrate detection by switching baudrates and checking for valid responses
 */
ConfigResult SerialPortManager::attemptBaudrateDetection() {
    ConfigResult result;
    qCDebug(log_core_serial) << "No data with initial baudrate, starting baudrate detection process";
    
    // Handle CH32V208 chip: Only supports 115200 baudrate, no command-based configuration
    if (isChipTypeCH32V208()) {
        qCInfo(log_core_serial) << "CH32V208 chip: Only supports 115200, retrying at 115200";
        closePort();
        openPort(serialPort->portName(), BAUDRATE_HIGHSPEED);
        QByteArray retByte = sendSyncCommand(CMD_GET_PARA_CFG, true);
        qCDebug(log_core_serial) << "Data read from CH32V208 serial port at 115200: " << retByte.toHex(' ');
        if (retByte.size() > 0) {
            CmdDataParamConfig config = CmdDataParamConfig::fromByteArray(retByte);
            qCDebug(log_core_serial) << "Connected with baudrate: " << BAUDRATE_HIGHSPEED;
            qCDebug(log_core_serial) << "Current working mode is:" << "0x" + QString::number(config.mode, 16);
            qCInfo(log_core_serial) << "CH32V208 chip connection successful (mode cannot be changed on CH32V208)";
            result.success = true;
            result.workingBaudrate = BAUDRATE_HIGHSPEED;
            setBaudRate(BAUDRATE_HIGHSPEED);
        } else {
            qCWarning(log_core_serial) << "No response from CH32V208 chip at 115200 baudrate";
        }
    // Handle CH9329 chip: Supports both baudrates with command-based configuration
    } else if (isChipTypeCH9329()) {
        int altBaudrate = anotherBaudrate();
        qCDebug(log_core_serial) << "CH9329 chip: Trying alternative baudrate" << altBaudrate;
        closePort();
        openPort(serialPort->portName(), altBaudrate);
        QByteArray retByte = sendSyncCommand(CMD_GET_PARA_CFG, true);
        qCDebug(log_core_serial) << "Data read from serial port with alternative baudrate: " << retByte.toHex(' ');
        if (retByte.size() > 0) {
            CmdDataParamConfig config = CmdDataParamConfig::fromByteArray(retByte);
            qCDebug(log_core_serial) << "Connected with baudrate: " << altBaudrate;
            qCDebug(log_core_serial) << "Current working mode is:" << "0x" + QString::number(config.mode, 16);
            
            // Check if the mode matches the expected host configuration mode
            static QSettings settings("Techxartisan", "Openterface");
            uint8_t hostConfigMode = (settings.value("hardware/operatingMode", 0x02).toUInt());
            
            if (config.mode == hostConfigMode) {
                qCDebug(log_core_serial) << "Mode is correct, connection successful";
                result.success = true;
                result.workingBaudrate = altBaudrate;
                setBaudRate(altBaudrate);
            } else {
                qCWarning(log_core_serial) << "Mode incorrect after CH9329 baudrate detection, mode:" << config.mode << "expected:" << hostConfigMode;
                // Attempt to reconfigure the chip to the correct mode
                if (reconfigureHidChip(altBaudrate)) {
                    qCDebug(log_core_serial) << "Reconfigured HID chip successfully, sending reset command";
                    if (sendResetCommand()) {
                        result.success = true;
                        result.workingBaudrate = altBaudrate;
                    } else {
                        qCWarning(log_core_serial) << "Failed to send reset command after reconfiguration";
                    }
                } else {
                    qCWarning(log_core_serial) << "Failed to reconfigure HID chip after CH9329 detection";
                }
            }
        }
    // Fallback for unknown chip types: Try alternative baudrate without mode checking
    } else {
        qCWarning(log_core_serial) << "Unknown chip type - attempting baudrate fallback";
        int altBaudrate = anotherBaudrate();
        closePort();
        openPort(serialPort->portName(), altBaudrate);
        QByteArray retByte = sendSyncCommand(CMD_GET_PARA_CFG, true);
        if (retByte.size() > 0) {
            CmdDataParamConfig config = CmdDataParamConfig::fromByteArray(retByte);
            qCDebug(log_core_serial) << "Connected with alternative baudrate: " << altBaudrate;
            result.success = true;
            result.workingBaudrate = altBaudrate;
            setBaudRate(altBaudrate);
        } else {
            qCWarning(log_core_serial) << "No data received with alternative baudrate - connection failed";
        }
    }
    return result;
}

void SerialPortManager::handleChipSpecificLogic(const ConfigResult &config) {
    if (config.success) {
        ready = true;
        resetErrorCounters();
        m_lastSuccessfulCommand.restart();
    }
}

void SerialPortManager::storeBaudrateIfNeeded(int workingBaudrate) {
    int stored = GlobalSetting::instance().getSerialPortBaudrate();
    if (stored != workingBaudrate) {
        if (isChipTypeCH32V208() && workingBaudrate != BAUDRATE_HIGHSPEED) {
            qCWarning(log_core_serial) << "CH32V208 chip: Forcing stored baudrate to 115200 instead of" << workingBaudrate;
            workingBaudrate = BAUDRATE_HIGHSPEED;
        }
        qCDebug(log_core_serial) << "Storing working baudrate:" << workingBaudrate;
        GlobalSetting::instance().setSerialPortBaudrate(workingBaudrate);
    }
}

int SerialPortManager::anotherBaudrate(){
    return serialPort->baudRate() == SerialPortManager::BAUDRATE_HIGHSPEED ? SerialPortManager::BAUDRATE_LOWSPEED : SerialPortManager::BAUDRATE_HIGHSPEED;
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
 * Reset the hid chip, set the baudrate to specified rate and mode to 0x82 and reset the chip
 * CH32V208: Only supports 115200, just close and reopen with 115200
 * CH9329: Supports both baudrates, requires reconfiguration command + reset command
 */
bool SerialPortManager::resetHipChip(int targetBaudrate){
    qCDebug(log_core_serial) << "Reset the hid chip now...";
    QString portName = serialPort->portName();
    
    // Handle CH32V208 chip - simple close/reopen, no commands needed
    if (isChipTypeCH32V208()) {
        qCInfo(log_core_serial) << "CH32V208 chip detected - using simple close/reopen (no commands)";
        
        // CH32V208 only supports 115200
        if (targetBaudrate != BAUDRATE_HIGHSPEED) {
            qCWarning(log_core_serial) << "CH32V208 chip only supports 115200 baudrate, ignoring requested baudrate:" << targetBaudrate;
            targetBaudrate = BAUDRATE_HIGHSPEED;
        }
        
        // Close and reopen the port with 115200
        closePort();
        
        // Use non-blocking timer instead of msleep
        QTimer::singleShot(100, this, [this, portName, targetBaudrate]() {
            if (openPort(portName, targetBaudrate)) {
                qCInfo(log_core_serial) << "CH32V208 chip successfully reopened at 115200";
                onSerialPortConnected(portName);
            } else {
                qCWarning(log_core_serial) << "Failed to reopen CH32V208 chip at 115200";
            }
        });
        
        return true;
    }
    
    // Handle CH9329 chip - requires commands for reconfiguration
    if (isChipTypeCH9329()) {
        qCInfo(log_core_serial) << "CH9329 chip detected - using command-based reconfiguration";
        
        if(reconfigureHidChip(targetBaudrate)) {
            qCDebug(log_core_serial) << "Reset the hid chip success.";
            if(sendResetCommand()){
                qCDebug(log_core_serial) << "Reopen the serial port with baudrate: " << targetBaudrate;
                setBaudRate(targetBaudrate);
                restartPort();
                return true;
            }else{
                qCWarning(log_core_serial) << "Reset the hid chip fail - send reset command failed";
                return false;
            }
        }else{
            qCWarning(log_core_serial) << "Set data config fail - reconfigureHidChip returned false";
            // Don't call restartPort() here as it causes infinite loop
            // Just mark as not ready and let the normal connection flow handle it
            ready = false;
            qCDebug(log_core_serial) << "Target baudrate was: " << targetBaudrate << "Current baudrate: " << serialPort->baudRate();
            return false;
        }
    }
    
    // Unknown chip type - try the CH9329 approach as fallback
    qCWarning(log_core_serial) << "Unknown chip type - attempting CH9329 approach";
    if(reconfigureHidChip(targetBaudrate)) {
        qCDebug(log_core_serial) << "Reset the hid chip success.";
        if(sendResetCommand()){
            qCDebug(log_core_serial) << "Reopen the serial port with baudrate: " << targetBaudrate;
            setBaudRate(targetBaudrate);
            restartPort();
            return true;
        }else{
            qCWarning(log_core_serial) << "Reset the hid chip fail - send reset command failed";
            return false;
        }
    }else{
        qCWarning(log_core_serial) << "Set data config fail - reconfigureHidChip returned false";
        // Don't call restartPort() here as it causes infinite loop
        // Just mark as not ready and let the normal connection flow handle it
        ready = false;
        qCDebug(log_core_serial) << "Target baudrate was: " << targetBaudrate << "Current baudrate: " << serialPort->baudRate();
        return false;
    }
}



/*
 * Send the reset command to the hid chip
 */
bool SerialPortManager::sendResetCommand(){
    QByteArray retByte = sendSyncCommand(CMD_RESET, true);
    if(retByte.size() > 0){
        qCDebug(log_core_serial) << "Send reset command success.";
        return true;
    } else{
        qCDebug(log_core_serial) << "Send reset command fail.";
        return false;
    }
}

/*
 * Supported hardware 1.9 and > 1.9.1
 * Factory reset the hid chip by holding the RTS pin to low for 4 seconds
 * CH32V208: Uses RTS pin reset method only
 * CH9329: Uses RTS pin reset method
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
 * CH32V208: May not support this command, will try at 115200 only
 * CH9329: Supports this command at both baudrates
 */
bool SerialPortManager::factoryResetHipChipV191(){
    qCDebug(log_core_serial) << "Factory reset Hid chip for 1.9.1 now...";
    if(eventCallback) eventCallback->onStatusUpdate("Factory reset Hid chip now.");

    // Clear stored baudrate on factory reset
    clearStoredBaudrate();

    // CH32V208 chip only supports 115200, don't try 9600
    if (isChipTypeCH32V208()) {
        qCInfo(log_core_serial) << "CH32V208 chip detected - attempting factory reset at 115200 only";
        QByteArray retByte = sendSyncCommand(CMD_SET_DEFAULT_CFG, true);
        if (retByte.size() > 0) {
            qCDebug(log_core_serial) << "Factory reset the hid chip success.";
            if(eventCallback) eventCallback->onStatusUpdate("Factory reset the hid chip success.");
            return true;
        } else {
            qCWarning(log_core_serial) << "CH32V208 chip factory reset failed - chip may not support this command";
            if(eventCallback) eventCallback->onStatusUpdate("Factory reset the hid chip failure.");
            return false;
        }
    }

    // CH9329 chip - try current baudrate first, then alternative
    QByteArray retByte = sendSyncCommand(CMD_SET_DEFAULT_CFG, true);
    if (retByte.size() > 0) {
        qCDebug(log_core_serial) << "Factory reset the hid chip success.";
        if(eventCallback) eventCallback->onStatusUpdate("Factory reset the hid chip success.");
        return true;
    } else{
        qCDebug(log_core_serial) << "Factory reset the hid chip fail.";
        // toggle to another baudrate
        serialPort->close();
        setBaudRate(anotherBaudrate());
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
        return true;
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
        
        // Wait progressively longer between attempts - use event loop
        QEventLoop loop;
        QTimer::singleShot(100 * (attempt + 1), &loop, &QEventLoop::quit);
        loop.exec();
        
        // Clear error before retry
        serialPort->clearError();
    }
    
    if (openResult) {
        qCDebug(log_core_serial) << "Open port" << portName + ", baudrate: " << baudRate;
        serialPort->setRequestToSend(false);

        // Clear any stale data in the serial port buffers to prevent data corruption
        // This is critical when device is unplugged and replugged
        qCDebug(log_core_serial) << "Clearing serial port buffers to remove stale data";
        serialPort->clear(QSerialPort::AllDirections);
        
        // Also clear our internal incomplete data buffer
        {
            QMutexLocker bufferLocker(&m_bufferMutex);
            if (!m_incompleteDataBuffer.isEmpty()) {
                qCDebug(log_core_serial) << "Clearing internal incomplete data buffer on port open";
                m_incompleteDataBuffer.clear();
            }
        }
        
        // Reset error counters on successful connection
        resetErrorCounters();
        
        // Reset error frequency tracking and ensure error handler is connected
        m_errorCount = 0;
        m_errorTrackingTimer.restart();
        if (m_errorHandlerDisconnected) {
            qCDebug(log_core_serial) << "Reconnecting error handler after successful port opening";
            connect(serialPort, QOverload<QSerialPort::SerialPortError>::of(&QSerialPort::errorOccurred),
                   this, &SerialPortManager::handleSerialError);
            m_errorHandlerDisconnected = false;
        }

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
        
        // Reset error handler state when port is closed
        m_errorHandlerDisconnected = false;
        m_errorCount = 0;
        m_errorTrackingTimer.restart();
    } else {
        qCDebug(log_core_serial) << "Serial port is not opened.";
    }
    
    ready = false;
    if (eventCallback != nullptr) {
        eventCallback->onPortConnected("NA", 0);
    }
    
    // Clear incomplete data buffer when closing port
    {
        QMutexLocker bufferLocker(&m_bufferMutex);
        if (!m_incompleteDataBuffer.isEmpty()) {
            qCDebug(log_core_serial) << "Clearing incomplete data buffer on port close";
            m_incompleteDataBuffer.clear();
        }
    }
    
    // Stop watchdog while port is closed
    stopConnectionWatchdog();
    
    // Use non-blocking timer instead of msleep - removed delay as it's not critical
    // Port closing should be immediate, any OS-level delays are handled internally
}

bool SerialPortManager::restartPort() {
    QString portName = serialPort->portName();
    qint32 baudRate = serialPort->baudRate();
    qCDebug(log_core_serial) << "Restart port" << portName << "baudrate:" << baudRate;
    if (eventCallback != nullptr) {
        eventCallback->serialPortReset(true);
    }
    closePort();
    
    // Use non-blocking timer instead of msleep
    QTimer::singleShot(100, this, [this, portName, baudRate]() {
        openPort(portName, baudRate);
        onSerialPortConnected(portName);
        if (eventCallback != nullptr) {
            eventCallback->serialPortReset(false);
        }
    });
    
    return ready;
}


void SerialPortManager::updateSpecialKeyState(uint8_t data){

    qCDebug(log_core_serial) << "Data received: " << data;
    NumLockState = (data & 0b00000001) != 0; // NumLockState bit
    CapsLockState = (data & 0b00000010) != 0; // CapsLockState bit
    ScrollLockState = (data & 0b00000100) != 0; // ScrollLockState bit
    
    // Notify callback about key state changes
    if (eventCallback != nullptr) {
        qDebug(log_core_serial) << "NumLockState:" << NumLockState 
                               << "CapsLockState:" << CapsLockState 
                               << "ScrollLockState:" << ScrollLockState;
        eventCallback->onKeyStatesChanged(NumLockState, CapsLockState, ScrollLockState);
    }
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

        // Prepend any buffered incomplete data from previous reads
    QByteArray completeData;
    {
        QMutexLocker bufferLocker(&m_bufferMutex);
        if (!m_incompleteDataBuffer.isEmpty()) {
            completeData = m_incompleteDataBuffer + data;
            m_incompleteDataBuffer.clear();
            qCDebug(log_core_serial) << "Prepended buffered data. Total size:" << completeData.size();
        } else {
            completeData = data;
        }
    }
    
    if (completeData.size() >= 6) {
        // Process one or more complete packets in the buffer
        while (!completeData.isEmpty()) {
            // If not enough bytes for the minimal packet, keep as incomplete
            if (completeData.size() < 6) {
                QMutexLocker bufferLocker(&m_bufferMutex);
                m_incompleteDataBuffer = completeData;
                qCDebug(log_core_serial) << "Buffered incomplete data packet of size:" << completeData.size() << "Data:" << completeData.toHex(' ');
                break;
            }

            // Validate header; resync if necessary
            if (static_cast<unsigned char>(completeData[0]) != 0x57 || static_cast<unsigned char>(completeData[1]) != 0xAB) {
                if (!resyncAndAlignHeader(completeData)) {
                    // No header found, resync function has already buffered state
                    break;
                }
                // After resync, check again for minimal length
                if (completeData.size() < 6) {
                    QMutexLocker bufferLocker(&m_bufferMutex);
                    m_incompleteDataBuffer = completeData;
                    qCDebug(log_core_serial) << "Buffered incomplete header after resync. Size:" << completeData.size() << "Data:" << completeData.toHex(' ');
                    break;
                }
            }

            // Extract payload length from 5th byte (index 4)
            int payloadLen = static_cast<unsigned char>(completeData[4]);
            int packetSize = 6 + payloadLen; // header (2) + reserved(1) + cmd(1) + len(1) + payload(len) + checksum(1)

            if (completeData.size() < packetSize) {
                // Not enough bytes yet - buffer and wait for next read
                QMutexLocker bufferLocker(&m_bufferMutex);
                m_incompleteDataBuffer = completeData;
                qCDebug(log_core_serial) << "Incomplete packet: expected size" << packetSize << "have size" << completeData.size();
                break;
            }

            // Extract the whole packet
            QByteArray packet = completeData.left(packetSize);
            // Remove processed packet from buffer
            completeData = completeData.mid(packetSize);

            // Validate packet header just in case
            if (static_cast<unsigned char>(packet[0]) != 0x57 || static_cast<unsigned char>(packet[1]) != 0xAB) {
                qCWarning(log_core_serial) << "Packet header invalid, skipping and resyncing. Data:" << packet.toHex(' ');
                if (!resyncAndAlignHeader(completeData)) {
                    break;
                }
                continue;
            }

            // Optional checksum validation: calculate over packet without the last checksum byte
            quint8 computed = 0;
            for (int i = 0; i < packetSize - 1; ++i) {
                computed = static_cast<quint8>((computed + static_cast<unsigned char>(packet[i])) & 0xFF);
            }
            quint8 pktChecksum = static_cast<unsigned char>(packet[packetSize - 1]);
            if (computed != pktChecksum) {
                qCWarning(log_core_serial) << "Checksum mismatch for packet (computed: 0x" << QString::number(computed, 16)
                                           << ", given: 0x" << QString::number(pktChecksum, 16) << "). Dropping packet:" << packet.toHex(' ');
                m_consecutiveErrors++;
                continue;
            }

            // Reset consecutive errors on successful data read
            resetErrorCounters();
            m_lastSuccessfulCommand.restart();

            unsigned char cmdCode = static_cast<unsigned char>(packet[3]);
            unsigned char responseKey = static_cast<unsigned char>(cmdCode | 0x80);

            // If there's a pending synchronous request, check for match
            bool handledBySync = false;
            {
                QMutexLocker syncLocker(&m_syncResponseMutex);
                if (m_pendingSyncCommand && responseKey == m_pendingSyncExpectedKey) {
                    // Store the response packet and notify the waiting thread
                    m_syncCommandResponse = packet;
                    m_syncResponseCondition.wakeAll();
                    handledBySync = true;
                } else if (m_pendingSyncCommand && responseKey != m_pendingSyncExpectedKey) {
                    // A sync request is pending but this packet does not match - ignore it for now
                    qCWarning(log_core_serial) << "Sync request pending but got unmatched command" << QString("0x%1").arg(cmdCode, 2, 16, QChar('0')) << "- ignoring until expected response arrives";
                    // intentionally drop this packet when a sync is pending
                    continue;
                }
            }

            if (handledBySync) {
                // Do not process synced packet further here; it's delivered to whoever called sendSyncCommand
                continue;
            }

            // Normal async processing for non-sync flows
            unsigned char status = static_cast<unsigned char>(packet[5]);

            if (status != DEF_CMD_SUCCESS && (cmdCode >= 0xC0 && cmdCode <= 0xCF)) {
                dumpError(status, packet);
                m_consecutiveErrors++;
            } else {
                qCDebug(log_core_serial) << "Receive from serial port @" << (serialPort ? serialPort->baudRate() : 0) << ":" << packet.toHex(' ');
                static QSettings settings("Techxartisan", "Openterface");
                latestUpdateTime = QDateTime::currentDateTime();
                ready = true;
                unsigned char code = static_cast<unsigned char>(cmdCode | 0x80);
                int checkedBaudrate = 0;
                uint8_t mode = (settings.value("hardware/operatingMode", 0x02).toUInt());
                uint8_t chip_mode = packet[5];

                switch (code) {
                    case 0x81:
                        isTargetUsbConnected = CmdGetInfoResult::fromByteArray(packet).targetConnected == 0x01;
                        if (eventCallback != nullptr) {
                            eventCallback->onTargetUsbConnected(isTargetUsbConnected);
                        }
                        updateSpecialKeyState(CmdGetInfoResult::fromByteArray(packet).indicators);
                        break;
                    case 0x82:
                        qCDebug(log_core_serial) << "Keyboard event sent, status" << statusCodeToString(packet[5]);
                        break;
                    case 0x84:
                        qCDebug(log_core_serial) << "Absolute mouse event sent, status" << statusCodeToString(packet[5]);
                        break;
                    case 0x85:
                        qCDebug(log_core_serial) << "Relative mouse event sent, status" << statusCodeToString(packet[5]);
                        break;
                    case 0x88:
                        // get parameter configuration
                        if (packet.size() >= 12) {
                            checkedBaudrate = ((unsigned char)packet[8] << 24) | ((unsigned char)packet[9] << 16) | ((unsigned char)packet[10] << 8) | (unsigned char)packet[11];
                            qCDebug(log_core_serial) << "Current serial port baudrate rate:" << checkedBaudrate << ", Mode:" << "0x" + QString::number(mode, 16);
                        } else {
                            qCWarning(log_core_serial) << "Incomplete parameter configuration response - expected at least 12 bytes, got:" << packet.size() << "Data:" << packet.toHex(' ');
                        }
                        break;
                    case 0x89:
                        qCDebug(log_core_serial) << "Set parameter configuration, status" << statusCodeToString(packet[5]);
                        if (packet[5] == DEF_CMD_SUCCESS) {
                            qCDebug(log_core_serial) << "Parameter configuration successful, emitting signal for reset command";
                            emit parameterConfigurationSuccess();
                        }
                        break;
                    case 0x8F:
                        qCDebug(log_core_serial) << "Reset command, status" << statusCodeToString(packet[5]);
                        if (packet[5] == DEF_CMD_SUCCESS) {
                            qCDebug(log_core_serial) << "Factory reset successful, clearing stored baudrate";
                        }
                        break;
                    default:
                        qCDebug(log_core_serial) << "Unknown command: " << packet.toHex(' ');
                        break;
                }
            }

            // Callback for processed packet
            emit dataReceived(packet);
        }
    } else {
        // Store incomplete data in buffer for next batch
        {
            QMutexLocker bufferLocker(&m_bufferMutex);
            
            // Check if buffer would exceed maximum size
            if (m_incompleteDataBuffer.size() + completeData.size() > MAX_BUFFER_SIZE) {
                qCWarning(log_core_serial) << "Buffer overflow protection: clearing old buffer. Old size:" 
                                           << m_incompleteDataBuffer.size() << "New data size:" << completeData.size();
                m_incompleteDataBuffer.clear();
                m_consecutiveErrors++;
            }
            
            m_incompleteDataBuffer = completeData;
            qCDebug(log_core_serial) << "Buffered incomplete data packet of size:" << completeData.size() << "Data:" << completeData.toHex(' ');
        }
        // Don't increment error counter for incomplete data, as it will be processed in next batch
    }
    
    // Check if recovery is needed based on error count
    if (isRecoveryNeeded()) {
        attemptRecovery();
    }
    
    // qCDebug(log_core_serial) << "Recv read" << data;
}

/*
 * Reconfigure the HID chip to the specified baudrate and mode
 */
/*
 * Reconfigure the HID chip to the specified baudrate and mode
 * CH32V208: Does not support command-based configuration, returns false
 * CH9329: Supports command-based configuration for baudrate switching
 */
bool SerialPortManager::reconfigureHidChip(int targetBaudrate)
{
    // CH32V208 chip does not support command-based reconfiguration
    if (isChipTypeCH32V208()) {
        qCInfo(log_core_serial) << "CH32V208 chip does not support command-based reconfiguration - use close/reopen instead";
        return false;
    }
    
    static QSettings settings("Techxartisan", "Openterface");
    uint8_t mode = (settings.value("hardware/operatingMode", 0x02).toUInt());
    qCDebug(log_core_serial) << "Reconfigure to baudrate to" << targetBaudrate << "and mode 0x" << QString::number(mode, 16);
    
    // Select the appropriate command prefix based on target baudrate
    QByteArray command;
    if (targetBaudrate == BAUDRATE_LOWSPEED) {
        command = CMD_SET_PARA_CFG_PREFIX_9600;
        qCDebug(log_core_serial) << "Using 9600 baudrate configuration";
    } else {
        command = CMD_SET_PARA_CFG_PREFIX_115200;
        qCDebug(log_core_serial) << "Using 115200 baudrate configuration";
    }
    
    command[5] = mode;  // Set mode byte at index 5 (6th byte)

    //append from date 12...31
    command.append(CMD_SET_PARA_CFG_MID);
    
    qCDebug(log_core_serial) << "Sending configuration command:" << command.toHex(' ');
    QByteArray retBtyes = sendSyncCommand(command, true);
    
    qCDebug(log_core_serial) << "Configuration response size:" << retBtyes.size() << "data:" << retBtyes.toHex(' ');
    
    if(retBtyes.size() > 0){
        CmdDataResult dataResult = fromByteArray<CmdDataResult>(retBtyes);
        if(dataResult.data == DEF_CMD_SUCCESS){
            qCDebug(log_core_serial) << "Set data config success, reconfig to" << targetBaudrate << "baudrate and mode 0x" << QString::number(mode, 16);
            return true;
        }else{
            qCWarning(log_core_serial) << "Set data config fail with status code:" << QString("0x%1").arg(dataResult.data, 2, 16, QChar('0'));
            dumpError(dataResult.data, retBtyes);
        } 
    }else{
        qCWarning(log_core_serial) << "Set data config response empty. Port may not be responding.";
        qCWarning(log_core_serial) << "Current port:" << (serialPort ? serialPort->portName() : "null") 
                                   << "Baudrate:" << (serialPort ? QString::number(serialPort->baudRate()) : "N/A")
                                   << "Open:" << (serialPort && serialPort->isOpen() ? "Yes" : "No");
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
        
        // Flush immediately instead of blocking wait - serial port should handle buffering
        serialPort->flush();
        
        qCDebug(log_core_serial) << "Data written to serial port:" << serialPort->portName()
                     << "baudrate:" << serialPort->baudRate() << ":" << data.toHex(' ');
        
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
    
    if (data.size() < 4) {
        qCWarning(log_core_serial) << "Invalid command length for sendSyncCommand";
        return QByteArray();
    }

    // Determine request command and expected response code
    unsigned char requestCmd = static_cast<unsigned char>(data[3]);
    unsigned char expectedRespKey = static_cast<unsigned char>(requestCmd | 0x80);

    // Prepare for sync response
    {
        QMutexLocker locker(&m_syncResponseMutex);
        m_pendingSyncCommand = true;
        m_pendingSyncExpectedKey = expectedRespKey;
        m_syncCommandResponse.clear();
    }

    emit dataSent(data);
    QByteArray command = data;
    command.append(calculateChecksum(command));
    // qCDebug(log_core_serial) <<  "Check sum" << command.toHex(' ');
    writeData(command);

    // Wait for response from readData() which will fill m_syncCommandResponse
    QElapsedTimer timer;
    timer.start();
    const int totalTimeoutMs = 1000;
    const int waitStepMs = 50;
    QByteArray responseData;

    // Small initial delay to allow device processing
    QThread::msleep(60);

    {
        QMutexLocker lock(&m_syncResponseMutex);
        while (timer.elapsed() < totalTimeoutMs && m_syncCommandResponse.isEmpty()) {
            m_syncResponseCondition.wait(&m_syncResponseMutex, waitStepMs);
        }
        responseData = m_syncCommandResponse;
        // clear pending flag always
        m_pendingSyncCommand = false;
        m_pendingSyncExpectedKey = 0;
    }

    if (!responseData.isEmpty()) {
        emit dataReceived(responseData);
    } else {
        qCWarning(log_core_serial) << "Timeout waiting for sync response for command" << QString("0x%1").arg(requestCmd, 2, 16, QChar('0'));
    }

    return responseData;
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
        
        // Use non-blocking timer instead of msleep
        QTimer::singleShot(500, this, [this]() {
            if (serialPort) {
                serialPort->setDataTerminalReady(false);
            }
        });
    }
}

/*
 * Switch USB to host via serial command (new CH32V208 protocol)
 * Command: 57 AB 00 17 05 00 00 00 00 00 + checksum
 * Asynchronous - sends command without waiting for response
 */
void SerialPortManager::switchUsbToHostViaSerial() {
    qCDebug(log_core_serial) << "Switching USB to host via serial command (async)...";
    
    if (!serialPort || !serialPort->isOpen()) {
        qCWarning(log_core_serial) << "Serial port not open, cannot switch USB to host";
        return;
    }
    
    // Only use this method for CH32V208 chips
    if (!isChipTypeCH32V208()) {
        qCDebug(log_core_serial) << "Not CH32V208 chip, skipping serial-based USB switch";
        return;
    }
    
    sendAsyncCommand(CMD_SWITCH_USB_TO_HOST, true);
    qCInfo(log_core_serial) << "USB switch to host command sent asynchronously";
}

/*
 * Switch USB to target via serial command (new CH32V208 protocol)
 * Command: 57 AB 00 17 05 00 00 00 00 01 + checksum
 * Asynchronous - sends command without waiting for response
 */
void SerialPortManager::switchUsbToTargetViaSerial() {
    qCDebug(log_core_serial) << "Switching USB to target via serial command (async)...";
    
    if (!serialPort || !serialPort->isOpen()) {
        qCWarning(log_core_serial) << "Serial port not open, cannot switch USB to target";
        return;
    }
    
    // Only use this method for CH32V208 chips
    if (!isChipTypeCH32V208()) {
        qCDebug(log_core_serial) << "Not CH32V208 chip, skipping serial-based USB switch";
        return;
    }
    
    sendAsyncCommand(CMD_SWITCH_USB_TO_TARGET, true);
    qCInfo(log_core_serial) << "USB switch to target command sent asynchronously";
}

/*
 * Check USB switch status via serial command (new CH32V208 protocol)
 * Command: 57 AB 00 17 05 00 00 00 00 03 + checksum
 * Returns: 0 if pointing to host, 1 if pointing to target, -1 on error
 */
int SerialPortManager::checkUsbStatusViaSerial() {
    qCDebug(log_core_serial) << "Checking USB switch status via serial command...";
    
    if (!serialPort || !serialPort->isOpen()) {
        qCWarning(log_core_serial) << "Serial port not open, cannot check USB status";
        return -1;
    }
    
    // Only use this method for CH32V208 chips
    if (!isChipTypeCH32V208()) {
        qCDebug(log_core_serial) << "Not CH32V208 chip, skipping serial-based USB status check";
        return -1;
    }
    
    QByteArray response = sendSyncCommand(CMD_CHECK_USB_STATUS, true);
    
    if (response.size() > 0) {
        qCDebug(log_core_serial) << "Check USB status response:" << response.toHex(' ');
        
        // Expected response: 57 AB 00 17 01 <status> + checksum
        // status: 0x00 = host, 0x01 = target
        if (response.size() >= 7 && 
            response[0] == 0x57 && response[1] == (char)0xAB && 
            response[2] == 0x00 && response[3] == 0x17 &&
            response[4] == 0x01) {
            
            int status = static_cast<unsigned char>(response[5]);
            if (status == 0x00) {
                qCInfo(log_core_serial) << "USB is currently pointing to HOST";
                return 0;
            } else if (status == 0x01) {
                qCInfo(log_core_serial) << "USB is currently pointing to TARGET";
                return 1;
            } else {
                qCWarning(log_core_serial) << "Unknown USB status value:" << QString::number(status, 16);
                return -1;
            }
        } else {
            qCWarning(log_core_serial) << "Unexpected response for check USB status:" << response.toHex(' ');
            return -1;
        }
    }
    
    qCWarning(log_core_serial) << "No response received for check USB status command";
    return -1;
}

/*
* Set the USB configuration
*/
void SerialPortManager::setUSBconfiguration(int targetBaudrate){
    QSettings settings("Techxartisan", "Openterface");
    
    // Select the appropriate command prefix based on target baudrate
    QByteArray command;
    if (targetBaudrate == BAUDRATE_LOWSPEED) {
        command = CMD_SET_PARA_CFG_PREFIX_9600;
        qCDebug(log_core_serial) << "Using 9600 baudrate configuration for USB setup";
    } else {
        command = CMD_SET_PARA_CFG_PREFIX_115200;
        qCDebug(log_core_serial) << "Using 115200 baudrate configuration for USB setup";
    }

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
        int delayIndex = 0;
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
                    // Use delayed execution to avoid blocking
                    QTimer::singleShot(10 * delayIndex++, this, [this, command]() {
                        QByteArray respon = sendSyncCommand(command, true);
                        qDebug(log_core_serial) << respon;
                        qDebug(log_core_serial) << " After sending command";
                    });
                }
                qCDebug(log_core_serial) <<  "usb descriptor" << command.toHex(' ');
            }
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
    
    // Check if this is an CH32V208 chip (only supports 115200)
    if (serialPort && serialPort->isOpen()) {
        QString portName = serialPort->portName();
        QList<QSerialPortInfo> availablePorts = QSerialPortInfo::availablePorts();
        for (const QSerialPortInfo &portInfo : availablePorts) {
            if (portName.indexOf(portInfo.portName())>=0) {
                QString vid = QString("%1").arg(portInfo.vendorIdentifier(), 4, 16, QChar('0')).toUpper();
                QString pid = QString("%1").arg(portInfo.productIdentifier(), 4, 16, QChar('0')).toUpper();
                
                uint32_t detectedVidPid = (vid.toUInt(nullptr, 16) << 16) | pid.toUInt(nullptr, 16);
                if (detectedVidPid == static_cast<uint32_t>(ChipType::CH32V208)) {
                    if (baudRate != BAUDRATE_HIGHSPEED) {
                        qCWarning(log_core_serial) << "CH32V208 chip only supports 115200 baudrate. Ignoring user request for" << baudRate;
                        if (eventCallback) {
                            eventCallback->onStatusUpdate("CH32V208 chip only supports 115200 baudrate");
                        }
                        return;
                    }
                }
                break;
            }
        }
    }
    
    // Store the user selection immediately
    GlobalSetting::instance().setSerialPortBaudrate(baudRate);
    
    // Handle CH32V208 chip - simple close/reopen, no commands
    if (isChipTypeCH32V208()) {
        qCInfo(log_core_serial) << "CH32V208 chip - using simple close/reopen (baudrate must be 115200)";
        QString portName = serialPort->portName();
        closePort();
        
        // Use non-blocking timer instead of msleep
        QTimer::singleShot(100, this, [this, portName]() {
            if (openPort(portName, BAUDRATE_HIGHSPEED)) {
                qCInfo(log_core_serial) << "CH32V208 chip successfully switched to 115200";
                onSerialPortConnected(portName);
            } else {
                qCWarning(log_core_serial) << "Failed to reopen CH32V208 chip";
            }
        });
        return;
    }
    
    // Handle CH9329 chip - use commands
    if (isChipTypeCH9329()) {
        qCInfo(log_core_serial) << "CH9329 chip - using command-based baudrate change";
        applyCommandBasedBaudrateChange(baudRate, "CH9329 chip: User selected baudrate");
        return;
    }
    
    // Unknown chip - try CH9329 approach as fallback
    qCWarning(log_core_serial) << "Unknown chip type - attempting CH9329 approach";
    applyCommandBasedBaudrateChange(baudRate, "User selected baudrate");
}

void SerialPortManager::clearStoredBaudrate() {
    qCDebug(log_core_serial) << "Clearing stored baudrate setting";
    GlobalSetting::instance().clearSerialPortBaudrate();
}

// Chip type detection and management
ChipType SerialPortManager::detectChipType(const QString &portName) const
{
    QList<QSerialPortInfo> availablePorts = QSerialPortInfo::availablePorts();
    for (const QSerialPortInfo &portInfo : availablePorts) {
        if (portName.indexOf(portInfo.portName()) >= 0) {
            QString vid = QString("%1").arg(portInfo.vendorIdentifier(), 4, 16, QChar('0')).toUpper();
            QString pid = QString("%1").arg(portInfo.productIdentifier(), 4, 16, QChar('0')).toUpper();
            
            qCDebug(log_core_serial) << "Detected VID:PID =" << vid << ":" << pid << "for port" << portName;
            
            uint32_t detectedVidPid = (vid.toUInt(nullptr, 16) << 16) | pid.toUInt(nullptr, 16);
            
            if (detectedVidPid == static_cast<uint32_t>(ChipType::CH9329)) {
                qCInfo(log_core_serial) << "Detected CH9329 chip - supports 9600 and 115200 with command-based configuration";
                return ChipType::CH9329;
            } else if (detectedVidPid == static_cast<uint32_t>(ChipType::CH32V208)) {
                qCInfo(log_core_serial) << "Detected CH32V208 chip - only supports 115200 baudrate, no command-based configuration";
                return ChipType::CH32V208;
            }
            
            break;
        }
    }
    
    qCWarning(log_core_serial) << "Unknown chip type for port" << portName;
    return ChipType::UNKNOWN;
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
    if (baudrate == BAUDRATE_LOWSPEED || !isArmArchitecture()) {
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

void SerialPortManager::clearIncompleteDataBuffer()
{
    QMutexLocker bufferLocker(&m_bufferMutex);
    if (!m_incompleteDataBuffer.isEmpty()) {
        qCDebug(log_core_serial) << "Manually clearing incomplete data buffer. Size:" << m_incompleteDataBuffer.size();
        m_incompleteDataBuffer.clear();
    }
}

void SerialPortManager::handleSerialError(QSerialPort::SerialPortError error)
{
    if (error == QSerialPort::NoError || m_isShuttingDown) {
        return;
    }
    
    // Check if error frequency tracking timer needs reset (more than 1 second elapsed)
    if (m_errorTrackingTimer.elapsed() > 1000) {
        m_errorCount = 0;
        m_errorTrackingTimer.restart();
    }
    
    // Increment error count for frequency tracking
    m_errorCount++;
    
    // Auto-disconnect mechanism: if more than 5 errors in one second
    if (m_errorCount > MAX_ERRORS_PER_SECOND && !m_errorHandlerDisconnected) {
        qCWarning(log_core_serial) << "Too many serial errors (" << m_errorCount.load() 
                                   << ") within one second. Disconnecting error handler to prevent flooding.";
        
        if (serialPort) {
            disconnect(serialPort, QOverload<QSerialPort::SerialPortError>::of(&QSerialPort::errorOccurred),
                      this, &SerialPortManager::handleSerialError);
        }
        m_errorHandlerDisconnected = true;
        
        // Schedule reconnection after 5 seconds
        QTimer::singleShot(5000, this, [this]() {
            if (serialPort && !m_isShuttingDown) {
                qCInfo(log_core_serial) << "Reconnecting error handler after cooldown period";
                connect(serialPort, QOverload<QSerialPort::SerialPortError>::of(&QSerialPort::errorOccurred),
                       this, &SerialPortManager::handleSerialError);
                m_errorHandlerDisconnected = false;
                m_errorCount = 0;
                m_errorTrackingTimer.restart();
            }
        });
        
        // Force immediate recovery attempt for the current error
        ready = false;
        attemptRecovery();
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

// Attempt to resynchronize the buffer to the next valid header sequence (0x57 0xAB).
// If resynchronization succeeds and completeData contains at least the minimal packet length,
// return true. Otherwise update m_incompleteDataBuffer accordingly and return false.
bool SerialPortManager::resyncAndAlignHeader(QByteArray &completeData)
{
    // Confirm we don't already have a valid header
    if (!completeData.isEmpty() && completeData.size() >= 2 &&
        static_cast<unsigned char>(completeData[0]) == 0x57 &&
        static_cast<unsigned char>(completeData[1]) == 0xAB) {
        return true;
    }

    // Find the next valid header 0x57 0xAB within the buffer
    int headerIndex = -1;
    for (int i = 0; i + 1 < completeData.size(); ++i) {
        if (static_cast<unsigned char>(completeData[i]) == 0x57 &&
            static_cast<unsigned char>(completeData[i + 1]) == 0xAB) {
            headerIndex = i;
            break;
        }
    }

    if (headerIndex == -1) {
        // No header found: keep trailing 0x57 if present as probable partial header
        QByteArray keep;
        if (!completeData.isEmpty() && static_cast<unsigned char>(completeData.back()) == 0x57) {
            keep = QByteArray(1, char(0x57));
            qCWarning(log_core_serial) << "No valid header found. Keeping trailing 0x57 byte and dropping rest. Full data:" << completeData.toHex(' ');
        } else {
            qCWarning(log_core_serial) << "No valid header found. Dropping data:" << completeData.toHex(' ');
        }
        {
            QMutexLocker bufferLocker(&m_bufferMutex);
            m_incompleteDataBuffer = keep;
        }
        m_consecutiveErrors++;
        return false;
    }

    // Found a header at some offset, drop everything before it
    if (headerIndex > 0) {
        qCWarning(log_core_serial) << "Skipping" << headerIndex << "bytes before valid header. Dropped:" << completeData.left(headerIndex).toHex(' ')
                                   << "Remaining:" << completeData.mid(headerIndex).toHex(' ');
    }

    completeData = completeData.mid(headerIndex);

    // If not enough bytes remain to be a valid minimal packet, buffer and wait
    if (completeData.size() < 6) {
        QMutexLocker bufferLocker(&m_bufferMutex);
        m_incompleteDataBuffer = completeData;
        qCDebug(log_core_serial) << "Buffered incomplete header after resync. Size:" << completeData.size() << "Data:" << completeData.toHex(' ');
        return false;
    }

    return true;
}

void SerialPortManager::applyCommandBasedBaudrateChange(int baudRate, const QString& logPrefix)
{
    QByteArray command;
    static QSettings settings("Techxartisan", "Openterface");
    uint8_t mode = (settings.value("hardware/operatingMode", 0x02).toUInt());
    if (baudRate == BAUDRATE_LOWSPEED) {
        command = CMD_SET_PARA_CFG_PREFIX_9600;
    } else {
        command = CMD_SET_PARA_CFG_PREFIX_115200;
    }
    command[5] = mode; 
    command.append(CMD_SET_PARA_CFG_MID);
    sendAsyncCommand(command, true);
    bool success = sendResetCommand() && setBaudRate(baudRate) && restartPort();
    if (success) {
        qCInfo(log_core_serial) << logPrefix << "applied successfully:" << baudRate;
    } else {
        qCWarning(log_core_serial) << logPrefix << "Failed to apply user selected baudrate:" << baudRate;
    }
}