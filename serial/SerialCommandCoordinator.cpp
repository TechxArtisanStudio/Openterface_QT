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
#include "SerialPortManager.h"
#include <QTimer>
#include <QLoggingCategory>
#include <QEventLoop>
#include <QElapsedTimer>

// Declare the unified serial logging category (defined in SerialPortManager.cpp)
Q_DECLARE_LOGGING_CATEGORY(log_core_serial)

// Consolidated helper methods to eliminate duplication
void SerialCommandCoordinator::logTransaction(const QString& direction, const QByteArray& data, 
                                            QSerialPort* port, bool includeInDiagnostics) {
    QString portName = port ? port->portName() : QString();
    int baudrate = port ? port->baudRate() : 0;
    
    // Single debug log
    qCDebug(log_core_serial).nospace().noquote() 
        << direction << " (" << portName << "@" << baudrate << "bps): " 
        << data.toHex(' ');
        
    // Single diagnostics file write if needed
    if (includeInDiagnostics && shouldLogToDiagnostics()) {
        SerialPortManager::getInstance().log(
            QString("%1 (%2@%3bps): %4")
            .arg(direction).arg(portName).arg(baudrate).arg(QString(data.toHex(' '))));
    }
}

bool SerialCommandCoordinator::shouldLogToDiagnostics() const {
    return SerialPortManager::getInstance().getSerialLogFilePath().contains("serial_log_diagnostics");
}

bool SerialCommandCoordinator::validateCommandPreconditions(QSerialPort* port, bool force) {
    if (m_isShuttingDown) {
        qCDebug(log_core_serial) << "Cannot send command: shutting down";
        return false;
    }
    if (!port || !port->isOpen()) {
        qCDebug(log_core_serial) << "Cannot send command: port not open";
        return false;
    }
    if (!force && !m_ready) {
        qCDebug(log_core_serial) << "Cannot send command: not ready";
        return false;
    }
    return true;
}

void SerialCommandCoordinator::applyCommandDelay() {
    if (m_lastCommandTime.isValid() && m_lastCommandTime.elapsed() < m_commandDelayMs) {
        int remainingDelay = m_commandDelayMs - m_lastCommandTime.elapsed();
        qCDebug(log_core_serial) << "Delaying command by" << remainingDelay << "ms";
        
        QEventLoop loop;
        QTimer::singleShot(remainingDelay, &loop, &QEventLoop::quit);
        loop.exec();
    }
}

QByteArray SerialCommandCoordinator::prepareCommand(const QByteArray& data) {
    QByteArray command = data;
    command.append(calculateChecksum(command));
    return command;
}

void SerialCommandCoordinator::recordCommand(bool success) {
    // Unified statistics recording - avoid duplicate counters
    if (m_statistics) {
        m_statistics->recordCommandSent();
        if (success) {
            m_statistics->recordResponseReceived();
        }
    }
    
    // Legacy statistics for backward compatibility
    if (m_isStatsEnabled) {
        m_statsSent++;
        if (success) {
            m_statsReceived++;
        }
    }
}

SerialCommandCoordinator::SerialCommandCoordinator(QObject *parent)
    : QObject(parent)
    , m_commandDelayMs(0)
    , m_statsStartTime(QDateTime::currentDateTime())
{
    qCDebug(log_core_serial) << "SerialCommandCoordinator initialized";
    m_lastCommandTime.start();
}

SerialCommandCoordinator::~SerialCommandCoordinator()
{
    qCDebug(log_core_serial) << "SerialCommandCoordinator destroyed";
    m_isShuttingDown = true;
    clearCommandQueue();
}

bool SerialCommandCoordinator::sendAsyncCommand(QSerialPort* serialPort, const QByteArray &data, bool force)
{
    if (!validateCommandPreconditions(serialPort, force)) {
        return false;
    }
    
    emit dataSent(data);
    
    // Consolidated logging
    logTransaction("TX", data, serialPort);
    
    // Apply command delay
    applyCommandDelay();
    
    // Prepare command with checksum
    QByteArray command = prepareCommand(data);
    
    bool result = executeCommand(serialPort, command);
    m_lastCommandTime.start();
    
    // Record command statistics (unified)
    recordCommand(result);
    
    emit commandExecuted(data, result);
    return result;
}

QByteArray SerialCommandCoordinator::sendSyncCommand(QSerialPort* serialPort, const QByteArray &data, bool force, int timeoutMs)
{
    if (!validateCommandPreconditions(serialPort, force)) {
        return QByteArray();
    }
    
    // Add bounds checking for data array access
    if (data.size() < 4) {
        qCWarning(log_core_serial) << "Command data too small:" << data.size();
        return QByteArray();
    }
    
    emit dataSent(data);
    const int commandCode = static_cast<unsigned char>(data[3]);
    
    // Consolidated TX logging
    logTransaction("TX", data, serialPort);
    
    serialPort->readAll(); // Clear any existing data in the buffer before sending command
    QByteArray command = prepareCommand(data);
    
    if (!executeCommand(serialPort, command)) {
        qCWarning(log_core_serial) << "Failed to execute sync command";
        return QByteArray();
    }
    
    // Use helper to wait for and collect the sync response
    QByteArray responseData = collectSyncResponse(serialPort, timeoutMs, 100);

    // Verify response command code matches expected
    bool validResponse = false;
    if (responseData.size() >= 4) {
        int responseCode = static_cast<unsigned char>(responseData[3]);
        int expectedResponseCode = commandCode | 0x80; // Response code is command code + 0x80
        if (responseCode != expectedResponseCode) {
            qCWarning(log_core_serial) << "Command code mismatch - sent:" 
                                        << QString("0x%1").arg(commandCode, 2, 16, QChar('0'))
                                        << "received:" 
                                        << QString("0x%1").arg(responseCode, 2, 16, QChar('0'))
                                        << "expected:"
                                        << QString("0x%1").arg(expectedResponseCode, 2, 16, QChar('0'));
            // Log error with consolidated logging
            if (shouldLogToDiagnostics()) {
                int baudrate = serialPort ? serialPort->baudRate() : 0;
                SerialPortManager::getInstance().log(QString("RX (%1): %2 (ERROR: Code mismatch - expected 0x%3, received 0x%4)")
                                                      .arg(baudrate)
                                                      .arg(QString(responseData.toHex(' ')))
                                                      .arg(expectedResponseCode, 2, 16, QChar('0'))
                                                      .arg(responseCode, 2, 16, QChar('0')));
            }
        } else {
            validResponse = true;
            qCDebug(log_core_serial) << "Command code verified:" 
                                       << QString("0x%1").arg(commandCode, 2, 16, QChar('0'));
        }
        
        // Consolidated RX logging
        logTransaction("RX", responseData, serialPort);
    } else {
        qCWarning(log_core_serial) << "Invalid response size:" << responseData.size();
    }
    
    // Unified statistics recording
    recordCommand(validResponse);

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
    qCDebug(log_core_serial) << "Command delay set to:" << m_commandDelayMs << "ms";
}

void SerialCommandCoordinator::startStats()
{
    m_isStatsEnabled = true;
    m_statsSent = 0;
    m_statsReceived = 0;
    m_statsStartTime = QDateTime::currentDateTime();
    qCDebug(log_core_serial) << "Command statistics tracking started";
}

void SerialCommandCoordinator::stopStats()
{
    m_isStatsEnabled = false;
    qCDebug(log_core_serial) << "Command statistics tracking stopped";
    emit statisticsUpdated(m_statsSent, m_statsReceived, getResponseRate());
}

void SerialCommandCoordinator::resetStats()
{
    m_statsSent = 0;
    m_statsReceived = 0;
    m_statsStartTime = QDateTime::currentDateTime();
    qCDebug(log_core_serial) << "Command statistics reset";
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
    qCDebug(log_core_serial) << "Command queue cleared";
}

int SerialCommandCoordinator::getQueueSize() const
{
    QMutexLocker locker(&m_commandQueueMutex);
    return m_commandQueue.size();
}

QByteArray SerialCommandCoordinator::collectSyncResponse(QSerialPort* serialPort, int totalTimeoutMs, int waitStepMs)
{
    if (!serialPort || !serialPort->isOpen()) {
        qCWarning(log_core_serial) << "Cannot collect response: port not available";
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
            // qCDebug(log_core_serial) << "Collected" << newData.size() << "bytes, total:" << responseData.size();
            
            // If we have enough data to determine packet length, update expected length
            // Protocol header: [0]=0x57 [1]=0xAB [2]=addr [3]=cmd [4]=len
            const int HEADER_MIN = 5; // need at least up to len byte
            if (responseData.size() >= HEADER_MIN && expectedResponseLength == MIN_PACKET_SIZE) {
                int lenField = static_cast<unsigned char>(responseData[4]);
                // Total packet size = header(5) + payload(lenField) + checksum(1) == lenField + 6
                int total = lenField + 6;
                if (total > MIN_PACKET_SIZE && total <= MAX_ACCEPTABLE_PACKET) {
                    expectedResponseLength = total;
                    // qCDebug(log_core_serial) << "Updated expected response length to:" << expectedResponseLength;
                } else {
                    qCWarning(log_core_serial) << "Invalid packet length detected:" << total << "ignoring";
                }
            }
        }
        
        // Break if we have collected enough data or hit maximum acceptable packet size
        if (responseData.size() >= expectedResponseLength || responseData.size() >= MAX_ACCEPTABLE_PACKET) {
            break;
        }
    }

    if (!responseData.isEmpty()) {
        QString portName = serialPort ? serialPort->portName() : QString();
        int baudrate = serialPort ? serialPort->baudRate() : 0;
        qCDebug(log_core_serial).nospace().noquote() << "RX (" << portName << "@" << baudrate << "bps): " << responseData.toHex(' ');
        // Also write to diagnostics file if enabled
        if (SerialPortManager::getInstance().getSerialLogFilePath().contains("serial_log_diagnostics")) {
            SerialPortManager::getInstance().log(QString("RX (%1@%2bps): %3").arg(portName).arg(baudrate).arg(QString(responseData.toHex(' '))));
        }
    }
    
    return responseData;
}

bool SerialCommandCoordinator::executeCommand(QSerialPort* serialPort, const QByteArray &command)
{
    if (!serialPort || !serialPort->isOpen()) {
        qCWarning(log_core_serial) << "Cannot execute command: port not available";
        return false;
    }

    try {
        qint64 bytesWritten = serialPort->write(command);
        if (bytesWritten == -1) {
            qCWarning(log_core_serial) << "Failed to write command to serial port:" << serialPort->errorString();
            return false;
        }
        
        if (bytesWritten != command.size()) {
            qCWarning(log_core_serial) << "Incomplete write: expected" << command.size() 
                                         << "bytes, wrote" << bytesWritten;
            return false;
        }
        
        if (!serialPort->waitForBytesWritten(1000)) {
            qCWarning(log_core_serial) << "Timeout waiting for bytes to be written:" << serialPort->errorString();
            return false;
        }
        
        // Statistics recording is now handled by the calling function to avoid duplication
        
        return true;
        
    } catch (...) {
        qCCritical(log_core_serial) << "Exception while writing to serial port";
        return false;
    }
}

void SerialCommandCoordinator::processCommandQueue()
{
    // This method can be extended in the future for advanced queue processing
    // Currently, commands are processed immediately rather than queued
    qCDebug(log_core_serial) << "Processing command queue (currently immediate execution)";
}

void SerialCommandCoordinator::setStatisticsModule(SerialStatistics* statistics)
{
    m_statistics = statistics;
    qCDebug(log_core_serial) << "Statistics module" << (statistics ? "connected" : "disconnected");
}