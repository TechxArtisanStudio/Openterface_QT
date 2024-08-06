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

#include "serialportevents.h"
#include <QObject>
#include <QSerialPort>
#include <QThread>
#include <QTimer>
#include <QLoggingCategory>

#include "ch9329.h"

Q_DECLARE_LOGGING_CATEGORY(log_core_serial)

class SerialPortManager : public QObject
{
    Q_OBJECT

public:
    static const int ORIGINAL_BAUDRATE = 9600;
    static const int DEFAULT_BAUDRATE = 115200;

    static SerialPortManager& getInstance() {
        static SerialPortManager instance; // Guaranteed to be destroyed, instantiated on first use.
        return instance;
    }

    SerialPortManager(SerialPortManager const&) = delete; // Don't Implement
    void operator=(SerialPortManager const&) = delete; // Don't implement

    virtual ~SerialPortManager(); // Declare the destructor

    void setEventCallback(SerialPortEventCallback* callback);
    bool openPort(const QString &portName, int baudRate);
    void closePort();
    bool restartPort();

    bool writeData(const QByteArray &data);
    bool sendAsyncCommand(const QByteArray &data, bool force);
    QByteArray sendSyncCommand(const QByteArray &data, bool force);
    bool sendResetCommand();
    bool resetHipChip();
    bool reconfigureHidChip();
    bool factoryResetHipChip();

signals:
    void dataReceived(const QByteArray &data);
    void serialPortConnected(const QString &portName);
    void serialPortDisconnected(const QString &portName);
    void serialPortConnectionSuccess(const QString &portName);

private slots:
    void checkSerialPort();

    void observeSerialPortNotification();
    void readData();
    void aboutToClose();
    void bytesWritten(qint64 bytes);

    static quint8 calculateChecksum(const QByteArray &data);
    //void checkSerialPortConnection();

    void checkSerialPorts();

    void onSerialPortConnected(const QString &portName);
    void onSerialPortDisconnected(const QString &portName);
    void onSerialPortConnectionSuccess(const QString &portName);

private:
    SerialPortManager(QObject *parent = nullptr);
    QSerialPort *serialPort;

    QSet<QString> availablePorts;

    // int baudrate = ORIGINAL_BAUDRATE;

    QThread *serialThread;
    QTimer *serialTimer;

    QList<QSerialPortInfo> m_lastPortList;
    bool ready = false;
    SerialPortEventCallback* eventCallback = nullptr;

};

#endif // SERIALPORTMANAGER_H
