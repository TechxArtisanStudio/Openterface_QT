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
    if (!force && !m_ready) {
        qCDebug(log_core_serial) << "Cannot send async command: not ready";
        return false;
    }
    
    if (m_isShuttingDown || !serialPort || !serialPort->isOpen()) {
        qCDebug(log_core_serial) << "Cannot send async command: shutting down or port not open";
        return false;
    }
    
    QByteArray command = data;
    emit dataSent(data);

    // Log TX using same format as RX: "TX (COM21@9600bps): <hex>"
    {
        QString portName = serialPort ? serialPort->portName() : QString();
        int baudrate = serialPort ? serialPort->baudRate() : 0;
        qCDebug(log_core_serial).nospace().noquote() << "TX (" << portName << "@" << baudrate << "bps): " << data.toHex(' ');
        // Also log to diagnostics file if enabled
        if (SerialPortManager::getInstance().getSerialLogFilePath().contains("serial_log_diagnostics")) {
            SerialPortManager::getInstance().log(QString("TX (%1@%2bps): %3").arg(portName).arg(baudrate).arg(QString(data.toHex(' '))));
        }
    }

    command.append(calculateChecksum(command));

    // Statistics tracking
    if (m_isStatsEnabled) {
        m_statsSent++;
    }

    // Check command delay
    if (m_lastCommandTime.isValid() && m_lastCommandTime.elapsed() < m_commandDelayMs) {
        int remainingDelay = m_commandDelayMs - m_lastCommandTime.elapsed();
        qCDebug(log_core_serial) << "Delaying command by" << remainingDelay << "ms";
        
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
        qCDebug(log_core_serial) << "Cannot send sync command: not ready";
        return QByteArray();
    }
    
    if (m_isShuttingDown || !serialPort || !serialPort->isOpen()) {
        qCDebug(log_core_serial) << "Cannot send sync command: shutting down or port not open";
        return QByteArray();
    }
    
    // Add bounds checking for data array access
    if (data.size() < 4) {
        qCWarning(log_core_serial) << "Command data too small:" << data.size();
        return QByteArray();
    }
    
    emit dataSent(data);
    QByteArray command = data;
    
    const int commandCode = static_cast<unsigned char>(data[3]);
    
    // Log TX using same format as RX: "TX (COM21@9600bps): <hex>"
    {
        QString portName = serialPort ? serialPort->portName() : QString();
        int baudrate = serialPort ? serialPort->baudRate() : 0;
        qCDebug(log_core_serial).nospace().noquote() << "TX (" << portName << "@" << baudrate << "bps): " << command.toHex(' ');
        // Also explicitly log command send to file during diagnostics
        if (SerialPortManager::getInstance().getSerialLogFilePath().contains("serial_log_diagnostics")) {
            SerialPortManager::getInstance().log(QString("TX (%1@%2bps): %3").arg(portName).arg(baudrate).arg(QString(command.toHex(' '))));
        }
    }

    serialPort->readAll(); // Clear any existing data in the buffer before sending command
    command.append(calculateChecksum(command));
    
    if (!executeCommand(serialPort, command)) {
        qCWarning(log_core_serial) << "Failed to execute sync command";
        return QByteArray();
    }
    
    // Use helper to wait for and collect the sync response
    QByteArray responseData = collectSyncResponse(serialPort, timeoutMs, 100);

    // Verify response command code matches expected
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
            // Log actual error to file during diagnostics
            if (SerialPortManager::getInstance().getSerialLogFilePath().contains("serial_log_diagnostics")) {
                int baudrate = serialPort ? serialPort->baudRate() : 0;
                SerialPortManager::getInstance().log(QString("RX (%1): %2 (ERROR: Code mismatch - expected 0x%3, received 0x%4)")
                                                      .arg(baudrate)
                                                      .arg(QString(responseData.toHex(' ')))
                                                      .arg(expectedResponseCode, 2, 16, QChar('0'))
                                                      .arg(responseCode, 2, 16, QChar('0')));
            }
        } else {
            qCDebug(log_core_serial) << "Command code verified:" 
                                       << QString("0x%1").arg(commandCode, 2, 16, QChar('0'));
            // Log successful response to file during diagnostics
            if (SerialPortManager::getInstance().getSerialLogFilePath().contains("serial_log_diagnostics")) {
                int baudrate = serialPort ? serialPort->baudRate() : 0;
                SerialPortManager::getInstance().log(QString("RX (%1): %2").arg(baudrate).arg(QString(responseData.toHex(' '))));
            }
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
        qCWarning(log_core_serial) << "Invalid response size:" << responseData.size();
        
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
            qCDebug(log_core_serial) << "Collected" << newData.size() << "bytes, total:" << responseData.size();
            
            // If we have enough data to determine packet length, update expected length
            // Protocol header: [0]=0x57 [1]=0xAB [2]=addr [3]=cmd [4]=len
            const int HEADER_MIN = 5; // need at least up to len byte
            if (responseData.size() >= HEADER_MIN && expectedResponseLength == MIN_PACKET_SIZE) {
                int lenField = static_cast<unsigned char>(responseData[4]);
                // Total packet size = header(5) + payload(lenField) + checksum(1) == lenField + 6
                int total = lenField + 6;
                if (total > MIN_PACKET_SIZE && total <= MAX_ACCEPTABLE_PACKET) {
                    expectedResponseLength = total;
                    qCDebug(log_core_serial) << "Updated expected response length to:" << expectedResponseLength;
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

    qCDebug(log_core_serial) << "collectSyncResponse: Total response size after wait:" << responseData.size() 
                               << "Data:" << responseData.toHex(' ');
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
        
        // Record command sent in statistics
        if (m_statistics) {
            m_statistics->recordCommandSent();
        }
        
        // Legacy statistics tracking
        if (m_isStatsEnabled) {
            m_statsSent++;
        }
        
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