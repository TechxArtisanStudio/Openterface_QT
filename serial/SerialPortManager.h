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
#include "../device/DeviceManager.h"
#include "../device/DeviceInfo.h"
#include "../device/HotplugMonitor.h"
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

    // Device management methods
    DeviceManager* getDeviceManager() const { return m_deviceManager; }
    HotplugMonitor* getHotplugMonitor() const { return m_hotplugMonitor; }
    QList<DeviceInfo> getAvailableDevices();
    QStringList getAvailablePortChains();
    QStringList getDevicePortChains() const;
    bool selectDeviceByPortChain(const QString& portChain);
    DeviceInfo getCurrentSelectedDevice() const;
    QString formatDeviceInfo(const DeviceInfo& device) const;
    
    // Enhanced complete device management
    bool selectCompleteDevice(const QString& portChain);
    bool switchToDevice(const QString& portChain);
    bool switchPhysicalDevice(const DeviceInfo& fromDevice, const DeviceInfo& toDevice);
    DeviceInfo getCurrentCompleteDevice() const;
    QStringList getActiveDeviceInterfaces() const;
    bool isDeviceCompletelyAvailable(const QString& portChain) const;
    
    // Physical device interface management
    void activateDeviceInterfaces(const DeviceInfo& device);
    void deactivateCurrentDevice();
    bool isDeviceCurrentlyActive(const QString& portChain) const;

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
    
    // Debug and diagnostic methods
    void debugDeviceStatus() const;

    
signals:
    void dataReceived(const QByteArray &data);
    void dataSent(const QByteArray &data);
    void serialPortConnected(const QString &portName);
    void serialPortDisconnected(const QString &portName);
    void serialPortConnectionSuccess(const QString &portName);
    void sendCommandAsync(const QByteArray &command, bool waitForAck);
    void connectedPortChanged(const QString &portName, const int &baudrate);
    
    // Enhanced device management signals
    void deviceInventoryChanged(int deviceCount, bool hasSelectedDevice);
    void noDevicesAvailable();
    
    // Physical device interface signals
    void hidDeviceAvailable(const QString& hidDevicePath);
    void hidDeviceDisconnected();
    void cameraDeviceAvailable(const QString& cameraDevicePath);
    void cameraDeviceDisconnected();
    void audioDeviceAvailable(const QString& audioDevicePath);
    void audioDeviceDisconnected();
    void physicalDeviceSwitched(const QString& fromPortChain, const QString& toPortChain);
    void completeDeviceSelected(const DeviceInfo& device);
    void deviceInterfacesActivated(const DeviceInfo& device);
    void deviceInterfacesDeactivated(const DeviceInfo& device);
    
private slots:
    void checkSerialPort();
    void observeSerialPortNotification();
    void readData();
    void bytesWritten(qint64 bytes);

    static quint8 calculateChecksum(const QByteArray &data);
    //void checkSerialPortConnection();

    void checkSerialPorts();

    // Device hotplug event handlers
    void onDeviceAdded(const DeviceInfo& device);
    void onDeviceRemoved(const DeviceInfo& device);
    void onDeviceModified(const DeviceInfo& oldDevice, const DeviceInfo& newDevice);
    
    // Enhanced hotplug event handler
    void onHotplugDeviceChangeEvent(const DeviceChangeEvent& event);
    void handleSelectedDeviceRemoval(const DeviceInfo& removedDevice);

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

    // Device management
    DeviceManager* m_deviceManager;
    HotplugMonitor* m_hotplugMonitor;
    DeviceInfo m_selectedDevice;
    QString m_selectedPortChain;

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

    QString statusCodeToString(uint8_t status);
};

#endif // SERIALPORTMANAGER_H
