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

#include "ffmpeg_capture_manager.h"
#include "capturethread.h"
#include "ffmpeg_device_manager.h"
#include "ffmpeg_hardware_accelerator.h"
#include "ffmpeg_device_validator.h"
#include "global.h"
#include "ui/globalsetting.h"

#include <QThread>
#include <QDateTime>
#include <QDebug>
#include <QLoggingCategory>
#include <QTimer>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavdevice/avdevice.h>
}

Q_DECLARE_LOGGING_CATEGORY(log_ffmpeg_backend)

FFmpegCaptureManager::FFmpegCaptureManager(FFmpegDeviceManager* deviceManager,
                                          FFmpegHardwareAccelerator* hardwareAccelerator,
                                          FFmpegDeviceValidator* deviceValidator,
                                          QObject* parent)
    : QObject(parent)
    , device_manager_(deviceManager)
    , hardware_accelerator_(hardwareAccelerator)
    , device_validator_(deviceValidator)
    , packet_(nullptr)
    , capture_running_(false)
    , video_stream_index_(-1)
    , interrupt_requested_(false)
    , operation_start_time_(0)
    , performance_timer_(nullptr)
{
}

FFmpegCaptureManager::~FFmpegCaptureManager()
{
    StopCapture();
}

bool FFmpegCaptureManager::StartCapture(const QString& devicePath, const QSize& resolution, int framerate)
{
    QMutexLocker locker(&mutex_);
    
    if (capture_running_) {
        qCDebug(log_ffmpeg_backend) << "Capture already running, stopping first";
        StopCapture();
    }
    
    // Cleanup any residual resources
    CleanupResources();
    
    // Set current device
    current_device_ = devicePath;
    
    // Auto-detect maximum camera capability if not specified
    QSize actualResolution = resolution;
    int actualFramerate = framerate;
    
    if (!resolution.isValid() || resolution.width() <= 0 || resolution.height() <= 0 ||
        framerate <= 0) {
        qCInfo(log_ffmpeg_backend) << "Resolution or framerate not specified, detecting camera capabilities...";
        
        // Try to get settings from GlobalSetting
        if (framerate <= 0 && device_validator_) {
            FFmpegDeviceValidator::CameraCapability tempCapability;
            if (device_validator_->GetMaxCameraCapability(devicePath, tempCapability)) {
                actualFramerate = tempCapability.framerate;
                qCInfo(log_ffmpeg_backend) << "✓ Got FPS from GlobalSetting:" << actualFramerate << "FPS";
                
                // Also use stored resolution if not specified
                if (!resolution.isValid() || resolution.width() <= 0 || resolution.height() <= 0) {
                    actualResolution = tempCapability.resolution;
                    qCInfo(log_ffmpeg_backend) << "✓ Got resolution from GlobalSetting:" << actualResolution;
                }
            } else {
                qCInfo(log_ffmpeg_backend) << "Could not load settings, will use defaults";
            }
        }
        
        // Fall back to defaults if still not set
        if (!actualResolution.isValid() || actualResolution.width() <= 0 || actualResolution.height() <= 0) {
            actualResolution = QSize(1920, 1080);
            qCInfo(log_ffmpeg_backend) << "Using default resolution:" << actualResolution;
        }
        if (framerate <= 0 && actualFramerate <= 0) {
            actualFramerate = 30;
            qCInfo(log_ffmpeg_backend) << "Using default framerate:" << actualFramerate << "FPS";
        }
    }
    
    // Store actual values
    current_resolution_ = actualResolution;
    current_framerate_ = actualFramerate;
    
    qCDebug(log_ffmpeg_backend) << "Starting FFmpeg capture:"
                                << "device=" << devicePath
                                << "resolution=" << actualResolution
                                << "framerate=" << actualFramerate;
    
    // Open input device
    if (!OpenInputDevice(devicePath, actualResolution, actualFramerate)) {
        qCWarning(log_ffmpeg_backend) << "Failed to open input device";
        return false;
    }
    
    // Initialize capture thread
    if (!InitializeCaptureThread()) {
        qCWarning(log_ffmpeg_backend) << "Failed to initialize capture thread";
        CloseInputDevice();
        return false;
    }
    
    capture_running_ = true;
    
    // Start performance monitoring if available
    if (performance_timer_) {
        performance_timer_->start();
    }
    
    emit CaptureStarted(devicePath);
    qCDebug(log_ffmpeg_backend) << "FFmpeg capture started successfully";
    return true;
}

void FFmpegCaptureManager::StopCapture()
{
    {
        QMutexLocker locker(&mutex_);
        
        if (!capture_running_) {
            return;
        }
        
        qCDebug(log_ffmpeg_backend) << "Stopping FFmpeg capture";
        
        capture_running_ = false;
        
        // Set interrupt flag to break out of any blocking FFmpeg operations
        interrupt_requested_ = true;
        
        // DO NOT close input device here - thread is still using it!
        // It will be closed after thread stops in StopCaptureThread()
    } // Release mutex before waiting for thread
    
    // Stop capture thread FIRST - this is critical!
    // The thread must exit before we close FFmpeg resources
    StopCaptureThread();
    
    // Now safe to close input device after thread has stopped
    {
        QMutexLocker locker(&mutex_);
        CloseInputDevice();
    }
    
    // Stop performance monitoring
    if (performance_timer_) {
        performance_timer_->stop();
    }
    
    emit CaptureStopped();
    qCDebug(log_ffmpeg_backend) << "FFmpeg capture stopped";
}

bool FFmpegCaptureManager::ReadFrame()
{
    // Check for interrupt request first to avoid blocking operations
    if (interrupt_requested_ || QThread::currentThread()->isInterruptionRequested()) {
        qCDebug(log_ffmpeg_backend) << "Read interrupted by request";
        return false;
    }

    AVFormatContext* formatContext = device_manager_ ? device_manager_->GetFormatContext() : nullptr;
    if (!formatContext || video_stream_index_ == -1) {
        static int noContextWarnings = 0;
        if (noContextWarnings < 5) { // Limit warnings to avoid spam
            qCWarning(log_ffmpeg_backend) << "readFrame called with invalid context or stream index";
            noContextWarnings++;
        }
        return false;
    }
    
    // Read packet from input with timeout handling
    int ret = av_read_frame(formatContext, AV_PACKET_RAW(packet_));
    if (ret < 0) {
        if (ret == AVERROR(EAGAIN)) {
            return false; // Try again later
        } else if (ret == AVERROR_EOF) {
            qCWarning(log_ffmpeg_backend) << "End of stream reached";
            return false;
        } else if (ret == AVERROR(EIO)) {
            qCWarning(log_ffmpeg_backend) << "I/O error while reading frame - device may be disconnected";
            return false;
        } else if (ret == AVERROR(ENODEV)) {
            qCWarning(log_ffmpeg_backend) << "No such device error - device disconnected";
            return false;
        } else if (ret == AVERROR(ENXIO)) {
            qCWarning(log_ffmpeg_backend) << "Device not configured or disconnected";
            return false;
        } else {
            char errbuf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
            static int errorCount = 0;
            if (errorCount < 10) { // Limit error spam
                qCWarning(log_ffmpeg_backend) << "Error reading frame:" << QString::fromUtf8(errbuf) << "error code:" << ret;
                errorCount++;
                
                // Check for device-related errors in error message
                QString errorMsg = QString::fromUtf8(errbuf).toLower();
                if (errorMsg.contains("no such device") || 
                    errorMsg.contains("device") ||
                    errorMsg.contains("vidioc") ||
                    ret == -19) { // ENODEV
                    qCWarning(log_ffmpeg_backend) << "Device error detected, likely disconnection";
                }
            }
            return false;
        }
    }
    
    // Check if this is our video stream
    if (AV_PACKET_RAW(packet_)->stream_index != video_stream_index_) {
        av_packet_unref(AV_PACKET_RAW(packet_));
        return false;
    }
    
    // Debug: Log first few successful reads
    static int readCount = 0;
    if (++readCount <= 5) {
        qCDebug(log_ffmpeg_backend) << "ReadFrame SUCCESS #" << readCount 
                                    << "packet size:" << AV_PACKET_RAW(packet_)->size
                                    << "stream:" << AV_PACKET_RAW(packet_)->stream_index;
    }
    
    return true;
}

int FFmpegCaptureManager::InterruptCallback(void* ctx)
{
    FFmpegCaptureManager* manager = static_cast<FFmpegCaptureManager*>(ctx);
    if (!manager) {
        return 0;
    }
    
    // Check if interrupt was explicitly requested
    if (manager->interrupt_requested_) {
        qCDebug(log_ffmpeg_backend) << "FFmpeg operation interrupted by request";
        return 1; // Interrupt the operation
    }
    
    // Check if operation has timed out
    if (manager->operation_start_time_ > 0) {
        qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - manager->operation_start_time_;
        if (elapsed > kOperationTimeoutMs) {
            qCWarning(log_ffmpeg_backend) << "FFmpeg operation timed out after" << elapsed << "ms";
            return 1; // Interrupt the operation
        }
    }
    
    return 0; // Continue the operation
}

bool FFmpegCaptureManager::OpenInputDevice(const QString& devicePath, const QSize& resolution, int framerate)
{
    if (!device_manager_) {
        qCCritical(log_ffmpeg_backend) << "Device manager not initialized";
        return false;
    }
    
    // Reset interrupt state for this new operation
    interrupt_requested_ = false;
    operation_start_time_ = QDateTime::currentMSecsSinceEpoch();
    
    // Open device via device manager
    if (!device_manager_->OpenDevice(devicePath, resolution, framerate, hardware_accelerator_)) {
        qCWarning(log_ffmpeg_backend) << "Failed to open device via device manager";
        return false;
    }
    
    // Update local video stream index from device manager
    video_stream_index_ = device_manager_->GetVideoStreamIndex();
    
    // Allocate packet
    packet_ = make_av_packet();
    
    if (!packet_) {
        qCCritical(log_ffmpeg_backend) << "Failed to allocate packet";
        return false;
    }
    
    // Reset operation timer - device opened successfully
    operation_start_time_ = 0;
    
    return true;
}

void FFmpegCaptureManager::CloseInputDevice()
{
    // Free packet
    if (packet_) {
        AV_PACKET_RESET(packet_);
    }
    
    // Close device via device manager
    if (device_manager_) {
        device_manager_->CloseDevice();
    }
    
    video_stream_index_ = -1;
}

void FFmpegCaptureManager::CleanupResources()
{
    CloseInputDevice();
}

bool FFmpegCaptureManager::InitializeCaptureThread()
{
    // Create capture thread - it will call ReadFrame() which needs access to packet_
    capture_thread_ = std::make_unique<CaptureThread>(this);
    capture_thread_->setRunning(true);
    
    // Connect signals
    connect(capture_thread_.get(), &CaptureThread::frameAvailable,
            this, &FFmpegCaptureManager::FrameAvailable, Qt::DirectConnection);
    connect(capture_thread_.get(), &CaptureThread::deviceDisconnected,
            this, &FFmpegCaptureManager::DeviceDisconnected);
    connect(capture_thread_.get(), &CaptureThread::readError,
            this, &FFmpegCaptureManager::CaptureError);
    
    // Start thread
    capture_thread_->start();
    
    // Set lower priority to not starve UI thread
    capture_thread_->setPriority(QThread::HighPriority);
    
    return true;
}

void FFmpegCaptureManager::StopCaptureThread()
{
    if (!capture_thread_) {
        return;
    }
    
    capture_thread_->setRunning(false);
    
    // Check if we're being called from the capture thread itself
    if (QThread::currentThread() == capture_thread_.get()) {
        qCDebug(log_ffmpeg_backend) << "stopCapture called from capture thread - will cleanup asynchronously";
        // Don't wait for the thread to finish since we ARE the thread
        // Just mark it for cleanup and let it finish naturally
        QTimer::singleShot(100, this, [this]() {
            if (capture_thread_ && capture_thread_->isFinished()) {
                capture_thread_.reset();
                qCDebug(log_ffmpeg_backend) << "Capture thread cleaned up asynchronously";
                // Don't call CleanupResources here either - parent StopCapture handles it
            }
        });
    } else {
        // We're being called from a different thread, wait gracefully
        qCDebug(log_ffmpeg_backend) << "Requesting capture thread to stop gracefully";
        capture_thread_->requestInterruption();
        
        // Wait longer for thread to finish gracefully (increased from 3s to 5s)
        // This is critical to prevent crashes when thread is processing frames
        if (!capture_thread_->wait(5000)) {
            qCWarning(log_ffmpeg_backend) << "Capture thread did not exit after 5 seconds";
            
            // Give it more time instead of terminating immediately
            qCDebug(log_ffmpeg_backend) << "Waiting additional 2 seconds for thread cleanup...";
            if (!capture_thread_->wait(2000)) {
                qCCritical(log_ffmpeg_backend) << "Capture thread still not finished after 7 seconds total";
                
                // As last resort, terminate - but this should rarely happen
                qCCritical(log_ffmpeg_backend) << "Force terminating thread (this may cause instability)";
                capture_thread_->terminate();
                
                // Give terminated thread time to cleanup
                if (!capture_thread_->wait(1000)) {
                    qCCritical(log_ffmpeg_backend) << "Capture thread still running after terminate!";
                }
            }
        } else {
            qCDebug(log_ffmpeg_backend) << "Capture thread exited gracefully";
        }
        
        // Only reset the thread after ensuring it's stopped
        if (!capture_thread_->isRunning()) {
            capture_thread_.reset();
            qCDebug(log_ffmpeg_backend) << "Capture thread cleaned up successfully";
        } else {
            qCCritical(log_ffmpeg_backend) << "WARNING: Capture thread still running, cannot safely destroy!";
            // Don't reset to avoid the crash, but this is a serious error
        }
        
        // Don't call CleanupResources here - it will be called by StopCapture()
        // after the thread has fully stopped to avoid use-after-free
    }
}
