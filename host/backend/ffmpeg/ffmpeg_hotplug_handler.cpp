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

#include "ffmpeg_hotplug_handler.h"
#include "ffmpeg_device_validator.h"
#include "device/DeviceManager.h"
#include "device/HotplugMonitor.h"
#include "device/DeviceInfo.h"

#include <QLoggingCategory>
#include <QDebug>
#include <QTimer>
#include <QMediaDevices>
#include <QCameraDevice>

Q_DECLARE_LOGGING_CATEGORY(log_ffmpeg_backend)

FFmpegHotplugHandler::FFmpegHotplugHandler(FFmpegDeviceValidator* validator, QObject* parent)
    : QObject(parent)
    , device_validator_(validator)
    , hotplug_monitor_(nullptr)
    , waiting_for_device_(false)
    , capture_running_(false)
    , suppress_errors_(false)
    , device_wait_timer_(nullptr)
    , device_check_timer_(nullptr)
{
    // Initialize device wait timer
    device_wait_timer_ = new QTimer(this);
    device_wait_timer_->setSingleShot(true);
    connect(device_wait_timer_, &QTimer::timeout, this, &FFmpegHotplugHandler::OnDeviceWaitTimeout);
    
    // Initialize device check timer
    device_check_timer_ = new QTimer(this);
    device_check_timer_->setInterval(1000); // Check every second
    connect(device_check_timer_, &QTimer::timeout, this, &FFmpegHotplugHandler::OnDeviceCheckTimer);
}

FFmpegHotplugHandler::~FFmpegHotplugHandler()
{
    DisconnectFromHotplugMonitor();
}

void FFmpegHotplugHandler::ConnectToHotplugMonitor()
{
    qCDebug(log_ffmpeg_backend) << "FFmpegHotplugHandler: Connecting to hotplug monitor";
    
    DeviceManager& deviceManager = DeviceManager::getInstance();
    hotplug_monitor_ = deviceManager.getHotplugMonitor();
    
    if (!hotplug_monitor_) {
        qCWarning(log_ffmpeg_backend) << "Failed to get hotplug monitor from device manager";
        return;
    }
    
    // Connect to device unplugging signal
    connect(hotplug_monitor_, &HotplugMonitor::deviceUnplugged,
            this, &FFmpegHotplugHandler::OnDeviceUnplugged);
            
    // Connect to new device plugged in signal
    connect(hotplug_monitor_, &HotplugMonitor::newDevicePluggedIn,
            this, &FFmpegHotplugHandler::OnDevicePluggedIn);
            
    qCDebug(log_ffmpeg_backend) << "FFmpegHotplugHandler successfully connected to hotplug monitor";
}

void FFmpegHotplugHandler::DisconnectFromHotplugMonitor()
{
    qCDebug(log_ffmpeg_backend) << "FFmpegHotplugHandler: Disconnecting from hotplug monitor";
    
    if (hotplug_monitor_) {
        disconnect(hotplug_monitor_, nullptr, this, nullptr);
        hotplug_monitor_ = nullptr;
        qCDebug(log_ffmpeg_backend) << "FFmpegHotplugHandler disconnected from hotplug monitor";
    }
}

void FFmpegHotplugHandler::WaitForDeviceActivation(const QString& devicePath, int timeoutMs)
{
    qCDebug(log_ffmpeg_backend) << "Waiting for device activation:" << devicePath << "timeout:" << timeoutMs << "ms";
    
    expected_device_path_ = devicePath.isEmpty() ? current_device_ : devicePath;
    waiting_for_device_ = true;
    
    emit WaitingForDevice(expected_device_path_);
    
    // Start timeout timer
    if (timeoutMs > 0) {
        device_wait_timer_->start(timeoutMs);
    }
    
    // Start periodic check timer
    device_check_timer_->start();
    
    qCDebug(log_ffmpeg_backend) << "Started waiting for device activation";
}

void FFmpegHotplugHandler::HandleDeviceActivation(const QString& devicePath, const QString& portChain)
{
    qCInfo(log_ffmpeg_backend) << "Handling device activation:" << devicePath << "port chain:" << portChain;
    
    waiting_for_device_ = false;
    device_wait_timer_->stop();
    device_check_timer_->stop();
    
    if (!devicePath.isEmpty()) {
        current_device_ = devicePath;
        current_device_port_chain_ = portChain;  // Store port chain for unplug detection
        qCDebug(log_ffmpeg_backend) << "Stored current device port chain:" << current_device_port_chain_;
    }
    
    // Request parent handler to start capture with a delay to allow device stabilization
    QTimer::singleShot(300, this, [this]() {
        qCInfo(log_ffmpeg_backend) << "Requesting capture start on activated device";
        emit DeviceActivated(current_device_);
        // Parent handler should call startDirectCapture() in response
    });
}

void FFmpegHotplugHandler::HandleDeviceDeactivation(const QString& devicePath)
{
    qCInfo(log_ffmpeg_backend) << "Handling device deactivation:" << devicePath;
    QString deactivatedDevice = devicePath.isEmpty() ? current_device_ : devicePath;

    suppress_errors_ = true;
    
    // Request parent handler to stop capture
    emit RequestStopCapture();
    
    // Clear device state
    current_device_port_chain_.clear();
    current_device_.clear();
    
    qCDebug(log_ffmpeg_backend) << "Cleared current device port chain and settings";
    emit DeviceDeactivated(deactivatedDevice);
    
    qCInfo(log_ffmpeg_backend) << "Device deactivation completed";
}

void FFmpegHotplugHandler::SetCurrentDevicePortChain(const QString& portChain)
{
    current_device_port_chain_ = portChain;
    qCDebug(log_ffmpeg_backend) << "Set current device port chain to:" << current_device_port_chain_;
}

void FFmpegHotplugHandler::SetCurrentDevice(const QString& devicePath)
{
    current_device_ = devicePath;
    qCDebug(log_ffmpeg_backend) << "Set current device to:" << current_device_;
}

void FFmpegHotplugHandler::SetCaptureRunning(bool running)
{
    capture_running_ = running;
}

void FFmpegHotplugHandler::SetSuppressErrors(bool suppress)
{
    suppress_errors_ = suppress;
}

void FFmpegHotplugHandler::OnDeviceUnplugged(const DeviceInfo& device)
{
    qCInfo(log_ffmpeg_backend) << "FFmpegHotplugHandler: Device unplugged event received";
    qCInfo(log_ffmpeg_backend) << "  Port Chain:" << device.portChain;
    qCInfo(log_ffmpeg_backend) << "  Current device port chain:" << current_device_port_chain_;
    qCInfo(log_ffmpeg_backend) << "  Current device:" << current_device_;
    qCInfo(log_ffmpeg_backend) << "  Capture running:" << capture_running_;
    
    // Match by port chain like serial port manager does
    // This works even when DeviceInfo doesn't have camera info populated yet
    if (!current_device_port_chain_.isEmpty() && 
        current_device_port_chain_ == device.portChain) {
        qCInfo(log_ffmpeg_backend) << "  → Our current camera device was unplugged, stopping capture";
        
        // Close immediately like serial port does - don't wait for I/O errors
        if (capture_running_) {
            // Use QTimer to avoid blocking the hotplug signal handler
            QTimer::singleShot(0, this, [this]() {
                HandleDeviceDeactivation(current_device_);
            });
        }
    } else {
        qCDebug(log_ffmpeg_backend) << "  → Unplugged device is not our current camera, ignoring";
    }
}

void FFmpegHotplugHandler::OnDevicePluggedIn(const DeviceInfo& device)
{
    qCInfo(log_ffmpeg_backend) << "FFmpegHotplugHandler: New device plugged in event received";
    qCInfo(log_ffmpeg_backend) << "  Port Chain:" << device.portChain;
    qCInfo(log_ffmpeg_backend) << "  Has Camera:" << device.hasCameraDevice();
    qCInfo(log_ffmpeg_backend) << "  Camera Path:" << device.cameraDevicePath;
    qCInfo(log_ffmpeg_backend) << "  Camera ID:" << device.cameraDeviceId;
    qCInfo(log_ffmpeg_backend) << "  Waiting for device:" << waiting_for_device_;
    qCInfo(log_ffmpeg_backend) << "  Expected device:" << expected_device_path_;
    qCInfo(log_ffmpeg_backend) << "  Capture running:" << capture_running_;
    
    // Get device path - either from DeviceInfo or try to find it
    QString devicePath = device.cameraDevicePath;
    
    // If device doesn't have camera info yet, wait and retry
    if (!device.hasCameraDevice() || devicePath.isEmpty()) {
        qCDebug(log_ffmpeg_backend) << "  → Device has no camera info yet, will retry after delay";
        RetryDeviceActivationAfterDelay(device.portChain);
        return;
    }
    
    // Process device with camera info available
    ProcessDevicePluggedIn(devicePath, device.portChain);
}

void FFmpegHotplugHandler::ProcessDevicePluggedIn(const QString& devicePath, const QString& portChain)
{
    // If we're waiting for a device (after unplug), activate it
    if (waiting_for_device_) {
        if (!devicePath.isEmpty() && 
            (expected_device_path_.isEmpty() || devicePath == expected_device_path_)) {
            qCInfo(log_ffmpeg_backend) << "  → Found expected device, attempting activation:" << devicePath;
            // Use QTimer to avoid blocking the hotplug signal handler
            QTimer::singleShot(0, this, [this, devicePath, portChain]() {
                HandleDeviceActivation(devicePath, portChain);
            });
        } else {
            qCDebug(log_ffmpeg_backend) << "  → Device path doesn't match expected device";
        }
        return;
    }
    
    // If capture is not running and we have a camera device, try to start capture
    // This handles the case where camera was unplugged and plugged back in
    if (!capture_running_ && !devicePath.isEmpty()) {
        // Check if this might be the device we were using before
        bool shouldAutoStart = false;
        
        // If we have a stored current device path that matches
        if (!current_device_.isEmpty() && devicePath == current_device_) {
            shouldAutoStart = true;
            qCInfo(log_ffmpeg_backend) << "  → Detected previously used camera device, will auto-restart capture";
        }
        // Or if we don't have any device set yet and this is the first camera
        else if (current_device_.isEmpty()) {
            shouldAutoStart = true;
            qCInfo(log_ffmpeg_backend) << "  → Detected new camera device and no capture running, will auto-start capture";
        }
        
        if (shouldAutoStart) {
            // Use a short delay to ensure device is fully initialized
            // This also prevents blocking the hotplug event handler
            QTimer::singleShot(300, this, [this, devicePath, portChain]() {
                if (!capture_running_) {
                    qCInfo(log_ffmpeg_backend) << "Auto-starting capture for plugged-in device:" << devicePath;
                    HandleDeviceActivation(devicePath, portChain);
                }
            });
        } else {
            qCDebug(log_ffmpeg_backend) << "  → New camera device detected but not auto-starting (different from previous device)";
        }
    } else {
        qCDebug(log_ffmpeg_backend) << "  → Capture already running or no valid device path, ignoring plug-in event";
    }
}

void FFmpegHotplugHandler::RetryDeviceActivationAfterDelay(const QString& portChain)
{
    // Wait 300ms for camera enumeration to complete, then retry
    QTimer::singleShot(300, this, [this, portChain]() {
        qCDebug(log_ffmpeg_backend) << "Retrying device activation for port chain:" << portChain;
        
        // Try to find camera device by port chain using Qt's device enumeration
        QList<QCameraDevice> cameras = QMediaDevices::videoInputs();
        QString foundDeviceName;
        
        for (const QCameraDevice& camera : cameras) {
            QString cameraDesc = camera.description();
            QString cameraId = QString::fromUtf8(camera.id());
            qCDebug(log_ffmpeg_backend) << "  Checking camera:" << cameraDesc << "ID:" << cameraId;
            
            // On Windows, DirectShow needs the friendly name (description), not the ID
            // On Linux, use the device path
            if (cameraDesc.contains("Openterface", Qt::CaseInsensitive)) {
                #ifdef Q_OS_WIN
                foundDeviceName = QString("video=%1").arg(cameraDesc);  // Format for DirectShow
                #else
                foundDeviceName = cameraId;     // Use device path for V4L2
                #endif
                qCInfo(log_ffmpeg_backend) << "  Found Openterface camera:" << foundDeviceName;
                break;
            }
        }
        
        if (!foundDeviceName.isEmpty()) {
            // Found a camera device, proceed with activation
            if (waiting_for_device_) {
                qCInfo(log_ffmpeg_backend) << "  → Found device after retry, attempting activation:" << foundDeviceName;
                // Add small delay to allow device to fully initialize after reconnection
                QTimer::singleShot(300, this, [this, foundDeviceName, portChain]() {
                    HandleDeviceActivation(foundDeviceName, portChain);
                });
            } else if (!capture_running_) {
                qCInfo(log_ffmpeg_backend) << "  → Found device after retry, auto-starting capture:" << foundDeviceName;
                QTimer::singleShot(300, this, [this, foundDeviceName, portChain]() {
                    if (!capture_running_) {
                        HandleDeviceActivation(foundDeviceName, portChain);
                    }
                });
            }
        } else {
            qCWarning(log_ffmpeg_backend) << "  → No camera device found after retry for port chain:" << portChain;
        }
    });
}

void FFmpegHotplugHandler::OnDeviceWaitTimeout()
{
    qCWarning(log_ffmpeg_backend) << "Device wait timeout for:" << expected_device_path_;
    waiting_for_device_ = false;
    device_check_timer_->stop();
    emit CaptureError(QString("Device wait timeout: %1").arg(expected_device_path_));
}

void FFmpegHotplugHandler::OnDeviceCheckTimer()
{
    if (!waiting_for_device_) {
        device_check_timer_->stop();
        return;
    }
    
    // Check if expected device becomes available
    if (!expected_device_path_.isEmpty() && device_validator_ &&
        device_validator_->CheckCameraAvailable(expected_device_path_, current_device_, 
                                               capture_running_, waiting_for_device_)) {
        qCDebug(log_ffmpeg_backend) << "Expected device became available during wait:" << expected_device_path_;
        device_check_timer_->stop();
        HandleDeviceActivation(expected_device_path_);
    }
}
