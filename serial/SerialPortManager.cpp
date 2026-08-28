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
#include "../device/DeviceLifecycleManager.h"
// Protocol constants
#include "ch9329.h"

// Local scancode constants for lock keys (used to build HID report)
static constexpr uint8_t SCANCODE_NUMLOCK = 0x53;
static constexpr uint8_t SCANCODE_CAPSLOCK = 0x39;
static constexpr uint8_t SCANCODE_SCROLLLOCK = 0x47;

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

#ifdef _WIN32
#include <windows.h>
#include <setupapi.h>
#include <devguid.h>
#endif


#include "log/opflogging.h"

// Legacy category kept for other translation units that Q_DECLARE_LOGGING_CATEGORY(log_core_serial).
// SerialPortManager.cpp itself uses the fine-grained sub-categories below.
OPF_LOGGING_CATEGORY(log_core_serial, "opf.core.serial")
OPF_LOGGING_CATEGORY(log_core_serial_tx, "opf.core.serial.tx")
OPF_LOGGING_CATEGORY(log_core_serial_rx, "opf.core.serial.rx")
OPF_LOGGING_CATEGORY(log_core_serial_cmd, "opf.core.serial.cmd")
OPF_LOGGING_CATEGORY(log_core_serial_conn, "opf.core.serial.conn")
OPF_LOGGING_CATEGORY(log_core_serial_watchdog, "opf.core.serial.watchdog")
OPF_LOGGING_CATEGORY(log_core_serial_hotplug, "opf.core.serial.hotplug")
OPF_LOGGING_CATEGORY(log_core_serial_config, "opf.core.serial.config")
OPF_LOGGING_CATEGORY(log_core_serial_lockkeys, "opf.core.serial.lockkeys")
OPF_LOGGING_CATEGORY(log_core_serial_usbswitch, "opf.core.serial.usbswitch")

// Define static constants
const int SerialPortManager::BAUDRATE_HIGHSPEED;
const int SerialPortManager::BAUDRATE_LOWSPEED;
const int SerialPortManager::DEFAULT_BAUDRATE;
const int SerialPortManager::SERIAL_TIMER_INTERVAL;

SerialPortManager::SerialPortManager(QObject *parent) : QObject(parent), serialPort(nullptr), m_serialWorkerThread(new QThread(nullptr)), serialTimer(new QTimer(nullptr)),
    m_connectionWatchdog(nullptr), m_errorRecoveryTimer(nullptr), m_usbStatusCheckTimer(nullptr), m_getInfoTimer(nullptr){
    qCDebug(log_core_serial_conn) << "Initialize serial port.";

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
        qCCritical(log_core_serial_watchdog) << "Connection watchdog: recovery failed";
    });
    connect(m_watchdog.get(), &ConnectionWatchdog::connectionStateChanged, this, [this](ConnectionState state) {
        qCDebug(log_core_serial_conn) << "Connection state changed to:" << static_cast<int>(state);
    });
    
    // Connect command coordinator signals to SerialPortManager
    connect(m_commandCoordinator.get(), &SerialCommandCoordinator::dataSent, this, &SerialPortManager::dataSent);
    connect(m_commandCoordinator.get(), &SerialCommandCoordinator::dataReceived, this, &SerialPortManager::dataReceived);
    connect(m_commandCoordinator.get(), &SerialCommandCoordinator::commandExecuted, this, [this](const QByteArray& cmd, bool success) {
        QString portName = serialPort ? serialPort->portName() : QString();
        int baud = serialPort ? serialPort->baudRate() : 0;
        qCDebug(log_core_serial_tx).nospace().noquote() << "TX (" << portName << "@" << baud << "bps): " << cmd.toHex(' ') << " Success:" << (success ? "true" : "false");
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
        qCDebug(log_core_serial_conn) << "Connection state changed, ready=" << ready;
    });
    connect(m_stateManager.get(), &SerialStateManager::serialPortInfoChanged, this, [this](const SerialPortInfo& newInfo, const SerialPortInfo& oldInfo) {
        Q_UNUSED(oldInfo);
        emit serialPortDeviceChanged(oldInfo.portPath, newInfo.portPath);
        emit connectedPortChanged(newInfo.portPath, newInfo.baudRate);
    });
    
    // Connect statistics module signals to SerialPortManager signals
    connect(m_statistics.get(), &SerialStatistics::statisticsUpdated, this, [this](const StatisticsData& data) {
        qCDebug(log_core_serial_cmd) << "Statistics updated - Commands sent:" << data.commandsSent
                                << "Responses received:" << data.responsesReceived
                                << "Error rate:" << QString::number(data.errorRate(), 'f', 2) << "%";
    });
    connect(m_statistics.get(), &SerialStatistics::performanceThresholdExceeded, this, [this](const QString& metric, int currentValue, int threshold) {
        qCWarning(log_core_serial_watchdog) << "Performance threshold exceeded for" << metric 
                                   << "- Current:" << currentValue << "Threshold:" << threshold;
    });
    connect(m_statistics.get(), &SerialStatistics::recoveryRecommended, this, [this](const QString& reason) {
        qCWarning(log_core_serial_watchdog) << "Recovery recommended:" << reason;
        emit statusUpdate(QString("Recovery recommended: %1").arg(reason));
    });
    connect(m_statistics.get(), &SerialStatistics::armBaudrateRecommendation, this, [this](int current, int recommended) {
        emit armBaudratePerformanceRecommendation(current);
        qCDebug(log_core_serial_config) << "ARM baudrate recommendation: Current=" << current << "Recommended=" << recommended;
    });
    
    // Connect protocol layer signals to SerialPortManager
    connect(m_protocol.get(), &SerialProtocol::getInfoReceived, this, [this](bool targetConnected, uint8_t indicators) {
            // Fallback - direct emission
            emit targetUSBStatus(targetConnected);
            updateSpecialKeyState(indicators);
    });

    // TARGET RESTART FIX: Connect targetUSBStatus to our recovery handler.
    // When the target computer restarts, the CH32V208's target-side USB HID interface
    // becomes stale while the host-side serial stays connected. The periodic GET_INFO
    // response still comes back (CH32V208 is alive), so the watchdog never fires.
    // This handler detects targetUSBStatus(false) and triggers RTS hardware reset
    // to reinitialize the entire CH32V208 (both target-side USB HID and video).
    connect(this, &SerialPortManager::targetUSBStatus, this, &SerialPortManager::handleTargetUsbStatusChanged);
    connect(m_protocol.get(), &SerialProtocol::usbSwitchStatusReceived, this, &SerialPortManager::usbStatusChanged);
    connect(m_protocol.get(), &SerialProtocol::paramConfigReceived, this, [this](int baudrate, uint8_t mode) {
        qCDebug(log_core_serial_config) << "Current serial port baudrate:" << baudrate << ", Mode: 0x" << QString::number(mode, 16);
    });
    connect(m_protocol.get(), &SerialProtocol::setParamConfigReceived, this, [this](uint8_t status) {
        qCDebug(log_core_serial_config) << "Set parameter configuration, status:" << m_protocol->statusToString(status);
        if (status == SerialProtocolConstants::STATUS_SUCCESS) {
            qCDebug(log_core_serial_config) << "Parameter configuration successful, emitting signal for reset command";
            emit parameterConfigurationSuccess();
        }
    });
    connect(m_protocol.get(), &SerialProtocol::resetResponseReceived, this, [this](uint8_t status) {
        qCDebug(log_core_serial_config) << "Reset command, status:" << m_protocol->statusToString(status);
    });
    
    // IMPORTANT: Timers must be created in the worker thread to avoid cross-thread issues
    // Use QThread::started signal to create timers after moveToThread takes effect
    connect(m_serialWorkerThread, &QThread::started, this, [this]() {
        qCDebug(log_core_serial_conn) << "Worker thread started, creating timers in worker thread context";
        
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

        // Create port chain delayed clear timer for hotplug protection
        m_portChainClearTimer = new QTimer(this);
        m_portChainClearTimer->setSingleShot(true);
        connect(m_portChainClearTimer, &QTimer::timeout, this, [this]() {
            if (!m_pendingPortChainClear.isEmpty()) {
                // Only clear if the chain hasn't been reused since the clear was scheduled.
                // During a close/reopen cycle (e.g. recovery or target reboot), the port chain
                // is set again before this timer fires. Without this guard, recovery would fail
                // because m_currentSerialPortChain would be empty when the watchdog tries to
                // call performRecovery() → switchSerialPortByPortChain().
                if (m_currentSerialPortChain == m_pendingPortChainClear) {
                    qCDebug(log_core_serial_hotplug) << "Port chain delayed clear timeout: clearing" << m_pendingPortChainClear;
                    m_currentSerialPortChain.clear();
                    m_pendingPortChainClear.clear();
                } else {
                    qCInfo(log_core_serial_hotplug) << "Port chain delayed clear skipped — chain has been reused:"
                                                    << "pending=" << m_pendingPortChainClear
                                                    << "current=" << m_currentSerialPortChain;
                    m_pendingPortChainClear.clear();
                }
            }
        });

        setupConnectionWatchdog();
        
        qCDebug(log_core_serial_conn) << "Timers created successfully in worker thread";
    }, Qt::DirectConnection);  // DirectConnection ensures it runs in the worker thread

    connect(this, &SerialPortManager::serialPortConnected, this, &SerialPortManager::onSerialPortConnected, Qt::QueuedConnection);
    connect(this, &SerialPortManager::serialPortDisconnected, this, &SerialPortManager::onSerialPortDisconnected);
    connect(this, &SerialPortManager::serialPortConnectionSuccess, this, &SerialPortManager::onSerialPortConnectionSuccess);
    
    // Connect thread-safe reset operation signals to handlers (QueuedConnection ensures they run in worker thread)
    connect(this, &SerialPortManager::requestFactoryReset, this, &SerialPortManager::handleFactoryReset, Qt::QueuedConnection);
    connect(this, &SerialPortManager::requestFactoryResetV191, this, &SerialPortManager::handleFactoryResetV191, Qt::QueuedConnection);
    
    // Connect parameter configuration success signal to automatically send reset command
    connect(this, &SerialPortManager::parameterConfigurationSuccess, this, [this]() {
        qCDebug(log_core_serial_config) << "Parameter configuration successful, sending reset command automatically";
        sendResetCommand();
        int storedBaudrate = GlobalSetting::instance().getSerialPortBaudrate();
        qCDebug(log_core_serial_conn) << "Reopen the serial port with baudrate: " << storedBaudrate;
        setBaudRate(storedBaudrate);
        restartPort();
    });

    // Connect hardware setting application signal to worker thread slot
    connect(this, &SerialPortManager::requestApplyHardwareSetting, this, &SerialPortManager::applyHardwareSettingInternal, Qt::QueuedConnection);

    // Initialize FactoryResetManager and forward its signals for backward compatibility.
    // Create without a QObject parent to avoid cross-thread parent/child creation warnings
    // (SerialPortManager is in worker thread, but constructor runs on main thread).
    m_factoryResetManager = std::make_unique<FactoryResetManager>(this, nullptr);
    // CRITICAL: Move FactoryResetManager to the worker thread so that all its
    // QTimer::singleShot callbacks fire in SerialWorkerThread.  Without this,
    // the timers run on the MainThread and access serialPort (owned by the
    // worker thread) without synchronisation, causing data races / segfaults.
    m_factoryResetManager->moveToThread(m_serialWorkerThread);
    connect(m_factoryResetManager.get(), &FactoryResetManager::factoryReset, this, &SerialPortManager::factoryReset, Qt::QueuedConnection);
    connect(m_factoryResetManager.get(), &FactoryResetManager::factoryResetCompleted, this, &SerialPortManager::factoryResetCompleted, Qt::QueuedConnection);

    // Initialize and hook up the serial hotplug handler (abstracted from inline hotplug lambdas).
    // Create without a QObject parent to avoid cross-thread parent/child creation warnings.
    m_hotplugHandler = std::make_unique<SerialHotplugHandler>(nullptr);

    // When the serial device matching our current port chain is unplugged, close and clear
    connect(m_hotplugHandler.get(), &SerialHotplugHandler::SerialPortUnplugged, this, [this](const QString& portChain) {
        qCDebug(log_core_serial_hotplug) << "Device unplugged (via SerialHotplugHandler):" << portChain << "Current port chain:" << m_currentSerialPortChain;
        if (!m_currentSerialPortChain.isEmpty() && m_currentSerialPortChain == portChain) {
            qCInfo(log_core_serial_hotplug) << "Serial port device unplugged, closing connection:" << portChain;
            
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
                qCDebug(log_core_serial_hotplug) << "Device unplugged cleanup completed, port operations can resume";
            });
        }
    });

    // Auto-connect requests are emitted twice by the handler (two attempts). Let SerialPortManager attempt a switch.
    connect(m_hotplugHandler.get(), &SerialHotplugHandler::AutoConnectRequested, this, [this](const QString& portChain) {
        qCInfo(log_core_serial_hotplug) << "Auto-connect requested for port chain:" << portChain;
        if (m_isShuttingDown) {
            qCDebug(log_core_serial_hotplug) << "Skipping auto-connect due to shutdown.";
            return;
        }

        // HOTPLUG FIX: If previous initialization state is stale (ready=false but port was opened),
        // force-clean the state before attempting a new connection. This prevents "not ready"
        // errors from blocking subsequent auto-connect attempts after rapid hotplug.
        if (m_openInProgress.load()) {
            // Check if we're stuck in a stale open state (ready=false and previous init failed)
            // This can happen when device was unplugged during CH9329 initialization
            if (!ready && m_currentSerialPortChain == portChain) {
                qCWarning(log_core_serial_conn) << "Stale open state detected (ready=false but open in progress)"
                                           << "- forcing state reset for port chain:" << portChain;

                // Reset the open-in-progress flag to allow a fresh attempt
                m_openInProgress.store(false);

                // Force-close any stale serial port that may still be open
                if (serialPort && serialPort->isOpen()) {
                    qCWarning(log_core_serial_conn) << "Force-closing stale serial port connection";
                    closePort();
                }

                // Clear pending retries since we're starting fresh
                m_initRetryCount = 0;
                m_pendingInitPortName.clear();
                m_pendingInitBaudrate = 0;

                qCInfo(log_core_serial_hotplug) << "State reset complete, proceeding with fresh auto-connect";
            } else {
                qCDebug(log_core_serial_hotplug) << "Skipping auto-connect because an open is already in progress for another request:" << portChain;
                return;
            }
        }

        // HOTPLUG FIX: If the serial port is not open and we're below max retries,
        // always attempt a fresh switch even if previous ones failed
        bool switchSuccess = switchSerialPortByPortChain(portChain);
        if (switchSuccess) {
            qCInfo(log_core_serial_hotplug) << "✓ Serial port auto-switched to new device at portChain:" << portChain;
            if (m_hotplugHandler) m_hotplugHandler->CancelAutoConnectAttempts();
        } else {
            qCWarning(log_core_serial_hotplug) << "Auto-connect attempt failed for portChain:" << portChain;
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
    
    // Connect to DeviceLifecycleManager for centralized hotplug management (Phase 2 migration)
    // DeviceLifecycleManager replaces the old SerialHotplugHandler + HotplugMonitor direct connection.
    {
        auto& lifecycle = DeviceLifecycleManager::getInstance();

        // shouldConnectSerial → switchSerialPortByPortChain, then report result
        connect(&lifecycle, &DeviceLifecycleManager::shouldConnectSerial,
            this, [this](const QString& sessionKey, const QString& portPath) {
                qCInfo(log_core_serial_hotplug) << "[Lifecycle] shouldConnectSerial:"
                                                << "session=" << sessionKey << "path=" << portPath;
                bool started = switchSerialPortByPortChain(portPath);
                if (!started) {
                    qCWarning(log_core_serial_hotplug) << "[Lifecycle] switchSerialPortByPortChain failed immediately for" << portPath;
                    DeviceLifecycleManager::getInstance().notifyInterfaceFailed(
                        sessionKey, InterfaceType::Serial, "switchSerialPortByPortChain returned false");
                    return;
                }
                // switchSerialPortByPortChain is async — wait for serialPortConnectionSuccess or timeout
                QTimer* connectTimer = new QTimer(this);
                connectTimer->setSingleShot(true);
                QMetaObject::Connection successConn;
                successConn = connect(this, &SerialPortManager::serialPortConnectionSuccess, this,
                    [this, sessionKey, connectTimer, successConn](const QString&) mutable {
                        QObject::disconnect(successConn);
                        connectTimer->stop();
                        connectTimer->deleteLater();
                        qCInfo(log_core_serial_hotplug) << "[Lifecycle] Serial connected for session" << sessionKey;
                        DeviceLifecycleManager::getInstance().notifyInterfaceConnected(
                            sessionKey, InterfaceType::Serial);
                    });
                connect(connectTimer, &QTimer::timeout, this,
                    [this, sessionKey, successConn, connectTimer]() mutable {
                        QObject::disconnect(successConn);
                        connectTimer->deleteLater();
                        qCWarning(log_core_serial_hotplug) << "[Lifecycle] Serial connect timed out for session" << sessionKey;
                        DeviceLifecycleManager::getInstance().notifyInterfaceFailed(
                            sessionKey, InterfaceType::Serial, "serialPortConnectionSuccess timeout");
                    });
                connectTimer->start(10000);  // 10s timeout for serial connection
            });

        // shouldDisconnectSerial → closePort, then report
        connect(&lifecycle, &DeviceLifecycleManager::shouldDisconnectSerial,
            this, [this](const QString& sessionKey) {
                qCInfo(log_core_serial_hotplug) << "[Lifecycle] shouldDisconnectSerial for session" << sessionKey;
                closePort();
                m_currentSerialPortPath.clear();
                m_currentSerialPortChain.clear();
                DeviceLifecycleManager::getInstance().notifyInterfaceDisconnected(
                    sessionKey, InterfaceType::Serial);
            });

        qCInfo(log_core_serial_hotplug) << "SerialPortManager connected to DeviceLifecycleManager";
    }

    // NOTE: Old hotplug handler kept as fallback — ConnectToHotplugMonitor is disabled
    // to prevent double-triggering with DeviceLifecycleManager.
    // connectToHotplugMonitor();
    
    // Initialize asynchronous logging
    QString logPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/serial_log.txt";
    m_logFilePath = logPath;
    m_logThread = new QThread(nullptr);
    // Log writer runs in its own thread; do not parent to this to avoid cross-thread warnings.
    m_logWriter = new LogWriter(logPath, nullptr);
    m_logWriter->moveToThread(m_logThread);
    connect(m_logThread, &QThread::finished, m_logWriter, &QObject::deleteLater);
    connect(this, &SerialPortManager::logMessage, m_logWriter, &LogWriter::writeLog);
    m_logThread->start();

    // NOTE: Old hotplug auto-connect disabled — DeviceLifecycleManager handles retries now.
    // if (m_hotplugHandler) {
    //     m_hotplugHandler->SetAllowAutoConnect(true);
    // }
    
    qCDebug(log_core_serial_conn) << "SerialPortManager initialized with DeviceManager integration and enhanced stability features";
}

void SerialPortManager::observeSerialPortNotification(){
    qCDebug(log_core_serial_conn) << "Created a timer to observer SerialPort...";

    serialTimer->moveToThread(m_serialWorkerThread);

    connect(m_serialWorkerThread, &QThread::finished, serialTimer, &QObject::deleteLater);
    connect(m_serialWorkerThread, &QThread::finished, m_serialWorkerThread, &QObject::deleteLater);
    connect(this, &SerialPortManager::sendCommandAsync, this, &SerialPortManager::sendCommand, Qt::QueuedConnection);

    m_serialWorkerThread->start();
}

void SerialPortManager::stop() {
    qCDebug(log_core_serial_conn) << "Stopping serial port manager...";
    
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
            qCDebug(log_core_serial_conn) << "Stopping all timers in worker thread...";
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
            qCDebug(log_core_serial_conn) << "All timers stopped in worker thread";
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
        qCDebug(log_core_serial_conn) << "Requesting worker thread to quit...";
        m_serialWorkerThread->quit();
        
        // Wait with a reasonable timeout
        if (!m_serialWorkerThread->wait(2000)) {
            qCWarning(log_core_serial_conn) << "Worker thread did not stop gracefully, forcing termination";
            m_serialWorkerThread->terminate();
            m_serialWorkerThread->wait(1000);
        }
        qCDebug(log_core_serial_conn) << "Worker thread stopped";
    }
    
    qCDebug(log_core_serial_conn) << "Serial port manager stopped";
}

// New serial port initialization logic using port chain and DeviceInfo
void SerialPortManager::initializeSerialPortFromPortChain() {
    // Get port chain from global settings
    QString portChain = GlobalSetting::instance().getOpenterfacePortChain();
    qCDebug(log_core_serial_config) << "Initializing serial port using port chain:" << portChain;
    if (portChain.isEmpty()) {
        qCWarning(log_core_serial_conn) << "No port chain found in global settings.";
        return;
    }

    // Use DeviceManager to look up device information by port chain
    DeviceManager& deviceManager = DeviceManager::getInstance();
    QList<DeviceInfo> devices = deviceManager.getDevicesByPortChain(portChain);
    if (devices.isEmpty()) {
        qCWarning(log_core_serial_conn) << "No device found for port chain:" << portChain;
        return;
    }

    // Find a device with a valid serial port path
    DeviceInfo selectedDevice;
    QString usedPortChain = portChain;
    for (const DeviceInfo& device : devices) {
        if (!device.serialPortPath.isEmpty()) {
            selectedDevice = device;
            qCDebug(log_core_serial_conn) << "Found device with serial port:" << device.serialPortPath;
            break;
        }
    }

    // If no device with serial found on main port chain, try companion port chain
    if (!selectedDevice.isValid() || selectedDevice.serialPortPath.isEmpty()) {
        QString companionPortChain = deviceManager.getCompanionPortChain(portChain);
        if (!companionPortChain.isEmpty()) {
            QList<DeviceInfo> companionDevices = deviceManager.getDevicesByPortChain(companionPortChain);
            qCDebug(log_core_serial_conn) << "Checking companion port chain:" << companionPortChain << "with" << companionDevices.size() << "devices";
            for (const DeviceInfo& companionDevice : companionDevices) {
                if (!companionDevice.serialPortPath.isEmpty()) {
                    selectedDevice = companionDevice;
                    usedPortChain = companionPortChain;
                    qCDebug(log_core_serial_conn) << "Found device with serial port on companion chain:" << companionDevice.serialPortPath;
                    break;
                }
            }
        }
    }



    if (!selectedDevice.isValid() || selectedDevice.serialPortPath.isEmpty()) {
        qCWarning(log_core_serial_conn) << "No valid device with serial port found for port chain:" << portChain;
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

bool SerialPortManager::isPortOpen() const
{
    return serialPort && serialPort->isOpen();
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
        qCWarning(log_core_serial_conn) << "Cannot switch to serial port with empty port chain";
        return false;
    }

    qCDebug(log_core_serial_conn) << "Attempting to switch to serial port by port chain:" << portChain;

    // Prevent concurrent open attempts - serialize switching
    bool expected = false;
    if (!m_openInProgress.compare_exchange_strong(expected, true)) {
        qCInfo(log_core_serial_conn) << "Open already in progress, ignoring request for portChain:" << portChain;
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
            qCWarning(log_core_serial_conn) << "No devices found for port chain:" << portChain;
            return false;
        }

        qCDebug(log_core_serial_conn) << "Found" << devices.size() << "device(s) for port chain:" << portChain;

        // Find a device with a valid serial port path
        DeviceInfo selectedDevice;
        for (const DeviceInfo& device : devices) {
            if (!device.serialPortPath.isEmpty()) {
                selectedDevice = device;
                qCDebug(log_core_serial_conn) << "Found device with serial port:" << device.serialPortPath;
                break;
            }
        }

        if (!selectedDevice.isValid() || selectedDevice.serialPortPath.isEmpty()) {
            qCWarning(log_core_serial_conn) << "No valid device with serial port found for port chain:" << portChain;
            return false;
        }

        // TARGET RESTART FIX: Even if the port path matches and appears open, force a
        // close-and-reopen cycle. When the target computer restarts, the CH32V208 chip
        // stays powered by the host USB but its internal state (especially the target-side
        // USB interface) becomes stale. Simply returning true here leaves the port in a
        // broken state where commands are sent but never forwarded to the target.
        // The close-and-reopen flushes stale buffers and triggers full HID re-initialization.
        if (!m_currentSerialPortPath.isEmpty()
            && m_currentSerialPortPath == selectedDevice.serialPortPath
            && isPortOpen()) {
            qCInfo(log_core_serial_conn) << "Port path matches and port is open — forcing close/reopen"
                                         << "for fresh initialization:" << selectedDevice.serialPortPath;
        }

        QString previousPortPath = m_currentSerialPortPath;
        QString previousPortChain = m_currentSerialPortChain;
        
        qCDebug(log_core_serial_conn) << "Switching serial port from" << previousPortPath 
                                << "to" << selectedDevice.serialPortPath;

        // Close current serial port if open
        if (serialPort && serialPort->isOpen()) {
            qCDebug(log_core_serial_conn) << "Closing current serial port before switch";
            closePort();

            // TARGET RESTART FIX: closePortInternal() sets m_portState = CLOSING synchronously.
            // The deferred deletion (QTimer::singleShot(0, ...)) inside closePortInternal will
            // set it to CLOSED, but that hasn't run yet. Without this explicit state fix,
            // openPort() in the queued onSerialPortConnected handler would reject the open with
            // "Rejecting openPort - port is in CLOSING state (deleteLater pending)".
            // This is the root cause of "VideoPane can't send data to target after target restart" —
            // the serial port was never actually reopened during recovery.
            m_portState.store(SerialPortState::CLOSED);
        }

        // HOTPLUG FIX: Cancel any pending port chain clear - we're about to use this chain
        cancelPendingPortChainClear();

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
        qCDebug(log_core_serial_config) << "Initializing serial port with HID chip configuration";
        emit serialPortConnected(selectedDevice.serialPortPath);

        if (!ready) {
            // Use async approach instead of blocking event loop
            QTimer::singleShot(2000, this, [this, selectedDevice, previousPortPath, previousPortChain, portChain]() {
                if (!ready) {
                    qCWarning(log_core_serial_config) << "Serial port initialization did not complete within timeout after switch";

                    // HOTPLUG FIX: Don't blindly revert to previous chain - check if a new device has connected
                    // If m_currentSerialPortChain has changed (e.g., by a new hotplug event), don't revert
                    if (m_currentSerialPortChain == portChain || m_currentSerialPortChain.isEmpty()) {
                        qCDebug(log_core_serial_conn) << "Reverting to previous port chain:" << previousPortChain;
                        m_currentSerialPortPath = previousPortPath;
                        m_currentSerialPortChain = previousPortChain;

                        if (m_hotplugHandler) {
                            m_hotplugHandler->SetCurrentSerialPortPortChain(previousPortChain);
                        }
                    } else {
                        qCInfo(log_core_serial_hotplug) << "Port chain changed during timeout (new device detected?), keeping current chain:"
                                                << m_currentSerialPortChain << "instead of reverting to" << portChain;
                    }
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
    
    qCDebug(log_core_serial_conn) << "Serial port switch successful to:" << selectedDevice.serialPortPath 
                            << "Ready state:" << ready;
    return true;
}

/*
 * Open the serial port and check the baudrate and mode
 * This method now runs in the worker thread due to QueuedConnection
 */
void SerialPortManager::onSerialPortConnected(const QString &portName){
    qCDebug(log_core_serial_conn) << "Serial port connected: " << portName;
    // Also explicitly log during diagnostics
    if (!m_logFilePath.contains("serial_log.txt")) {
        log(QString("Serial port connected: %1").arg(portName));
    }
    
    // Detect chip type FIRST
    m_currentChipType = detectChipType(portName);
    qCDebug(log_core_serial_config) << "Detected chip type:" << (m_currentChipType == ChipType::CH9329 ? "CH9329" : 
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
    qCInfo(log_core_serial_config) << "Using chip strategy:" << m_chipStrategy->chipName();
    
    if (m_currentChipType == ChipType::CH9329) {
        // Check if CH340 driver is installed for CH9329 chip
        if (!checkCH340DriverInstalled()) {
            qCWarning(log_core_serial_config) << "CH9329 detected but CH340 driver is not installed";
            emit driverInstallationRequired();
        }
        // Start async initialization for CH9329
        int tryBaudrate = determineBaudrate();
        initializeCH9329Async(portName, tryBaudrate);
    } else {
        // Handle CH32V208 synchronously since it's simpler
        initializeCH32V208Sync(portName);
    }
    
    qCDebug(log_core_serial_conn) << "Serial port connection process initiated for port:" << portName;
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

bool SerialPortManager::openPortWithRetries(const QString &/*portName*/, int /*tryBaudrate*/) {
    // This method is now deprecated in favor of the async initialization methods
    // It should not be called in the new flow, but keeping it for backward compatibility
    qCWarning(log_core_serial_conn) << "openPortWithRetries called - this should not happen with new async initialization";
    return false;
}

/*
* Send configuration command and process the response
*/
ConfigResult SerialPortManager::sendAndProcessConfigCommand() {
    ConfigResult result;
    QByteArray retByte = sendSyncCommand(CMD_GET_PARA_CFG, true);
    if (retByte.isEmpty()) return result;
    
    // qCDebug(log_core_serial_conn) << "Data read from serial port: " << retByte.toHex(' ');
    CmdDataParamConfig config = CmdDataParamConfig::fromByteArray(retByte);

    // Persist key parameters to GlobalSetting so UI and other modules can access device configuration
    GlobalSetting::instance().setSerialPortBaudrate(static_cast<int>(config.baudrate));
    GlobalSetting::instance().setVID(QString("%1").arg(config.vid, 4, 16, QChar('0')).toUpper());
    GlobalSetting::instance().setPID(QString("%1").arg(config.pid, 4, 16, QChar('0')).toUpper());
    // Store the custom USB descriptor flag (single byte) as hex string for compatibility with existing UI
    GlobalSetting::instance().setUSBEnabelFlag(QString("%1").arg(config.custom_usb_desc, 2, 16, QChar('0')).toUpper());

    qCDebug(log_core_serial_config) << "Stored device config to GlobalSetting: baudrate:" << config.baudrate
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
            qCWarning(log_core_serial_config) << "CH32V208 chip: Forcing stored baudrate to 115200 instead of" << workingBaudrate;
            workingBaudrate = BAUDRATE_HIGHSPEED;
        }
        qCDebug(log_core_serial_config) << "Storing working baudrate:" << workingBaudrate;
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
    qCDebug(log_core_serial_config) << "Starting CH9329 initialization with baudrate order:" << baudOrder;
    attemptCH9329Connection(portName, baudOrder, 0, 0, 2); // 2 cycles max
}

// Helper method for CH32V208 sync initialization
void SerialPortManager::initializeCH32V208Sync(const QString &portName) {
    qCDebug(log_core_serial_config) << "Initializing CH32V208 behavior for serial port";
    storeBaudrateIfNeeded(BAUDRATE_HIGHSPEED);

    // CH32V208 only supports 115200; ensure the port is opened at 115200 before signalling success
    if (openPort(portName, BAUDRATE_HIGHSPEED)) {
        // Give the port a moment to stabilize
        QThread::msleep(100);

        // TARGET RESTART FIX: Send CMD_RESET to force the CH32V208 to reinitialize its HID interfaces.
        // After a target restart, the CH32V208 stays powered by the host USB but its target-side
        // USB HID interface becomes stale. Simply opening the port and sending GET_INFO is not enough —
        // the chip needs an explicit reset to re-establish HID communication with the target.
        // Without this reset, commands (keyboard/mouse) are accepted by the serial port but never
        // forwarded to the target, causing "VideoPane can't send data to target" after target restart.
        qCDebug(log_core_serial_config) << "Sending CMD_RESET to CH32V208 to reinitialize HID interfaces...";
        QByteArray resetResp = sendSyncCommand(CMD_RESET, true);
        if (resetResp.isEmpty()) {
            qCWarning(log_core_serial_conn) << "No response to CMD_RESET — chip may still be booting, retrying...";
            // Give the chip more time and retry
            for (int retry = 0; retry < 5; retry++) {
                QThread::msleep(500 * (retry + 1));  // 500ms, 1000ms, 1500ms, 2000ms, 2500ms
                resetResp = sendSyncCommand(CMD_RESET, true);
                if (!resetResp.isEmpty()) {
                    qCInfo(log_core_serial_conn) << "CMD_RESET succeeded after" << (retry + 1) << "retries";
                    break;
                }
                qCDebug(log_core_serial_conn) << "CMD_RESET retry" << (retry + 1) << "— no response yet";
            }
        }

        // Now verify with GET_INFO — retry with backoff to give the chip time to fully initialize
        QByteArray getInfoResp;
        for (int retry = 0; retry < 5; retry++) {
            getInfoResp = sendSyncCommand(CMD_GET_INFO, true);
            if (!getInfoResp.isEmpty()) {
                qCInfo(log_core_serial_conn) << "CMD_GET_INFO responded after" << retry << "retries";
                break;
            }
            if (retry < 4) {
                QThread::msleep(300 * (retry + 1));  // 300ms, 600ms, 900ms, 1200ms
            }
        }

        if (getInfoResp.isEmpty()) {
            // Chip is not responding at all — don't set ready=true.
            // The watchdog will detect the communication timeout and trigger another recovery.
            qCWarning(log_core_serial_conn) << "CH32V208 not responding after retries — ready stays false, watchdog will retry";
            ready = false;
            if (m_commandCoordinator) {
                m_commandCoordinator->setReady(false);
            }
            // Still emit the signal so timers are set up, but ready=false prevents commands
            emit serialPortConnectionSuccess(portName);
            return;
        }

        // Set ready state and sync with command coordinator
        ready = true;
        if (m_commandCoordinator) {
            m_commandCoordinator->setReady(true);
        }

        emit serialPortConnectionSuccess(portName);
        qCInfo(log_core_serial_config) << "CH32V208 initialization completed successfully";
    } else {
        qCWarning(log_core_serial_conn) << "Failed to open CH32V208 port at" << BAUDRATE_HIGHSPEED << "for" << portName;
        ready = false;
        if (m_commandCoordinator) {
            m_commandCoordinator->setReady(false);
        }
    }
}

// Improved async CH9329 connection attempt
void SerialPortManager::attemptCH9329Connection(const QString &portName, const QList<int> &baudOrder, int baudIndex, int cycle, int maxCycles) {
    qCDebug(log_core_serial_config) << "attemptCH9329Connection: port=" << portName
                             << " baudIndex=" << baudIndex
                             << " cycle=" << cycle
                             << " maxCycles=" << maxCycles;

    if (cycle >= maxCycles) {
        qCWarning(log_core_serial_config) << "CH9329 initialization failed after" << maxCycles << "cycles";

        // HOTPLUG FIX: Instead of leaving ready=false permanently, schedule a delayed retry
        // This gives the USB stack time to stabilize after rapid plug/unplug
        if (m_initRetryCount < MAX_INIT_RETRIES) {
            scheduleInitRetry(portName, baudOrder.first());
            return;
        }

        qCCritical(log_core_serial_config) << "CH9329 initialization failed after" << MAX_INIT_RETRIES << "retries, giving up";
        ready = false;
        if (m_commandCoordinator) {
            m_commandCoordinator->setReady(false);
        }
        return;
    }

    if (baudIndex >= baudOrder.size()) {
        // Move to next cycle
        qCDebug(log_core_serial_config) << "Starting cycle" << (cycle + 1) << "of CH9329 initialization";
        attemptCH9329Connection(portName, baudOrder, 0, cycle + 1, maxCycles);
        return;
    }

    int currentBaud = baudOrder[baudIndex];
    qCDebug(log_core_serial_conn) << "Attempting to open port" << portName << "at baud" << currentBaud << "(cycle" << (cycle + 1) << "of" << maxCycles << ")";

    if (openPort(portName, currentBaud)) {
        // Give the port a moment to stabilize
        QThread::msleep(50);

        qCDebug(log_core_serial_conn) << "Serial port opened, validating with synchronous CMD_GET_INFO:" << portName << "baud" << currentBaud;
        QByteArray getInfoResp = sendSyncCommand(CMD_GET_INFO, true);

         if (!getInfoResp.isEmpty() && getInfoResp.size() >= 4) {
            // Valid response received - initialization successful
            qCInfo(log_core_serial_config) << "CH9329 initialization successful at baudrate" << currentBaud;

            // HOTPLUG FIX: Reset retry counter on success
            m_initRetryCount = 0;
            m_pendingInitPortName.clear();
            m_pendingInitBaudrate = 0;

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

        qCWarning(log_core_serial_conn) << "No valid CMD_GET_INFO response received after opening port" << portName << "at baud" << currentBaud << "- closing and will try the next baud/attempt";
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
    qCDebug(log_core_serial_conn) << "Serial port disconnected:" << portName;
    
    // Update state manager
    if (m_stateManager) {
        m_stateManager->setConnectionState(ConnectionState::Disconnected);
    }
    
    // Stop USB status check timer when device is unplugged
    if (m_usbStatusCheckTimer) {
        if (m_usbStatusCheckTimer->isActive()) {
            m_usbStatusCheckTimer->stop();
            qCDebug(log_core_serial_hotplug) << "USB status check timer stopped due to device unplug";
        }
    }
    
    if (serialPort) {
        qCDebug(log_core_serial_conn) << "Last error:" << serialPort->errorString();
        qCDebug(log_core_serial_conn) << "Port state:" << (serialPort->isOpen() ? "Open" : "Closed");
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
    qCDebug(log_core_serial_conn) << "Serial port connection success: " << portName;

    // Async handle the keyboard and mouse events
    qCDebug(log_core_serial_conn) << "Observe" << portName << "data ready and bytes written.";
    if (serialPort) {
        connect(serialPort, &QSerialPort::readyRead, this, &SerialPortManager::readData);
        connect(serialPort, &QSerialPort::bytesWritten, this, &SerialPortManager::bytesWritten);
        // Connect error signal for enhanced error handling
        connect(serialPort, QOverload<QSerialPort::SerialPortError>::of(&QSerialPort::errorOccurred),
                this, &SerialPortManager::handleSerialError);
    } else {
        qCWarning(log_core_serial_conn) << "SerialPortManager: serialPort is null - skipping readyRead/bytesWritten/error connects";
    }
    
    // Re-establish timer signal connections in case they were disconnected during port close
    // CRITICAL: Must be done in worker thread to avoid cross-thread timer issues
    if (QThread::currentThread() == m_serialWorkerThread) {
        reconnectTimerSignals();
    } else {
        qCDebug(log_core_serial_conn) << "onSerialPortConnectionSuccess called from different thread, delegating timer reconnect to worker thread";
        QMetaObject::invokeMethod(this, [this]() {
            reconnectTimerSignals();
        }, Qt::QueuedConnection);
    }
    
    // TARGET RESTART FIX: For CH32V208, only set ready=true if initializeCH32V208Sync
    // successfully got a response from the chip. If the chip is still booting (after
    // target restart + recovery), ready was set to false by initializeCH32V208Sync.
    // We must NOT override it here — doing so would make sendAsyncCommand send commands
    // to a chip that can't respond, causing silent data loss.
    // For CH9329 (or if CH32V208 init already set ready=true), always set ready=true.
    if (!isChipTypeCH32V208() || ready) {
        ready = true;
        if (m_commandCoordinator) {
            m_commandCoordinator->setReady(true);
        }
    } else {
        qCWarning(log_core_serial_conn) << "CH32V208 initialization did not get chip response — keeping ready=false";
    }
    resetErrorCounters();
    m_lastSuccessfulCommand.restart();

    // Clear unplugged detection flags now that device is successfully connected
    // This allows new open attempts if the device is replugged
    m_deviceUnpluggedDetected.store(false);
    m_deviceUnplugCleanupInProgress.store(false);
    // Clear RTS recovery flag (set when CH32V208 was unresponsive and needed hardware reset)
    m_rtsRecoveryInProgress.store(false);
    qCDebug(log_core_serial_hotplug) << "Device unplugged detection flags cleared - port is ready for operation";

    // Cancel any pending port chain clear that was scheduled by a close/reopen cycle.
    // If we don't cancel here, the 2-second delayed timer (scheduled in completePortCloseCleanup)
    // will fire and clear m_currentSerialPortChain, making recovery impossible later.
    cancelPendingPortChainClear();

    int currentBaud = serialPort ? serialPort->baudRate() : 0;
    emit connectedPortChanged(portName, currentBaud);
    qCDebug(log_core_serial_conn) << "Serial port opened successfully:" << portName << "at" << currentBaud << "baud";

    // Inform hotplug handler that serial is open and update its current port chain
    if (m_hotplugHandler) {
        m_hotplugHandler->SetSerialOpen(true);
        m_hotplugHandler->SetCurrentSerialPortPortChain(m_currentSerialPortChain);
    }
    // Also explicitly log during diagnostics
    if (!m_logFilePath.contains("serial_log.txt")) {
        log(QString("Serial port opened successfully: %1 at %2 baud").arg(portName).arg(serialPort->baudRate()));
    }

    qCDebug(log_core_serial_usbswitch) << "Enable the switchable USB now...";
    // serialPort->setDataTerminalReady(false);

    // Start connection watchdog (Phase 3 refactoring)
    if (m_watchdog) {
        m_watchdog->start();
        qCDebug(log_core_serial_watchdog) << "Started ConnectionWatchdog";
    }
    // NOTE: Legacy setupConnectionWatchdog() removed - ConnectionWatchdog handles monitoring

    // Start USB status check timer for CH32V208 (thread-safe)
    if (isChipTypeCH32V208() && m_usbStatusCheckTimer) {
        if (QThread::currentThread() == m_usbStatusCheckTimer->thread()) {
            m_usbStatusCheckTimer->start();
        } else {
            QMetaObject::invokeMethod(m_usbStatusCheckTimer, "start", Qt::QueuedConnection);
        }
        qCDebug(log_core_serial_config) << "Started USB status check timer for CH32V208";
    } else if (!m_usbStatusCheckTimer) {
        qCWarning(log_core_serial_config) << "USB status check timer is null, cannot start";
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
            qCDebug(log_core_serial_config) << "Started periodic GET_INFO timer for" << (isChipTypeCH9329() ? "CH9329" : "CH32V208");
        } else {
            qCDebug(log_core_serial_config) << "Periodic GET_INFO timer suppressed because diagnostics dialog is active";
        }

        // Send an immediate GET_INFO to prime the connection
        sendAsyncCommand(CMD_GET_INFO, true);
    } else {
        qCWarning(log_core_serial_config) << "GET_INFO timer is null, cannot start periodic status updates";
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
    qCDebug(log_core_serial_config) << "Diagnostics dialog active set to:" << active;

    if (m_getInfoTimer) {
        if (active) {
            if (QThread::currentThread() == m_getInfoTimer->thread()) {
                m_getInfoTimer->stop();
            } else {
                QMetaObject::invokeMethod(m_getInfoTimer, "stop", Qt::QueuedConnection);
            }
            qCDebug(log_core_serial_config) << "GET_INFO timer stopped due to diagnostics dialog active";
        } else {
            // Restart GET_INFO for supported devices (CH9329 and CH32V208)
            if (serialPort && serialPort->isOpen() && (isChipTypeCH9329() || isChipTypeCH32V208())) {
                if (QThread::currentThread() == m_getInfoTimer->thread()) {
                    m_getInfoTimer->start();
                } else {
                    QMetaObject::invokeMethod(m_getInfoTimer, "start", Qt::QueuedConnection);
                }
                qCDebug(log_core_serial_conn) << "GET_INFO timer restarted after diagnostics dialog closed";
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
        qCDebug(log_core_serial_config) << "Send reset command success.";
        return true;
    } else{
        qCDebug(log_core_serial_config) << "Send reset command fail.";
        return false;
    }
}

void SerialPortManager::onUsbStatusCheckTimeout() {
    if (m_isShuttingDown || !serialPort || !serialPort->isOpen() || !isChipTypeCH32V208()) {
        return;  // Skip if shutting down, port not open, or not CH32V208
    }

    sendAsyncCommand(CMD_CHECK_USB_STATUS, true);
    qCDebug(log_core_serial_cmd) << "Sent USB status check command asynchronously";
}

// TARGET RESTART FIX: Handle target USB status changes detected from GET_INFO responses.
//
// When the target computer restarts, the CH32V208 chip stays powered by the host USB
// and continues responding to serial commands on the host side. However, its target-side
// USB HID interface (keyboard/mouse pass-through to target) and video path become stale.
// The existing ConnectionWatchdog never triggers because:
//   1. Serial port stays open (CH32V208 is still enumerated on host)
//   2. Periodic CMD_GET_INFO still gets a response (CH32V208 is alive)
//   3. recordSuccess() is called, resetting the watchdog timer
//   4. Keyboard/mouse commands are "written" successfully to serial port
// But none of these commands actually reach the target — the target-side USB is dead.
//
// This handler detects targetUSBStatus(false) from the GET_INFO response and triggers
// an RTS hardware reset of the CH32V208, which resets the entire chip (both target-side
// USB HID and video interfaces). This is the same mechanism used by the factory reset
// button, which is known to restore full functionality.
void SerialPortManager::handleTargetUsbStatusChanged(bool connected)
{
    if (m_isShuttingDown) {
        return;
    }

    // Only relevant for CH32V208 (KVMGO) — other chips don't have target-side USB
    if (!isChipTypeCH32V208()) {
        return;
    }

    // Create debounce timer lazily (must be in correct thread — we're in worker thread
    // because this slot is connected via Qt::AutoConnection and SerialPortManager lives
    // in the worker thread)
    if (!m_targetDisconnectRecoveryTimer) {
        m_targetDisconnectRecoveryTimer = new QTimer(this);
        m_targetDisconnectRecoveryTimer->setSingleShot(true);
        connect(m_targetDisconnectRecoveryTimer, &QTimer::timeout, this, [this]() {
            if (m_isShuttingDown || !m_targetRecoveryInProgress.load()) {
                return;
            }

            qCWarning(log_core_serial_watchdog) << "TARGET RESTART DETECTED — target USB disconnected for >2s, initiating RTS hardware reset";

            // Block all commands during recovery
            ready = false;
            if (m_commandCoordinator) {
                m_commandCoordinator->setReady(false);
            }

            // Stop watchdog and periodic timers to prevent interference during RTS reset.
            // Without stopping these, the watchdog would call recordSuccess() on the
            // (still-responding) CH32V208 responses, masking the recovery, and periodic
            // GET_INFO commands would be sent during the RTS reset sequence.
            if (m_watchdog) {
                m_watchdog->stop();
            }
            if (m_usbStatusCheckTimer && m_usbStatusCheckTimer->isActive()) {
                m_usbStatusCheckTimer->stop();
            }
            if (m_getInfoTimer && m_getInfoTimer->isActive()) {
                m_getInfoTimer->stop();
            }

            // Trigger RTS hardware reset — this resets the entire CH32V208 chip:
            // - Target-side USB HID interface (keyboard/mouse)
            // - Video capture path (HDMI passthrough)
            // - Serial communication interface
            // The RTS reset sequence takes ~6.5s total:
            //   RTS low (4s) → RTS high → close port (0.5s) → reopen port (2s) → reinit
            qCInfo(log_core_serial_watchdog) << "Initiating RTS hardware reset to reinitialize CH32V208...";
            factoryResetHipChip();

            // Safety timeout: if target doesn't reconnect within 20s after RTS reset,
            // the CH32V208's target-side USB interface didn't recover. On Linux, emit
            // serialRecoveryFailed() so DeviceLifecycleManager can trigger USB hub port
            // reset as a last resort (forces full USB re-enumeration of the composite device).
            QTimer::singleShot(20000, this, [this]() {
                if (m_targetRecoveryInProgress.load()) {
                    qCWarning(log_core_serial_watchdog) << "Safety timeout: target didn't reconnect after 20s, clearing recovery flag";
                    m_targetRecoveryInProgress.store(false);
                    // Ensure watchdog and timers are running
                    if (m_watchdog && !m_watchdog->isRunning()) {
                        m_watchdog->start();
                    }
                    if (m_usbStatusCheckTimer && !m_usbStatusCheckTimer->isActive()) {
                        m_usbStatusCheckTimer->start();
                    }
                    if (m_getInfoTimer && !m_getInfoTimer->isActive()) {
                        m_getInfoTimer->start();
                    }

#ifdef __linux__
                    // CH32V208 target-side USB didn't recover after RTS reset.
                    // Emit serialRecoveryFailed() to trigger USB hub port reset via
                    // DeviceLifecycleManager → UsbPortResetter.
                    if (isChipTypeCH32V208()) {
                        qCWarning(log_core_serial_watchdog) << "CH32V208 target-side USB didn't recover after RTS reset"
                                                            << "— emitting serialRecoveryFailed() for USB hub port reset";
                        emit serialRecoveryFailed();
                    }
#endif
                }
            });
        });
    }

    if (!connected && serialPort && serialPort->isOpen() && !m_targetRecoveryInProgress.load()) {
        // Target USB just disconnected (target restart). Start debounce timer —
        // wait 2 seconds to confirm the disconnection is persistent before triggering
        // the expensive RTS reset. This avoids false triggers from momentary glitches.
        qCInfo(log_core_serial_watchdog) << "Target USB disconnected — starting 2s debounce before RTS recovery";
        m_targetRecoveryInProgress.store(true);
        m_targetDisconnectRecoveryTimer->start(2000);
    } else if (connected && m_targetRecoveryInProgress.load()) {
        // Target USB came back (either naturally after target boot, or from RTS reset recovery).
        if (m_targetDisconnectRecoveryTimer && m_targetDisconnectRecoveryTimer->isActive()) {
            // Was still debouncing — the disconnect was momentary, cancel recovery
            m_targetDisconnectRecoveryTimer->stop();
            m_targetRecoveryInProgress.store(false);
            qCInfo(log_core_serial_watchdog) << "Target USB reconnected during debounce — canceling RTS recovery";
        } else {
            // Recovery was in progress (RTS reset completed and target came back).
            // Clear the recovery flag, restart watchdog and timers.
            m_targetRecoveryInProgress.store(false);
            qCInfo(log_core_serial_watchdog) << "Target USB reconnected after RTS recovery — resuming normal operation";

            // Restart watchdog and periodic timers
            if (m_watchdog) {
                m_watchdog->resetCounters();
                m_watchdog->start();
            }
            if (m_usbStatusCheckTimer && !m_usbStatusCheckTimer->isActive()) {
                m_usbStatusCheckTimer->start();
            }
            if (m_getInfoTimer && !m_getInfoTimer->isActive()) {
                m_getInfoTimer->start();
            }
        }
    }
}

void SerialPortManager::onGetInfoTimeout() {
    if (m_isShuttingDown || !serialPort || !serialPort->isOpen()) {
        return;  // Skip if shutting down or port not open
    }

    {
        QMutexLocker locker(&m_diagMutex);
        if (m_suppressGetInfo) {
            qCDebug(log_core_serial_conn) << "Suppressed periodic GET_INFO due to diagnostics dialog active";
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
    qCDebug(log_core_serial_config) << "Factory reset HID chip requested from thread:" << QThread::currentThread()->objectName();
    
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
    qCWarning(log_core_serial_config) << "FactoryResetManager not initialized";
    emit factoryResetCompleted(false);
    return false;
}

// Slot handler for thread-safe factory reset operation
void SerialPortManager::handleFactoryReset() {
    qCDebug(log_core_serial_config) << "handleFactoryReset slot called in thread:" << QThread::currentThread()->objectName();
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
    qCDebug(log_core_serial_config) << "Factory reset HID chip V191 requested from thread:" << QThread::currentThread()->objectName();
    
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
    qCWarning(log_core_serial_config) << "FactoryResetManager not initialized";
    emit factoryResetCompleted(false);
    return false;
}

// Slot handler for thread-safe factory reset V191 operation
void SerialPortManager::handleFactoryResetV191() {
    qCDebug(log_core_serial_config) << "handleFactoryResetV191 slot called in thread:" << QThread::currentThread()->objectName();
    handleFactoryResetV191Internal();
}

/*
 * Synchronous factory reset for diagnostics - RTS pin method
 * This blocks until reset is complete or timeout occurs
 */
bool SerialPortManager::factoryResetHipChipSync(int timeoutMs) {
    qCDebug(log_core_serial_config) << "Synchronous factory reset HID chip requested, timeout:" << timeoutMs << "ms";
    
    // Always execute directly in the calling thread for diagnostics
    return handleFactoryResetSyncInternal(timeoutMs);
}

/*
 * Synchronous factory reset V191 for diagnostics - command method
 * This blocks until reset is complete or timeout occurs
 */
bool SerialPortManager::factoryResetHipChipV191Sync(int timeoutMs) {
    qCDebug(log_core_serial_config) << "Synchronous factory reset V191 HID chip requested, timeout:" << timeoutMs << "ms";
    
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
    qCWarning(log_core_serial_config) << "FactoryResetManager not initialized";
    return false;
}

/*
 * Internal synchronous factory reset V191 implementation - command method
 */
bool SerialPortManager::handleFactoryResetV191SyncInternal(int timeoutMs) {
    if (m_factoryResetManager) {
        return m_factoryResetManager->handleFactoryResetV191SyncInternal(timeoutMs);
    }
    qCWarning(log_core_serial_config) << "FactoryResetManager not initialized";
    return false;
}

/*
 * Destructor
 */
SerialPortManager::~SerialPortManager() {
    qCDebug(log_core_serial_conn) << "Destroy serial port manager.";
    
    // Fast exit if main shutdown already completed - avoid any risky operations 
    if (m_isShuttingDown) {
        qCDebug(log_core_serial_conn) << "SerialPortManager: Main shutdown completed, skipping destructor cleanup";
        qCDebug(log_core_serial_conn) << "Serial port manager destroyed";
        return;
    }
    
    // Only do cleanup if we're in abnormal termination (m_isShuttingDown not set)
    qCDebug(log_core_serial_conn) << "SerialPortManager: Abnormal termination detected, performing emergency cleanup";
    
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
            qCWarning(log_core_serial_conn) << "Logging thread did not stop gracefully, forcing termination";
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
    
    qCDebug(log_core_serial_conn) << "Serial port manager destroyed";
}

/*
 * Open the serial port
 */
bool SerialPortManager::openPort(const QString &portName, int baudRate) {
    qCDebug(log_core_serial_conn) << "Trying to open serial port: " << portName << ", baudrate: " << baudRate;
    if (m_isShuttingDown) {
        qCDebug(log_core_serial_conn) << "Cannot open port during shutdown";
        return false;
    }

    // Reset fatal error guard — new open attempt means previous error state is cleared
    m_fatalErrorHandled.store(false);

    // CRITICAL FIX: Check state machine to prevent opens during close/error states
    SerialPortState currentState = m_portState.load();
    if (currentState == SerialPortState::CLOSING) {
        qCWarning(log_core_serial_conn) << "Rejecting openPort - port is in CLOSING state (deleteLater pending)";
        return false;
    }
    if (currentState == SerialPortState::ERROR_STATE) {
        // Before rejecting, check if the device is actually available again.
        // This handles the case where a transient error put us in ERROR_STATE
        // but the device has since recovered (e.g., USB glitch resolved).
        // First try port name match, then fall back to VID/PID matching for Linux
        // where device node names can change after re-enumeration (e.g., ttyACM0 -> ttyACM1).
        bool portPresent = false;
        for (const QSerialPortInfo &pi : QSerialPortInfo::availablePorts()) {
            if (pi.portName() == portName || portName.contains(pi.portName())) {
                portPresent = true;
                break;
            }
        }
        if (!portPresent) {
            // Port name didn't match — try VID/PID fallback (handles Linux device node renaming)
            portPresent = isKnownDevicePresent();
        }
        if (portPresent) {
            // Device is available - clear error state and allow reconnection
            qCInfo(log_core_serial_conn) << "Port is in ERROR_STATE but device is available at" << portName << "- clearing error state and allowing reconnection";
            m_portState.store(SerialPortState::CLOSED);
            m_deviceUnpluggedDetected.store(false);
            m_deviceUnplugCleanupInProgress.store(false);
        } else {
            qCWarning(log_core_serial_conn) << "Rejecting openPort - port is in ERROR_STATE and device is not available:" << portName;
            return false;
        }
    }

    // Check if device was just unplugged - if so, reject the open attempt to prevent "Access denied" errors
    // This is critical to avoid race conditions between device removal and port open operations
    if (m_deviceUnplugCleanupInProgress.load()) {
        // If the OS already reports the port as present, clear the transient cleanup guard and continue.
        // This prevents a stuck flag from permanently blocking open attempts (observed in the field).
        // First try port name match, then fall back to VID/PID matching for Linux
        // where device node names can change after re-enumeration (e.g., ttyACM0 -> ttyACM1).
        bool portPresent = false;
        for (const QSerialPortInfo &pi : QSerialPortInfo::availablePorts()) {
            if (pi.portName() == portName || portName.contains(pi.portName())) {
                portPresent = true;
                break;
            }
        }
        if (!portPresent) {
            // Port name didn't match — try VID/PID fallback (handles Linux device node renaming)
            portPresent = isKnownDevicePresent();
        }
        if (portPresent) {
            qCWarning(log_core_serial_hotplug) << "Transient unplug-cleanup flag set but port is present -> clearing flag and continuing open:" << portName;
            m_deviceUnplugCleanupInProgress.store(false);
            m_deviceUnpluggedDetected.store(false);
            // Also clear error state if port is available again
            SerialPortState expectedError = SerialPortState::ERROR_STATE;
            m_portState.compare_exchange_strong(expectedError, SerialPortState::CLOSED);
        } else {
            qCWarning(log_core_serial_hotplug) << "Device unplugged cleanup in progress, rejecting open attempt for port:" << portName;
            qCWarning(log_core_serial_hotplug) << "This prevents race conditions that cause 'Access denied' errors during hotplug events";
            return false;
        }
    }

    // Transition to OPENING state
    SerialPortState expected = SerialPortState::CLOSED;
    if (!m_portState.compare_exchange_strong(expected, SerialPortState::OPENING)) {
        // Check if already open
        if (expected == SerialPortState::OPEN) {
            qCDebug(log_core_serial_conn) << "Serial port is already opened.";
            return true;
        }
        qCWarning(log_core_serial_conn) << "Cannot open port - invalid state transition from" << static_cast<int>(expected);
        return false;
    }

    QMutexLocker locker(&m_serialPortMutex);

    // If there is an existing QSerialPort instance that is not open, delete it to avoid stale internal state (e.g., stale file descriptor / notifiers)
    // FIX: Use deleteLater instead of direct delete to match closePortInternal's deletion strategy
    if (serialPort != nullptr && !serialPort->isOpen()) {
        qCDebug(log_core_serial_conn) << "Existing closed QSerialPort instance found - marking for deletion to ensure fresh instance before open.";
        serialPort->deleteLater();
        serialPort = nullptr;  // Clear pointer temporarily
    }

    if (serialPort != nullptr && serialPort->isOpen()) {
        qCDebug(log_core_serial_conn) << "Serial port is already opened.";
        m_portState.store(SerialPortState::OPEN);
        return true;
    }

    emit statusUpdate("Going to open the port");

    if(serialPort == nullptr){
        qCDebug(log_core_serial_conn) << "Creating new QSerialPort in worker thread";
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
        qCDebug(log_core_serial_conn) << "Open port" << portName + ", baudrate: " << baudRate << "with read buffer size" << serialPort->readBufferSize();

        // Show existing buffer sizes before clearing them (read and write sizes)
        qCDebug(log_core_serial_conn) << "Serial buffer sizes before clear - bytesAvailable:" << serialPort->bytesAvailable()
                                 << "bytesToWrite:" << serialPort->bytesToWrite();

        // Clear any stale data in the serial port buffers to prevent data corruption
        // This is critical when device is unplugged and replugged
        qCDebug(log_core_serial_conn) << "Clearing serial port buffers to remove stale data";
        serialPort->clear(QSerialPort::AllDirections);

        // Log buffer sizes after clearing to confirm the clear worked
        qCDebug(log_core_serial_conn) << "Serial buffer sizes after clear - bytesAvailable:" << serialPort->bytesAvailable()
                                 << "bytesToWrite:" << serialPort->bytesToWrite();


        // Reset error counters on successful connection
        resetErrorCounters();

        // CRITICAL: Transition to OPEN state after successful open
        m_portState.store(SerialPortState::OPEN);

        emit statusUpdate("");
        emit connectedPortChanged(portName, baudRate);
        qCDebug(log_core_serial_conn) << "Serial port: " << portName << ", baudrate: " << baudRate << "opened";
        return true;
    } else {
        QString errorMsg = QString("Open port failure: %1 (Error: %2)")
                          .arg(serialPort->errorString())
                          .arg(static_cast<int>(lastError));
        qCWarning(log_core_serial_conn) << errorMsg;

        // CRITICAL: Roll back to CLOSED state on failure
        m_portState.store(SerialPortState::CLOSED);

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
        qCWarning(log_core_serial_conn) << "Failed to open port on attempt" << (attempt + 1) 
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
        qCDebug(log_core_serial_conn) << "closePort called from different thread, routing through worker thread";
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
    qCDebug(log_core_serial_conn) << "Close serial port";

    // CRITICAL: Transition to CLOSING state immediately to block new open attempts
    SerialPortState previousState = m_portState.exchange(SerialPortState::CLOSING);
    qCDebug(log_core_serial_conn) << "Port state transition:" << static_cast<int>(previousState) << "-> CLOSING";

    QMutexLocker locker(&m_serialPortMutex);

    if (serialPort != nullptr) {
        qCDebug(log_core_serial_conn) << "Closing serial port instance:" << static_cast<void*>(serialPort);

        if (serialPort->isOpen()) {
            // Disconnect all signals BEFORE any operations
            disconnect(serialPort, nullptr, this, nullptr);

            qCDebug(log_core_serial_conn) << "Close serial port - current buffer sizes before close - bytesAvailable:" << serialPort->bytesAvailable()
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
                qCDebug(log_core_serial_conn) << "Serial port closed";
                // NOTE: Do NOT call QCoreApplication::processEvents() here.
                // Calling it inside a mutex lock risks re-entrancy/deadlock.
                // deleteLater() + QTimer::singleShot(0) below handle cleanup safely.
            } catch (...) {
                qCWarning(log_core_serial_conn) << "Exception during serial port close";
            }
        }

        // Enhanced deletion with additional safety measures
        // Store pointer but DO NOT clear serialPort immediately to avoid race conditions
        QObject* portPtr = serialPort;

        // Schedule deletion for next event loop to avoid immediate socket notifier issues
        // Use QTimer::singleShot for more reliable deferred deletion
        QTimer::singleShot(0, this, [this, portPtr]() {
            if (portPtr) {
                qCDebug(log_core_serial_conn) << "Deleting serial port instance:" << static_cast<void*>(portPtr);
                portPtr->deleteLater();
                // Clear pointer only after scheduling deletion
                QMutexLocker deleteLocker(&m_serialPortMutex);
                if (serialPort == portPtr) {
                    serialPort = nullptr;
                    qCDebug(log_core_serial_conn) << "SerialPort instance pointer cleared";
                }
                // CRITICAL: Transition to CLOSED state after deletion is scheduled
                m_portState.store(SerialPortState::CLOSED);
                qCDebug(log_core_serial_conn) << "Port state transition: CLOSING -> CLOSED";
            }
        });
    } else {
        qCDebug(log_core_serial_conn) << "Serial port is not opened (serialPort is nullptr).";
        // Transition to CLOSED state even if no port instance
        m_portState.store(SerialPortState::CLOSED);
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

    // HOTPLUG FIX: Don't immediately clear the port chain - use delayed clear instead
    // This prevents race conditions where the device is unplugged and immediately replugged
    // The delayed timer gives the USB stack time to re-enumerate before we clear state
    if (!m_currentSerialPortChain.isEmpty()) {
        qCDebug(log_core_serial_hotplug) << "Scheduling delayed port chain clear for" << m_currentSerialPortChain
                                 << "(will clear after 2s if no new device connects)";
        m_pendingPortChainClear = m_currentSerialPortChain;
        if (m_portChainClearTimer) {
            // Stop any previous timer and start fresh
            m_portChainClearTimer->stop();
            m_portChainClearTimer->start(2000);  // 2 second delay
        }
    }

    // Inform hotplug handler that serial is closed and cancel any pending auto-connect attempts
    if (m_hotplugHandler) {
        m_hotplugHandler->SetSerialOpen(false);
        m_hotplugHandler->CancelAutoConnectAttempts();
    }

    // Check if there are any available ports before emitting the status signal
    // This prevents showing "NA" when the system is simply transitioning between ports
    bool hasAvailablePorts = false;
    QString firstAvailablePort;
    for (const QSerialPortInfo &info : QSerialPortInfo::availablePorts()) {
        if (!info.portName().isEmpty()) {
            hasAvailablePorts = true;
            firstAvailablePort = info.portName();
            break;
        }
    }

    // Only emit "NA" if no ports are available, otherwise emit the first available port
    if (!hasAvailablePorts) {
        // Notify listeners that port is not available
        emit connectedPortChanged("NA", 0);
    } else {
        // Find and emit the first available port with its baudrate instead of "Searching" to reduce delay
        for (const QSerialPortInfo &info : QSerialPortInfo::availablePorts()) {
            if (!info.portName().isEmpty()) {
                // Emit the actual port name and baudrate immediately to reduce UI delay
                int currentBaud = (serialPort && serialPort->isOpen()) ? serialPort->baudRate() : 0;
                emit connectedPortChanged(info.portName(), currentBaud);
                
                // Schedule a timeout to revert to "Not Connected" if no device connects within a reasonable time
                QTimer::singleShot(3000, this, [this, info]() {
                    // Only update status if we're still in the disconnected state and no actual connection happened
                    if (ready == false && serialPort && serialPort->isOpen() == false) {
                        emit connectedPortChanged("NA", 0);
                    }
                });
                break;
            }
        }
    }
    
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
        qCWarning(log_core_serial_conn) << "Cannot restart - no serial port instance (serialPort is nullptr)";
        // Try to recover using stored port information
        if (!m_currentSerialPortPath.isEmpty()) {
            qCInfo(log_core_serial_conn) << "Attempting to restore serialPort using stored path:" << m_currentSerialPortPath;
            int defaultBaud = getCurrentBaudrate();
            if (defaultBaud <= 0) defaultBaud = DEFAULT_BAUDRATE;
            
            // Try to restore the connection
            return openPort(m_currentSerialPortPath, defaultBaud);
        }
        return false;
    }
    
    QString portName = serialPort->portName();
    qint32 baudRate = serialPort->baudRate();
    qCDebug(log_core_serial_conn) << "Restart port" << portName << "baudrate:" << baudRate;
    
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
    qCDebug(log_core_serial_conn) << "Starting internal restart for port" << portName << "at" << baudRate << "bps";
    
    // Close the port first
    closePortInternal();
    
    // Use QTimer instead of blocking QEventLoop
    QTimer::singleShot(150, this, [this, portName, baudRate]() {
        qCDebug(log_core_serial_conn) << "Reopening port after restart delay";
        
        // Attempt to reopen the port
        bool openResult = openPort(portName, baudRate);
        if (openResult) {
            qCDebug(log_core_serial_conn) << "Port restart successful for" << portName;
            // After successful restart, emit connection success to restart timers  
            emit serialPortConnectionSuccess(portName);
            qCInfo(log_core_serial_conn) << "Serial port restart completed - timers restarted";
            emit serialPortReset(false);
        } else {
            qCWarning(log_core_serial_conn) << "Port restart failed for" << portName;
            emit serialPortReset(false);
        }
    });
    
    return true;  // Return true since restart is now async
}

// New async implementation
void SerialPortManager::restartPortInternalAsync(const QString &portName, qint32 baudRate) {
    qCDebug(log_core_serial_conn) << "Async restart for port" << portName << "at" << baudRate << "bps";
    
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
        // Ensure the open operation happens in the worker thread
        QMetaObject::invokeMethod(this, [this, portName, baudRate]() {
            bool openResult = openPort(portName, baudRate);
            if (openResult) {
                qCDebug(log_core_serial_conn) << "Port restart successful for" << portName;
                // After successful restart, restart the timers and emit connection success directly
                // Instead of calling onSerialPortConnected which would repeat the full initialization
                emit serialPortConnectionSuccess(portName);
                qCInfo(log_core_serial_conn) << "Serial port restart completed - timers should be restarted automatically";
            } else {
                qCWarning(log_core_serial_conn) << "Port restart failed for" << portName;
            }
            emit serialPortReset(false);
        }, Qt::QueuedConnection);
    });
}

// Helper method to stop all timers safely (must be called from worker thread)
void SerialPortManager::stopAllTimers(bool disconnectSignals) {
    // Use BlockingQueuedConnection so the caller only returns after timers are
    // truly stopped.  The previous QueuedConnection was async, meaning a caller
    // on the MainThread could proceed while timers were still firing on the
    // WorkerThread, leading to use-after-free / double-close scenarios.
    if (QThread::currentThread() != this->thread()) {
        QMetaObject::invokeMethod(this, [this, disconnectSignals]() {
            stopAllTimers(disconnectSignals);
        }, Qt::BlockingQueuedConnection);
        return;
    }
    
    // Add shutdown flag check to prevent operations during shutdown
    if (m_isShuttingDown) {
        qCDebug(log_core_serial_conn) << "stopAllTimers: Already shutting down, ensuring all timers stopped";
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
    
    qCDebug(log_core_serial_conn) << "All timers stopped and disconnected";
}

void SerialPortManager::reconnectTimerSignals() {
    // CRITICAL: Ensure we're executing in the worker thread to avoid cross-thread timer issues
    // This is the root cause of "QObject::killTimer: Timers cannot be stopped from another thread"
    if (QThread::currentThread() != m_serialWorkerThread) {
        qCWarning(log_core_serial_conn) << "reconnectTimerSignals called from wrong thread, delegating to worker thread";
        QMetaObject::invokeMethod(this, [this]() {
            reconnectTimerSignals();
        }, Qt::QueuedConnection);
        return;
    }

    // Re-establish timer signal connections that may have been disconnected during port close
    // CRITICAL: Always disconnect first to prevent duplicate connections during rapid hotplug
    if (m_usbStatusCheckTimer) {
        disconnect(m_usbStatusCheckTimer, nullptr, this, nullptr);
        connect(m_usbStatusCheckTimer, &QTimer::timeout, this, &SerialPortManager::onUsbStatusCheckTimeout);
        qCDebug(log_core_serial_conn) << "USB status check timer signal reconnected (cleaned first)";
    }

    if (m_getInfoTimer) {
        disconnect(m_getInfoTimer, nullptr, this, nullptr);
        connect(m_getInfoTimer, &QTimer::timeout, this, &SerialPortManager::onGetInfoTimeout);
        qCDebug(log_core_serial_conn) << "GET_INFO timer signal reconnected (cleaned first)";
    }

    if (m_connectionWatchdog) {
        disconnect(m_connectionWatchdog, nullptr, this, nullptr);
        qCDebug(log_core_serial_watchdog) << "Connection watchdog timer signals checked (cleaned first)";
    }

    qCDebug(log_core_serial_conn) << "Timer signal connections restored after port reconnection";
}

void SerialPortManager::updateSpecialKeyState(uint8_t data){
    qCDebug(log_core_serial_rx) << "Data received:" << data;
    
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
        qCDebug(log_core_serial_rx) << "readData: Ignored read - shutting down or port not open";
        return;
    }
    
    // Additional safety: Ensure we're in the correct thread
    if (QThread::currentThread() != this->thread()) {
        qCWarning(log_core_serial_rx) << "readData called from wrong thread, ignoring";
        return;
    }
    
    // Mutex protection for serial port access to prevent concurrent access
    QMutexLocker locker(&m_serialPortMutex);
    if (!serialPort || !serialPort->isOpen()) {
        qCDebug(log_core_serial_rx) << "Serial port became invalid during readData";
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
            qCWarning(log_core_serial_rx) << "Large buffer detected:" << bytesAvailable << "bytes - possible data burst or slow processing";
        }
        
        if (bytesAvailable > MAX_READ_SIZE) {
            qCWarning(log_core_serial_rx) << "Limiting read to" << MAX_READ_SIZE << "bytes (" << bytesAvailable << "available)";
            data = serialPort->read(MAX_READ_SIZE);
            
            // Clear excess data to prevent indefinite accumulation
            if (bytesAvailable > MAX_READ_SIZE * 4) {
                qCCritical(log_core_serial_rx) << "CRITICAL: Buffer overflow detected, clearing excess data to prevent crash";
                serialPort->clear();
            }
        } else {
            data = serialPort->readAll();
        }
    } catch (const std::exception& e) {
        qCCritical(log_core_serial_rx) << "Exception occurred while reading serial data:" << e.what();
        // Clear buffer to prevent crash
        if (serialPort && serialPort->isOpen()) {
            serialPort->clear();
        }
        if (isRecoveryNeeded()) {
            attemptRecovery();
        }
        return;
    } catch (...) {
        qCCritical(log_core_serial_rx) << "Unknown exception occurred while reading serial data";
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
        qCDebug(log_core_serial_rx) << "Received empty data from serial port";
        checkAndLogAsyncMessageStatistics();
        return;
    }

    // Use protocol layer for packet parsing (Phase 2 refactoring)
    using namespace SerialProtocolConstants;
    
    // Validate minimum packet size
    if (data.size() < MIN_PACKET_SIZE) {
        qCWarning(log_core_serial_rx) << "Received packet too small, size:" << data.size() << "Data:" << data.toHex(' ');
        return;
    }
    
    // Use protocol layer to extract packet size
    int packetSize = m_protocol->extractPacketSize(data);
    if (packetSize < 0 || packetSize > data.size()) {
        qCWarning(log_core_serial_rx) << "Invalid packet size:" << packetSize 
                                   << "actual data size:" << data.size()
                                   << "Data:" << data.toHex(' ');
        return;
    }
    
    QByteArray packet = data.left(packetSize);
    
    // Use protocol layer to parse packet
    ParsedPacket parsed = m_protocol->parsePacket(packet);
    if (!parsed.valid) {
        qCWarning(log_core_serial_rx) << "Failed to parse packet:" << parsed.errorMessage;
        return;
    }

    // WATCHDOG FIX: Record successful communication so the ConnectionWatchdog knows
    // the link is alive. Previously recordSuccess() was never called, causing the watchdog
    // to always think communication was stale and triggering unnecessary recovery cycles
    // that would close/reopen the port every 30s even when everything was working fine.
    if (m_watchdog) {
        m_watchdog->recordSuccess();
    }
    
    // Check for error status in certain command ranges
    if (parsed.status != STATUS_SUCCESS && (parsed.commandCode >= 0xC0 && parsed.commandCode <= 0xCF)) {
        dumpError(parsed.status, packet);
    } else {
        qCDebug(log_core_serial_rx).nospace().noquote() << "RX (" << serialPort->portName() << "@"
            << (serialPort ? serialPort->baudRate() : 0) << "bps): " << packet.toHex(' ');

        // Log diagnostic info about the received command
        {
            uint8_t cmdCode = packet.size() > 3 ? static_cast<uint8_t>(packet[3]) : 0;
            uint8_t statusByte = packet.size() > 5 ? static_cast<uint8_t>(packet[5]) : 0xFF;
            const char* cmdName = "UNKNOWN";
            switch (cmdCode) {
                case 0x81: cmdName = "GET_INFO_RSP"; break;
                case 0x82: cmdName = "KB_RSP"; break;
                case 0x84: cmdName = "MOUSE_ABS_RSP"; break;
                case 0x85: cmdName = "MOUSE_REL_RSP"; break;
                case 0x88: cmdName = "GET_PARA_CFG_RSP"; break;
                case 0x89: cmdName = "SET_PARA_CFG_RSP"; break;
                case 0x8F: cmdName = "RESET_RSP"; break;
                case 0x97: cmdName = "USB_SWITCH_RSP"; break;
                case 0x99: cmdName = "USB_STATUS_RSP"; break;
                default: break;
            }
            qCDebug(log_core_serial_rx) << "[RX-DIAG] cmd=0x" << Qt::hex << Qt::uppercasedigits << cmdCode
                                        << "(" << cmdName << ") status=0x" << statusByte
                                        << "size=" << packet.size();
        }

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
        qCInfo(log_core_serial_conn) << m_chipStrategy->chipName() << "does not support command-based reconfiguration - use close/reopen instead";
        return false;
    }
    
    // Fallback check for CH32V208 chip (backward compatibility)
    if (!m_chipStrategy && isChipTypeCH32V208()) {
        qCInfo(log_core_serial_conn) << "CH32V208 chip does not support command-based reconfiguration - use close/reopen instead";
        return false;
    }
    
    static QSettings settings("Techxartisan", "Openterface");
    uint8_t mode = (settings.value("hardware/operatingMode", 0x02).toUInt());
    qCDebug(log_core_serial_config) << "Reconfigure to baudrate to" << targetBaudrate << "and mode 0x" << QString::number(mode, 16);
    
    // Use chip strategy to build configuration command if available
    QByteArray command;
    if (m_chipStrategy) {
        command = m_chipStrategy->buildReconfigurationCommand(targetBaudrate, mode);
        if (command.isEmpty()) {
            qCWarning(log_core_serial_config) << "Chip strategy returned empty configuration command";
            return false;
        }
    } else {
        // Legacy command building
        if (targetBaudrate == BAUDRATE_LOWSPEED) {
            command = CMD_SET_PARA_CFG_PREFIX_9600;
            qCDebug(log_core_serial_config) << "Using 9600 baudrate configuration";
        } else {
            command = CMD_SET_PARA_CFG_PREFIX_115200;
            qCDebug(log_core_serial_config) << "Using 115200 baudrate configuration";
        }
        command[5] = mode;  // Set mode byte at index 5 (6th byte)
        command.append(CMD_SET_PARA_CFG_MID);
    }
    
    qCDebug(log_core_serial_config) << "Sending configuration command:" << command.toHex(' ');
    QByteArray retBtyes = sendSyncCommand(command, true);
    
    qCDebug(log_core_serial_rx) << "Configuration response size:" << retBtyes.size() << "data:" << retBtyes.toHex(' ');
    
    if(retBtyes.size() > 0){
        CmdDataResult dataResult = fromByteArray<CmdDataResult>(retBtyes);
        if(dataResult.data == DEF_CMD_SUCCESS){
            qCDebug(log_core_serial_config) << "Set data config success, reconfig to" << targetBaudrate << "baudrate and mode 0x" << QString::number(mode, 16);
            return true;
        }else{
            qCWarning(log_core_serial_config) << "Set data config fail with status code:" << QString("0x%1").arg(dataResult.data, 2, 16, QChar('0'));
            dumpError(dataResult.data, retBtyes);
        } 
    }else{
        qCWarning(log_core_serial_rx) << "Set data config response empty. Port may not be responding.";
        qCWarning(log_core_serial_conn) << "Current port:" << (serialPort ? serialPort->portName() : "null") 
                                   << "Baudrate:" << (serialPort ? QString::number(serialPort->baudRate()) : "N/A")
                                   << "Open:" << (serialPort && serialPort->isOpen() ? "Yes" : "No");
    }

    return false;
}

/*
 * Bytes written to the serial port
 */
void SerialPortManager::bytesWritten(qint64 nBytes){
    // qCDebug(log_core_serial_tx) << nBytes << "bytesWritten";
    Q_UNUSED(nBytes);
}

/*
 * Write the data to the serial port
 */
bool SerialPortManager::writeData(const QByteArray &data) {
    if (m_isShuttingDown) {
        qCDebug(log_core_serial_conn) << "Cannot write data during shutdown";
        return false;
    }
    
    // Execute write operation directly in worker thread
    return writeDataInThread(data);
}

bool SerialPortManager::writeDataInThread(const QByteArray &data) {
    // DEBUG: Log to file for MCP keyboard diagnostics
    {
        QFile debugLog("/tmp/write-data-debug.log");
        if (debugLog.open(QIODevice::Append | QIODevice::Text)) {
            QTextStream out(&debugLog);
            out << "=== writeDataInThread called ===\n";
            out << "data: " << QString::fromLatin1(data.toHex(' ')) << "\n";
            debugLog.close();
        }
    }

    // Enhanced serial port validation with detailed diagnostics
    if (!isSerialPortValid()) {
        qCWarning(log_core_serial_conn) << "Serial port not valid for write operation - state:"
                                   << "serialPort=" << static_cast<void*>(serialPort)
                                   << "isOpen=" << (serialPort ? (serialPort->isOpen() ? "true" : "false") : "N/A")
                                   << "portName=" << (serialPort ? serialPort->portName() : "N/A");
        // Log failure
        QFile debugLog("/tmp/write-data-debug.log");
        if (debugLog.open(QIODevice::Append | QIODevice::Text)) {
            QTextStream out(&debugLog);
            out << "⚠️ WRITE FAILED: serial port not valid\n";
            debugLog.close();
        }
        ready = false;
        if (m_commandCoordinator) {
            m_commandCoordinator->setReady(false);
        }
        return false;
    }

    QMutexLocker locker(&m_serialPortMutex);

    // Double-check after acquiring mutex
    if (!serialPort || !serialPort->isOpen()) {
        qCWarning(log_core_serial_conn) << "Serial port became invalid after mutex lock";
        // Log failure
        QFile debugLog("/tmp/write-data-debug.log");
        if (debugLog.open(QIODevice::Append | QIODevice::Text)) {
            QTextStream out(&debugLog);
            out << "⚠️ WRITE FAILED: serial port invalid after mutex\n";
            debugLog.close();
        }
        ready = false;
        if (m_commandCoordinator) {
            m_commandCoordinator->setReady(false);
        }
        return false;
    }

    try {
        qint64 bytesWritten = serialPort->write(data);
        if (bytesWritten == -1) {
            qCWarning(log_core_serial_tx) << "Failed to write data to serial port:" << serialPort->errorString();
            // Log failure
            QFile debugLog("/tmp/write-data-debug.log");
            if (debugLog.open(QIODevice::Append | QIODevice::Text)) {
                QTextStream out(&debugLog);
                out << "⚠️ WRITE FAILED: " << serialPort->errorString() << "\n";
                debugLog.close();
            }
            return false;
        } else if (bytesWritten != data.size()) {
            qCWarning(log_core_serial_tx) << "Partial write: expected" << data.size() << "bytes, wrote" << bytesWritten;
            // Log failure
            QFile debugLog("/tmp/write-data-debug.log");
            if (debugLog.open(QIODevice::Append | QIODevice::Text)) {
                QTextStream out(&debugLog);
                out << "⚠️ PARTIAL WRITE: expected " << data.size() << ", wrote " << bytesWritten << "\n";
                debugLog.close();
            }
            return false;
        }

        // Ensure data is flushed to OS driver and wait for kernel write completion
        serialPort->flush();

        qCDebug(log_core_serial_tx).nospace().noquote() << "Data written (" << serialPort->portName()
                        << "@" << serialPort->baudRate() << "bps): " << data.toHex(' ');

        // Also explicitly log TX to file during diagnostics
        if (!m_logFilePath.contains("serial_log.txt")) {
            log(QString("TX (%1): %2").arg(serialPort->baudRate()).arg(QString(data.toHex(' '))));
        }

        // Log success
        {
            QFile debugLog("/tmp/write-data-debug.log");
            if (debugLog.open(QIODevice::Append | QIODevice::Text)) {
                QTextStream out(&debugLog);
                out << "✅ WRITE SUCCESS: " << bytesWritten << " bytes\n";
                debugLog.close();
            }
        }


        return true;
        
    } catch (...) {
        qCCritical(log_core_serial_tx) << "Exception occurred while writing to serial port";
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
        qCWarning(log_core_serial_usbswitch) << "Cannot restart switchable USB - serial port not valid";
        return;
    }
    
    qCDebug(log_core_serial_usbswitch) << "Restart the USB port now...";
    
    QMutexLocker locker(&m_serialPortMutex);
    if (!serialPort || !serialPort->isOpen()) {
        qCWarning(log_core_serial_conn) << "Serial port became invalid during USB restart";
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
    qCDebug(log_core_serial_usbswitch) << "Switching USB to host via serial command (async)...";
    
    if (!serialPort || !serialPort->isOpen()) {
        qCWarning(log_core_serial_usbswitch) << "Serial port not open, cannot switch USB to host";
        return;
    }
    
    // Use chip strategy to check if USB switch is supported
    if (m_chipStrategy && !m_chipStrategy->supportsUsbSwitchCommand()) {
        qCDebug(log_core_serial_usbswitch) << m_chipStrategy->chipName() << "does not support serial-based USB switch";
        return;
    }
    
    // Fallback: Only use this method for CH32V208 chips
    if (!m_chipStrategy && !isChipTypeCH32V208()) {
        qCDebug(log_core_serial_usbswitch) << "Not CH32V208 chip, skipping serial-based USB switch";
        return;
    }
    
    sendAsyncCommand(CMD_SWITCH_USB_TO_HOST, true);
    qCInfo(log_core_serial_usbswitch) << "USB switch to host command sent asynchronously";
}

/*
 * Switch USB to target via serial command (new CH32V208 protocol)
 * Command: 57 AB 00 17 05 00 00 00 00 01 + checksum
 * Asynchronous - sends command without waiting for response
 */
void SerialPortManager::switchUsbToTargetViaSerial() {
    qCDebug(log_core_serial_usbswitch) << "Switching USB to target via serial command (async)...";
    
    if (!serialPort || !serialPort->isOpen()) {
        qCWarning(log_core_serial_usbswitch) << "Serial port not open, cannot switch USB to target";
        return;
    }
    
    // Use chip strategy to check if USB switch is supported
    if (m_chipStrategy && !m_chipStrategy->supportsUsbSwitchCommand()) {
        qCDebug(log_core_serial_usbswitch) << m_chipStrategy->chipName() << "does not support serial-based USB switch";
        return;
    }
    
    // Fallback: Only use this method for CH32V208 chips
    if (!m_chipStrategy && !isChipTypeCH32V208()) {
        qCDebug(log_core_serial_usbswitch) << "Not CH32V208 chip, skipping serial-based USB switch";
        return;
    }
    
    sendAsyncCommand(CMD_SWITCH_USB_TO_TARGET, true);
    qCInfo(log_core_serial_usbswitch) << "USB switch to target command sent asynchronously";
}

/*
* Set the USB configuration
*/
void SerialPortManager::setUSBconfiguration(int targetBaudrate){
    qCDebug(log_core_serial_usbswitch) << "================== setUSBconfiguration START ==================";
    qCDebug(log_core_serial_usbswitch) << "setUSBconfiguration called with targetBaudrate=" << targetBaudrate;
    qCDebug(log_core_serial_conn) << "  - serialPort=" << static_cast<void*>(serialPort);
    qCDebug(log_core_serial_conn) << "  - isOpen=" << (serialPort && serialPort->isOpen());
    
    QSettings settings("Techxartisan", "Openterface");
    uint8_t mode = (settings.value("hardware/operatingMode", 0x02).toUInt());
    qCDebug(log_core_serial_config) << "  - Mode from settings: 0x" << QString::number(mode, 16);

    // Select the appropriate command prefix based on target baudrate
    QByteArray command;
    if (targetBaudrate == BAUDRATE_LOWSPEED) {
        command = CMD_SET_PARA_CFG_PREFIX_9600;
        qCDebug(log_core_serial_config) << "  - Using 9600 baudrate configuration for USB setup";
    } else {
        command = CMD_SET_PARA_CFG_PREFIX_115200;
        qCDebug(log_core_serial_config) << "  - Using 115200 baudrate configuration for USB setup";
    }
    command[5] = mode;  // Set mode byte at index 5 (6th byte)
    qCDebug(log_core_serial_conn) << "  - Command byte 5 (mode) set to: 0x" << QString::number(mode, 16);

    QString VID = settings.value("serial/vid", "86 1A").toString();
    QString PID = settings.value("serial/pid", "29 E1").toString();
    QString enable = settings.value("serial/enableflag", "00").toString();
    qCDebug(log_core_serial_conn) << "  - VID: " << VID << ", PID: " << PID << ", enable: " << enable;

    QByteArray VIDbyte = GlobalSetting::instance().convertStringToByteArray(VID);
    QByteArray PIDbyte = GlobalSetting::instance().convertStringToByteArray(PID);
    QByteArray enableByte =  GlobalSetting::instance().convertStringToByteArray(enable);
    qCDebug(log_core_serial_conn) << "  - VIDbyte size: " << VIDbyte.size() << ", PIDbyte size: " << PIDbyte.size() << ", enableByte size: " << enableByte.size();

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
    
    qCDebug(log_core_serial_conn) << "  - Final command (without checksum): " << command.toHex(' ');
    
    bool commandSent = false;
    if (serialPort != nullptr && serialPort->isOpen()){
        qCDebug(log_core_serial_cmd) << "  - Calling sendSyncCommand()...";
        commandSent = true;
        QByteArray respon = sendSyncCommand(command, true); 
        qCDebug(log_core_serial_rx) << "  - sendSyncCommand completed, response size: " << respon.size() << ", data: " << respon.toHex(' ');
    } else {
        qCWarning(log_core_serial_conn) << "  - WARNING: serialPort is null or not open, command NOT sent!";
        qCWarning(log_core_serial_conn) << "  - serialPort=" << static_cast<void*>(serialPort);
        qCWarning(log_core_serial_conn) << "  - isOpen=" << (serialPort && serialPort->isOpen());
    }
    qCDebug(log_core_serial_usbswitch) << "================== setUSBconfiguration END (commandSent=" << commandSent << ") ==================";
}

/*
 * change USB Descriptor of the device
 */
void SerialPortManager::changeUSBDescriptor() {
    qCDebug(log_core_serial_usbswitch) << "================== changeUSBDescriptor START ==================";
    qCDebug(log_core_serial_usbswitch) << "changeUSBDescriptor called";
    qCDebug(log_core_serial_conn) << "  - serialPort=" << static_cast<void*>(serialPort);
    qCDebug(log_core_serial_conn) << "  - isOpen=" << (serialPort && serialPort->isOpen());
    
    QSettings settings("Techxartisan", "Openterface");
    
    QString USBDescriptors[3];
    USBDescriptors[0] = settings.value("serial/customVIDDescriptor", "www.openterface.com").toString(); // 00
    USBDescriptors[1] = settings.value("serial/customPIDDescriptor", "test").toString(); // 01
    USBDescriptors[2] = settings.value("serial/serialnumber", "1").toString(); //02
    QString enableflag = settings.value("serial/enableflag", "00").toString();
    bool bits[4];

    bool ok;    
    int hexValue = enableflag.toInt(&ok, 16);

    qCDebug(log_core_serial_conn) << "  - enableflag hex value: " << hexValue;

    if (!ok) {
        qCWarning(log_core_serial_conn) << "  - WARNING: convert enableflag failed";
        qCDebug(log_core_serial_usbswitch) << "================== changeUSBDescriptor END (FAILED) ==================";
        return; // return empty array
    }
    
    bits[0] = (hexValue >> 0) & 1;
    bits[1] = (hexValue >> 1) & 1;
    bits[2] = (hexValue >> 2) & 1;
    bits[3] = (hexValue >> 7) & 1;
    
    qCDebug(log_core_serial_conn) << "  - enableflag bits: [" << bits[0] << ", " << bits[1] << ", " << bits[2] << ", " << bits[3] << "]";
    
    if (bits[3]){
        qCDebug(log_core_serial_usbswitch) << "  - USB descriptor customization is ENABLED, processing descriptors...";
        for(uint i=0; i < sizeof(bits)/ sizeof(bits[0]) -1; i++){
            if (bits[i]){
                qCDebug(log_core_serial_conn) << "  - Processing descriptor " << i << ": " << USBDescriptors[i];
                QByteArray command = CMD_SET_USB_STRING_PREFIX;
                QByteArray tmp = USBDescriptors[i].toUtf8();
                int descriptor_size = tmp.length();
                qCDebug(log_core_serial_conn) << "    - descriptor_size: " << descriptor_size;
                
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

                qCDebug(log_core_serial_conn) << "    - Final command: " << command.toHex(' ');
                
                if (serialPort != nullptr && serialPort->isOpen()){
                    qCDebug(log_core_serial_cmd) << "    - Calling sendSyncCommand()...";
                    QByteArray respon = sendSyncCommand(command, true);
                    qCDebug(log_core_serial_rx) << "    - sendSyncCommand completed, response size: " << respon.size() << ", data: " << respon.toHex(' ');
                } else {
                    qCWarning(log_core_serial_conn) << "    - WARNING: serialPort is null or not open, descriptor " << i << " NOT sent!";
                }
                qCDebug(log_core_serial_conn) << "    - Descriptor " << i << " processed";
            } else {
                qCDebug(log_core_serial_conn) << "  - Descriptor " << i << " is DISABLED, skipping";
            }
        }
        qCDebug(log_core_serial_conn) << "  - All descriptors processed";
    } else {
        qCDebug(log_core_serial_usbswitch) << "  - USB descriptor customization is DISABLED (bits[3]=false), skipping";
    }
    qCDebug(log_core_serial_usbswitch) << "================== changeUSBDescriptor END ==================";
}

void SerialPortManager::sendCommand(const QByteArray &command, bool waitForAck) {
    Q_UNUSED(waitForAck);
    // qCDebug(log_core_serial_tx)  << "sendCommand:" << command.toHex(' ');
    sendAsyncCommand(command, false);

}

bool SerialPortManager::setBaudRate(int baudRate) {
    if (!serialPort) {
        qCWarning(log_core_serial_conn) << "Cannot set baud rate: serialPort is null";
        return false;
    }

    // If called from a different thread, route through worker thread via queued connection
    if (QThread::currentThread() != m_serialWorkerThread) {
        qCDebug(log_core_serial_config) << "setBaudRate called from different thread, routing through worker thread";
        // FIXED: Use non-blocking call to avoid deadlock
        // Caller should not depend on immediate return value when calling from different thread
        QMetaObject::invokeMethod(this, [this, baudRate]() {
            // Call the actual baudrate setting logic
            setBaudRateInternal(baudRate);
        }, Qt::QueuedConnection);
        qCDebug(log_core_serial_config) << "setBaudRate request queued for worker thread";
        return true; // Return optimistic result as operation is queued
    }
    
    // Already in worker thread, proceed directly
    return setBaudRateInternal(baudRate);
}

bool SerialPortManager::setBaudRateInternal(int baudRate) {
    if (!isSerialPortValid()) {
        qCWarning(log_core_serial_conn) << "Cannot set baud rate - serial port not valid";
        return false;
    }

    QMutexLocker locker(&m_serialPortMutex);
    
    // Double-check after acquiring mutex
    if (!serialPort) {
        qCWarning(log_core_serial_conn) << "Cannot set baud rate: serialPort became null after mutex lock";
        return false;
    }

    if (serialPort->baudRate() == baudRate) {
        qCDebug(log_core_serial_config) << "Baud rate is already set to" << baudRate;
        // Keep state manager in sync
        if (m_stateManager) {
            m_stateManager->setBaudRate(baudRate);
        }
        return true;
    }

    qCDebug(log_core_serial_config) << "Setting baud rate to" << baudRate;

    // Suppress transient errors and pause periodic operations while changing baud
    m_baudChangeInProgress.store(true);

    // CRITICAL: Clear all buffers and pending data BEFORE changing baudrate
    // This prevents stale data from interfering with the baudrate transition
    if (serialPort && serialPort->isOpen()) {
        qCDebug(log_core_serial_conn) << "Clearing serial buffers before baud rate change";
        qCDebug(log_core_serial_conn) << "Buffer state before clear - bytesAvailable:" << serialPort->bytesAvailable()
                                 << "bytesToWrite:" << serialPort->bytesToWrite();
        serialPort->clear(QSerialPort::AllDirections);
        serialPort->waitForBytesWritten(500);  // Wait for any pending writes to complete
        qCDebug(log_core_serial_conn) << "Buffer state after clear - bytesAvailable:" << serialPort->bytesAvailable()
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
        qCDebug(log_core_serial_config) << "Baud rate successfully set to" << baudRate;
        // Update state manager so getCurrentBaudrate() reflects actual host setting
        if (m_stateManager) {
            m_stateManager->setBaudRate(baudRate);
        }
        emit connectedPortChanged(serialPort->portName(), baudRate);
    } else {
        qCWarning(log_core_serial_config) << "Failed to set baud rate to" << baudRate << ": " << serialPort->errorString();
    }

    // Allow stabilization time before re-enabling timers and watchdog; clear error counters to avoid immediate recovery
    // Use shorter stabilization time (1500ms) since we cleared buffers upfront
    QTimer::singleShot(1500, this, [this]() {
        // Clear any transient error records accumulated during transition
        resetErrorCounters();

        // Re-clear buffers after stabilization to ensure no stale data remains
        if (serialPort && serialPort->isOpen()) {
            qCDebug(log_core_serial_conn) << "Clearing buffers after stabilization period";
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
                qCDebug(log_core_serial_config) << "GET_INFO timer restarted after baud change";
            } else {
                qCDebug(log_core_serial_config) << "GET_INFO timer suppressed due to diagnostics dialog active";
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
        qCDebug(log_core_serial_config) << "Baud change stabilization complete";
    });

    return setResult;
} 

void SerialPortManager::setUserSelectedBaudrate(int baudRate) {
    qCDebug(log_core_serial_config) << "User manually selected baudrate:" << baudRate;
    
    // If we already know the chip type, prefer that check rather than re-detecting using VID/PID
    if (serialPort && serialPort->isOpen() && isChipTypeCH32V208()) {
        if (baudRate != BAUDRATE_HIGHSPEED) {
            qCWarning(log_core_serial_config) << "CH32V208 chip only supports 115200 baudrate. Ignoring user request for" << baudRate;
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
        qCInfo(log_core_serial_conn) << "CH32V208 chip - using simple close/reopen (baudrate must be 115200)";
        QString portName = serialPort->portName();
        closePort();
        
        // Use non-blocking timer instead of msleep
        QTimer::singleShot(100, this, [this, portName]() {
            if (openPort(portName, BAUDRATE_HIGHSPEED)) {
                qCInfo(log_core_serial_config) << "CH32V208 chip successfully switched to 115200";
                emit serialPortConnected(portName);
            } else {
                qCWarning(log_core_serial_config) << "Failed to reopen CH32V208 chip";
            }
        });
        return;
    }
    
    // Handle CH9329 chip - use commands
    if (isChipTypeCH9329()) {
        qCInfo(log_core_serial_config) << "CH9329 chip - using command-based baudrate change";
        applyCommandBasedBaudrateChange(baudRate, "CH9329 chip: User selected baudrate");
        return;
    }
    
    // Unknown chip - try CH9329 approach as fallback
    qCWarning(log_core_serial_config) << "Unknown chip type - attempting CH9329 approach";
    applyCommandBasedBaudrateChange(baudRate, "User selected baudrate");
}

void SerialPortManager::clearStoredBaudrate() {
    qCDebug(log_core_serial_config) << "Clearing stored baudrate setting";
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
            
            qCDebug(log_core_serial_config) << "Detected VID:PID =" << vid << ":" << pid << "for port" << portName;
            
            uint32_t detectedVidPid = (vid.toUInt(nullptr, 16) << 16) | pid.toUInt(nullptr, 16);
            
            if (detectedVidPid == static_cast<uint32_t>(ChipType::CH9329)) {
                qCInfo(log_core_serial_config) << "Detected CH9329 chip - supports 9600 and 115200 with command-based configuration";
                return ChipType::CH9329;
            } else if (detectedVidPid == static_cast<uint32_t>(ChipType::CH32V208)) {
                qCInfo(log_core_serial_config) << "Detected CH32V208 chip - only supports 115200 baudrate, no command-based configuration";
                return ChipType::CH32V208;
            }
            
            break;
        }
    }
    
    qCWarning(log_core_serial_config) << "Unknown chip type for port" << portName;
    return ChipType::UNKNOWN;
}

// Check if CH340 driver is installed (required for CH9329 chip)
bool SerialPortManager::checkCH340DriverInstalled() {
#ifdef _WIN32
    // Step 1: Check if CH340 COM port exists (indicates driver is working)
    bool ch340ComPortFound = false;
    const auto ports = QSerialPortInfo::availablePorts();
    for (const QSerialPortInfo& port : ports) {
        if (port.vendorIdentifier() == 0x1A86 && port.productIdentifier() == 0x7523) {
            ch340ComPortFound = true;
            qCDebug(log_core_serial_config) << "CH340 COM port found:" << port.portName();
            break;
        }
    }

    if (ch340ComPortFound) {
        qCDebug(log_core_serial_config) << "CH340 driver is installed (COM port found)";
        return true; // COM port found → driver is working
    }

    // Step 2: No COM port found. Check if CH9329 or CH32 USB device is physically present
    // Use DIGCF_ALLCLASSES to enumerate ALL devices including those without drivers
    bool ch9329UsbDeviceFound = false;
    bool ch32UsbDeviceFound = false;
    bool captureCardUsbDeviceFound = false;
    HDEVINFO deviceInfoSet = SetupDiGetClassDevsW(NULL, NULL, NULL, DIGCF_PRESENT | DIGCF_ALLCLASSES);
    if (deviceInfoSet == INVALID_HANDLE_VALUE) {
        qCWarning(log_core_serial_config) << "SetupDiGetClassDevs failed:" << GetLastError();
        return true; // Can't enumerate, assume OK
    }

    SP_DEVINFO_DATA deviceInfoData;
    deviceInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
    WCHAR hwIdBuffer[256];

    for (DWORD i = 0; SetupDiEnumDeviceInfo(deviceInfoSet, i, &deviceInfoData); i++) {
        if (SetupDiGetDeviceRegistryPropertyW(deviceInfoSet, &deviceInfoData, SPDRP_HARDWAREID, NULL,
            (PBYTE)hwIdBuffer, sizeof(hwIdBuffer), NULL)) {
            if (wcsstr(hwIdBuffer, L"VID_1A86") != NULL && wcsstr(hwIdBuffer, L"PID_7523") != NULL) {
                ch9329UsbDeviceFound = true;
                qCWarning(log_core_serial_config) << "CH9329 USB device found (no COM port):" << QString::fromWCharArray(hwIdBuffer);
            }
            if (wcsstr(hwIdBuffer, L"VID_1A86") != NULL && wcsstr(hwIdBuffer, L"PID_FE0C") != NULL) {
                ch32UsbDeviceFound = true;
                qCDebug(log_core_serial_config) << "CH32V208 USB device found (does not need CH340 driver):" << QString::fromWCharArray(hwIdBuffer);
            }
            if (wcsstr(hwIdBuffer, L"VID_534D") != NULL && wcsstr(hwIdBuffer, L"PID_2109") != NULL) {
                captureCardUsbDeviceFound = true;
            }
            if (wcsstr(hwIdBuffer, L"VID_345F") != NULL && (wcsstr(hwIdBuffer, L"PID_2109") != NULL || wcsstr(hwIdBuffer, L"PID_2132") != NULL)) {
                captureCardUsbDeviceFound = true;
            }
        }
    }

    SetupDiDestroyDeviceInfoList(deviceInfoSet);

    qCWarning(log_core_serial_config) << "Driver check result: CH9329=" << ch9329UsbDeviceFound
                                     << "CH32=" << ch32UsbDeviceFound
                                     << "capture card=" << captureCardUsbDeviceFound
                                     << "CH340 COM port=" << ch340ComPortFound;

    // CH32V208 found — does not need CH340 driver, skip the check
    if (ch32UsbDeviceFound) {
        qCDebug(log_core_serial_config) << "CH32V208 USB device found — CH340 driver check not needed";
        return true;
    }

    // CH9329 USB device physically present but no COM port → driver missing
    if (ch9329UsbDeviceFound) {
        qCWarning(log_core_serial_config) << "CH9329 USB device present but no COM port → CH340 driver missing";
        return false;
    }

    // No CH9329 or CH32 USB device found, but capture card present
    // This means the composite USB device might not be exposing CH9329 separately
    // Infer: capture card present → CH9329 should be present → driver likely missing
    if (captureCardUsbDeviceFound) {
        qCWarning(log_core_serial_config) << "Openterface capture card found but CH9329/CH32 not found → CH340 driver likely missing";
        return false;
    }

    // No relevant device found at all → nothing to check
    qCDebug(log_core_serial_config) << "No Openterface device found, skipping driver check";
    return true;
#elif defined(__linux__)
    // Step 1: Check if any Openterface USB device is connected
    // Only check driver if CH9329 device is actually present
    bool ch9329DeviceFound = false;
    bool ch32DeviceFound = false;
    QDir usbDir("/sys/bus/usb/devices");
    if (usbDir.exists()) {
        const QStringList entries = usbDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &entry : entries) {
            QString vendorPath = "/sys/bus/usb/devices/" + entry + "/idVendor";
            QString productPath = "/sys/bus/usb/devices/" + entry + "/idProduct";
            QFile vendorFile(vendorPath);
            QFile productFile(productPath);
            if (vendorFile.open(QIODevice::ReadOnly) && productFile.open(QIODevice::ReadOnly)) {
                QString vid = QString::fromUtf8(vendorFile.readLine()).trimmed().toUpper();
                QString pid = QString::fromUtf8(productFile.readLine()).trimmed().toUpper();
                // CH9329: 1a86:7523 — needs CH340 driver
                if (vid == "1A86" && pid == "7523") ch9329DeviceFound = true;
                // CH32V208: 1a86:fe0c — does NOT need CH340 driver
                if (vid == "1A86" && pid == "FE0C") ch32DeviceFound = true;
            }
        }
    }

    // CH32V208 found — does not need CH340 driver, skip the check
    if (ch32DeviceFound) {
        qCDebug(log_core_serial_config) << "CH32V208 USB device found on Linux — CH340 driver check not needed";
        return true;
    }

    if (!ch9329DeviceFound) {
        qCDebug(log_core_serial_config) << "No CH9329 USB device found on Linux, skipping driver check";
        return true; // No CH9329 device → nothing to check
    }

    // Step 2: CH9329 device found — check if ch341 driver module is loaded
    std::string command = "cat /proc/modules | grep 'ch341'";
    int result = system(command.c_str());
    if (result == 0) {
        return true; // Driver found
    }
    qCWarning(log_core_serial_config) << "CH9329 device found but ch341 driver not loaded on Linux";
    return false; // Driver not found
#else
    return true; // Assume installed for other platforms
#endif
}

// Check if CH9329 USB device is present but CH340 driver is missing
// Uses USB device enumeration (not serial port), so it works even when driver is not installed
bool SerialPortManager::isCH9329PresentAndDriverMissing() {
    return !checkCH340DriverInstalled();
}

// ARM architecture detection and performance prompt
bool SerialPortManager::isArmArchitecture() {
    QString architecture = QSysInfo::currentCpuArchitecture();
    qCDebug(log_core_serial_config) << "Current CPU architecture:" << architecture;
    
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
        qCDebug(log_core_serial_config) << "ARM baudrate performance prompt is disabled by user";
        return;
    }
    
    qCInfo(log_core_serial_config) << "ARM architecture detected with 115200 baudrate - emitting performance recommendation signal";
    
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
    qCDebug(log_core_serial_hotplug) << "Connecting SerialPortManager to hotplug monitor via SerialHotplugHandler";
    
    if (!m_hotplugHandler) {
        qCWarning(log_core_serial_hotplug) << "No SerialHotplugHandler available to connect";
        return;
    }

    m_hotplugHandler->ConnectToHotplugMonitor();

    qCDebug(log_core_serial_hotplug) << "SerialPortManager successfully connected to hotplug monitor";
}

void SerialPortManager::disconnectFromHotplugMonitor()
{
    qCDebug(log_core_serial_hotplug) << "Disconnecting SerialPortManager from hotplug monitor via SerialHotplugHandler";

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
    qCDebug(log_core_serial_watchdog) << "Auto recovery" << (enable ? "enabled" : "disabled");
}

void SerialPortManager::setMaxRetryAttempts(int maxRetries)
{
    m_maxRetryAttempts = qMax(1, maxRetries);
    if (m_watchdog) {
        m_watchdog->setMaxRetryAttempts(m_maxRetryAttempts);
    }
    qCDebug(log_core_serial_watchdog) << "Max retry attempts set to:" << m_maxRetryAttempts;
}

void SerialPortManager::setMaxConsecutiveErrors(int maxErrors)
{
    m_maxConsecutiveErrors = qMax(1, maxErrors);
    if (m_watchdog) {
        m_watchdog->setMaxConsecutiveErrors(m_maxConsecutiveErrors);
    }
    qCDebug(log_core_serial_watchdog) << "Max consecutive errors set to:" << m_maxConsecutiveErrors;
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
    qCInfo(log_core_serial_watchdog) << "Force recovery requested";
    if (m_watchdog) {
        m_watchdog->forceRecovery();
    }
}


void SerialPortManager::handleSerialError(QSerialPort::SerialPortError error)
{
    // Critical: Check serialPort validity first to prevent crashes
    if (!serialPort) {
        qCWarning(log_core_serial_conn) << "Serial error occurred but serialPort instance is null! Error code:" << static_cast<int>(error);
        return;
    }

    // Fatal error guard: once we've handled a fatal error (ResourceError) and closed
    // the port, ignore all subsequent errors. This prevents a flood of millions of
    // error signals from a broken USB device from overwhelming the event loop and
    // preventing the main thread from processing recovery signals (e.g., serialRecoveryFailed).
    if (m_fatalErrorHandled.load()) {
        return;
    }

    QString errorString = serialPort->errorString();

    // If we're performing a controlled baud-rate change, suppress transient errors
    if (m_baudChangeInProgress.load()) {
        qCDebug(log_core_serial_config) << "Transient serial error during baud change suppressed:" << errorString << "Error code:" << static_cast<int>(error);
        return;
    }

    // Suppress all errors during RTS recovery (hardware reset generates expected errors)
    if (m_rtsRecoveryInProgress.load()) {
        qCDebug(log_core_serial_conn) << "Serial error suppressed during RTS recovery:" << errorString << "Code:" << static_cast<int>(error);
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
        qCDebug(log_core_serial_conn) << "Error throttled:" << errorString << "Code:" << static_cast<int>(error);
        return;
    }
    m_lastErrorLogTime.restart();

    qCWarning(log_core_serial_conn) << "Serial port error occurred:" << errorString << "Error code:" << static_cast<int>(error);

    // Error classification for device failure recovery:
    //
    // Error code 8 (UnknownError on Windows): "A device attached to the system is not functioning"
    //   → The CH32V208 chip is present on USB but in a bad state. This typically happens when the
    //     target computer restarts and the chip's target-side USB or internal state becomes corrupted.
    //     The chip stays enumerated on the host side but can no longer communicate properly.
    //     We treat this as CRITICAL — it's the precursor to error code 6.
    //
    // Error code 6 (ResourceError on Windows): "The I/O operation has been aborted"
    //   → The serial port is completely broken. If the device is still physically present on USB,
    //     this means the CH32V208 needs an RTS hardware reset to recover (not just close/reopen).
    //     If the device is NOT present, it was physically unplugged — use standard unplug recovery.

    bool isResourceError = (error == QSerialPort::ResourceError);
    bool isUnknownDeviceError = (error == QSerialPort::UnknownError &&
                                  errorString.contains("not functioning"));
    bool isAccessDenied = errorString.contains("设备不识别此命令") ||
                          errorString.contains("拒绝访问") ||
                          errorString.contains("Access is denied");

    if (isResourceError || isAccessDenied) {
        // Device may have been physically unplugged OR may be present but unresponsive.
        // Check if the device is still on the USB bus to decide recovery strategy.
        bool devicePresent = false;
        const QList<QSerialPortInfo> ports = QSerialPortInfo::availablePorts();
        for (const QSerialPortInfo &info : ports) {
            if (info.portName() == serialPort->portName()) {
                devicePresent = true;
                break;
            }
        }

        if (devicePresent && isChipTypeCH32V208()) {
            // Device IS present on USB but serial port is broken.
            // This means CH32V208 entered a bad state (e.g., after target restart).
            // Simple close/reopen won't fix it — need RTS hardware reset.
            qCInfo(log_core_serial_conn) << "Device present but unresponsive (error"
                                          << static_cast<int>(error)
                                          << ") — triggering RTS hardware reset recovery";

            // Transition to ERROR_STATE to block new open attempts during recovery
            SerialPortState previousState = m_portState.exchange(SerialPortState::ERROR_STATE);
            qCWarning(log_core_serial_conn) << "Port state transition:" << static_cast<int>(previousState) << "-> ERROR_STATE (RTS recovery)";

            // NOTE: Do NOT set m_deviceUnpluggedDetected — the device IS present.
            // Setting it would block recovery because openPort checks this flag.

            // Stop periodic timers to prevent interference during recovery
            if (m_usbStatusCheckTimer && m_usbStatusCheckTimer->isActive()) {
                m_usbStatusCheckTimer->stop();
            }
            if (m_getInfoTimer && m_getInfoTimer->isActive()) {
                m_getInfoTimer->stop();
            }

            // Trigger RTS hardware reset to recover the CH32V208 chip
            triggerRtsRecoveryForUnresponsiveDevice();
        } else {
            // Device is physically gone — standard unplug recovery
            qCInfo(log_core_serial_conn) << "Device disconnection error detected, transitioning to ERROR_STATE";

            SerialPortState previousState = m_portState.exchange(SerialPortState::ERROR_STATE);
            qCWarning(log_core_serial_conn) << "Port state transition:" << static_cast<int>(previousState) << "-> ERROR_STATE";

            m_deviceUnpluggedDetected.store(true);

            // CRITICAL: Mark fatal error as handled IMMEDIATELY to prevent re-entry.
            // When a USB device fails with error code 6, QSerialPort can fire error signals
            // at an enormous rate (~73,000/sec). Without this guard, handleSerialError would
            // be called millions of times, flooding the log and overwhelming the event loop,
            // preventing the main thread from processing recovery signals like serialRecoveryFailed().
            m_fatalErrorHandled.store(true);

            // Stop periodic timers
            if (m_usbStatusCheckTimer && m_usbStatusCheckTimer->isActive()) {
                m_usbStatusCheckTimer->stop();
                qCDebug(log_core_serial_config) << "USB status check timer stopped due to device error";
            }
            if (m_getInfoTimer && m_getInfoTimer->isActive()) {
                m_getInfoTimer->stop();
                qCDebug(log_core_serial_config) << "GET_INFO timer stopped due to device error";
            }

            // On Linux, after target PC restart the CH32V208 may fail to re-enumerate (USB error -71).
            // The sysfs entry may persist as stale or disappear, but HotplugMonitor may not detect
            // the removal. Emit serialRecoveryFailed() so DeviceLifecycleManager can trigger
            // USB hub port reset as a last-resort recovery. UsbPortResetter checks if the composite
            // device is still present before attempting reset.
#ifdef __linux__
            if (isChipTypeCH32V208()) {
                qCWarning(log_core_serial_conn) << "CH32V208 serial device disappeared from availablePorts()"
                                                 << "— emitting serialRecoveryFailed() for USB hub port reset";
                emit serialRecoveryFailed();
            }
#endif

            // CRITICAL: Close the serial port IMMEDIATELY to stop the error flood.
            // After a fatal disconnect (error 6, device gone), QSerialPort keeps firing
            // error signals because the port is still "open" in a broken state. Each error
            // signal triggers handleSerialError, generating millions of log messages that
            // overwhelm the event loop. Closing the port disconnects the error signal and
            // releases the OS handle, stopping the flood at its source.
            // This is essential for the serialRecoveryFailed() signal (queued to main thread)
            // to be processed in a timely manner.
            qCInfo(log_core_serial_conn) << "Closing broken serial port immediately to stop error flood";
            closePortInternal();
        }
    } else if (isUnknownDeviceError) {
        // Error code 8: "A device attached to the system is not functioning"
        // The device is present but malfunctioning. This is critical — it often
        // precedes error code 6 (ResourceError). Mark state but don't trigger
        // recovery yet; recovery will be triggered by the subsequent error 6.
        qCWarning(log_core_serial_conn) << "Device present but malfunctioning (error code 8) —"
                                          << "awaiting ResourceError to trigger RTS recovery";
        // Don't change port state — error 6 will follow and trigger recovery
    } else {
        // Other transient errors (UnknownError, TimeoutError, etc.)
        qCDebug(log_core_serial_conn) << "Transient serial error logged but state machine not changed:" << errorString;
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

void SerialPortManager::triggerRtsRecoveryForUnresponsiveDevice()
{
    // Guard against concurrent recovery attempts
    bool expected = false;
    if (!m_rtsRecoveryInProgress.compare_exchange_strong(expected, true)) {
        qCDebug(log_core_serial_conn) << "RTS recovery already in progress, skipping duplicate trigger";
        return;
    }

    qCInfo(log_core_serial_conn) << "Triggering RTS hardware reset to recover unresponsive CH32V208";

    // Ensure this runs in the worker thread (where serialPort lives)
    if (QThread::currentThread() != this->thread()) {
        qCDebug(log_core_serial_conn) << "RTS recovery: not in worker thread, dispatching";
        QMetaObject::invokeMethod(this, [this]() {
            triggerRtsRecoveryForUnresponsiveDevice();
        }, Qt::QueuedConnection);
        return;
    }

    // Stop watchdog to prevent it from triggering competing recovery
    if (m_watchdog) {
        m_watchdog->stop();
    }

    // Perform RTS hardware reset via factoryResetHipChip().
    // Since we're in the worker thread, this calls handleFactoryResetInternal() directly.
    // The sequence is: RTS low (4s) → RTS high (500ms) → close port (2s) → reopen → init.
    // This fully resets the CH32V208 chip including its target-side USB HID and video interfaces.
    qCInfo(log_core_serial_conn) << "Starting RTS reset sequence for unresponsive device recovery";
    bool resetResult = factoryResetHipChip();

    if (!resetResult) {
        qCWarning(log_core_serial_conn) << "RTS reset initiation failed — clearing recovery flag";
        m_rtsRecoveryInProgress.store(false);
        // Restart watchdog so it can attempt recovery through its own mechanism
        if (m_watchdog && !m_watchdog->isRunning()) {
            m_watchdog->start();
        }
        // RTS recovery failed — the device is likely not truly present on the USB bus
        // (e.g., CH32V208 failed to enumerate after target restart, USB error -71).
        // Emit signal so DeviceLifecycleManager can trigger USB hub port reset as fallback.
#ifdef __linux__
        qCWarning(log_core_serial_conn) << "RTS recovery failed on Linux — emitting serialRecoveryFailed()"
                                         << "to trigger USB hub port reset";
        emit serialRecoveryFailed();
#endif
        return;
    }

    // Safety timeout: if recovery doesn't complete within 20 seconds, clear the flag
    // and restart the watchdog. The factoryResetHipChip sequence takes ~6.5 seconds,
    // so 20 seconds provides ample margin. If the device still doesn't work after that,
    // the next error cascade will trigger another RTS reset attempt.
    QTimer::singleShot(20000, this, [this]() {
        if (m_rtsRecoveryInProgress.load()) {
            qCWarning(log_core_serial_conn) << "RTS recovery safety timeout — clearing flag and restarting watchdog";
            m_rtsRecoveryInProgress.store(false);
            if (m_watchdog && !m_watchdog->isRunning()) {
                m_watchdog->start();
            }
        }
    });
}

void SerialPortManager::attemptRecovery()
{
    if (m_isShuttingDown || !m_autoRecoveryEnabled) {
        return;
    }
    
    qCInfo(log_core_serial_watchdog) << "attemptRecovery called - delegating to ConnectionWatchdog";
    
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
    qCDebug(log_core_serial_watchdog) << "Error counters reset";
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
        qCDebug(log_core_serial_watchdog) << "setupConnectionWatchdog: legacy timer not created, using ConnectionWatchdog class";
        return;
    }
    
    // Only start legacy timer if new ConnectionWatchdog is not available
    if (m_watchdog) {
        qCDebug(log_core_serial_watchdog) << "setupConnectionWatchdog: ConnectionWatchdog is active, skipping legacy timer";
        return;
    }
    
    qCDebug(log_core_serial_watchdog) << "setupConnectionWatchdog: starting legacy fallback timer";
    m_connectionWatchdog->setInterval(30000); // 30 seconds
    disconnect(m_connectionWatchdog, &QTimer::timeout, nullptr, nullptr);
    
    connect(m_connectionWatchdog, &QTimer::timeout, this, [this]() {
        if (m_isShuttingDown) {
            return;
        }
        if (m_lastSuccessfulCommand.elapsed() > 30000) {
            qCWarning(log_core_serial_watchdog) << "Legacy watchdog triggered";
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
        qCInfo(log_core_serial_config) << logPrefix << "applied successfully:" << baudRate;
    } else {
        qCWarning(log_core_serial_config) << logPrefix << "Failed to apply user selected baudrate:" << baudRate;
    }
}

// ========== IRecoveryHandler Interface Implementation (Phase 3) ==========

bool SerialPortManager::performRecovery(int attempt)
{
    // Skip if RTS hardware reset recovery is already in progress.
    // RTS recovery (factoryResetHipChip) fully resets the CH32V208 chip and reopens
    // the port. The watchdog's close/reopen recovery would conflict with it.
    if (m_rtsRecoveryInProgress.load()) {
        qCInfo(log_core_serial_watchdog) << "Skipping watchdog recovery — RTS hardware reset in progress";
        return true;  // Return true to prevent watchdog from counting this as a failure
    }

    qCInfo(log_core_serial_watchdog) << "Performing recovery attempt" << attempt;
    
    // Record connection retry in statistics
    if (m_statistics) {
        m_statistics->recordConnectionRetry();
    }
    
    // Check if serialPort is null and log diagnostic information
    if (!serialPort) {
        qCWarning(log_core_serial_watchdog) << "Recovery attempt" << attempt << "- serialPort instance is null!";
        qCDebug(log_core_serial_conn) << "Current port path:" << m_currentSerialPortPath << "Current port chain:" << m_currentSerialPortChain;
    }
    
    QString currentPortPath = m_currentSerialPortPath;
    QString currentPortChain = m_currentSerialPortChain;
    
    if (currentPortPath.isEmpty() || currentPortChain.isEmpty()) {
        qCWarning(log_core_serial_watchdog) << "Cannot recover - no port chain information available";
        return false;
    }
    
    // Try to restart the current port.
    // switchSerialPortByPortChain initiates an async close/reopen cycle and returns true
    // when the async operation has been successfully started. The actual port-open result
    // arrives later through onSerialPortConnectionSuccess (which calls recordSuccess())
    // or handleSerialError (which calls recordError()). We must NOT check `ready` here
    // because it is false immediately after the async init — the port hasn't reopened yet.
    // Previously this function returned false in that case, causing the watchdog to believe
    // recovery failed and scheduling unnecessary retries.
    bool recoverySuccess = switchSerialPortByPortChain(currentPortChain);

    if (recoverySuccess) {
        qCInfo(log_core_serial_watchdog) << "Recovery attempt" << attempt
                                         << "initiated — async close/reopen started, waiting for port to open";
        return true;
    }

    qCWarning(log_core_serial_watchdog) << "Recovery attempt" << attempt
                                        << "failed to initiate switch, serialPort=" << static_cast<void*>(serialPort);
    return false;
}

void SerialPortManager::onRecoveryFailed()
{
    qCCritical(log_core_serial_watchdog) << "Recovery failed after all attempts";
    ready = false;
    // Sync the command coordinator ready state
    if (m_commandCoordinator) {
        m_commandCoordinator->setReady(false);
    }
    emit statusUpdate("Serial port recovery failed - max retries exceeded");
}

void SerialPortManager::onRecoverySuccess()
{
    qCInfo(log_core_serial_watchdog) << "Recovery completed successfully";
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
        
        qCDebug(log_core_serial_watchdog) << "Polling ready state, attempt" << *attemptCount << "of" << maxPollingAttempts << ", ready=" << ready;
        
        // Check if ready is true
        if (ready) {
            qCInfo(log_core_serial_watchdog) << "Factory reset reconnection successful, ready=true after" << *attemptCount << "polling attempts";
            emit factoryResetCompleted(true);
            return;
        }
        
        // Check if we've exceeded max attempts
        if (*attemptCount >= maxPollingAttempts) {
            qCWarning(log_core_serial_watchdog) << "Ready state polling timeout, attempting manual verification...";
            
            // Try one final manual verification
            if (serialPort && serialPort->isOpen()) {
                QByteArray verifyResponse = sendSyncCommand(CMD_GET_INFO, true);
                if (!verifyResponse.isEmpty()) {
                    ready = true;
                    resetErrorCounters();
                    m_lastSuccessfulCommand.restart();
                    
                    qCInfo(log_core_serial_config) << "Manual verification successful after factory reset";
                    emit serialPortConnectionSuccess(portName);
                    emit factoryResetCompleted(true);
                    return;
                }
            }
            
            qCWarning(log_core_serial_watchdog) << "Factory reset reconnection failed after all polling attempts";
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
    qCDebug(log_core_serial_cmd) << "Statistics tracking started";
}

void SerialPortManager::stopStats()
{
    if (m_statistics) {
        m_statistics->stopTracking();
    }
    if (m_commandCoordinator) {
        m_commandCoordinator->stopStats();
    }
    qCDebug(log_core_serial_cmd) << "Statistics tracking stopped";
}

void SerialPortManager::resetStats()
{
    if (m_statistics) {
        m_statistics->resetStatistics();
    }
    if (m_commandCoordinator) {
        m_commandCoordinator->resetStats();
    }
    qCDebug(log_core_serial_cmd) << "Statistics reset";
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
        // Enable debug logging for all serial sub-categories
        QLoggingCategory::setFilterRules("opf.core.serial.*.debug=true");
        qCDebug(log_core_serial_conn) << "Serial debug logging enabled for diagnostics";
    } else {
        // Disable debug logging for all serial sub-categories
        QLoggingCategory::setFilterRules("opf.core.serial.*.debug=false");
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
            qCWarning(log_core_serial_conn) << "SerialPort has empty port name - possible stale object";
            return false;
        }
    } catch (...) {
        qCWarning(log_core_serial_conn) << "Exception during serialPort validation - possible stale object";
        return false;
    }
    
    return true;
}

bool SerialPortManager::isKnownDevicePresent() const
{
    // Check if any device with known VID/PID (CH9329 or CH32V208) is present on the USB bus.
    // This is used as a fallback when port name matching fails, which can happen on Linux
    // when the device node name changes after re-enumeration (e.g., ttyACM0 -> ttyACM1).
    const QList<QSerialPortInfo> ports = QSerialPortInfo::availablePorts();
    for (const QSerialPortInfo &info : ports) {
        if (!info.hasVendorIdentifier() || !info.hasProductIdentifier()) {
            continue;
        }
        const quint16 vid = info.vendorIdentifier();
        const quint16 pid = info.productIdentifier();

        // CH9329: VID 0x1A86, PID 0x7523
        if (vid == 0x1A86 && pid == 0x7523) {
            qCDebug(log_core_serial_conn) << "Known device present: CH9329 at" << info.portName()
                                          << "(VID:PID =" << QString::number(vid, 16) << ":" << QString::number(pid, 16) << ")";
            return true;
        }
        // CH32V208: VID 0x1A86, PID 0xFE0C
        if (vid == 0x1A86 && pid == 0xFE0C) {
            qCDebug(log_core_serial_conn) << "Known device present: CH32V208 at" << info.portName()
                                          << "(VID:PID =" << QString::number(vid, 16) << ":" << QString::number(pid, 16) << ")";
            return true;
        }
    }
    return false;
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
            qCInfo(log_core_serial_cmd) << "Async Message Statistics:"
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
                        qCWarning(log_core_serial_cmd) << "Async message imbalance detected!"
                                                   << "Received/Sent ratio:" << QString::number(imbalanceRatio, 'f', 2)
                                                   << "(threshold:" << ASYNC_IMBALANCE_THRESHOLD << ")";
                    } else {
                        // Imbalance continues - check if we've exceeded the 3-second tolerance window
                        qint64 imbalanceDuration = m_imbalanceDetectionTimer.elapsed();
                        qCWarning(log_core_serial_cmd) << "Async message imbalance persisting for" << imbalanceDuration << "ms"
                                                   << "Received/Sent ratio:" << QString::number(imbalanceRatio, 'f', 2);
                        
                        // ===== STATE 2: TIMEOUT THRESHOLD EXCEEDED (3+ seconds) =====
                        if (imbalanceDuration >= ASYNC_IMBALANCE_TIMEOUT_MS) {
                            // Imbalance has persisted for 3+ seconds - device likely in bad state
                            // Action: Send reset command to device to recover
                            qCCritical(log_core_serial_cmd) << "Async message imbalance exceeded 3 seconds threshold!"
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
                        qCInfo(log_core_serial_cmd) << "Async message imbalance cleared."
                                               << "Received/Sent ratio:" << QString::number(imbalanceRatio, 'f', 2);
                        
                        // Clear imbalance flag and reset timer for next monitoring cycle
                        m_imbalanceDetected = false;
                        m_imbalanceDetectionTimer.restart();
                    }
                }
            }
        } else {
            // No activity in this window; be conservative and reset counter
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
    qCDebug(log_core_serial_config) << "Continuing initialization cycle" << (cycle+1) << "of" << cycles;
    
    if (cycle >= cycles) {
        qCWarning(log_core_serial_conn) << "All attempts exhausted: failed to open and validate serial port:" << portName;
        return;
    }
    
    qCDebug(log_core_serial_conn) << "Attempting to open port" << portName << "at baud" << baud << "(cycle" << (cycle+1) << "of" << cycles << ")";

    bool opened = openPort(portName, baud);
    if (!opened) {
        qCWarning(log_core_serial_conn) << "Failed to open serial port:" << portName << "at baud" << baud;
        // Use async delay instead of blocking
        QTimer::singleShot(1000 * (cycle+1), this, [this, portName, baud, cycle, cycles]() {
            continueInitializeWithBaudrates(portName, baud, cycle + 1, cycles);
        });
        return;
    }

    qCDebug(log_core_serial_conn) << "Serial port opened, validating with synchronous CMD_GET_INFO:" << portName << "baud" << baud;

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
            qCDebug(log_core_serial_config) << "Device validation successful at baud" << baud;
        }
    }

    if (!valid) {
        qCWarning(log_core_serial_conn) << "No valid CMD_GET_INFO response received after opening port" << portName << "at baud" << baud << "- closing and will try the next baud/attempt";
        if (serialPort && serialPort->isOpen()) {
            closePort();
        }

        // Use async delay instead of blocking
        QTimer::singleShot(300, this, [this, portName, baud, cycle, cycles]() {
            continueInitializeWithBaudrates(portName, baud, cycle + 1, cycles);
        });
        return;
    }

    qCDebug(log_core_serial_config) << "Successfully initialized and validated port" << portName << "at baud" << baud;
}

void SerialPortManager::continueOpenPortRetry(const QString &portName, qint32 baudRate, int attempt, int maxRetries) {
    if (attempt >= maxRetries) {
        qCWarning(log_core_serial_conn) << "Failed to open port after" << maxRetries << "attempts. Final error:" 
                                   << (serialPort ? serialPort->errorString() : "No port instance");
        return;
    }

    qCDebug(log_core_serial_watchdog) << "Retry attempt" << (attempt + 1) << "for port" << portName;
    
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
        qCDebug(log_core_serial_conn) << "Open port" << portName + ", baudrate: " << baudRate << "with read buffer size" << serialPort->readBufferSize();
        
        // Show existing buffer sizes before clearing them
        qCDebug(log_core_serial_conn) << "Serial buffer sizes before clear - bytesAvailable:" << serialPort->bytesAvailable()
                                 << "bytesToWrite:" << serialPort->bytesToWrite();

        // Clear any stale data in the serial port buffers
        qCDebug(log_core_serial_conn) << "Clearing serial port buffers to remove stale data";
        serialPort->clear();
        return; // Success - exit
    }

    // Failed to open, log error and continue with next retry
    qCWarning(log_core_serial_conn) << "Failed to open port on attempt" << (attempt + 1) 
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
        qCDebug(log_core_serial_conn) << "Abandoning async port retries due to shutdown";
        return;
    }
    
    if (cycle >= maxCycles) {
        qCWarning(log_core_serial_conn) << "All attempts exhausted: failed to open and validate serial port:" << portName;
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
    qCDebug(log_core_serial_conn) << "Attempting to open port" << portName << "at baud" << baud << "(cycle" << (cycle+1) << "of" << maxCycles << ")";

    // Simplified async approach to prevent complex nested operations
    bool opened = openPort(portName, baud);
    if (!opened) {
        qCWarning(log_core_serial_conn) << "Failed to open serial port:" << portName << "at baud" << baud;
        // Simple delay before next attempt
        QTimer::singleShot(1000, this, [this, portName, baudOrder, baudIndex, cycle, maxCycles]() {
            if (!m_isShuttingDown) {
                startAsyncPortRetries(portName, baudOrder, baudIndex + 1, cycle, maxCycles);
            }
        });
        return;
    }

    qCDebug(log_core_serial_conn) << "Serial port opened, validating with synchronous CMD_GET_INFO:" << portName << "baud" << baud;

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
        qCDebug(log_core_serial_conn) << "Validation abandoned due to shutdown or null serial port";
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
        qCDebug(log_core_serial_conn) << "Received valid CMD_GET_INFO response, open considered successful:" << portName << "baud" << baud;
        return; // Success!
    }

    qCWarning(log_core_serial_conn) << "No valid CMD_GET_INFO response received after opening port" << portName << "at baud" << baud << "- closing and will try the next baud/attempt";
    
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

/**
 * @brief Send NumLock toggle command to device
 * @return true if command was sent successfully, false otherwise
 */
bool SerialPortManager::toggleNumLock()
{
    // Build NumLock HID command
    // Format: Header(0x57 0xAB) | Address(0x00) | Cmd(0x02 = Send KB) | Length(0x08) | 
    //         Modifier(0x00) | Reserved(0x00) | KeyCode(0x53 = NumLock) | Padding...
    // Use protocol template and key mapping rather than hard-coded hex string
    QByteArray lockCmd = CMD_SEND_KB_GENERAL_DATA;
    uint8_t scancode = SCANCODE_NUMLOCK;
    if (lockCmd.size() >= 8) {
        lockCmd[5] = static_cast<char>(0x00); // modifier
        lockCmd[6] = static_cast<char>(0x00); // reserved
        lockCmd[7] = static_cast<char>(scancode);
    }

    bool success = sendAsyncCommand(lockCmd, false);
    
    if (success) {
        qCDebug(log_core_serial_lockkeys) << "✓ NumLock toggle command sent to device";
    } else {
        qCWarning(log_core_serial_lockkeys) << "✗ Failed to send NumLock toggle command";
    }
    
    return success;
}

/**
 * @brief Send CapsLock toggle command to device
 * @return true if command was sent successfully, false otherwise
 */
bool SerialPortManager::toggleCapsLock()
{
    // Build CapsLock HID command
    // Format: Header(0x57 0xAB) | Address(0x00) | Cmd(0x02 = Send KB) | Length(0x08) | 
    //         Modifier(0x00) | Reserved(0x00) | KeyCode(0x39 = CapsLock on US QWERTY) | Padding...
    // Use protocol template and key mapping rather than hard-coded hex string
    QByteArray lockCmd = CMD_SEND_KB_GENERAL_DATA;
    uint8_t scancode = SCANCODE_CAPSLOCK;
    if (lockCmd.size() >= 8) {
        lockCmd[5] = static_cast<char>(0x00); // modifier
        lockCmd[6] = static_cast<char>(0x00); // reserved
        lockCmd[7] = static_cast<char>(scancode);
    }

    bool success = sendAsyncCommand(lockCmd, false);
    
    if (success) {
        qCDebug(log_core_serial_lockkeys) << "✓ CapsLock toggle command sent to device";
    } else {
        qCWarning(log_core_serial_lockkeys) << "✗ Failed to send CapsLock toggle command";
    }
    
    return success;
}

/**
 * @brief Send ScrollLock toggle command to device
 * @return true if command was sent successfully, false otherwise
 */
bool SerialPortManager::toggleScrollLock()
{
    // Build ScrollLock HID command
    // Format: Header(0x57 0xAB) | Address(0x00) | Cmd(0x02 = Send KB) | Length(0x08) | 
    //         Modifier(0x00) | Reserved(0x00) | KeyCode(0x47 = ScrollLock) | Padding...
    // Use protocol template and key mapping rather than hard-coded hex string
    QByteArray lockCmd = CMD_SEND_KB_GENERAL_DATA;
    uint8_t scancode = SCANCODE_SCROLLLOCK;
    if (lockCmd.size() >= 8) {
        lockCmd[5] = static_cast<char>(0x00); // modifier
        lockCmd[6] = static_cast<char>(0x00); // reserved
        lockCmd[7] = static_cast<char>(scancode);
    }

    bool success = sendAsyncCommand(lockCmd, false);
    
    if (success) {
        qCDebug(log_core_serial_lockkeys) << "✓ ScrollLock toggle command sent to device";
    } else {
        qCWarning(log_core_serial_lockkeys) << "✗ Failed to send ScrollLock toggle command";
    }
    
    return success;
}

/*
 * Apply hardware setting in worker thread (thread-safe)
 * This slot is called in the worker thread context to safely access serialPort
 */
void SerialPortManager::applyHardwareSettingInternal(int baudrate, uint8_t mode, bool needFactoryReset)
{
    qCDebug(log_core_serial_config) << "================== applyHardwareSettingInternal START ==================";
    qCDebug(log_core_serial_config) << "applyHardwareSettingInternal called in thread:" << QThread::currentThread()->objectName();
    qCDebug(log_core_serial_config) << "Parameters: baudrate=" << baudrate << ", mode=0x" << QString::number(mode, 16) << ", needFactoryReset=" << needFactoryReset;
    
    // Check if we're in the correct thread
    if (QThread::currentThread() != this->thread()) {
        qCWarning(log_core_serial_config) << "applyHardwareSettingInternal called from WRONG THREAD!";
        qCWarning(log_core_serial_conn) << "Current thread:" << QThread::currentThread() << " Expected:" << this->thread();
        return;
    }
    qCDebug(log_core_serial_config) << "✓ Thread check passed - running in worker thread";
    
    // Log serial port state
    qCDebug(log_core_serial_conn) << "Serial port state: serialPort=" << static_cast<void*>(serialPort)
                             << ", isOpen=" << (serialPort && serialPort->isOpen())
                             << ", portName=" << (serialPort && serialPort->isOpen() ? serialPort->portName() : "N/A")
                             << ", baudRate=" << (serialPort && serialPort->isOpen() ? QString::number(serialPort->baudRate()) : "N/A");
    
    // Step 1: Apply USB configuration
    qCDebug(log_core_serial_usbswitch) << "Step 1: Calling setUSBconfiguration()...";
    if (serialPort && serialPort->isOpen()) {
        qCDebug(log_core_serial_usbswitch) << "  - Calling setUSBconfiguration with baudrate=" << baudrate;
        setUSBconfiguration(baudrate);
        qCDebug(log_core_serial_usbswitch) << "  - setUSBconfiguration completed";
    } else {
        qCDebug(log_core_serial_conn) << "Step 1 SKIPPED: Serial port not open, skipping USB configuration";
    }
    qCDebug(log_core_serial_usbswitch) << "Step 1 COMPLETED: setUSBconfiguration (or skipped)";
    
    // Step 2: Apply USB descriptor changes
    qCDebug(log_core_serial_usbswitch) << "Step 2: Calling changeUSBDescriptor()...";
    qCDebug(log_core_serial_usbswitch) << "  - Before changeUSBDescriptor: serialPort=" << static_cast<void*>(serialPort);
    changeUSBDescriptor();
    qCDebug(log_core_serial_usbswitch) << "  - After changeUSBDescriptor: serialPort=" << static_cast<void*>(serialPort);
    qCDebug(log_core_serial_usbswitch) << "Step 2 COMPLETED: changeUSBDescriptor";
    
    // Step 3: Perform factory reset if mode changed
    if (needFactoryReset) {
        qCDebug(log_core_serial_config) << "Step 3: Operating mode changed, performing factory reset...";
        
        if (serialPort && serialPort->isOpen()) {
            qCDebug(log_core_serial_config) << "  - Calling handleFactoryResetInternal...";
            bool factoryResetResult = handleFactoryResetInternal();
            qCDebug(log_core_serial_config) << "  - handleFactoryResetInternal completed with result=" << factoryResetResult;
        } else {
            qCWarning(log_core_serial_conn) << "Step 3 SKIPPED: Serial port not open, cannot perform factory reset";
            qCWarning(log_core_serial_conn) << "  - serialPort=" << static_cast<void*>(serialPort)
                                      << ", isOpen=" << (serialPort && serialPort->isOpen());
        }
        qCDebug(log_core_serial_config) << "Step 3 COMPLETED: factory reset (or skipped)";
    } else {
        qCDebug(log_core_serial_config) << "Step 3 SKIPPED: Mode not changed, no factory reset needed";
    }
    
    qCDebug(log_core_serial_config) << "================== applyHardwareSettingInternal END ==================";
}

// Hotplug recovery: Schedule a delayed retry when CH9329 initialization fails
void SerialPortManager::scheduleInitRetry(const QString& portName, int baudrate)
{
    m_initRetryCount++;
    m_pendingInitPortName = portName;
    m_pendingInitBaudrate = baudrate;

    qCWarning(log_core_serial_watchdog) << "CH9329 initialization failed, scheduling delayed retry"
                               << m_initRetryCount << "of" << MAX_INIT_RETRIES
                               << "after 500ms delay (giving USB stack time to stabilize)";

    // Use a timer to delay the retry, giving the USB stack time to stabilize after rapid hotplug
    QTimer::singleShot(500, this, [this]() {
        attemptInitRetry();
    });
}

// Hotplug recovery: Attempt the delayed initialization retry
void SerialPortManager::attemptInitRetry()
{
    if (m_pendingInitPortName.isEmpty()) {
        qCDebug(log_core_serial_config) << "attemptInitRetry: No pending port name, skipping";
        return;
    }

    // Check if we're in the worker thread
    if (QThread::currentThread() != m_serialWorkerThread) {
        QMetaObject::invokeMethod(this, [this]() {
            attemptInitRetry();
        }, Qt::QueuedConnection);
        return;
    }

    qCInfo(log_core_serial_config) << "Attempting CH9329 initialization retry"
                            << m_initRetryCount << "of" << MAX_INIT_RETRIES
                            << "for port" << m_pendingInitPortName;

    // Check if the device is still available at this port
    DeviceManager& deviceManager = DeviceManager::getInstance();
    QList<DeviceInfo> devices = deviceManager.getDevicesByPortChain(m_currentSerialPortChain);
    bool deviceStillAvailable = false;
    for (const DeviceInfo& device : devices) {
        if (device.serialPortPath == m_pendingInitPortName) {
            deviceStillAvailable = true;
            break;
        }
    }

    // Also check available ports directly
    if (!deviceStillAvailable) {
        for (const QSerialPortInfo& info : QSerialPortInfo::availablePorts()) {
            if (info.portName() == m_pendingInitPortName) {
                deviceStillAvailable = true;
                break;
            }
        }
    }

    if (!deviceStillAvailable) {
        qCWarning(log_core_serial_hotplug) << "Device no longer available at port" << m_pendingInitPortName
                                   << ", aborting retry";
        m_initRetryCount = MAX_INIT_RETRIES;  // Stop further retries
        m_pendingInitPortName.clear();
        m_pendingInitBaudrate = 0;
        return;
    }

    // Clear any stale state before retry
    m_openInProgress.store(false);

    // Re-attempt initialization from scratch
    qCInfo(log_core_serial_watchdog) << "Re-initializing serial port for retry";
    initializeCH9329Async(m_pendingInitPortName, m_pendingInitBaudrate);
}

// HOTPLUG FIX: Cancel any pending port chain clear - called when a new device connects
void SerialPortManager::cancelPendingPortChainClear()
{
    if (m_portChainClearTimer && m_portChainClearTimer->isActive()) {
        m_portChainClearTimer->stop();
        qCDebug(log_core_serial_hotplug) << "Cancelled pending port chain clear for" << m_pendingPortChainClear;
    }
    m_pendingPortChainClear.clear();
}
