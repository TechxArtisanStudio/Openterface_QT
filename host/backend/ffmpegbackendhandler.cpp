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

FFmpegBackendHandler::FFmpegBackendHandler(QObject *parent)
    : MultimediaBackendHandler(parent)
    , m_frame(nullptr),
    m_frameRGB(nullptr),
    m_packet(nullptr),
      m_swsContext(nullptr),
      m_deviceManager(std::make_unique<FFmpegDeviceManager>()),
      m_hardwareAccelerator(std::make_unique<FFmpegHardwareAccelerator>()),
      m_captureRunning(false),
      m_videoStreamIndex(-1),
      m_frameCount(0),
      m_lastFrameTime(0),
      m_recordingFormatContext(nullptr),
      m_recordingCodecContext(nullptr),
      m_recordingVideoStream(nullptr),
      m_recordingSwsContext(nullptr),
      m_recordingFrame(nullptr),
      m_recordingPacket(nullptr),
      m_interruptRequested(false),
      m_operationStartTime(0)
#ifdef HAVE_LIBJPEG_TURBO
      , m_turboJpegHandle(nullptr)
#endif
      , m_hotplugMonitor(nullptr),
      m_waitingForDevice(false),
      m_deviceWaitTimer(nullptr),
      m_suppressErrors(false),
      m_graphicsVideoItem(nullptr),
      m_videoPane(nullptr),
      m_recordingActive(false),
      m_recordingPaused(false),
      m_recordingStartTime(0),
      m_recordingPausedTime(0),
      m_totalPausedDuration(0),
      m_lastRecordedFrameTime(0),
      m_recordingTargetFramerate(30),
      m_recordingFrameNumber(0)
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
    if (m_recordingActive) {
        stopRecording();
    }
    
    // Disconnect from hotplug monitor
    disconnectFromHotplugMonitor();
    
    stopDirectCapture();
    cleanupRecording();
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
    qCDebug(log_ffmpeg_backend) << "FFmpeg: Configuring camera device";

    qCDebug(log_ffmpeg_backend) << "FFmpeg: Camera device configuration";
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
    
#ifdef HAVE_LIBJPEG_TURBO
    // Initialize TurboJPEG decompressor
    m_turboJpegHandle = tjInitDecompress();
    if (!m_turboJpegHandle) {
        qCWarning(log_ffmpeg_backend) << "Failed to initialize TurboJPEG decompressor:" << tjGetErrorStr();
        qCDebug(log_ffmpeg_backend) << "Will fall back to FFmpeg decoder for MJPEG frames";
    } else {
        qCDebug(log_ffmpeg_backend) << "TurboJPEG decompressor initialized successfully";
    }
#else
    qCDebug(log_ffmpeg_backend) << "TurboJPEG support not compiled in";
#endif
    
    qCDebug(log_ffmpeg_backend) << "FFmpeg initialization completed";
    return true;
}

void FFmpegBackendHandler::cleanupFFmpeg()
{
    qCDebug(log_ffmpeg_backend) << "Cleaning up FFmpeg";
    
    closeInputDevice();
    
    // Cleanup TurboJPEG
#ifdef HAVE_LIBJPEG_TURBO
    if (m_turboJpegHandle) {
        tjDestroy(m_turboJpegHandle);
        m_turboJpegHandle = nullptr;
    }
#endif
    
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
                m_captureThread->wait(1000);
                if (m_captureThread->isRunning()) {
                    qCCritical(log_ffmpeg_backend) << "Capture thread did not exit gracefully!";
                }
                m_captureThread.reset();
                qCDebug(log_ffmpeg_backend) << "Capture thread terminated and cleaned up";
                
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
    
    // Allocate frames
    m_frame = make_av_frame();
    m_frameRGB = make_av_frame();
    m_packet = make_av_packet();
    
    if (!m_frame || !m_frameRGB || !m_packet) {
        qCCritical(log_ffmpeg_backend) << "Failed to allocate frames or packet";
        return false;
    }
    
    // Reset operation timer - device opened successfully
    m_operationStartTime = 0;
    
    return true;
}

void FFmpegBackendHandler::closeInputDevice()
{
    // Free frames and packet
    if (m_frame) {
        AV_FRAME_RESET(m_frame);
    }
    
    if (m_frameRGB) {
        AV_FRAME_RESET(m_frameRGB);
    }
    
    if (m_packet) {
        AV_PACKET_RESET(m_packet);
    }
    
    // Free scaling context
    if (m_swsContext) {
        sws_freeContext(m_swsContext);
        m_swsContext = nullptr;
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
    
    if (!m_captureRunning || !formatContext || !codecContext) {
        return; // Exit early if capture is stopping or resources are gone
    }

    if (!m_packet || !codecContext) {
        return;
    }
    
    // Validate packet data before processing
    if (AV_PACKET_RAW(m_packet)->size <= 0 || !AV_PACKET_RAW(m_packet)->data) {
        if (AV_PACKET_RAW(m_packet)->size > 0 && !AV_PACKET_RAW(m_packet)->data) {
            qCWarning(log_ffmpeg_backend) << "Invalid packet: null data but size" << AV_PACKET_RAW(m_packet)->size;
        }
        av_packet_unref(AV_PACKET_RAW(m_packet));
        return;
    }
    
    // RESPONSIVENESS OPTIMIZATION: Implement frame dropping for better mouse response
    // But be less aggressive when recording is active
    static qint64 lastProcessTime = 0;
    static int droppedFrames = 0;
    qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
    
    // Check if recording is active
    bool isRecording = false;
    {
        QMutexLocker recordingLocker(&m_recordingMutex);
        isRecording = m_recordingActive && !m_recordingPaused;
    }
    
    // Drop frames if we're processing too slowly, but be less aggressive when recording
    // IMPROVED: More reasonable thresholds - 20ms for recording (allows 30fps), 2ms for display-only
    int frameDropThreshold = isRecording ? 20 : 2; 
    if (currentTime - lastProcessTime < frameDropThreshold) {
        droppedFrames++;
        av_packet_unref(AV_PACKET_RAW(m_packet));
        return;
    }
    lastProcessTime = currentTime;
    
    // Log dropped frames occasionally for monitoring
    if (droppedFrames > 0 && m_frameCount % 1000 == 0) {
        qCDebug(log_ffmpeg_backend) << "Dropped" << droppedFrames << "frames for responsiveness (last 1000 frames)";
        droppedFrames = 0;
    }
    
    QPixmap pixmap;
    
    // Determine if we're using a hardware decoder
    bool usingHardwareDecoder = false;
    if (codecContext->codec) {
        const char* codecName = codecContext->codec->name;
        // Check if this is a hardware decoder (ends with _cuvid, _qsv, etc.)
        usingHardwareDecoder = (strstr(codecName, "_cuvid") != nullptr || 
                               strstr(codecName, "_qsv") != nullptr ||
                               strstr(codecName, "_nvdec") != nullptr);
    }
    
    // PRIORITY ORDER for MJPEG decoding:
    // 1. Hardware decoder (CUVID/NVDEC, QSV) - Best performance, GPU acceleration
    // 2. TurboJPEG - Fast CPU-based JPEG decoder
    // 3. FFmpeg software decoder - Fallback option
    
    if (codecContext->codec_id == AV_CODEC_ID_MJPEG) {
        if (usingHardwareDecoder) {
            // Use hardware decoder (CUVID/NVDEC or QSV)
            static int hwFrameCount = 0;
            hwFrameCount++;
            
            if (hwFrameCount % 1000 == 1) {
                const char* hwType = (strstr(codecContext->codec->name, "cuvid") != nullptr) ? "NVDEC/CUVID" : "Hardware";
                qCInfo(log_ffmpeg_backend) << "Using" << hwType << "GPU acceleration for MJPEG (frame" << hwFrameCount << ")";
            }
            
            pixmap = decodeFrame(AV_PACKET_RAW(m_packet));
            
            // If hardware decoder fails, try TurboJPEG fallback
            if (pixmap.isNull()) {
#ifdef HAVE_LIBJPEG_TURBO
                if (hwFrameCount % 1000 == 1) {
                    qCWarning(log_ffmpeg_backend) << "Hardware decoder failed, falling back to TurboJPEG";
                }
                pixmap = decodeJpegFrame(AV_PACKET_RAW(m_packet)->data, AV_PACKET_RAW(m_packet)->size);
#endif
            }
        } else {
            // No hardware decoder - use TurboJPEG or FFmpeg software decoder
#ifdef HAVE_LIBJPEG_TURBO
            // Use TurboJPEG for significant performance improvement on MJPEG
            static int turbojpegFrameCount = 0;
            turbojpegFrameCount++;
            
            // RESPONSIVENESS: Reduce logging overhead
            if (turbojpegFrameCount % 5000 == 1) {
                qCDebug(log_ffmpeg_backend) << "Using TurboJPEG CPU acceleration (frame" << turbojpegFrameCount << ")";
            }
            
            // Additional validation for JPEG data
            if (AV_PACKET_RAW(m_packet)->size < 10) { // Minimum JPEG header size
                if (turbojpegFrameCount % 5000 == 1) {
                    qCWarning(log_ffmpeg_backend) << "JPEG packet too small:" << AV_PACKET_RAW(m_packet)->size << "bytes, falling back to FFmpeg decoder";
                }
                pixmap = decodeFrame(AV_PACKET_RAW(m_packet));
            } else {
                pixmap = decodeJpegFrame(AV_PACKET_RAW(m_packet)->data, AV_PACKET_RAW(m_packet)->size);
                
                // If TurboJPEG failed, fall back to FFmpeg decoder
                if (pixmap.isNull()) {
                    if (turbojpegFrameCount % 5000 == 1) {
                        qCDebug(log_ffmpeg_backend) << "TurboJPEG failed, falling back to FFmpeg software decoder";
                    }
                    pixmap = decodeFrame(AV_PACKET_RAW(m_packet));
                }
            }
#else
            // RESPONSIVENESS: Only log this occasionally to reduce overhead
            static int noTurboLogCount = 0;
            if (++noTurboLogCount % 5000 == 1) {
                qCDebug(log_ffmpeg_backend) << "Using FFmpeg software decoder for MJPEG frame (TurboJPEG not available)";
            }
            pixmap = decodeFrame(AV_PACKET_RAW(m_packet));
#endif
        }
    } else {
        // Non-MJPEG codecs use FFmpeg decoder
        static int ffmpegFrameCount = 0;
        ffmpegFrameCount++;
        
        // Only log every 1000th frame to minimize debug overhead
        if (ffmpegFrameCount % 1000 == 1) {
            qCDebug(log_ffmpeg_backend) << "Using FFmpeg decoder (frame" << ffmpegFrameCount << "), codec:" << codecContext->codec_id;
        }
        pixmap = decodeFrame(AV_PACKET_RAW(m_packet));
    }
    
    // Clean up packet
    av_packet_unref(AV_PACKET_RAW(m_packet));
    
    if (!pixmap.isNull()) {
        m_frameCount++;
        
        // Drastically reduce logging frequency for performance (only log every 1000th frame)
        if (m_frameCount % 1000 == 1) {
            qCDebug(log_ffmpeg_backend) << "Successfully decoded frame" << m_frameCount << "of size:" << pixmap.size();
        }
        
        // CRITICAL FIX: Skip first few frames to allow device signal to stabilize
        // Many USB video capture devices (including Openterface) output black frames initially
        // This can be configured via environment variable if needed
        // OPTIMIZATION: Skip this when using hardware acceleration as it's not needed
        static int startupFramesToSkip = -1;
        if (startupFramesToSkip == -1) {
            // Check environment variable first, otherwise use default
            const char* envSkipFrames = qgetenv("OPENTERFACE_SKIP_FRAMES").constData();
            if (envSkipFrames && strlen(envSkipFrames) > 0) {
                startupFramesToSkip = QString(envSkipFrames).toInt();
                qCDebug(log_ffmpeg_backend) << "Using environment variable OPENTERFACE_SKIP_FRAMES:" << startupFramesToSkip;
            } else {
                // When using hardware acceleration, don't skip frames (HW decoders are stable from start)
                if (usingHardwareDecoder) {
                    startupFramesToSkip = 0; // No frame skipping needed with HW acceleration
                    qCInfo(log_ffmpeg_backend) << "Hardware decoder detected - skipping frame stabilization wait";
                } else {
                    startupFramesToSkip = 5; // Default: skip first 5 frames for software decoders
                    qCDebug(log_ffmpeg_backend) << "Software decoder - will skip first" << startupFramesToSkip << "frames for signal stabilization";
                }
            }
        }
        
        if (m_frameCount <= startupFramesToSkip) {
            qCDebug(log_ffmpeg_backend) << "Skipping startup frame" << m_frameCount << "of" << startupFramesToSkip << "- waiting for signal to stabilize";
            return; // Don't emit this frame, just continue
        }
        
        // PERFORMANCE: Skip frame analysis completely after initialization to reduce CPU usage
        // Frame analysis was only needed for initial debugging and is now disabled for production use
        
        // Reduce emission logging frequency for performance
        if (m_frameCount % 1000 == 1) {
            qCDebug(log_ffmpeg_backend) << "Emitting frameReady signal with pixmap size:" << pixmap.size();
        }
        
        // Store the latest frame for image capture
        QImage img = pixmap.toImage();
        {
            QMutexLocker locker(&m_mutex);
            m_latestFrame = img;
        }
        
        // CRITICAL: Use QueuedConnection to ensure UI thread handles frame updates properly
        // This prevents blocking the capture thread and ensures smooth frame delivery
        if (m_captureRunning) {
            emit frameReady(pixmap);
        }
        
        // Write frame to recording file if recording is active
        // Use mutex to safely check recording state and prevent race conditions during stop
        bool shouldRecord = false;
        {
            QMutexLocker recordingLocker(&m_recordingMutex);
            shouldRecord = m_recordingActive && !m_recordingPaused && m_recordingFrame && m_recordingSwsContext;
        }
        
        if (shouldRecord) {
            // FRAME RATE CONTROL: Only write frames at the target recording framerate
            // Check if enough time has passed since the last recorded frame
            qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
            
            bool shouldWriteFrame = false;
            {
                QMutexLocker recordingLocker(&m_recordingMutex);
                if (m_recordingActive && !m_recordingPaused) {
                    // IMPROVED: Use frame count based on elapsed time to prevent timing jitter
                    // Calculate expected frame number based on actual elapsed recording time
                    qint64 elapsedMs = currentTime - m_recordingStartTime - m_totalPausedDuration;
                    int64_t expectedFrameNumber = (elapsedMs * m_recordingTargetFramerate) / 1000;
                    
                    // Write frame if we're behind or at the expected frame count
                    if (m_recordingFrameNumber <= expectedFrameNumber) {
                        shouldWriteFrame = true;
                        m_lastRecordedFrameTime = currentTime;
                    } else {
                        // We're ahead of schedule - skip this frame to maintain timing
                        static int skipLogCount = 0;
                        if (++skipLogCount % 100 == 0) {
                            qCDebug(log_ffmpeg_backend) << "Skipping frame - ahead of schedule:" 
                                                       << "recorded:" << m_recordingFrameNumber 
                                                       << "expected:" << expectedFrameNumber;
                        }
                    }
                }
            }
            
            if (shouldWriteFrame) {
                // Convert pixmap to AVFrame for recording
                QImage image = pixmap.toImage().convertToFormat(QImage::Format_RGB888);
                if (!image.isNull()) {
                    // Debug logging for recording frame processing
                    static int recordingDebugCount = 0;
                    if (++recordingDebugCount <= 10 || recordingDebugCount % 30 == 0) {
                        qCDebug(log_ffmpeg_backend) << "Writing recording frame" << recordingDebugCount 
                                                   << "- image size:" << image.size()
                                                   << "recording frame size:" << AV_FRAME_RAW(m_recordingFrame)->width << "x" << AV_FRAME_RAW(m_recordingFrame)->height
                                                   << "frame interval:" << (currentTime - m_lastRecordedFrameTime) << "ms";
                    }
                    bool canWriteFrame = false;
                    {
                        QMutexLocker recordingLocker(&m_recordingMutex);
                        canWriteFrame = m_recordingActive && !m_recordingPaused && m_recordingFrame && m_recordingSwsContext;
                    }
                    
                    if (canWriteFrame) {
                        // Fill frame with image data
                        const uint8_t* srcData[1] = { image.constBits() };
                        int srcLinesize[1] = { static_cast<int>(image.bytesPerLine()) };
                        
                        // Convert RGB to target pixel format for encoding (YUVJ420P for MJPEG)
                        int scaleResult = sws_scale(m_recordingSwsContext, srcData, srcLinesize, 0, image.height(),
                                                  AV_FRAME_RAW(m_recordingFrame)->data, AV_FRAME_RAW(m_recordingFrame)->linesize);
                        
                        if (scaleResult != image.height()) {
                            qCWarning(log_ffmpeg_backend) << "sws_scale conversion warning: converted" << scaleResult << "lines, expected" << image.height();
                        }
                        
                        // Write frame to file (this function will handle mutex locking internally)
                    if (!writeFrameToFile(AV_FRAME_RAW(m_recordingFrame))) {
                        // Frame was skipped (likely because recording is stopping) - this is normal
                    } else if (recordingDebugCount <= 10) {
                        qCDebug(log_ffmpeg_backend) << "Successfully wrote recording frame" << recordingDebugCount;
                    }
                    
                    // Update recording duration periodically
                    static int recordingFrameCount = 0;
                    if (++recordingFrameCount % 30 == 0) { // Every 30 frames (~1 second at 30fps)
                        emit recordingDurationChanged(getRecordingDuration());
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
}

#ifdef HAVE_LIBJPEG_TURBO
QPixmap FFmpegBackendHandler::decodeJpegFrame(const uint8_t* data, int size)
{
    // PERFORMANCE OPTIMIZED: Minimal validation for TurboJPEG
    if (!m_turboJpegHandle) {
        return QPixmap(); // Will trigger fallback to FFmpeg decoder
    }
    
    if (!data || size <= 10) { // Quick size check
        return QPixmap();
    }
    
    // Quick JPEG magic bytes check (essential for safety)
    if (data[0] != 0xFF || data[1] != 0xD8) {
        return QPixmap();
    }
    
    int width, height, subsamp, colorspace;
    
    // Get JPEG header information
    if (tjDecompressHeader3(m_turboJpegHandle, data, size, &width, &height, &subsamp, &colorspace) < 0) {
        return QPixmap(); // Silent failure for performance - will trigger FFmpeg fallback
    }
    
    // Quick dimension validation
    if (width <= 0 || height <= 0 || width > 4096 || height > 4096) {
        return QPixmap();
    }
    
    // Allocate buffer for RGB data
    QImage image(width, height, QImage::Format_RGB888);
    if (image.isNull()) {
        return QPixmap();
    }
    
    // Decompress JPEG directly to RGB888 format for Qt
    if (tjDecompress2(m_turboJpegHandle, data, size, image.bits(), width, 0, height, TJPF_RGB, TJFLAG_FASTDCT) < 0) {
        return QPixmap(); // Silent failure for performance
    }
    
    // Skip expensive pixel validation for performance - trust TurboJPEG output
    static int turbojpegSuccessCount = 0;
    turbojpegSuccessCount++;
    
    // Only log success every 2000th frame to confirm TurboJPEG is working
    if (turbojpegSuccessCount % 2000 == 1) {
        qCDebug(log_ffmpeg_backend) << "TurboJPEG: Successfully decoded" << width << "x" << height 
                                   << "MJPEG frame (success count:" << turbojpegSuccessCount << ")";
    }
    
    return QPixmap::fromImage(image);
}
#endif // HAVE_LIBJPEG_TURBO

QPixmap FFmpegBackendHandler::decodeFrame(AVPacket* packet)
{
    AVCodecContext* codecContext = m_deviceManager ? m_deviceManager->GetCodecContext() : nullptr;
    if (!codecContext || !m_frame) {
        qCWarning(log_ffmpeg_backend) << "decodeFrame: Missing codec context or frame";
        return QPixmap();
    }
    
    // Send packet to decoder
    int ret = avcodec_send_packet(codecContext, packet);
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
        qCWarning(log_ffmpeg_backend) << "Error sending packet to decoder:" << QString::fromUtf8(errbuf);
        return QPixmap();
    }
    
    // Receive frame from decoder
    ret = avcodec_receive_frame(codecContext, AV_FRAME_RAW(m_frame));
    if (ret < 0) {
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            return QPixmap(); // Not an error, just no frame ready
        }
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
        
        // Log decoder information for debugging hardware decoder issues
        static int errorCount = 0;
        if (++errorCount <= 5) {
            qCWarning(log_ffmpeg_backend) << "Error receiving frame from decoder:" << QString::fromUtf8(errbuf);
            qCWarning(log_ffmpeg_backend) << "  Decoder:" << (codecContext->codec ? codecContext->codec->name : "unknown");
            qCWarning(log_ffmpeg_backend) << "  Pixel format:" << codecContext->pix_fmt;
            qCWarning(log_ffmpeg_backend) << "  Error code:" << ret;
        }
        return QPixmap();
    }
    
    // Validate frame data
    if (!AV_FRAME_RAW(m_frame)->data[0]) {
        qCWarning(log_ffmpeg_backend) << "decodeFrame: Frame data is null";
        return QPixmap();
    }
    
    if (AV_FRAME_RAW(m_frame)->width <= 0 || AV_FRAME_RAW(m_frame)->height <= 0) {
        qCWarning(log_ffmpeg_backend) << "decodeFrame: Invalid frame dimensions:" 
                                     << AV_FRAME_RAW(m_frame)->width << "x" << AV_FRAME_RAW(m_frame)->height;
        return QPixmap();
    }
    
    // Log frame format information for hardware decoder debugging
    static int frameFormatLogCount = 0;
    if (++frameFormatLogCount <= 5 || frameFormatLogCount % 1000 == 1) {
        const char* codecName = codecContext->codec ? codecContext->codec->name : "unknown";
        const char* formatName = av_get_pix_fmt_name(static_cast<AVPixelFormat>(AV_FRAME_RAW(m_frame)->format));
        qCInfo(log_ffmpeg_backend) << "Received frame from" << codecName 
                                   << "- format:" << AV_FRAME_RAW(m_frame)->format 
                                   << "(" << (formatName ? formatName : "unknown") << ")"
                                   << "size:" << AV_FRAME_RAW(m_frame)->width << "x" << AV_FRAME_RAW(m_frame)->height;
    }
    
    // IMPORTANT: mjpeg_cuvid on Windows decodes to system memory in NV12/YUV420P format
    // It does NOT output AV_PIX_FMT_CUDA format like H264/HEVC CUVID decoders
    // The GPU is still used for decoding, but output is directly in system memory
    // This is actually GOOD - no need for slow GPU->CPU transfer!
    
    // Check if this is a hardware frame that needs to be transferred to system memory
    AVFrame* frameToConvert = AV_FRAME_RAW(m_frame);
    AvFramePtr swFrame;
    
    // Check for hardware pixel formats that need transfer
    // NOTE: mjpeg_cuvid outputs to system memory (NV12/YUV420P), so no transfer needed
    bool isHardwareFrame = (AV_FRAME_RAW(m_frame)->format == AV_PIX_FMT_QSV ||       // Intel QSV
                           AV_FRAME_RAW(m_frame)->format == AV_PIX_FMT_CUDA);        // NVIDIA CUDA (H264/HEVC, not MJPEG)
    
    if (isHardwareFrame) {
        // This is a hardware frame - need to transfer to system memory
        static int hwTransferAttempts = 0;
        hwTransferAttempts++;
        
        if (hwTransferAttempts <= 5) {
            const char* hwType = (AV_FRAME_RAW(m_frame)->format == AV_PIX_FMT_QSV) ? "QSV" : "CUDA";
            qCInfo(log_ffmpeg_backend) << "Attempting to transfer hardware frame (" << hwType 
                                       << ") to system memory (attempt" << hwTransferAttempts << ")";
        }
        
        swFrame = make_av_frame();
        if (!swFrame) {
            qCWarning(log_ffmpeg_backend) << "Failed to allocate frame for hardware transfer";
            return QPixmap();
        }
        
        // Transfer data from GPU to CPU
        ret = av_hwframe_transfer_data(AV_FRAME_RAW(swFrame), AV_FRAME_RAW(m_frame), 0);
        if (ret < 0) {
            char errbuf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
            
            static int transferErrorCount = 0;
            if (++transferErrorCount <= 5) {
                qCWarning(log_ffmpeg_backend) << "Error transferring hardware frame to system memory:" 
                                              << QString::fromUtf8(errbuf);
                qCWarning(log_ffmpeg_backend) << "  Frame format:" << AV_FRAME_RAW(m_frame)->format;
                qCWarning(log_ffmpeg_backend) << "  Frame size:" << AV_FRAME_RAW(m_frame)->width << "x" << AV_FRAME_RAW(m_frame)->height;
                qCWarning(log_ffmpeg_backend) << "  This might indicate the decoder doesn't support hardware frames properly";
            }
            
            AV_FRAME_RESET(swFrame);
            return QPixmap();
        }
        
        // Copy metadata from hardware frame
            av_frame_copy_props(AV_FRAME_RAW(swFrame), AV_FRAME_RAW(m_frame));
        
        frameToConvert = AV_FRAME_RAW(swFrame);
        
        static int hwTransferCount = 0;
        if (++hwTransferCount % 1000 == 1) {
            const char* hwType = (AV_FRAME_RAW(m_frame)->format == AV_PIX_FMT_QSV) ? "QSV" : "CUDA";
            qCDebug(log_ffmpeg_backend) << "Hardware frame (" << hwType 
                                       << ") transferred to system memory (count:" << hwTransferCount << ")";
        }
    }
    
    // PERFORMANCE: Skip success logging for every frame
    QPixmap result = convertFrameToPixmap(frameToConvert);
    
    // Free the software frame if we allocated it
    if (swFrame) {
        AV_FRAME_RESET(swFrame);
    }
    
    return result;
}

QPixmap FFmpegBackendHandler::convertFrameToPixmap(AVFrame* frame)
{
    if (!frame) {
        qCWarning(log_ffmpeg_backend) << "convertFrameToPixmap: frame is null";
        return QPixmap();
    }
    
    int width = frame->width;
    int height = frame->height;
    AVPixelFormat format = static_cast<AVPixelFormat>(frame->format);
    
    // Validate frame dimensions and data
    if (width <= 0 || height <= 0) {
        qCWarning(log_ffmpeg_backend) << "convertFrameToPixmap: Invalid dimensions:" << width << "x" << height;
        return QPixmap();
    }
    
    if (!frame->data[0]) {
        qCWarning(log_ffmpeg_backend) << "convertFrameToPixmap: Frame data pointer is null";
        return QPixmap();
    }
    
    if (frame->linesize[0] <= 0) {
        qCWarning(log_ffmpeg_backend) << "convertFrameToPixmap: Invalid linesize:" << frame->linesize[0];
        return QPixmap();
    }
    
    // PERFORMANCE: Minimize debug logging for better performance
    // Only log critical conversion details for first few frames and every 1000th frame
    static int conversionLogCounter = 0;
    conversionLogCounter++;
    
    const char* formatName = av_get_pix_fmt_name(format);
    // TEMP: Always log format for debugging
    // qCDebug(log_ffmpeg_backend) << "convertFrameToPixmap: frame" << width << "x" << height 
    //                            << "format:" << format << "(" << (formatName ? formatName : "unknown") << ")"
    //                            << "linesize:" << frame->linesize[0];
    
    // FAST PATH: If frame is already RGB, create QImage directly without scaling
    if (format == AV_PIX_FMT_RGB24 || format == AV_PIX_FMT_BGR24 || 
        format == AV_PIX_FMT_RGBA || format == AV_PIX_FMT_BGRA ||
        format == AV_PIX_FMT_BGR0 || format == AV_PIX_FMT_RGB0) {
        
        qCDebug(log_ffmpeg_backend) << "Using RGB fast path for format:" << formatName;
        
        QImage::Format qtFormat;
        if (format == AV_PIX_FMT_RGB24) qtFormat = QImage::Format_RGB888;
        else if (format == AV_PIX_FMT_BGR24) qtFormat = QImage::Format_RGB888; // Qt will handle BGR->RGB
        else if (format == AV_PIX_FMT_RGBA) qtFormat = QImage::Format_RGBA8888;
        else if (format == AV_PIX_FMT_BGRA) qtFormat = QImage::Format_RGBA8888;
        else if (format == AV_PIX_FMT_BGR0) qtFormat = QImage::Format_RGB32;
        else if (format == AV_PIX_FMT_RGB0) qtFormat = QImage::Format_RGB32;
        else qtFormat = QImage::Format_RGB888; // fallback
        
        // Create QImage directly from frame data
        QImage image(frame->data[0], width, height, frame->linesize[0], qtFormat);
        if (qtFormat == QImage::Format_RGB888 && format == AV_PIX_FMT_BGR24) {
            // Need to swap BGR to RGB
            image = image.rgbSwapped();
        }
        
        QPixmap pixmap = QPixmap::fromImage(image);
        qCDebug(log_ffmpeg_backend) << "Created pixmap from RGB fast path, size:" << pixmap.size() << "isNull:" << pixmap.isNull();
        return pixmap;
    }
    
    // Check if we need to recreate the scaling context (format or size changed)
    static int lastWidth = -1;
    static int lastHeight = -1;
    static AVPixelFormat lastFormat = AV_PIX_FMT_NONE;
    
    if (!m_swsContext || width != lastWidth || height != lastHeight || format != lastFormat) {
        if (m_swsContext) {
            sws_freeContext(m_swsContext);
            m_swsContext = nullptr;
        }
        
        const char* formatName = av_get_pix_fmt_name(format);
        qCInfo(log_ffmpeg_backend) << "Creating scaling context:" 
                                   << width << "x" << height 
                                   << "from format" << format << "(" << (formatName ? formatName : "unknown") << ")"
                                   << "to RGB24";
        
        m_swsContext = sws_getContext(
            width, height, format,
            width, height, AV_PIX_FMT_RGB24,
            SWS_BILINEAR, nullptr, nullptr, nullptr
        );
        
        if (!m_swsContext) {
            qCCritical(log_ffmpeg_backend) << "✗ Failed to create scaling context for format:" << format 
                                          << "(" << (formatName ? formatName : "unknown") << ")";
            return QPixmap();
        }
        
        lastWidth = width;
        lastHeight = height;
        lastFormat = format;
        
        qCInfo(log_ffmpeg_backend) << "✓ Successfully created scaling context";
    }
    
    // Allocate buffer for RGB frame
    QImage image(width, height, QImage::Format_RGB888);
    if (image.isNull()) {
        qCWarning(log_ffmpeg_backend) << "Failed to allocate QImage for" << width << "x" << height;
        return QPixmap();
    }
    
    // Set up frame data for RGB output
    uint8_t* rgbData[1] = { image.bits() };
    int rgbLinesize[1] = { static_cast<int>(image.bytesPerLine()) };
    
    // Log first few scale operations for debugging
    static int scaleLogCounter = 0;
    if (++scaleLogCounter <= 5) {
        qCDebug(log_ffmpeg_backend) << "Calling sws_scale: input" << width << "x" << height 
                                   << "linesize[0]:" << frame->linesize[0]
                                   << "output linesize:" << rgbLinesize[0];
    }
    
    // Convert frame to RGB
    int scaleResult = sws_scale(m_swsContext, frame->data, frame->linesize, 0, height, rgbData, rgbLinesize);
    if (scaleResult < 0) {
        qCCritical(log_ffmpeg_backend) << "✗ sws_scale failed with result:" << scaleResult;
        return QPixmap();
    }
    
    if (scaleResult != height) {
        qCWarning(log_ffmpeg_backend) << "sws_scale converted" << scaleResult << "lines, expected" << height;
        return QPixmap(); // Return error instead of continuing with partial conversion
    }
    
    if (scaleLogCounter <= 5) {
        qCInfo(log_ffmpeg_backend) << "✓ sws_scale successful, converted" << scaleResult << "lines";
    }
    
    // PERFORMANCE: Skip sws_scale result logging for better performance
    // The scale operation success/failure is already validated above
    
    // PERFORMANCE: Skip pixel analysis completely - trust the conversion process
    // This eliminates unnecessary CPU overhead from pixel sampling
    QPixmap result = QPixmap::fromImage(image);
    // PERFORMANCE: Skip pixmap creation logging for better performance
    return result;
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
// Video Recording Implementation
// ============================================================================

bool FFmpegBackendHandler::startRecording(const QString& outputPath, const QString& format, int videoBitrate)
{
    QMutexLocker locker(&m_recordingMutex);
    
    if (m_recordingActive) {
        qCWarning(log_ffmpeg_backend) << "Recording is already active";
        return false;
    }
    
    if (!m_captureRunning) {
        qCWarning(log_ffmpeg_backend) << "Cannot start recording: capture is not running";
        emit recordingError("Cannot start recording: capture is not running");
        return false;
    }
    
    // Update recording config
    m_recordingConfig.outputPath = outputPath;
    m_recordingConfig.format = format;
    m_recordingConfig.videoBitrate = videoBitrate;
    m_recordingOutputPath = outputPath;
    
    qCDebug(log_ffmpeg_backend) << "Starting recording to:" << outputPath << "format:" << format;
    
    if (!initializeRecording()) {
        emit recordingError("Failed to initialize recording");
        return false;
    }
    
    m_recordingActive = true;
    m_recordingPaused = false;
    m_recordingStartTime = QDateTime::currentMSecsSinceEpoch();
    m_recordingPausedTime = 0;
    m_totalPausedDuration = 0;
    m_lastRecordedFrameTime = 0; // Reset frame timing for proper framerate control
    m_recordingFrameNumber = 0;
    
    qCInfo(log_ffmpeg_backend) << "Recording started successfully";
    emit recordingStarted(outputPath);
    return true;
}

bool FFmpegBackendHandler::stopRecording()
{
    qCDebug(log_ffmpeg_backend) << "Stopping recording";
    
    // First, mark recording as inactive to prevent new frames from being processed
    {
        QMutexLocker locker(&m_recordingMutex);
        if (!m_recordingActive) {
            qCDebug(log_ffmpeg_backend) << "Recording is not active";
            return false;
        }
        m_recordingActive = false;
        m_recordingPaused = false;
    }
    QThread::msleep(10);
    
    {
        QMutexLocker locker(&m_recordingMutex);
        finalizeRecording();
        cleanupRecording();
    }
    
    qCInfo(log_ffmpeg_backend) << "Recording stopped successfully";
    emit recordingStopped();
    return true;
}

void FFmpegBackendHandler::pauseRecording()
{
    QMutexLocker locker(&m_recordingMutex);
    
    if (!m_recordingActive || m_recordingPaused) {
        qCDebug(log_ffmpeg_backend) << "Recording is not active or already paused";
        return;
    }
    
    m_recordingPaused = true;
    m_recordingPausedTime = QDateTime::currentMSecsSinceEpoch();
    
    qCDebug(log_ffmpeg_backend) << "Recording paused";
    emit recordingPaused();
}

void FFmpegBackendHandler::resumeRecording()
{
    QMutexLocker locker(&m_recordingMutex);
    
    if (!m_recordingActive || !m_recordingPaused) {
        qCDebug(log_ffmpeg_backend) << "Recording is not active or not paused";
        return;
    }
    
    if (m_recordingPausedTime > 0) {
        m_totalPausedDuration += QDateTime::currentMSecsSinceEpoch() - m_recordingPausedTime;
    }
    
    m_recordingPaused = false;
    m_recordingPausedTime = 0;
    
    qCDebug(log_ffmpeg_backend) << "Recording resumed";
    emit recordingResumed();
}

bool FFmpegBackendHandler::isRecording() const
{
    QMutexLocker locker(&m_recordingMutex);
    return m_recordingActive;
}

bool FFmpegBackendHandler::isPaused() const
{
    QMutexLocker locker(&m_recordingMutex);
    return m_recordingActive && m_recordingPaused;
}

QString FFmpegBackendHandler::getCurrentRecordingPath() const
{
    QMutexLocker locker(&m_recordingMutex);
    return m_recordingOutputPath;
}

qint64 FFmpegBackendHandler::getRecordingDuration() const
{
    QMutexLocker locker(&m_recordingMutex);
    
    if (!m_recordingActive) {
        return 0;
    }
    
    qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
    qint64 totalDuration = currentTime - m_recordingStartTime - m_totalPausedDuration;
    
    if (m_recordingPaused && m_recordingPausedTime > 0) {
        totalDuration -= (currentTime - m_recordingPausedTime);
    }
    
    return qMax(0LL, totalDuration);
}

void FFmpegBackendHandler::setRecordingConfig(const RecordingConfig& config)
{
    QMutexLocker locker(&m_recordingMutex);
    m_recordingConfig = config;
}

FFmpegBackendHandler::RecordingConfig FFmpegBackendHandler::getRecordingConfig() const
{
    QMutexLocker locker(&m_recordingMutex);
    return m_recordingConfig;
}

bool FFmpegBackendHandler::initializeRecording()
{
    // Clean up any existing recording context
    cleanupRecording();
    
    // Allocate output format context
    const char* formatName = nullptr;
    if (m_recordingConfig.format == "avi") {
        formatName = "avi";  // Use AVI muxer (available in current build for playable video files)
    } else if (m_recordingConfig.format == "rawvideo") {
        formatName = "rawvideo";  // Use rawvideo muxer (available in current build for video streams)
    } else if (m_recordingConfig.format == "mjpeg") {
        formatName = "mjpeg";  // Use MJPEG muxer (creates single images, not video streams)
    } else {
        formatName = nullptr; // Let FFmpeg auto-detect from filename
    }
    
    // Try to allocate output context, first with format name, then with auto-detection
    int ret = avformat_alloc_output_context2(&m_recordingFormatContext, nullptr, formatName, m_recordingConfig.outputPath.toUtf8().data());
    if (ret < 0 || !m_recordingFormatContext) {
        // Try auto-detection from filename if format name failed
        qCWarning(log_ffmpeg_backend) << "Failed with format" << formatName << ", trying auto-detection from filename";
        ret = avformat_alloc_output_context2(&m_recordingFormatContext, nullptr, nullptr, m_recordingConfig.outputPath.toUtf8().data());
        if (ret < 0 || !m_recordingFormatContext) {
            qCWarning(log_ffmpeg_backend) << "Failed to allocate output context for recording";
            return false;
        }
    }
    
    // Configure encoder with current capture settings (ensure we have valid values)
    // If current settings are not valid, they should have been auto-detected during startDirectCapture
    QSize encoderResolution = m_currentResolution.isValid() ? m_currentResolution : QSize(1920, 1080);
    int encoderFramerate = m_currentFramerate > 0 ? m_currentFramerate : 30;
    
    qCDebug(log_ffmpeg_backend) << "Configuring encoder with resolution:" << encoderResolution << "framerate:" << encoderFramerate;
    
    if (!configureEncoder(encoderResolution, encoderFramerate)) {
        return false;
    }
    
    // Open output file
    if (!(m_recordingFormatContext->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&m_recordingFormatContext->pb, m_recordingConfig.outputPath.toUtf8().data(), AVIO_FLAG_WRITE);
        if (ret < 0) {
            char errbuf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
            qCWarning(log_ffmpeg_backend) << "Failed to open output file:" << QString::fromUtf8(errbuf);
            return false;
        }
    }
    
    // Write file header
    ret = avformat_write_header(m_recordingFormatContext, nullptr);
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
        qCWarning(log_ffmpeg_backend) << "Failed to write header:" << QString::fromUtf8(errbuf);
        return false;
    }
    
    qCDebug(log_ffmpeg_backend) << "Recording initialized successfully";
    return true;
}

bool FFmpegBackendHandler::configureEncoder(const QSize& resolution, int framerate)
{
    // Helper lambda to try finding an encoder
    auto tryEncoder = [](AVCodecID codecId, const char* name) -> const AVCodec* {
        const AVCodec* codec = avcodec_find_encoder(codecId);
        if (codec) {
            qCDebug(log_ffmpeg_backend) << "Using" << name << "encoder";
        }
        return codec;
    };
    
    // Find encoder - try requested codec first
    const AVCodec* codec = nullptr;
    if (!m_recordingConfig.videoCodec.isEmpty()) {
        codec = avcodec_find_encoder_by_name(m_recordingConfig.videoCodec.toUtf8().data());
        if (codec) {
            qCDebug(log_ffmpeg_backend) << "Found requested encoder:" << m_recordingConfig.videoCodec;
        } else {
            qCWarning(log_ffmpeg_backend) << "Requested encoder not found:" << m_recordingConfig.videoCodec << "- trying fallbacks";
        }
    }
    
    // Fallback chain for available encoders in current FFmpeg build
    if (!codec) codec = tryEncoder(AV_CODEC_ID_MJPEG, "MJPEG");
    if (!codec) codec = tryEncoder(AV_CODEC_ID_RAWVIDEO, "rawvideo");
    if (!codec) codec = tryEncoder(AV_CODEC_ID_H264, "H264");
    
    if (!codec) {
        qCWarning(log_ffmpeg_backend) << "Failed to find any video encoder (tried mjpeg, rawvideo, h264)";
        return false;
    }
    
    // Add video stream
    m_recordingVideoStream = avformat_new_stream(m_recordingFormatContext, codec);
    if (!m_recordingVideoStream) {
        qCWarning(log_ffmpeg_backend) << "Failed to create video stream";
        return false;
    }
    
    // Allocate codec context
    m_recordingCodecContext = avcodec_alloc_context3(codec);
    if (!m_recordingCodecContext) {
        qCWarning(log_ffmpeg_backend) << "Failed to allocate codec context";
        return false;
    }
    
    // Set codec parameters based on codec type
    m_recordingCodecContext->width = resolution.width();
    m_recordingCodecContext->height = resolution.height();
    m_recordingCodecContext->time_base = {1, framerate};
    m_recordingCodecContext->framerate = {framerate, 1};
    
    // Cache target framerate for thread-safe access during frame processing
    m_recordingTargetFramerate = framerate;
    
    // Set pixel format based on codec
    if (codec->id == AV_CODEC_ID_MJPEG) {
        m_recordingCodecContext->pix_fmt = AV_PIX_FMT_YUVJ420P; // MJPEG typically uses YUVJ420P
        m_recordingCodecContext->bit_rate = m_recordingConfig.videoBitrate;
        // MJPEG quality (range 1-31, lower is better)
        m_recordingCodecContext->qmin = 1;
        m_recordingCodecContext->qmax = 10; // Good quality
    } else if (codec->id == AV_CODEC_ID_RAWVIDEO) {
        m_recordingCodecContext->pix_fmt = AV_PIX_FMT_RGB24; // Raw video uses RGB24
        // No bitrate for raw video (uncompressed)
    } else {
        // H264 or other codecs
        m_recordingCodecContext->pix_fmt = AV_PIX_FMT_YUV420P;
        m_recordingCodecContext->bit_rate = m_recordingConfig.videoBitrate;
    }
    
    // Set codec-specific options
    if (codec->id == AV_CODEC_ID_H264) {
        av_opt_set_int(m_recordingCodecContext->priv_data, "crf", m_recordingConfig.videoQuality, 0);
        av_opt_set(m_recordingCodecContext->priv_data, "preset", "ultrafast", 0); // Use ultrafast for real-time recording
        av_opt_set(m_recordingCodecContext->priv_data, "tune", "zerolatency", 0);
        
        // Use constant bitrate mode for more predictable performance
        m_recordingCodecContext->rc_max_rate = m_recordingConfig.videoBitrate;
        m_recordingCodecContext->rc_buffer_size = m_recordingConfig.videoBitrate * 2;
    } else if (codec->id == AV_CODEC_ID_MJPEG) {
        // MJPEG-specific options for better quality
        av_opt_set_int(m_recordingCodecContext->priv_data, "q:v", m_recordingConfig.videoQuality, 0);
    }
    
    // Global header flag for MP4
    if (m_recordingFormatContext->oformat->flags & AVFMT_GLOBALHEADER) {
        m_recordingCodecContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }
    
    // Open codec
    int ret = avcodec_open2(m_recordingCodecContext, codec, nullptr);
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
        qCWarning(log_ffmpeg_backend) << "Failed to open codec:" << QString::fromUtf8(errbuf);
        return false;
    }
    
    // Copy codec parameters to stream
    ret = avcodec_parameters_from_context(m_recordingVideoStream->codecpar, m_recordingCodecContext);
    if (ret < 0) {
        qCWarning(log_ffmpeg_backend) << "Failed to copy codec parameters";
        return false;
    }
    
    // CRITICAL: Set the video stream time base to match codec time base
    // This ensures proper frame timing in the output file
    m_recordingVideoStream->time_base = m_recordingCodecContext->time_base;
    qCDebug(log_ffmpeg_backend) << "Set video stream time base to" << m_recordingVideoStream->time_base.num << "/" << m_recordingVideoStream->time_base.den;
    
    // Allocate frame for encoding
    m_recordingFrame = make_av_frame();
    if (!m_recordingFrame) {
        qCWarning(log_ffmpeg_backend) << "Failed to allocate recording frame";
        return false;
    }
    
    AV_FRAME_RAW(m_recordingFrame)->format = m_recordingCodecContext->pix_fmt;
    AV_FRAME_RAW(m_recordingFrame)->width = m_recordingCodecContext->width;
    AV_FRAME_RAW(m_recordingFrame)->height = m_recordingCodecContext->height;
    
    ret = av_frame_get_buffer(AV_FRAME_RAW(m_recordingFrame), 0);
    if (ret < 0) {
        qCWarning(log_ffmpeg_backend) << "Failed to allocate frame buffer";
        return false;
    }
    
    // Allocate packet
    m_recordingPacket = make_av_packet();
    if (!m_recordingPacket) {
        qCWarning(log_ffmpeg_backend) << "Failed to allocate recording packet";
        return false;
    }
    
    // Initialize scaling context for color space conversion
    AVPixelFormat outputFormat = static_cast<AVPixelFormat>(m_recordingCodecContext->pix_fmt);
    m_recordingSwsContext = sws_getContext(
        resolution.width(), resolution.height(), AV_PIX_FMT_RGB24,
        resolution.width(), resolution.height(), outputFormat,
        SWS_BICUBIC, nullptr, nullptr, nullptr);
    
    if (!m_recordingSwsContext) {
        qCWarning(log_ffmpeg_backend) << "Failed to initialize scaling context for recording (output format:" << outputFormat << ")";
        return false;
    }
    
    qCDebug(log_ffmpeg_backend) << "Encoder configured successfully" 
                               << "Resolution:" << resolution
                               << "Framerate:" << framerate
                               << "Bitrate:" << m_recordingConfig.videoBitrate;
    
    return true;
}

bool FFmpegBackendHandler::writeFrameToFile(AVFrame* frame)
{
    // Use QMutexLocker for automatic unlocking
    QMutexLocker locker(&m_recordingMutex);
    
    // Quick check recording state within the mutex-protected context
    if (!m_recordingActive || m_recordingPaused || !m_recordingCodecContext || !frame 
        || !m_recordingFormatContext || !m_recordingVideoStream || !m_recordingPacket) {
        return false;
    }
    
    // FIXED: Calculate PTS based on actual elapsed time for proper A/V synchronization
    // This handles variable frame rates and provides better timing accuracy
    qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
    qint64 elapsedMs = currentTime - m_recordingStartTime - m_totalPausedDuration;
    
    // Convert elapsed milliseconds to PTS using the codec's time base
    int64_t pts = av_rescale_q(elapsedMs, AVRational{1, 1000}, m_recordingCodecContext->time_base);
    frame->pts = pts;
    
    // Debug logging for first few frames to verify timing
    static int debugFrameCount = 0;
    if (++debugFrameCount <= 5 || debugFrameCount % 100 == 0) { // Log first 5 and every 100th frame
        qCDebug(log_ffmpeg_backend) << "Writing frame" << m_recordingFrameNumber 
                                   << "with PTS" << pts 
                                   << "(elapsed:" << elapsedMs << "ms)"
                                   << "time_base" << m_recordingCodecContext->time_base.num << "/" << m_recordingCodecContext->time_base.den;
    }
    
    m_recordingFrameNumber++;
    
    // Send frame to encoder
    int ret = avcodec_send_frame(m_recordingCodecContext, frame);
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
        qCWarning(log_ffmpeg_backend) << "Error sending frame to encoder:" << QString::fromUtf8(errbuf);
        return false;
    }
    
    // Receive encoded packets
    while (ret >= 0) {
        ret = avcodec_receive_packet(m_recordingCodecContext, AV_PACKET_RAW(m_recordingPacket));
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        } else if (ret < 0) {
            char errbuf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
            qCWarning(log_ffmpeg_backend) << "Error receiving packet from encoder:" << QString::fromUtf8(errbuf);
            return false;
        }
        
        // Check if we're still recording before writing the packet
        if (!m_recordingActive || !m_recordingFormatContext) {
            qCDebug(log_ffmpeg_backend) << "Recording stopped during packet processing, discarding packet";
            av_packet_unref(AV_PACKET_RAW(m_recordingPacket));
            return false;
        }
        
        // Scale packet timestamp
        av_packet_rescale_ts(AV_PACKET_RAW(m_recordingPacket), m_recordingCodecContext->time_base, m_recordingVideoStream->time_base);
        AV_PACKET_RAW(m_recordingPacket)->stream_index = m_recordingVideoStream->index;
        
        // Write packet to output file
        ret = av_interleaved_write_frame(m_recordingFormatContext, AV_PACKET_RAW(m_recordingPacket));
        if (ret < 0) {
            char errbuf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
            qCWarning(log_ffmpeg_backend) << "Error writing frame to file:" << QString::fromUtf8(errbuf);
            av_packet_unref(AV_PACKET_RAW(m_recordingPacket));
            return false;
        }
        
        av_packet_unref(AV_PACKET_RAW(m_recordingPacket));
    }
    
    return true;
}

void FFmpegBackendHandler::finalizeRecording()
{
    if (!m_recordingFormatContext || !m_recordingCodecContext) {
        qCDebug(log_ffmpeg_backend) << "Recording context already cleaned up, skipping finalization";
        return;
    }
    
    qCDebug(log_ffmpeg_backend) << "Finalizing recording...";
    
    // Flush encoder - send NULL frame to signal end of input
    if (m_recordingCodecContext) {
        int ret = avcodec_send_frame(m_recordingCodecContext, nullptr);
        if (ret < 0 && ret != AVERROR_EOF) {
            char errbuf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
            qCWarning(log_ffmpeg_backend) << "Error flushing encoder:" << QString::fromUtf8(errbuf);
        } else {
            // Receive remaining packets from encoder
            while (ret >= 0) {
                ret = avcodec_receive_packet(m_recordingCodecContext, AV_PACKET_RAW(m_recordingPacket));
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    break;
                } else if (ret < 0) {
                    char errbuf[AV_ERROR_MAX_STRING_SIZE];
                    av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
                    qCWarning(log_ffmpeg_backend) << "Error receiving final packets:" << QString::fromUtf8(errbuf);
                    break;
                }
                
                // Scale packet timestamp and write to file
                if (m_recordingVideoStream && m_recordingFormatContext) {
                    av_packet_rescale_ts(AV_PACKET_RAW(m_recordingPacket), m_recordingCodecContext->time_base, m_recordingVideoStream->time_base);
                    AV_PACKET_RAW(m_recordingPacket)->stream_index = m_recordingVideoStream->index;
                    av_interleaved_write_frame(m_recordingFormatContext, AV_PACKET_RAW(m_recordingPacket));
                }
                av_packet_unref(AV_PACKET_RAW(m_recordingPacket));
            }
        }
    }
    
    // Write trailer
    if (m_recordingFormatContext) {
        int ret = av_write_trailer(m_recordingFormatContext);
        if (ret < 0) {
            char errbuf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
            qCWarning(log_ffmpeg_backend) << "Error writing trailer:" << QString::fromUtf8(errbuf);
        }
    }
    
    qCDebug(log_ffmpeg_backend) << "Recording finalized, total frames:" << m_recordingFrameNumber;
}

void FFmpegBackendHandler::cleanupRecording()
{
    if (m_recordingSwsContext) {
        sws_freeContext(m_recordingSwsContext);
        m_recordingSwsContext = nullptr;
    }
    
    if (m_recordingFrame) {
        AV_FRAME_RESET(m_recordingFrame);
    }
    
    if (m_recordingPacket) {
        AV_PACKET_RESET(m_recordingPacket);
    }
    
    if (m_recordingCodecContext) {
        avcodec_free_context(&m_recordingCodecContext);
    }
    
    if (m_recordingFormatContext) {
        if (!(m_recordingFormatContext->oformat->flags & AVFMT_NOFILE)) {
            avio_closep(&m_recordingFormatContext->pb);
        }
        avformat_free_context(m_recordingFormatContext);
        m_recordingFormatContext = nullptr;
    }
    
    m_recordingVideoStream = nullptr;
    
    qCDebug(log_ffmpeg_backend) << "Recording cleanup completed";
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
    return true; // FFmpeg backend always supports advanced recording
}

bool FFmpegBackendHandler::startRecordingAdvanced(const QString& outputPath, const RecordingConfig& config)
{
    setRecordingConfig(config);
    return startRecording(outputPath, config.format, config.videoBitrate);
}

bool FFmpegBackendHandler::forceStopRecording()
{
    qCDebug(log_ffmpeg_backend) << "Force stopping recording";
    m_recordingActive = false;
    m_recordingPaused = false;
    cleanupRecording();
    emit recordingStopped();
    return true;
}

QString FFmpegBackendHandler::getLastError() const
{
    return m_lastError;
}

bool FFmpegBackendHandler::supportsRecordingStats() const
{
    return true; // FFmpeg backend supports recording statistics
}

qint64 FFmpegBackendHandler::getRecordingFileSize() const
{
    if (m_recordingOutputPath.isEmpty() || !m_recordingActive) {
        return 0;
    }
    
    QFileInfo fileInfo(m_recordingOutputPath);
    return fileInfo.size();
}

void FFmpegBackendHandler::takeImage(const QString& filePath)
{
    QMutexLocker locker(&m_mutex);
    if (!m_latestFrame.isNull()) {
        if (m_latestFrame.save(filePath)) {
            qCDebug(log_ffmpeg_backend) << "Image saved to:" << filePath;
        } else {
            qCWarning(log_ffmpeg_backend) << "Failed to save image to:" << filePath;
        }
    } else {
        qCWarning(log_ffmpeg_backend) << "No frame available for image capture";
    }
}

void FFmpegBackendHandler::takeAreaImage(const QString& filePath, const QRect& captureArea)
{
    QMutexLocker locker(&m_mutex);
    if (!m_latestFrame.isNull()) {
        QImage cropped = m_latestFrame.copy(captureArea);
        if (cropped.save(filePath)) {
            qCDebug(log_ffmpeg_backend) << "Cropped image saved to:" << filePath;
        } else {
            qCWarning(log_ffmpeg_backend) << "Failed to save cropped image to:" << filePath;
        }
    } else {
        qCWarning(log_ffmpeg_backend) << "No frame available for area image capture";
    }
}

#include "ffmpegbackendhandler.moc"
