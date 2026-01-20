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

#ifndef CONNECTION_WATCHDOG_H
#define CONNECTION_WATCHDOG_H

#include <QObject>
#include <QTimer>
#include <QElapsedTimer>
#include <QLoggingCategory>
#include <atomic>
#include <functional>

Q_DECLARE_LOGGING_CATEGORY(log_core_serial)

/**
 * @brief Connection state enumeration
 */
enum class ConnectionState {
    Disconnected,       // No connection
    Connecting,         // Attempting to connect
    Connected,          // Connected and healthy
    Unstable,           // Connected but experiencing issues
    Recovering,         // Attempting recovery
    Failed              // Recovery failed, needs manual intervention
};

/**
 * @brief Statistics about connection health
 */
struct ConnectionStats {
    int consecutiveErrors = 0;
    int totalErrors = 0;
    int recoveryAttempts = 0;
    int successfulRecoveries = 0;
    qint64 lastSuccessfulCommandMs = 0;
    qint64 uptimeMs = 0;
    double errorRate = 0.0;  // Errors per second
    
    void reset() {
        consecutiveErrors = 0;
        totalErrors = 0;
        recoveryAttempts = 0;
        successfulRecoveries = 0;
        lastSuccessfulCommandMs = 0;
        uptimeMs = 0;
        errorRate = 0.0;
    }
};

/**
 * @brief Configuration for watchdog behavior
 */
struct WatchdogConfig {
    int watchdogIntervalMs = 30000;      // How often to check connection (30s)
    int maxConsecutiveErrors = 10;        // Errors before triggering recovery
    int maxRetryAttempts = 5;             // Maximum recovery attempts
    int baseRetryDelayMs = 1000;          // Base delay for exponential backoff
    int maxRetryDelayMs = 10000;          // Maximum retry delay (10s)
    int communicationTimeoutMs = 30000;   // Time without communication before watchdog triggers
    bool autoRecoveryEnabled = true;      // Enable automatic recovery
};

/**
 * @brief Interface for recovery actions
 * 
 * Implement this interface to provide custom recovery logic
 */
class IRecoveryHandler {
public:
    virtual ~IRecoveryHandler() = default;
    
    /**
     * @brief Called when recovery is needed
     * @param attempt Current recovery attempt number (1-based)
     * @return true if recovery was successful
     */
    virtual bool performRecovery(int attempt) = 0;
    
    /**
     * @brief Called when recovery has failed after all attempts
     */
    virtual void onRecoveryFailed() = 0;
    
    /**
     * @brief Called when recovery succeeds
     */
    virtual void onRecoverySuccess() = 0;
};

/**
 * @brief Connection watchdog for monitoring serial port health
 * 
 * This class monitors connection health and triggers automatic recovery
 * when issues are detected. It uses:
 * - Periodic heartbeat checking
 * - Error counting and rate tracking
 * - Exponential backoff for retries
 * - Customizable recovery handlers
 * 
 * Phase 3 refactoring: Extracted from SerialPortManager
 */
class ConnectionWatchdog : public QObject
{
    Q_OBJECT

public:
    explicit ConnectionWatchdog(QObject *parent = nullptr);
    ~ConnectionWatchdog();
    
    // ========== Configuration ==========
    
    /**
     * @brief Set watchdog configuration
     */
    void setConfig(const WatchdogConfig& config);
    WatchdogConfig getConfig() const { return m_config; }
    
    /**
     * @brief Set the recovery handler
     */
    void setRecoveryHandler(IRecoveryHandler* handler);
    
    /**
     * @brief Enable or disable auto recovery
     */
    void setAutoRecoveryEnabled(bool enabled);
    bool isAutoRecoveryEnabled() const { return m_config.autoRecoveryEnabled; }
    
    /**
     * @brief Set maximum retry attempts
     */
    void setMaxRetryAttempts(int maxRetries);
    int getMaxRetryAttempts() const { return m_config.maxRetryAttempts; }
    
    /**
     * @brief Set maximum consecutive errors before recovery
     */
    void setMaxConsecutiveErrors(int maxErrors);
    int getMaxConsecutiveErrors() const { return m_config.maxConsecutiveErrors; }
    
    // ========== Lifecycle ==========
    
    /**
     * @brief Start the watchdog monitoring
     */
    void start();
    
    /**
     * @brief Stop the watchdog monitoring
     */
    void stop();
    
    /**
     * @brief Check if watchdog is currently running
     */
    bool isRunning() const;
    
    /**
     * @brief Set shutdown flag to gracefully stop operations
     */
    void setShuttingDown(bool shuttingDown);
    
    // ========== Error Tracking ==========
    
    /**
     * @brief Record a successful command/communication
     */
    void recordSuccess();
    
    /**
     * @brief Record an error occurrence
     */
    void recordError();
    
    /**
     * @brief Reset all error counters
     */
    void resetCounters();
    
    /**
     * @brief Check if recovery is needed based on current error state
     */
    bool isRecoveryNeeded() const;
    
    /**
     * @brief Get current connection state
     */
    ConnectionState getConnectionState() const { return m_connectionState; }
    
    /**
     * @brief Get connection statistics
     */
    ConnectionStats getStats() const;
    
    /**
     * @brief Check if connection is considered stable
     */
    bool isConnectionStable() const;
    
    /**
     * @brief Get consecutive error count
     */
    int getConsecutiveErrorCount() const { return m_consecutiveErrors.load(); }
    
    /**
     * @brief Get retry attempt count
     */
    int getRetryAttemptCount() const { return m_retryAttemptCount.load(); }
    
    // ========== Manual Recovery ==========
    
    /**
     * @brief Force a recovery attempt
     */
    void forceRecovery();
    
signals:
    /**
     * @brief Emitted when connection state changes
     */
    void connectionStateChanged(ConnectionState newState);
    
    /**
     * @brief Emitted when recovery is starting
     */
    void recoveryStarted(int attempt);
    
    /**
     * @brief Emitted when recovery succeeds
     */
    void recoverySucceeded();
    
    /**
     * @brief Emitted when recovery fails after all attempts
     */
    void recoveryFailed();
    
    /**
     * @brief Emitted when watchdog timeout occurs
     */
    void watchdogTimeout();
    
    /**
     * @brief Emitted with status updates for UI
     */
    void statusUpdate(const QString& status);
    
    /**
     * @brief Emitted when error threshold is reached
     */
    void errorThresholdReached(int errorCount);

private slots:
    void onWatchdogTimeout();
    void executeRecovery();

private:
    void setConnectionState(ConnectionState state);
    void scheduleRecovery();
    int calculateRetryDelay() const;
    void updateErrorRate();
    
    // Configuration
    WatchdogConfig m_config;
    
    // State
    ConnectionState m_connectionState = ConnectionState::Disconnected;
    std::atomic<bool> m_isShuttingDown{false};
    std::atomic<bool> m_isRunning{false};
    
    // Error tracking
    std::atomic<int> m_consecutiveErrors{0};
    std::atomic<int> m_totalErrors{0};
    std::atomic<int> m_retryAttemptCount{0};
    std::atomic<int> m_successfulRecoveries{0};
    
    // Timers
    QTimer* m_watchdogTimer = nullptr;
    QTimer* m_recoveryTimer = nullptr;
    QElapsedTimer m_lastSuccessfulCommand;
    QElapsedTimer m_uptimeTimer;
    QElapsedTimer m_errorRateTimer;
    
    // Error rate tracking
    int m_errorsInWindow = 0;
    static const int ERROR_RATE_WINDOW_MS = 1000;  // 1 second window
    
    // Recovery handler
    IRecoveryHandler* m_recoveryHandler = nullptr;
};

#endif // CONNECTION_WATCHDOG_H
