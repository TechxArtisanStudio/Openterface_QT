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

#ifndef SERIALCOMMANDCOORDINATOR_H
#define SERIALCOMMANDCOORDINATOR_H

#include <QObject>
#include <QQueue>
#include <QMutex>
#include <QElapsedTimer>
#include <QSerialPort>
#include <QDateTime>
#include <atomic>

/**
 * @brief Command structure for queued operations
 */
struct SerialCommand {
    QByteArray data;
    bool isSync;
    bool force;
    qint64 timestamp;
    
    SerialCommand(const QByteArray& cmd = QByteArray(), bool sync = false, bool forceCmd = false)
        : data(cmd), isSync(sync), force(forceCmd), timestamp(QDateTime::currentMSecsSinceEpoch()) {}
};

/**
 * @brief SerialCommandCoordinator manages command queuing, synchronization, and response handling
 * 
 * This class extracts command-related functionality from SerialPortManager to improve
 * maintainability and separation of concerns. It handles:
 * - Command queuing and prioritization
 * - Synchronous/asynchronous command execution
 * - Response collection and timeout handling
 * - Command statistics and performance tracking
 * - Checksum calculation and validation
 */
class SerialCommandCoordinator : public QObject
{
    Q_OBJECT

public:
    explicit SerialCommandCoordinator(QObject *parent = nullptr);
    ~SerialCommandCoordinator();

    // Command execution methods
    bool sendAsyncCommand(QSerialPort* serialPort, const QByteArray &data, bool force = false);
    QByteArray sendSyncCommand(QSerialPort* serialPort, const QByteArray &data, bool force = false, int timeoutMs = 1000);
    
    // Command delay management
    void setCommandDelay(int delayMs);
    int getCommandDelay() const { return m_commandDelayMs; }
    
    // Statistics integration
    void setStatisticsModule(class SerialStatistics* statistics);
    
    // Statistics methods (legacy support)
    void startStats();
    void stopStats();
    void resetStats();
    double getResponseRate() const;
    qint64 getStatsElapsedMs() const;
    int getStatsSent() const { return m_statsSent; }
    int getStatsReceived() const { return m_statsReceived; }
    
    // Utility methods
    static quint8 calculateChecksum(const QByteArray &data);
    
    // State management
    void setReady(bool ready) { m_ready = ready; }
    bool isReady() const { return m_ready; }
    
    // Queue management
    void clearCommandQueue();
    int getQueueSize() const;

signals:
    void dataSent(const QByteArray &data);
    void dataReceived(const QByteArray &data);
    void commandExecuted(const QByteArray &command, bool success);
    void statisticsUpdated(int sent, int received, double responseRate);

private slots:
    void processCommandQueue();

private:
    // Response collection for sync commands
    QByteArray collectSyncResponse(QSerialPort* serialPort, int totalTimeoutMs, int waitStepMs = 100);
    
    // Internal command execution
    bool executeCommand(QSerialPort* serialPort, const QByteArray &command);
    
    // Command queue management
    QQueue<SerialCommand> m_commandQueue;
    mutable QMutex m_commandQueueMutex;
    
    // Timing and delay management
    QElapsedTimer m_lastCommandTime;
    int m_commandDelayMs;
    
    // Statistics integration
    class SerialStatistics* m_statistics = nullptr;
    
    // Statistics tracking (legacy support)
    std::atomic<bool> m_isStatsEnabled{false};
    std::atomic<int> m_statsSent{0};
    std::atomic<int> m_statsReceived{0};
    QDateTime m_statsStartTime;
    
    // State
    std::atomic<bool> m_ready{false};
    std::atomic<bool> m_isShuttingDown{false};
    
    // Constants
    static const int MAX_ACCEPTABLE_PACKET = 1024;
    static const int MIN_PACKET_SIZE = 6;
};

#endif // SERIALCOMMANDCOORDINATOR_H