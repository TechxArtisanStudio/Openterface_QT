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

#include "ConnectionWatchdog.h"
#include <QDebug>
#include <QtMath>
#include <QThread>
#include <QMetaObject>


ConnectionWatchdog::ConnectionWatchdog(QObject *parent)
    : QObject(parent)
{
    // Timers are created lazily in start() to ensure correct thread affinity
    m_watchdogTimer = nullptr;
    m_recoveryTimer = nullptr;

    // Initialize elapsed timers (no thread affinity)
    m_lastSuccessfulCommand.start();
    m_uptimeTimer.start();
    m_errorRateTimer.start();
    
    qCDebug(log_core_serial) << "ConnectionWatchdog initialized";
}

ConnectionWatchdog::~ConnectionWatchdog()
{
    stop();
    qCDebug(log_core_serial) << "ConnectionWatchdog destroyed";
}

// ========== Configuration ==========

void ConnectionWatchdog::setConfig(const WatchdogConfig& config)
{
    m_config = config;
    qCDebug(log_core_serial) << "Watchdog config updated:"
                          << "interval=" << config.watchdogIntervalMs << "ms"
                          << "maxErrors=" << config.maxConsecutiveErrors
                          << "maxRetries=" << config.maxRetryAttempts
                          << "autoRecovery=" << config.autoRecoveryEnabled;
}

void ConnectionWatchdog::setRecoveryHandler(IRecoveryHandler* handler)
{
    m_recoveryHandler = handler;
    qCDebug(log_core_serial) << "Recovery handler set:" << (handler ? "valid" : "null");
}

void ConnectionWatchdog::setAutoRecoveryEnabled(bool enabled)
{
    m_config.autoRecoveryEnabled = enabled;
    qCDebug(log_core_serial) << "Auto recovery" << (enabled ? "enabled" : "disabled");
}

void ConnectionWatchdog::setMaxRetryAttempts(int maxRetries)
{
    m_config.maxRetryAttempts = maxRetries;
    qCDebug(log_core_serial) << "Max retry attempts set to" << maxRetries;
}

void ConnectionWatchdog::setMaxConsecutiveErrors(int maxErrors)
{
    m_config.maxConsecutiveErrors = maxErrors;
    qCDebug(log_core_serial) << "Max consecutive errors set to" << maxErrors;
}

// ========== Lifecycle ==========

void ConnectionWatchdog::start()
{
    if (m_isRunning) {
        qCDebug(log_core_serial) << "Watchdog already running";
        return;
    }
    
    m_isRunning = true;
    m_isShuttingDown = false;
    m_uptimeTimer.restart();
    m_lastSuccessfulCommand.restart();
    
    // Ensure timers are created in this object's current thread (thread-safe)
    if (!m_watchdogTimer) {
        m_watchdogTimer = new QTimer(this);
        m_watchdogTimer->setSingleShot(true);
        connect(m_watchdogTimer, &QTimer::timeout, this, &ConnectionWatchdog::onWatchdogTimeout);
    }
    
    if (!m_recoveryTimer) {
        m_recoveryTimer = new QTimer(this);
        m_recoveryTimer->setSingleShot(true);
        connect(m_recoveryTimer, &QTimer::timeout, this, &ConnectionWatchdog::executeRecovery);
    }

    // Start watchdog timer
    m_watchdogTimer->setInterval(m_config.watchdogIntervalMs);
    m_watchdogTimer->start();
    
    setConnectionState(ConnectionState::Connected);
    qCInfo(log_core_serial) << "Watchdog started with interval" << m_config.watchdogIntervalMs << "ms";
}

void ConnectionWatchdog::stop()
{
    if (!m_isRunning) {
        return;
    }
    
    m_isRunning = false;
    m_isShuttingDown = true;  // Set shutdown flag immediately to block new operations
    
    // Stop timers safely
    if (m_watchdogTimer && m_watchdogTimer->isActive()) {
        m_watchdogTimer->stop();
    }
    
    if (m_recoveryTimer && m_recoveryTimer->isActive()) {
        m_recoveryTimer->stop();
    }
    
    setConnectionState(ConnectionState::Disconnected);
    qCInfo(log_core_serial) << "Watchdog stopped";
}

bool ConnectionWatchdog::isRunning() const
{
    return m_isRunning;
}

void ConnectionWatchdog::setShuttingDown(bool shuttingDown)
{
    m_isShuttingDown = shuttingDown;
    if (shuttingDown) {
        stop();
    }
}

// ========== Error Tracking ==========

void ConnectionWatchdog::recordSuccess()
{
    m_consecutiveErrors = 0;
    m_lastSuccessfulCommand.restart();
    
    // If we were in unstable state, return to connected
    if (m_connectionState == ConnectionState::Unstable) {
        setConnectionState(ConnectionState::Connected);
    }
    
    // If we were recovering, mark as successful
    if (m_connectionState == ConnectionState::Recovering) {
        m_successfulRecoveries++;
        setConnectionState(ConnectionState::Connected);
        emit recoverySucceeded();
        
        if (m_recoveryHandler) {
            m_recoveryHandler->onRecoverySuccess();
        }
        
        qCInfo(log_core_serial) << "Recovery successful after" << m_retryAttemptCount.load() << "attempts";
        m_retryAttemptCount = 0;
    }
}

void ConnectionWatchdog::recordError()
{
    m_consecutiveErrors++;
    m_totalErrors++;
    m_errorsInWindow++;
    
    updateErrorRate();
    
    qCDebug(log_core_serial) << "Error recorded. Consecutive:" << m_consecutiveErrors.load()
                          << "Total:" << m_totalErrors.load();
    
    // Check if we should transition to unstable state
    if (m_connectionState == ConnectionState::Connected && 
        m_consecutiveErrors >= m_config.maxConsecutiveErrors / 2) {
        setConnectionState(ConnectionState::Unstable);
    }
    
    // Check if recovery is needed
    if (isRecoveryNeeded()) {
        emit errorThresholdReached(m_consecutiveErrors.load());
        
        if (m_config.autoRecoveryEnabled) {
            scheduleRecovery();
        }
    }
}

void ConnectionWatchdog::resetCounters()
{
    m_consecutiveErrors = 0;
    m_retryAttemptCount = 0;
    m_errorsInWindow = 0;
    m_errorRateTimer.restart();
    
    qCDebug(log_core_serial) << "Error counters reset";
}

bool ConnectionWatchdog::isRecoveryNeeded() const
{
    return m_config.autoRecoveryEnabled && 
           m_consecutiveErrors >= m_config.maxConsecutiveErrors &&
           m_retryAttemptCount < m_config.maxRetryAttempts;
}

ConnectionStats ConnectionWatchdog::getStats() const
{
    ConnectionStats stats;
    stats.consecutiveErrors = m_consecutiveErrors.load();
    stats.totalErrors = m_totalErrors.load();
    stats.recoveryAttempts = m_retryAttemptCount.load();
    stats.successfulRecoveries = m_successfulRecoveries.load();
    stats.lastSuccessfulCommandMs = m_lastSuccessfulCommand.elapsed();
    stats.uptimeMs = m_uptimeTimer.elapsed();
    
    // Calculate error rate
    if (m_errorRateTimer.elapsed() > 0) {
        stats.errorRate = static_cast<double>(m_errorsInWindow) * 1000.0 / m_errorRateTimer.elapsed();
    }
    
    return stats;
}

bool ConnectionWatchdog::isConnectionStable() const
{
    return m_connectionState == ConnectionState::Connected &&
           m_consecutiveErrors < m_config.maxConsecutiveErrors / 2 &&
           m_lastSuccessfulCommand.elapsed() < m_config.communicationTimeoutMs;
}

// ========== Manual Recovery ==========

void ConnectionWatchdog::forceRecovery()
{
    qCInfo(log_core_serial) << "Force recovery requested";
    
    // Force error threshold to trigger recovery
    m_consecutiveErrors = m_config.maxConsecutiveErrors;
    scheduleRecovery();
}

// ========== Private Slots ==========

void ConnectionWatchdog::onWatchdogTimeout()
{
    if (m_isShuttingDown || !m_isRunning) {
        return;
    }
    
    qCDebug(log_core_serial) << "Watchdog check - last success:" 
                          << m_lastSuccessfulCommand.elapsed() << "ms ago";
    
    // Check if we haven't had successful communication
    if (m_lastSuccessfulCommand.elapsed() > m_config.communicationTimeoutMs) {
        qCWarning(log_core_serial) << "Watchdog triggered - no communication for" 
                                << m_config.communicationTimeoutMs << "ms";
        
        emit watchdogTimeout();
        emit statusUpdate(QString("No communication for %1 seconds")
                         .arg(m_config.communicationTimeoutMs / 1000));
        
        // Force recovery if auto-recovery is enabled
        if (m_config.autoRecoveryEnabled && m_retryAttemptCount < m_config.maxRetryAttempts) {
            m_consecutiveErrors = m_config.maxConsecutiveErrors;
            scheduleRecovery();
        }
    }
    
    // Restart watchdog timer
    if (m_isRunning && !m_isShuttingDown && m_watchdogTimer) {
        m_watchdogTimer->start();
    }
}

void ConnectionWatchdog::executeRecovery()
{
    if (m_isShuttingDown) {
        return;
    }
    
    m_retryAttemptCount++;
    
    qCInfo(log_core_serial) << "Executing recovery attempt" << m_retryAttemptCount.load()
                         << "of" << m_config.maxRetryAttempts;
    
    emit recoveryStarted(m_retryAttemptCount.load());
    emit statusUpdate(QString("Recovery attempt %1 of %2")
                     .arg(m_retryAttemptCount.load())
                     .arg(m_config.maxRetryAttempts));
    
    setConnectionState(ConnectionState::Recovering);
    
    bool success = false;
    
    if (m_recoveryHandler) {
        success = m_recoveryHandler->performRecovery(m_retryAttemptCount.load());
    } else {
        qCWarning(log_core_serial) << "No recovery handler set - cannot perform recovery";
    }
    
    if (success) {
        recordSuccess();
    } else {
        qCWarning(log_core_serial) << "Recovery attempt" << m_retryAttemptCount.load() << "failed";
        
        if (m_retryAttemptCount >= m_config.maxRetryAttempts) {
            qCCritical(log_core_serial) << "Maximum retry attempts reached. Recovery failed.";
            setConnectionState(ConnectionState::Failed);
            emit recoveryFailed();
            emit statusUpdate("Recovery failed - max retries exceeded");
            
            if (m_recoveryHandler) {
                m_recoveryHandler->onRecoveryFailed();
            }
        } else {
            // Schedule another recovery attempt
            scheduleRecovery();
        }
    }
}

// ========== Private Methods ==========

void ConnectionWatchdog::setConnectionState(ConnectionState state)
{
    if (m_connectionState != state) {
        ConnectionState oldState = m_connectionState;
        m_connectionState = state;
        
        qCDebug(log_core_serial) << "Connection state changed from" 
                              << static_cast<int>(oldState) << "to" << static_cast<int>(state);
        
        emit connectionStateChanged(state);
    }
}

void ConnectionWatchdog::scheduleRecovery()
{
    if (m_isShuttingDown || m_connectionState == ConnectionState::Recovering) {
        return;
    }
    
    if (m_retryAttemptCount >= m_config.maxRetryAttempts) {
        qCWarning(log_core_serial) << "Cannot schedule recovery - max attempts reached";
        return;
    }
    int delay = calculateRetryDelay();

    // Avoid scheduling if a recovery is already scheduled
    bool alreadyScheduled = false;
    if (QThread::currentThread() == thread()) {
        if (m_recoveryTimer && m_recoveryTimer->isActive()) {
            alreadyScheduled = true;
        }
    } else {
        // Query the watchdog thread for the timer status in a thread-safe way
        QMetaObject::invokeMethod(this, "isRecoveryScheduled", Qt::BlockingQueuedConnection,
                                  Q_RETURN_ARG(bool, alreadyScheduled));
    }

    if (alreadyScheduled) {
        qCDebug(log_core_serial) << "Recovery already scheduled, skipping duplicate schedule";
        return;
    }

    qCInfo(log_core_serial) << "Scheduling recovery in" << delay << "ms"
                         << "(attempt" << (m_retryAttemptCount.load() + 1) << ")";

    // Use QMetaObject::invokeMethod to safely start the timer on this object's thread
    QMetaObject::invokeMethod(this, [this, delay]() {
        // Check shutdown and timer validity before accessing timer
        if (m_isShuttingDown || !m_isRunning) {
            return;
        }

        if (m_connectionState == ConnectionState::Recovering) {
            return;
        }

        // Guard against null pointer and ensure timer exists
        if (!m_recoveryTimer) {
            qCWarning(log_core_serial) << "Recovery timer is null - cannot schedule recovery";
            return;
        }

        if (m_recoveryTimer->isActive()) {
            qCDebug(log_core_serial) << "(invoke) Recovery already scheduled, skipping";
            return;
        }

        // Safe to access timer now
        m_recoveryTimer->stop();
        m_recoveryTimer->setInterval(delay);
        m_recoveryTimer->start();
    }, Qt::QueuedConnection);
}

int ConnectionWatchdog::calculateRetryDelay() const
{
    // Exponential backoff: baseDelay * 2^attempt, capped at maxDelay
    int exponent = qMin(m_retryAttemptCount.load(), 10);  // Cap exponent to prevent overflow
    int delay = m_config.baseRetryDelayMs * (1 << exponent);
    return qMin(delay, m_config.maxRetryDelayMs);
}

void ConnectionWatchdog::updateErrorRate()
{
    // Reset error window if more than the window time has passed
    if (m_errorRateTimer.elapsed() > ERROR_RATE_WINDOW_MS) {
        m_errorsInWindow = 1;  // Count current error
        m_errorRateTimer.restart();
    }
}

bool ConnectionWatchdog::isRecoveryScheduled() const
{
    return m_recoveryTimer && m_recoveryTimer->isActive();
}
