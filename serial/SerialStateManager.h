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

#ifndef SERIALSTATEMANAGER_H
#define SERIALSTATEMANAGER_H

#include <QObject>
#include <QString>
#include <QDateTime>
#include <QElapsedTimer>
#include <QMutex>
#include <atomic>

// Include necessary headers for enums
#include "watchdog/ConnectionWatchdog.h" // For ConnectionState

// Forward declarations - include SerialPortManager.h for ChipType enum
#include "SerialPortManager.h"

/**
 * @brief Key states structure
 */
struct KeyStates {
    bool numLock = false;
    bool capsLock = false;
    bool scrollLock = false;
    
    bool operator==(const KeyStates& other) const {
        return numLock == other.numLock && 
               capsLock == other.capsLock && 
               scrollLock == other.scrollLock;
    }
    
    bool operator!=(const KeyStates& other) const {
        return !(*this == other);
    }
};

/**
 * @brief USB switch state enumeration
 */
enum class UsbSwitchState {
    Unknown,
    ToHost,
    ToTarget
};

/**
 * @brief Serial port information structure
 */
struct SerialPortInfo {
    QString portPath;
    QString portChain;
    int baudRate = 9600;
    ChipType chipType = ChipType::UNKNOWN;
    
    bool isValid() const {
        return !portPath.isEmpty() && !portChain.isEmpty();
    }
};

/**
 * @brief Error tracking information
 */
struct ErrorTrackingInfo {
    int consecutiveErrors = 0;
    int connectionRetryCount = 0;
    QElapsedTimer lastSuccessfulCommand;
    QElapsedTimer errorTrackingTimer;
    bool autoRecoveryEnabled = true;
    int maxRetryAttempts = 5;
    int maxConsecutiveErrors = 10;
    
    void reset() {
        consecutiveErrors = 0;
        connectionRetryCount = 0;
        lastSuccessfulCommand.restart();
    }
};

/**
 * @brief SerialStateManager manages all state information for the serial port connection
 * 
 * This class centralizes state management to improve maintainability and provide
 * a clear interface for state access and modification. It handles:
 * - Connection state tracking
 * - Serial port information management
 * - Key states (NumLock, CapsLock, ScrollLock)
 * - USB switch state tracking
 * - Error tracking and recovery state
 * - Thread-safe state access
 */
class SerialStateManager : public QObject
{
    Q_OBJECT

public:
    explicit SerialStateManager(QObject *parent = nullptr);
    ~SerialStateManager() = default;

    // Connection state management
    ConnectionState getConnectionState() const;
    void setConnectionState(ConnectionState state);
    bool isReady() const { return getConnectionState() == ConnectionState::Connected; }
    bool isConnected() const { 
        auto state = getConnectionState();
        return state == ConnectionState::Connected; 
    }
    
    // Serial port information
    SerialPortInfo getSerialPortInfo() const;
    void setSerialPortInfo(const SerialPortInfo& info);
    void setPortPath(const QString& path);
    void setPortChain(const QString& chain);
    void setBaudRate(int baudRate);
    void setChipType(ChipType chipType);
    
    QString getCurrentPortPath() const;
    QString getCurrentPortChain() const;
    int getCurrentBaudRate() const;
    ChipType getCurrentChipType() const;
    
    // Key states management
    KeyStates getKeyStates() const;
    void setKeyStates(const KeyStates& states);
    void updateKeyStates(uint8_t keyStateData);
    bool getNumLockState() const;
    bool getCapsLockState() const;
    bool getScrollLockState() const;
    
    // USB switch state management
    UsbSwitchState getUsbSwitchState() const;
    void setUsbSwitchState(UsbSwitchState state);
    bool isTargetUsbConnected() const;
    void setTargetUsbConnected(bool connected);
    
    // Error tracking and recovery state
    ErrorTrackingInfo getErrorTrackingInfo() const;
    void setErrorTrackingInfo(const ErrorTrackingInfo& info);
    void incrementConsecutiveErrors();
    void incrementConnectionRetryCount();
    void resetErrorCounters();
    void updateLastSuccessfulCommand();
    
    int getConsecutiveErrorCount() const;
    int getConnectionRetryCount() const;
    bool isAutoRecoveryEnabled() const;
    void setAutoRecoveryEnabled(bool enabled);
    void setMaxRetryAttempts(int maxRetries);
    void setMaxConsecutiveErrors(int maxErrors);
    
    // Shutdown state
    bool isShuttingDown() const { return m_isShuttingDown; }
    void setShuttingDown(bool shuttingDown);
    
    // State validation and diagnostics
    bool isStateValid() const;
    QString getStateDescription() const;
    void clearAllStates();

signals:
    void connectionStateChanged(ConnectionState newState, ConnectionState oldState);
    void serialPortInfoChanged(const SerialPortInfo& newInfo, const SerialPortInfo& oldInfo);
    void keyStatesChanged(bool numLock, bool capsLock, bool scrollLock);
    void usbSwitchStateChanged(UsbSwitchState newState, UsbSwitchState oldState);
    void targetUsbStatusChanged(bool isConnected);
    void errorCountersChanged(int consecutiveErrors, int retryCount);
    void recoveryStateChanged(bool isRecovering);

private:
    // Thread safety
    mutable QMutex m_stateMutex;
    
    // Connection state
    std::atomic<ConnectionState> m_connectionState{ConnectionState::Disconnected};
    
    // Serial port information
    SerialPortInfo m_serialPortInfo;
    
    // Key states
    KeyStates m_keyStates;
    
    // USB switch state
    std::atomic<UsbSwitchState> m_usbSwitchState{UsbSwitchState::Unknown};
    std::atomic<bool> m_isTargetUsbConnected{false};
    
    // Error tracking
    ErrorTrackingInfo m_errorTrackingInfo;
    
    // Shutdown state
    std::atomic<bool> m_isShuttingDown{false};
    
    // Helper methods
    void emitStateChangedSignals(ConnectionState oldState, const SerialPortInfo& oldInfo, 
                                const KeyStates& oldKeyStates, UsbSwitchState oldUsbState);
};

#endif // SERIALSTATEMANAGER_H