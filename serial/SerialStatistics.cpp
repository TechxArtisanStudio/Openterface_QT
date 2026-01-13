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

#include "SerialStatistics.h"
#include <QLoggingCategory>
#include <QMutexLocker>
#include <QSysInfo>
#include <QStandardPaths>
#include <QFile>
#include <QTextStream>
#include <QJsonDocument>
#include <QJsonObject>

Q_LOGGING_CATEGORY(log_serial_statistics, "opf.serial.statistics")

// Define static constants
const int SerialStatistics::DEFAULT_MONITORING_INTERVAL;
const int SerialStatistics::HIGH_FREQUENCY_MONITORING_INTERVAL;

SerialStatistics::SerialStatistics(QObject *parent)
    : QObject(parent)
    , m_performanceMonitor(nullptr)
    , m_performanceMonitoringEnabled(false)
{
    qCDebug(log_serial_statistics) << "SerialStatistics initialized";
    
    // Initialize data
    m_data.reset();
    
    // Initialize ARM detection
    initializeArmDetection();
    
    // Create performance monitor timer (will be started when needed)
    m_performanceMonitor = new QTimer(this);
    m_performanceMonitor->setSingleShot(false);
    m_performanceMonitor->setInterval(DEFAULT_MONITORING_INTERVAL);
    connect(m_performanceMonitor, &QTimer::timeout, this, &SerialStatistics::onPerformanceMonitorTimeout);
}

SerialStatistics::~SerialStatistics()
{
    m_isShuttingDown = true;
    if (m_performanceMonitor) {
        m_performanceMonitor->stop();
    }
    qCDebug(log_serial_statistics) << "SerialStatistics destroyed";
}

// Statistics control
void SerialStatistics::startTracking()
{
    QMutexLocker locker(&m_statisticsMutex);
    if (!m_isTrackingEnabled) {
        m_data.reset();
        m_isTrackingEnabled = true;
        qCDebug(log_serial_statistics) << "Statistics tracking started";
        
        if (m_performanceMonitoringEnabled && m_performanceMonitor) {
            m_performanceMonitor->start();
        }
    }
}

void SerialStatistics::stopTracking()
{
    QMutexLocker locker(&m_statisticsMutex);
    if (m_isTrackingEnabled) {
        m_isTrackingEnabled = false;
        
        if (m_performanceMonitor) {
            m_performanceMonitor->stop();
        }
        
        StatisticsData finalData = m_data;
        locker.unlock();
        
        qCDebug(log_serial_statistics) << "Statistics tracking stopped";
        emit statisticsUpdated(finalData);
    }
}

void SerialStatistics::resetStatistics()
{
    QMutexLocker locker(&m_statisticsMutex);
    m_data.reset();
    qCDebug(log_serial_statistics) << "Statistics reset";
    
    if (m_isTrackingEnabled) {
        StatisticsData resetData = m_data;
        locker.unlock();
        emit statisticsUpdated(resetData);
    }
}

bool SerialStatistics::isTrackingEnabled() const
{
    return m_isTrackingEnabled;
}

// Command tracking
void SerialStatistics::recordCommandSent()
{
    if (!m_isTrackingEnabled) return;
    
    QMutexLocker locker(&m_statisticsMutex);
    m_data.commandsSent++;
    qCDebug(log_serial_statistics) << "Command sent recorded, total:" << m_data.commandsSent;
}

void SerialStatistics::recordResponseReceived()
{
    if (!m_isTrackingEnabled) return;
    
    QMutexLocker locker(&m_statisticsMutex);
    // Suppress duplicates recorded within a short timeframe (e.g., sync path + async handler)
    if (m_lastResponseTimer.isValid() && m_lastResponseTimer.elapsed() < 10) {
        qCDebug(log_serial_statistics) << "Suppressing duplicate response recorded within 10ms";
        return;
    }

    m_data.responsesReceived++;
    // Reset consecutive errors on successful response
    m_data.consecutiveErrors = 0;
    // Update last response time
    m_lastResponseTimer.start();
    qCDebug(log_serial_statistics) << "Response received recorded, total:" << m_data.responsesReceived;
}

void SerialStatistics::recordCommandLost()
{
    if (!m_isTrackingEnabled) return;
    
    QMutexLocker locker(&m_statisticsMutex);
    m_data.commandsLost++;
    qCDebug(log_serial_statistics) << "Command lost recorded, total:" << m_data.commandsLost;
}

void SerialStatistics::recordConsecutiveError()
{
    if (!m_isTrackingEnabled) return;
    
    QMutexLocker locker(&m_statisticsMutex);
    m_data.consecutiveErrors++;
    qCDebug(log_serial_statistics) << "Consecutive error recorded, total:" << m_data.consecutiveErrors;
    
    // Check if threshold exceeded
    if (m_data.consecutiveErrors >= m_thresholds.maxConsecutiveErrors) {
        locker.unlock();
        emit performanceThresholdExceeded("consecutiveErrors", m_data.consecutiveErrors, m_thresholds.maxConsecutiveErrors);
        emit recoveryRecommended("Too many consecutive errors");
    }
}

void SerialStatistics::recordConnectionRetry()
{
    if (!m_isTrackingEnabled) return;
    
    QMutexLocker locker(&m_statisticsMutex);
    m_data.connectionRetries++;
    qCDebug(log_serial_statistics) << "Connection retry recorded, total:" << m_data.connectionRetries;
    
    // Check if threshold exceeded
    if (m_data.connectionRetries >= m_thresholds.maxConnectionRetries) {
        locker.unlock();
        emit performanceThresholdExceeded("connectionRetries", m_data.connectionRetries, m_thresholds.maxConnectionRetries);
    }
}

void SerialStatistics::recordSerialReset()
{
    if (!m_isTrackingEnabled) return;
    
    QMutexLocker locker(&m_statisticsMutex);
    m_data.serialResets++;
    qCDebug(log_serial_statistics) << "Serial reset recorded, total:" << m_data.serialResets;
    
    // Check if threshold exceeded
    if (m_data.serialResets >= m_thresholds.maxSerialResets) {
        locker.unlock();
        emit performanceThresholdExceeded("serialResets", m_data.serialResets, m_thresholds.maxSerialResets);
        emit criticalPerformanceDetected("Too many serial resets - connection unstable");
    }
}

void SerialStatistics::resetErrorCounters()
{
    QMutexLocker locker(&m_statisticsMutex);
    m_data.consecutiveErrors = 0;
    qCDebug(log_serial_statistics) << "Error counters reset";
}

// Data access
StatisticsData SerialStatistics::getCurrentData() const
{
    QMutexLocker locker(&m_statisticsMutex);
    return m_data;
}

int SerialStatistics::getCommandsSent() const
{
    QMutexLocker locker(&m_statisticsMutex);
    return m_data.commandsSent;
}

int SerialStatistics::getResponsesReceived() const
{
    QMutexLocker locker(&m_statisticsMutex);
    return m_data.responsesReceived;
}

int SerialStatistics::getCommandsLost() const
{
    QMutexLocker locker(&m_statisticsMutex);
    return m_data.commandsLost;
}

double SerialStatistics::getResponseRate() const
{
    QMutexLocker locker(&m_statisticsMutex);
    return m_data.responseRate();
}

double SerialStatistics::getErrorRate() const
{
    QMutexLocker locker(&m_statisticsMutex);
    return m_data.errorRate();
}

qint64 SerialStatistics::getElapsedMs() const
{
    QMutexLocker locker(&m_statisticsMutex);
    return m_data.elapsedMs();
}

int SerialStatistics::getConsecutiveErrors() const
{
    QMutexLocker locker(&m_statisticsMutex);
    return m_data.consecutiveErrors;
}

int SerialStatistics::getConnectionRetries() const
{
    QMutexLocker locker(&m_statisticsMutex);
    return m_data.connectionRetries;
}

int SerialStatistics::getSerialResets() const
{
    QMutexLocker locker(&m_statisticsMutex);
    return m_data.serialResets;
}

// Performance monitoring
void SerialStatistics::setPerformanceThresholds(const PerformanceThresholds& thresholds)
{
    QMutexLocker locker(&m_statisticsMutex);
    m_thresholds = thresholds;
    qCDebug(log_serial_statistics) << "Performance thresholds updated";
}

PerformanceThresholds SerialStatistics::getPerformanceThresholds() const
{
    QMutexLocker locker(&m_statisticsMutex);
    return m_thresholds;
}

bool SerialStatistics::isPerformanceCritical() const
{
    QMutexLocker locker(&m_statisticsMutex);
    
    // Check multiple criteria for critical performance
    bool highErrorRate = m_data.errorRate() > m_thresholds.commandLossThreshold * 100;
    bool tooManyErrors = m_data.consecutiveErrors >= m_thresholds.maxConsecutiveErrors;
    bool tooManyResets = m_data.serialResets >= m_thresholds.maxSerialResets;
    bool tooManyRetries = m_data.connectionRetries >= m_thresholds.maxConnectionRetries;
    
    return highErrorRate || tooManyErrors || tooManyResets || tooManyRetries;
}

bool SerialStatistics::isRecoveryNeeded() const
{
    QMutexLocker locker(&m_statisticsMutex);
    
    // Recovery needed if consecutive errors exceed threshold
    return m_data.consecutiveErrors >= m_thresholds.maxConsecutiveErrors;
}

// ARM architecture support
void SerialStatistics::setArmPerformanceData(const ArmPerformanceData& data)
{
    QMutexLocker locker(&m_statisticsMutex);
    m_armData = data;
    qCDebug(log_serial_statistics) << "ARM performance data updated";
}

ArmPerformanceData SerialStatistics::getArmPerformanceData() const
{
    QMutexLocker locker(&m_statisticsMutex);
    return m_armData;
}

bool SerialStatistics::shouldRecommendBaudrateChange(int currentBaudrate) const
{
    QMutexLocker locker(&m_statisticsMutex);
    
    // Only recommend for ARM architecture and high baudrate
    return m_armData.isArmArchitecture && 
           !m_armData.promptDisabled && 
           currentBaudrate == 115200 && 
           m_armData.recommendedBaudrate != currentBaudrate;
}

// Real-time monitoring
void SerialStatistics::enablePerformanceMonitoring(bool enabled)
{
    QMutexLocker locker(&m_statisticsMutex);
    bool wasEnabled = m_performanceMonitoringEnabled;
    m_performanceMonitoringEnabled = enabled;
    
    if (enabled && !wasEnabled && m_isTrackingEnabled && m_performanceMonitor) {
        m_performanceMonitor->start();
    } else if (!enabled && wasEnabled && m_performanceMonitor) {
        m_performanceMonitor->stop();
    }
    
    qCDebug(log_serial_statistics) << "Performance monitoring" << (enabled ? "enabled" : "disabled");
}

void SerialStatistics::setMonitoringInterval(int intervalMs)
{
    if (m_performanceMonitor) {
        m_performanceMonitor->setInterval(qMax(1000, intervalMs)); // Minimum 1 second
        qCDebug(log_serial_statistics) << "Monitoring interval set to:" << intervalMs << "ms";
    }
}

// Diagnostics and reporting
QString SerialStatistics::getPerformanceReport() const
{
    QMutexLocker locker(&m_statisticsMutex);
    
    QString report;
    report += QString("=== Serial Performance Report ===\n");
    report += QString("Tracking Time: %1 seconds\n").arg(m_data.elapsedMs() / 1000.0, 0, 'f', 1);
    report += QString("Commands Sent: %1\n").arg(m_data.commandsSent);
    report += QString("Responses Received: %1\n").arg(m_data.responsesReceived);
    report += QString("Commands Lost: %1\n").arg(m_data.commandsLost);
    report += QString("Response Rate: %1%\n").arg(m_data.responseRate(), 0, 'f', 2);
    report += QString("Error Rate: %1%\n").arg(m_data.errorRate(), 0, 'f', 2);
    report += QString("Consecutive Errors: %1\n").arg(m_data.consecutiveErrors);
    report += QString("Connection Retries: %1\n").arg(m_data.connectionRetries);
    report += QString("Serial Resets: %1\n").arg(m_data.serialResets);
    
    // Performance status
    if (isPerformanceCritical()) {
        report += QString("Status: CRITICAL - Performance issues detected\n");
    } else {
        report += QString("Status: OK - Performance within normal range\n");
    }
    
    return report;
}

QString SerialStatistics::getDetailedReport() const
{
    QString report = getPerformanceReport();
    
    QMutexLocker locker(&m_statisticsMutex);
    
    report += QString("\n=== Configuration ===\n");
    report += QString("Command Loss Threshold: %1%\n").arg(m_thresholds.commandLossThreshold * 100, 0, 'f', 1);
    report += QString("Max Consecutive Errors: %1\n").arg(m_thresholds.maxConsecutiveErrors);
    report += QString("Max Serial Resets: %1\n").arg(m_thresholds.maxSerialResets);
    report += QString("Max Connection Retries: %1\n").arg(m_thresholds.maxConnectionRetries);
    
    if (m_armData.isArmArchitecture) {
        report += QString("\n=== ARM Architecture ===\n");
        report += QString("CPU Architecture: %1\n").arg(m_armData.cpuArchitecture);
        report += QString("Recommended Baudrate: %1\n").arg(m_armData.recommendedBaudrate);
        report += QString("Prompt Disabled: %1\n").arg(m_armData.promptDisabled ? "Yes" : "No");
    }
    
    return report;
}

bool SerialStatistics::exportStatistics(const QString& filePath) const
{
    QMutexLocker locker(&m_statisticsMutex);
    
    QJsonObject json;
    json["timestamp"] = m_data.startTime.toString(Qt::ISODate);
    json["elapsedMs"] = m_data.elapsedMs();
    json["commandsSent"] = m_data.commandsSent;
    json["responsesReceived"] = m_data.responsesReceived;
    json["commandsLost"] = m_data.commandsLost;
    json["responseRate"] = m_data.responseRate();
    json["errorRate"] = m_data.errorRate();
    json["consecutiveErrors"] = m_data.consecutiveErrors;
    json["connectionRetries"] = m_data.connectionRetries;
    json["serialResets"] = m_data.serialResets;
    
    QJsonDocument doc(json);
    
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        qCWarning(log_serial_statistics) << "Failed to open file for export:" << filePath;
        return false;
    }
    
    file.write(doc.toJson());
    qCDebug(log_serial_statistics) << "Statistics exported to:" << filePath;
    return true;
}

// Private slots
void SerialStatistics::onPerformanceMonitorTimeout()
{
    if (m_isShuttingDown || !m_isTrackingEnabled) {
        return;
    }
    
    analyzePerformance();
    
    QMutexLocker locker(&m_statisticsMutex);
    StatisticsData currentData = m_data;
    locker.unlock();
    
    emit statisticsUpdated(currentData);
}

void SerialStatistics::analyzePerformance()
{
    checkPerformanceThresholds();
    emitPerformanceSignals();
}

// Private helper methods
void SerialStatistics::checkPerformanceThresholds()
{
    QMutexLocker locker(&m_statisticsMutex);
    
    // Check error rate threshold
    double errorRate = m_data.errorRate();
    if (errorRate > m_thresholds.commandLossThreshold * 100) {
        locker.unlock();
        emit performanceThresholdExceeded("errorRate", errorRate, m_thresholds.commandLossThreshold * 100);
        emit recoveryRecommended("High command loss rate detected");
        return;
    }
    
    // Other thresholds are checked in individual record methods
}

void SerialStatistics::emitPerformanceSignals()
{
    QMutexLocker locker(&m_statisticsMutex);
    
    // Check for ARM baudrate recommendation
    if (shouldRecommendBaudrateChange(115200)) {
        locker.unlock();
        emit armBaudrateRecommendation(115200, m_armData.recommendedBaudrate);
        return;
    }
}

void SerialStatistics::initializeArmDetection()
{
    QString architecture = QSysInfo::currentCpuArchitecture();
    
    m_armData.cpuArchitecture = architecture;
    m_armData.isArmArchitecture = architecture.contains("arm", Qt::CaseInsensitive) || 
                                  architecture.contains("aarch64", Qt::CaseInsensitive);
    m_armData.recommendedBaudrate = 9600; // Recommended for ARM performance
    m_armData.promptDisabled = false;
    
    qCDebug(log_serial_statistics) << "ARM detection initialized:"
                                  << "Architecture:" << architecture
                                  << "Is ARM:" << m_armData.isArmArchitecture;
}