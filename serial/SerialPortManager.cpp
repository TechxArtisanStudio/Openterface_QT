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
#include "FactoryResetManager.h"
#include "SerialCommandCoordinator.h"
#include "SerialStateManager.h"
#include "SerialStatistics.h"
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
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <unistd.h>
#include <errno.h>


Q_LOGGING_CATEGORY(log_core_serial, "opf.core.serial")

// Define static constants
const int SerialPortManager::BAUDRATE_HIGHSPEED;
const int SerialPortManager::BAUDRATE_LOWSPEED;
const int SerialPortManager::DEFAULT_BAUDRATE;
const int SerialPortManager::SERIAL_TIMER_INTERVAL;

SerialPortManager::SerialPortManager(QObject *parent) : QObject(parent), serialPort(nullptr), m_serialWorkerThread(new QThread(nullptr)), serialTimer(new QTimer(nullptr)),
    m_connectionWatchdog(nullptr), m_errorRecoveryTimer(nullptr), m_usbStatusCheckTimer(nullptr), m_getInfoTimer(nullptr){
    qCDebug(log_core_serial) << "Initialize serial port.";

    // Set name for the serial worker thread for better logging
    m_serialWorkerThread->setObjectName("SerialWorkerThread");

    this->moveToThread(m_serialWorkerThread);

    // Initialize port chain tracking member variables
    m_currentSerialPortPath = QString();
    m_currentSerialPortChain = QString();
    
    // Initialize elapsed timers (these don't need thread affinity)
    m_lastSuccessfulCommand.start();
    m_errorTrackingTimer.start();
    
    // Initialize protocol layer (Phase 2 refactoring)
    m_protocol = std::make_unique<SerialProtocol>(nullptr);
    
    // Initialize command coordinator (Phase 4 refactoring)
    m_commandCoordinator = std::make_unique<SerialCommandCoordinator>(nullptr);
    
    // Initialize state manager (Phase 4 refactoring)
    m_stateManager = std::make_unique<SerialStateManager>(nullptr);
    
    // Initialize statistics module (Phase 4 refactoring)
    m_statistics = std::make_unique<SerialStatistics>(nullptr);
    
    // Connect command coordinator with statistics module
    m_commandCoordinator->setStatisticsModule(m_statistics.get());
    
    // Initialize connection watchdog (Phase 3 refactoring)
    m_watchdog = std::make_unique<ConnectionWatchdog>(nullptr);
    m_watchdog->setRecoveryHandler(this);  // SerialPortManager implements IRecoveryHandler
    
    // Configure watchdog
    WatchdogConfig watchdogConfig;
    watchdogConfig.maxConsecutiveErrors = m_maxConsecutiveErrors;
    watchdogConfig.maxRetryAttempts = m_maxRetryAttempts;
    watchdogConfig.autoRecoveryEnabled = m_autoRecoveryEnabled;
    m_watchdog->setConfig(watchdogConfig);
    
    // Connect watchdog signals
    connect(m_watchdog.get(), &ConnectionWatchdog::statusUpdate, this, &SerialPortManager::statusUpdate);
    connect(m_watchdog.get(), &ConnectionWatchdog::recoveryFailed, this, [this]() {
        ready = false;
        qCCritical(log_core_serial) << "Connection watchdog: recovery failed";
    });
    connect(m_watchdog.get(), &ConnectionWatchdog::connectionStateChanged, this, [this](ConnectionState state) {
        qCDebug(log_core_serial) << "Connection state changed to:" << static_cast<int>(state);
    });
    
    // Connect command coordinator signals to SerialPortManager
    connect(m_commandCoordinator.get(), &SerialCommandCoordinator::dataSent, this, &SerialPortManager::dataSent);
    connect(m_commandCoordinator.get(), &SerialCommandCoordinator::dataReceived, this, &SerialPortManager::dataReceived);
    connect(m_commandCoordinator.get(), &SerialCommandCoordinator::commandExecuted, this, [this](const QByteArray& cmd, bool success) {
        qCDebug(log_core_serial) << "Tx: :" << cmd.toHex(' ') << "Success:" << success;
    });
    
    // Connect state manager signals to SerialPortManager signals
    connect(m_stateManager.get(), &SerialStateManager::keyStatesChanged, this, &SerialPortManager::keyStatesChanged);
    connect(m_stateManager.get(), &SerialStateManager::targetUsbStatusChanged, this, &SerialPortManager::targetUSBStatus);
    connect(m_stateManager.get(), &SerialStateManager::connectionStateChanged, this, [this](ConnectionState newState, ConnectionState oldState) {
        Q_UNUSED(oldState);
        // Update ready flag when connection state changes
        ready = (newState == ConnectionState::Connected);
        qCDebug(log_core_serial) << "Connection state changed, ready=" << ready;
    });
    connect(m_stateManager.get(), &SerialStateManager::serialPortInfoChanged, this, [this](const SerialPortInfo& newInfo, const SerialPortInfo& oldInfo) {
        Q_UNUSED(oldInfo);
        emit serialPortDeviceChanged(oldInfo.portPath, newInfo.portPath);
        emit connectedPortChanged(newInfo.portPath, newInfo.baudRate);
    });
    
    // Connect statistics module signals to SerialPortManager signals
    connect(m_statistics.get(), &SerialStatistics::statisticsUpdated, this, [this](const StatisticsData& data) {
        qCDebug(log_core_serial) << "Statistics updated - Commands sent:" << data.commandsSent
                                << "Responses received:" << data.responsesReceived
                                << "Error rate:" << QString::number(data.errorRate(), 'f', 2) << "%";
    });
    connect(m_statistics.get(), &SerialStatistics::performanceThresholdExceeded, this, [this](const QString& metric, int currentValue, int threshold) {
        qCWarning(log_core_serial) << "Performance threshold exceeded for" << metric 
                                   << "- Current:" << currentValue << "Threshold:" << threshold;
    });
    connect(m_statistics.get(), &SerialStatistics::recoveryRecommended, this, [this](const QString& reason) {
        qCWarning(log_core_serial) << "Recovery recommended:" << reason;
        emit statusUpdate(QString("Recovery recommended: %1").arg(reason));
    });
    connect(m_statistics.get(), &SerialStatistics::armBaudrateRecommendation, this, [this](int current, int recommended) {
        emit armBaudratePerformanceRecommendation(current);
        qCDebug(log_core_serial) << "ARM baudrate recommendation: Current=" << current << "Recommended=" << recommended;
    });
    
    // Connect protocol layer signals to SerialPortManager
    connect(m_protocol.get(), &SerialProtocol::getInfoReceived, this, [this](bool targetConnected, uint8_t indicators) {
            // Fallback - direct emission
            emit targetUSBStatus(targetConnected);
            updateSpecialKeyState(indicators);
    });
    connect(m_protocol.get(), &SerialProtocol::usbSwitchStatusReceived, this, &SerialPortManager::usbStatusChanged);
    connect(m_protocol.get(), &SerialProtocol::paramConfigReceived, this, [this](int baudrate, uint8_t mode) {
        qCDebug(log_core_serial) << "Current serial port baudrate:" << baudrate << ", Mode: 0x" << QString::number(mode, 16);
    });
    connect(m_protocol.get(), &SerialProtocol::setParamConfigReceived, this, [this](uint8_t status) {
        qCDebug(log_core_serial) << "Set parameter configuration, status:" << m_protocol->statusToString(status);
        if (status == SerialProtocolConstants::STATUS_SUCCESS) {
            qCDebug(log_core_serial) << "Parameter configuration successful, emitting signal for reset command";
            emit parameterConfigurationSuccess();
        }
    });
    connect(m_protocol.get(), &SerialProtocol::resetResponseReceived, this, [this](uint8_t status) {
        qCDebug(log_core_serial) << "Reset command, status:" << m_protocol->statusToString(status);
    });
    
    // IMPORTANT: Timers must be created in the worker thread to avoid cross-thread issues
    // Use QThread::started signal to create timers after moveToThread takes effect
    connect(m_serialWorkerThread, &QThread::started, this, [this]() {
        qCDebug(log_core_serial) << "Worker thread started, creating timers in worker thread context";
        
        // Create timers in the worker thread context
        m_connectionWatchdog = new QTimer(this);
        m_errorRecoveryTimer = new QTimer(this);
        m_usbStatusCheckTimer = new QTimer(this);
        m_getInfoTimer = new QTimer(this);
        
        m_connectionWatchdog->setSingleShot(true);
        m_errorRecoveryTimer->setSingleShot(true);
        m_usbStatusCheckTimer->setInterval(2000);  // Check every 2 seconds
        m_getInfoTimer->setInterval(3000);  // Send GET_INFO every 3 seconds
        
        connect(m_usbStatusCheckTimer, &QTimer::timeout, this, &SerialPortManager::onUsbStatusCheckTimeout);
        connect(m_getInfoTimer, &QTimer::timeout, this, &SerialPortManager::onGetInfoTimeout);
        
        setupConnectionWatchdog();
        
        qCDebug(log_core_serial) << "Timers created successfully in worker thread";
    }, Qt::DirectConnection);  // DirectConnection ensures it runs in the worker thread

    connect(this, &SerialPortManager::serialPortConnected, this, &SerialPortManager::onSerialPortConnected);
    connect(this, &SerialPortManager::serialPortDisconnected, this, &SerialPortManager::onSerialPortDisconnected);
    connect(this, &SerialPortManager::serialPortConnectionSuccess, this, &SerialPortManager::onSerialPortConnectionSuccess);
    
    // Connect thread-safe reset operation signals to handlers (QueuedConnection ensures they run in worker thread)
    connect(this, &SerialPortManager::requestResetHidChip, this, &SerialPortManager::handleResetHidChip, Qt::QueuedConnection);
    connect(this, &SerialPortManager::requestFactoryReset, this, &SerialPortManager::handleFactoryReset, Qt::QueuedConnection);
    connect(this, &SerialPortManager::requestFactoryResetV191, this, &SerialPortManager::handleFactoryResetV191, Qt::QueuedConnection);
    
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

    // Initialize FactoryResetManager and forward its signals for backward compatibility
    m_factoryResetManager = std::make_unique<FactoryResetManager>(this);
    connect(m_factoryResetManager.get(), &FactoryResetManager::factoryReset, this, &SerialPortManager::factoryReset, Qt::QueuedConnection);
    connect(m_factoryResetManager.get(), &FactoryResetManager::factoryResetCompleted, this, &SerialPortManager::factoryResetCompleted, Qt::QueuedConnection);

    observeSerialPortNotification();
    m_lastCommandTime.start();
    m_commandDelayMs = 0;  // Default no delay
    lastSerialPortCheckTime = QDateTime::currentDateTime().addMSecs(-SERIAL_TIMER_INTERVAL);  // Initialize check time in the past 
    
    // Connect to hotplug monitor for automatic device management
    connectToHotplugMonitor();
    
    // Initialize asynchronous logging
    QString logPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/serial_log.txt";
    m_logFilePath = logPath;
    m_logThread = new QThread(this);
    m_logWriter = new LogWriter(logPath, this);
    m_logWriter->moveToThread(m_logThread);
    connect(this, &SerialPortManager::logMessage, m_logWriter, &LogWriter::writeLog);
    m_logThread->start();
    
    qCDebug(log_core_serial) << "SerialPortManager initialized with DeviceManager integration and enhanced stability features";
}

void SerialPortManager::observeSerialPortNotification(){
    qCDebug(log_core_serial) << "Created a timer to observer SerialPort...";

    serialTimer->moveToThread(m_serialWorkerThread);

    connect(m_serialWorkerThread, &QThread::finished, serialTimer, &QObject::deleteLater);
    connect(m_serialWorkerThread, &QThread::finished, m_serialWorkerThread, &QObject::deleteLater);
    connect(this, &SerialPortManager::sendCommandAsync, this, &SerialPortManager::sendCommand);
    
    m_serialWorkerThread->start();
}

void SerialPortManager::stop() {
    qCDebug(log_core_serial) << "Stopping serial port manager...";
    
    // Set shutdown flag to prevent new operations
    m_isShuttingDown = true;
    
    // Stop watchdog timers
    stopConnectionWatchdog();
    
    // Prevent callback access during shutdown
    eventCallback = nullptr;
    
    if (m_serialWorkerThread && m_serialWorkerThread->isRunning()) {
        m_serialWorkerThread->quit();
        m_serialWorkerThread->wait(3000); // Wait up to 3 seconds
    }
    
    if (serialPort && serialPort->isOpen()) {
        closePort();
    }
    
    // Clear command coordinator queue
    if (m_commandCoordinator) {
        m_commandCoordinator->clearCommandQueue();
    }
    
    // Clear state manager
    if (m_stateManager) {
        m_stateManager->clearAllStates();
    }
    
    // Clear command queue
    if (m_commandCoordinator) {
        m_commandCoordinator->clearCommandQueue();
    }
    
    qCDebug(log_core_serial) << "Serial port manager stopped";
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
    return m_stateManager ? m_stateManager->getCurrentPortPath() : QString();
}

QString SerialPortManager::getCurrentSerialPortChain() const
{
    return m_stateManager ? m_stateManager->getCurrentPortChain() : QString();
}

int SerialPortManager::getCurrentBaudrate() const
{
    if (m_stateManager) {
        int stateBaudrate = m_stateManager->getCurrentBaudRate();
        if (stateBaudrate > 0) {
            return stateBaudrate;
        }
    }
    
    // Fallback to serial port if state manager not available or has invalid baudrate
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

        // Update current device tracking in state manager
        if (m_stateManager) {
            SerialPortInfo newPortInfo;
            newPortInfo.portPath = selectedDevice.serialPortPath;
            newPortInfo.portChain = portChain;
            newPortInfo.baudRate = getCurrentBaudrate();
            newPortInfo.chipType = m_currentChipType;
            m_stateManager->setSerialPortInfo(newPortInfo);
        }
        
        // Update legacy tracking for backward compatibility
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

/*
 * Open the serial port and check the baudrate and mode
 */
void SerialPortManager::onSerialPortConnected(const QString &portName){
    qCDebug(log_core_serial) << "Serial port connected: " << portName;
    // Also explicitly log during diagnostics
    if (!m_logFilePath.contains("serial_log.txt")) {
        log(QString("Serial port connected: %1").arg(portName));
    }
    
    // Detect chip type FIRST
    m_currentChipType = detectChipType(portName);
    qCDebug(log_core_serial) << "Detected chip type:" << (m_currentChipType == ChipType::CH9329 ? "CH9329" : 
                                               m_currentChipType == ChipType::CH32V208 ? "CH32V208" : "Unknown");
    if (!m_logFilePath.contains("serial_log.txt")) {
        log(QString("Detected chip type: %1").arg(m_currentChipType == ChipType::CH9329 ? "CH9329" : 
                                                   m_currentChipType == ChipType::CH32V208 ? "CH32V208" : "Unknown"));
    }
    
    // Update state manager with port and chip info
    if (m_stateManager) {
        SerialPortInfo portInfo;
        portInfo.portPath = portName;
        portInfo.chipType = m_currentChipType;
        m_stateManager->setSerialPortInfo(portInfo);
        m_stateManager->setConnectionState(ConnectionState::Connecting);
    }
    
    // Create appropriate chip strategy based on detected chip type
    m_chipStrategy = ChipStrategyFactory::createStrategyForPort(portName);
    qCInfo(log_core_serial) << "Using chip strategy:" << m_chipStrategy->chipName();
    
    int tryBaudrate = determineBaudrate();
    
    if (!openPortWithRetries(portName, tryBaudrate)) {
        qCWarning(log_core_serial) << "Failed to open port after retries";
        return;
    }
    
    ConfigResult config = sendAndProcessConfigCommand();
    const int maxRetries = 2; // Keep same retry behavior as before
    const int retryDelayMs = 1000; // 1 second delay between retries (non-blocking)

    if (config.success) {
        handleChipSpecificLogic(config);
        storeBaudrateIfNeeded(config.workingBaudrate);
        emit serialPortConnectionSuccess(portName);
    } else {
        qCWarning(log_core_serial) << "Configuration command failed, scheduling async retry attempts (maxRetries=" << maxRetries << ")";
        // Schedule the first asynchronous retry (attempt #1)

        QThread::msleep(retryDelayMs);
        scheduleConfigRetry(portName, 1, maxRetries, retryDelayMs);
    }
}

int SerialPortManager::determineBaudrate() const {
    int stored = GlobalSetting::instance().getSerialPortBaudrate();
    
    // Use chip strategy if available
    if (m_chipStrategy) {
        return m_chipStrategy->determineInitialBaudrate(stored);
    }
    
    // Fallback to legacy behavior
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
        qCWarning(log_core_serial).nospace() << "The mode is incorrect, mode: 0x" << QString::number(config.mode, 16)
                       << ", expected: 0x" << QString::number(hostConfigMode, 16);
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
    QString portName = serialPort->portName();
    // Handle CH32V208 chip: Only supports 115200 baudrate, no command-based configuration
    if (isChipTypeCH32V208()) {
        qCInfo(log_core_serial) << "CH32V208 chip: Only supports 115200, retrying at 115200";
        closePort();
        openPort(portName, BAUDRATE_HIGHSPEED);
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
        openPort(portName, altBaudrate);
        QByteArray retByte = sendSyncCommand(CMD_GET_PARA_CFG, true);
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
        openPort(portName, altBaudrate);
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
        
        // Update state manager
        if (m_stateManager) {
            m_stateManager->setConnectionState(ConnectionState::Connected);
            m_stateManager->setBaudRate(config.workingBaudrate);
            m_stateManager->resetErrorCounters();
            m_stateManager->updateLastSuccessfulCommand();
        }
        
        resetErrorCounters();
        m_lastSuccessfulCommand.restart();
    }
}

void SerialPortManager::storeBaudrateIfNeeded(int workingBaudrate) {
    int stored = GlobalSetting::instance().getSerialPortBaudrate();
    if (stored != workingBaudrate) {
        // Use chip strategy to validate baudrate if available
        if (m_chipStrategy) {
            workingBaudrate = m_chipStrategy->validateBaudrate(workingBaudrate);
        } else if (isChipTypeCH32V208() && workingBaudrate != BAUDRATE_HIGHSPEED) {
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
    
    // Update state manager
    if (m_stateManager) {
        m_stateManager->setConnectionState(ConnectionState::Disconnected);
    }
    
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
    // Extra debug: confirm readyRead signals and thread id (and bytesAvailable at the moment)
    // This lambda helps validate whether readyRead is fired and on which thread context.
    // connect(serialPort, &QSerialPort::readyRead, this, [this]() {
        // qCDebug(log_core_serial) << "readyRead: emitted; threadId:" << (qulonglong)QThread::currentThreadId()
        //                          << "port:" << (serialPort ? serialPort->portName() : QString("null"))
        //                          << "bytesAvailable:" << (serialPort ? serialPort->bytesAvailable() : -1)
        //                          << "bytesToWrite:" << (serialPort ? serialPort->bytesToWrite() : -1);
    // });
    
    // Connect error signal for enhanced error handling
    connect(serialPort, QOverload<QSerialPort::SerialPortError>::of(&QSerialPort::errorOccurred),
            this, &SerialPortManager::handleSerialError);
    
    ready = true;
    resetErrorCounters();
    m_lastSuccessfulCommand.restart();

    emit connectedPortChanged(portName, serialPort->baudRate());
    qCDebug(log_core_serial) << "Serial port opened successfully:" << portName << "at" << serialPort->baudRate() << "baud";
    // Also explicitly log during diagnostics
    if (!m_logFilePath.contains("serial_log.txt")) {
        log(QString("Serial port opened successfully: %1 at %2 baud").arg(portName).arg(serialPort->baudRate()));
    }

    qCDebug(log_core_serial) << "Enable the switchable USB now...";
    // serialPort->setDataTerminalReady(false);

    // Start connection watchdog (Phase 3 refactoring)
    if (m_watchdog) {
        m_watchdog->start();
        qCDebug(log_core_serial) << "Started ConnectionWatchdog";
    }
    // NOTE: Legacy setupConnectionWatchdog() removed - ConnectionWatchdog handles monitoring

    // Start USB status check timer for CH32V208 (thread-safe)
    if (isChipTypeCH32V208() && m_usbStatusCheckTimer) {
        if (QThread::currentThread() == m_usbStatusCheckTimer->thread()) {
            m_usbStatusCheckTimer->start();
        } else {
            QMetaObject::invokeMethod(m_usbStatusCheckTimer, "start", Qt::QueuedConnection);
        }
        qCDebug(log_core_serial) << "Started USB status check timer for CH32V208";
    }

    // Start GET_INFO timer for periodic status updates
    if (m_getInfoTimer) {
        if (QThread::currentThread() == m_getInfoTimer->thread()) {
            m_getInfoTimer->start();
        } else {
            QMetaObject::invokeMethod(m_getInfoTimer, "start", Qt::QueuedConnection);
        }
        qCDebug(log_core_serial) << "Started periodic GET_INFO timer";
    }

    sendAsyncCommand(CMD_GET_INFO, true);
}

void SerialPortManager::setEventCallback(StatusEventCallback* callback) {
    eventCallback = callback;
}

/* 
 * Reset the hid chip, set the baudrate to specified rate and mode to 0x82 and reset the chip
 * This is a thread-safe wrapper that emits a signal to perform the actual reset in the worker thread
 * CH32V208: Only supports 115200, just close and reopen with 115200
 * CH9329: Supports both baudrates, requires reconfiguration command + reset command
 */
bool SerialPortManager::resetHipChip(int targetBaudrate){
    qCDebug(log_core_serial) << "Reset HID chip requested from thread:" << QThread::currentThread()->objectName();
    qCDebug(log_core_serial) << "Resetting HID chip to baudrate:" << targetBaudrate;
    // Also explicitly log during diagnostics
    if (!m_logFilePath.contains("serial_log.txt")) {
        log(QString("Resetting HID chip to baudrate: %1").arg(targetBaudrate));
    }
    
    // If called from the worker thread, execute directly
    if (QThread::currentThread() == this->thread()) {
        return handleResetHidChipInternal(targetBaudrate);
    }
    
    // Otherwise, emit signal to execute in worker thread (non-blocking)
    emit requestResetHidChip(targetBaudrate);
    return true;  // Return true as the request was queued successfully
}

// Internal implementation that runs in the worker thread
bool SerialPortManager::handleResetHidChipInternal(int targetBaudrate) {
    qCDebug(log_core_serial) << "Reset the hid chip now (internal)...";
    
    if (!serialPort) {
        qCWarning(log_core_serial) << "Serial port is null, cannot reset";
        emit resetHidChipCompleted(false);
        return false;
    }
    
    QString portName = serialPort->portName();
    bool success = false;
    
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
            bool reopenSuccess = false;
            if (openPort(portName, targetBaudrate)) {
                qCInfo(log_core_serial) << "CH32V208 chip successfully reopened at 115200";
                onSerialPortConnected(portName);
                reopenSuccess = true;
            } else {
                qCWarning(log_core_serial) << "Failed to reopen CH32V208 chip at 115200";
            }
            emit resetHidChipCompleted(reopenSuccess);
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
                success = true;
            }else{
                qCWarning(log_core_serial) << "Reset the hid chip fail - send reset command failed";
            }
        }else{
            qCWarning(log_core_serial) << "Set data config fail - reconfigureHidChip returned false";
            ready = false;
            qCDebug(log_core_serial) << "Target baudrate was: " << targetBaudrate << "Current baudrate: " << serialPort->baudRate();
        }
        emit resetHidChipCompleted(success);
        return success;
    }
    
    // Unknown chip type - try the CH9329 approach as fallback
    qCWarning(log_core_serial) << "Unknown chip type - attempting CH9329 approach";
    if(reconfigureHidChip(targetBaudrate)) {
        qCDebug(log_core_serial) << "Reset the hid chip success.";
        if(sendResetCommand()){
            qCDebug(log_core_serial) << "Reopen the serial port with baudrate: " << targetBaudrate;
            setBaudRate(targetBaudrate);
            restartPort();
            success = true;
        }else{
            qCWarning(log_core_serial) << "Reset the hid chip fail - send reset command failed";
        }
    }else{
        qCWarning(log_core_serial) << "Set data config fail - reconfigureHidChip returned false";
        ready = false;
        qCDebug(log_core_serial) << "Target baudrate was: " << targetBaudrate << "Current baudrate: " << serialPort->baudRate();
    }
    emit resetHidChipCompleted(success);
    return success;
}

// Slot handler for thread-safe reset operation
void SerialPortManager::handleResetHidChip(int targetBaudrate) {
    qCDebug(log_core_serial) << "handleResetHidChip slot called in thread:" << QThread::currentThread()->objectName();
    handleResetHidChipInternal(targetBaudrate);
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

void SerialPortManager::onUsbStatusCheckTimeout() {
    if (m_isShuttingDown || !serialPort || !serialPort->isOpen() || !isChipTypeCH32V208()) {
        return;  // Skip if shutting down, port not open, or not CH32V208
    }

    sendAsyncCommand(CMD_CHECK_USB_STATUS, true);
    qCDebug(log_core_serial) << "Sent USB status check command asynchronously";
}

void SerialPortManager::onGetInfoTimeout() {
    if (m_isShuttingDown || !serialPort || !serialPort->isOpen()) {
        return;  // Skip if shutting down or port not open
    }

    sendAsyncCommand(CMD_GET_INFO, true);
    qCDebug(log_core_serial) << "Sent periodic GET_INFO command asynchronously";
}

/*
 * Supported hardware 1.9 and > 1.9.1
 * Factory reset the hid chip by holding the RTS pin to low for 4 seconds
 * This is a thread-safe wrapper that emits a signal to perform the actual reset in the worker thread
 * CH32V208: Uses RTS pin reset method only
 * CH9329: Uses RTS pin reset method
 */
bool SerialPortManager::factoryResetHipChip(){
    qCDebug(log_core_serial) << "Factory reset HID chip requested from thread:" << QThread::currentThread()->objectName();
    
    // If called from the worker thread, execute directly
    if (QThread::currentThread() == this->thread()) {
        return handleFactoryResetInternal();
    }
    
    // Otherwise, emit signal to execute in worker thread (non-blocking)
    emit requestFactoryReset();
    return true;  // Return true as the request was queued successfully
}

// Internal implementation that runs in the worker thread
bool SerialPortManager::handleFactoryResetInternal() {
    // Delegate to FactoryResetManager for implementation
    if (m_factoryResetManager) {
        return m_factoryResetManager->handleFactoryResetInternal();
    }
    qCWarning(log_core_serial) << "FactoryResetManager not initialized";
    emit factoryResetCompleted(false);
    return false;
}

// Slot handler for thread-safe factory reset operation
void SerialPortManager::handleFactoryReset() {
    qCDebug(log_core_serial) << "handleFactoryReset slot called in thread:" << QThread::currentThread()->objectName();
    handleFactoryResetInternal();
}

/*
 * Supported hardware == 1.9.1
 * Factory reset the hid chip by sending set default cfg command
 * This is a thread-safe wrapper that emits a signal to perform the actual reset in the worker thread
 * CH32V208: May not support this command, will try at 115200 only
 * CH9329: Supports this command at both baudrates
 */
bool SerialPortManager::factoryResetHipChipV191(){
    qCDebug(log_core_serial) << "Factory reset HID chip V191 requested from thread:" << QThread::currentThread()->objectName();
    
    // If called from the worker thread, execute directly
    if (QThread::currentThread() == this->thread()) {
        return handleFactoryResetV191Internal();
    }
    
    // Otherwise, emit signal to execute in worker thread (non-blocking)
    emit requestFactoryResetV191();
    return true;  // Return true as the request was queued successfully
}

// Internal implementation that runs in the worker thread
bool SerialPortManager::handleFactoryResetV191Internal() {
    if (m_factoryResetManager) {
        return m_factoryResetManager->handleFactoryResetV191Internal();
    }
    qCWarning(log_core_serial) << "FactoryResetManager not initialized";
    emit factoryResetCompleted(false);
    return false;
}

// Slot handler for thread-safe factory reset V191 operation
void SerialPortManager::handleFactoryResetV191() {
    qCDebug(log_core_serial) << "handleFactoryResetV191 slot called in thread:" << QThread::currentThread()->objectName();
    handleFactoryResetV191Internal();
}

/*
 * Synchronous factory reset for diagnostics - RTS pin method
 * This blocks until reset is complete or timeout occurs
 */
bool SerialPortManager::factoryResetHipChipSync(int timeoutMs) {
    qCDebug(log_core_serial) << "Synchronous factory reset HID chip requested, timeout:" << timeoutMs << "ms";
    
    // Always execute directly in the calling thread for diagnostics
    return handleFactoryResetSyncInternal(timeoutMs);
}

/*
 * Synchronous factory reset V191 for diagnostics - command method
 * This blocks until reset is complete or timeout occurs
 */
bool SerialPortManager::factoryResetHipChipV191Sync(int timeoutMs) {
    qCDebug(log_core_serial) << "Synchronous factory reset V191 HID chip requested, timeout:" << timeoutMs << "ms";
    
    // Always execute directly in the calling thread for diagnostics
    return handleFactoryResetV191SyncInternal(timeoutMs);
}

/*
 * Internal synchronous factory reset implementation - RTS pin method
 */
bool SerialPortManager::handleFactoryResetSyncInternal(int timeoutMs) {
    if (m_factoryResetManager) {
        return m_factoryResetManager->handleFactoryResetSyncInternal(timeoutMs);
    }
    qCWarning(log_core_serial) << "FactoryResetManager not initialized";
    return false;
}

/*
 * Internal synchronous factory reset V191 implementation - command method
 */
bool SerialPortManager::handleFactoryResetV191SyncInternal(int timeoutMs) {
    if (m_factoryResetManager) {
        return m_factoryResetManager->handleFactoryResetV191SyncInternal(timeoutMs);
    }
    qCWarning(log_core_serial) << "FactoryResetManager not initialized";
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
    
    // Stop ConnectionWatchdog (Phase 3)
    if (m_watchdog) {
        m_watchdog->setShuttingDown(true);
        m_watchdog->stop();
    }
        
    // Properly stop the manager first
    stop();
    
    // Disconnect from hotplug monitor
    disconnectFromHotplugMonitor();
    
    // Clean up timers - use thread-safe stopping first
    // Note: Since we're in destructor and thread may be stopped, we need to be careful
    // The stop() call above should have already stopped the worker thread
    
    if (m_connectionWatchdog) {
        // If thread is still running, use invokeMethod; otherwise direct stop is safe
        if (m_serialWorkerThread && m_serialWorkerThread->isRunning() && 
            QThread::currentThread() != m_connectionWatchdog->thread()) {
            QMetaObject::invokeMethod(m_connectionWatchdog, "stop", Qt::BlockingQueuedConnection);
        } else {
            m_connectionWatchdog->stop();
        }
        m_connectionWatchdog->deleteLater();
        m_connectionWatchdog = nullptr;
    }
    
    if (m_errorRecoveryTimer) {
        if (m_serialWorkerThread && m_serialWorkerThread->isRunning() && 
            QThread::currentThread() != m_errorRecoveryTimer->thread()) {
            QMetaObject::invokeMethod(m_errorRecoveryTimer, "stop", Qt::BlockingQueuedConnection);
        } else {
            m_errorRecoveryTimer->stop();
        }
        m_errorRecoveryTimer->deleteLater();
        m_errorRecoveryTimer = nullptr;
    }
    
    // Clean up USB status check timer
    if (m_usbStatusCheckTimer) {
        if (m_serialWorkerThread && m_serialWorkerThread->isRunning() && 
            QThread::currentThread() != m_usbStatusCheckTimer->thread()) {
            QMetaObject::invokeMethod(m_usbStatusCheckTimer, "stop", Qt::BlockingQueuedConnection);
        } else {
            m_usbStatusCheckTimer->stop();
        }
        m_usbStatusCheckTimer->deleteLater();
        m_usbStatusCheckTimer = nullptr;
    }
    
    // Final cleanup
    if (serialPort) {
        delete serialPort;
        serialPort = nullptr;
    }
    
    // Clean up logging thread
    if (m_logThread) {
        m_logThread->quit();
        m_logThread->wait();
        delete m_logThread;
        m_logThread = nullptr;
    }
    if (m_logWriter) {
        delete m_logWriter;
        m_logWriter = nullptr;
    }
    
    qCDebug(log_core_serial) << "Serial port manager destroyed";
}

/*
 * Open the serial port
 */
bool SerialPortManager::openPort(const QString &portName, int baudRate) {
    qCDebug(log_core_serial) << "Trying to open serial port: " << portName << ", baudrate: " << baudRate;
    if (m_isShuttingDown) {
        qCDebug(log_core_serial) << "Cannot open port during shutdown";
        return false;
    }
    
    QMutexLocker locker(&m_serialPortMutex);
    
    if (serialPort != nullptr && serialPort->isOpen()) {
        qCDebug(log_core_serial) << "Serial port is already opened.";
        return true;
    }
    
    emit statusUpdate("Going to open the port");
    
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
        // serialPort->setReadBufferSize(4096);  // Set a larger read buffer size
        qCDebug(log_core_serial) << "Open port" << portName + ", baudrate: " << baudRate << "with read buffer size" << serialPort->readBufferSize();
        // serialPort->setRequestToSend(false);

        // Show existing buffer sizes before clearing them (read and write sizes)
        qCDebug(log_core_serial) << "Serial buffer sizes before clear - bytesAvailable:" << serialPort->bytesAvailable()
                                 << "bytesToWrite:" << serialPort->bytesToWrite();

        // Clear any stale data in the serial port buffers to prevent data corruption
        // This is critical when device is unplugged and replugged
        qCDebug(log_core_serial) << "Clearing serial port buffers to remove stale data";
        serialPort->clear(QSerialPort::AllDirections);

        // Log buffer sizes after clearing to confirm the clear worked
        qCDebug(log_core_serial) << "Serial buffer sizes after clear - bytesAvailable:" << serialPort->bytesAvailable()
                                 << "bytesToWrite:" << serialPort->bytesToWrite();
        
        
        // Reset error counters on successful connection
        resetErrorCounters();

        emit statusUpdate("");
        emit connectedPortChanged(portName, baudRate);
        qCDebug(log_core_serial) << "Serial port: " << portName << ", baudrate: " << baudRate << "opened";
        return true;
    } else {
        QString errorMsg = QString("Open port failure: %1 (Error: %2)")
                          .arg(serialPort->errorString())
                          .arg(static_cast<int>(lastError));
        qCWarning(log_core_serial) << errorMsg;

        emit statusUpdate(errorMsg);
        return false;
    }
}

/*
 * Close the serial port
 */
void SerialPortManager::closePort() {
    // Ensure closePort is called in the worker thread to avoid QSocketNotifier issues
    if (QThread::currentThread() != m_serialWorkerThread) {
        qCDebug(log_core_serial) << "closePort called from different thread, routing through worker thread";
        QEventLoop waitLoop;
        QMetaObject::invokeMethod(this, [this, &waitLoop]() {
            closePortInternal();
            waitLoop.quit();
        }, Qt::QueuedConnection);
        waitLoop.exec();
        return;
    }
    
    // Already in worker thread, proceed directly
    closePortInternal();
}

void SerialPortManager::closePortInternal() {
    qCDebug(log_core_serial) << "Close serial port";
    
    QMutexLocker locker(&m_serialPortMutex);
    
    if (serialPort != nullptr) {
        if (serialPort->isOpen()) {
            // Disconnect all signals first - CRITICAL: do this BEFORE any QSerialPort operations
            disconnect(serialPort, &QSerialPort::readyRead, this, &SerialPortManager::readData);
            disconnect(serialPort, &QSerialPort::bytesWritten, this, &SerialPortManager::bytesWritten);
            disconnect(serialPort, QOverload<QSerialPort::SerialPortError>::of(&QSerialPort::errorOccurred),
                      this, &SerialPortManager::handleSerialError);
            
            qCDebug(log_core_serial) << "Close serial port - current buffer sizes before close - bytesAvailable:" << serialPort->bytesAvailable()
                                     << "bytesToWrite:" << serialPort->bytesToWrite();
            
            // IMPORTANT: After factory reset or physical device removal, QSerialPort's internal state can be corrupted.
            // Calling flush(), clear(), or close() can trigger QSocketNotifier warnings from internal Qt code.
            // To avoid crashes, we DIRECTLY close the underlying file descriptor without involving QSerialPort's signal/slot machinery.
            try {
                // Get the native file descriptor BEFORE closing anything
                int fd = serialPort->handle();
                
                // Directly close the file descriptor to bypass QSerialPort's QSocketNotifier manipulation
                // This is critical after factory reset or device removal when internal state is corrupted
                if (fd >= 0) {
                    int closeResult = ::close(fd);
                    if (closeResult == 0) {
                        qCDebug(log_core_serial) << "File descriptor closed directly (bypassed QSerialPort close)";
                    } else {
                        qCWarning(log_core_serial) << "Failed to close file descriptor:" << strerror(errno);
                    }
                }
                
                // Now safely delete the QSerialPort object without calling its close()
                // The object will be cleaned up, but without triggering QSocketNotifier operations
                qCDebug(log_core_serial) << "Serial port closed successfully";
            } catch (...) {
                qCWarning(log_core_serial) << "Exception during port closure";
            }
        }
        delete serialPort;
        serialPort = nullptr;
        
        // Reset error handler state when port is closed
        m_errorHandlerDisconnected = false;
        // LEGACY: m_errorCount removed - error tracking handled by SerialStatistics
        m_errorTrackingTimer.restart();
    } else {
        qCDebug(log_core_serial) << "Serial port is not opened.";
    }
    
    ready = false;
    // Notify listeners that port is not available
    emit connectedPortChanged("NA", 0);
    
    // Stop watchdog while port is closed (thread-safe)
    stopConnectionWatchdog();
    
    // Stop USB status check timer (thread-safe)
    if (m_usbStatusCheckTimer) {
        if (QThread::currentThread() == m_usbStatusCheckTimer->thread()) {
            m_usbStatusCheckTimer->stop();
        } else {
            QMetaObject::invokeMethod(m_usbStatusCheckTimer, "stop", Qt::QueuedConnection);
        }
        qCDebug(log_core_serial) << "Stopped USB status check timer";
    }
    
    // Use non-blocking timer instead of msleep - removed delay as it's not critical
    // Port closing should be immediate, any OS-level delays are handled internally
}

bool SerialPortManager::restartPort() {
    QString portName = serialPort->portName();
    qint32 baudRate = serialPort->baudRate();
    qCDebug(log_core_serial) << "Restart port" << portName << "baudrate:" << baudRate;
    
    // Record serial reset in statistics
    if (m_statistics) {
        m_statistics->recordSerialReset();
    }
    
    emit serialPortReset(true);
    closePort();
    
    // Use non-blocking timer instead of msleep
    QTimer::singleShot(100, this, [this, portName, baudRate]() {
        openPort(portName, baudRate);
        onSerialPortConnected(portName);
        emit serialPortReset(false);
    });
    
    return ready;
} 

void SerialPortManager::scheduleConfigRetry(const QString &portName, int attempt, int maxAttempts, int delayMs)
{
        if (m_isShuttingDown) {
            qCWarning(log_core_serial) << "scheduleConfigRetry: shutdown in progress, aborting retry";
            return;
        }
        if (!serialPort || !serialPort->isOpen()) {
            qCWarning(log_core_serial) << "scheduleConfigRetry: Serial port not open, aborting retry";
            return;
        }

        qCWarning(log_core_serial) << "Configuration retry attempt:" << attempt << "of" << maxAttempts;
        ConfigResult config = attemptBaudrateDetection();
        if (config.success) {
            qCInfo(log_core_serial) << "Configuration retry succeeded on attempt:" << attempt;
            handleChipSpecificLogic(config);
            storeBaudrateIfNeeded(config.workingBaudrate);
            emit serialPortConnectionSuccess(portName);
            return;
        }

        if (attempt < maxAttempts) {
            qCWarning(log_core_serial) << "Configuration still failed, scheduling next attempt:" << (attempt + 1) << ", portName:" << portName;
            scheduleConfigRetry(portName, attempt + 1, maxAttempts, delayMs);
        } else {
            qCWarning(log_core_serial) << "Configuration attempts exhausted after" << attempt << "tries";
        }

}


void SerialPortManager::updateSpecialKeyState(uint8_t data){
    qCDebug(log_core_serial) << "Data received:" << data;
    
    if (m_stateManager) {
        m_stateManager->updateKeyStates(data);
    } else {
        // Fallback to legacy behavior if state manager not available
    if (m_stateManager) {
        m_stateManager->updateKeyStates(data);
    } else {
        // Fallback - direct emission
        bool numLock = (data & 0b00000001) != 0;
        bool capsLock = (data & 0b00000010) != 0; 
        bool scrollLock = (data & 0b00000100) != 0;
        emit keyStatesChanged(numLock, capsLock, scrollLock);
    }
    }
}
/*
 * Read the data from the serial port
 */
void SerialPortManager::readData() {
    if (m_isShuttingDown || !serialPort || !serialPort->isOpen()) {
        qCDebug(log_core_serial) << "readData: Ignored read - shutting down or port not open";
        return;
    }
    
    QByteArray data;
    try {
        data = serialPort->readAll();
    } catch (...) {
        qCWarning(log_core_serial) << "Exception occurred while reading serial data";
        if (isRecoveryNeeded()) {
            attemptRecovery();
        }
        return;
    }
    
    if (data.isEmpty()) {
        qCDebug(log_core_serial) << "Received empty data from serial port";
        return;
    }

    // Use protocol layer for packet parsing (Phase 2 refactoring)
    using namespace SerialProtocolConstants;
    
    // Validate minimum packet size
    if (data.size() < MIN_PACKET_SIZE) {
        qCWarning(log_core_serial) << "Received packet too small, size:" << data.size() << "Data:" << data.toHex(' ');
        return;
    }
    
    // Use protocol layer to extract packet size
    int packetSize = m_protocol->extractPacketSize(data);
    if (packetSize < 0 || packetSize > data.size()) {
        qCWarning(log_core_serial) << "Invalid packet size:" << packetSize 
                                   << "actual data size:" << data.size()
                                   << "Data:" << data.toHex(' ');
        return;
    }
    
    QByteArray packet = data.left(packetSize);
    
    // Use protocol layer to parse packet
    ParsedPacket parsed = m_protocol->parsePacket(packet);
    if (!parsed.valid) {
        qCWarning(log_core_serial) << "Failed to parse packet:" << parsed.errorMessage;
        return;
    }
    
    // Check for error status in certain command ranges
    if (parsed.status != STATUS_SUCCESS && (parsed.commandCode >= 0xC0 && parsed.commandCode <= 0xCF)) {
        dumpError(parsed.status, packet);
    } else {
        qCDebug(log_core_serial).nospace().noquote() << "RX (" << serialPort->portName() << "@" 
            << (serialPort ? serialPort->baudRate() : 0) << "bps): " << packet.toHex(' ');
        
        // Also explicitly log RX to file during diagnostics
        if (!m_logFilePath.contains("serial_log.txt")) {
            log(QString("RX (%1): %2").arg(serialPort ? serialPort->baudRate() : 0).arg(QString(packet.toHex(' '))));
        }
        
        latestUpdateTime = QDateTime::currentDateTime();
        ready = true;
        
        // Process response using protocol layer - signals are already connected
        m_protocol->processRawData(packet);

        // Record response for statistics tracking (counts async responses)
        if (m_statistics) {
            m_statistics->recordResponseReceived();
        }
        
        // Additional chip-specific handling for 0x84 (absolute mouse) response
        if (parsed.responseCode == RESP_SEND_MOUSE_ABS && isChipTypeCH32V208()) {
            ready = true;
            emit targetUSBStatus(true);
        }
    }
    
    // Callback for processed packet
    emit dataReceived(packet);
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
    // Use chip strategy if available
    if (m_chipStrategy && !m_chipStrategy->supportsCommandBasedConfiguration()) {
        qCInfo(log_core_serial) << m_chipStrategy->chipName() << "does not support command-based reconfiguration - use close/reopen instead";
        return false;
    }
    
    // Fallback check for CH32V208 chip (backward compatibility)
    if (!m_chipStrategy && isChipTypeCH32V208()) {
        qCInfo(log_core_serial) << "CH32V208 chip does not support command-based reconfiguration - use close/reopen instead";
        return false;
    }
    
    static QSettings settings("Techxartisan", "Openterface");
    uint8_t mode = (settings.value("hardware/operatingMode", 0x02).toUInt());
    qCDebug(log_core_serial) << "Reconfigure to baudrate to" << targetBaudrate << "and mode 0x" << QString::number(mode, 16);
    
    // Use chip strategy to build configuration command if available
    QByteArray command;
    if (m_chipStrategy) {
        command = m_chipStrategy->buildReconfigurationCommand(targetBaudrate, mode);
        if (command.isEmpty()) {
            qCWarning(log_core_serial) << "Chip strategy returned empty configuration command";
            return false;
        }
    } else {
        // Legacy command building
        if (targetBaudrate == BAUDRATE_LOWSPEED) {
            command = CMD_SET_PARA_CFG_PREFIX_9600;
            qCDebug(log_core_serial) << "Using 9600 baudrate configuration";
        } else {
            command = CMD_SET_PARA_CFG_PREFIX_115200;
            qCDebug(log_core_serial) << "Using 115200 baudrate configuration";
        }
        command[5] = mode;  // Set mode byte at index 5 (6th byte)
        command.append(CMD_SET_PARA_CFG_MID);
    }
    
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
    
    // QMutexLocker locker(&m_serialPortMutex);
    
    if (!serialPort || !serialPort->isOpen()) {
        qCWarning(log_core_serial) << "Serial port not open, cannot write data";
        ready = false;
        // m_consecutiveErrors++;
        return false;
    }

    try {
        qint64 bytesWritten = serialPort->write(data);
        if (bytesWritten == -1) {
            qCWarning(log_core_serial) << "Failed to write data to serial port:" << serialPort->errorString();
            // m_consecutiveErrors++;
            return false;
        } else if (bytesWritten != data.size()) {
            qCWarning(log_core_serial) << "Partial write: expected" << data.size() << "bytes, wrote" << bytesWritten;
            // m_consecutiveErrors++;
            return false;
        }
        
        // Ensure data is flushed to OS driver and wait for kernel write completion
        serialPort->flush();

        qCDebug(log_core_serial).nospace().noquote() << "Data written (" << serialPort->portName()
                        << "@" << serialPort->baudRate() << "bps): " << data.toHex(' ');
        
        // Also explicitly log TX to file during diagnostics
        if (!m_logFilePath.contains("serial_log.txt")) {
            log(QString("TX (%1): %2").arg(serialPort->baudRate()).arg(QString(data.toHex(' '))));
        }
            
        
        return true;
        
    } catch (...) {
        qCCritical(log_core_serial) << "Exception occurred while writing to serial port";
        ready = false;
        return false;
    }
}

/*
 * Send the async command to the serial port
 */
bool SerialPortManager::sendAsyncCommand(const QByteArray &data, bool force) {
    if (m_isShuttingDown || !m_commandCoordinator) {
        return false;
    }
    
    // Update command coordinator ready state
    m_commandCoordinator->setReady(ready);
    
    // Delegate to command coordinator
    return m_commandCoordinator->sendAsyncCommand(serialPort, data, force);
}

 /*
 * Send the sync command to the serial port
 */
QByteArray SerialPortManager::sendSyncCommand(const QByteArray &data, bool force) {
    if (m_isShuttingDown || !m_commandCoordinator) {
        return QByteArray();
    }
    
    // Update command coordinator ready state
    m_commandCoordinator->setReady(ready);
    
    // Delegate to command coordinator
    return m_commandCoordinator->sendSyncCommand(serialPort, data, force);
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
    
    // Use chip strategy to check if USB switch is supported
    if (m_chipStrategy && !m_chipStrategy->supportsUsbSwitchCommand()) {
        qCDebug(log_core_serial) << m_chipStrategy->chipName() << "does not support serial-based USB switch";
        return;
    }
    
    // Fallback: Only use this method for CH32V208 chips
    if (!m_chipStrategy && !isChipTypeCH32V208()) {
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
    
    // Use chip strategy to check if USB switch is supported
    if (m_chipStrategy && !m_chipStrategy->supportsUsbSwitchCommand()) {
        qCDebug(log_core_serial) << m_chipStrategy->chipName() << "does not support serial-based USB switch";
        return;
    }
    
    // Fallback: Only use this method for CH32V208 chips
    if (!m_chipStrategy && !isChipTypeCH32V208()) {
        qCDebug(log_core_serial) << "Not CH32V208 chip, skipping serial-based USB switch";
        return;
    }
    
    sendAsyncCommand(CMD_SWITCH_USB_TO_TARGET, true);
    qCInfo(log_core_serial) << "USB switch to target command sent asynchronously";
}

/*
* Set the USB configuration
*/
void SerialPortManager::setUSBconfiguration(int targetBaudrate){
    QSettings settings("Techxartisan", "Openterface");
    uint8_t mode = (settings.value("hardware/operatingMode", 0x02).toUInt());

    // Select the appropriate command prefix based on target baudrate
    QByteArray command;
    if (targetBaudrate == BAUDRATE_LOWSPEED) {
        command = CMD_SET_PARA_CFG_PREFIX_9600;
        qCDebug(log_core_serial) << "Using 9600 baudrate configuration for USB setup";
    } else {
        command = CMD_SET_PARA_CFG_PREFIX_115200;
        qCDebug(log_core_serial) << "Using 115200 baudrate configuration for USB setup";
    }
    command[5] = mode;  // Set mode byte at index 5 (6th byte)

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
    if (!serialPort) {
        qCWarning(log_core_serial) << "Cannot set baud rate: serialPort is null";
        return false;
    }

    // If called from a different thread, route through worker thread via queued connection
    if (QThread::currentThread() != m_serialWorkerThread) {
        qCDebug(log_core_serial) << "setBaudRate called from different thread, routing through worker thread";
        bool result = false;
        QEventLoop waitLoop;
        
        // Use BlockingQueuedConnection to wait for the operation to complete
        QMetaObject::invokeMethod(this, [this, baudRate, &result, &waitLoop]() {
            // Call the actual baudrate setting logic
            result = setBaudRateInternal(baudRate);
            waitLoop.quit();
        }, Qt::QueuedConnection);
        
        waitLoop.exec();
        return result;
    }
    
    // Already in worker thread, proceed directly
    return setBaudRateInternal(baudRate);
}

bool SerialPortManager::setBaudRateInternal(int baudRate) {
    if (!serialPort) {
        qCWarning(log_core_serial) << "Cannot set baud rate: serialPort is null";
        return false;
    }

    if (serialPort->baudRate() == baudRate) {
        qCDebug(log_core_serial) << "Baud rate is already set to" << baudRate;
        // Keep state manager in sync
        if (m_stateManager) {
            m_stateManager->setBaudRate(baudRate);
        }
        return true;
    }

    qCDebug(log_core_serial) << "Setting baud rate to" << baudRate;
    
    if (serialPort->setBaudRate(baudRate)) {
        qCDebug(log_core_serial) << "Baud rate successfully set to" << baudRate;
        
        // Update state manager so getCurrentBaudrate() reflects actual host setting
        if (m_stateManager) {
            m_stateManager->setBaudRate(baudRate);
        }

        emit connectedPortChanged(serialPort->portName(), baudRate);
        return true;
    } else {
        qCWarning(log_core_serial) << "Failed to set baud rate to" << baudRate << ": " << serialPort->errorString();
        return false;
    }
} 

void SerialPortManager::setUserSelectedBaudrate(int baudRate) {
    qCDebug(log_core_serial) << "User manually selected baudrate:" << baudRate;
    
    // If we already know the chip type, prefer that check rather than re-detecting using VID/PID
    if (serialPort && serialPort->isOpen() && isChipTypeCH32V208()) {
        if (baudRate != BAUDRATE_HIGHSPEED) {
            qCWarning(log_core_serial) << "CH32V208 chip only supports 115200 baudrate. Ignoring user request for" << baudRate;
            if (eventCallback) {
                emit statusUpdate("CH32V208 chip only supports 115200 baudrate");
            }
            return;
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

    // Also reset runtime state so that getCurrentBaudrate() falls back to the actual serial port
    // This prevents stale state (e.g., 9600) causing tests to incorrectly report a mismatch after factory reset
    if (m_stateManager) {
        m_stateManager->setBaudRate(-1);
    }
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
    if (m_commandCoordinator) {
        m_commandCoordinator->setCommandDelay(delayMs);
    }
    
    // Keep local setting for backward compatibility
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

// Enhanced stability implementation (delegated to ConnectionWatchdog - Phase 3)

void SerialPortManager::enableAutoRecovery(bool enable)
{
    m_autoRecoveryEnabled = enable;
    if (m_watchdog) {
        m_watchdog->setAutoRecoveryEnabled(enable);
    }
    qCDebug(log_core_serial) << "Auto recovery" << (enable ? "enabled" : "disabled");
}

void SerialPortManager::setMaxRetryAttempts(int maxRetries)
{
    m_maxRetryAttempts = qMax(1, maxRetries);
    if (m_watchdog) {
        m_watchdog->setMaxRetryAttempts(m_maxRetryAttempts);
    }
    qCDebug(log_core_serial) << "Max retry attempts set to:" << m_maxRetryAttempts;
}

void SerialPortManager::setMaxConsecutiveErrors(int maxErrors)
{
    m_maxConsecutiveErrors = qMax(1, maxErrors);
    if (m_watchdog) {
        m_watchdog->setMaxConsecutiveErrors(m_maxConsecutiveErrors);
    }
    qCDebug(log_core_serial) << "Max consecutive errors set to:" << m_maxConsecutiveErrors;
}

bool SerialPortManager::isConnectionStable() const
{
    if (m_watchdog) {
        return m_watchdog->isConnectionStable();
    }
    return false;  // No watchdog available
}

int SerialPortManager::getConsecutiveErrorCount() const
{
    // Delegate to statistics module first
    if (m_statistics) {
        return m_statistics->getConsecutiveErrors();
    }
    
    // Fallback to watchdog
    if (m_watchdog) {
        return m_watchdog->getConsecutiveErrorCount();
    }
    return 0;
}

int SerialPortManager::getConnectionRetryCount() const
{
    return m_statistics ? m_statistics->getConnectionRetries() :
           (m_watchdog ? m_watchdog->getRetryAttemptCount() : 0);
}

void SerialPortManager::forceRecovery()
{
    qCInfo(log_core_serial) << "Force recovery requested";
    if (m_watchdog) {
        m_watchdog->forceRecovery();
    }
}


void SerialPortManager::handleSerialError(QSerialPort::SerialPortError error)
{
    QString errorString = serialPort ? serialPort->errorString() : "Unknown error";
    qCWarning(log_core_serial) << "Serial port error occurred:" << errorString << "Error code:" << static_cast<int>(error);
    
    // Record error in statistics module
    if (m_statistics && error != QSerialPort::NoError) {
        m_statistics->recordConsecutiveError();
    }
    
    // Report error to ConnectionWatchdog (Phase 3)
    if (m_watchdog && error != QSerialPort::NoError) {
        m_watchdog->recordError();
    }
}

void SerialPortManager::attemptRecovery()
{
    if (m_isShuttingDown || !m_autoRecoveryEnabled) {
        return;
    }
    
    qCInfo(log_core_serial) << "attemptRecovery called - delegating to ConnectionWatchdog";
    
    // Delegate to ConnectionWatchdog (Phase 3)
    if (m_watchdog) {
        m_watchdog->forceRecovery();
    }
}

void SerialPortManager::resetErrorCounters()
{
    if (m_statistics) {
        m_statistics->resetErrorCounters();
    }
    if (m_watchdog) {
        m_watchdog->resetCounters();
    }
    qCDebug(log_core_serial) << "Error counters reset";
}

bool SerialPortManager::isRecoveryNeeded() const
{
    // Delegate to ConnectionWatchdog (Phase 3)
    if (m_watchdog) {
        return m_watchdog->isRecoveryNeeded();
    }
    return false;
}

void SerialPortManager::setupConnectionWatchdog()
{
    // NOTE: This legacy method is kept for backward compatibility
    // ConnectionWatchdog class now handles connection monitoring (Phase 3)
    // This method is no longer called from onSerialPortConnectionSuccess()
    
    if (!m_connectionWatchdog) {
        qCDebug(log_core_serial) << "setupConnectionWatchdog: legacy timer not created, using ConnectionWatchdog class";
        return;
    }
    
    // Only start legacy timer if new ConnectionWatchdog is not available
    if (m_watchdog) {
        qCDebug(log_core_serial) << "setupConnectionWatchdog: ConnectionWatchdog is active, skipping legacy timer";
        return;
    }
    
    qCDebug(log_core_serial) << "setupConnectionWatchdog: starting legacy fallback timer";
    m_connectionWatchdog->setInterval(30000); // 30 seconds
    disconnect(m_connectionWatchdog, &QTimer::timeout, nullptr, nullptr);
    
    connect(m_connectionWatchdog, &QTimer::timeout, this, [this]() {
        if (m_isShuttingDown) {
            return;
        }
        if (m_lastSuccessfulCommand.elapsed() > 30000) {
            qCWarning(log_core_serial) << "Legacy watchdog triggered";
            forceRecovery();
        }
        if (m_connectionWatchdog) {
            m_connectionWatchdog->start();
        }
    });
    
    if (!m_isShuttingDown) {
        m_connectionWatchdog->start();
    }
}

void SerialPortManager::stopConnectionWatchdog()
{
    // Stop new ConnectionWatchdog (Phase 3)
    if (m_watchdog) {
        m_watchdog->stop();
    }
    
    // Stop legacy timers (thread-safe)
    if (m_connectionWatchdog) {
        if (QThread::currentThread() == m_connectionWatchdog->thread()) {
            m_connectionWatchdog->stop();
        } else {
            QMetaObject::invokeMethod(m_connectionWatchdog, "stop", Qt::QueuedConnection);
        }
    }
    if (m_errorRecoveryTimer) {
        if (QThread::currentThread() == m_errorRecoveryTimer->thread()) {
            m_errorRecoveryTimer->stop();
        } else {
            QMetaObject::invokeMethod(m_errorRecoveryTimer, "stop", Qt::QueuedConnection);
        }
    }
    if (m_usbStatusCheckTimer) {
        if (QThread::currentThread() == m_usbStatusCheckTimer->thread()) {
            m_usbStatusCheckTimer->stop();
        } else {
            QMetaObject::invokeMethod(m_usbStatusCheckTimer, "stop", Qt::QueuedConnection);
        }
    }
    if (m_getInfoTimer) {
        if (QThread::currentThread() == m_getInfoTimer->thread()) {
            m_getInfoTimer->stop();
        } else {
            QMetaObject::invokeMethod(m_getInfoTimer, "stop", Qt::QueuedConnection);
        }
    }
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
    sendSyncCommand(command, true);
    bool success = sendResetCommand();
    QThread::msleep(500);
    success = success && setBaudRate(baudRate);
    QThread::msleep(500);
    success = success && restartPort();
    if (success) {
        qCInfo(log_core_serial) << logPrefix << "applied successfully:" << baudRate;
    } else {
        qCWarning(log_core_serial) << logPrefix << "Failed to apply user selected baudrate:" << baudRate;
    }
}

// ========== IRecoveryHandler Interface Implementation (Phase 3) ==========

bool SerialPortManager::performRecovery(int attempt)
{
    qCInfo(log_core_serial) << "Performing recovery attempt" << attempt;
    
    // Record connection retry in statistics
    if (m_statistics) {
        m_statistics->recordConnectionRetry();
    }
    
    QString currentPortPath = m_currentSerialPortPath;
    QString currentPortChain = m_currentSerialPortChain;
    
    if (currentPortPath.isEmpty() || currentPortChain.isEmpty()) {
        qCWarning(log_core_serial) << "Cannot recover - no port chain information available";
        return false;
    }
    
    // Try to restart the current port
    bool recoverySuccess = switchSerialPortByPortChain(currentPortChain);
    
    if (recoverySuccess && ready) {
        qCInfo(log_core_serial) << "✓ Serial port recovery successful on attempt" << attempt;
        return true;
    }
    
    qCWarning(log_core_serial) << "Recovery attempt" << attempt << "failed";
    return false;
}

void SerialPortManager::onRecoveryFailed()
{
    qCCritical(log_core_serial) << "Recovery failed after all attempts";
    ready = false;
    emit statusUpdate("Serial port recovery failed - max retries exceeded");
}

void SerialPortManager::onRecoverySuccess()
{
    qCInfo(log_core_serial) << "Recovery completed successfully";
    resetErrorCounters();
    emit statusUpdate("Serial port recovered successfully");
}

// Helper function to poll for ready state after factory reset
// This handles the case where onSerialPortConnected triggers async retry logic
void SerialPortManager::startReadyStatePolling(const QString& portName)
{
    const int maxPollingAttempts = 10;  // Max 10 attempts
    const int pollingIntervalMs = 500;   // 500ms between attempts
    
    // Use a shared pointer to track polling state across async calls
    auto attemptCount = std::make_shared<int>(0);
    
    // Create a recursive polling function using QTimer::singleShot
    std::function<void()> pollReadyState = [this, portName, attemptCount, maxPollingAttempts, pollingIntervalMs, &pollReadyState]() {
        (*attemptCount)++;
        
        qCDebug(log_core_serial) << "Polling ready state, attempt" << *attemptCount << "of" << maxPollingAttempts << ", ready=" << ready;
        
        // Check if ready is true
        if (ready) {
            qCInfo(log_core_serial) << "Factory reset reconnection successful, ready=true after" << *attemptCount << "polling attempts";
            emit factoryResetCompleted(true);
            return;
        }
        
        // Check if we've exceeded max attempts
        if (*attemptCount >= maxPollingAttempts) {
            qCWarning(log_core_serial) << "Ready state polling timeout, attempting manual verification...";
            
            // Try one final manual verification
            if (serialPort && serialPort->isOpen()) {
                QByteArray verifyResponse = sendSyncCommand(CMD_GET_INFO, true);
                if (!verifyResponse.isEmpty()) {
                    ready = true;
                    resetErrorCounters();
                    m_lastSuccessfulCommand.restart();
                    
                    qCInfo(log_core_serial) << "Manual verification successful after factory reset";
                    emit serialPortConnectionSuccess(portName);
                    emit factoryResetCompleted(true);
                    return;
                }
            }
            
            qCWarning(log_core_serial) << "Factory reset reconnection failed after all polling attempts";
            emit factoryResetCompleted(false);
            return;
        }
        
        // Schedule next polling attempt
        QTimer::singleShot(pollingIntervalMs, this, pollReadyState);
    };
    
    // Start the first polling attempt after a short delay
    QTimer::singleShot(pollingIntervalMs, this, pollReadyState);
}

// Statistics tracking implementation
void SerialPortManager::startStats()
{
    if (m_statistics) {
        m_statistics->startTracking();
    }
    if (m_commandCoordinator) {
        m_commandCoordinator->startStats();
    }
    qCDebug(log_core_serial) << "Statistics tracking started";
}

void SerialPortManager::stopStats()
{
    if (m_statistics) {
        m_statistics->stopTracking();
    }
    if (m_commandCoordinator) {
        m_commandCoordinator->stopStats();
    }
    qCDebug(log_core_serial) << "Statistics tracking stopped";
}

void SerialPortManager::resetStats()
{
    if (m_statistics) {
        m_statistics->resetStatistics();
    }
    if (m_commandCoordinator) {
        m_commandCoordinator->resetStats();
    }
    qCDebug(log_core_serial) << "Statistics reset";
}

int SerialPortManager::getCommandsSent() const
{
    return m_statistics ? m_statistics->getCommandsSent() : 
           (m_commandCoordinator ? m_commandCoordinator->getStatsSent() : 0);
}

int SerialPortManager::getResponsesReceived() const
{
    return m_statistics ? m_statistics->getResponsesReceived() :
           (m_commandCoordinator ? m_commandCoordinator->getStatsReceived() : 0);
}

double SerialPortManager::getResponseRate() const
{
    return m_statistics ? m_statistics->getResponseRate() :
           (m_commandCoordinator ? m_commandCoordinator->getResponseRate() : 0.0);
}

qint64 SerialPortManager::getStatsElapsedMs() const
{
    return m_statistics ? m_statistics->getElapsedMs() :
           (m_commandCoordinator ? m_commandCoordinator->getStatsElapsedMs() : 0);
}

// Key state accessor methods (moved from header to avoid incomplete type issues)
bool SerialPortManager::getNumLockState() 
{
    return m_stateManager ? m_stateManager->getNumLockState() : false;
}

bool SerialPortManager::getCapsLockState() 
{
    return m_stateManager ? m_stateManager->getCapsLockState() : false;
}

bool SerialPortManager::getScrollLockState() 
{
    return m_stateManager ? m_stateManager->getScrollLockState() : false;
}

void SerialPortManager::log(const QString& message)
{
    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
    QString logEntry = QString("[%1] %2").arg(timestamp, message);
    emit logMessage(logEntry);
}

QString SerialPortManager::getSerialLogFilePath() const {
    return m_logFilePath;
}

void SerialPortManager::setSerialLogFilePath(const QString& path) {
    if (path.isEmpty()) return;
    m_logFilePath = path;
    
    // Ensure directory exists for the new log file
    QFileInfo fileInfo(path);
    QDir dir = fileInfo.absoluteDir();
    if (!dir.exists()) {
        dir.mkpath(".");
    }
    
    if (m_logWriter) {
        // Ask the writer (running in its thread) to change file path
        QMetaObject::invokeMethod(m_logWriter, "setFilePath", Qt::QueuedConnection, Q_ARG(QString, path));
    }
    
    // For diagnostics, we need to ensure the log file is created immediately
    // Write an initial log entry to create the file
    log("Serial logging started for diagnostics session");
}

void SerialPortManager::enableDebugLogging(bool enabled) {
    if (enabled) {
        // Enable debug logging for serial category
        QLoggingCategory::setFilterRules("opf.core.serial.debug=true");
        qCDebug(log_core_serial) << "Serial debug logging enabled for diagnostics";
    } else {
        // Disable debug logging for serial category
        QLoggingCategory::setFilterRules("opf.core.serial.debug=false");
    }
} 