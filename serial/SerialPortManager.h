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

#ifndef SERIALPORTMANAGER_H
#define SERIALPORTMANAGER_H

#include "../ui/statusevents.h"
#include <QObject>
#include <QSerialPort>
#include <QThread>
#include <QTimer>
#include <QLoggingCategory>
#include <QDateTime>
#include <QElapsedTimer>
#include <QMutex>
#include <QQueue>
#include <QWaitCondition>
#include <atomic>

#include "ch9329.h"

Q_DECLARE_LOGGING_CATEGORY(log_core_serial)

// Forward declaration
class DeviceInfo;

class SerialPortManager : public QObject
{
    Q_OBJECT

public:
    static const int BAUDRATE_HIGHSPEED = 115200;
    static const int BAUDRATE_LOWSPEED = 9600;
    // Set the default baudrate to 9600 for better compatibility
    static const int DEFAULT_BAUDRATE = BAUDRATE_LOWSPEED;
    static const int SERIAL_TIMER_INTERVAL = 5000;
    
    static SerialPortManager& getInstance() {
        static SerialPortManager instance; // Guaranteed to be destroyed, instantiated on first use.
        return instance;
    }
    
    

    SerialPortManager(SerialPortManager const&) = delete; // Don't Implement
    void operator=(SerialPortManager const&) = delete; // Don't implement

    virtual ~SerialPortManager(); // Declare the destructor

    void setEventCallback(StatusEventCallback* callback);
    bool openPort(const QString &portName, int baudRate);
    void closePort();
    bool restartPort();

    bool getNumLockState(){return NumLockState;};
    bool getCapsLockState(){return CapsLockState;};
    bool getScrollLockState(){return ScrollLockState;};

    bool writeData(const QByteArray &data);
    bool sendAsyncCommand(const QByteArray &data, bool force);
    bool sendResetCommand();
    QByteArray sendSyncCommand(const QByteArray &data, bool force);

    bool resetHipChip(int targetBaudrate = DEFAULT_BAUDRATE);
    bool reconfigureHidChip(int targetBaudrate = DEFAULT_BAUDRATE);
    bool factoryResetHipChipV191();
    bool factoryResetHipChip();
    void restartSwitchableUSB();
    void setUSBconfiguration(int targetBaudrate = DEFAULT_BAUDRATE);
    void changeUSBDescriptor();
    bool setBaudRate(int baudrate);
    void setUserSelectedBaudrate(int baudrate); // Set baudrate from user menu selection
    void clearStoredBaudrate(); // Clear stored baudrate setting
    
    // ARM architecture detection and performance prompt
    static bool isArmArchitecture();
    void checkArmBaudratePerformance(int baudrate); // Check and emit signal if needed
    void setCommandDelay(int delayMs);  // set the delay
    void stop(); //stop the serial port manager

    // DeviceManager integration methods
    void checkDeviceConnections(const QList<DeviceInfo>& devices);
    
    // Serial port switching by port chain (similar to CameraManager and VideoHid)
    bool switchSerialPortByPortChain(const QString& portChain);
    QString getCurrentSerialPortPath() const;
    QString getCurrentSerialPortChain() const;
    
    // Hotplug monitoring integration
    void connectToHotplugMonitor();
    void disconnectFromHotplugMonitor();
    
    // Enhanced stability features
    void enableAutoRecovery(bool enable = true);
    void setMaxRetryAttempts(int maxRetries);
    void setMaxConsecutiveErrors(int maxErrors);
    bool isConnectionStable() const;
    int getConsecutiveErrorCount() const;
    int getConnectionRetryCount() const;
    void forceRecovery();
    
    // Get current baudrate
    int getCurrentBaudrate() const;
    
    // Data buffer management
    void clearIncompleteDataBuffer();
    
signals:
    void dataReceived(const QByteArray &data);
    void dataSent(const QByteArray &data);
    void serialPortConnected(const QString &portName);
    void serialPortDisconnected(const QString &portName);
    void serialPortConnectionSuccess(const QString &portName);
    void sendCommandAsync(const QByteArray &command, bool waitForAck);
    void connectedPortChanged(const QString &portName, const int &baudrate);
    void serialPortSwitched(const QString& fromPortChain, const QString& toPortChain);
    void serialPortDeviceChanged(const QString& oldPortPath, const QString& newPortPath);
    void armBaudratePerformanceRecommendation(int currentBaudrate); // Signal for ARM performance recommendation
    void parameterConfigurationSuccess(); // Signal emitted when parameter configuration is successful and reset is needed
    
private slots:
    void checkSerialPort();
    void observeSerialPortNotification();
    void readData();
    void bytesWritten(qint64 bytes);

    static quint8 calculateChecksum(const QByteArray &data);
    //void checkSerialPortConnection();

    // Removed: void checkSerialPorts();
    void initializeSerialPortFromPortChain();

    // /*
    //  * Check if the USB switch status
    //  * CH340 DSR pin is connected to the hard USB toggle switch,
    //  * HIGH value means connecting to host, while LOW value means connecting to target
    //  */
    // void checkSwitchableUSB();

    void onSerialPortConnected(const QString &portName);
    void onSerialPortDisconnected(const QString &portName);
    void onSerialPortConnectionSuccess(const QString &portName);
    
    
private:
    SerialPortManager(QObject *parent = nullptr);
    QSerialPort *serialPort;

    void sendCommand(const QByteArray &command, bool waitForAck);

    QSet<QString> availablePorts;
    
    QThread *serialThread;
    QTimer *serialTimer;

    QList<QSerialPortInfo> m_lastPortList;
    std::atomic<bool> ready = false;
    StatusEventCallback* eventCallback = nullptr;
    bool isSwitchToHost = false;
    bool isTargetUsbConnected = false;
    bool NumLockState;
    bool CapsLockState;
    bool ScrollLockState;
    void updateSpecialKeyState(uint8_t data);
    QDateTime lastSerialPortCheckTime;
    
    // Variable to store the latest update time
    QDateTime latestUpdateTime;
    QElapsedTimer m_lastCommandTime;  // New member for timing
    int m_commandDelayMs;  // New member for configurable delay
    
    // Current serial port tracking
    QString m_currentSerialPortPath;
    QString m_currentSerialPortChain;
    
    // Enhanced stability members
    std::atomic<bool> m_isShuttingDown = false;
    std::atomic<int> m_connectionRetryCount = 0;
    std::atomic<int> m_consecutiveErrors = 0;
    QTimer* m_connectionWatchdog;
    QTimer* m_errorRecoveryTimer;
    QMutex m_serialPortMutex;
    QQueue<QByteArray> m_commandQueue;
    QMutex m_commandQueueMutex;
    bool m_autoRecoveryEnabled = true;
    int m_maxRetryAttempts = 5;
    int m_maxConsecutiveErrors = 10;
    QElapsedTimer m_lastSuccessfulCommand;
    
    // Error frequency tracking for auto-disconnect mechanism
    std::atomic<int> m_errorCount = 0;
    QElapsedTimer m_errorTrackingTimer;
    bool m_errorHandlerDisconnected = false;
    static const int MAX_ERRORS_PER_SECOND = 5;
    
    // Data buffering for incomplete packets
    QByteArray m_incompleteDataBuffer;
    QMutex m_bufferMutex;
    static const int MAX_BUFFER_SIZE = 256; // Maximum buffer size to prevent memory issues
    
    // Sync command handling to prevent race conditions
    std::atomic<bool> m_pendingSyncCommand = false;
    QByteArray m_syncCommandResponse;
    QMutex m_syncResponseMutex;
    QWaitCondition m_syncResponseCondition;
    
    // Enhanced error handling
    void handleSerialError(QSerialPort::SerialPortError error);
    void attemptRecovery();
    void resetErrorCounters();
    bool isRecoveryNeeded() const;
    void setupConnectionWatchdog();
    void stopConnectionWatchdog();
    int anotherBaudrate();
    QString statusCodeToString(uint8_t status);
};

#endif // SERIALPORTMANAGER_H
