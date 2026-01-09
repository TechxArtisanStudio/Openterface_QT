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

#include "ffmpeg_device_validator.h"
#include "global.h"
#include "ui/globalsetting.h"

#include <QFile>
#include <QDebug>
#include <QLoggingCategory>

extern "C" {
#include <libavformat/avformat.h>
#include <libavdevice/avdevice.h>
}

Q_DECLARE_LOGGING_CATEGORY(log_ffmpeg_backend)

FFmpegDeviceValidator::FFmpegDeviceValidator()
{
}

FFmpegDeviceValidator::~FFmpegDeviceValidator()
{
}

bool FFmpegDeviceValidator::CheckCameraAvailable(const QString& devicePath, 
                                                  const QString& currentDevice,
                                                  bool captureRunning,
                                                  bool waitingForDevice)
{
    if (devicePath.isEmpty()) {
        qCDebug(log_ffmpeg_backend) << "No device path provided for availability check";
        return false;
    }
    
    qCDebug(log_ffmpeg_backend) << "Checking camera availability for device:" << devicePath;
    
    // OS-specific device access check
    if (!CheckOSSpecificDeviceAccess(devicePath, currentDevice, captureRunning)) {
        return false;
    }
    
    // Skip intrusive FFmpeg check if device is currently being used for capture
    if (devicePath == currentDevice && captureRunning) {
        qCDebug(log_ffmpeg_backend) << "Device is currently in use for capture, skipping FFmpeg compatibility check";
        return true;
    }
    
    // Skip intrusive FFmpeg check if we're waiting for device activation
    if (waitingForDevice) {
        qCDebug(log_ffmpeg_backend) << "Waiting for device activation, skipping intrusive FFmpeg compatibility check";
        return true; // Rely on OS-specific checks above
    }
    
    // FFmpeg compatibility check
    return CheckFFmpegCompatibility(devicePath);
}

bool FFmpegDeviceValidator::GetMaxCameraCapability(const QString& devicePath, CameraCapability& capability)
{
    qCInfo(log_ffmpeg_backend) << "Loading video settings from GlobalSetting for:" << devicePath;
    
    // Load video settings from GlobalSetting into GlobalVar
    GlobalSetting::instance().loadVideoSettings();
    
    // Get the stored resolution and framerate
    int width = GlobalVar::instance().getCaptureWidth();
    int height = GlobalVar::instance().getCaptureHeight();
    int fps = GlobalVar::instance().getCaptureFps();
    
    capability.resolution = QSize(width, height);
    capability.framerate = fps;
    
    qCInfo(log_ffmpeg_backend) << "✓ Maximum capability from GlobalSetting:" 
                              << capability.resolution << "@" << capability.framerate << "FPS";
    return true;
}

bool FFmpegDeviceValidator::CheckOSSpecificDeviceAccess(const QString& devicePath, 
                                                        const QString& currentDevice,
                                                        bool captureRunning)
{
#ifdef Q_OS_WIN
    // On Windows, DirectShow device names like "video=Openterface" are not file paths
    // Skip file existence check for DirectShow devices
    if (devicePath.startsWith("video=")) {
        qCDebug(log_ffmpeg_backend) << "DirectShow device detected, skipping file existence check:" << devicePath;
        return true;
    }
    
    // For V4L2 devices on Windows (unlikely but handle it)
    QFile deviceFile(devicePath);
    if (!deviceFile.exists()) {
        qCDebug(log_ffmpeg_backend) << "Device file does not exist:" << devicePath;
        return false;
    }
    
    // Try to open the device for reading to verify it's accessible
    // Skip this check if we're currently capturing to avoid device conflicts
    if (devicePath == currentDevice && captureRunning) {
        qCDebug(log_ffmpeg_backend) << "Device is currently in use for capture, skipping file open check";
        return true;
    }
    
    if (!deviceFile.open(QIODevice::ReadOnly)) {
        qCDebug(log_ffmpeg_backend) << "Cannot open device for reading:" << devicePath << "Error:" << deviceFile.errorString();
        return false;
    }
    
    deviceFile.close();
#else
    // On Linux/macOS, check if device file exists and is accessible
    QFile deviceFile(devicePath);
    if (!deviceFile.exists()) {
        qCDebug(log_ffmpeg_backend) << "Device file does not exist:" << devicePath;
        return false;
    }
    
    // Try to open the device for reading to verify it's accessible
    // Skip this check if we're currently capturing to avoid device conflicts
    if (devicePath == currentDevice && captureRunning) {
        qCDebug(log_ffmpeg_backend) << "Device is currently in use for capture, skipping file open check";
        return true;
    }
    
    if (!deviceFile.open(QIODevice::ReadOnly)) {
        qCDebug(log_ffmpeg_backend) << "Cannot open device for reading:" << devicePath << "Error:" << deviceFile.errorString();
        return false;
    }
    
    deviceFile.close();
#endif
    
    return true;
}

bool FFmpegDeviceValidator::CheckFFmpegCompatibility(const QString& devicePath)
{
    AVFormatContext* testContext = avformat_alloc_context();
    if (!testContext) {
        qCDebug(log_ffmpeg_backend) << "Failed to allocate test format context";
        return false;
    }
    
    const AVInputFormat* inputFormat = GetInputFormat();
    if (!inputFormat) {
        avformat_free_context(testContext);
        return false;
    }
    
    // Try to open the device with minimal options
    AVDictionary* options = nullptr;
    av_dict_set(&options, "framerate", "1", 0); // Very low framerate for quick test
    av_dict_set(&options, "video_size", "160x120", 0); // Very small resolution for quick test
    
    int ret = avformat_open_input(&testContext, devicePath.toUtf8().constData(), inputFormat, &options);
    av_dict_free(&options);
    
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
        qCDebug(log_ffmpeg_backend) << "FFmpeg cannot open device:" << devicePath << "Error:" << QString::fromUtf8(errbuf);
        avformat_free_context(testContext);
        return false;
    }
    
    // Device opened successfully, clean up
    avformat_close_input(&testContext);
    qCDebug(log_ffmpeg_backend) << "Camera device is available:" << devicePath;
    return true;
}

const AVInputFormat* FFmpegDeviceValidator::GetInputFormat()
{
#ifdef Q_OS_WIN
    // On Windows, use DirectShow input format
    const AVInputFormat* inputFormat = av_find_input_format("dshow");
    if (!inputFormat) {
        qCDebug(log_ffmpeg_backend) << "DirectShow input format not available";
        return nullptr;
    }
    return inputFormat;
#else
    // On Linux/macOS, use V4L2 input format
    const AVInputFormat* inputFormat = av_find_input_format("v4l2");
    if (!inputFormat) {
        qCDebug(log_ffmpeg_backend) << "V4L2 input format not available";
        return nullptr;
    }
    return inputFormat;
#endif
}
