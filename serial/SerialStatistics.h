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

#ifndef SERIALSTATISTICS_H
#define SERIALSTATISTICS_H

#include <QObject>
#include <QDateTime>
#include <QElapsedTimer>
#include <QMutex>
#include <QTimer>
#include <atomic>

/**
 * @brief Statistics data structure for performance tracking
 */
struct StatisticsData {
    int commandsSent = 0;
    int responsesReceived = 0;
    int commandsLost = 0;
    int consecutiveErrors = 0;
    int connectionRetries = 0;
    int serialResets = 0;
    QDateTime startTime;
    QElapsedTimer sessionTimer;
    
    // Performance metrics
    double responseRate() const {
        return commandsSent > 0 ? (double)responsesReceived / commandsSent * 100.0 : 0.0;
    }
    
    double errorRate() const {
        return commandsSent > 0 ? (double)commandsLost / commandsSent * 100.0 : 0.0;
    }
    
    qint64 elapsedMs() const {
        return sessionTimer.isValid() ? sessionTimer.elapsed() : 0;
    }
    
    void reset() {
        commandsSent = 0;
        responsesReceived = 0;
        commandsLost = 0;
        consecutiveErrors = 0;
        connectionRetries = 0;
        serialResets = 0;
        startTime = QDateTime::currentDateTime();
        sessionTimer.start();
    }
};

/**
 * @brief Performance thresholds for monitoring
 */
struct PerformanceThresholds {
    double commandLossThreshold = 0.30;  // 30% loss rate
    int maxConsecutiveErrors = 10;
    int maxSerialResets = 3;
    int maxConnectionRetries = 5;
    qint64 commandTrackingIntervalMs = 5000;  // 5 seconds
};

/**
 * @brief ARM architecture performance data
 */
struct ArmPerformanceData {
    bool isArmArchitecture = false;
    bool promptDisabled = false;
    int recommendedBaudrate = 9600;  // For ARM performance
    QString cpuArchitecture;
};

/**
 * @brief SerialStatistics manages performance metrics and diagnostics
 * 
 * This class provides centralized statistics collection and analysis for
 * serial port operations. It tracks:
 * - Command/response performance metrics
 * - Error rates and recovery statistics
 * - Connection stability metrics
 * - Architecture-specific performance recommendations
 * - Real-time performance monitoring with thresholds
 */
class SerialStatistics : public QObject
{
    Q_OBJECT

public:
    explicit SerialStatistics(QObject *parent = nullptr);
    ~SerialStatistics();

    // Statistics control
    void startTracking();
    void stopTracking();
    void resetStatistics();
    bool isTrackingEnabled() const;
    
    // Command tracking
    void recordCommandSent();
    void recordResponseReceived();
    void recordCommandLost();
    void recordConsecutiveError();
    void recordConnectionRetry();
    void recordSerialReset();
    void resetErrorCounters();
    
    // Data access
    StatisticsData getCurrentData() const;
    int getCommandsSent() const;
    int getResponsesReceived() const;
    int getCommandsLost() const;
    double getResponseRate() const;
    double getErrorRate() const;
    qint64 getElapsedMs() const;
    int getConsecutiveErrors() const;
    int getConnectionRetries() const;
    int getSerialResets() const;
    
    // Performance monitoring
    void setPerformanceThresholds(const PerformanceThresholds& thresholds);
    PerformanceThresholds getPerformanceThresholds() const;
    bool isPerformanceCritical() const;
    bool isRecoveryNeeded() const;
    
    // ARM architecture support
    void setArmPerformanceData(const ArmPerformanceData& data);
    ArmPerformanceData getArmPerformanceData() const;
    bool shouldRecommendBaudrateChange(int currentBaudrate) const;
    
    // Real-time monitoring
    void enablePerformanceMonitoring(bool enabled);
    void setMonitoringInterval(int intervalMs);
    
    // Diagnostics and reporting
    QString getPerformanceReport() const;
    QString getDetailedReport() const;
    bool exportStatistics(const QString& filePath) const;

signals:
    void statisticsUpdated(const StatisticsData& data);
    void performanceThresholdExceeded(const QString& metric, double value, double threshold);
    void recoveryRecommended(const QString& reason);
    void armBaudrateRecommendation(int currentBaudrate, int recommendedBaudrate);
    void criticalPerformanceDetected(const QString& details);

private slots:
    void onPerformanceMonitorTimeout();
    void analyzePerformance();

private:
    // Thread safety
    mutable QMutex m_statisticsMutex;
    
    // Core statistics data
    StatisticsData m_data;
    PerformanceThresholds m_thresholds;
    ArmPerformanceData m_armData;
    
    // State management
    std::atomic<bool> m_isTrackingEnabled{false};
    std::atomic<bool> m_isShuttingDown{false};
    
    // Performance monitoring
    QTimer* m_performanceMonitor;
    bool m_performanceMonitoringEnabled;
    
    // Last response timestamp to avoid duplicate counting from sync+async paths
    QElapsedTimer m_lastResponseTimer;

    // Internal helper methods
    void checkPerformanceThresholds();
    void emitPerformanceSignals();
    QString formatStatistics() const;
    void initializeArmDetection();
    
    // Constants
    static const int DEFAULT_MONITORING_INTERVAL = 5000; // 5 seconds
    static const int HIGH_FREQUENCY_MONITORING_INTERVAL = 1000; // 1 second for critical monitoring
};

#endif // SERIALSTATISTICS_H