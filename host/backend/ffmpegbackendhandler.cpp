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

/**
 * @brief Capture thread for handling FFmpeg video capture in background
 */
class FFmpegBackendHandler::CaptureThread : public QThread
{
    Q_OBJECT

public:
    explicit CaptureThread(FFmpegBackendHandler* handler, QObject* parent = nullptr)
        : QThread(parent), m_handler(handler), m_running(false) {}

    void setRunning(bool running) {
        QMutexLocker locker(&m_mutex);
        m_running = running;
    }

    bool isRunning() const {
        QMutexLocker locker(&m_mutex);
        return m_running;
    }

protected:
    void run() override {
        qCDebug(log_ffmpeg_backend) << "FFmpeg capture thread started";
        
        QElapsedTimer performanceTimer;
        performanceTimer.start();
        
        int consecutiveFailures = 0;
        int framesProcessed = 0;
        const int maxConsecutiveFailures = 20; // Reduced from 100 - stop faster on device disconnect
        
        while (isRunning()) {
            // Check for interruption request
            if (isInterruptionRequested()) {
                qCDebug(log_ffmpeg_backend) << "Capture thread interrupted";
                break;
            }
            
            if (m_handler && m_handler->readFrame()) {
                // Reset failure counter on successful read
                consecutiveFailures = 0;
                
                // Process frame directly in capture thread to avoid packet invalidation
                // This ensures packet data remains valid during processing
                m_handler->processFrame();
                framesProcessed++;
                
                // Log performance periodically (less frequently to reduce overhead)
                if (performanceTimer.elapsed() > 15000) { // Every 15 seconds (increased from 10)
                    double actualFps = (framesProcessed * 1000.0) / performanceTimer.elapsed();
                    qCDebug(log_ffmpeg_backend) << "Capture thread performance:" << actualFps << "FPS, processed" << framesProcessed << "frames";
                    performanceTimer.restart();
                    framesProcessed = 0;
                }
            } else {
                // Track consecutive failures
                consecutiveFailures++;
                
                // Be more aggressive about detecting device disconnections
                // Check device availability after fewer failures, especially for I/O errors
                if (consecutiveFailures >= 10 && consecutiveFailures % 10 == 0) { // Reduced from 50/25 - check every 10 failures
                    qCDebug(log_ffmpeg_backend) << "Checking device availability due to consecutive failures:" << consecutiveFailures;
                    if (m_handler && !m_handler->isCurrentDeviceAvailable()) {
                        qCWarning(log_ffmpeg_backend) << "Device no longer available, stopping capture thread";
                        // Use QTimer::singleShot to avoid calling handleDeviceDeactivation from the capture thread
                        QTimer::singleShot(0, m_handler, [handler = m_handler]() {
                            handler->handleDeviceDeactivation(handler->m_currentDevice);
                        });
                        break; // Exit the capture loop
                    }
                }
                
                if (consecutiveFailures >= maxConsecutiveFailures) {
                    qCWarning(log_ffmpeg_backend) << "Too many consecutive frame read failures (" << consecutiveFailures << "), may indicate device issue";
                    // Also trigger device disconnection handling asynchronously
                    if (m_handler) {
                        qCWarning(log_ffmpeg_backend) << "Triggering device disconnection due to persistent failures";
                        QTimer::singleShot(0, m_handler, [handler = m_handler]() {
                            handler->handleDeviceDeactivation(handler->m_currentDevice);
                        });
                        break;
                    }
                    consecutiveFailures = 0; // Reset to avoid spam
                }
                
                // Adaptive sleep - longer sleep for repeated failures
                if (consecutiveFailures < 10) {
                    msleep(1); // Short sleep for occasional failures
                } else if (consecutiveFailures < 50) {
                    msleep(5); // Medium sleep for frequent failures
                } else {
                    msleep(10); // Longer sleep for persistent failures
                }
            }
        }
        
        qCDebug(log_ffmpeg_backend) << "FFmpeg capture thread finished, processed" << framesProcessed << "frames total";
    }

private:
    FFmpegBackendHandler* m_handler;
    mutable QMutex m_mutex;
    bool m_running;
};

FFmpegBackendHandler::FFmpegBackendHandler(QObject *parent)
    : MultimediaBackendHandler(parent)
      , m_formatContext(nullptr),
      m_codecContext(nullptr),
      m_frame(nullptr),
      m_frameRGB(nullptr),
      m_packet(nullptr),
      m_swsContext(nullptr),
      m_hwDeviceContext(nullptr),
      m_hwDeviceType(AV_HWDEVICE_TYPE_NONE),
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
    qCDebug(log_ffmpeg_backend) << "Initializing hardware acceleration, preferred:" << m_preferredHwAccel;
    
    // Explicitly handle CPU-only mode
    if (m_preferredHwAccel == "none") {
        qCInfo(log_ffmpeg_backend) << "Hardware acceleration disabled - using CPU decoding";
        m_hwDeviceContext = nullptr;
        m_hwDeviceType = AV_HWDEVICE_TYPE_NONE;
        return true;
    }
    
    // For MJPEG decoding on Windows, CUVID decoders work differently than on Linux
    // They can be used directly without creating a hardware device context first
    // We just need to verify the decoder is available
    
    // Priority order for MJPEG hardware decoders:
    // 1. NVIDIA CUVID (mjpeg_cuvid) - works on Windows without device context
    // 2. Intel QSV (mjpeg_qsv) - may need device context on some platforms
    
    HwDecoderInfo hwDecoders[] = {
        {"CUDA/NVDEC", "mjpeg_cuvid", AV_HWDEVICE_TYPE_CUDA, false, "cuda"},
        {"Intel QSV", "mjpeg_qsv", AV_HWDEVICE_TYPE_QSV, false, "qsv"},
        {nullptr, nullptr, AV_HWDEVICE_TYPE_NONE, false, ""}
    };
    
    // If not auto, try the preferred one first
    if (m_preferredHwAccel != "auto") {
        for (int i = 0; hwDecoders[i].name != nullptr; i++) {
            if (hwDecoders[i].settingName == m_preferredHwAccel) {
                qCInfo(log_ffmpeg_backend) << "Trying preferred hardware decoder:" << hwDecoders[i].name;
                if (tryInitializeHwDecoder(hwDecoders[i])) {
                    return true;
                }
                break;
            }
        }
        qCWarning(log_ffmpeg_backend) << "Preferred hardware acceleration" << m_preferredHwAccel << "not available, falling back to auto";
    }
    
    // Auto mode or fallback: try all available
    for (int i = 0; hwDecoders[i].name != nullptr; i++) {
        if (tryInitializeHwDecoder(hwDecoders[i])) {
            return true;
        }
    }
    
    qCWarning(log_ffmpeg_backend) << "No MJPEG-capable hardware acceleration found - using software decoding";
    qCInfo(log_ffmpeg_backend) << "  - For NVIDIA GPU: Ensure latest drivers are installed and FFmpeg is built with --enable-cuda --enable-cuvid --enable-nvdec";
    qCInfo(log_ffmpeg_backend) << "  - For Intel GPU: Ensure QSV drivers are installed and FFmpeg is built with --enable-libmfx";
    m_hwDeviceContext = nullptr;
    m_hwDeviceType = AV_HWDEVICE_TYPE_NONE;
    return false;
}

bool FFmpegBackendHandler::tryInitializeHwDecoder(const HwDecoderInfo& decoder)
{
    qCInfo(log_ffmpeg_backend) << "Checking for" << decoder.name << "hardware decoder...";
    
    // First check if the decoder itself is available
    const AVCodec* testCodec = avcodec_find_decoder_by_name(decoder.decoderName);
    if (!testCodec) {
        qCInfo(log_ffmpeg_backend) << "  ✗" << decoder.decoderName << "decoder not found in this FFmpeg build";
        return false;
    }
    
    qCInfo(log_ffmpeg_backend) << "  ✓ Found" << decoder.decoderName << "decoder";
    
    // For decoders that need a device context, try to create it
    if (decoder.needsDeviceContext) {
        m_hwDeviceType = decoder.deviceType;
        
        int ret = av_hwdevice_ctx_create(&m_hwDeviceContext, decoder.deviceType, nullptr, nullptr, 0);
        if (ret < 0) {
            char errbuf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
            qCWarning(log_ffmpeg_backend) << "  ✗ Failed to create device context:" << QString::fromUtf8(errbuf);
            return false;
        }
        qCInfo(log_ffmpeg_backend) << "  ✓ Hardware device context created";
    } else {
        // For CUVID on Windows, we don't need a device context
        qCInfo(log_ffmpeg_backend) << "  ℹ This decoder doesn't require a device context";
        m_hwDeviceType = decoder.deviceType;
        m_hwDeviceContext = nullptr;  // Explicitly set to nullptr for CUVID
    }
    
    qCInfo(log_ffmpeg_backend) << "✓✓✓ Successfully initialized" << decoder.name 
                               << "hardware acceleration for MJPEG decoding ✓✓✓";
    return true;
}

void FFmpegBackendHandler::updatePreferredHardwareAcceleration()
{
    m_preferredHwAccel = GlobalSetting::instance().getHardwareAcceleration();
    qCDebug(log_ffmpeg_backend) << "Updated preferred hardware acceleration to:" << m_preferredHwAccel;
}

bool FFmpegBackendHandler::tryHardwareDecoder(const AVCodecParameters* codecpar, 
                                               const AVCodec** outCodec, 
                                               bool* outUsingHwDecoder)
{
    // Allow trying hardware decoder even without device context (needed for CUVID on Windows)
    if (m_hwDeviceType == AV_HWDEVICE_TYPE_NONE || !codecpar || !outCodec || !outUsingHwDecoder) {
        return false;
    }
    
    *outCodec = nullptr;
    *outUsingHwDecoder = false;
    
    // Only try hardware acceleration for MJPEG
    if (codecpar->codec_id != AV_CODEC_ID_MJPEG) {
        return false;
    }
    
    // Map hardware device types to MJPEG decoder names
    const char* hwDecoderName = nullptr;
    const char* hwDeviceTypeName = av_hwdevice_get_type_name(m_hwDeviceType);
    
    if (strcmp(hwDeviceTypeName, "cuda") == 0) {
        hwDecoderName = "mjpeg_cuvid";
        qCInfo(log_ffmpeg_backend) << "Attempting to use NVIDIA NVDEC/CUVID for MJPEG decoding";
    } else if (strcmp(hwDeviceTypeName, "qsv") == 0) {
        hwDecoderName = "mjpeg_qsv";
        qCInfo(log_ffmpeg_backend) << "Attempting to use Intel QSV for MJPEG decoding";
    } else {
        // Unknown or unsupported hardware type for MJPEG
        qCWarning(log_ffmpeg_backend) << "Hardware type" << hwDeviceTypeName 
                                      << "does not support MJPEG hardware decoding";
        return false;
    }
    
    qCInfo(log_ffmpeg_backend) << "Looking for hardware decoder:" << hwDecoderName;
    *outCodec = avcodec_find_decoder_by_name(hwDecoderName);
    
    if (*outCodec) {
        qCInfo(log_ffmpeg_backend) << "✓ Found" << hwDecoderName << "hardware decoder";
        qCInfo(log_ffmpeg_backend) << "  - Codec long name:" << (*outCodec)->long_name;
        qCInfo(log_ffmpeg_backend) << "  - This will offload MJPEG decoding to GPU";
        *outUsingHwDecoder = true;
        return true;
    } else {
        qCWarning(log_ffmpeg_backend) << "✗ Hardware decoder" << hwDecoderName << "not found";
        qCWarning(log_ffmpeg_backend) << "  - Your FFmpeg build may not include" << hwDecoderName << "support";
        return false;
    }
}

void FFmpegBackendHandler::cleanupHardwareAcceleration()
{
    if (m_hwDeviceContext) {
        qCDebug(log_ffmpeg_backend) << "Cleaning up hardware device context";
        av_buffer_unref(&m_hwDeviceContext);
        m_hwDeviceContext = nullptr;
    }
    m_hwDeviceType = AV_HWDEVICE_TYPE_NONE;
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
                m_captureThread->terminate();
                m_captureThread->wait(0); // Don't wait, just check
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
    qCDebug(log_ffmpeg_backend) << "Opening input device:" << devicePath;
    
    // Reset interrupt state for this new operation
    m_interruptRequested = false;
    m_operationStartTime = QDateTime::currentMSecsSinceEpoch();
    
#ifdef Q_OS_WIN
    // WINDOWS: Use DirectShow for video capture
    qCDebug(log_ffmpeg_backend) << "Windows platform detected - using DirectShow input";
    
    // Allocate format context
    m_formatContext = avformat_alloc_context();
    if (!m_formatContext) {
        qCCritical(log_ffmpeg_backend) << "Failed to allocate format context";
        return false;
    }
    
    // CRITICAL FIX: Set interrupt callback to prevent blocking operations
    m_formatContext->interrupt_callback.callback = FFmpegBackendHandler::interruptCallback;
    m_formatContext->interrupt_callback.opaque = this;
    
    // Find DirectShow input format
    const AVInputFormat* inputFormat = av_find_input_format("dshow");
    if (!inputFormat) {
        qCCritical(log_ffmpeg_backend) << "DirectShow input format not found - FFmpeg may not be built with dshow support";
        return false;
    }
    
    // RESPONSIVENESS: Set low-latency input options for DirectShow
    AVDictionary* options = nullptr;
    av_dict_set(&options, "video_size", QString("%1x%2").arg(resolution.width()).arg(resolution.height()).toUtf8().constData(), 0);
    av_dict_set(&options, "framerate", QString::number(framerate).toUtf8().constData(), 0);
    
    // CRITICAL LOW-LATENCY OPTIMIZATIONS for KVM responsiveness:
    av_dict_set(&options, "rtbufsize", "10000000", 0);        // 10MB buffer to prevent overflow
    av_dict_set(&options, "fflags", "discardcorrupt", 0); // Discard corrupt frames only
    av_dict_set(&options, "flags", "low_delay", 0);       // Enable low delay mode
    av_dict_set(&options, "max_delay", "2000", 0);           // Allow 3ms delay for stability
    av_dict_set(&options, "probesize", "32", 0);          // Minimal probe size for fastest start (minimum allowed)
    av_dict_set(&options, "analyzeduration", "0", 0);     // Skip analysis to reduce startup delay
    
    // CRITICAL FIX: Add timeout to prevent blocking on device reconnection
    av_dict_set(&options, "timeout", "5000000", 0);       // 5 second timeout in microseconds
    
    // Try to set video format - DirectShow supports various formats
    // Try MJPEG first for best performance
    av_dict_set(&options, "vcodec", "mjpeg", 0);
    
    qCDebug(log_ffmpeg_backend) << "Trying DirectShow with MJPEG format, resolution" << resolution << "and framerate" << framerate;
    qCDebug(log_ffmpeg_backend) << "DirectShow device string:" << devicePath;
    
    // Open input - devicePath should be in format "video=Device Name"
    int ret = avformat_open_input(&m_formatContext, devicePath.toUtf8().constData(), inputFormat, &options);
    av_dict_free(&options);
    
    // If MJPEG fails, try without specifying codec (auto-detect)
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
        qCWarning(log_ffmpeg_backend) << "MJPEG format failed:" << QString::fromUtf8(errbuf) << "- trying auto-detection";
        
        // Reset format context
        if (m_formatContext) {
            avformat_close_input(&m_formatContext);
        }
        m_formatContext = avformat_alloc_context();
        
        // Re-set interrupt callback
        m_formatContext->interrupt_callback.callback = FFmpegBackendHandler::interruptCallback;
        m_formatContext->interrupt_callback.opaque = this;
        
        // Try without codec specification
        AVDictionary* fallbackOptions = nullptr;
        av_dict_set(&fallbackOptions, "video_size", QString("%1x%2").arg(resolution.width()).arg(resolution.height()).toUtf8().constData(), 0);
        av_dict_set(&fallbackOptions, "framerate", QString::number(framerate).toUtf8().constData(), 0);
        av_dict_set(&fallbackOptions, "rtbufsize", "100M", 0);
        
        ret = avformat_open_input(&m_formatContext, devicePath.toUtf8().constData(), inputFormat, &fallbackOptions);
        av_dict_free(&fallbackOptions);
    }
    
    // If that fails, try minimal options
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
        qCWarning(log_ffmpeg_backend) << "Auto-detection failed:" << QString::fromUtf8(errbuf) << "- trying minimal options";
        
        // Reset format context
        if (m_formatContext) {
            avformat_close_input(&m_formatContext);
        }
        m_formatContext = avformat_alloc_context();
        
        // Re-set interrupt callback
        m_formatContext->interrupt_callback.callback = FFmpegBackendHandler::interruptCallback;
        m_formatContext->interrupt_callback.opaque = this;
        
        // Try with just the device path
        ret = avformat_open_input(&m_formatContext, devicePath.toUtf8().constData(), inputFormat, nullptr);
    }
    
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
        qCCritical(log_ffmpeg_backend) << "Failed to open DirectShow device:" << QString::fromUtf8(errbuf);
        qCCritical(log_ffmpeg_backend) << "Device path:" << devicePath;
        qCCritical(log_ffmpeg_backend) << "Make sure the device name is correct and the camera is not in use by another application";
        return false;
    }
    
    qCDebug(log_ffmpeg_backend) << "Successfully opened DirectShow device" << devicePath;
    
#else
    // LINUX/MACOS: Use V4L2 for video capture
    // RESPONSIVENESS OPTIMIZATION: Configure device for minimal latency
    // This is critical for KVM applications where mouse responsiveness is key
    qCDebug(log_ffmpeg_backend) << "Pre-configuring device for low-latency MJPEG capture...";
    
    QString configCommand = QString("v4l2-ctl --device=%1 --set-fmt-video=width=%2,height=%3,pixelformat=MJPG")
                           .arg(devicePath).arg(resolution.width()).arg(resolution.height());
    int configResult = system(configCommand.toUtf8().constData());
    
    QString framerateCommand = QString("v4l2-ctl --device=%1 --set-parm=%2")
                              .arg(devicePath).arg(framerate);
    int framerateResult = system(framerateCommand.toUtf8().constData());
    
    // RESPONSIVENESS: Try to configure minimal buffering for lower latency
    QString bufferCommand = QString("v4l2-ctl --device=%1")
                           .arg(devicePath);
    [[maybe_unused]] auto bufferResult = system(bufferCommand.toUtf8().constData()); // Don't check result - optional optimization
    
    if (configResult == 0 && framerateResult == 0) {
        qCDebug(log_ffmpeg_backend) << "Device pre-configured successfully for low-latency MJPEG" << resolution << "at" << framerate << "fps";
    } else {
        qCWarning(log_ffmpeg_backend) << "Device pre-configuration failed, continuing with FFmpeg initialization";
    }
    
    // Reduce delay to minimize latency
    // QThread::msleep(100); // Reduced from 200ms to 100ms
    
    // Allocate format context
    m_formatContext = avformat_alloc_context();
    if (!m_formatContext) {
        qCCritical(log_ffmpeg_backend) << "Failed to allocate format context";
        return false;
    }
    
    // CRITICAL FIX: Set interrupt callback to prevent blocking operations
    m_formatContext->interrupt_callback.callback = FFmpegBackendHandler::interruptCallback;
    m_formatContext->interrupt_callback.opaque = this;
    
    // Find input format (V4L2) - try multiple format names
    const AVInputFormat* inputFormat = av_find_input_format("v4l2");
    if (!inputFormat) {
        qCCritical(log_ffmpeg_backend) << "V4L2 input format not found";
        return false;
    }
    
    // RESPONSIVENESS: Set low-latency input options for MJPEG
    AVDictionary* options = nullptr;
    av_dict_set(&options, "video_size", QString("%1x%2").arg(resolution.width()).arg(resolution.height()).toUtf8().constData(), 0);
    av_dict_set(&options, "framerate", QString::number(framerate).toUtf8().constData(), 0);
    av_dict_set(&options, "input_format", "mjpeg", 0); // Should work due to pre-configuration
    
    // CRITICAL LOW-LATENCY OPTIMIZATIONS for KVM responsiveness:
    av_dict_set(&options, "fflags", "nobuffer", 0);        // Disable input buffering
    av_dict_set(&options, "flags", "low_delay", 0);        // Enable low delay mode  
    av_dict_set(&options, "framedrop", "1", 0);            // Allow frame dropping
    av_dict_set(&options, "use_wallclock_as_timestamps", "1", 0); // Use wall clock for timestamps
    av_dict_set(&options, "probesize", "32", 0);           // Minimal probe size for faster start
    av_dict_set(&options, "analyzeduration", "0", 0);      // Skip analysis to reduce startup delay
    
    qCDebug(log_ffmpeg_backend) << "Trying low-latency MJPEG format with resolution" << resolution << "and framerate" << framerate;
    
    // Open input
    int ret = avformat_open_input(&m_formatContext, devicePath.toUtf8().constData(), inputFormat, &options);
    av_dict_free(&options);
    
    // If MJPEG fails, try YUYV422
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
        qCWarning(log_ffmpeg_backend) << "MJPEG format failed:" << QString::fromUtf8(errbuf) << "- trying YUYV422";
        
        // Reset format context
        if (m_formatContext) {
            avformat_close_input(&m_formatContext);
        }
        m_formatContext = avformat_alloc_context();
        
        // Re-set interrupt callback
        m_formatContext->interrupt_callback.callback = FFmpegBackendHandler::interruptCallback;
        m_formatContext->interrupt_callback.opaque = this;
        
        // Try YUYV422 format
        AVDictionary* yuvOptions = nullptr;
        av_dict_set(&yuvOptions, "video_size", QString("%1x%2").arg(resolution.width()).arg(resolution.height()).toUtf8().constData(), 0);
        av_dict_set(&yuvOptions, "framerate", QString::number(framerate).toUtf8().constData(), 0);
        av_dict_set(&yuvOptions, "input_format", "yuyv422", 0);
        
        ret = avformat_open_input(&m_formatContext, devicePath.toUtf8().constData(), inputFormat, &yuvOptions);
        av_dict_free(&yuvOptions);
    }
    
    // If that fails, try without specifying input format (auto-detect)
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
        qCWarning(log_ffmpeg_backend) << "YUYV422 format failed:" << QString::fromUtf8(errbuf) << "- trying auto-detection";
        
        // Reset format context
        if (m_formatContext) {
            avformat_close_input(&m_formatContext);
        }
        m_formatContext = avformat_alloc_context();
        
        // Re-set interrupt callback
        m_formatContext->interrupt_callback.callback = FFmpegBackendHandler::interruptCallback;
        m_formatContext->interrupt_callback.opaque = this;
        
        // Try again without input_format specification (auto-detect)
        AVDictionary* fallbackOptions = nullptr;
        av_dict_set(&fallbackOptions, "video_size", QString("%1x%2").arg(resolution.width()).arg(resolution.height()).toUtf8().constData(), 0);
        av_dict_set(&fallbackOptions, "framerate", QString::number(framerate).toUtf8().constData(), 0);
        
        ret = avformat_open_input(&m_formatContext, devicePath.toUtf8().constData(), inputFormat, &fallbackOptions);
        av_dict_free(&fallbackOptions);
    }
    
    // If everything fails, try minimal options
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
        qCWarning(log_ffmpeg_backend) << "Auto-detection failed:" << QString::fromUtf8(errbuf) << "- trying minimal options";
        
        // Reset format context
        if (m_formatContext) {
            avformat_close_input(&m_formatContext);
        }
        m_formatContext = avformat_alloc_context();
        
        // Re-set interrupt callback
        m_formatContext->interrupt_callback.callback = FFmpegBackendHandler::interruptCallback;
        m_formatContext->interrupt_callback.opaque = this;
        
        // Try with minimal options (just the device path)
        ret = avformat_open_input(&m_formatContext, devicePath.toUtf8().constData(), inputFormat, nullptr);
    }
    
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
        qCCritical(log_ffmpeg_backend) << "Failed to open input device with all attempts:" << QString::fromUtf8(errbuf);
        return false;
    }
    
    qCDebug(log_ffmpeg_backend) << "Successfully opened device" << devicePath;
#endif // Q_OS_WIN
    
    // CRITICAL FIX: Set strict timeout for stream info to prevent blocking on device reconnection
    // This prevents the app from hanging when a device is unplugged and replugged
    m_formatContext->max_analyze_duration = 1000000; // 1 second max (in microseconds)
    m_formatContext->probesize = 5000000; // 5MB max probe size
    
    // Find stream info with timeout protection
    qCDebug(log_ffmpeg_backend) << "Finding stream info (max 1 second)...";
    ret = avformat_find_stream_info(m_formatContext, nullptr);
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
        qCCritical(log_ffmpeg_backend) << "Failed to find stream info:" << QString::fromUtf8(errbuf);
        return false;
    }
    qCDebug(log_ffmpeg_backend) << "Stream info found successfully";
    
    // Set real-time flags to prevent buffer overflow
    // m_formatContext->flags |= AVFMT_FLAG_NONBLOCK; // May not be supported
    m_formatContext->max_analyze_duration = 50000; // 0.05 seconds
    m_formatContext->probesize = 1000000; // 1MB
    
    // Find video stream
    m_videoStreamIndex = -1;
    for (unsigned int i = 0; i < m_formatContext->nb_streams; i++) {
        if (m_formatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            m_videoStreamIndex = i;
            break;
        }
    }
    
    if (m_videoStreamIndex == -1) {
        qCCritical(log_ffmpeg_backend) << "No video stream found";
        return false;
    }
    
    // Get codec parameters
    AVCodecParameters* codecpar = m_formatContext->streams[m_videoStreamIndex]->codecpar;
    
    // Update preferred hardware acceleration from settings
    updatePreferredHardwareAcceleration();
    
    // Try to initialize hardware acceleration if not already done
    if (m_hwDeviceType == AV_HWDEVICE_TYPE_NONE) {
        initializeHardwareAcceleration();
    }
    
    // Find decoder - try hardware decoder for MJPEG if available
    const AVCodec* codec = nullptr;
    bool usingHwDecoder = false;
    
    // Try hardware decoder first if hardware acceleration is available
    // Note: Some decoders like CUVID don't need a device context, so check device type instead
    if (m_hwDeviceType != AV_HWDEVICE_TYPE_NONE) {
        bool hwDecoderFound = tryHardwareDecoder(codecpar, &codec, &usingHwDecoder);
        if (hwDecoderFound && codec) {
            qCInfo(log_ffmpeg_backend) << "✓✓✓ Successfully selected hardware decoder:" << codec->name << "✓✓✓";
        }
    }
    
    // Fallback to software decoder if hardware decoder not found
    if (!codec) {
        codec = avcodec_find_decoder(codecpar->codec_id);
        if (!codec) {
            qCCritical(log_ffmpeg_backend) << "Decoder not found for codec ID:" << codecpar->codec_id;
            return false;
        }
        const char* codecName = codec->name ? codec->name : "unknown";
        qCDebug(log_ffmpeg_backend) << "Using software decoder:" << codecName;
    }
    
    // Allocate codec context
    m_codecContext = avcodec_alloc_context3(codec);
    if (!m_codecContext) {
        qCCritical(log_ffmpeg_backend) << "Failed to allocate codec context";
        return false;
    }
    
    // Copy codec parameters
    ret = avcodec_parameters_to_context(m_codecContext, codecpar);
    if (ret < 0) {
        qCCritical(log_ffmpeg_backend) << "Failed to copy codec parameters";
        return false;
    }
    
    // CRITICAL: Set low-latency codec options before opening
    m_codecContext->flags |= AV_CODEC_FLAG_LOW_DELAY;      // Enable low-delay decoding
    m_codecContext->flags2 |= AV_CODEC_FLAG2_FAST;         // Prioritize speed over quality
    
    // Use single-threading for lower latency, even with hardware acceleration
    if (usingHwDecoder) {
        m_codecContext->thread_count = 1;  // Single thread for lowest latency with GPU
    } else {
        m_codecContext->thread_count = 1;  // Single thread for software decoding latency
    }
    
    // Set hardware device context if using hardware decoder AND context is available
    // Note: CUVID on Windows doesn't require a device context, so we skip this step for CUDA
    if (usingHwDecoder && m_hwDeviceContext) {
        m_codecContext->hw_device_ctx = av_buffer_ref(m_hwDeviceContext);
        if (!m_codecContext->hw_device_ctx) {
            qCWarning(log_ffmpeg_backend) << "Failed to reference hardware device context";
            // Continue with software fallback
            avcodec_free_context(&m_codecContext);
            
            // Try again with software decoder
            codec = avcodec_find_decoder(codecpar->codec_id);
            if (!codec) {
                qCCritical(log_ffmpeg_backend) << "Software decoder not found for codec ID:" << codecpar->codec_id;
                return false;
            }
            
            m_codecContext = avcodec_alloc_context3(codec);
            if (!m_codecContext) {
                qCCritical(log_ffmpeg_backend) << "Failed to allocate codec context for software decoder";
                return false;
            }
            
            ret = avcodec_parameters_to_context(m_codecContext, codecpar);
            if (ret < 0) {
                qCCritical(log_ffmpeg_backend) << "Failed to copy codec parameters to software decoder";
                return false;
            }
            
            // CRITICAL: Set low-latency codec options for software decoder
            m_codecContext->flags |= AV_CODEC_FLAG_LOW_DELAY;
            m_codecContext->flags2 |= AV_CODEC_FLAG2_FAST;
            m_codecContext->thread_count = 1;
            
            usingHwDecoder = false;
            qCInfo(log_ffmpeg_backend) << "Falling back to software decoder:" << codec->name;
        } else {
            const char* hwType = (m_hwDeviceType == AV_HWDEVICE_TYPE_CUDA) ? "CUDA/NVDEC" : "QSV";
            qCInfo(log_ffmpeg_backend) << "✓" << hwType << "hardware device context set successfully";
        }
    } else if (usingHwDecoder && !m_hwDeviceContext) {
        // CUVID on Windows case - no device context needed
        const char* hwType = (m_hwDeviceType == AV_HWDEVICE_TYPE_CUDA) ? "CUDA/NVDEC" : "Hardware";
        qCInfo(log_ffmpeg_backend) << "✓" << hwType << "decoder will be used without device context (normal for CUVID on Windows)";
    }
    
    // Prepare decoder options for CUDA/NVDEC optimization
    AVDictionary* codecOptions = nullptr;
    if (usingHwDecoder && m_hwDeviceType == AV_HWDEVICE_TYPE_CUDA) {
        // CUDA/NVDEC specific options for ultra-low latency
        av_dict_set(&codecOptions, "gpu", "0", 0);  // Use first GPU (can be changed if multiple GPUs)
        av_dict_set(&codecOptions, "surfaces", "1", 0);  // Minimal surfaces for lowest latency (deprecated but still used)
        av_dict_set(&codecOptions, "low_latency", "1", 0);  // Enable low latency mode for CUVID decoders
        av_dict_set(&codecOptions, "delay", "0", 0);  // No delay
        av_dict_set(&codecOptions, "rgb_mode", "1", 0);  // Output RGB directly from GPU for faster rendering
        
        qCInfo(log_ffmpeg_backend) << "Setting CUDA/NVDEC decoder options: gpu=0, surfaces=1, low_latency=1, delay=0, rgb_mode=1";
    }
    
    // Open codec
    qCInfo(log_ffmpeg_backend) << "Attempting to open codec:" << codec->name;
    ret = avcodec_open2(m_codecContext, codec, &codecOptions);
    
    // Log any unused options (helps debug configuration issues)
    if (codecOptions) {
        AVDictionaryEntry* entry = nullptr;
        while ((entry = av_dict_get(codecOptions, "", entry, AV_DICT_IGNORE_SUFFIX))) {
            qCWarning(log_ffmpeg_backend) << "Unused codec option:" << entry->key << "=" << entry->value;
        }
    }
    av_dict_free(&codecOptions);
    
    // Set low delay flags to prevent buffer overflow
    m_codecContext->flags |= AV_CODEC_FLAG_LOW_DELAY;
    m_codecContext->delay = 0;
    // Set video delay to 0 for the stream
    if (m_formatContext->streams[m_videoStreamIndex]) {
        m_formatContext->streams[m_videoStreamIndex]->codecpar->video_delay = 0;
    }
    
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
        
        // If hardware decoder fails, try software fallback
        if (usingHwDecoder) {
            const char* hwType = (m_hwDeviceType == AV_HWDEVICE_TYPE_CUDA) ? "CUDA/NVDEC" : "QSV";
            qCWarning(log_ffmpeg_backend) << "✗ Failed to open" << hwType << "hardware codec:" 
                                          << QString::fromUtf8(errbuf);
            qCWarning(log_ffmpeg_backend) << "  - Falling back to software decoder...";
            
            avcodec_free_context(&m_codecContext);
            
            // Try software decoder
            codec = avcodec_find_decoder(codecpar->codec_id);
            if (!codec) {
                qCCritical(log_ffmpeg_backend) << "Software decoder not found for codec ID:" << codecpar->codec_id;
                return false;
            }
            
            m_codecContext = avcodec_alloc_context3(codec);
            if (!m_codecContext) {
                qCCritical(log_ffmpeg_backend) << "Failed to allocate codec context for software decoder";
                return false;
            }
            
            ret = avcodec_parameters_to_context(m_codecContext, codecpar);
            if (ret < 0) {
                qCCritical(log_ffmpeg_backend) << "Failed to copy codec parameters to software decoder";
                return false;
            }
            
            // CRITICAL: Set low-latency codec options for software decoder fallback
            m_codecContext->flags |= AV_CODEC_FLAG_LOW_DELAY;
            m_codecContext->flags2 |= AV_CODEC_FLAG2_FAST;
            m_codecContext->thread_count = 1;
            
            ret = avcodec_open2(m_codecContext, codec, nullptr);
            if (ret < 0) {
                av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
                qCCritical(log_ffmpeg_backend) << "Failed to open software codec:" << QString::fromUtf8(errbuf);
                return false;
            }
            
            usingHwDecoder = false;
            qCInfo(log_ffmpeg_backend) << "✓ Successfully opened software decoder:" << codec->name;
        } else {
            qCCritical(log_ffmpeg_backend) << "Failed to open codec:" << QString::fromUtf8(errbuf);
            return false;
        }
    } else if (usingHwDecoder) {
        // Hardware codec opened successfully - log details
        qCInfo(log_ffmpeg_backend) << "✓✓✓ Successfully opened hardware codec:" << codec->name << "✓✓✓";
        qCInfo(log_ffmpeg_backend) << "  - Codec pixel format:" << m_codecContext->pix_fmt 
                                   << "(" << av_get_pix_fmt_name(m_codecContext->pix_fmt) << ")";
        qCInfo(log_ffmpeg_backend) << "  - Codec capabilities:" << codec->capabilities;
        
        // Check if this codec outputs hardware frames
        if (codec->capabilities & AV_CODEC_CAP_HARDWARE) {
            qCInfo(log_ffmpeg_backend) << "  - Codec has AV_CODEC_CAP_HARDWARE capability";
        }
    }
    
    // Allocate frames
    m_frame = av_frame_alloc();
    m_frameRGB = av_frame_alloc();
    m_packet = av_packet_alloc();
    
    if (!m_frame || !m_frameRGB || !m_packet) {
        qCCritical(log_ffmpeg_backend) << "Failed to allocate frames or packet";
        return false;
    }
    
    qCDebug(log_ffmpeg_backend) << "Input device opened successfully:"
                                << "codec=" << codec->name
                                << "hw_accel=" << (usingHwDecoder ? av_hwdevice_get_type_name(m_hwDeviceType) : "none")
                                << "codec_id=" << codecpar->codec_id
                                << "resolution=" << codecpar->width << "x" << codecpar->height
                                << "pixel_format=" << codecpar->format;
    
    // Reset operation timer - device opened successfully
    m_operationStartTime = 0;
    
    return true;
}

void FFmpegBackendHandler::closeInputDevice()
{
    // Free frames and packet
    if (m_frame) {
        av_frame_free(&m_frame);
        m_frame = nullptr;
    }
    
    if (m_frameRGB) {
        av_frame_free(&m_frameRGB);
        m_frameRGB = nullptr;
    }
    
    if (m_packet) {
        av_packet_free(&m_packet);
        m_packet = nullptr;
    }
    
    // Free scaling context
    if (m_swsContext) {
        sws_freeContext(m_swsContext);
        m_swsContext = nullptr;
    }
    
    // Close codec context
    if (m_codecContext) {
        avcodec_free_context(&m_codecContext);
        m_codecContext = nullptr;
    }
    
    // Close format context
    if (m_formatContext) {
        avformat_close_input(&m_formatContext);
        m_formatContext = nullptr;
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

    if (!m_formatContext || m_videoStreamIndex == -1) {
        static int noContextWarnings = 0;
        if (noContextWarnings < 5) { // Limit warnings to avoid spam
            qCWarning(log_ffmpeg_backend) << "readFrame called with invalid context or stream index";
            noContextWarnings++;
        }
        return false;
    }
    
    // Read packet from input with timeout handling
    int ret = av_read_frame(m_formatContext, m_packet);
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
    if (m_packet->stream_index != m_videoStreamIndex) {
        av_packet_unref(m_packet);
        return false;
    }
    
    return true;
}

void FFmpegBackendHandler::processFrame()
{
    // Check if capture is stopping or resources are gone (no lock to avoid blocking)
    if (!m_captureRunning || !m_formatContext || !m_codecContext) {
        return; // Exit early if capture is stopping or resources are gone
    }

    if (!m_packet || !m_codecContext) {
        return;
    }
    
    // Validate packet data before processing
    if (m_packet->size <= 0 || !m_packet->data) {
        if (m_packet->size > 0 && !m_packet->data) {
            qCWarning(log_ffmpeg_backend) << "Invalid packet: null data but size" << m_packet->size;
        }
        av_packet_unref(m_packet);
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
    // ULTRA LOW LATENCY: Very aggressive frame dropping for minimal delay
    int frameDropThreshold = isRecording ? 1 : 2; 
    if (currentTime - lastProcessTime < frameDropThreshold) {
        droppedFrames++;
        av_packet_unref(m_packet);
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
    if (m_codecContext->codec) {
        const char* codecName = m_codecContext->codec->name;
        // Check if this is a hardware decoder (ends with _cuvid, _qsv, etc.)
        usingHardwareDecoder = (strstr(codecName, "_cuvid") != nullptr || 
                               strstr(codecName, "_qsv") != nullptr ||
                               strstr(codecName, "_nvdec") != nullptr);
    }
    
    // PRIORITY ORDER for MJPEG decoding:
    // 1. Hardware decoder (CUVID/NVDEC, QSV) - Best performance, GPU acceleration
    // 2. TurboJPEG - Fast CPU-based JPEG decoder
    // 3. FFmpeg software decoder - Fallback option
    
    if (m_codecContext->codec_id == AV_CODEC_ID_MJPEG) {
        if (usingHardwareDecoder) {
            // Use hardware decoder (CUVID/NVDEC or QSV)
            static int hwFrameCount = 0;
            hwFrameCount++;
            
            if (hwFrameCount % 1000 == 1) {
                const char* hwType = (strstr(m_codecContext->codec->name, "cuvid") != nullptr) ? "NVDEC/CUVID" : "Hardware";
                qCInfo(log_ffmpeg_backend) << "Using" << hwType << "GPU acceleration for MJPEG (frame" << hwFrameCount << ")";
            }
            
            pixmap = decodeFrame(m_packet);
            
            // If hardware decoder fails, try TurboJPEG fallback
            if (pixmap.isNull()) {
#ifdef HAVE_LIBJPEG_TURBO
                if (hwFrameCount % 1000 == 1) {
                    qCWarning(log_ffmpeg_backend) << "Hardware decoder failed, falling back to TurboJPEG";
                }
                pixmap = decodeJpegFrame(m_packet->data, m_packet->size);
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
            if (m_packet->size < 10) { // Minimum JPEG header size
                if (turbojpegFrameCount % 5000 == 1) {
                    qCWarning(log_ffmpeg_backend) << "JPEG packet too small:" << m_packet->size << "bytes, falling back to FFmpeg decoder";
                }
                pixmap = decodeFrame(m_packet);
            } else {
                pixmap = decodeJpegFrame(m_packet->data, m_packet->size);
                
                // If TurboJPEG failed, fall back to FFmpeg decoder
                if (pixmap.isNull()) {
                    if (turbojpegFrameCount % 5000 == 1) {
                        qCDebug(log_ffmpeg_backend) << "TurboJPEG failed, falling back to FFmpeg software decoder";
                    }
                    pixmap = decodeFrame(m_packet);
                }
            }
#else
            // RESPONSIVENESS: Only log this occasionally to reduce overhead
            static int noTurboLogCount = 0;
            if (++noTurboLogCount % 5000 == 1) {
                qCDebug(log_ffmpeg_backend) << "Using FFmpeg software decoder for MJPEG frame (TurboJPEG not available)";
            }
            pixmap = decodeFrame(m_packet);
#endif
        }
    } else {
        // Non-MJPEG codecs use FFmpeg decoder
        static int ffmpegFrameCount = 0;
        ffmpegFrameCount++;
        
        // Only log every 1000th frame to minimize debug overhead
        if (ffmpegFrameCount % 1000 == 1) {
            qCDebug(log_ffmpeg_backend) << "Using FFmpeg decoder (frame" << ffmpegFrameCount << "), codec:" << m_codecContext->codec_id;
        }
        pixmap = decodeFrame(m_packet);
    }
    
    // Clean up packet
    av_packet_unref(m_packet);
    
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
                    // Calculate target interval based on cached recording framerate
                    qint64 targetInterval = 1000 / m_recordingTargetFramerate; // milliseconds between frames
                    
                    if (m_lastRecordedFrameTime == 0 || (currentTime - m_lastRecordedFrameTime) >= targetInterval) {
                        shouldWriteFrame = true;
                        m_lastRecordedFrameTime = currentTime;
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
                                                   << "recording frame size:" << m_recordingFrame->width << "x" << m_recordingFrame->height
                                                   << "frame interval:" << (currentTime - m_lastRecordedFrameTime) << "ms";
                    }
                    
                    // Quick check without mutex (writeFrameToFile will do proper mutex checking)
                    if (m_recordingActive && m_recordingFrame && m_recordingSwsContext) {
                        // Fill frame with image data
                        const uint8_t* srcData[1] = { image.constBits() };
                        int srcLinesize[1] = { static_cast<int>(image.bytesPerLine()) };
                        
                        // Convert RGB to target pixel format for encoding (YUVJ420P for MJPEG)
                        int scaleResult = sws_scale(m_recordingSwsContext, srcData, srcLinesize, 0, image.height(),
                                                  m_recordingFrame->data, m_recordingFrame->linesize);
                        
                        if (scaleResult != image.height()) {
                            qCWarning(log_ffmpeg_backend) << "sws_scale conversion warning: converted" << scaleResult << "lines, expected" << image.height();
                        }
                        
                        // Write frame to file (this function will handle mutex locking internally)
                    if (!writeFrameToFile(m_recordingFrame)) {
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
        if (m_packet->size > 0) {
            qCWarning(log_ffmpeg_backend) << "Failed to decode frame - pixmap is null";
            qCWarning(log_ffmpeg_backend) << "Frame decode failure details:";
            qCWarning(log_ffmpeg_backend) << "  - Packet size:" << m_packet->size;
            qCWarning(log_ffmpeg_backend) << "  - Codec ID:" << (m_codecContext ? m_codecContext->codec_id : -1);
            qCWarning(log_ffmpeg_backend) << "  - Stream index:" << m_packet->stream_index;
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
    if (!m_codecContext || !m_frame) {
        qCWarning(log_ffmpeg_backend) << "decodeFrame: Missing codec context or frame";
        return QPixmap();
    }
    
    // Send packet to decoder
    int ret = avcodec_send_packet(m_codecContext, packet);
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
        qCWarning(log_ffmpeg_backend) << "Error sending packet to decoder:" << QString::fromUtf8(errbuf);
        return QPixmap();
    }
    
    // Receive frame from decoder
    ret = avcodec_receive_frame(m_codecContext, m_frame);
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
            qCWarning(log_ffmpeg_backend) << "  Decoder:" << (m_codecContext->codec ? m_codecContext->codec->name : "unknown");
            qCWarning(log_ffmpeg_backend) << "  Pixel format:" << m_codecContext->pix_fmt;
            qCWarning(log_ffmpeg_backend) << "  Error code:" << ret;
        }
        return QPixmap();
    }
    
    // Validate frame data
    if (!m_frame->data[0]) {
        qCWarning(log_ffmpeg_backend) << "decodeFrame: Frame data is null";
        return QPixmap();
    }
    
    if (m_frame->width <= 0 || m_frame->height <= 0) {
        qCWarning(log_ffmpeg_backend) << "decodeFrame: Invalid frame dimensions:" 
                                     << m_frame->width << "x" << m_frame->height;
        return QPixmap();
    }
    
    // Log frame format information for hardware decoder debugging
    static int frameFormatLogCount = 0;
    if (++frameFormatLogCount <= 5 || frameFormatLogCount % 1000 == 1) {
        const char* codecName = m_codecContext->codec ? m_codecContext->codec->name : "unknown";
        const char* formatName = av_get_pix_fmt_name(static_cast<AVPixelFormat>(m_frame->format));
        qCInfo(log_ffmpeg_backend) << "Received frame from" << codecName 
                                   << "- format:" << m_frame->format 
                                   << "(" << (formatName ? formatName : "unknown") << ")"
                                   << "size:" << m_frame->width << "x" << m_frame->height;
    }
    
    // IMPORTANT: mjpeg_cuvid on Windows decodes to system memory in NV12/YUV420P format
    // It does NOT output AV_PIX_FMT_CUDA format like H264/HEVC CUVID decoders
    // The GPU is still used for decoding, but output is directly in system memory
    // This is actually GOOD - no need for slow GPU->CPU transfer!
    
    // Check if this is a hardware frame that needs to be transferred to system memory
    AVFrame* frameToConvert = m_frame;
    AVFrame* swFrame = nullptr;
    
    // Check for hardware pixel formats that need transfer
    // NOTE: mjpeg_cuvid outputs to system memory (NV12/YUV420P), so no transfer needed
    bool isHardwareFrame = (m_frame->format == AV_PIX_FMT_QSV ||       // Intel QSV
                           m_frame->format == AV_PIX_FMT_CUDA);        // NVIDIA CUDA (H264/HEVC, not MJPEG)
    
    if (isHardwareFrame) {
        // This is a hardware frame - need to transfer to system memory
        static int hwTransferAttempts = 0;
        hwTransferAttempts++;
        
        if (hwTransferAttempts <= 5) {
            const char* hwType = (m_frame->format == AV_PIX_FMT_QSV) ? "QSV" : "CUDA";
            qCInfo(log_ffmpeg_backend) << "Attempting to transfer hardware frame (" << hwType 
                                       << ") to system memory (attempt" << hwTransferAttempts << ")";
        }
        
        swFrame = av_frame_alloc();
        if (!swFrame) {
            qCWarning(log_ffmpeg_backend) << "Failed to allocate frame for hardware transfer";
            return QPixmap();
        }
        
        // Transfer data from GPU to CPU
        ret = av_hwframe_transfer_data(swFrame, m_frame, 0);
        if (ret < 0) {
            char errbuf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
            
            static int transferErrorCount = 0;
            if (++transferErrorCount <= 5) {
                qCWarning(log_ffmpeg_backend) << "Error transferring hardware frame to system memory:" 
                                              << QString::fromUtf8(errbuf);
                qCWarning(log_ffmpeg_backend) << "  Frame format:" << m_frame->format;
                qCWarning(log_ffmpeg_backend) << "  Frame size:" << m_frame->width << "x" << m_frame->height;
                qCWarning(log_ffmpeg_backend) << "  This might indicate the decoder doesn't support hardware frames properly";
            }
            
            av_frame_free(&swFrame);
            return QPixmap();
        }
        
        // Copy metadata from hardware frame
        av_frame_copy_props(swFrame, m_frame);
        
        frameToConvert = swFrame;
        
        static int hwTransferCount = 0;
        if (++hwTransferCount % 1000 == 1) {
            const char* hwType = (m_frame->format == AV_PIX_FMT_QSV) ? "QSV" : "CUDA";
            qCDebug(log_ffmpeg_backend) << "Hardware frame (" << hwType 
                                       << ") transferred to system memory (count:" << hwTransferCount << ")";
        }
    }
    
    // PERFORMANCE: Skip success logging for every frame
    QPixmap result = convertFrameToPixmap(frameToConvert);
    
    // Free the software frame if we allocated it
    if (swFrame) {
        av_frame_free(&swFrame);
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
                    QTimer::singleShot(1000, this, [this, portChain]() {
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
                                QTimer::singleShot(500, this, [this, foundDeviceName, portChain]() {
                                    handleDeviceActivation(foundDeviceName, portChain);
                                });
                            } else if (!m_captureRunning) {
                                qCInfo(log_ffmpeg_backend) << "  → Found device after retry, auto-starting capture:" << foundDeviceName;
                                QTimer::singleShot(500, this, [this, foundDeviceName, portChain]() {
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
                        QTimer::singleShot(500, this, [this, devicePath, portChain]() {
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
    QTimer::singleShot(500, this, [this, resolution, framerate]() {
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
    
    // Wait a small amount of time to ensure any ongoing frame processing completes
    // This prevents race conditions with the capture thread
    QThread::msleep(50);
    
    // Now safely finalize and cleanup recording resources
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
    // Find encoder - try requested codec first
    const AVCodec* codec = nullptr;
    if (!m_recordingConfig.videoCodec.isEmpty()) {
        codec = avcodec_find_encoder_by_name(m_recordingConfig.videoCodec.toUtf8().data());
        if (codec) {
            qCDebug(log_ffmpeg_backend) << "Found requested encoder:" << m_recordingConfig.videoCodec;
        }
    }
    
    // Fallback chain for available encoders in current FFmpeg build
    if (!codec) {
        qCWarning(log_ffmpeg_backend) << "Requested encoder not found:" << m_recordingConfig.videoCodec << "- trying fallbacks";
        
        // Try MJPEG encoder first (works well with AVI container for playable video files)
        codec = avcodec_find_encoder(AV_CODEC_ID_MJPEG);
        if (codec) {
            qCDebug(log_ffmpeg_backend) << "Using MJPEG encoder as fallback";
        }
    }
    
    // Try rawvideo encoder (creates uncompressed video streams)
    if (!codec) {
        codec = avcodec_find_encoder(AV_CODEC_ID_RAWVIDEO);
        if (codec) {
            qCDebug(log_ffmpeg_backend) << "Using rawvideo encoder as fallback";
        }
    }
    
    // Final fallback attempts
    if (!codec) {
        codec = avcodec_find_encoder(AV_CODEC_ID_H264);
        if (codec) {
            qCDebug(log_ffmpeg_backend) << "Found H264 encoder";
        }
    }
    
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
        av_opt_set(m_recordingCodecContext->priv_data, "preset", "medium", 0);
        av_opt_set(m_recordingCodecContext->priv_data, "tune", "zerolatency", 0);
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
    m_recordingFrame = av_frame_alloc();
    if (!m_recordingFrame) {
        qCWarning(log_ffmpeg_backend) << "Failed to allocate recording frame";
        return false;
    }
    
    m_recordingFrame->format = m_recordingCodecContext->pix_fmt;
    m_recordingFrame->width = m_recordingCodecContext->width;
    m_recordingFrame->height = m_recordingCodecContext->height;
    
    ret = av_frame_get_buffer(m_recordingFrame, 0);
    if (ret < 0) {
        qCWarning(log_ffmpeg_backend) << "Failed to allocate frame buffer";
        return false;
    }
    
    // Allocate packet
    m_recordingPacket = av_packet_alloc();
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
    
    // Calculate proper timestamp based on frame rate and frame number
    // This ensures even frame spacing in the output video
    frame->pts = m_recordingFrameNumber;
    
    // Debug logging for first few frames to verify timing
    static int debugFrameCount = 0;
    if (++debugFrameCount <= 5 || debugFrameCount % 100 == 0) { // Log first 5 and every 100th frame
        qCDebug(log_ffmpeg_backend) << "Writing frame" << m_recordingFrameNumber 
                                   << "with PTS" << frame->pts 
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
        ret = avcodec_receive_packet(m_recordingCodecContext, m_recordingPacket);
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
            av_packet_unref(m_recordingPacket);
            return false;
        }
        
        // Scale packet timestamp
        av_packet_rescale_ts(m_recordingPacket, m_recordingCodecContext->time_base, m_recordingVideoStream->time_base);
        m_recordingPacket->stream_index = m_recordingVideoStream->index;
        
        // Write packet to output file
        ret = av_interleaved_write_frame(m_recordingFormatContext, m_recordingPacket);
        if (ret < 0) {
            char errbuf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
            qCWarning(log_ffmpeg_backend) << "Error writing frame to file:" << QString::fromUtf8(errbuf);
            av_packet_unref(m_recordingPacket);
            return false;
        }
        
        av_packet_unref(m_recordingPacket);
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
                ret = avcodec_receive_packet(m_recordingCodecContext, m_recordingPacket);
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
                    av_packet_rescale_ts(m_recordingPacket, m_recordingCodecContext->time_base, m_recordingVideoStream->time_base);
                    m_recordingPacket->stream_index = m_recordingVideoStream->index;
                    av_interleaved_write_frame(m_recordingFormatContext, m_recordingPacket);
                }
                av_packet_unref(m_recordingPacket);
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
        av_frame_free(&m_recordingFrame);
    }
    
    if (m_recordingPacket) {
        av_packet_free(&m_recordingPacket);
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
    return m_captureRunning && m_formatContext != nullptr && m_codecContext != nullptr;
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
