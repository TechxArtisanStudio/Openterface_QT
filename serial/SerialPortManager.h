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

#ifndef SERIALPORTMANAGER_H
#define SERIALPORTMANAGER_H

#include "../ui/statusevents.h"
#include <QObject>
#include <QSerialPort>
#include <QThread>
#include <QTimer>
#include <QLoggingCategory>
#include <QDateTime>
#include <QElapsedTimer>
#include <QMutex>
#include <QQueue>
#include <QWaitCondition>
#include <QEventLoop>
#include <atomic>
#include <memory>

#include "ch9329.h"
#include "chipstrategy/IChipStrategy.h"
#include "chipstrategy/ChipStrategyFactory.h"
#include "protocol/SerialProtocol.h"
#include "watchdog/ConnectionWatchdog.h"
#include "FactoryResetManager.h"
#include "../ui/advance/diagnostics/LogWriter.h"

Q_DECLARE_LOGGING_CATEGORY(log_core_serial)

// Forward declarations
class DeviceInfo;
class SerialCommandCoordinator;
class SerialStateManager;
class SerialStatistics;
class SerialHotplugHandler;

// Chip type enumeration (kept for backward compatibility)
// New code should use ChipTypeId from ChipStrategyFactory.h
enum class ChipType : uint32_t {
    UNKNOWN = 0,
    CH9329 = 0x1A867523,     // 1A86:7523 - Supports both 9600 and 115200, requires commands for baudrate switching and reset
    CH32V208 = 0x1A86FE0C    // 1A86:FE0C - Only supports 115200, uses simple close/reopen for baudrate changes
};

// Struct to hold configuration command results (kept for backward compatibility)
// New code should use ChipConfigResult from IChipStrategy.h
struct ConfigResult {
    bool success = false;
    int workingBaudrate = 9600;  // Use literal value instead of SerialPortManager::DEFAULT_BAUDRATE
    uint8_t mode = 0;
};

/**
 * @brief SerialPortManager - Core serial port management with modular architecture
 * 
 * REFACTORED ARCHITECTURE (Phase 4 Complete):
 * This class has been significantly refactored to use a modular, component-based architecture:
 * 
 * Core Components:
 * - SerialCommandCoordinator: Handles command queuing, execution, and response processing
 * - SerialStateManager: Manages connection states, device info, and key states  
 * - SerialStatistics: Performance monitoring, error tracking, and ARM optimization
 * - ConnectionWatchdog: Auto-recovery and connection monitoring
 * - SerialFacade: Simplified high-level interface for common operations
 * 
 * Legacy Compatibility:
 * - Public interface maintained for backward compatibility
 * - All methods delegate to appropriate specialized components
 * - Deprecated functionality marked with LEGACY comments
 * 
 * Usage Patterns:
 * - For simple operations: Use SerialFacade class for clean, high-level API
 * - For advanced usage: Access SerialPortManager directly (maintains full compatibility)
 * - For statistics: All tracking is automatic via SerialStatistics module
 * - For recovery: Auto-recovery handled by ConnectionWatchdog component
 * 
 * Architecture Benefits:
 * - Improved maintainability through separation of concerns
 * - Better testability with isolated components  
 * - Enhanced performance monitoring and diagnostics
 * - Simplified interface through facade pattern
 * - Retained backward compatibility for existing code
 */
class SerialPortManager : public QObject, public IRecoveryHandler
{
    Q_OBJECT

public:
    static const int BAUDRATE_HIGHSPEED = 115200;
    static const int BAUDRATE_LOWSPEED = 9600;
    // Set the default baudrate to 9600 for better compatibility
    static const int DEFAULT_BAUDRATE = BAUDRATE_LOWSPEED;
    static const int SERIAL_TIMER_INTERVAL = 5000;
    
    static SerialPortManager& getInstance() {
        static SerialPortManager instance; // Guaranteed to be destroyed, instantiated on first use.
        return instance;
    }
    
    SerialPortManager(SerialPortManager const&) = delete; // Don't Implement
    void operator=(SerialPortManager const&) = delete; // Don't implement

    virtual ~SerialPortManager(); // Declare the destructor

    void setEventCallback(StatusEventCallback* callback);
    bool openPort(const QString &portName, int baudRate);
    void closePort();
    bool restartPort();

    bool getNumLockState();
    bool getCapsLockState();
    bool getScrollLockState();

    bool writeData(const QByteArray &data);
    bool sendAsyncCommand(const QByteArray &data, bool force);
    bool sendResetCommand();
    QByteArray sendSyncCommand(const QByteArray &data, bool force);

    bool resetHipChip(int targetBaudrate = DEFAULT_BAUDRATE);
    bool reconfigureHidChip(int targetBaudrate = DEFAULT_BAUDRATE);
    bool factoryResetHipChipV191();
    bool factoryResetHipChip();
    
    // Synchronous factory reset methods for diagnostics
    bool factoryResetHipChipSync(int timeoutMs = 10000);
    bool factoryResetHipChipV191Sync(int timeoutMs = 5000);
    void restartSwitchableUSB();
    void setUSBconfiguration(int targetBaudrate = DEFAULT_BAUDRATE);
    void changeUSBDescriptor();
    bool setBaudRate(int baudrate);
    void setUserSelectedBaudrate(int baudrate); // Set baudrate from user menu selection
    void clearStoredBaudrate(); // Clear stored baudrate setting
    
    // ARM architecture detection and performance prompt
    static bool isArmArchitecture();
    void checkArmBaudratePerformance(int baudrate); // Check and emit signal if needed
    void setCommandDelay(int delayMs);  // set the delay
    void stop(); //stop the serial port manager

    // DeviceManager integration methods
    // void checkDeviceConnections(const QList<DeviceInfo>& devices);
    
    // Serial port switching by port chain (similar to CameraManager and VideoHid)
    bool switchSerialPortByPortChain(const QString& portChain);
    QString getCurrentSerialPortPath() const;
    QString getCurrentSerialPortChain() const;
    
    // Hotplug monitoring integration
    void connectToHotplugMonitor();
    void disconnectFromHotplugMonitor();
    
    // Enhanced stability features (delegated to ConnectionWatchdog - Phase 3 refactoring)
    void enableAutoRecovery(bool enable = true);
    void setMaxRetryAttempts(int maxRetries);
    void setMaxConsecutiveErrors(int maxErrors);
    bool isConnectionStable() const;
    int getConsecutiveErrorCount() const;
    int getConnectionRetryCount() const;
    void forceRecovery();
    
    // IRecoveryHandler interface implementation (Phase 3 refactoring)
    bool performRecovery(int attempt) override;
    void onRecoveryFailed() override;
    void onRecoverySuccess() override;
    
    // Factory reset helper - polls for ready state after reconnection
    void startReadyStatePolling(const QString& portName);
    
    // Get current baudrate
    int getCurrentBaudrate() const;
    
    // Statistics - delegated to SerialStatistics module
    void startStats();
    void stopStats();
    void resetStats();
    int getCommandsSent() const;
    int getResponsesReceived() const;
    double getResponseRate() const;
    qint64 getStatsElapsedMs() const;
    
    // Chip type detection and management
    ChipType detectChipType(const QString &portName) const;
    ChipType getCurrentChipType() const { return m_currentChipType; }
    inline bool isChipTypeCH32V208() const { return m_currentChipType == ChipType::CH32V208; }
    inline bool isChipTypeCH9329() const { return m_currentChipType == ChipType::CH9329; }

    // Query current target USB connection state (thread-safe via state manager)
    bool getTargetUsbConnected() const;
    
    // New USB switch methods for CH32V208 serial port (firmware with new protocol)
    void switchUsbToHostViaSerial();      // Switch USB to host via serial command (57 AB 00 17...)
    void switchUsbToTargetViaSerial();    // Switch USB to target via serial command (57 AB 00 17...)
    
    // Logging
    void log(const QString& message);
    QString getSerialLogFilePath() const;
    void setSerialLogFilePath(const QString& path);
    
    // Enable/disable debug logging for diagnostics
    static void enableDebugLogging(bool enabled);

public slots:
    void setDiagnosticsDialogActive(bool active);

signals:
    void dataReceived(const QByteArray &data);
    void dataSent(const QByteArray &data);
    void serialPortConnected(const QString &portName);
    void serialPortDisconnected(const QString &portName);
    void serialPortConnectionSuccess(const QString &portName);
    void sendCommandAsync(const QByteArray &command, bool waitForAck);
    void connectedPortChanged(const QString &portName, const int &baudrate);
    void serialPortSwitched(const QString& fromPortChain, const QString& toPortChain);
    void serialPortDeviceChanged(const QString& oldPortPath, const QString& newPortPath);
    void armBaudratePerformanceRecommendation(int currentBaudrate); // Signal for ARM performance recommendation
    void parameterConfigurationSuccess(); // Signal emitted when parameter configuration is successful and reset is needed
    void syncResponseReady();  // Emitted when m_syncCommandResponse is filled for sync commands
    void usbStatusChanged(bool isToTarget);  // New signal: true = target, false = host
    void targetUSBStatus(bool isTargetUSBConnected);
    void keyStatesChanged(bool numLock, bool capsLock, bool scrollLock); // Key state updates (thread-safe)
    void serialPortReset(bool isStarted); // Serial port reset started/ended
    void statusUpdate(const QString &status); // General status update for UI
    void factoryReset(bool isStarted); // Factory reset started/ended
    
    // Thread-safe reset operation signals (internal use)
    void requestResetHidChip(int targetBaudrate);
    void requestFactoryReset();
    void requestFactoryResetV191();
    
    // Signals to notify completion of reset operations
    void resetHidChipCompleted(bool success);
    void factoryResetCompleted(bool success);
    
    void logMessage(const QString& msg);
    
private slots:
    void observeSerialPortNotification();
    void readData();
    void bytesWritten(qint64 bytes);
    
    void initializeSerialPortFromPortChain();
    
    // Thread-safe reset operation handlers (called via queued connection)
    void handleResetHidChip(int targetBaudrate);
    void handleFactoryReset();
    void handleFactoryResetV191();

    // /*
    //  * Check if the USB switch status
    //  * CH340 DSR pin is connected to the hard USB toggle switch,
    //  * HIGH value means connecting to host, while LOW value means connecting to target
    //  */
    // void checkSwitchableUSB();

    void onSerialPortConnected(const QString &portName);
    void onSerialPortDisconnected(const QString &portName);
    void onSerialPortConnectionSuccess(const QString &portName);
    void onUsbStatusCheckTimeout();  // New slot for USB status check timer
    void onGetInfoTimeout();  // New slot for periodic GET_INFO requests
    
    
private:
    SerialPortManager(QObject *parent = nullptr);
    QSerialPort *serialPort;

    void sendCommand(const QByteArray &command, bool waitForAck);
    
    // Refactored helper methods for onSerialPortConnected
    int determineBaudrate() const;
    bool openPortWithRetries(const QString &portName, int tryBaudrate);
    ConfigResult sendAndProcessConfigCommand();
    void handleChipSpecificLogic(const ConfigResult &config);
    void storeBaudrateIfNeeded(int workingBaudrate);
    
    // Thread-safe baudrate setting (must be called from worker thread to access serialPort)
    bool setBaudRateInternal(int baudRate);
    
    // Thread-safe port closing (ensures QSocketNotifier operations happen in worker thread)
    void closePortInternal();
    bool handleResetHidChipInternal(int targetBaudrate);
    bool handleFactoryResetInternal();
    bool handleFactoryResetV191Internal();
    
    // Synchronous reset internal implementations (run in worker thread)
    bool handleFactoryResetSyncInternal(int timeoutMs);
    bool handleFactoryResetV191SyncInternal(int timeoutMs);
    
    // Helper for blocking wait with timeout
    bool waitForFactoryResetCompletion(int timeoutMs);

    QSet<QString> availablePorts;
    
    QThread *m_serialWorkerThread;
    QTimer *serialTimer;

    QList<QSerialPortInfo> m_lastPortList;
    std::atomic<bool> ready = false;
    StatusEventCallback* eventCallback = nullptr;
    // Legacy state variables moved to SerialStateManager
    void updateSpecialKeyState(uint8_t data);
    QDateTime lastSerialPortCheckTime;
    
    // Variable to store the latest update time
    QDateTime latestUpdateTime;
    QElapsedTimer m_lastCommandTime;  // New member for timing
    int m_commandDelayMs;  // New member for configurable delay
    
    // Current serial port tracking
    QString m_currentSerialPortPath;
    QString m_currentSerialPortChain;
    ChipType m_currentChipType = ChipType::UNKNOWN;
    
    // Chip strategy for chip-specific operations (Phase 1 refactoring)
    std::unique_ptr<IChipStrategy> m_chipStrategy;
    
    // Protocol layer for packet building/parsing (Phase 2 refactoring)
    std::unique_ptr<SerialProtocol> m_protocol;
    
    // Command coordinator for command handling (Phase 4 refactoring)
    std::unique_ptr<SerialCommandCoordinator> m_commandCoordinator;
    
    // State manager for centralized state management (Phase 4 refactoring)
    std::unique_ptr<SerialStateManager> m_stateManager;
    
    // Statistics module for performance monitoring (Phase 4 refactoring)
    std::unique_ptr<SerialStatistics> m_statistics;
    
    // Connection watchdog for monitoring and recovery (Phase 3 refactoring)
    std::unique_ptr<ConnectionWatchdog> m_watchdog;
    
    // Enhanced stability members (some delegated to ConnectionWatchdog)
    std::atomic<bool> m_isShuttingDown = false;

    // Indicates an open operation is currently in progress to prevent concurrent opens
    std::atomic<bool> m_openInProgress{false};

    // Indicates a baud-rate change is in progress; used to suppress transient errors
    std::atomic<bool> m_baudChangeInProgress{false};

    // Flag set to true when device is detected as unplugged, preventing port operations until cleared
    // This prevents race conditions where open attempts occur while device is being removed
    std::atomic<bool> m_deviceUnpluggedDetected{false};
    std::atomic<bool> m_deviceUnplugCleanupInProgress{false};

    // Legacy error counters removed - handled by SerialStatistics and ConnectionWatchdog
    QTimer* m_connectionWatchdog;
    QTimer* m_errorRecoveryTimer;
    QTimer* m_usbStatusCheckTimer;
    QTimer* m_getInfoTimer;  // Timer for periodic GET_INFO requests
    // Flag to suppress periodic GET_INFO while diagnostics dialog is active
    bool m_suppressGetInfo = false;
    QMutex m_diagMutex;
    QMutex m_serialPortMutex;
    QQueue<QByteArray> m_commandQueue;
    QMutex m_commandQueueMutex;
    bool m_autoRecoveryEnabled = true;
    int m_maxRetryAttempts = 5;
    int m_maxConsecutiveErrors = 10;
    QElapsedTimer m_lastSuccessfulCommand;
    QElapsedTimer m_errorTrackingTimer;
    QElapsedTimer m_lastErrorLogTime;      // Throttle error logging to prevent spam
    static constexpr int ERROR_LOG_THROTTLE_MS = 50;  // Min milliseconds between error processes
    bool m_errorHandlerDisconnected = false;
    static const int MAX_ERRORS_PER_SECOND = 10;
    
    // Data buffering for incomplete packets
    QByteArray m_incompleteDataBuffer;
    QMutex m_bufferMutex;
    static const int MAX_BUFFER_SIZE = 256; // Maximum buffer size to prevent memory issues
    
    // Sync command handling to prevent race conditions
    std::atomic<bool> m_pendingSyncCommand = false;
    QByteArray m_syncCommandResponse;
    QMutex m_syncResponseMutex;
    QWaitCondition m_syncResponseCondition;
    unsigned char m_pendingSyncExpectedKey = 0;
    
    // Internal state tracking for sync factory reset operations
    std::atomic<bool> m_factoryResetInProgress = false;
    std::atomic<bool> m_factoryResetResult = false;
    QMutex m_factoryResetMutex;
    QWaitCondition m_factoryResetCondition;

    // Factory reset manager (extracted for compatibility and testability)
    friend class FactoryResetManager;
    std::unique_ptr<FactoryResetManager> m_factoryResetManager;

    // New: Serial hotplug handler (extracted from inline logic)
    std::unique_ptr<SerialHotplugHandler> m_hotplugHandler;
    
    // Command tracking for auto-restart logic
    std::atomic<int> m_commandsSent = 0;
    std::atomic<int> m_commandsReceived = 0;
    std::atomic<int> m_serialResetCount = 0;
    QTimer* m_commandTrackingTimer;
    static const int COMMAND_TRACKING_INTERVAL = 5000; // 5 seconds
    static const int MAX_SERIAL_RESETS = 3;
    static constexpr double COMMAND_LOSS_THRESHOLD = 0.30; // 30% loss rate
    
    // Async message send/receive statistics (simple tracking)
    qint64 m_asyncMessagesSent = 0;
    qint64 m_asyncMessagesReceived = 0;
    QElapsedTimer m_asyncStatsTimer;
    static const int ASYNC_STATS_INTERVAL_MS = 1000; // Report every 1 second
    void logAsyncMessageStatistics();
    
    // Legacy variables removed - functionality moved to specialized modules
    
    // Enhanced error handling
    void handleSerialError(QSerialPort::SerialPortError error);
    // Attempt to resynchronize the buffer to the next valid header sequence (0x57 0xAB).
    // If resynchronization succeeds and completeData contains at least the minimal packet length,
    // return true. Otherwise update m_incompleteDataBuffer accordingly and return false.
    // bool resyncAndAlignHeader(QByteArray &completeData);
    void attemptRecovery();
    void resetErrorCounters();
    bool isRecoveryNeeded() const;
    void setupConnectionWatchdog();
    void stopConnectionWatchdog();
    int anotherBaudrate();
    QString statusCodeToString(uint8_t status) {
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
            default:
                return "Unknown status code";
        } 
    }
    
    // Command-based baudrate change for CH9329 and unknown chips
    void applyCommandBasedBaudrateChange(int baudRate, const QString& logPrefix);
    
    // Command tracking methods
    void checkCommandLossRate();
    void resetCommandCounters();

    // Logging
    QThread* m_logThread;
    LogWriter* m_logWriter;
    QString m_logFilePath; // current serial log file path
};

#endif // SERIALPORTMANAGER_H