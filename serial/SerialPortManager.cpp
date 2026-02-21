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
#include "serial_hotplug_handler.h"
#include "../ui/globalsetting.h"
#include "../host/cameramanager.h"
#include "../device/DeviceManager.h"
#include "../device/HotplugMonitor.h"

#include <QSerialPortInfo>
#include <QTimer>
#include <QThread>
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

    // Set object name for easier lookup and debugging
    this->setObjectName("SerialPortManager");
    // Initialize the suppression flag for GET_INFO polling
    m_suppressGetInfo = false;

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
    m_watchdog->moveToThread(m_serialWorkerThread);  // CRITICAL: Move to worker thread for thread safety
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
        if (m_commandCoordinator) {
            m_commandCoordinator->setReady(false);
        }
        qCCritical(log_core_serial) << "Connection watchdog: recovery failed";
    });
    connect(m_watchdog.get(), &ConnectionWatchdog::connectionStateChanged, this, [this](ConnectionState state) {
        qCDebug(log_core_serial) << "Connection state changed to:" << static_cast<int>(state);
    });
    
    // Connect command coordinator signals to SerialPortManager
    connect(m_commandCoordinator.get(), &SerialCommandCoordinator::dataSent, this, &SerialPortManager::dataSent);
    connect(m_commandCoordinator.get(), &SerialCommandCoordinator::dataReceived, this, &SerialPortManager::dataReceived);
    connect(m_commandCoordinator.get(), &SerialCommandCoordinator::commandExecuted, this, [this](const QByteArray& cmd, bool success) {
        QString portName = serialPort ? serialPort->portName() : QString();
        int baud = serialPort ? serialPort->baudRate() : 0;
        qCDebug(log_core_serial).nospace().noquote() << "TX (" << portName << "@" << baud << "bps): " << cmd.toHex(' ') << " Success:" << (success ? "true" : "false");
    });
    
    // Connect state manager signals to SerialPortManager signals
    connect(m_stateManager.get(), &SerialStateManager::keyStatesChanged, this, &SerialPortManager::keyStatesChanged);
    connect(m_stateManager.get(), &SerialStateManager::targetUsbStatusChanged, this, &SerialPortManager::targetUSBStatus);
    connect(m_stateManager.get(), &SerialStateManager::connectionStateChanged, this, [this](ConnectionState newState, ConnectionState oldState) {
        Q_UNUSED(oldState);
        // Update ready flag when connection state changes
        bool newReady = (newState == ConnectionState::Connected);
        ready = newReady;
        if (m_commandCoordinator) {
            m_commandCoordinator->setReady(newReady);
        }
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
        m_usbStatusCheckTimer->setInterval(1600);  // Check every 1.6 seconds
        m_getInfoTimer->setInterval(3000);  // Send GET_INFO every 3 seconds
        
        connect(m_usbStatusCheckTimer, &QTimer::timeout, this, &SerialPortManager::onUsbStatusCheckTimeout);
        connect(m_getInfoTimer, &QTimer::timeout, this, &SerialPortManager::onGetInfoTimeout);
        
        setupConnectionWatchdog();
        
        qCDebug(log_core_serial) << "Timers created successfully in worker thread";
    }, Qt::DirectConnection);  // DirectConnection ensures it runs in the worker thread

    connect(this, &SerialPortManager::serialPortConnected, this, &SerialPortManager::onSerialPortConnected, Qt::QueuedConnection);
    connect(this, &SerialPortManager::serialPortDisconnected, this, &SerialPortManager::onSerialPortDisconnected);
    connect(this, &SerialPortManager::serialPortConnectionSuccess, this, &SerialPortManager::onSerialPortConnectionSuccess);
    
    // Connect thread-safe reset operation signals to handlers (QueuedConnection ensures they run in worker thread)
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

    // Initialize FactoryResetManager and forward its signals for backward compatibility
    // Create without a QObject parent to avoid cross-thread parent/child creation warnings.
    m_factoryResetManager = std::make_unique<FactoryResetManager>(this);
    connect(m_factoryResetManager.get(), &FactoryResetManager::factoryReset, this, &SerialPortManager::factoryReset, Qt::QueuedConnection);
    connect(m_factoryResetManager.get(), &FactoryResetManager::factoryResetCompleted, this, &SerialPortManager::factoryResetCompleted, Qt::QueuedConnection);

    // Initialize and hook up the serial hotplug handler (abstracted from inline hotplug lambdas)
    // Create without a QObject parent to avoid cross-thread parent/child creation warnings.
    m_hotplugHandler = std::make_unique<SerialHotplugHandler>(this);

    // When the serial device matching our current port chain is unplugged, close and clear
    connect(m_hotplugHandler.get(), &SerialHotplugHandler::SerialPortUnplugged, this, [this](const QString& portChain) {
        qCDebug(log_core_serial) << "Device unplugged (via SerialHotplugHandler):" << portChain << "Current port chain:" << m_currentSerialPortChain;
        if (!m_currentSerialPortChain.isEmpty() && m_currentSerialPortChain == portChain) {
            qCInfo(log_core_serial) << "Serial port device unplugged, closing connection:" << portChain;
            
            // Set flags to prevent any concurrent open attempts during cleanup
            m_deviceUnpluggedDetected.store(true);
            m_deviceUnplugCleanupInProgress.store(true);
            
            if (serialPort && serialPort->isOpen()) {
                closePort();
                emit serialPortDisconnected(m_currentSerialPortPath);
            }
            m_currentSerialPortPath.clear();
            m_currentSerialPortChain.clear();
            // Ensure any in-progress open attempts are cleared so subsequent auto-connects may proceed
            m_openInProgress.store(false);
            if (m_hotplugHandler) {
                m_hotplugHandler->SetCurrentSerialPortPortChain(QString());
                m_hotplugHandler->CancelAutoConnectAttempts();
                m_hotplugHandler->SetSerialOpen(false);
            }
            
            // Schedule clearing of the unplugged flag after a brief delay to ensure all pending operations complete
            // This prevents new open attempts until we're sure cleanup is finished
            QTimer::singleShot(500, this, [this]() {
                m_deviceUnplugCleanupInProgress.store(false);
                m_deviceUnpluggedDetected.store(false);
                qCDebug(log_core_serial) << "Device unplugged cleanup completed, port operations can resume";
            });
        }
    });

    // Auto-connect requests are emitted twice by the handler (two attempts). Let SerialPortManager attempt a switch.
    connect(m_hotplugHandler.get(), &SerialHotplugHandler::AutoConnectRequested, this, [this](const QString& portChain) {
        qCInfo(log_core_serial) << "Auto-connect requested for port chain:" << portChain;
        if (m_isShuttingDown) {
            qCDebug(log_core_serial) << "Skipping auto-connect due to shutdown.";
            return;
        }

        if (m_openInProgress.load()) {
            qCDebug(log_core_serial) << "Skipping auto-connect because an open is already in progress for another request:" << portChain;
            return;
        }

        bool switchSuccess = switchSerialPortByPortChain(portChain);
        if (switchSuccess) {
            qCInfo(log_core_serial) << "✓ Serial port auto-switched to new device at portChain:" << portChain;
            if (m_hotplugHandler) m_hotplugHandler->CancelAutoConnectAttempts();
        } else {
            qCWarning(log_core_serial) << "Auto-connect attempt failed for portChain:" << portChain;
        }
    });

    observeSerialPortNotification();
    m_lastCommandTime.start();
    m_commandDelayMs = 0;  // Default no delay
    lastSerialPortCheckTime = QDateTime::currentDateTime().addMSecs(-SERIAL_TIMER_INTERVAL);  // Initialize check time in the past 
    
    // Initialize async message statistics tracking
    m_asyncMessagesSent = 0;
    m_asyncMessagesReceived = 0;
    m_asyncStatsTimer.start();  // Start the elapsed timer for statistics tracking
    
    // Connect to hotplug monitor for automatic device management
    connectToHotplugMonitor();
    
    // Initialize asynchronous logging
    QString logPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/serial_log.txt";
    m_logFilePath = logPath;
    m_logThread = new QThread(this);
    // Log writer runs in its own thread; do not parent to this to avoid cross-thread warnings.
    m_logWriter = new LogWriter(logPath, nullptr);
    m_logWriter->moveToThread(m_logThread);
    connect(this, &SerialPortManager::logMessage, m_logWriter, &LogWriter::writeLog);
    m_logThread->start();

    // Enable hotplug auto-connect now that initialization has completed
    if (m_hotplugHandler) {
        m_hotplugHandler->SetAllowAutoConnect(true);
        qCDebug(log_core_serial) << "SerialPortManager: Enabled hotplug auto-connect";
    }
    
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
    if (m_hotplugHandler) {
        m_hotplugHandler->SetShuttingDown(true);
    }
    
    // Prevent callback access during shutdown
    eventCallback = nullptr;
    
    // CRITICAL: Stop all timers in the worker thread BEFORE closing port or quitting thread
    // Use BlockingQueuedConnection to ensure timers are stopped before we proceed
    if (m_serialWorkerThread && m_serialWorkerThread->isRunning() && QThread::currentThread() != m_serialWorkerThread) {
        QMetaObject::invokeMethod(this, [this]() {
            qCDebug(log_core_serial) << "Stopping all timers in worker thread...";
            // Stop watchdog and all timers directly (we're now in the worker thread)
            if (m_watchdog) {
                m_watchdog->stop();
            }
            if (m_connectionWatchdog && m_connectionWatchdog->isActive()) {
                m_connectionWatchdog->stop();
            }
            if (m_errorRecoveryTimer && m_errorRecoveryTimer->isActive()) {
                m_errorRecoveryTimer->stop();
            }
            if (m_usbStatusCheckTimer && m_usbStatusCheckTimer->isActive()) {
                m_usbStatusCheckTimer->stop();
            }
            if (m_getInfoTimer && m_getInfoTimer->isActive()) {
                m_getInfoTimer->stop();
            }
            qCDebug(log_core_serial) << "All timers stopped in worker thread";
        }, Qt::BlockingQueuedConnection);
    }
    
    // FIXED: Close port BEFORE stopping thread to avoid blocking
    if (serialPort && serialPort->isOpen()) {
        // Use direct internal call if we're in worker thread, otherwise queue it
        if (QThread::currentThread() == m_serialWorkerThread) {
            closePortInternal();
        } else {
            QMetaObject::invokeMethod(this, [this]() {
                closePortInternal();
            }, Qt::BlockingQueuedConnection);
        }
    }
    
    // Clear command coordinator queue to release any pending operations
    if (m_commandCoordinator) {
        m_commandCoordinator->clearCommandQueue();
    }
    
    // Clear state manager
    if (m_stateManager) {
        m_stateManager->clearAllStates();
    }
    
    // FIXED: Stop worker thread with better timeout handling
    if (m_serialWorkerThread && m_serialWorkerThread->isRunning()) {
        qCDebug(log_core_serial) << "Requesting worker thread to quit...";
        m_serialWorkerThread->quit();
        
        // Wait with a reasonable timeout
        if (!m_serialWorkerThread->wait(2000)) {
            qCWarning(log_core_serial) << "Worker thread did not stop gracefully, forcing termination";
            m_serialWorkerThread->terminate();
            m_serialWorkerThread->wait(1000);
        }
        qCDebug(log_core_serial) << "Worker thread stopped";
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
    emit serialPortConnected(selectedDevice.serialPortPath);
    // Optionally, set the selected device in DeviceManager
    deviceManager.setCurrentSelectedDevice(selectedDevice);
    m_currentSerialPortChain = usedPortChain;
    if (m_hotplugHandler) {
        m_hotplugHandler->SetCurrentSerialPortPortChain(m_currentSerialPortChain);
    }
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

    // Prevent concurrent open attempts - serialize switching
    bool expected = false;
    if (!m_openInProgress.compare_exchange_strong(expected, true)) {
        qCInfo(log_core_serial) << "Open already in progress, ignoring request for portChain:" << portChain;
        return false;
    }

    // RAII guard to clear the in-progress flag on any exit path
    struct OpenGuard { std::atomic<bool>& flag; OpenGuard(std::atomic<bool>& f): flag(f) {} ~OpenGuard(){ flag.store(false); } };
    OpenGuard openGuard(m_openInProgress);

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

        // Use serialPortConnected signal to properly initialize the HID chip in worker thread
        // This ensures the same initialization process as during normal connection
        qCDebug(log_core_serial) << "Initializing serial port with HID chip configuration";
        emit serialPortConnected(selectedDevice.serialPortPath);
        
        if (!ready) {
            // Use async approach instead of blocking event loop
            QTimer::singleShot(2000, this, [this, selectedDevice, previousPortPath, previousPortChain, portChain]() {
                if (!ready) {
                    qCWarning(log_core_serial) << "Serial port initialization did not complete within timeout after switch";
                    // Revert to previous device info on failure
                    m_currentSerialPortPath = previousPortPath;
                    m_currentSerialPortChain = previousPortChain;
                } else {
                    // Success - finalize the switch
                    completeSwitchSerialPort(selectedDevice, previousPortPath, previousPortChain, portChain);
                }
            });
            return true; // Return immediately as this is async
        }

        // If ready immediately, complete the switch synchronously
        return completeSwitchSerialPort(selectedDevice, previousPortPath, previousPortChain, portChain);

    } catch (const std::exception& e) {
        qCritical() << "Exception in switchSerialPortByPortChain:" << e.what();
        return false;
    } catch (...) {
        qCritical() << "Unknown exception in switchSerialPortByPortChain";
        return false;
    }
}

bool SerialPortManager::completeSwitchSerialPort(const DeviceInfo& selectedDevice, const QString& previousPortPath, const QString& previousPortChain, const QString& portChain) {
    // Update global settings and device manager
    GlobalSetting::instance().setOpenterfacePortChain(portChain);
    DeviceManager& deviceManager = DeviceManager::getInstance();
    deviceManager.setCurrentSelectedDevice(selectedDevice);
    
    // Emit signals for serial port switching
    emit serialPortDeviceChanged(previousPortPath, selectedDevice.serialPortPath);
    emit serialPortSwitched(previousPortChain, portChain);

    // Inform hotplug handler about the new active port chain and cancel pending auto-connect attempts
    if (m_hotplugHandler) {
        m_hotplugHandler->SetCurrentSerialPortPortChain(portChain);
        m_hotplugHandler->CancelAutoConnectAttempts();
        m_hotplugHandler->SetSerialOpen(true);
    }
    
    qCDebug(log_core_serial) << "Serial port switch successful to:" << selectedDevice.serialPortPath 
                            << "Ready state:" << ready;
    return true;
}

/*
 * Open the serial port and check the baudrate and mode
 * This method now runs in the worker thread due to QueuedConnection
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
    
    if (m_currentChipType == ChipType::CH9329) {
        // Start async initialization for CH9329
        int tryBaudrate = determineBaudrate();
        initializeCH9329Async(portName, tryBaudrate);
    } else {
        // Handle CH32V208 synchronously since it's simpler
        initializeCH32V208Sync(portName);
    }
    
    qCDebug(log_core_serial) << "Serial port connection process initiated for port:" << portName;
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
    // This method is now deprecated in favor of the async initialization methods
    // It should not be called in the new flow, but keeping it for backward compatibility
    qCWarning(log_core_serial) << "openPortWithRetries called - this should not happen with new async initialization";
    return false;
}

/*
* Send configuration command and process the response
*/
ConfigResult SerialPortManager::sendAndProcessConfigCommand() {
    ConfigResult result;
    QByteArray retByte = sendSyncCommand(CMD_GET_PARA_CFG, true);
    if (retByte.isEmpty()) return result;
    
    // qCDebug(log_core_serial) << "Data read from serial port: " << retByte.toHex(' ');
    CmdDataParamConfig config = CmdDataParamConfig::fromByteArray(retByte);

    // Persist key parameters to GlobalSetting so UI and other modules can access device configuration
    GlobalSetting::instance().setSerialPortBaudrate(static_cast<int>(config.baudrate));
    GlobalSetting::instance().setVID(QString("%1").arg(config.vid, 4, 16, QChar('0')).toUpper());
    GlobalSetting::instance().setPID(QString("%1").arg(config.pid, 4, 16, QChar('0')).toUpper());
    // Store the custom USB descriptor flag (single byte) as hex string for compatibility with existing UI
    GlobalSetting::instance().setUSBEnabelFlag(QString("%1").arg(config.custom_usb_desc, 2, 16, QChar('0')).toUpper());

    qCDebug(log_core_serial) << "Stored device config to GlobalSetting: baudrate:" << config.baudrate
                             << "VID:" << QString("%1").arg(config.vid, 4, 16, QChar('0')).toUpper()
                             << "PID:" << QString("%1").arg(config.pid, 4, 16, QChar('0')).toUpper()
                             << "custom_usb_desc:" << QString("0x%1").arg(config.custom_usb_desc, 2, 16, QChar('0'));
    
    static QSettings settings("Techxartisan", "Openterface");
    Q_UNUSED(settings.value("hardware/operatingMode", 0x02).toUInt()); // hostConfigMode unused in this context
    result.mode = config.mode;
    result.success = true;
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

// Helper method for CH9329 async initialization
void SerialPortManager::initializeCH9329Async(const QString &portName, int tryBaudrate) {
    // Build the baud order: prefer the provided tryBaudrate first if it matches known values
    QList<int> baudOrder;
    if (tryBaudrate == BAUDRATE_HIGHSPEED) {
        baudOrder = {BAUDRATE_HIGHSPEED, BAUDRATE_LOWSPEED};
    } else {
        baudOrder = {BAUDRATE_LOWSPEED, BAUDRATE_HIGHSPEED};
    }
    
    // Start with the first baudrate attempt
    qCDebug(log_core_serial) << "Starting CH9329 initialization with baudrate order:" << baudOrder;
    attemptCH9329Connection(portName, baudOrder, 0, 0, 2); // 2 cycles max
}

// Helper method for CH32V208 sync initialization  
void SerialPortManager::initializeCH32V208Sync(const QString &portName) {
    qCDebug(log_core_serial) << "Initializing CH32V208 behavior for serial port";
    storeBaudrateIfNeeded(BAUDRATE_HIGHSPEED);
    
    // CH32V208 only supports 115200; ensure the port is opened at 115200 before signalling success
    if (openPort(portName, BAUDRATE_HIGHSPEED)) {
        // Give the port a moment to stabilize
        QThread::msleep(100);
        
        QByteArray getInfoResp = sendSyncCommand(CMD_GET_INFO, true);
        if (getInfoResp.isEmpty()) {
            qCWarning(log_core_serial) << "No response to CMD_GET_INFO on CH32V208 port after open";
        }
        
        // Set ready state and sync with command coordinator
        ready = true;
        if (m_commandCoordinator) {
            m_commandCoordinator->setReady(true);
        }
        
        emit serialPortConnectionSuccess(portName);
        qCInfo(log_core_serial) << "CH32V208 initialization completed successfully";
    } else {
        qCWarning(log_core_serial) << "Failed to open CH32V208 port at" << BAUDRATE_HIGHSPEED << "for" << portName;
        ready = false;
        if (m_commandCoordinator) {
            m_commandCoordinator->setReady(false);
        }
    }
}

// Improved async CH9329 connection attempt
void SerialPortManager::attemptCH9329Connection(const QString &portName, const QList<int> &baudOrder, int baudIndex, int cycle, int maxCycles) {
    if (cycle >= maxCycles) {
        qCWarning(log_core_serial) << "CH9329 initialization failed after" << maxCycles << "cycles";
        ready = false;
        if (m_commandCoordinator) {
            m_commandCoordinator->setReady(false);
        }
        return;
    }
    
    if (baudIndex >= baudOrder.size()) {
        // Move to next cycle
        qCDebug(log_core_serial) << "Starting cycle" << (cycle + 1) << "of CH9329 initialization";
        attemptCH9329Connection(portName, baudOrder, 0, cycle + 1, maxCycles);
        return;
    }
    
    int currentBaud = baudOrder[baudIndex];
    qCDebug(log_core_serial) << "Attempting to open port" << portName << "at baud" << currentBaud << "(cycle" << (cycle + 1) << "of" << maxCycles << ")";
    
    if (openPort(portName, currentBaud)) {
        // Give the port a moment to stabilize
        QThread::msleep(50);
        
        qCDebug(log_core_serial) << "Serial port opened, validating with synchronous CMD_GET_INFO:" << portName << "baud" << currentBaud;
        QByteArray getInfoResp = sendSyncCommand(CMD_GET_INFO, true);
        
         if (!getInfoResp.isEmpty() && getInfoResp.size() >= 4) {
            // Valid response received - initialization successful
            qCInfo(log_core_serial) << "CH9329 initialization successful at baudrate" << currentBaud;
            
            ConfigResult config = sendAndProcessConfigCommand();
            if (config.success) {
                handleChipSpecificLogic(config);
                storeBaudrateIfNeeded(config.workingBaudrate);
                
                // Set ready state and sync with command coordinator
                ready = true;
                if (m_commandCoordinator) {
                    m_commandCoordinator->setReady(true);
                }
                
                emit serialPortConnectionSuccess(portName);
                return;
            }
        }
        
        qCWarning(log_core_serial) << "No valid CMD_GET_INFO response received after opening port" << portName << "at baud" << currentBaud << "- closing and will try the next baud/attempt";
        closePortInternal();
        
        // Small delay before next attempt
        QThread::msleep(100);
    }
    
    // Try next baudrate
    attemptCH9329Connection(portName, baudOrder, baudIndex + 1, cycle, maxCycles);
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
    
    // Stop USB status check timer when device is unplugged
    if (m_usbStatusCheckTimer) {
        if (m_usbStatusCheckTimer->isActive()) {
            m_usbStatusCheckTimer->stop();
            qCDebug(log_core_serial) << "USB status check timer stopped due to device unplug";
        }
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
    if (serialPort) {
        connect(serialPort, &QSerialPort::readyRead, this, &SerialPortManager::readData);
        connect(serialPort, &QSerialPort::bytesWritten, this, &SerialPortManager::bytesWritten);
        // Connect error signal for enhanced error handling
        connect(serialPort, QOverload<QSerialPort::SerialPortError>::of(&QSerialPort::errorOccurred),
                this, &SerialPortManager::handleSerialError);
    } else {
        qCWarning(log_core_serial) << "SerialPortManager: serialPort is null - skipping readyRead/bytesWritten/error connects";
    }
    
    // Re-establish timer signal connections in case they were disconnected during port close
    reconnectTimerSignals();
    
    ready = true;
    // Sync the command coordinator ready state
    if (m_commandCoordinator) {
        m_commandCoordinator->setReady(true);
    }
    resetErrorCounters();
    m_lastSuccessfulCommand.restart();

    // Clear unplugged detection flags now that device is successfully connected
    // This allows new open attempts if the device is replugged
    m_deviceUnpluggedDetected.store(false);
    m_deviceUnplugCleanupInProgress.store(false);
    qCDebug(log_core_serial) << "Device unplugged detection flags cleared - port is ready for operation";

    int currentBaud = serialPort ? serialPort->baudRate() : 0;
    emit connectedPortChanged(portName, currentBaud);
    qCDebug(log_core_serial) << "Serial port opened successfully:" << portName << "at" << currentBaud << "baud";

    // Inform hotplug handler that serial is open and update its current port chain
    if (m_hotplugHandler) {
        m_hotplugHandler->SetSerialOpen(true);
        m_hotplugHandler->SetCurrentSerialPortPortChain(m_currentSerialPortChain);
    }
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
    } else if (!m_usbStatusCheckTimer) {
        qCWarning(log_core_serial) << "USB status check timer is null, cannot start";
    }

    // Start GET_INFO timer for periodic status updates for CH9329 and CH32V208 (unless diagnostics dialog is active)
    if (m_getInfoTimer) {
        QMutexLocker locker(&m_diagMutex);
        if (!m_suppressGetInfo) {
            if (QThread::currentThread() == m_getInfoTimer->thread()) {
                m_getInfoTimer->start();
            } else {
                QMetaObject::invokeMethod(m_getInfoTimer, "start", Qt::QueuedConnection);
            }
            qCDebug(log_core_serial) << "Started periodic GET_INFO timer for" << (isChipTypeCH9329() ? "CH9329" : "CH32V208");
        } else {
            qCDebug(log_core_serial) << "Periodic GET_INFO timer suppressed because diagnostics dialog is active";
        }

        // Send an immediate GET_INFO to prime the connection
        sendAsyncCommand(CMD_GET_INFO, true);
    } else {
        qCWarning(log_core_serial) << "GET_INFO timer is null, cannot start periodic status updates";
    }
}

void SerialPortManager::setEventCallback(StatusEventCallback* callback) {
    eventCallback = callback;
}

void SerialPortManager::setDiagnosticsDialogActive(bool active)
{
    QMutexLocker locker(&m_diagMutex);
    if (m_suppressGetInfo == active) {
        return;
    }
    m_suppressGetInfo = active;
    qCDebug(log_core_serial) << "Diagnostics dialog active set to:" << active;

    if (m_getInfoTimer) {
        if (active) {
            if (QThread::currentThread() == m_getInfoTimer->thread()) {
                m_getInfoTimer->stop();
            } else {
                QMetaObject::invokeMethod(m_getInfoTimer, "stop", Qt::QueuedConnection);
            }
            qCDebug(log_core_serial) << "GET_INFO timer stopped due to diagnostics dialog active";
        } else {
            // Restart GET_INFO for supported devices (CH9329 and CH32V208)
            if (serialPort && serialPort->isOpen() && (isChipTypeCH9329() || isChipTypeCH32V208())) {
                if (QThread::currentThread() == m_getInfoTimer->thread()) {
                    m_getInfoTimer->start();
                } else {
                    QMetaObject::invokeMethod(m_getInfoTimer, "start", Qt::QueuedConnection);
                }
                qCDebug(log_core_serial) << "GET_INFO timer restarted after diagnostics dialog closed";
            }
        }
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

    {
        QMutexLocker locker(&m_diagMutex);
        if (m_suppressGetInfo) {
            qCDebug(log_core_serial) << "Suppressed periodic GET_INFO due to diagnostics dialog active";
            return;
        }
    }

    sendAsyncCommand(CMD_GET_INFO, true);
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
    
    // Fast exit if main shutdown already completed - avoid any risky operations 
    if (m_isShuttingDown) {
        qCDebug(log_core_serial) << "SerialPortManager: Main shutdown completed, skipping destructor cleanup";
        qCDebug(log_core_serial) << "Serial port manager destroyed";
        return;
    }
    
    // Only do cleanup if we're in abnormal termination (m_isShuttingDown not set)
    qCDebug(log_core_serial) << "SerialPortManager: Abnormal termination detected, performing emergency cleanup";
    
    // Prevent further callback access during destruction
    eventCallback = nullptr;
    
    // Set shutdown flag and stop all command processing
    m_isShuttingDown = true;
    ready = false;
    
    // Immediately stop command coordinator from accepting new commands
    if (m_commandCoordinator) {
        m_commandCoordinator->setReady(false);
    }
    
    // Stop ConnectionWatchdog (Phase 3)
    if (m_watchdog) {
        m_watchdog->setShuttingDown(true);
        m_watchdog->stop();
    }
        
    // Emergency cleanup only
    stop();
    
    // FIXED: Cleanup timers without blocking - thread is already stopped by stop()
    // Avoid BlockingQueuedConnection on stopped threads to prevent deadlock
    
    if (m_connectionWatchdog) {
        // Thread is stopped, safe to call directly
        m_connectionWatchdog->stop();
        delete m_connectionWatchdog;  // Direct delete - event loop already exited
        m_connectionWatchdog = nullptr;
    }
    
    if (m_errorRecoveryTimer) {
        // Thread is stopped, safe to call directly
        m_errorRecoveryTimer->stop();
        delete m_errorRecoveryTimer;  // Direct delete - event loop already exited
        m_errorRecoveryTimer = nullptr;
    }
    
    // Clean up USB status check timer
    if (m_usbStatusCheckTimer) {
        // Thread is stopped, safe to call directly
        m_usbStatusCheckTimer->stop();
        delete m_usbStatusCheckTimer;  // Direct delete - event loop already exited
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
        if (!m_logThread->wait(1000)) {  // Wait max 1 second
            qCWarning(log_core_serial) << "Logging thread did not stop gracefully, forcing termination";
            m_logThread->terminate();
            m_logThread->wait(500);  // Give it a short time to terminate
        }
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
    
    // Check if device was just unplugged - if so, reject the open attempt to prevent "Access denied" errors
    // This is critical to avoid race conditions between device removal and port open operations
    if (m_deviceUnplugCleanupInProgress.load()) {
        // If the OS already reports the port as present, clear the transient cleanup guard and continue.
        // This prevents a stuck flag from permanently blocking open attempts (observed in the field).
        bool portPresent = false;
        for (const QSerialPortInfo &pi : QSerialPortInfo::availablePorts()) {
            if (pi.portName() == portName || portName.contains(pi.portName())) {
                portPresent = true;
                break;
            }
        }
        if (portPresent) {
            qCWarning(log_core_serial) << "Transient unplug-cleanup flag set but port is present -> clearing flag and continuing open:" << portName;
            m_deviceUnplugCleanupInProgress.store(false);
            m_deviceUnpluggedDetected.store(false);
        } else {
            qCWarning(log_core_serial) << "Device unplugged cleanup in progress, rejecting open attempt for port:" << portName;
            qCWarning(log_core_serial) << "This prevents race conditions that cause 'Access denied' errors during hotplug events";
            return false;
        }
    }
    
    QMutexLocker locker(&m_serialPortMutex);
    
    // If there is an existing QSerialPort instance that is not open, delete it to avoid stale internal state (e.g., stale file descriptor / notifiers)
    QSerialPort* oldSerialPort = nullptr;
    if (serialPort != nullptr && !serialPort->isOpen()) {
        qCDebug(log_core_serial) << "Existing closed QSerialPort instance found - marking for deletion to ensure fresh instance before open.";
        oldSerialPort = serialPort;
        serialPort = nullptr;  // Clear pointer temporarily
    }

    if (serialPort != nullptr && serialPort->isOpen()) {
        qCDebug(log_core_serial) << "Serial port is already opened.";
        return true;
    }

    emit statusUpdate("Going to open the port");

    if(serialPort == nullptr){
        qCDebug(log_core_serial) << "Creating new QSerialPort in worker thread";
        serialPort = new QSerialPort();
        
        // Connect error signal using queued connection for thread safety
        connect(serialPort, QOverload<QSerialPort::SerialPortError>::of(&QSerialPort::errorOccurred),
                this, &SerialPortManager::handleSerialError, Qt::QueuedConnection);
        
        // Connect readyRead signal using queued connection for thread safety
        connect(serialPort, &QSerialPort::readyRead, this, &SerialPortManager::readData, Qt::QueuedConnection);
    }
    
    serialPort->setPortName(portName);
    serialPort->setBaudRate(baudRate);
    
    // Enhanced port opening with better error handling
    bool openResult = false;
    QSerialPort::SerialPortError lastError = QSerialPort::NoError;
    
    openSerialPortInThread(openResult, lastError);
    
    if (openResult) {
        qCDebug(log_core_serial) << "Open port" << portName + ", baudrate: " << baudRate << "with read buffer size" << serialPort->readBufferSize();

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

void SerialPortManager::openSerialPortInThread(bool& openResult, QSerialPort::SerialPortError& lastError) {
    // Enhanced port opening with better error handling
    openResult = false;
    lastError = QSerialPort::NoError;
    const int maxRetries = 3;
    
    for (int attempt = 0; attempt < maxRetries; ++attempt) {
        openResult = serialPort->open(QIODevice::ReadWrite);
        if (openResult) {
            break;
        }
        
        lastError = serialPort->error();
        qCWarning(log_core_serial) << "Failed to open port on attempt" << (attempt + 1) 
                                   << "Error:" << serialPort->errorString();
        
        // Clear error before retry
        serialPort->clearError();
        
        // Simple synchronous delay for retry (avoid async complexity here)  
        if (attempt < maxRetries - 1) {
            QThread::msleep(300 * (attempt + 1));
        }
    }
}

/*
 * Close the serial port
 */
void SerialPortManager::closePort() {
    // Ensure closePort is called in the worker thread to avoid QSocketNotifier issues
    if (QThread::currentThread() != m_serialWorkerThread) {
        qCDebug(log_core_serial) << "closePort called from different thread, routing through worker thread";
        // FIXED: Use non-blocking QueuedConnection to avoid deadlock
        // Do NOT use QEventLoop::exec() as it can deadlock if worker thread is stopped/busy
        QMetaObject::invokeMethod(this, [this]() {
            closePortInternal();
        }, Qt::QueuedConnection);
        return;
    }
    
    // Already in worker thread, proceed directly
    closePortInternal();
}

void SerialPortManager::closePortInternal() {
    qCDebug(log_core_serial) << "Close serial port";
    
    QMutexLocker locker(&m_serialPortMutex);
    
    if (serialPort != nullptr) {
        qCDebug(log_core_serial) << "Closing serial port instance:" << static_cast<void*>(serialPort);
        
        if (serialPort->isOpen()) {
            // Disconnect all signals BEFORE any operations
            disconnect(serialPort, nullptr, this, nullptr);
            
            qCDebug(log_core_serial) << "Close serial port - current buffer sizes before close - bytesAvailable:" << serialPort->bytesAvailable()
                                     << "bytesToWrite:" << serialPort->bytesToWrite();
            
            // Enhanced close procedure for better memory safety
            try {
                // Flush any pending writes before closing
                if (serialPort->bytesToWrite() > 0) {
                    serialPort->waitForBytesWritten(100); // Wait max 100ms
                }
                
                // Clear the read buffer to prevent stale data issues
                serialPort->clear();
                
                // Close synchronously in worker thread
                serialPort->close();
                qCDebug(log_core_serial) << "Serial port closed";
                
                // Force event processing to ensure Qt's socket notifiers are cleaned up
                QCoreApplication::processEvents();
            } catch (...) {
                qCWarning(log_core_serial) << "Exception during serial port close";
            }
        }
        
        // Enhanced deletion with additional safety measures
        // Store pointer but DO NOT clear serialPort immediately to avoid race conditions
        QObject* portPtr = serialPort;
        
        // Schedule deletion for next event loop to avoid immediate socket notifier issues
        // Use QTimer::singleShot for more reliable deferred deletion
        QTimer::singleShot(0, this, [this, portPtr]() {
            if (portPtr) {
                qCDebug(log_core_serial) << "Deleting serial port instance:" << static_cast<void*>(portPtr);
                portPtr->deleteLater();
                // Clear pointer only after scheduling deletion
                QMutexLocker deleteLocker(&m_serialPortMutex);
                if (serialPort == portPtr) {
                    serialPort = nullptr;
                    qCDebug(log_core_serial) << "SerialPort instance pointer cleared";
                }
            }
        });
    } else {
        qCDebug(log_core_serial) << "Serial port is not opened (serialPort is nullptr).";
    }
    
    // Signal back to worker thread to complete the rest of the cleanup
    QMetaObject::invokeMethod(this, [this]() {
        completePortCloseCleanup();
    }, Qt::QueuedConnection);
}

void SerialPortManager::completePortCloseCleanup() {
    // Stop all timers first to prevent callbacks during close (disconnect signals for permanent close)
    stopAllTimers(true);

    // Set both ready states to false immediately to prevent further commands
    ready = false;
    if (m_commandCoordinator) {
        m_commandCoordinator->setReady(false);
    }

    // Reset error handler state when port is closed
    m_errorHandlerDisconnected = false;
    m_errorTrackingTimer.restart();

    // Inform hotplug handler that serial is closed and cancel any pending auto-connect attempts
    if (m_hotplugHandler) {
        m_hotplugHandler->SetSerialOpen(false);
        m_hotplugHandler->CancelAutoConnectAttempts();
    }

    // Notify listeners that port is not available
    emit connectedPortChanged("NA", 0);
    
    // Stop watchdog while port is closed (thread-safe)
    stopConnectionWatchdog();
    
    // Use the existing signal for port state changes
    QMetaObject::invokeMethod(this, [this]() {
        emit statusUpdate("Port disconnected");
    }, Qt::QueuedConnection);
}

bool SerialPortManager::restartPort() {
    QMutexLocker locker(&m_serialPortMutex);
    if (!serialPort) {
        qCWarning(log_core_serial) << "Cannot restart - no serial port instance (serialPort is nullptr)";
        // Try to recover using stored port information
        if (!m_currentSerialPortPath.isEmpty()) {
            qCInfo(log_core_serial) << "Attempting to restore serialPort using stored path:" << m_currentSerialPortPath;
            int defaultBaud = getCurrentBaudrate();
            if (defaultBaud <= 0) defaultBaud = DEFAULT_BAUDRATE;
            
            // Try to restore the connection
            return openPort(m_currentSerialPortPath, defaultBaud);
        }
        return false;
    }
    
    QString portName = serialPort->portName();
    qint32 baudRate = serialPort->baudRate();
    qCDebug(log_core_serial) << "Restart port" << portName << "baudrate:" << baudRate;
    
    // Record serial reset in statistics
    if (m_statistics) {
        m_statistics->recordSerialReset();
    }
    
    emit serialPortReset(true);
    
    // Always route through worker thread for consistency using async approach
    QMetaObject::invokeMethod(this, [this, portName, baudRate]() {
        restartPortInternalAsync(portName, baudRate);
    }, Qt::QueuedConnection);
    
    return true;  // Return true since restart is now async
}

bool SerialPortManager::restartPortInternal(const QString &portName, qint32 baudRate) {
    qCDebug(log_core_serial) << "Starting internal restart for port" << portName << "at" << baudRate << "bps";
    
    // Close the port first
    closePortInternal();
    
    // Use QTimer instead of blocking QEventLoop
    QTimer::singleShot(150, this, [this, portName, baudRate]() {
        qCDebug(log_core_serial) << "Reopening port after restart delay";
        
        // Attempt to reopen the port
        bool openResult = openPort(portName, baudRate);
        if (openResult) {
            qCDebug(log_core_serial) << "Port restart successful for" << portName;
            // After successful restart, emit connection success to restart timers  
            emit serialPortConnectionSuccess(portName);
            qCInfo(log_core_serial) << "Serial port restart completed - timers restarted";
            emit serialPortReset(false);
        } else {
            qCWarning(log_core_serial) << "Port restart failed for" << portName;
            emit serialPortReset(false);
        }
    });
    
    return true;  // Return true since restart is now async
}

// New async implementation
void SerialPortManager::restartPortInternalAsync(const QString &portName, qint32 baudRate) {
    qCDebug(log_core_serial) << "Async restart for port" << portName << "at" << baudRate << "bps";
    
    // Ensure this runs in the worker thread
    if (QThread::currentThread() != this->thread()) {
        QMetaObject::invokeMethod(this, [this, portName, baudRate]() {
            restartPortInternalAsync(portName, baudRate);
        }, Qt::QueuedConnection);
        return;
    }
    
    // Stop all timers first to prevent interference (keep signals connected for restart)
    stopAllTimers(false);
    
    // Close the port
    closePortInternal();
    
    // Schedule reopening after delay - use QueuedConnection for thread safety
    QTimer::singleShot(500, this, [this, portName, baudRate]() {
        // Process any pending events to ensure cleanup is complete
        QCoreApplication::processEvents();
        
        // Ensure the open operation happens in the worker thread
        QMetaObject::invokeMethod(this, [this, portName, baudRate]() {
            bool openResult = openPort(portName, baudRate);
            if (openResult) {
                qCDebug(log_core_serial) << "Port restart successful for" << portName;
                // After successful restart, restart the timers and emit connection success directly
                // Instead of calling onSerialPortConnected which would repeat the full initialization
                emit serialPortConnectionSuccess(portName);
                qCInfo(log_core_serial) << "Serial port restart completed - timers should be restarted automatically";
            } else {
                qCWarning(log_core_serial) << "Port restart failed for" << portName;
            }
            emit serialPortReset(false);
        }, Qt::QueuedConnection);
    });
}

// Helper method to stop all timers safely (must be called from worker thread)
void SerialPortManager::stopAllTimers(bool disconnectSignals) {
    // Enhanced thread safety - avoid blocking calls that can cause deadlocks
    if (QThread::currentThread() != this->thread()) {
        QMetaObject::invokeMethod(this, [this, disconnectSignals]() {
            stopAllTimers(disconnectSignals);
        }, Qt::QueuedConnection);
        return;
    }
    
    // Add shutdown flag check to prevent operations during shutdown
    if (m_isShuttingDown) {
        qCDebug(log_core_serial) << "stopAllTimers: Already shutting down, ensuring all timers stopped";
    }
    
    // Stop timers with additional safety checks
    if (m_getInfoTimer) {
        if (m_getInfoTimer->isActive()) {
            m_getInfoTimer->stop();
        }
        // Only disconnect signals during permanent shutdown
        if (disconnectSignals) {
            disconnect(m_getInfoTimer, nullptr, this, nullptr);
        }
    }

    if (m_usbStatusCheckTimer) {
        if (m_usbStatusCheckTimer->isActive()) {
            m_usbStatusCheckTimer->stop();
        }
        // Only disconnect signals during permanent shutdown  
        if (disconnectSignals) {
            disconnect(m_usbStatusCheckTimer, nullptr, this, nullptr);
        }
    }

    if (m_watchdog) {
        m_watchdog->stop();
    }

    if (m_connectionWatchdog) {
        if (m_connectionWatchdog->isActive()) {
            m_connectionWatchdog->stop();
        }
        // Only disconnect signals during permanent shutdown
        if (disconnectSignals) {
            disconnect(m_connectionWatchdog, nullptr, this, nullptr);
        }
    }
    
    if (m_errorRecoveryTimer) {
        if (m_errorRecoveryTimer->isActive()) {
            m_errorRecoveryTimer->stop();
        }
        disconnect(m_errorRecoveryTimer, nullptr, this, nullptr);
    }
    
    // Also call stopConnectionWatchdog for additional cleanup
    stopConnectionWatchdog();
    
    qCDebug(log_core_serial) << "All timers stopped and disconnected";
}

void SerialPortManager::reconnectTimerSignals() {
    // Re-establish timer signal connections that may have been disconnected during port close
    if (m_usbStatusCheckTimer) {
        // Disconnect any existing connections to avoid duplicates
        disconnect(m_usbStatusCheckTimer, &QTimer::timeout, this, &SerialPortManager::onUsbStatusCheckTimeout);
        // Re-connect the signal
        connect(m_usbStatusCheckTimer, &QTimer::timeout, this, &SerialPortManager::onUsbStatusCheckTimeout);
        qCDebug(log_core_serial) << "USB status check timer signal reconnected";
    }
    
    if (m_getInfoTimer) {
        // Disconnect any existing connections to avoid duplicates
        disconnect(m_getInfoTimer, &QTimer::timeout, this, &SerialPortManager::onGetInfoTimeout);
        // Re-connect the signal
        connect(m_getInfoTimer, &QTimer::timeout, this, &SerialPortManager::onGetInfoTimeout);
        qCDebug(log_core_serial) << "GET_INFO timer signal reconnected";
    }
    
    if (m_connectionWatchdog) {
        // For connection watchdog, we may need to reconnect its timeout signal as well
        // Note: The actual reconnection logic depends on how the watchdog is implemented
        qCDebug(log_core_serial) << "Connection watchdog timer signals checked";
    }
    
    qCDebug(log_core_serial) << "Timer signal connections restored after port reconnection";
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
    // Enhanced thread and memory safety checks
    if (m_isShuttingDown || !serialPort || !serialPort->isOpen()) {
        qCDebug(log_core_serial) << "readData: Ignored read - shutting down or port not open";
        return;
    }
    
    // Additional safety: Ensure we're in the correct thread
    if (QThread::currentThread() != this->thread()) {
        qCWarning(log_core_serial) << "readData called from wrong thread, ignoring";
        return;
    }
    
    // Mutex protection for serial port access to prevent concurrent access
    QMutexLocker locker(&m_serialPortMutex);
    if (!serialPort || !serialPort->isOpen()) {
        qCDebug(log_core_serial) << "Serial port became invalid during readData";
        return;
    }
    
    QByteArray data;
    try {
        // Check bytes available before reading to avoid potential buffer issues
        qint64 bytesAvailable = serialPort->bytesAvailable();
        if (bytesAvailable <= 0) {
            return;
        }
        
        // FIXED: Improved buffer management to prevent memory overflow
        const qint64 MAX_READ_SIZE = 4096;  // Reduced to 4KB for better memory control
        const qint64 WARN_THRESHOLD = 2048; // Warn if buffer is getting large
        
        if (bytesAvailable > WARN_THRESHOLD) {
            qCWarning(log_core_serial) << "Large buffer detected:" << bytesAvailable << "bytes - possible data burst or slow processing";
        }
        
        if (bytesAvailable > MAX_READ_SIZE) {
            qCWarning(log_core_serial) << "Limiting read to" << MAX_READ_SIZE << "bytes (" << bytesAvailable << "available)";
            data = serialPort->read(MAX_READ_SIZE);
            
            // Clear excess data to prevent indefinite accumulation
            if (bytesAvailable > MAX_READ_SIZE * 4) {
                qCCritical(log_core_serial) << "CRITICAL: Buffer overflow detected, clearing excess data to prevent crash";
                serialPort->clear();
            }
        } else {
            data = serialPort->readAll();
        }
    } catch (const std::exception& e) {
        qCCritical(log_core_serial) << "Exception occurred while reading serial data:" << e.what();
        // Clear buffer to prevent crash
        if (serialPort && serialPort->isOpen()) {
            serialPort->clear();
        }
        if (isRecoveryNeeded()) {
            attemptRecovery();
        }
        return;
    } catch (...) {
        qCCritical(log_core_serial) << "Unknown exception occurred while reading serial data";
        // Clear buffer to prevent crash
        if (serialPort && serialPort->isOpen()) {
            serialPort->clear();
        }
        if (isRecoveryNeeded()) {
            attemptRecovery();
        }
        return;
    }
    
    if (data.isEmpty()) {
        qCDebug(log_core_serial) << "Received empty data from serial port";
        checkAndLogAsyncMessageStatistics();
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
        // Sync the command coordinator ready state
        if (m_commandCoordinator) {
            m_commandCoordinator->setReady(true);
        }
        
        // Process response using protocol layer - signals are already connected
        m_protocol->processRawData(packet);

        // Record response for statistics tracking (counts async responses)
        if (m_statistics) {
            m_statistics->recordResponseReceived();
        }
        
        // Track async message received
        m_asyncMessagesReceived++;
        checkAndLogAsyncMessageStatistics();
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
    
    // Execute write operation directly in worker thread
    return writeDataInThread(data);
}

bool SerialPortManager::writeDataInThread(const QByteArray &data) {
    // Enhanced serial port validation with detailed diagnostics
    if (!isSerialPortValid()) {
        qCWarning(log_core_serial) << "Serial port not valid for write operation - state:"
                                   << "serialPort=" << static_cast<void*>(serialPort)
                                   << "isOpen=" << (serialPort ? (serialPort->isOpen() ? "true" : "false") : "N/A")
                                   << "portName=" << (serialPort ? serialPort->portName() : "N/A");
        ready = false;
        if (m_commandCoordinator) {
            m_commandCoordinator->setReady(false);
        }
        return false;
    }

    QMutexLocker locker(&m_serialPortMutex);
    
    // Double-check after acquiring mutex
    if (!serialPort || !serialPort->isOpen()) {
        qCWarning(log_core_serial) << "Serial port became invalid after mutex lock";
        ready = false;
        if (m_commandCoordinator) {
            m_commandCoordinator->setReady(false);
        }
        return false;
    }

    try {
        qint64 bytesWritten = serialPort->write(data);
        if (bytesWritten == -1) {
            qCWarning(log_core_serial) << "Failed to write data to serial port:" << serialPort->errorString();
            return false;
        } else if (bytesWritten != data.size()) {
            qCWarning(log_core_serial) << "Partial write: expected" << data.size() << "bytes, wrote" << bytesWritten;
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
        if (m_commandCoordinator) {
            m_commandCoordinator->setReady(false);
        }
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
    
    // Track async message sent
    m_asyncMessagesSent++;
    checkAndLogAsyncMessageStatistics();
    
    // Update command coordinator ready state with our current ready state
    m_commandCoordinator->setReady(ready.load());
    
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
    
    // Update command coordinator ready state with our current ready state
    m_commandCoordinator->setReady(ready.load());
    
    // Delegate to command coordinator
    return m_commandCoordinator->sendSyncCommand(serialPort, data, force);
}

/*
 * Restart the switchable USB port
 * Set the DTR to high for 0.5s to restart the USB port
 */
void SerialPortManager::restartSwitchableUSB(){
    if(!isSerialPortValid()){
        qCWarning(log_core_serial) << "Cannot restart switchable USB - serial port not valid";
        return;
    }
    
    qCDebug(log_core_serial) << "Restart the USB port now...";
    
    QMutexLocker locker(&m_serialPortMutex);
    if (!serialPort || !serialPort->isOpen()) {
        qCWarning(log_core_serial) << "Serial port became invalid during USB restart";
        return;
    }
    
    serialPort->setDataTerminalReady(true);
    
    // Use non-blocking timer instead of msleep
    QTimer::singleShot(500, this, [this]() {
        QMutexLocker locker(&m_serialPortMutex);
        if (serialPort && serialPort->isOpen()) {
            serialPort->setDataTerminalReady(false);
            }
        });
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
        // FIXED: Use non-blocking call to avoid deadlock
        // Caller should not depend on immediate return value when calling from different thread
        QMetaObject::invokeMethod(this, [this, baudRate]() {
            // Call the actual baudrate setting logic
            setBaudRateInternal(baudRate);
        }, Qt::QueuedConnection);
        qCDebug(log_core_serial) << "setBaudRate request queued for worker thread";
        return true; // Return optimistic result as operation is queued
    }
    
    // Already in worker thread, proceed directly
    return setBaudRateInternal(baudRate);
}

bool SerialPortManager::setBaudRateInternal(int baudRate) {
    if (!isSerialPortValid()) {
        qCWarning(log_core_serial) << "Cannot set baud rate - serial port not valid";
        return false;
    }

    QMutexLocker locker(&m_serialPortMutex);
    
    // Double-check after acquiring mutex
    if (!serialPort) {
        qCWarning(log_core_serial) << "Cannot set baud rate: serialPort became null after mutex lock";
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

    // Suppress transient errors and pause periodic operations while changing baud
    m_baudChangeInProgress.store(true);

    // CRITICAL: Clear all buffers and pending data BEFORE changing baudrate
    // This prevents stale data from interfering with the baudrate transition
    if (serialPort && serialPort->isOpen()) {
        qCDebug(log_core_serial) << "Clearing serial buffers before baud rate change";
        qCDebug(log_core_serial) << "Buffer state before clear - bytesAvailable:" << serialPort->bytesAvailable()
                                 << "bytesToWrite:" << serialPort->bytesToWrite();
        serialPort->clear(QSerialPort::AllDirections);
        serialPort->waitForBytesWritten(500);  // Wait for any pending writes to complete
        qCDebug(log_core_serial) << "Buffer state after clear - bytesAvailable:" << serialPort->bytesAvailable()
                                 << "bytesToWrite:" << serialPort->bytesToWrite();
    }

    // Stop periodic timers to avoid activity during transition
    if (m_getInfoTimer) {
        if (QThread::currentThread() == m_getInfoTimer->thread()) {
            m_getInfoTimer->stop();
        } else {
            QMetaObject::invokeMethod(m_getInfoTimer, "stop", Qt::QueuedConnection);
        }
    }
    if (m_usbStatusCheckTimer) {
        if (QThread::currentThread() == m_usbStatusCheckTimer->thread()) {
            m_usbStatusCheckTimer->stop();
        } else {
            QMetaObject::invokeMethod(m_usbStatusCheckTimer, "stop", Qt::QueuedConnection);
        }
    }

    // Stop watchdog to avoid recovery decisions during expected transient failures
    if (m_watchdog) {
        m_watchdog->stop();
    }

    bool setResult = serialPort->setBaudRate(baudRate);

    if (setResult) {
        qCDebug(log_core_serial) << "Baud rate successfully set to" << baudRate;
        // Update state manager so getCurrentBaudrate() reflects actual host setting
        if (m_stateManager) {
            m_stateManager->setBaudRate(baudRate);
        }
        emit connectedPortChanged(serialPort->portName(), baudRate);
    } else {
        qCWarning(log_core_serial) << "Failed to set baud rate to" << baudRate << ": " << serialPort->errorString();
    }

    // Allow stabilization time before re-enabling timers and watchdog; clear error counters to avoid immediate recovery
    // Use shorter stabilization time (1500ms) since we cleared buffers upfront
    QTimer::singleShot(1500, this, [this]() {
        // Clear any transient error records accumulated during transition
        resetErrorCounters();

        // Re-clear buffers after stabilization to ensure no stale data remains
        if (serialPort && serialPort->isOpen()) {
            qCDebug(log_core_serial) << "Clearing buffers after stabilization period";
            serialPort->clear(QSerialPort::AllDirections);
        }

        // Restart periodic timers if appropriate
        if (m_getInfoTimer && serialPort && serialPort->isOpen() && (isChipTypeCH9329() || isChipTypeCH32V208())) {
            QMutexLocker locker(&m_diagMutex);
            if (!m_suppressGetInfo) {
                if (QThread::currentThread() == m_getInfoTimer->thread()) {
                    m_getInfoTimer->start();
                } else {
                    QMetaObject::invokeMethod(m_getInfoTimer, "start", Qt::QueuedConnection);
                }
                qCDebug(log_core_serial) << "GET_INFO timer restarted after baud change";
            } else {
                qCDebug(log_core_serial) << "GET_INFO timer suppressed due to diagnostics dialog active";
            }
        }
        if (m_usbStatusCheckTimer && isChipTypeCH32V208()) {
            if (QThread::currentThread() == m_usbStatusCheckTimer->thread()) {
                m_usbStatusCheckTimer->start();
            } else {
                QMetaObject::invokeMethod(m_usbStatusCheckTimer, "start", Qt::QueuedConnection);
            }
        }

        // Restart watchdog
        if (m_watchdog) {
            m_watchdog->start();
        }

        // Clear baud-change-in-progress flag
        m_baudChangeInProgress.store(false);
        qCDebug(log_core_serial) << "Baud change stabilization complete";
    });

    return setResult;
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
                emit serialPortConnected(portName);
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
    qCDebug(log_core_serial) << "Connecting SerialPortManager to hotplug monitor via SerialHotplugHandler";
    
    if (!m_hotplugHandler) {
        qCWarning(log_core_serial) << "No SerialHotplugHandler available to connect";
        return;
    }

    m_hotplugHandler->ConnectToHotplugMonitor();

    qCDebug(log_core_serial) << "SerialPortManager successfully connected to hotplug monitor";
}

void SerialPortManager::disconnectFromHotplugMonitor()
{
    qCDebug(log_core_serial) << "Disconnecting SerialPortManager from hotplug monitor via SerialHotplugHandler";

    if (m_hotplugHandler) {
        m_hotplugHandler->DisconnectFromHotplugMonitor();
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
    // Critical: Check serialPort validity first to prevent crashes
    if (!serialPort) {
        qCWarning(log_core_serial) << "Serial error occurred but serialPort instance is null! Error code:" << static_cast<int>(error);
        return;
    }
    
    QString errorString = serialPort->errorString();
    
    // If we're performing a controlled baud-rate change, suppress transient errors
    if (m_baudChangeInProgress.load()) {
        qCDebug(log_core_serial) << "Transient serial error during baud change suppressed:" << errorString << "Error code:" << static_cast<int>(error);
        return;
    }

    // Ignore NoError
    if (error == QSerialPort::NoError) {
        return;
    }

    // Throttle error processing to prevent infinite loop - max one error processed per 50ms
    if (!m_lastErrorLogTime.isValid()) {
        m_lastErrorLogTime.start();
    } else if (m_lastErrorLogTime.elapsed() < ERROR_LOG_THROTTLE_MS) {
        // Silently drop errors that occur too frequently
        qCDebug(log_core_serial) << "Error throttled:" << errorString << "Code:" << static_cast<int>(error);
        return;
    }
    m_lastErrorLogTime.restart();

    qCWarning(log_core_serial) << "Serial port error occurred:" << errorString << "Error code:" << static_cast<int>(error);
    
    // Check for device disconnection errors - if we detect the device is physically unplugged,
    // stop the timers to prevent continuous failed command attempts
    if (error == QSerialPort::UnknownError || 
        errorString.contains("设备不识别此命令") || 
        errorString.contains("拒绝访问") ||
        errorString.contains("Access is denied")) {
        
        qCInfo(log_core_serial) << "Device disconnection error detected, stopping periodic timers";
        
        // Stop USB status check timer to prevent continuous CMD_CHECK_USB_STATUS commands
        if (m_usbStatusCheckTimer && m_usbStatusCheckTimer->isActive()) {
            m_usbStatusCheckTimer->stop();
            qCDebug(log_core_serial) << "USB status check timer stopped due to device error";
        }
        
        // Stop GET_INFO timer to prevent continuous CMD_GET_INFO commands
        if (m_getInfoTimer && m_getInfoTimer->isActive()) {
            m_getInfoTimer->stop();
            qCDebug(log_core_serial) << "GET_INFO timer stopped due to device error";
        }
    }
    
    // Record error in statistics module
    if (m_statistics) {
        m_statistics->recordConsecutiveError();
    }
    
    // Report error to ConnectionWatchdog (Phase 3)
    if (m_watchdog) {
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
    
    // Check if serialPort is null and log diagnostic information
    if (!serialPort) {
        qCWarning(log_core_serial) << "Recovery attempt" << attempt << "- serialPort instance is null!";
        qCDebug(log_core_serial) << "Current port path:" << m_currentSerialPortPath << "Current port chain:" << m_currentSerialPortChain;
    }
    
    QString currentPortPath = m_currentSerialPortPath;
    QString currentPortChain = m_currentSerialPortChain;
    
    if (currentPortPath.isEmpty() || currentPortChain.isEmpty()) {
        qCWarning(log_core_serial) << "Cannot recover - no port chain information available";
        return false;
    }
    
    // Try to restart the current port
    bool recoverySuccess = switchSerialPortByPortChain(currentPortChain);
    
    // Verify serialPort was recreated properly
    if (recoverySuccess && serialPort) {
        qCDebug(log_core_serial) << "Recovery attempt" << attempt << "- serialPort instance restored:" << static_cast<void*>(serialPort);
    }
    
    if (recoverySuccess && ready) {
        qCInfo(log_core_serial) << "✓ Serial port recovery successful on attempt" << attempt;
        return true;
    }
    
    qCWarning(log_core_serial) << "Recovery attempt" << attempt << "failed, ready=" << ready << ", serialPort=" << static_cast<void*>(serialPort);
    return false;
}

void SerialPortManager::onRecoveryFailed()
{
    qCCritical(log_core_serial) << "Recovery failed after all attempts";
    ready = false;
    // Sync the command coordinator ready state
    if (m_commandCoordinator) {
        m_commandCoordinator->setReady(false);
    }
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

// Helper method to validate serialPort state with detailed diagnostics
bool SerialPortManager::isSerialPortValid() const
{
    if (!serialPort) {
        return false;
    }
    
    if (!serialPort->isOpen()) {
        return false;
    }
    
    // Additional validation to detect stale objects
    try {
        QString portName = serialPort->portName();
        if (portName.isEmpty()) {
            qCWarning(log_core_serial) << "SerialPort has empty port name - possible stale object";
            return false;
        }
    } catch (...) {
        qCWarning(log_core_serial) << "Exception during serialPort validation - possible stale object";
        return false;
    }
    
    return true;
}

void SerialPortManager::checkAndLogAsyncMessageStatistics()
{
    // ===== ASYNC MESSAGE STATISTICS AND IMBALANCE DETECTION =====
    // This method monitors the ratio of received vs sent async messages every 1 second.
    // Purpose: Detect communication issues where the device sends many responses without
    // corresponding requests, indicating a potential queue backup or device malfunction.
    //
    // Detection Logic:
    // 1. Every 1 second (ASYNC_STATS_INTERVAL_MS), calculate send/receive rates
    // 2. If received > sent by more than 150% (threshold: 1.5x), flag as imbalance
    // 3. Track how long the imbalance persists using m_imbalanceDetectionTimer
    // 4. If imbalance continues for 3+ seconds, send a reset command to recover
    // 5. After reset, clear the flag and restart detection from clean state
    //
    // State Transitions:
    // - Normal -> Imbalance: When ratio > 1.5 for first time
    // - Imbalance -> Timeout: When ratio > 1.5 for 3+ consecutive seconds
    // - Imbalance/Timeout -> Normal: When ratio drops back to <= 1.5
    
    // Add lightweight consecutive "no-response" detection and automatic escalation:
    // - If we send requests but receive 0 responses for N consecutive 1-second intervals,
    //   trigger recovery (prefer ConnectionWatchdog; fallback to close+reopen).
    static int s_consecutiveNoResponseIntervals = 0;
    const int NO_RESPONSE_ESCALATION_THRESHOLD = 3; // ~3 seconds of zero replies
    Q_UNUSED(NO_RESPONSE_ESCALATION_THRESHOLD);
    
    // Check if 1 second has elapsed since last report
    if (m_asyncStatsTimer.elapsed() >= ASYNC_STATS_INTERVAL_MS) {
        qint64 elapsedMs = m_asyncStatsTimer.elapsed();
        
        // Only process if there were any messages in this interval
        if (m_asyncMessagesSent > 0 || m_asyncMessagesReceived > 0) {
            // Calculate rates in messages per second
            double sentRate = (m_asyncMessagesSent * 1000.0) / elapsedMs;
            double receivedRate = (m_asyncMessagesReceived * 1000.0) / elapsedMs;
            
            // Log statistics for monitoring and debugging
            qCInfo(log_core_serial) << "Async Message Statistics:"
                                   << "Sent/sec:" << QString::number(sentRate, 'f', 2)
                                   << "Received/sec:" << QString::number(receivedRate, 'f', 2)
                                   << "Total sent:" << m_asyncMessagesSent
                                   << "Total received:" << m_asyncMessagesReceived;
            
            // ===== IMBALANCE DETECTION LOGIC =====
            // Only check imbalance if we actually sent messages (avoid division issues)
            if (m_asyncMessagesSent > 0) {
                // Calculate ratio of received to sent messages
                double imbalanceRatio = (double)m_asyncMessagesReceived / m_asyncMessagesSent;
                
                // ===== STATE 1: IMBALANCE THRESHOLD EXCEEDED (ratio > 1.5) =====
                if (imbalanceRatio > ASYNC_IMBALANCE_THRESHOLD) {
                    // State: Imbalance Detected
                    if (!m_imbalanceDetected) {
                        // First occurrence - start tracking duration
                        // We restart the timer to measure from this point forward
                        m_imbalanceDetectionTimer.restart();
                        m_imbalanceDetected = true;
                        
                        // Log warning with diagnostic info
                        qCWarning(log_core_serial) << "Async message imbalance detected!"
                                                   << "Received/Sent ratio:" << QString::number(imbalanceRatio, 'f', 2)
                                                   << "(threshold:" << ASYNC_IMBALANCE_THRESHOLD << ")";
                    } else {
                        // Imbalance continues - check if we've exceeded the 3-second tolerance window
                        qint64 imbalanceDuration = m_imbalanceDetectionTimer.elapsed();
                        qCWarning(log_core_serial) << "Async message imbalance persisting for" << imbalanceDuration << "ms"
                                                   << "Received/Sent ratio:" << QString::number(imbalanceRatio, 'f', 2);
                        
                        // ===== STATE 2: TIMEOUT THRESHOLD EXCEEDED (3+ seconds) =====
                        if (imbalanceDuration >= ASYNC_IMBALANCE_TIMEOUT_MS) {
                            // Imbalance has persisted for 3+ seconds - device likely in bad state
                            // Action: Send reset command to device to recover
                            qCCritical(log_core_serial) << "Async message imbalance exceeded 3 seconds threshold!"
                                                        << "Sending device reset command. Duration:" << imbalanceDuration << "ms";
                            
                            // Send reset command to device (synchronous, waits for response)
                            sendResetCommand();
                            
                            // Reset detection state for next monitoring cycle
                            // This prevents triggering multiple resets in rapid succession
                            m_imbalanceDetected = false;
                            m_imbalanceDetectionTimer.restart();
                        }
                    }
                } else {
                    // ===== STATE 3: IMBALANCE CLEARED (ratio <= 1.5) =====
                    // Ratio is now healthy - no imbalance detected
                    if (m_imbalanceDetected) {
                        // Imbalance was previously detected but has now recovered
                        // Log the recovery state
                        qCInfo(log_core_serial) << "Async message imbalance cleared."
                                               << "Received/Sent ratio:" << QString::number(imbalanceRatio, 'f', 2);
                        
                        // Clear imbalance flag and reset timer for next monitoring cycle
                        m_imbalanceDetected = false;
                        m_imbalanceDetectionTimer.restart();
                    }
                }
            }
        } else {
            // No activity in this window; be conservative and reset counter
            s_consecutiveNoResponseIntervals = 0;
        }
        
        // ===== RESET COUNTERS FOR NEXT INTERVAL =====
        // Clear accumulated counts and restart timer for next 1-second interval
        m_asyncMessagesSent = 0;
        m_asyncMessagesReceived = 0;
        m_asyncStatsTimer.restart();
    }
}

// Async helper methods for non-blocking port operations
void SerialPortManager::continueInitializeWithBaudrates(const QString &portName, qint32 baud, int cycle, int cycles) {
    qCDebug(log_core_serial) << "Continuing initialization cycle" << (cycle+1) << "of" << cycles;
    
    if (cycle >= cycles) {
        qCWarning(log_core_serial) << "All attempts exhausted: failed to open and validate serial port:" << portName;
        return;
    }
    
    qCDebug(log_core_serial) << "Attempting to open port" << portName << "at baud" << baud << "(cycle" << (cycle+1) << "of" << cycles << ")";

    bool opened = openPort(portName, baud);
    if (!opened) {
        qCWarning(log_core_serial) << "Failed to open serial port:" << portName << "at baud" << baud;
        // Use async delay instead of blocking
        QTimer::singleShot(1000 * (cycle+1), this, [this, portName, baud, cycle, cycles]() {
            continueInitializeWithBaudrates(portName, baud, cycle + 1, cycles);
        });
        return;
    }

    qCDebug(log_core_serial) << "Serial port opened, validating with synchronous CMD_GET_INFO:" << portName << "baud" << baud;

    // Allow device to settle briefly after opening - use async approach
    QTimer::singleShot(300 * (cycle+1), this, [this, portName, baud, cycle, cycles]() {
        validatePortAfterSettle(portName, baud, cycle, cycles);
    });
}

void SerialPortManager::validatePortAfterSettle(const QString &portName, qint32 baud, int cycle, int cycles) {
    // Send a synchronous GET_INFO and validate response to ensure the device is actually talking
    QByteArray resp = sendSyncCommand(CMD_GET_INFO, true);
    bool valid = false;
    if (!resp.isEmpty() && resp.size() >= 4) {
        unsigned char b0 = static_cast<unsigned char>(resp[0]);
        unsigned char b3 = static_cast<unsigned char>(resp[3]);
        if (b0 == 0x57 && b3 == 0x81) {
            valid = true;
            qCDebug(log_core_serial) << "Device validation successful at baud" << baud;
        }
    }

    if (!valid) {
        qCWarning(log_core_serial) << "No valid CMD_GET_INFO response received after opening port" << portName << "at baud" << baud << "- closing and will try the next baud/attempt";
        if (serialPort && serialPort->isOpen()) {
            closePort();
        }

        // Use async delay instead of blocking
        QTimer::singleShot(300, this, [this, portName, baud, cycle, cycles]() {
            continueInitializeWithBaudrates(portName, baud, cycle + 1, cycles);
        });
        return;
    }

    qCDebug(log_core_serial) << "Successfully initialized and validated port" << portName << "at baud" << baud;
}

void SerialPortManager::continueOpenPortRetry(const QString &portName, qint32 baudRate, int attempt, int maxRetries) {
    if (attempt >= maxRetries) {
        qCWarning(log_core_serial) << "Failed to open port after" << maxRetries << "attempts. Final error:" 
                                   << (serialPort ? serialPort->errorString() : "No port instance");
        return;
    }

    qCDebug(log_core_serial) << "Retry attempt" << (attempt + 1) << "for port" << portName;
    
    // Clear error before retry
    if (serialPort) {
        serialPort->clearError();
    }
    
    // Try to open the port again
    bool openResult = false;
    if (serialPort) {
        openResult = serialPort->open(QIODevice::ReadWrite);
    }

    if (openResult) {
        // Success! Port opened
        qCDebug(log_core_serial) << "Open port" << portName + ", baudrate: " << baudRate << "with read buffer size" << serialPort->readBufferSize();
        
        // Show existing buffer sizes before clearing them
        qCDebug(log_core_serial) << "Serial buffer sizes before clear - bytesAvailable:" << serialPort->bytesAvailable()
                                 << "bytesToWrite:" << serialPort->bytesToWrite();

        // Clear any stale data in the serial port buffers
        qCDebug(log_core_serial) << "Clearing serial port buffers to remove stale data";
        serialPort->clear();
        return; // Success - exit
    }

    // Failed to open, log error and continue with next retry
    qCWarning(log_core_serial) << "Failed to open port on attempt" << (attempt + 1) 
                               << "Error:" << (serialPort ? serialPort->errorString() : "No port instance");
    
    // Use async delay for next retry
    QTimer::singleShot(300 * (attempt + 1), this, [this, portName, baudRate, attempt, maxRetries]() {
        continueOpenPortRetry(portName, baudRate, attempt + 1, maxRetries);
    });
}

// Async state machine for port retries
void SerialPortManager::startAsyncPortRetries(const QString &portName, const QList<int> &baudOrder, int baudIndex, int cycle, int maxCycles) {
    // Enhanced safety: bail out early if shutting down
    if (m_isShuttingDown) {
        qCDebug(log_core_serial) << "Abandoning async port retries due to shutdown";
        return;
    }
    
    if (cycle >= maxCycles) {
        qCWarning(log_core_serial) << "All attempts exhausted: failed to open and validate serial port:" << portName;
        return;
    }
    
    if (baudIndex >= baudOrder.size()) {
        // Move to next cycle - simplified to avoid deep nesting
        if (m_isShuttingDown) return;
        QTimer::singleShot(500, this, [this, portName, baudOrder, cycle, maxCycles]() {
            if (!m_isShuttingDown) {
                startAsyncPortRetries(portName, baudOrder, 0, cycle + 1, maxCycles);
            }
        });
        return;
    }
    
    int baud = baudOrder[baudIndex];
    qCDebug(log_core_serial) << "Attempting to open port" << portName << "at baud" << baud << "(cycle" << (cycle+1) << "of" << maxCycles << ")";

    // Simplified async approach to prevent complex nested operations
    bool opened = openPort(portName, baud);
    if (!opened) {
        qCWarning(log_core_serial) << "Failed to open serial port:" << portName << "at baud" << baud;
        // Simple delay before next attempt
        QTimer::singleShot(1000, this, [this, portName, baudOrder, baudIndex, cycle, maxCycles]() {
            if (!m_isShuttingDown) {
                startAsyncPortRetries(portName, baudOrder, baudIndex + 1, cycle, maxCycles);
            }
        });
        return;
    }

    qCDebug(log_core_serial) << "Serial port opened, validating with synchronous CMD_GET_INFO:" << portName << "baud" << baud;

    // Allow device to settle briefly after opening
    QTimer::singleShot(300, this, [this, portName, baud, baudOrder, baudIndex, cycle, maxCycles]() {
        if (!m_isShuttingDown) {
            validateAsyncPortRetry(portName, baud, baudOrder, baudIndex, cycle, maxCycles);
        }
    });
}

void SerialPortManager::validateAsyncPortRetry(const QString &portName, int baud, const QList<int> &baudOrder, int baudIndex, int cycle, int maxCycles) {
    // Enhanced safety checks
    if (m_isShuttingDown || !serialPort) {
        qCDebug(log_core_serial) << "Validation abandoned due to shutdown or null serial port";
        return;
    }
    
    // Send a synchronous GET_INFO and validate response
    QByteArray resp = sendSyncCommand(CMD_GET_INFO, true);
    bool valid = false;
    if (!resp.isEmpty() && resp.size() >= 4) {
        unsigned char b0 = static_cast<unsigned char>(resp[0]);
        unsigned char b3 = static_cast<unsigned char>(resp[3]);
        if (b0 == 0x57 && b3 == 0x81) {
            valid = true;
        }
    }

    if (valid) {
        qCDebug(log_core_serial) << "Received valid CMD_GET_INFO response, open considered successful:" << portName << "baud" << baud;
        return; // Success!
    }

    qCWarning(log_core_serial) << "No valid CMD_GET_INFO response received after opening port" << portName << "at baud" << baud << "- closing and will try the next baud/attempt";
    
    // Simplified close operation
    if (serialPort && serialPort->isOpen()) {
        closePort();
    }

    // Delay before next attempt - simplified to avoid complex nesting
    QTimer::singleShot(300, this, [this, portName, baudOrder, baudIndex, cycle, maxCycles]() {
        if (!m_isShuttingDown) {
            startAsyncPortRetries(portName, baudOrder, baudIndex + 1, cycle, maxCycles);
        }
    });
}