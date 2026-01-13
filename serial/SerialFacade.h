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

#ifndef SERIALFACADE_H
#define SERIALFACADE_H

#include <QObject>
#include <QString>
#include <QByteArray>

// Forward declarations
class SerialPortManager;
class StatusEventCallback;

/**
 * @brief Connection status enumeration for simplified interface
 */
enum class SerialConnectionStatus {
    Disconnected,
    Connecting,
    Connected,
    Error,
    Recovering
};

/**
 * @brief Simple statistics structure for facade interface
 */
struct SerialStats {
    int commandsSent = 0;
    int responsesReceived = 0;
    double responseRate = 0.0;
    qint64 elapsedMs = 0;
    bool isTracking = false;
    
    SerialStats() = default;
    SerialStats(int sent, int received, double rate, qint64 elapsed, bool tracking)
        : commandsSent(sent), responsesReceived(received), responseRate(rate)
        , elapsedMs(elapsed), isTracking(tracking) {}
};

/**
 * @brief SerialFacade provides a simplified interface to serial port operations
 * 
 * This facade pattern implementation hides the complexity of SerialPortManager and its
 * various components (CommandCoordinator, StateManager, Statistics, etc.) behind a 
 * clean, simple API. It's designed for common use cases while still allowing access 
 * to the full SerialPortManager for advanced operations.
 * 
 * Key design goals:
 * - Simplify the most common serial port operations
 * - Hide internal complexity and refactored components
 * - Provide sensible defaults for configuration
 * - Maintain backward compatibility through delegation
 * - Support both synchronous and asynchronous operations
 * 
 * Usage examples:
 * @code
 * SerialFacade serial;
 * 
 * // Simple connection
 * serial.connect("COM3");
 * 
 * // Send commands
 * serial.sendCommand(data);
 * 
 * // Get status
 * if (serial.isConnected()) {
 *     auto stats = serial.getStatistics();
 * }
 * @endcode
 */
class SerialFacade : public QObject
{
    Q_OBJECT

public:
    explicit SerialFacade(QObject *parent = nullptr);
    ~SerialFacade();

    // ============================================================================
    // Core Connection Management - Simplified Interface
    // ============================================================================
    
    /**
     * @brief Connect to serial port with automatic configuration
     * @param portName Port name (e.g., "COM3", "/dev/ttyUSB0") or port chain
     * @param baudrate Optional baudrate (defaults to auto-detection)
     * @return true if connection initiated successfully
     */
    bool connectToPort(const QString& portName, int baudrate = 0);
    
    /**
     * @brief Disconnect from current serial port
     */
    void disconnect();
    
    /**
     * @brief Check if currently connected to a serial port
     * @return true if connected and ready for communication
     */
    bool isConnected() const;
    
    /**
     * @brief Get current connection status
     * @return Connection status enum
     */
    SerialConnectionStatus getConnectionStatus() const;
    
    /**
     * @brief Get current port information
     * @return Port name or empty string if not connected
     */
    QString getCurrentPort() const;
    
    /**
     * @brief Get current baudrate
     * @return Baudrate or 0 if not connected
     */
    int getCurrentBaudrate() const;

    // ============================================================================
    // Command Execution - Simplified Interface
    // ============================================================================
    
    /**
     * @brief Send command asynchronously (fire and forget)
     * @param data Command data to send
     * @return true if command was queued successfully
     */
    bool sendCommand(const QByteArray& data);
    
    /**
     * @brief Send command and wait for response
     * @param data Command data to send
     * @param timeoutMs Timeout in milliseconds (default: 1000ms)
     * @return Response data or empty array if failed/timeout
     */
    QByteArray sendCommandSync(const QByteArray& data, int timeoutMs = 1000);
    
    /**
     * @brief Send raw data without command formatting
     * @param data Raw data to send
     * @return true if sent successfully
     */
    bool sendRawData(const QByteArray& data);

    // ============================================================================
    // Device Control - Simplified Interface
    // ============================================================================
    
    /**
     * @brief Reset the HID chip with automatic configuration
     * @param newBaudrate Target baudrate (0 = auto-detect)
     * @return true if reset successful
     */
    bool resetDevice(int newBaudrate = 0);
    
    /**
     * @brief Perform factory reset (automatically detects chip version)
     * @return true if factory reset successful
     */
    bool factoryReset();
    
    /**
     * @brief Switch USB connection between host and target
     * @param toHost true to switch to host, false for target
     * @return true if switch successful
     */
    bool switchUSB(bool toHost);
    
    /**
     * @brief Get current LED key states
     * @return Struct with NumLock, CapsLock, ScrollLock states
     */
    struct KeyStates {
        bool numLock = false;
        bool capsLock = false;
        bool scrollLock = false;
    };
    KeyStates getKeyStates() const;

    // ============================================================================
    // Statistics and Monitoring - Simplified Interface
    // ============================================================================
    
    /**
     * @brief Start statistics tracking
     */
    void startStatistics();
    
    /**
     * @brief Stop statistics tracking
     */
    void stopStatistics();
    
    /**
     * @brief Reset statistics counters
     */
    void resetStatistics();
    
    /**
     * @brief Get current statistics
     * @return Statistics structure with current data
     */
    SerialStats getStatistics() const;
    
    /**
     * @brief Check if connection is stable (low error rate)
     * @return true if connection is stable
     */
    bool isConnectionStable() const;

    // ============================================================================
    // Configuration - Simplified Interface
    // ============================================================================
    
    /**
     * @brief Enable/disable automatic recovery on connection issues
     * @param enabled true to enable auto-recovery
     */
    void setAutoRecovery(bool enabled);
    
    /**
     * @brief Set command delay for timing-sensitive operations
     * @param delayMs Delay in milliseconds between commands
     */
    void setCommandDelay(int delayMs);
    
    /**
     * @brief Set timeout for synchronous operations
     * @param timeoutMs Default timeout in milliseconds
     */
    void setDefaultTimeout(int timeoutMs);

    // ============================================================================
    // Advanced Access - For Complex Use Cases
    // ============================================================================
    
    /**
     * @brief Get direct access to SerialPortManager for advanced operations
     * @return Reference to the underlying SerialPortManager
     * @warning Use with caution - bypasses facade abstractions
     */
    SerialPortManager& getSerialPortManager();

    /**
     * @brief Set event callback for status updates
     * @param callback Callback object for status events
     */
    void setEventCallback(StatusEventCallback* callback);

signals:
    /**
     * @brief Emitted when connection status changes
     * @param status New connection status
     * @param portName Port name or empty if disconnected
     */
    void connectionStatusChanged(SerialConnectionStatus status, const QString& portName);
    
    /**
     * @brief Emitted when data is received
     * @param data Received data
     */
    void dataReceived(const QByteArray& data);
    
    /**
     * @brief Emitted when command execution completes
     * @param command Command that was executed
     * @param success true if successful
     */
    void commandCompleted(const QByteArray& command, bool success);
    
    /**
     * @brief Emitted when statistics are updated
     * @param stats Current statistics
     */
    void statisticsUpdated(const SerialStats& stats);
    
    /**
     * @brief Emitted when USB switch status changes
     * @param connectedToHost true if USB is connected to host
     */
    void usbSwitchChanged(bool connectedToHost);
    
    /**
     * @brief Emitted when key states change
     * @param keyStates Current key states
     */
    void keyStatesChanged(const KeyStates& keyStates);

private slots:
    // Internal slots for SerialPortManager signal conversion
    void onSerialConnectionChanged(bool connected);
    void onSerialDataReceived(const QByteArray& data);
    void onSerialStatusUpdate(const QString& status);
    void onSerialKeyStatesChanged(bool numLock, bool capsLock, bool scrollLock);
    void onSerialUSBStatusChanged(bool connectedToHost);

private:
    SerialPortManager* m_serialManager;
    SerialConnectionStatus m_currentStatus;
    int m_defaultTimeoutMs;
    
    // Internal helper methods
    void connectSerialManagerSignals();
    SerialConnectionStatus mapToFacadeStatus(bool connected, bool ready, bool recovering) const;
    void updateConnectionStatus(SerialConnectionStatus newStatus, const QString& portName = QString());
};

#endif // SERIALFACADE_H