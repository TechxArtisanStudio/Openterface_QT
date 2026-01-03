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

#ifndef FFMPEG_HOTPLUG_HANDLER_H
#define FFMPEG_HOTPLUG_HANDLER_H

#include <QObject>
#include <QString>
#include <QTimer>

// Forward declarations
class HotplugMonitor;
class FFmpegDeviceValidator;
struct DeviceInfo;

/**
 * @brief Handles hotplug device monitoring and activation/deactivation for FFmpeg backend
 * 
 * This class encapsulates hotplug monitoring logic including:
 * - Connecting to system hotplug monitor
 * - Detecting device plug/unplug events by port chain
 * - Waiting for device activation with timeout
 * - Managing device state transitions (activation/deactivation)
 * - Coordinating with device validator for device availability checks
 */
class FFmpegHotplugHandler : public QObject
{
    Q_OBJECT

public:
    explicit FFmpegHotplugHandler(FFmpegDeviceValidator* validator, QObject* parent = nullptr);
    ~FFmpegHotplugHandler();

    // Hotplug monitor connection
    void ConnectToHotplugMonitor();
    void DisconnectFromHotplugMonitor();
    
    // Device waiting and activation
    void WaitForDeviceActivation(const QString& devicePath = QString(), int timeoutMs = 30000);
    void HandleDeviceActivation(const QString& devicePath, const QString& portChain = QString());
    void HandleDeviceDeactivation(const QString& devicePath);
    
    // Device state management
    void SetCurrentDevicePortChain(const QString& portChain);
    void SetCurrentDevice(const QString& devicePath);
    void SetCaptureRunning(bool running);
    void SetSuppressErrors(bool suppress);
    
    // State queries
    QString GetCurrentDevicePortChain() const { return current_device_port_chain_; }
    QString GetCurrentDevice() const { return current_device_; }
    bool IsWaitingForDevice() const { return waiting_for_device_; }

signals:
    // Device state signals - forwarded to FFmpegBackendHandler
    void DeviceActivated(const QString& devicePath);
    void DeviceDeactivated(const QString& devicePath);
    void WaitingForDevice(const QString& devicePath);
    void CaptureError(const QString& error);
    
    // Request capture operations from parent handler
    void RequestStartCapture(const QString& devicePath, const QSize& resolution, int framerate);
    void RequestStopCapture();

private slots:
    void OnDevicePluggedIn(const DeviceInfo& device);
    void OnDeviceUnplugged(const DeviceInfo& device);
    void OnDeviceWaitTimeout();
    void OnDeviceCheckTimer();

private:
    void ProcessDevicePluggedIn(const QString& devicePath, const QString& portChain);
    void RetryDeviceActivationAfterDelay(const QString& portChain);
    
    // Device validator for availability checks
    FFmpegDeviceValidator* device_validator_;
    
    // Hotplug monitor connection
    HotplugMonitor* hotplug_monitor_;
    
    // Device state
    QString current_device_;
    QString current_device_port_chain_;
    QString expected_device_path_;
    bool waiting_for_device_;
    bool capture_running_;
    bool suppress_errors_;
    
    // Timers
    QTimer* device_wait_timer_;
    QTimer* device_check_timer_;
};

#endif // FFMPEG_HOTPLUG_HANDLER_H
