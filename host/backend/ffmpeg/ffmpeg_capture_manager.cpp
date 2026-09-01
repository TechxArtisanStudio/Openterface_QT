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
#include <thread>

#include <QThread>
#include <QDateTime>
#include <QElapsedTimer>
#include <QDebug>
#include <QLoggingCategory>
#include <QTimer>
#include <QElapsedTimer>

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
    
    // CRITICAL: Initialize hardware acceleration BEFORE opening device
    // This ensures hardware decoders are available when setting up the device
    if (hardware_accelerator_ && !hardware_accelerator_->IsHardwareAccelEnabled()) {
        QString preferredHwAccel = GlobalSetting::instance().getHardwareAcceleration();
        qCInfo(log_ffmpeg_backend) << "Initializing hardware acceleration with preferred:" << preferredHwAccel;
        if (hardware_accelerator_->Initialize(preferredHwAccel)) {
            qCInfo(log_ffmpeg_backend) << "✓ Hardware acceleration initialized successfully";
        } else {
            qCInfo(log_ffmpeg_backend) << "Hardware acceleration initialization returned false (may use software decoding)";
        }
    } else if (hardware_accelerator_ && hardware_accelerator_->IsHardwareAccelEnabled()) {
        qCDebug(log_ffmpeg_backend) << "Hardware acceleration already enabled";
    } else {
        qCDebug(log_ffmpeg_backend) << "No hardware accelerator available";
    }
    
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
    
    qCInfo(log_ffmpeg_backend) << "✓✓✓ ZERO LATENCY MODE ACTIVE - capture thread will discard stale frames ✓✓✓";
    
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
    } // Release mutex before waiting for thread

    // HOTPLUG FIX: StopCaptureThread() is now non-blocking.
    // It either returns quickly (thread exits within 100ms) or detaches a background
    // thread to handle the blocking wait + force-terminate.
    // CloseInputDevice() is handled in both paths:
    //   - Quick path: thread exited within 100ms, device closed in StopCaptureThread()
    //   - Slow path: deferred_thread_cleanup_ is true, device closed by detached thread
    StopCaptureThread();

    // Only close device here if neither path above handled it.
    // This shouldn't happen in practice, but provides a safety net.
    if (!deferred_thread_cleanup_) {
        QMutexLocker locker(&mutex_);
        CloseInputDevice();
    }

    // Stop performance monitoring
    if (performance_timer_) {
        performance_timer_->stop();
    }

    emit CaptureStopped();
    qCDebug(log_ffmpeg_backend) << "FFmpeg capture stopped (async)";
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
        if (noContextWarnings < 5) {
            qCWarning(log_ffmpeg_backend) << "readFrame called with invalid context or stream index";
            noContextWarnings++;
        }
        return false;
    }

    // ── LIVE-EDGE SEEKING ─────────────────────────────────────────────────────
    //
    // Problem: when the decoder (especially QSV) is slower than the camera frame
    // rate, frames pile up in the DirectShow ring buffer.  Each ReadFrame call
    // then returns the *oldest* waiting packet, so the display always lags by
    // however many frames are queued.  With QSV at 60fps this can accumulate to
    // 1-2 seconds of steady-state lag (CPU/TurboJPEG never has this issue because
    // it decodes faster than the frame interval).
    //
    // Solution: loop until av_read_frame "blocks" (takes ≥ kLiveEdgeMs to return),
    // which means the ring buffer is empty and we had to wait for the camera itself.
    // All fast-returning (buffered/stale) packets are discarded without decoding.
    // This guarantees that the packet we hand to the decoder is the most recently
    // produced frame, regardless of how long decoding takes.
    //
    // Thresholds:
    //   kLiveEdgeMs = 5 ms  – a DirectShow ring-buffer read (simple memcpy + mutex)
    //                         takes ≤ 2 ms even on slow systems.  5 ms comfortably
    //                         separates "got from buffer" (<2 ms) from "waited for
    //                         camera" (≥ 1000/fps ms, e.g. 8 ms at 120fps).
    //
    //   kMaxDiscard = 300   – safety cap; at 120fps the ring buffer (8MB) holds
    //                         ≈ 40 frames, so 300 is a very generous upper bound.
    //
    // Note: this loop does NOT introduce extra latency on a CPU decoder (TurboJPEG)
    // because CPU decode is faster than the frame interval – the first read always
    // blocks and the loop executes exactly once.

    static constexpr qint64 kLiveEdgeMs = 5;
    static constexpr int    kMaxDiscard = 300;

    int discarded = 0;

    for (int attempt = 0; attempt <= kMaxDiscard; ++attempt) {
        if (interrupt_requested_ || QThread::currentThread()->isInterruptionRequested()) {
            return false;
        }

        // Clear previous packet data before each read
        av_packet_unref(AV_PACKET_RAW(packet_));

        QElapsedTimer readTimer;
        readTimer.start();

        int ret = av_read_frame(formatContext, AV_PACKET_RAW(packet_));

        qint64 readMs = readTimer.elapsed();

        if (ret < 0) {
            if (ret == AVERROR(EAGAIN)) {
                // DirectShow returned EAGAIN: no frame data available yet.
                // This is NORMAL during device startup or transient idle periods.
                // Return false WITHOUT logging as error — the capture thread
                // should not count this as a real failure.
                return false;
            } else if (ret == AVERROR_EOF) {
                qCWarning(log_ffmpeg_backend) << "End of stream reached";
                return false;
            } else if (ret == AVERROR(EIO)) {
                qCWarning(log_ffmpeg_backend) << "I/O error - device may be disconnected";
                return false;
            } else if (ret == AVERROR(ENODEV) || ret == AVERROR(ENXIO)) {
                qCWarning(log_ffmpeg_backend) << "Device not found / disconnected";
                return false;
            } else {
                char errbuf[AV_ERROR_MAX_STRING_SIZE];
                av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
                static int errorCount = 0;
                if (errorCount < 10) {
                    qCWarning(log_ffmpeg_backend) << "Error reading frame:" << QString::fromUtf8(errbuf);
                    errorCount++;
                }
                return false;
            }
        }

        // Skip non-video packets (audio, subtitle …)
        if (AV_PACKET_RAW(packet_)->stream_index != video_stream_index_) {
            continue;
        }

        // ── Live-edge check ──────────────────────────────────────────────────
        // If av_read_frame returned quickly the frame was already sitting in the
        // ring buffer (stale). Discard it and loop to get a fresher one.
        // If it blocked for ≥ kLiveEdgeMs we had to wait for the camera: this IS
        // the latest frame. Return it immediately.
        if (readMs >= kLiveEdgeMs || attempt == kMaxDiscard) {
            // Live-edge (or fallback) frame – use it.
            if (discarded > 0) {
                qCDebug(log_ffmpeg_backend) << "Live-edge seek: discarded" << discarded
                                            << "stale buffered frames (read took" << readMs << "ms)";
            }
            break;  // packet_ now holds the live (or last-resort) frame
        }

        // Fast read = stale frame. Discard and continue.
        discarded++;
    }

    // Log first few successful reads
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

    // HOTPLUG FIX (修复十-B): Serialize with the detach thread's CloseDevice().
    // The detach thread holds device_op_mutex_ while calling dm->CloseDevice().
    // We use tryLock(5000) to wait up to 5s for the detach thread to finish.
    // If it times out (CloseDevice hangs on DirectShow graph teardown), we abort
    // rather than racing on format_context_ — the frame timeout handler will retry.
    if (!device_op_mutex_.tryLock(5000)) {
        qCWarning(log_ffmpeg_backend) << "Device open timed out waiting for deferred device close — aborting";
        return false;
    }

    // Open device via device manager (serialized with deferred CloseDevice)
    bool opened = device_manager_->OpenDevice(devicePath, resolution, framerate, hardware_accelerator_);

    device_op_mutex_.unlock();

    if (!opened) {
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
        // HOTPLUG FIX: Make thread join non-blocking to prevent main thread freeze.
        // When a USB device is unplugged, the capture thread can be stuck in av_read_frame()
        // on a dead device handle. Waiting for it blocks the main thread for up to 8 seconds
        // (5s wait + 2s retry + 1s terminate), freezing the entire UI.
        //
        // Strategy: signal the thread to stop, give it a brief chance to exit naturally,
        // then defer the blocking wait + cleanup to a detached background thread.
        // The 2000ms delay in CameraManager's reconnect handler provides additional
        // settling time before a new capture starts.
        qCDebug(log_ffmpeg_backend) << "Requesting capture thread to stop (non-blocking)";
        capture_thread_->requestInterruption();
        deferred_thread_cleanup_ = false;

        // Brief wait — thread exits quickly when device is still connected
        if (capture_thread_->wait(100)) {
            qCDebug(log_ffmpeg_backend) << "Capture thread exited gracefully (quick path)";
            capture_thread_.reset();
            // Thread has exited — safe to close device now
            QMutexLocker locker(&mutex_);
            CloseInputDevice();
        } else {
            // HOTPLUG FIX (修复十-B): Thread is stuck — likely blocked in av_read_frame() on a
            // dead/unplugged USB device.
            //
            // CRITICAL: We CANNOT call CloseInputDevice() from the main thread here!
            // avformat_close_input() tears down the DirectShow graph, which waits for the
            // capture thread's GetNextSample() to return. But the capture thread is blocked
            // waiting for data from the dead device → DEADLOCK. The main thread freezes.
            //
            // Instead, we delegate BOTH the device close AND the thread join to a detached
            // background thread:
            //   1. Detached thread closes the device FIRST (~10ms) → av_read_frame() fails
            //      → capture thread exits quickly
            //   2. Detached thread waits for capture thread to exit (max 5s)
            //   3. Detached thread cleans up QThread memory
            //
            // This eliminates the race condition from 修复八 (where the detached thread closed
            // the device AFTER waiting, potentially closing a NEW device):
            //   修复八 timing (BUGGY):
            //     T+0ms:    Unplug → slow path → detached thread WAITS (5s)
            //     T+2000ms: Replug → StartCapture → opens NEW device
            //     T+5000ms: Detached thread wakes → closes NEW device! (CRASH)
            //
            //   修复十-B timing (FIXED):
            //     T+0ms:    Unplug → slow path → detached thread CLOSES device (10ms)
            //     T+10ms:   av_read_frame() fails → capture thread exits
            //     T+100ms:  Detached thread finishes cleanup
            //     T+2000ms: Replug → StartCapture → opens device (SAFE — old device already closed)
            qCWarning(log_ffmpeg_backend) << "Capture thread still running after 100ms — delegating device close + cleanup to detached thread";

            deferred_thread_cleanup_ = true;

            // Move ownership of the raw pointer to the detached thread.
            QThread* rawThread = capture_thread_.release();
            FFmpegDeviceManager* dm = device_manager_;

            std::thread([rawThread, dm, this]() {
                // STEP 1: Close the device FIRST to unblock av_read_frame().
                // device_op_mutex_ serializes this with OpenInputDevice() in StartCapture(),
                // preventing a data race on format_context_ / codec_context_.
                if (dm) {
                    QMutexLocker locker(&device_op_mutex_);
                    qCDebug(log_ffmpeg_backend) << "[async] Closing device to unblock capture thread";
                    dm->CloseDevice();
                }

                // STEP 2: Wait for the capture thread to exit (max 5s).
                if (rawThread->wait(5000)) {
                    qCDebug(log_ffmpeg_backend) << "[async] Capture thread exited after device close";
                } else {
                    qCWarning(log_ffmpeg_backend) << "[async] Capture thread did not exit after 5s, terminating";
                    rawThread->terminate();
                    if (!rawThread->wait(1000)) {
                        qCCritical(log_ffmpeg_backend) << "[async] Capture thread still running after terminate!";
                    }
                }

                // STEP 3: Clean up QThread memory.
                delete rawThread;
            }).detach();
        }
    }
}
