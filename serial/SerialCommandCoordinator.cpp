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

#include "SerialCommandCoordinator.h"
#include "SerialStatistics.h"
#include <QTimer>
#include <QLoggingCategory>
#include <QEventLoop>
#include <QElapsedTimer>

Q_LOGGING_CATEGORY(log_serial_command, "opf.serial.command")

SerialCommandCoordinator::SerialCommandCoordinator(QObject *parent)
    : QObject(parent)
    , m_commandDelayMs(0)
    , m_statsStartTime(QDateTime::currentDateTime())
{
    qCDebug(log_serial_command) << "SerialCommandCoordinator initialized";
    m_lastCommandTime.start();
}

SerialCommandCoordinator::~SerialCommandCoordinator()
{
    qCDebug(log_serial_command) << "SerialCommandCoordinator destroyed";
    m_isShuttingDown = true;
    clearCommandQueue();
}

bool SerialCommandCoordinator::sendAsyncCommand(QSerialPort* serialPort, const QByteArray &data, bool force)
{
    if (!force && !m_ready) {
        qCDebug(log_serial_command) << "Cannot send async command: not ready";
        return false;
    }
    
    if (m_isShuttingDown || !serialPort || !serialPort->isOpen()) {
        qCDebug(log_serial_command) << "Cannot send async command: shutting down or port not open";
        return false;
    }
    
    QByteArray command = data;
    emit dataSent(data);
    command.append(calculateChecksum(command));

    // Statistics tracking
    if (m_isStatsEnabled) {
        m_statsSent++;
    }

    // Check command delay
    if (m_lastCommandTime.isValid() && m_lastCommandTime.elapsed() < m_commandDelayMs) {
        int remainingDelay = m_commandDelayMs - m_lastCommandTime.elapsed();
        qCDebug(log_serial_command) << "Delaying command by" << remainingDelay << "ms";
        
        QEventLoop loop;
        QTimer::singleShot(remainingDelay, &loop, &QEventLoop::quit);
        loop.exec();
    }

    bool result = executeCommand(serialPort, command);
    m_lastCommandTime.start();
    
    emit commandExecuted(data, result);
    return result;
}

QByteArray SerialCommandCoordinator::sendSyncCommand(QSerialPort* serialPort, const QByteArray &data, bool force, int timeoutMs)
{
    if (!force && !m_ready) {
        qCDebug(log_serial_command) << "Cannot send sync command: not ready";
        return QByteArray();
    }
    
    if (m_isShuttingDown || !serialPort || !serialPort->isOpen()) {
        qCDebug(log_serial_command) << "Cannot send sync command: shutting down or port not open";
        return QByteArray();
    }
    
    // Add bounds checking for data array access
    if (data.size() < 4) {
        qCWarning(log_serial_command) << "Command data too small:" << data.size();
        return QByteArray();
    }
    
    emit dataSent(data);
    QByteArray command = data;
    
    const int commandCode = static_cast<unsigned char>(data[3]);

    serialPort->readAll(); // Clear any existing data in the buffer before sending command
    command.append(calculateChecksum(command));
    
    if (!executeCommand(serialPort, command)) {
        qCWarning(log_serial_command) << "Failed to execute sync command";
        return QByteArray();
    }
    
    // Use helper to wait for and collect the sync response
    QByteArray responseData = collectSyncResponse(serialPort, timeoutMs, 100);

    // Verify response command code matches expected
    if (responseData.size() >= 4) {
        int responseCode = static_cast<unsigned char>(responseData[3]);
        if (responseCode != commandCode) {
            qCWarning(log_serial_command) << "Command code mismatch - sent:" 
                                        << QString("0x%1").arg(commandCode, 2, 16, QChar('0'))
                                        << "received:" 
                                        << QString("0x%1").arg(responseCode, 2, 16, QChar('0'));
        } else {
            qCDebug(log_serial_command) << "Command code verified:" 
                                       << QString("0x%1").arg(commandCode, 2, 16, QChar('0'));
        }
        
        // Statistics tracking for successful responses
        if (responseData.size() >= 4) {
            // Record successful response in statistics
            if (m_statistics) {
                m_statistics->recordResponseReceived();
            }
        }
        
        // Legacy statistics tracking
        if (m_isStatsEnabled) {
            m_statsSent++;
            m_statsReceived++;
        }
    } else {
        qCWarning(log_serial_command) << "Invalid response size:" << responseData.size();
        
        // Record command loss in statistics
        if (m_statistics) {
            m_statistics->recordCommandLost();
        }
        
        // Legacy statistics tracking
        if (m_isStatsEnabled) {
            m_statsSent++;  // Count failed commands too
        }
    }

    // Notify of received data
    if (!responseData.isEmpty()) {
        emit dataReceived(responseData);
    }
    
    emit commandExecuted(data, !responseData.isEmpty());
    return responseData;
}

void SerialCommandCoordinator::setCommandDelay(int delayMs)
{
    m_commandDelayMs = qMax(0, delayMs);
    qCDebug(log_serial_command) << "Command delay set to:" << m_commandDelayMs << "ms";
}

void SerialCommandCoordinator::startStats()
{
    m_isStatsEnabled = true;
    m_statsSent = 0;
    m_statsReceived = 0;
    m_statsStartTime = QDateTime::currentDateTime();
    qCDebug(log_serial_command) << "Command statistics tracking started";
}

void SerialCommandCoordinator::stopStats()
{
    m_isStatsEnabled = false;
    qCDebug(log_serial_command) << "Command statistics tracking stopped";
    emit statisticsUpdated(m_statsSent, m_statsReceived, getResponseRate());
}

void SerialCommandCoordinator::resetStats()
{
    m_statsSent = 0;
    m_statsReceived = 0;
    m_statsStartTime = QDateTime::currentDateTime();
    qCDebug(log_serial_command) << "Command statistics reset";
    emit statisticsUpdated(0, 0, 0.0);
}

double SerialCommandCoordinator::getResponseRate() const
{
    if (m_statsSent == 0) return 0.0;
    return (double)m_statsReceived / m_statsSent * 100.0;
}

qint64 SerialCommandCoordinator::getStatsElapsedMs() const
{
    return m_statsStartTime.msecsTo(QDateTime::currentDateTime());
}

quint8 SerialCommandCoordinator::calculateChecksum(const QByteArray &data)
{
    quint32 sum = 0;
    for (auto byte : data) {
        sum += static_cast<quint8>(byte);
    }
    return sum % 256;
}

void SerialCommandCoordinator::clearCommandQueue()
{
    QMutexLocker locker(&m_commandQueueMutex);
    m_commandQueue.clear();
    qCDebug(log_serial_command) << "Command queue cleared";
}

int SerialCommandCoordinator::getQueueSize() const
{
    QMutexLocker locker(&m_commandQueueMutex);
    return m_commandQueue.size();
}

QByteArray SerialCommandCoordinator::collectSyncResponse(QSerialPort* serialPort, int totalTimeoutMs, int waitStepMs)
{
    if (!serialPort || !serialPort->isOpen()) {
        qCWarning(log_serial_command) << "Cannot collect response: port not available";
        return QByteArray();
    }

    QElapsedTimer timer;
    timer.start();
    QByteArray responseData;

    int expectedResponseLength = MIN_PACKET_SIZE; // minimal header + checksum

    while (timer.elapsed() < totalTimeoutMs && responseData.size() < expectedResponseLength) {
        if (!serialPort->waitForReadyRead(waitStepMs)) {
            continue; // Timeout on this wait step, but continue if overall timeout not reached
        }

        QByteArray newData = serialPort->readAll();
        if (!newData.isEmpty()) {
            responseData.append(newData);
            qCDebug(log_serial_command) << "Collected" << newData.size() << "bytes, total:" << responseData.size();
            
            // If we have enough data to determine packet length, update expected length
            if (responseData.size() >= 2 && expectedResponseLength == MIN_PACKET_SIZE) {
                int packetLength = static_cast<unsigned char>(responseData[1]);
                if (packetLength > MIN_PACKET_SIZE && packetLength <= MAX_ACCEPTABLE_PACKET) {
                    expectedResponseLength = packetLength;
                    qCDebug(log_serial_command) << "Updated expected response length to:" << expectedResponseLength;
                }
            }
        }
        
        // Break if we have collected enough data or hit maximum acceptable packet size
        if (responseData.size() >= expectedResponseLength || responseData.size() >= MAX_ACCEPTABLE_PACKET) {
            break;
        }
    }

    qCDebug(log_serial_command) << "collectSyncResponse: Total response size after wait:" << responseData.size() 
                               << "Data:" << responseData.toHex(' ');
    return responseData;
}

bool SerialCommandCoordinator::executeCommand(QSerialPort* serialPort, const QByteArray &command)
{
    if (!serialPort || !serialPort->isOpen()) {
        qCWarning(log_serial_command) << "Cannot execute command: port not available";
        return false;
    }

    try {
        qint64 bytesWritten = serialPort->write(command);
        if (bytesWritten == -1) {
            qCWarning(log_serial_command) << "Failed to write command to serial port:" << serialPort->errorString();
            return false;
        }
        
        if (bytesWritten != command.size()) {
            qCWarning(log_serial_command) << "Incomplete write: expected" << command.size() 
                                         << "bytes, wrote" << bytesWritten;
            return false;
        }
        
        if (!serialPort->waitForBytesWritten(1000)) {
            qCWarning(log_serial_command) << "Timeout waiting for bytes to be written:" << serialPort->errorString();
            return false;
        }
        
        // Record command sent in statistics
        if (m_statistics) {
            m_statistics->recordCommandSent();
        }
        
        // Legacy statistics tracking
        if (m_isStatsEnabled) {
            m_statsSent++;
        }
        
        qCDebug(log_serial_command) << "Successfully wrote" << bytesWritten << "bytes to serial port";
        return true;
        
    } catch (...) {
        qCCritical(log_serial_command) << "Exception while writing to serial port";
        return false;
    }
}

void SerialCommandCoordinator::processCommandQueue()
{
    // This method can be extended in the future for advanced queue processing
    // Currently, commands are processed immediately rather than queued
    qCDebug(log_serial_command) << "Processing command queue (currently immediate execution)";
}

void SerialCommandCoordinator::setStatisticsModule(SerialStatistics* statistics)
{
    m_statistics = statistics;
    qCDebug(log_serial_command) << "Statistics module" << (statistics ? "connected" : "disconnected");
}