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

#include "ffmpegbackendhandler.h"
#include "../../ui/videopane.h"
#include "../../global.h"
#include "../../ui/globalsetting.h"
#include "../../device/DeviceManager.h"
#include "../../device/HotplugMonitor.h"
#include "../../device/DeviceInfo.h"

#include <QThread>
#include <QDebug>
#include <QLoggingCategory>
#include <QApplication>
#include <QElapsedTimer>
#include <QImage>
#include <QGraphicsVideoItem>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QTimer>
#include <QFileInfo>
#include <QMediaDevices>
#include <QCameraDevice>

// FFmpeg includes
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
#include <libavdevice/avdevice.h>
}

// libjpeg-turbo includes (conditional compilation)
#ifdef HAVE_LIBJPEG_TURBO
#include <turbojpeg.h>
#endif

Q_LOGGING_CATEGORY(log_ffmpeg_backend, "opf.backend.ffmpeg")

// Capture thread extracted to host/backend/ffmpeg/capturethread.h/.cpp
#include "ffmpeg/capturethread.h"
#include "ffmpeg/ffmpeg_hardware_accelerator.h"
#include "ffmpeg/ffmpeg_device_manager.h"
#include "ffmpeg/ffmpeg_frame_processor.h"
#include "ffmpeg/ffmpeg_recorder.h"

FFmpegBackendHandler::FFmpegBackendHandler(QObject *parent)
    : MultimediaBackendHandler(parent), 
    m_packet(nullptr),
    m_deviceManager(std::make_unique<FFmpegDeviceManager>()),
    m_hardwareAccelerator(std::make_unique<FFmpegHardwareAccelerator>()),
    m_frameProcessor(std::make_unique<FFmpegFrameProcessor>()),
    m_recorder(std::make_unique<FFmpegRecorder>()),
    m_captureRunning(false),
    m_videoStreamIndex(-1),
    m_frameCount(0),
    m_lastFrameTime(0),
    m_interruptRequested(false),
    m_operationStartTime(0),
    m_hotplugMonitor(nullptr),
    m_waitingForDevice(false),
    m_deviceWaitTimer(nullptr),
    m_suppressErrors(false),
    m_graphicsVideoItem(nullptr),
    m_videoPane(nullptr),
    m_recordingActive(false)
{
    m_config = getDefaultConfig();
    m_preferredHwAccel = GlobalSetting::instance().getHardwareAcceleration();
    
    // Initialize device wait timer
    m_deviceWaitTimer = new QTimer(this);
    m_deviceWaitTimer->setSingleShot(true);
    connect(m_deviceWaitTimer, &QTimer::timeout, this, [this]() {
        qCWarning(log_ffmpeg_backend) << "Device wait timeout for:" << m_expectedDevicePath;
        m_waitingForDevice = false;
        emit captureError(QString("Device wait timeout: %1").arg(m_expectedDevicePath));
    });
    
    // Connect to hotplug monitor
    connectToHotplugMonitor();
    
    // Initialize FFmpeg
    if (!initializeFFmpeg()) {
        qCCritical(log_ffmpeg_backend) << "Failed to initialize FFmpeg";
    }
    
    // Setup performance monitoring
    m_performanceTimer = new QTimer(this);
    m_performanceTimer->setInterval(5000); // Report every 5 seconds
    connect(m_performanceTimer, &QTimer::timeout, this, [this]() {
        if (m_frameCount > 0) {
            double fps = m_frameCount / 5.0;
            qCDebug(log_ffmpeg_backend) << QString("FFmpeg capture performance: %1 FPS").arg(fps, 0, 'f', 1);
            emit fpsChanged(fps);
            m_frameCount = 0;
        }
    });
}

FFmpegBackendHandler::~FFmpegBackendHandler()
{
    // Stop recording first if active
    if (m_recorder && m_recorder->IsRecording()) {
        m_recorder->StopRecording();
    }
    
    // Disconnect from hotplug monitor
    disconnectFromHotplugMonitor();
    
    stopDirectCapture();
    cleanupFFmpeg();
}

MultimediaBackendType FFmpegBackendHandler::getBackendType() const
{
    return MultimediaBackendType::FFmpeg;
}

QString FFmpegBackendHandler::getBackendName() const
{
    return "FFmpeg Direct Capture";
}

QStringList FFmpegBackendHandler::getAvailableHardwareAccelerations() const
{
    QStringList availableHwAccel;
    availableHwAccel << "none";  // CPU option
    availableHwAccel << "auto";  // Always include auto

    // Check for CUDA/NVDEC
    const AVCodec* cudaCodec = avcodec_find_decoder_by_name("mjpeg_cuvid");
    if (cudaCodec) {
        availableHwAccel << "cuda";
    }

    // Check for Intel QSV
    const AVCodec* qsvCodec = avcodec_find_decoder_by_name("mjpeg_qsv");
    if (qsvCodec) {
        availableHwAccel << "qsv";
    }

    // Check for other hardware types if needed
    // For now, focus on CUDA and QSV as they are the main ones for MJPEG

    return availableHwAccel;
}

bool FFmpegBackendHandler::isDirectCaptureRunning() const
{
    return m_captureRunning;
}

MultimediaBackendConfig FFmpegBackendHandler::getDefaultConfig() const
{
    MultimediaBackendConfig config;
    config.cameraInitDelay = 10;
    config.captureSessionDelay = 10;
    config.useConservativeFrameRates = false;
    config.requireVideoOutputReset = false;
    config.useGradualVideoOutputSetup = false;
    return config;
}

void FFmpegBackendHandler::prepareCameraCreation()
{
    qCDebug(log_ffmpeg_backend) << "FFmpeg: Preparing camera creation";
    stopDirectCapture();
    // QThread::msleep(m_config.deviceSwitchDelay);
}

void FFmpegBackendHandler::configureCameraDevice()
{
    return;
}

void FFmpegBackendHandler::setupCaptureSession(QMediaCaptureSession* session)
{
    // For FFmpeg backend, skip Qt capture session setup to avoid device conflicts
    qCDebug(log_ffmpeg_backend) << "FFmpeg: Skipping Qt capture session setup - using direct capture";
    
    // Do not call session->setCamera(camera) for FFmpeg backend
    // The direct capture will handle video rendering without Qt camera
}

void FFmpegBackendHandler::prepareVideoOutputConnection(QMediaCaptureSession* session, QObject* videoOutput)
{
    qCDebug(log_ffmpeg_backend) << "FFmpeg: Preparing video output connection";
    
    // Check if videoOutput is a VideoPane
    if (VideoPane* videoPane = qobject_cast<VideoPane*>(videoOutput)) {
        setVideoOutput(videoPane);
        qCDebug(log_ffmpeg_backend) << "FFmpeg: Set VideoPane for direct rendering";
        return;
    }
    
    // Check if videoOutput is a QGraphicsVideoItem
    if (QGraphicsVideoItem* graphicsVideoItem = qobject_cast<QGraphicsVideoItem*>(videoOutput)) {
        setVideoOutput(graphicsVideoItem);
        qCDebug(log_ffmpeg_backend) << "FFmpeg: Set graphics video item for direct rendering";
        return;
    }
    
    qCDebug(log_ffmpeg_backend) << "FFmpeg: Video output type not supported for direct rendering";
}

void FFmpegBackendHandler::finalizeVideoOutputConnection(QMediaCaptureSession* session, QObject* videoOutput)
{
    // For FFmpeg backend, skip Qt video output setup
    qCDebug(log_ffmpeg_backend) << "FFmpeg: Skipping Qt video output setup - using direct rendering";
    
    // Do not call session->setVideoOutput(videoOutput) for FFmpeg backend
    // The direct rendering will handle video display
}

void FFmpegBackendHandler::startCamera()
{
    qCDebug(log_ffmpeg_backend) << "FFmpeg: Starting camera with direct capture";
    
    qCDebug(log_ffmpeg_backend) << "Current device:" << m_currentDevice;
    qCDebug(log_ffmpeg_backend) << "Current resolution:" << m_currentResolution;
    qCDebug(log_ffmpeg_backend) << "Current framerate:" << m_currentFramerate;
    
    // Skip availability check - just try to open the device directly
    // The checkCameraAvailable() opens the device which can interfere with immediate reopening
    // We'll rely on startDirectCapture() to handle device availability
    
    // Use direct FFmpeg capture
    qCDebug(log_ffmpeg_backend) << "FFmpeg: Using direct capture";
    
    // Start direct FFmpeg capture
    QSize resolution = m_currentResolution.isValid() ? m_currentResolution : QSize(0, 0);
    int framerate = m_currentFramerate > 0 ? m_currentFramerate : 0;
    
    if (!startDirectCapture(m_currentDevice, resolution, framerate)) {
        qCWarning(log_ffmpeg_backend) << "Failed to start FFmpeg direct capture";
        emit captureError("Failed to start FFmpeg video capture");
    } else {
        qCDebug(log_ffmpeg_backend) << "FFmpeg direct capture started successfully";
        emit deviceActivated(m_currentDevice);
    }
}

void FFmpegBackendHandler::stopCamera()
{
    qCDebug(log_ffmpeg_backend) << "FFmpeg: Stopping camera";
    
    // Stop direct capture
    stopDirectCapture();
}

QCameraFormat FFmpegBackendHandler::selectOptimalFormat(const QList<QCameraFormat>& formats,
                                                       const QSize& resolution,
                                                       int desiredFrameRate,
                                                       QVideoFrameFormat::PixelFormat pixelFormat) const
{
    qCDebug(log_ffmpeg_backend) << "FFmpeg: Selecting optimal format with flexible frame rate matching";
    
    // Store resolution and framerate for direct capture
    const_cast<FFmpegBackendHandler*>(this)->m_currentResolution = resolution;
    const_cast<FFmpegBackendHandler*>(this)->m_currentFramerate = desiredFrameRate;
    
    // Return the first available format since we'll handle capture directly
    return formats.isEmpty() ? QCameraFormat() : formats.first();
}

// Direct FFmpeg capture implementation

bool FFmpegBackendHandler::initializeFFmpeg()
{
    qCDebug(log_ffmpeg_backend) << "Initializing FFmpeg";
    
    // Initialize FFmpeg
    av_log_set_level(AV_LOG_WARNING); // Reduce FFmpeg log noise
    avdevice_register_all();
    
    qCDebug(log_ffmpeg_backend) << "FFmpeg initialization completed";
    return true;
}

void FFmpegBackendHandler::cleanupFFmpeg()
{
    qCDebug(log_ffmpeg_backend) << "Cleaning up FFmpeg";
    
    closeInputDevice();
    
    // Cleanup hardware acceleration
    cleanupHardwareAcceleration();
    
    qCDebug(log_ffmpeg_backend) << "FFmpeg cleanup completed";
}

void FFmpegBackendHandler::cleanupFFmpegResources()
{
    closeInputDevice();
}

bool FFmpegBackendHandler::initializeHardwareAcceleration()
{
    if (!m_hardwareAccelerator) {
        qCWarning(log_ffmpeg_backend) << "Hardware accelerator not initialized";
        return false;
    }
    
    return m_hardwareAccelerator->Initialize(m_preferredHwAccel);
}

void FFmpegBackendHandler::updatePreferredHardwareAcceleration()
{
    m_preferredHwAccel = GlobalSetting::instance().getHardwareAcceleration();
    qCDebug(log_ffmpeg_backend) << "Updated preferred hardware acceleration to:" << m_preferredHwAccel;
    
    if (m_hardwareAccelerator) {
        m_hardwareAccelerator->UpdatePreferredAcceleration(m_preferredHwAccel);
    }
}

bool FFmpegBackendHandler::tryHardwareDecoder(const AVCodecParameters* codecpar, 
                                               const AVCodec** outCodec, 
                                               bool* outUsingHwDecoder)
{
    if (!m_hardwareAccelerator) {
        return false;
    }
    
    return m_hardwareAccelerator->TryHardwareDecoder(codecpar, outCodec, outUsingHwDecoder);
}

void FFmpegBackendHandler::cleanupHardwareAcceleration()
{
    if (m_hardwareAccelerator) {
        m_hardwareAccelerator->Cleanup();
    }
}

bool FFmpegBackendHandler::getMaxCameraCapability(const QString& devicePath, CameraCapability& capability)
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

bool FFmpegBackendHandler::startDirectCapture(const QString& devicePath, const QSize& resolution, int framerate)
{
    QMutexLocker locker(&m_mutex);
    
    if (m_captureRunning) {
        qCDebug(log_ffmpeg_backend) << "Capture already running, stopping first";
        stopDirectCapture();
    }
    
    // Cleanup any residual FFmpeg resources
    cleanupFFmpegResources();
    
    // Set current device
    m_currentDevice = devicePath;
    
    // Reset error suppression flag when starting capture
    m_suppressErrors = false;
    
    // Restore FFmpeg logging level
    av_log_set_level(AV_LOG_WARNING);
    
    // Auto-detect maximum camera capability if not specified
    QSize actualResolution = resolution;
    int actualFramerate = framerate;
    
    if (!resolution.isValid() || resolution.width() <= 0 || resolution.height() <= 0 ||
        framerate <= 0) {
        qCInfo(log_ffmpeg_backend) << "Resolution or framerate not specified, detecting camera capabilities...";
        
        // Try to get settings from GlobalSetting
        // Use stored values from video page settings
        if (framerate <= 0) {
            // Use getMaxCameraCapability which loads from GlobalSetting
            CameraCapability tempCapability;
            if (getMaxCameraCapability(devicePath, tempCapability)) {
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
    
    qCDebug(log_ffmpeg_backend) << "Starting direct FFmpeg capture:"
                                << "device=" << devicePath
                                << "resolution=" << actualResolution
                                << "framerate=" << actualFramerate;
    
    // Open input device
    if (!openInputDevice(devicePath, actualResolution, actualFramerate)) {
        qCWarning(log_ffmpeg_backend) << "Failed to open input device";
        return false;
    }
    
    // Create and start capture thread
    m_captureThread = std::make_unique<CaptureThread>(this);
    m_captureThread->setRunning(true);
    connect(m_captureThread.get(), &CaptureThread::frameAvailable,
        this, &FFmpegBackendHandler::processFrame, Qt::DirectConnection);
    connect(m_captureThread.get(), &CaptureThread::deviceDisconnected,
        this, [this]() { handleDeviceDeactivation(m_currentDevice); });
    connect(m_captureThread.get(), &CaptureThread::readError,
        this, [this](const QString& msg) {
        qCWarning(log_ffmpeg_backend) << "Capture thread read error:" << msg;
        emit captureError(msg);
        });
    m_captureRunning = true;
    m_captureThread->start();
    
    // Set lower priority for capture thread to not starve UI thread
    m_captureThread->setPriority(QThread::LowPriority);
    
    // Start performance monitoring
    if (m_performanceTimer) {
        m_performanceTimer->start();
    }
    
    qCDebug(log_ffmpeg_backend) << "Direct FFmpeg capture started successfully";
    return true;
}

void FFmpegBackendHandler::stopDirectCapture()
{
    {
        QMutexLocker locker(&m_mutex);
        
        if (!m_captureRunning) {
            return;
        }
        
        qCDebug(log_ffmpeg_backend) << "Stopping direct FFmpeg capture";
        
        m_captureRunning = false;
        
        // Set interrupt flag to break out of any blocking FFmpeg operations
        m_interruptRequested = true;
        
        // Request thread interruption
        m_captureThread->requestInterruption();
        
        // Close input device first to stop buffering and prevent buffer overflow
        closeInputDevice();
    } // Release mutex before waiting

        // Stop capture thread
        if (m_captureThread) {
            m_captureThread->setRunning(false);
            
            // Check if we're being called from the capture thread itself
            if (QThread::currentThread() == m_captureThread.get()) {
                qCDebug(log_ffmpeg_backend) << "stopDirectCapture called from capture thread - will cleanup asynchronously";
                // Don't wait for the thread to finish since we ARE the thread
                // Just mark it for cleanup and let it finish naturally
                QTimer::singleShot(100, this, [this]() {
                    if (m_captureThread && m_captureThread->isFinished()) {
                        m_captureThread.reset();
                        qCDebug(log_ffmpeg_backend) << "Capture thread cleaned up asynchronously";
                        // Cleanup resources after thread finishes (closeInputDevice already called)
                        QMutexLocker locker(&m_mutex);
                        cleanupFFmpegResources();
                    }
                });
            } else {
                // We're being called from a different thread, terminate immediately
                qCDebug(log_ffmpeg_backend) << "Terminating capture thread immediately";
                m_captureThread->requestInterruption();
                
                // Wait for thread to finish with proper timeout handling
                if (!m_captureThread->wait(3000)) {
                    qCCritical(log_ffmpeg_backend) << "Capture thread did not exit after 3 seconds, forcing termination";
                    m_captureThread->terminate();
                    
                    // Give it one more chance to clean up after terminate
                    if (!m_captureThread->wait(1000)) {
                        qCCritical(log_ffmpeg_backend) << "Capture thread still running after terminate! This should not happen.";
                    }
                }
                
                // Only reset the thread after ensuring it's stopped
                if (!m_captureThread->isRunning()) {
                    m_captureThread.reset();
                    qCDebug(log_ffmpeg_backend) << "Capture thread terminated and cleaned up";
                } else {
                    qCCritical(log_ffmpeg_backend) << "WARNING: Capture thread still running, cannot safely destroy!";
                    // Don't reset to avoid the crash, but this is a serious error
                }
                
                // Resources already cleaned up by closeInputDevice()
            }
        } else {
            // No thread, but ensure resources are cleaned
            QMutexLocker locker(&m_mutex);
            cleanupFFmpegResources();
        }    // Stop performance monitoring
    if (m_performanceTimer) {
        m_performanceTimer->stop();
    }
    
    qCDebug(log_ffmpeg_backend) << "Direct FFmpeg capture stopped";
}

// Static interrupt callback for FFmpeg operations to prevent blocking
int FFmpegBackendHandler::interruptCallback(void* ctx)
{
    FFmpegBackendHandler* handler = static_cast<FFmpegBackendHandler*>(ctx);
    if (!handler) {
        return 0;
    }
    
    // Check if interrupt was explicitly requested
    if (handler->m_interruptRequested) {
        qCDebug(log_ffmpeg_backend) << "FFmpeg operation interrupted by request";
        return 1; // Interrupt the operation
    }
    
    // Check if operation has timed out
    if (handler->m_operationStartTime > 0) {
        qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - handler->m_operationStartTime;
        if (elapsed > FFMPEG_OPERATION_TIMEOUT_MS) {
            qCWarning(log_ffmpeg_backend) << "FFmpeg operation timed out after" << elapsed << "ms";
            return 1; // Interrupt the operation
        }
    }
    
    return 0; // Continue the operation
}

bool FFmpegBackendHandler::openInputDevice(const QString& devicePath, const QSize& resolution, int framerate)
{
    if (!m_deviceManager) {
        qCCritical(log_ffmpeg_backend) << "Device manager not initialized";
        return false;
    }
    
    // Reset interrupt state for this new operation
    m_interruptRequested = false;
    m_operationStartTime = QDateTime::currentMSecsSinceEpoch();
    
    // CRITICAL: Initialize hardware acceleration BEFORE opening device
    // This ensures hardware decoders are available when setting up the device
    updatePreferredHardwareAcceleration();
    
    if (m_hardwareAccelerator && !m_hardwareAccelerator->IsHardwareAccelEnabled()) {
        initializeHardwareAcceleration();
    }
    
    if (!m_deviceManager->OpenDevice(devicePath, resolution, framerate, m_hardwareAccelerator.get())) {
        qCWarning(log_ffmpeg_backend) << "Failed to open device via device manager";
        return false;
    }
    
    // Update local video stream index from device manager
    m_videoStreamIndex = m_deviceManager->GetVideoStreamIndex();
    
    // Allocate packet
    m_packet = make_av_packet();
    
    if (!m_packet) {
        qCCritical(log_ffmpeg_backend) << "Failed to allocate packet";
        return false;
    }
    
    // Reset operation timer - device opened successfully
    m_operationStartTime = 0;
    
    return true;
}

void FFmpegBackendHandler::closeInputDevice()
{
    // Free packet
    if (m_packet) {
        AV_PACKET_RESET(m_packet);
    }
    
    // Close device via device manager
    if (m_deviceManager) {
        m_deviceManager->CloseDevice();
    }
    
    m_videoStreamIndex = -1;
}

bool FFmpegBackendHandler::readFrame()
{
    // Check for interrupt request first to avoid blocking operations
    if (m_interruptRequested || QThread::currentThread()->isInterruptionRequested()) {
        qCDebug(log_ffmpeg_backend) << "Read interrupted by request";
        return false;
    }

    AVFormatContext* formatContext = m_deviceManager ? m_deviceManager->GetFormatContext() : nullptr;
    if (!formatContext || m_videoStreamIndex == -1) {
        static int noContextWarnings = 0;
        if (noContextWarnings < 5) { // Limit warnings to avoid spam
            qCWarning(log_ffmpeg_backend) << "readFrame called with invalid context or stream index";
            noContextWarnings++;
        }
        return false;
    }
    
    // Read packet from input with timeout handling
    int ret = av_read_frame(formatContext, AV_PACKET_RAW(m_packet));
    if (ret < 0) {
        if (ret == AVERROR(EAGAIN)) {
            return false; // Try again later
        } else if (ret == AVERROR_EOF) {
            if (!m_suppressErrors) {
                qCWarning(log_ffmpeg_backend) << "End of stream reached";
            }
            return false;
        } else if (ret == AVERROR(EIO)) {
            if (!m_suppressErrors) {
                qCWarning(log_ffmpeg_backend) << "I/O error while reading frame - device may be disconnected";
            }
            return false;
        } else if (ret == AVERROR(ENODEV)) {
            if (!m_suppressErrors) {
                qCWarning(log_ffmpeg_backend) << "No such device error - device disconnected";
            }
            return false;
        } else if (ret == AVERROR(ENXIO)) {
            if (!m_suppressErrors) {
                qCWarning(log_ffmpeg_backend) << "Device not configured or disconnected";
            }
            return false;
        } else {
            char errbuf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
            static int errorCount = 0;
            if (errorCount < 10 && !m_suppressErrors) { // Limit error spam and respect suppression flag
                qCWarning(log_ffmpeg_backend) << "Error reading frame:" << QString::fromUtf8(errbuf) << "error code:" << ret;
                errorCount++;
                
                // Check for device-related errors in error message
                QString errorMsg = QString::fromUtf8(errbuf).toLower();
                if (errorMsg.contains("no such device") || 
                    errorMsg.contains("device") ||
                    errorMsg.contains("vidioc") ||
                    ret == -19) { // ENODEV
                    qCWarning(log_ffmpeg_backend) << "Device error detected in error message, likely disconnection";
                    // Return false to trigger consecutive failure counting
                }
            }
            return false;
        }
    }
    
    // Check if this is our video stream
    if (AV_PACKET_RAW(m_packet)->stream_index != m_videoStreamIndex) {
        av_packet_unref(AV_PACKET_RAW(m_packet));
        return false;
    }
    
    return true;
}

void FFmpegBackendHandler::processFrame()
{
    // Check if capture is stopping or resources are gone (no lock to avoid blocking)
    AVFormatContext* formatContext = m_deviceManager ? m_deviceManager->GetFormatContext() : nullptr;
    AVCodecContext* codecContext = m_deviceManager ? m_deviceManager->GetCodecContext() : nullptr;
    
    if (!m_captureRunning || !formatContext || !codecContext || !m_frameProcessor) {
        return; // Exit early if capture is stopping or resources are gone
    }

    if (!m_packet) {
        return;
    }
    
    // Check if recording is active
    bool isRecording = m_recorder && m_recorder->IsRecording() && !m_recorder->IsPaused();
    
    // Process frame using FFmpegFrameProcessor
    QPixmap pixmap = m_frameProcessor->ProcessPacket(AV_PACKET_RAW(m_packet), codecContext, isRecording);
    
    // Clean up packet
    av_packet_unref(AV_PACKET_RAW(m_packet));
    
    if (!pixmap.isNull()) {
        m_frameCount++;
        
        // Reduce logging frequency for performance
        if (m_frameCount % 1000 == 1) {
            qCDebug(log_ffmpeg_backend) << "Processed frame" << m_frameCount << "size:" << pixmap.size();
        }
        
        // Emit frame to UI (QueuedConnection ensures thread safety)
        if (m_captureRunning) {
            emit frameReady(pixmap);
        }
        
        // Write frame to recording file if recording is active
        if (m_recorder && m_recorder->IsRecording() && !m_recorder->IsPaused()) {
            // FRAME RATE CONTROL: Only write frames at the target recording framerate
            qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
            
            if (m_recorder->ShouldWriteFrame(currentTime)) {
                if (m_recorder->WriteFrame(pixmap)) {
                    // Update recording duration periodically
                    static int recordingFrameCount = 0;
                    if (++recordingFrameCount % 30 == 0) { // Every 30 frames (~1 second at 30fps)
                        emit recordingDurationChanged(m_recorder->GetRecordingDuration());
                    }
                }
            }
        }
        
        // Reduce success logging frequency for performance
        if (m_frameCount % 1000 == 1) {
            qCDebug(log_ffmpeg_backend) << "frameReady signal emitted successfully for frame" << m_frameCount;
        }
        
        // REMOVED: QCoreApplication::processEvents() - this was causing excessive CPU usage
        // Let Qt's event loop handle frame processing naturally
    } else {
        // Only warn for decode failures on packets with actual data
        if (AV_PACKET_RAW(m_packet)->size > 0) {
            qCWarning(log_ffmpeg_backend) << "Failed to decode frame - pixmap is null";
            qCWarning(log_ffmpeg_backend) << "Frame decode failure details:";
            qCWarning(log_ffmpeg_backend) << "  - Packet size:" << AV_PACKET_RAW(m_packet)->size;
            qCWarning(log_ffmpeg_backend) << "  - Codec ID:" << (codecContext ? codecContext->codec_id : -1);
            qCWarning(log_ffmpeg_backend) << "  - Stream index:" << AV_PACKET_RAW(m_packet)->stream_index;
        }
    }
}

void FFmpegBackendHandler::setVideoOutput(QGraphicsVideoItem* videoItem)
{
    QMutexLocker locker(&m_mutex);
    
    // Disconnect previous connections
    disconnect(this, &FFmpegBackendHandler::frameReady, this, nullptr);
    
    m_graphicsVideoItem = videoItem;
    m_videoPane = nullptr;
    
    if (videoItem) {
        qCDebug(log_ffmpeg_backend) << "Graphics video item set for FFmpeg direct rendering";
        
        // Connect frame ready signal to update graphics item
        connect(this, &FFmpegBackendHandler::frameReady, this, [this](const QPixmap& frame) {
            if (m_graphicsVideoItem && m_graphicsVideoItem->scene()) {
                // Create or update pixmap item for display
                QGraphicsPixmapItem* pixmapItem = nullptr;
                
                // Find existing pixmap item or create new one
                QList<QGraphicsItem*> items = m_graphicsVideoItem->scene()->items();
                for (auto item : items) {
                    if (auto pItem = qgraphicsitem_cast<QGraphicsPixmapItem*>(item)) {
                        pixmapItem = pItem;
                        break;
                    }
                }
                
                if (!pixmapItem) {
                    pixmapItem = m_graphicsVideoItem->scene()->addPixmap(frame);
                    pixmapItem->setZValue(1); // Above video item
                } else {
                    pixmapItem->setPixmap(frame);
                }
            }
        }, Qt::QueuedConnection);
    }
}

void FFmpegBackendHandler::setVideoOutput(VideoPane* videoPane)
{
    QMutexLocker locker(&m_mutex);
    
    // Disconnect previous connections
    disconnect(this, &FFmpegBackendHandler::frameReady, this, nullptr);
    
    m_videoPane = videoPane;
    m_graphicsVideoItem = nullptr;
    
    if (videoPane) {
        qCDebug(log_ffmpeg_backend) << "VideoPane set for FFmpeg direct rendering";
        
        // Connect frame ready signal to VideoPane updateVideoFrame method
        // Use QueuedConnection to ensure thread safety and prevent blocking capture thread
        connect(this, &FFmpegBackendHandler::frameReady,
                videoPane, &VideoPane::updateVideoFrame,
                Qt::QueuedConnection);
        
        qCDebug(log_ffmpeg_backend) << "Connected frameReady signal to VideoPane::updateVideoFrame with QueuedConnection";
        
        // Enable direct FFmpeg mode in the VideoPane
        videoPane->enableDirectFFmpegMode(true);
        qCDebug(log_ffmpeg_backend) << "Enabled direct FFmpeg mode in VideoPane";
    }
}

// Device availability and hotplug support methods
bool FFmpegBackendHandler::checkCameraAvailable(const QString& devicePath)
{
    QString device = devicePath.isEmpty() ? m_currentDevice : devicePath;
    
    if (device.isEmpty()) {
        qCDebug(log_ffmpeg_backend) << "No device path provided for availability check";
        return false;
    }
    
    qCDebug(log_ffmpeg_backend) << "Checking camera availability for device:" << device;
    
#ifdef Q_OS_WIN
    // On Windows, DirectShow device names like "video=Openterface" are not file paths
    // Skip file existence check for DirectShow devices
    if (device.startsWith("video=")) {
        qCDebug(log_ffmpeg_backend) << "DirectShow device detected, skipping file existence check:" << device;
    } else {
        // Check if device file exists and is accessible (for V4L2 devices)
        QFile deviceFile(device);
        if (!deviceFile.exists()) {
            qCDebug(log_ffmpeg_backend) << "Device file does not exist:" << device;
            return false;
        }
        
        // Try to open the device for reading to verify it's accessible
        // Skip this check if we're currently capturing to avoid device conflicts
        if (device == m_currentDevice && m_captureRunning) {
            qCDebug(log_ffmpeg_backend) << "Device is currently in use for capture, skipping file open check";
            return true;
        }
        
        if (!deviceFile.open(QIODevice::ReadOnly)) {
            qCDebug(log_ffmpeg_backend) << "Cannot open device for reading:" << device << "Error:" << deviceFile.errorString();
            return false;
        }
        
        deviceFile.close();
    }
#endif
    
    // Additional check: try to briefly open with FFmpeg to verify compatibility
    // Skip this intrusive check if device is currently being used for capture
    if (device == m_currentDevice && m_captureRunning) {
        qCDebug(log_ffmpeg_backend) << "Device is currently in use for capture, skipping FFmpeg compatibility check";
        return true;
    }
    
    // Skip intrusive FFmpeg check if we're waiting for device activation
    if (m_waitingForDevice) {
        qCDebug(log_ffmpeg_backend) << "Waiting for device activation, skipping intrusive FFmpeg compatibility check";
        return true; // Rely on OS-specific checks above
    }
    
    AVFormatContext* testContext = avformat_alloc_context();
    if (!testContext) {
        qCDebug(log_ffmpeg_backend) << "Failed to allocate test format context";
        return false;
    }
    
#ifdef Q_OS_WIN
    // On Windows, use DirectShow input format
    const AVInputFormat* inputFormat = av_find_input_format("dshow");
    if (!inputFormat) {
        qCDebug(log_ffmpeg_backend) << "DirectShow input format not available";
        avformat_free_context(testContext);
        return false;
    }
#else
    const AVInputFormat* inputFormat = av_find_input_format("v4l2");
    if (!inputFormat) {
        qCDebug(log_ffmpeg_backend) << "V4L2 input format not available";
        avformat_free_context(testContext);
        return false;
    }
#endif
    
    // Try to open the device with minimal options
    AVDictionary* options = nullptr;
    av_dict_set(&options, "framerate", "1", 0); // Very low framerate for quick test
    av_dict_set(&options, "video_size", "160x120", 0); // Very small resolution for quick test
    
    int ret = avformat_open_input(&testContext, device.toUtf8().constData(), inputFormat, &options);
    av_dict_free(&options);
    
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
        qCDebug(log_ffmpeg_backend) << "FFmpeg cannot open device:" << device << "Error:" << QString::fromUtf8(errbuf);
        avformat_free_context(testContext);
        return false;
    }
    
    // Device opened successfully, clean up
    avformat_close_input(&testContext);
    qCDebug(log_ffmpeg_backend) << "Camera device is available:" << device;
    return true;
}

bool FFmpegBackendHandler::isCurrentDeviceAvailable() const
{
    // If capture is currently running, assume device is available to avoid interference
    if (m_captureRunning) {
        qCDebug(log_ffmpeg_backend) << "Capture is running, assuming device is available";
        return true;
    }
    
    return const_cast<FFmpegBackendHandler*>(this)->checkCameraAvailable(m_currentDevice);
}

void FFmpegBackendHandler::handleDeviceDisconnection()
{
    qCDebug(log_ffmpeg_backend) << "Handling device disconnection for FFmpeg backend";
    
    // Stop current capture if running
    if (m_captureRunning) {
        qCDebug(log_ffmpeg_backend) << "Stopping capture due to device disconnection";
        stopDirectCapture();
    }
    
    // Emit device deactivation signal
    emit deviceDeactivated(m_currentDevice);
    
    // Emit error signal to notify UI
    emit captureError("Camera device disconnected: " + m_currentDevice);
    
    qCDebug(log_ffmpeg_backend) << "Device disconnection handled";
}

bool FFmpegBackendHandler::restartCaptureWithDevice(const QString& devicePath, const QSize& resolution, int framerate)
{
    qCDebug(log_ffmpeg_backend) << "Attempting to restart capture with device:" << devicePath;
    
    // Stop current capture first
    if (m_captureRunning) {
        qCDebug(log_ffmpeg_backend) << "Stopping current capture before restart";
        stopDirectCapture();
        QThread::msleep(200); // Give time for cleanup
    }
    
    // Check if new device is available
    if (!checkCameraAvailable(devicePath)) {
        qCWarning(log_ffmpeg_backend) << "New device not available for restart:" << devicePath;
        return false;
    }
    
    // Update current device
    m_currentDevice = devicePath;
    m_currentResolution = resolution.isValid() ? resolution : QSize(1920, 1080);
    m_currentFramerate = framerate > 0 ? framerate : 30;
    
    qCDebug(log_ffmpeg_backend) << "Restarting capture with device:" << m_currentDevice
                                << "resolution:" << m_currentResolution 
                                << "framerate:" << m_currentFramerate;
    
    // Start capture with new device
    bool success = startDirectCapture(m_currentDevice, m_currentResolution, m_currentFramerate);
    
    if (success) {
        qCDebug(log_ffmpeg_backend) << "Successfully restarted capture with new device";
        emit deviceActivated(m_currentDevice);
    } else {
        qCWarning(log_ffmpeg_backend) << "Failed to restart capture with new device";
    }
    
    return success;
}

// Enhanced hotplug support methods
void FFmpegBackendHandler::connectToHotplugMonitor()
{
    qCDebug(log_ffmpeg_backend) << "Connecting FFmpegBackendHandler to hotplug monitor";
    
    DeviceManager& deviceManager = DeviceManager::getInstance();
    m_hotplugMonitor = deviceManager.getHotplugMonitor();
    
    if (!m_hotplugMonitor) {
        qCWarning(log_ffmpeg_backend) << "Failed to get hotplug monitor from device manager";
        return;
    }
    
    // Connect to device unplugging signal
    connect(m_hotplugMonitor, &HotplugMonitor::deviceUnplugged,
            this, [this](const DeviceInfo& device) {
                qCInfo(log_ffmpeg_backend) << "FFmpeg: Device unplugged event received";
                qCInfo(log_ffmpeg_backend) << "  Port Chain:" << device.portChain;
                qCInfo(log_ffmpeg_backend) << "  Current device port chain:" << m_currentDevicePortChain;
                qCInfo(log_ffmpeg_backend) << "  Current device:" << m_currentDevice;
                qCInfo(log_ffmpeg_backend) << "  Capture running:" << m_captureRunning;
                
                // Match by port chain like serial port manager does
                // This works even when DeviceInfo doesn't have camera info populated yet
                if (!m_currentDevicePortChain.isEmpty() && 
                    m_currentDevicePortChain == device.portChain) {
                    qCInfo(log_ffmpeg_backend) << "  → Our current camera device was unplugged, stopping capture";
                    
                    // Close immediately like serial port does - don't wait for I/O errors
                    if (m_captureRunning) {
                        // Use QTimer to avoid blocking the hotplug signal handler
                        QTimer::singleShot(0, this, [this]() {
                            handleDeviceDeactivation(m_currentDevice);
                        });
                    }
                } else {
                    qCDebug(log_ffmpeg_backend) << "  → Unplugged device is not our current camera, ignoring";
                }
            });
            
    // Connect to new device plugged in signal
    connect(m_hotplugMonitor, &HotplugMonitor::newDevicePluggedIn,
            this, [this](const DeviceInfo& device) {
                qCInfo(log_ffmpeg_backend) << "FFmpeg: New device plugged in event received";
                qCInfo(log_ffmpeg_backend) << "  Port Chain:" << device.portChain;
                qCInfo(log_ffmpeg_backend) << "  Has Camera:" << device.hasCameraDevice();
                qCInfo(log_ffmpeg_backend) << "  Camera Path:" << device.cameraDevicePath;
                qCInfo(log_ffmpeg_backend) << "  Camera ID:" << device.cameraDeviceId;
                qCInfo(log_ffmpeg_backend) << "  Waiting for device:" << m_waitingForDevice;
                qCInfo(log_ffmpeg_backend) << "  Expected device:" << m_expectedDevicePath;
                qCInfo(log_ffmpeg_backend) << "  Capture running:" << m_captureRunning;
                
                // Get device path - either from DeviceInfo or try to find it
                QString devicePath = device.cameraDevicePath;
                
                // If device doesn't have camera info yet, wait and retry
                if (!device.hasCameraDevice() || devicePath.isEmpty()) {
                    qCDebug(log_ffmpeg_backend) << "  → Device has no camera info yet, will retry after delay";
                    
                    // Capture port chain for later use
                    QString portChain = device.portChain;
                    
                    // Wait 1 second for camera enumeration to complete, then retry
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
                            if (m_waitingForDevice) {
                                qCInfo(log_ffmpeg_backend) << "  → Found device after retry, attempting activation:" << foundDeviceName;
                                // Add small delay to allow device to fully initialize after reconnection
                                QTimer::singleShot(300, this, [this, foundDeviceName, portChain]() {
                                    handleDeviceActivation(foundDeviceName, portChain);
                                });
                            } else if (!m_captureRunning) {
                                qCInfo(log_ffmpeg_backend) << "  → Found device after retry, auto-starting capture:" << foundDeviceName;
                                QTimer::singleShot(300, this, [this, foundDeviceName, portChain]() {
                                    if (!m_captureRunning) {
                                        handleDeviceActivation(foundDeviceName, portChain);
                                    }
                                });
                            }
                        } else {
                            qCWarning(log_ffmpeg_backend) << "  → No camera device found after retry for port chain:" << portChain;
                        }
                    });
                    return;
                }
                
                // If we're waiting for a device (after unplug), activate it
                if (m_waitingForDevice) {
                    if (!devicePath.isEmpty() && 
                        (m_expectedDevicePath.isEmpty() || devicePath == m_expectedDevicePath)) {
                        qCInfo(log_ffmpeg_backend) << "  → Found expected device, attempting activation:" << devicePath;
                        QString portChain = device.portChain;
                        // Use QTimer to avoid blocking the hotplug signal handler
                        QTimer::singleShot(0, this, [this, devicePath, portChain]() {
                            handleDeviceActivation(devicePath, portChain);
                        });
                    } else {
                        qCDebug(log_ffmpeg_backend) << "  → Device path doesn't match expected device";
                    }
                    return;
                }
                
                // If capture is not running and we have a camera device, try to start capture
                // This handles the case where camera was unplugged and plugged back in
                if (!m_captureRunning && !devicePath.isEmpty()) {
                    // Check if this might be the device we were using before
                    bool shouldAutoStart = false;
                    
                    // If we have a stored current device path that matches
                    if (!m_currentDevice.isEmpty() && devicePath == m_currentDevice) {
                        shouldAutoStart = true;
                        qCInfo(log_ffmpeg_backend) << "  → Detected previously used camera device, will auto-restart capture";
                    }
                    // Or if we don't have any device set yet and this is the first camera
                    else if (m_currentDevice.isEmpty()) {
                        shouldAutoStart = true;
                        qCInfo(log_ffmpeg_backend) << "  → Detected new camera device and no capture running, will auto-start capture";
                    }
                    
                    if (shouldAutoStart) {
                        QString portChain = device.portChain;
                        // Use a short delay to ensure device is fully initialized
                        // This also prevents blocking the hotplug event handler
                        QTimer::singleShot(300, this, [this, devicePath, portChain]() {
                            if (!m_captureRunning) {
                                qCInfo(log_ffmpeg_backend) << "Auto-starting capture for plugged-in device:" << devicePath;
                                handleDeviceActivation(devicePath, portChain);
                            }
                        });
                    } else {
                        qCDebug(log_ffmpeg_backend) << "  → New camera device detected but not auto-starting (different from previous device)";
                    }
                } else {
                    qCDebug(log_ffmpeg_backend) << "  → Capture already running or no valid device path, ignoring plug-in event";
                }
            });
            
    qCDebug(log_ffmpeg_backend) << "FFmpegBackendHandler successfully connected to hotplug monitor";
}

void FFmpegBackendHandler::disconnectFromHotplugMonitor()
{
    qCDebug(log_ffmpeg_backend) << "Disconnecting FFmpegBackendHandler from hotplug monitor";
    
    if (m_hotplugMonitor) {
        disconnect(m_hotplugMonitor, nullptr, this, nullptr);
        m_hotplugMonitor = nullptr;
        qCDebug(log_ffmpeg_backend) << "FFmpegBackendHandler disconnected from hotplug monitor";
    }
}

void FFmpegBackendHandler::waitForDeviceActivation(const QString& devicePath, int timeoutMs)
{
    qCDebug(log_ffmpeg_backend) << "Waiting for device activation:" << devicePath << "timeout:" << timeoutMs << "ms";
    
    m_expectedDevicePath = devicePath.isEmpty() ? m_currentDevice : devicePath;
    m_waitingForDevice = true;
    
    emit waitingForDevice(m_expectedDevicePath);
    
    // Start timeout timer
    if (timeoutMs > 0) {
        m_deviceWaitTimer->start(timeoutMs);
    }
    
    // Create a periodic check timer
    QTimer* checkTimer = new QTimer(this);
    checkTimer->setInterval(1000); // Check every second
    
    connect(checkTimer, &QTimer::timeout, this, [this, checkTimer]() {
        if (!m_waitingForDevice) {
            checkTimer->deleteLater();
            return;
        }
        
        // Check if expected device becomes available
        if (!m_expectedDevicePath.isEmpty() && checkCameraAvailable(m_expectedDevicePath)) {
            qCDebug(log_ffmpeg_backend) << "Expected device became available during wait:" << m_expectedDevicePath;
            checkTimer->deleteLater();
            handleDeviceActivation(m_expectedDevicePath);
        }
    });
    
    checkTimer->start();
    qCDebug(log_ffmpeg_backend) << "Started waiting for device activation";
}

void FFmpegBackendHandler::handleDeviceActivation(const QString& devicePath, const QString& portChain)
{
    qCInfo(log_ffmpeg_backend) << "Handling device activation:" << devicePath << "port chain:" << portChain;
    
    m_waitingForDevice = false;
    m_deviceWaitTimer->stop();
    
    if (!devicePath.isEmpty()) {
        m_currentDevice = devicePath;
        m_currentDevicePortChain = portChain;  // Store port chain for unplug detection
        qCDebug(log_ffmpeg_backend) << "Stored current device port chain:" << m_currentDevicePortChain;
    }
    
    // Start capture with current settings, or auto-detect if not set
    QSize resolution = m_currentResolution.isValid() ? m_currentResolution : QSize(0, 0);
    int framerate = m_currentFramerate > 0 ? m_currentFramerate : 0;
    
    qCDebug(log_ffmpeg_backend) << "Starting capture on activated device:" << m_currentDevice
                                << "resolution:" << resolution << "framerate:" << framerate;
    
    // Delay starting capture to allow device to stabilize after hotplug
    QTimer::singleShot(300, this, [this, resolution, framerate]() {
        if (startDirectCapture(m_currentDevice, resolution, framerate)) {
            qCInfo(log_ffmpeg_backend) << "Successfully started capture on activated device";
            emit deviceActivated(m_currentDevice);
        } else {
            qCWarning(log_ffmpeg_backend) << "Failed to start capture on activated device";
            emit captureError("Failed to start capture on activated device: " + m_currentDevice);
        }
    });
}

void FFmpegBackendHandler::handleDeviceDeactivation(const QString& devicePath)
{
    qCInfo(log_ffmpeg_backend) << "Handling device deactivation:" << devicePath;
    QString deactivatedDevice = devicePath.isEmpty() ? m_currentDevice : devicePath;

    m_suppressErrors = true;
    av_log_set_level(AV_LOG_QUIET);

    // STOP CAPTURE FIRST - This now properly waits for thread exit & cleans resources
    if (m_captureRunning) {
        qCDebug(log_ffmpeg_backend) << "Stopping capture due to device deactivation";
        stopDirectCapture(); // <-- This now includes full cleanup
    }

    // NOW clear device state safely (mutex protected inside stopDirectCapture/cleanup)
    {
        QMutexLocker locker(&m_mutex); // Protect access to member vars
        m_currentDevicePortChain.clear();
        m_currentResolution = QSize();
        m_currentFramerate = 0;
        m_currentDevice.clear(); // <-- Also clear the device path itself
    }

    qCDebug(log_ffmpeg_backend) << "Cleared current device port chain and settings";
    emit deviceDeactivated(deactivatedDevice);

    qCInfo(log_ffmpeg_backend) << "Starting to wait for device reconnection";
}

void FFmpegBackendHandler::setCurrentDevicePortChain(const QString& portChain)
{
    m_currentDevicePortChain = portChain;
    qCDebug(log_ffmpeg_backend) << "Set current device port chain to:" << m_currentDevicePortChain;
}

void FFmpegBackendHandler::setCurrentDevice(const QString& devicePath)
{
    m_currentDevice = devicePath;
    qCDebug(log_ffmpeg_backend) << "Set current device to:" << m_currentDevice;
}

// ============================================================================
// Video Recording Implementation - Delegated to FFmpegRecorder
// ============================================================================

bool FFmpegBackendHandler::startRecording(const QString& outputPath, const QString& format, int videoBitrate)
{
    if (!m_recorder) {
        qCWarning(log_ffmpeg_backend) << "Recorder not initialized";
        emit recordingError("Recorder not initialized");
        return false;
    }
    
    if (!m_captureRunning) {
        qCWarning(log_ffmpeg_backend) << "Cannot start recording: capture is not running";
        emit recordingError("Cannot start recording: capture is not running");
        return false;
    }
    
    // Get current capture resolution and framerate
    QSize resolution = m_currentResolution.isValid() ? m_currentResolution : QSize(1920, 1080);
    int framerate = m_currentFramerate > 0 ? m_currentFramerate : 30;
    
    bool success = m_recorder->StartRecording(outputPath, format, videoBitrate, resolution, framerate);
    
    if (success) {
        m_recordingActive = true;
        emit recordingStarted(outputPath);
    } else {
        emit recordingError("Failed to initialize recording");
    }
    
    return success;
}

bool FFmpegBackendHandler::stopRecording()
{
    if (!m_recorder) {
        return false;
    }
    
    bool success = m_recorder->StopRecording();
    
    if (success) {
        m_recordingActive = false;
        emit recordingStopped();
    }
    
    return success;
}

void FFmpegBackendHandler::pauseRecording()
{
    if (m_recorder) {
        m_recorder->PauseRecording();
        emit recordingPaused();
    }
}

void FFmpegBackendHandler::resumeRecording()
{
    if (m_recorder) {
        m_recorder->ResumeRecording();
        emit recordingResumed();
    }
}

bool FFmpegBackendHandler::isRecording() const
{
    return m_recorder ? m_recorder->IsRecording() : false;
}

bool FFmpegBackendHandler::isPaused() const
{
    return m_recorder ? m_recorder->IsPaused() : false;
}

QString FFmpegBackendHandler::getCurrentRecordingPath() const
{
    return m_recorder ? m_recorder->GetCurrentRecordingPath() : QString();
}

qint64 FFmpegBackendHandler::getRecordingDuration() const
{
    return m_recorder ? m_recorder->GetRecordingDuration() : 0;
}

void FFmpegBackendHandler::setRecordingConfig(const RecordingConfig& config)
{
    if (m_recorder) {
        m_recorder->SetRecordingConfig(config);
    }
}

RecordingConfig FFmpegBackendHandler::getRecordingConfig() const
{
    return m_recorder ? m_recorder->GetRecordingConfig() : RecordingConfig();
}

// Advanced recording methods
bool FFmpegBackendHandler::isCameraReady() const
{
    AVFormatContext* formatContext = m_deviceManager ? m_deviceManager->GetFormatContext() : nullptr;
    AVCodecContext* codecContext = m_deviceManager ? m_deviceManager->GetCodecContext() : nullptr;
    return m_captureRunning && formatContext != nullptr && codecContext != nullptr;
}

bool FFmpegBackendHandler::supportsAdvancedRecording() const
{
    return m_recorder ? m_recorder->SupportsAdvancedRecording() : false;
}

bool FFmpegBackendHandler::startRecordingAdvanced(const QString& outputPath, const RecordingConfig& config)
{
    if (!m_recorder) {
        return false;
    }
    
    m_recorder->SetRecordingConfig(config);
    return startRecording(outputPath, config.format, config.video_bitrate);
}

bool FFmpegBackendHandler::forceStopRecording()
{
    if (!m_recorder) {
        return false;
    }
    
    bool success = m_recorder->ForceStopRecording();
    
    if (success) {
        m_recordingActive = false;
        emit recordingStopped();
    }
    
    return success;
}

QString FFmpegBackendHandler::getLastError() const
{
    return m_lastError;
}

bool FFmpegBackendHandler::supportsRecordingStats() const
{
    return m_recorder ? m_recorder->SupportsRecordingStats() : false;
}

qint64 FFmpegBackendHandler::getRecordingFileSize() const
{
    return m_recorder ? m_recorder->GetRecordingFileSize() : 0;
}

void FFmpegBackendHandler::takeImage(const QString& filePath)
{
    if (!m_frameProcessor || !m_recorder) {
        qCWarning(log_ffmpeg_backend) << "Frame processor or recorder not initialized";
        return;
    }
    
    QPixmap latestFrame = QPixmap::fromImage(m_frameProcessor->GetLatestFrame());
    m_recorder->TakeImage(filePath, latestFrame);
}

void FFmpegBackendHandler::takeAreaImage(const QString& filePath, const QRect& captureArea)
{
    if (!m_frameProcessor || !m_recorder) {
        qCWarning(log_ffmpeg_backend) << "Frame processor or recorder not initialized";
        return;
    }
    
    QPixmap latestFrame = QPixmap::fromImage(m_frameProcessor->GetLatestFrame());
    m_recorder->TakeAreaImage(filePath, latestFrame, captureArea);
}

#include "ffmpegbackendhandler.moc"
