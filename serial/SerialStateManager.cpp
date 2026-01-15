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

#include "SerialStateManager.h"
#include "SerialPortManager.h" // For ChipType enum
#include <QLoggingCategory>
#include <QMutexLocker>
#include <QDebug>

Q_LOGGING_CATEGORY(log_serial_state, "opf.serial.state")

SerialStateManager::SerialStateManager(QObject *parent)
    : QObject(parent)
{
    qCDebug(log_serial_state) << "SerialStateManager initialized";
    
    // Initialize error tracking timers
    m_errorTrackingInfo.lastSuccessfulCommand.start();
    m_errorTrackingInfo.errorTrackingTimer.start();
}

// Connection state management
ConnectionState SerialStateManager::getConnectionState() const
{
    return m_connectionState.load();
}

void SerialStateManager::setConnectionState(ConnectionState state)
{
    ConnectionState oldState = m_connectionState.exchange(state);
    
    if (oldState != state) {
        qCDebug(log_serial_state) << "Connection state changed from" 
                                 << static_cast<int>(oldState) << "to" << static_cast<int>(state);
        emit connectionStateChanged(state, oldState);
    }
}

// Serial port information
SerialPortInfo SerialStateManager::getSerialPortInfo() const
{
    QMutexLocker locker(&m_stateMutex);
    return m_serialPortInfo;
}

void SerialStateManager::setSerialPortInfo(const SerialPortInfo& info)
{
    QMutexLocker locker(&m_stateMutex);
    SerialPortInfo oldInfo = m_serialPortInfo;
    m_serialPortInfo = info;
    locker.unlock();
    
    qCDebug(log_serial_state) << "Serial port info updated - Path:" << info.portPath 
                             << "Chain:" << info.portChain 
                             << "Baudrate:" << info.baudRate
                             << "Chip:" << static_cast<int>(info.chipType);
    
    if (oldInfo.portPath != info.portPath || oldInfo.portChain != info.portChain ||
        oldInfo.baudRate != info.baudRate || oldInfo.chipType != info.chipType) {
        emit serialPortInfoChanged(info, oldInfo);
    }
}

void SerialStateManager::setPortPath(const QString& path)
{
    QMutexLocker locker(&m_stateMutex);
    SerialPortInfo oldInfo = m_serialPortInfo;
    m_serialPortInfo.portPath = path;
    locker.unlock();
    
    if (oldInfo.portPath != path) {
        emit serialPortInfoChanged(m_serialPortInfo, oldInfo);
    }
}

void SerialStateManager::setPortChain(const QString& chain)
{
    QMutexLocker locker(&m_stateMutex);
    SerialPortInfo oldInfo = m_serialPortInfo;
    m_serialPortInfo.portChain = chain;
    locker.unlock();
    
    if (oldInfo.portChain != chain) {
        emit serialPortInfoChanged(m_serialPortInfo, oldInfo);
    }
}

void SerialStateManager::setBaudRate(int baudRate)
{
    QMutexLocker locker(&m_stateMutex);
    SerialPortInfo oldInfo = m_serialPortInfo;
    m_serialPortInfo.baudRate = baudRate;
    locker.unlock();
    
    if (oldInfo.baudRate != baudRate) {
        emit serialPortInfoChanged(m_serialPortInfo, oldInfo);
    }
}

void SerialStateManager::setChipType(ChipType chipType)
{
    QMutexLocker locker(&m_stateMutex);
    SerialPortInfo oldInfo = m_serialPortInfo;
    m_serialPortInfo.chipType = chipType;
    locker.unlock();
    
    if (oldInfo.chipType != chipType) {
        emit serialPortInfoChanged(m_serialPortInfo, oldInfo);
    }
}

QString SerialStateManager::getCurrentPortPath() const
{
    QMutexLocker locker(&m_stateMutex);
    return m_serialPortInfo.portPath;
}

QString SerialStateManager::getCurrentPortChain() const
{
    QMutexLocker locker(&m_stateMutex);
    return m_serialPortInfo.portChain;
}

int SerialStateManager::getCurrentBaudRate() const
{
    QMutexLocker locker(&m_stateMutex);
    return m_serialPortInfo.baudRate;
}

ChipType SerialStateManager::getCurrentChipType() const
{
    QMutexLocker locker(&m_stateMutex);
    return m_serialPortInfo.chipType;
}

// Key states management
KeyStates SerialStateManager::getKeyStates() const
{
    QMutexLocker locker(&m_stateMutex);
    return m_keyStates;
}

void SerialStateManager::setKeyStates(const KeyStates& states)
{
    QMutexLocker locker(&m_stateMutex);
    KeyStates oldStates = m_keyStates;
    m_keyStates = states;
    locker.unlock();
    
    if (oldStates != states) {
        qCDebug(log_serial_state) << "Key states changed - NumLock:" << states.numLock 
                                 << "CapsLock:" << states.capsLock 
                                 << "ScrollLock:" << states.scrollLock;
        emit keyStatesChanged(states.numLock, states.capsLock, states.scrollLock);
    }
}

void SerialStateManager::updateKeyStates(uint8_t keyStateData)
{
    KeyStates newStates;
    newStates.numLock = (keyStateData & 0b00000001) != 0;
    newStates.capsLock = (keyStateData & 0b00000010) != 0;
    newStates.scrollLock = (keyStateData & 0b00000100) != 0;
    
    setKeyStates(newStates);
}

bool SerialStateManager::getNumLockState() const
{
    QMutexLocker locker(&m_stateMutex);
    return m_keyStates.numLock;
}

bool SerialStateManager::getCapsLockState() const
{
    QMutexLocker locker(&m_stateMutex);
    return m_keyStates.capsLock;
}

bool SerialStateManager::getScrollLockState() const
{
    QMutexLocker locker(&m_stateMutex);
    return m_keyStates.scrollLock;
}

// USB switch state management
UsbSwitchState SerialStateManager::getUsbSwitchState() const
{
    return m_usbSwitchState.load();
}

void SerialStateManager::setUsbSwitchState(UsbSwitchState state)
{
    UsbSwitchState oldState = m_usbSwitchState.exchange(state);
    
    if (oldState != state) {
        qCDebug(log_serial_state) << "USB switch state changed from" 
                                 << static_cast<int>(oldState) << "to" << static_cast<int>(state);
        emit usbSwitchStateChanged(state, oldState);
    }
}

bool SerialStateManager::isTargetUsbConnected() const
{
    return m_isTargetUsbConnected.load();
}

void SerialStateManager::setTargetUsbConnected(bool connected)
{
    bool oldState = m_isTargetUsbConnected.exchange(connected);
    
    if (oldState != connected) {
        qCDebug(log_serial_state) << "Target USB connected state changed to:" << connected;
        emit targetUsbStatusChanged(connected);
    }
}

// Error tracking and recovery state
ErrorTrackingInfo SerialStateManager::getErrorTrackingInfo() const
{
    QMutexLocker locker(&m_stateMutex);
    return m_errorTrackingInfo;
}

void SerialStateManager::setErrorTrackingInfo(const ErrorTrackingInfo& info)
{
    QMutexLocker locker(&m_stateMutex);
    ErrorTrackingInfo oldInfo = m_errorTrackingInfo;
    m_errorTrackingInfo = info;
    locker.unlock();
    
    if (oldInfo.consecutiveErrors != info.consecutiveErrors || 
        oldInfo.connectionRetryCount != info.connectionRetryCount) {
        emit errorCountersChanged(info.consecutiveErrors, info.connectionRetryCount);
    }
}

void SerialStateManager::incrementConsecutiveErrors()
{
    QMutexLocker locker(&m_stateMutex);
    int oldErrors = m_errorTrackingInfo.consecutiveErrors;
    m_errorTrackingInfo.consecutiveErrors++;
    int newErrors = m_errorTrackingInfo.consecutiveErrors;
    int retryCount = m_errorTrackingInfo.connectionRetryCount;
    locker.unlock();
    
    qCDebug(log_serial_state) << "Consecutive errors incremented to:" << newErrors;
    emit errorCountersChanged(newErrors, retryCount);
}

void SerialStateManager::incrementConnectionRetryCount()
{
    QMutexLocker locker(&m_stateMutex);
    int errors = m_errorTrackingInfo.consecutiveErrors;
    int oldRetryCount = m_errorTrackingInfo.connectionRetryCount;
    m_errorTrackingInfo.connectionRetryCount++;
    int newRetryCount = m_errorTrackingInfo.connectionRetryCount;
    locker.unlock();
    
    qCDebug(log_serial_state) << "Connection retry count incremented to:" << newRetryCount;
    emit errorCountersChanged(errors, newRetryCount);
}

void SerialStateManager::resetErrorCounters()
{
    QMutexLocker locker(&m_stateMutex);
    m_errorTrackingInfo.reset();
    locker.unlock();
    
    qCDebug(log_serial_state) << "Error counters reset";
    emit errorCountersChanged(0, 0);
}

void SerialStateManager::updateLastSuccessfulCommand()
{
    QMutexLocker locker(&m_stateMutex);
    m_errorTrackingInfo.lastSuccessfulCommand.restart();
    qCDebug(log_serial_state) << "Last successful command timestamp updated";
}

int SerialStateManager::getConsecutiveErrorCount() const
{
    QMutexLocker locker(&m_stateMutex);
    return m_errorTrackingInfo.consecutiveErrors;
}

int SerialStateManager::getConnectionRetryCount() const
{
    QMutexLocker locker(&m_stateMutex);
    return m_errorTrackingInfo.connectionRetryCount;
}

bool SerialStateManager::isAutoRecoveryEnabled() const
{
    QMutexLocker locker(&m_stateMutex);
    return m_errorTrackingInfo.autoRecoveryEnabled;
}

void SerialStateManager::setAutoRecoveryEnabled(bool enabled)
{
    QMutexLocker locker(&m_stateMutex);
    m_errorTrackingInfo.autoRecoveryEnabled = enabled;
    qCDebug(log_serial_state) << "Auto recovery" << (enabled ? "enabled" : "disabled");
}

void SerialStateManager::setMaxRetryAttempts(int maxRetries)
{
    QMutexLocker locker(&m_stateMutex);
    m_errorTrackingInfo.maxRetryAttempts = qMax(1, maxRetries);
    qCDebug(log_serial_state) << "Max retry attempts set to:" << m_errorTrackingInfo.maxRetryAttempts;
}

void SerialStateManager::setMaxConsecutiveErrors(int maxErrors)
{
    QMutexLocker locker(&m_stateMutex);
    m_errorTrackingInfo.maxConsecutiveErrors = qMax(1, maxErrors);
    qCDebug(log_serial_state) << "Max consecutive errors set to:" << m_errorTrackingInfo.maxConsecutiveErrors;
}

// Shutdown state
void SerialStateManager::setShuttingDown(bool shuttingDown)
{
    bool oldState = m_isShuttingDown.exchange(shuttingDown);
    
    if (oldState != shuttingDown) {
        qCDebug(log_serial_state) << "Shutting down state changed to:" << shuttingDown;
    }
}

// State validation and diagnostics
bool SerialStateManager::isStateValid() const
{
    QMutexLocker locker(&m_stateMutex);
    
    // Basic validation checks
    bool hasValidPortInfo = !m_serialPortInfo.portPath.isEmpty() && !m_serialPortInfo.portChain.isEmpty();
    bool hasValidBaudRate = m_serialPortInfo.baudRate > 0;
    bool hasValidChipType = m_serialPortInfo.chipType != ChipType::UNKNOWN;
    
    return hasValidPortInfo && hasValidBaudRate && hasValidChipType;
}

QString SerialStateManager::getStateDescription() const
{
    QMutexLocker locker(&m_stateMutex);
    
    QString description;
    description += QString("Connection: %1, ").arg(static_cast<int>(m_connectionState.load()));
    description += QString("Port: %1, ").arg(m_serialPortInfo.portPath);
    description += QString("Chain: %1, ").arg(m_serialPortInfo.portChain);
    description += QString("Baudrate: %1, ").arg(m_serialPortInfo.baudRate);
    description += QString("Chip: %1, ").arg(static_cast<int>(m_serialPortInfo.chipType));
    description += QString("Keys: N:%1 C:%2 S:%3, ").arg(m_keyStates.numLock).arg(m_keyStates.capsLock).arg(m_keyStates.scrollLock);
    description += QString("USB: %1, ").arg(static_cast<int>(m_usbSwitchState.load()));
    description += QString("Errors: %1, ").arg(m_errorTrackingInfo.consecutiveErrors);
    description += QString("Retries: %1").arg(m_errorTrackingInfo.connectionRetryCount);
    
    return description;
}

void SerialStateManager::clearAllStates()
{
    qCDebug(log_serial_state) << "Clearing all states";
    
    setConnectionState(ConnectionState::Disconnected);
    
    QMutexLocker locker(&m_stateMutex);
    m_serialPortInfo = SerialPortInfo();
    m_keyStates = KeyStates();
    m_errorTrackingInfo.reset();
    locker.unlock();
    
    m_usbSwitchState = UsbSwitchState::Unknown;
    m_isTargetUsbConnected = false;
    
    // Emit change signals
    emit serialPortInfoChanged(m_serialPortInfo, SerialPortInfo());
    emit keyStatesChanged(false, false, false);
    emit usbSwitchStateChanged(UsbSwitchState::Unknown, UsbSwitchState::Unknown);
    emit targetUsbStatusChanged(false);
    emit errorCountersChanged(0, 0);
}