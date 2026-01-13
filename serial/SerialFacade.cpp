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

#include "SerialFacade.h"
#include "SerialPortManager.h"
#include "../ui/statusevents.h"
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(log_serial_facade, "opf.serial.facade")

SerialFacade::SerialFacade(QObject *parent)
    : QObject(parent)
    , m_serialManager(&SerialPortManager::getInstance())
    , m_currentStatus(SerialConnectionStatus::Disconnected)
    , m_defaultTimeoutMs(1000)
{
    qCDebug(log_serial_facade) << "SerialFacade initialized";
    connectSerialManagerSignals();
}

SerialFacade::~SerialFacade()
{
    qCDebug(log_serial_facade) << "SerialFacade destroyed";
    if (isConnected()) {
        disconnect();
    }
}

// ============================================================================
// Core Connection Management - Simplified Interface
// ============================================================================

bool SerialFacade::connectToPort(const QString& portName, int baudrate)
{
    qCDebug(log_serial_facade) << "Connecting to port:" << portName << "baudrate:" << baudrate;
    
    if (portName.isEmpty()) {
        qCWarning(log_serial_facade) << "Cannot connect: empty port name";
        return false;
    }
    
    updateConnectionStatus(SerialConnectionStatus::Connecting, portName);
    
    // Use auto-detection if baudrate not specified
    int targetBaudrate = (baudrate <= 0) ? SerialPortManager::DEFAULT_BAUDRATE : baudrate;
    
    bool success = false;
    
    // Try port chain switching first (for multi-device scenarios)
    if (portName.contains("|") || portName.contains("-")) {
        success = m_serialManager->switchSerialPortByPortChain(portName);
    } else {
        // Direct port connection
        success = m_serialManager->openPort(portName, targetBaudrate);
    }
    
    if (success) {
        qCDebug(log_serial_facade) << "Connection successful to:" << portName;
        updateConnectionStatus(SerialConnectionStatus::Connected, portName);
    } else {
        qCWarning(log_serial_facade) << "Connection failed to:" << portName;
        updateConnectionStatus(SerialConnectionStatus::Error, portName);
    }
    
    return success;
}

void SerialFacade::disconnect()
{
    qCDebug(log_serial_facade) << "Disconnecting from serial port";
    
    if (m_serialManager) {
        m_serialManager->closePort();
    }
    
    updateConnectionStatus(SerialConnectionStatus::Disconnected);
}

bool SerialFacade::isConnected() const
{
    return m_currentStatus == SerialConnectionStatus::Connected;
}

SerialConnectionStatus SerialFacade::getConnectionStatus() const
{
    return m_currentStatus;
}

QString SerialFacade::getCurrentPort() const
{
    if (m_serialManager && isConnected()) {
        return m_serialManager->getCurrentSerialPortPath();
    }
    return QString();
}

int SerialFacade::getCurrentBaudrate() const
{
    if (m_serialManager && isConnected()) {
        return m_serialManager->getCurrentBaudrate();
    }
    return 0;
}

// ============================================================================
// Command Execution - Simplified Interface
// ============================================================================

bool SerialFacade::sendCommand(const QByteArray& data)
{
    if (!isConnected() || data.isEmpty()) {
        qCDebug(log_serial_facade) << "Cannot send command: not connected or empty data";
        return false;
    }
    
    bool success = m_serialManager->sendAsyncCommand(data, false);
    qCDebug(log_serial_facade) << "Async command sent:" << data.toHex(' ') << "Success:" << success;
    
    return success;
}

QByteArray SerialFacade::sendCommandSync(const QByteArray& data, int timeoutMs)
{
    if (!isConnected() || data.isEmpty()) {
        qCDebug(log_serial_facade) << "Cannot send sync command: not connected or empty data";
        return QByteArray();
    }
    
    int timeout = (timeoutMs > 0) ? timeoutMs : m_defaultTimeoutMs;
    QByteArray response = m_serialManager->sendSyncCommand(data, false);
    
    qCDebug(log_serial_facade) << "Sync command sent:" << data.toHex(' ') 
                               << "Response size:" << response.size();
    
    return response;
}

bool SerialFacade::sendRawData(const QByteArray& data)
{
    if (!isConnected() || data.isEmpty()) {
        qCDebug(log_serial_facade) << "Cannot send raw data: not connected or empty data";
        return false;
    }
    
    bool success = m_serialManager->writeData(data);
    qCDebug(log_serial_facade) << "Raw data sent:" << data.toHex(' ') << "Success:" << success;
    
    return success;
}

// ============================================================================
// Device Control - Simplified Interface
// ============================================================================

bool SerialFacade::resetDevice(int newBaudrate)
{
    if (!isConnected()) {
        qCDebug(log_serial_facade) << "Cannot reset device: not connected";
        return false;
    }
    
    int targetBaudrate = (newBaudrate > 0) ? newBaudrate : SerialPortManager::DEFAULT_BAUDRATE;
    bool success = m_serialManager->resetHipChip(targetBaudrate);
    
    qCDebug(log_serial_facade) << "Device reset:" << (success ? "successful" : "failed") 
                               << "Target baudrate:" << targetBaudrate;
    
    return success;
}

bool SerialFacade::factoryReset()
{
    if (!isConnected()) {
        qCDebug(log_serial_facade) << "Cannot factory reset: not connected";
        return false;
    }
    
    // Use synchronous factory reset for better reliability
    bool success = m_serialManager->factoryResetHipChipSync();
    
    qCDebug(log_serial_facade) << "Factory reset:" << (success ? "successful" : "failed");
    
    return success;
}

bool SerialFacade::switchUSB(bool toHost)
{
    if (!isConnected()) {
        qCDebug(log_serial_facade) << "Cannot switch USB: not connected";
        return false;
    }
    
    if (toHost) {
        m_serialManager->switchUsbToHostViaSerial();
    } else {
        m_serialManager->switchUsbToTargetViaSerial();
    }
    
    qCDebug(log_serial_facade) << "USB switched to:" << (toHost ? "host" : "target");
    return true; // Switch commands are generally fire-and-forget
}

SerialFacade::KeyStates SerialFacade::getKeyStates() const
{
    KeyStates states;
    
    if (m_serialManager && isConnected()) {
        states.numLock = m_serialManager->getNumLockState();
        states.capsLock = m_serialManager->getCapsLockState();
        states.scrollLock = m_serialManager->getScrollLockState();
    }
    
    return states;
}

// ============================================================================
// Statistics and Monitoring - Simplified Interface
// ============================================================================

void SerialFacade::startStatistics()
{
    if (m_serialManager) {
        m_serialManager->startStats();
        qCDebug(log_serial_facade) << "Statistics tracking started";
    }
}

void SerialFacade::stopStatistics()
{
    if (m_serialManager) {
        m_serialManager->stopStats();
        qCDebug(log_serial_facade) << "Statistics tracking stopped";
    }
}

void SerialFacade::resetStatistics()
{
    if (m_serialManager) {
        m_serialManager->resetStats();
        qCDebug(log_serial_facade) << "Statistics reset";
    }
}

SerialStats SerialFacade::getStatistics() const
{
    if (!m_serialManager) {
        return SerialStats();
    }
    
    return SerialStats(
        m_serialManager->getCommandsSent(),
        m_serialManager->getResponsesReceived(),
        m_serialManager->getResponseRate(),
        m_serialManager->getStatsElapsedMs(),
        true  // Assume tracking if we're getting data
    );
}

bool SerialFacade::isConnectionStable() const
{
    if (!m_serialManager || !isConnected()) {
        return false;
    }
    
    return m_serialManager->isConnectionStable();
}

// ============================================================================
// Configuration - Simplified Interface
// ============================================================================

void SerialFacade::setAutoRecovery(bool enabled)
{
    if (m_serialManager) {
        m_serialManager->enableAutoRecovery(enabled);
        qCDebug(log_serial_facade) << "Auto recovery" << (enabled ? "enabled" : "disabled");
    }
}

void SerialFacade::setCommandDelay(int delayMs)
{
    if (m_serialManager) {
        m_serialManager->setCommandDelay(delayMs);
        qCDebug(log_serial_facade) << "Command delay set to:" << delayMs << "ms";
    }
}

void SerialFacade::setDefaultTimeout(int timeoutMs)
{
    m_defaultTimeoutMs = qMax(100, timeoutMs); // Minimum 100ms timeout
    qCDebug(log_serial_facade) << "Default timeout set to:" << m_defaultTimeoutMs << "ms";
}

// ============================================================================
// Advanced Access - For Complex Use Cases
// ============================================================================

SerialPortManager& SerialFacade::getSerialPortManager()
{
    Q_ASSERT(m_serialManager);
    return *m_serialManager;
}

void SerialFacade::setEventCallback(StatusEventCallback* callback)
{
    if (m_serialManager) {
        m_serialManager->setEventCallback(callback);
        qCDebug(log_serial_facade) << "Event callback" << (callback ? "set" : "cleared");
    }
}

// ============================================================================
// Private Slots - Signal Conversion
// ============================================================================

void SerialFacade::onSerialConnectionChanged(bool connected)
{
    SerialConnectionStatus newStatus = connected ? 
        SerialConnectionStatus::Connected : 
        SerialConnectionStatus::Disconnected;
        
    if (newStatus != m_currentStatus) {
        updateConnectionStatus(newStatus, connected ? getCurrentPort() : QString());
    }
}

void SerialFacade::onSerialDataReceived(const QByteArray& data)
{
    emit dataReceived(data);
    qCDebug(log_serial_facade) << "Data received:" << data.size() << "bytes";
}

void SerialFacade::onSerialStatusUpdate(const QString& status)
{
    qCDebug(log_serial_facade) << "Status update:" << status;
    
    // Map status strings to connection states
    if (status.contains("recovery", Qt::CaseInsensitive) || 
        status.contains("recovering", Qt::CaseInsensitive)) {
        updateConnectionStatus(SerialConnectionStatus::Recovering);
    } else if (status.contains("error", Qt::CaseInsensitive) ||
               status.contains("failed", Qt::CaseInsensitive)) {
        updateConnectionStatus(SerialConnectionStatus::Error);
    }
}

void SerialFacade::onSerialKeyStatesChanged(bool numLock, bool capsLock, bool scrollLock)
{
    KeyStates states;
    states.numLock = numLock;
    states.capsLock = capsLock;
    states.scrollLock = scrollLock;
    
    emit keyStatesChanged(states);
    qCDebug(log_serial_facade) << "Key states changed - NumLock:" << numLock 
                               << "CapsLock:" << capsLock 
                               << "ScrollLock:" << scrollLock;
}

void SerialFacade::onSerialUSBStatusChanged(bool connectedToHost)
{
    emit usbSwitchChanged(connectedToHost);
    qCDebug(log_serial_facade) << "USB switch changed to:" << (connectedToHost ? "host" : "target");
}

// ============================================================================
// Private Helper Methods
// ============================================================================

void SerialFacade::connectSerialManagerSignals()
{
    if (!m_serialManager) {
        return;
    }
    
    // Connect SerialPortManager signals to our slots for conversion to facade signals
    QObject::connect(m_serialManager, &SerialPortManager::dataReceived,
            this, &SerialFacade::onSerialDataReceived);
    
    QObject::connect(m_serialManager, &SerialPortManager::statusUpdate,
            this, &SerialFacade::onSerialStatusUpdate);
    
    QObject::connect(m_serialManager, &SerialPortManager::keyStatesChanged,
            this, &SerialFacade::onSerialKeyStatesChanged);
    
    QObject::connect(m_serialManager, &SerialPortManager::targetUSBStatus,
            this, &SerialFacade::onSerialUSBStatusChanged);
    
    // Connect to connection-related signals (these may not exist yet, so ignore if they fail)
    QObject::connect(m_serialManager, &SerialPortManager::serialPortConnected,
            this, [this](const QString& portName) {
                Q_UNUSED(portName);
                onSerialConnectionChanged(true);
            });
    
    QObject::connect(m_serialManager, &SerialPortManager::serialPortDisconnected,
            this, [this]() {
                onSerialConnectionChanged(false);
            });
    
    qCDebug(log_serial_facade) << "SerialPortManager signals connected to facade";
}

SerialConnectionStatus SerialFacade::mapToFacadeStatus(bool connected, bool ready, bool recovering) const
{
    if (recovering) {
        return SerialConnectionStatus::Recovering;
    }
    if (connected && ready) {
        return SerialConnectionStatus::Connected;
    }
    if (connected && !ready) {
        return SerialConnectionStatus::Connecting;
    }
    return SerialConnectionStatus::Disconnected;
}

void SerialFacade::updateConnectionStatus(SerialConnectionStatus newStatus, const QString& portName)
{
    if (newStatus != m_currentStatus) {
        SerialConnectionStatus oldStatus = m_currentStatus;
        m_currentStatus = newStatus;
        
        qCDebug(log_serial_facade) << "Connection status changed from" 
                                  << static_cast<int>(oldStatus) << "to" 
                                  << static_cast<int>(newStatus)
                                  << "port:" << portName;
        
        emit connectionStatusChanged(newStatus, portName);
    }
}