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

#include "ch9329.h"

Q_DECLARE_LOGGING_CATEGORY(log_core_serial)

class SerialPortManager : public QObject
{
    Q_OBJECT

public:
    static const int ORIGINAL_BAUDRATE = 9600;
    static const int DEFAULT_BAUDRATE = 115200;
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

    bool resetHipChip();
    bool reconfigureHidChip();
    bool factoryResetHipChipV191();
    bool factoryResetHipChip();
    void restartSwitchableUSB();
    void setUSBconfiguration();
    void changeUSBDescriptor();
    bool setBaudRate(int baudrate);
    void setCommandDelay(int delayMs);  // set the delay
    void stop(); //stop the serial port manager

    
signals:
    void dataReceived(const QByteArray &data);
    void dataSent(const QByteArray &data);
    void serialPortConnected(const QString &portName);
    void serialPortDisconnected(const QString &portName);
    void serialPortConnectionSuccess(const QString &portName);
    void sendCommandAsync(const QByteArray &command, bool waitForAck);
    void connectedPortChanged(const QString &portName, const int &baudrate);

private slots:
    void checkSerialPort();

    void observeSerialPortNotification();
    void readData();
    void bytesWritten(qint64 bytes);

    static quint8 calculateChecksum(const QByteArray &data);
    //void checkSerialPortConnection();

    void checkSerialPorts();

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

    // int baudrate = ORIGINAL_BAUDRATE;

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

    void enableNotifier();
    
};

#endif // SERIALPORTMANAGER_H
