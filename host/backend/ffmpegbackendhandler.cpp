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

// FFmpeg includes (conditional compilation)
#ifdef HAVE_FFMPEG
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
#include <libavdevice/avdevice.h>
}
#endif

// libjpeg-turbo includes (conditional compilation)
#ifdef HAVE_LIBJPEG_TURBO
#include <turbojpeg.h>
#endif

Q_LOGGING_CATEGORY(log_ffmpeg_backend, "opf.backend.ffmpeg")

#ifdef HAVE_FFMPEG
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
        
        QElapsedTimer frameTimer;
        QElapsedTimer performanceTimer;
        frameTimer.start();
        performanceTimer.start();
        
        int consecutiveFailures = 0;
        int framesProcessed = 0;
        const int maxConsecutiveFailures = 100; // Allow some tolerance
        
        while (isRunning()) {
            if (m_handler && m_handler->readFrame()) {
                // Reset failure counter on successful read
                consecutiveFailures = 0;
                
                // RESPONSIVENESS OPTIMIZATION: For KVM applications, prioritize responsiveness over smooth video
                // Remove artificial frame rate limiting to reduce perceived mouse lag
                qint64 elapsed = frameTimer.elapsed();
                
                // Only enforce a minimal interval to prevent complete CPU saturation
                // But prioritize responsiveness - especially important for mouse interaction
                qint64 minInterval = 8; // ~120 FPS max, much more responsive than 25 FPS
                if (elapsed < minInterval) {
                    // Very short sleep to yield CPU but maintain responsiveness
                    msleep(5); // Just 1ms instead of forcing frame intervals
                    frameTimer.restart();
                } else {
                    frameTimer.restart();
                }
                
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
                if (consecutiveFailures >= 50 && consecutiveFailures % 25 == 0) { // Reduced from 200/100
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
#endif // HAVE_FFMPEG

FFmpegBackendHandler::FFmpegBackendHandler(QObject *parent)
    : MultimediaBackendHandler(parent)
#ifdef HAVE_FFMPEG
      , m_formatContext(nullptr),
      m_codecContext(nullptr),
      m_frame(nullptr),
      m_frameRGB(nullptr),
      m_packet(nullptr),
      m_swsContext(nullptr),
      m_captureRunning(false),
      m_videoStreamIndex(-1),
      m_frameCount(0),
      m_lastFrameTime(0),
      m_recordingFormatContext(nullptr),
      m_recordingCodecContext(nullptr),
      m_recordingVideoStream(nullptr),
      m_recordingSwsContext(nullptr),
      m_recordingFrame(nullptr),
      m_recordingPacket(nullptr)
#endif
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
    
#ifdef HAVE_FFMPEG
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
            m_frameCount = 0;
        }
    });
#endif
}

FFmpegBackendHandler::~FFmpegBackendHandler()
{
    // Stop recording first if active
    if (m_recordingActive) {
        stopRecording();
    }
    
    // Disconnect from hotplug monitor
    disconnectFromHotplugMonitor();
    
#ifdef HAVE_FFMPEG
    stopDirectCapture();
    cleanupRecording();
    cleanupFFmpeg();
#endif
}

MultimediaBackendType FFmpegBackendHandler::getBackendType() const
{
    return MultimediaBackendType::FFmpeg;
}

QString FFmpegBackendHandler::getBackendName() const
{
    return "FFmpeg Direct Capture";
}

#ifdef HAVE_FFMPEG
bool FFmpegBackendHandler::isDirectCaptureRunning() const
{
    return m_captureRunning;
}
#else
bool FFmpegBackendHandler::isDirectCaptureRunning() const
{
    return false;
}
#endif

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

void FFmpegBackendHandler::prepareCameraCreation(QCamera* oldCamera)
{
    if (oldCamera) {
        qCDebug(log_ffmpeg_backend) << "FFmpeg: Stopping old camera before creating new one";
        stopDirectCapture();
        QThread::msleep(m_config.deviceSwitchDelay);
    }
}

void FFmpegBackendHandler::configureCameraDevice(QCamera* camera, const QCameraDevice& device)
{
    qCDebug(log_ffmpeg_backend) << "FFmpeg: Configuring camera device:" << device.description() << "ID:" << device.id();

#ifdef HAVE_FFMPEG
    // Extract device path for direct FFmpeg usage
    QString deviceId = QString::fromUtf8(device.id());
    QString deviceDescription = device.description();
    
    // Convert Qt device ID to V4L2 device path if needed
    if (!deviceId.startsWith("/dev/video")) {
        // Check if deviceId is a simple number (like "0", "1", etc.)
        bool isNumber = false;
        int deviceNumber = deviceId.toInt(&isNumber);
        
        if (isNumber) {
            // Direct numeric ID - convert to /dev/video path
            m_currentDevice = QString("/dev/video%1").arg(deviceNumber);
            qCDebug(log_ffmpeg_backend) << "Converted numeric device ID" << deviceId << "to path:" << m_currentDevice;
        } else {
            // Complex device ID - try to extract number from it
            QRegularExpression re("(\\d+)");
            QRegularExpressionMatch match = re.match(deviceId);
            if (match.hasMatch()) {
                m_currentDevice = QString("/dev/video%1").arg(match.captured(1));
                qCDebug(log_ffmpeg_backend) << "Extracted device number from complex ID" << deviceId << "-> path:" << m_currentDevice;
            } else {
                qCWarning(log_ffmpeg_backend) << "Could not parse device ID:" << deviceId << "- this may cause issues";
                m_currentDevice = deviceId; // Use as-is and hope for the best
            }
        }
    } else {
        // Already a proper V4L2 device path
        m_currentDevice = deviceId;
        qCDebug(log_ffmpeg_backend) << "Using direct device path:" << m_currentDevice;
    }
    
    qCDebug(log_ffmpeg_backend) << "FFmpeg device path configured as:" << m_currentDevice;
    
    // Check device availability and emit connection status
    bool deviceAvailable = checkCameraAvailable(m_currentDevice);
    emit deviceConnectionChanged(m_currentDevice, deviceAvailable);
    
    if (!deviceAvailable) {
        qCWarning(log_ffmpeg_backend) << "Configured device is not available:" << m_currentDevice;
    }
#endif
    
    // Don't start Qt camera for FFmpeg backend
    if (camera) {
        qCDebug(log_ffmpeg_backend) << "Stopping Qt camera to prevent device conflicts";
        camera->stop();
        QThread::msleep(100);
    }
}

void FFmpegBackendHandler::setupCaptureSession(QMediaCaptureSession* session, QCamera* camera)
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

void FFmpegBackendHandler::startCamera(QCamera* camera)
{
    qCDebug(log_ffmpeg_backend) << "FFmpeg: Starting camera with direct capture";
    
#ifdef HAVE_FFMPEG
    qCDebug(log_ffmpeg_backend) << "Current device:" << m_currentDevice;
    qCDebug(log_ffmpeg_backend) << "Current resolution:" << m_currentResolution;
    qCDebug(log_ffmpeg_backend) << "Current framerate:" << m_currentFramerate;
    
    // If no device is set or device is not available, wait for device activation
    if (m_currentDevice.isEmpty() || !checkCameraAvailable(m_currentDevice)) {
        qCInfo(log_ffmpeg_backend) << "Device not available, waiting for device activation:" << m_currentDevice;
        waitForDeviceActivation(m_currentDevice, 30000); // Wait up to 30 seconds
        return;
    }
    
    // Use direct FFmpeg capture instead of Qt's camera
    qCDebug(log_ffmpeg_backend) << "FFmpeg: Using direct capture - Qt camera will NOT be started";
    
    // Ensure Qt camera is stopped
    if (camera) {
        qCDebug(log_ffmpeg_backend) << "Ensuring Qt camera is stopped";
        camera->stop();
        QThread::msleep(300); // Give time for device to be released
    }
    
    // Start direct FFmpeg capture
    QSize resolution = m_currentResolution.isValid() ? m_currentResolution : QSize(1920, 1080);
    int framerate = m_currentFramerate > 0 ? m_currentFramerate : 30;
    
    if (!startDirectCapture(m_currentDevice, resolution, framerate)) {
        qCWarning(log_ffmpeg_backend) << "Failed to start FFmpeg direct capture, not falling back to Qt camera";
        emit captureError("Failed to start FFmpeg video capture");
    } else {
        qCDebug(log_ffmpeg_backend) << "FFmpeg direct capture started successfully";
        emit deviceActivated(m_currentDevice);
    }
#else
    qCWarning(log_ffmpeg_backend) << "FFmpeg backend not available, cannot start direct capture";
    emit captureError("FFmpeg backend not available");
#endif
}

void FFmpegBackendHandler::stopCamera(QCamera* camera)
{
    qCDebug(log_ffmpeg_backend) << "FFmpeg: Stopping camera";
    
#ifdef HAVE_FFMPEG
    // Stop direct capture
    stopDirectCapture();
#endif
    
    // Also stop Qt camera
    if (camera) {
        camera->stop();
        QThread::msleep(100);
    }
}

QCameraFormat FFmpegBackendHandler::selectOptimalFormat(const QList<QCameraFormat>& formats,
                                                       const QSize& resolution,
                                                       int desiredFrameRate,
                                                       QVideoFrameFormat::PixelFormat pixelFormat) const
{
    qCDebug(log_ffmpeg_backend) << "FFmpeg: Selecting optimal format with flexible frame rate matching";
    
#ifdef HAVE_FFMPEG
    // Store resolution and framerate for direct capture
    const_cast<FFmpegBackendHandler*>(this)->m_currentResolution = resolution;
    const_cast<FFmpegBackendHandler*>(this)->m_currentFramerate = desiredFrameRate;
#else
    Q_UNUSED(resolution);
    Q_UNUSED(desiredFrameRate);
    Q_UNUSED(pixelFormat);
#endif
    
    // Return the first available format since we'll handle capture directly
    return formats.isEmpty() ? QCameraFormat() : formats.first();
}

// Direct FFmpeg capture implementation
#ifdef HAVE_FFMPEG

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
    
    qCDebug(log_ffmpeg_backend) << "FFmpeg cleanup completed";
}
#endif

bool FFmpegBackendHandler::startDirectCapture(const QString& devicePath, const QSize& resolution, int framerate)
{
    QMutexLocker locker(&m_mutex);
    
    if (m_captureRunning) {
        qCDebug(log_ffmpeg_backend) << "Capture already running, stopping first";
        stopDirectCapture();
    }
    
    // Reset error suppression flag when starting capture
    m_suppressErrors = false;
    
    // Restore FFmpeg logging level
    av_log_set_level(AV_LOG_WARNING);
    
    qCDebug(log_ffmpeg_backend) << "Starting direct FFmpeg capture:"
                                << "device=" << devicePath
                                << "resolution=" << resolution
                                << "framerate=" << framerate;
    
    // Open input device
    if (!openInputDevice(devicePath, resolution, framerate)) {
        qCWarning(log_ffmpeg_backend) << "Failed to open input device";
        return false;
    }
    
    // Create and start capture thread
    m_captureThread = std::make_unique<CaptureThread>(this);
    m_captureThread->setRunning(true);
    m_captureRunning = true;
    m_captureThread->start();
    
    // Start performance monitoring
    if (m_performanceTimer) {
        m_performanceTimer->start();
    }
    
    qCDebug(log_ffmpeg_backend) << "Direct FFmpeg capture started successfully";
    return true;
}

void FFmpegBackendHandler::stopDirectCapture()
{
    QMutexLocker locker(&m_mutex);
    
    if (!m_captureRunning) {
        return;
    }
    
    qCDebug(log_ffmpeg_backend) << "Stopping direct FFmpeg capture";
    
    m_captureRunning = false;
    
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
                }
            });
        } else {
            // We're being called from a different thread, safe to wait
            qCDebug(log_ffmpeg_backend) << "Waiting for capture thread to finish...";
            m_captureThread->wait(3000); // Wait up to 3 seconds
            if (m_captureThread->isRunning()) {
                qCWarning(log_ffmpeg_backend) << "Capture thread did not stop gracefully, terminating";
                m_captureThread->terminate();
                m_captureThread->wait(1000);
            }
            m_captureThread.reset();
            qCDebug(log_ffmpeg_backend) << "Capture thread stopped and cleaned up";
        }
    }
    
    // Stop performance monitoring
    if (m_performanceTimer) {
        m_performanceTimer->stop();
    }
    
    // Close input device
    closeInputDevice();
    
    qCDebug(log_ffmpeg_backend) << "Direct FFmpeg capture stopped";
}

bool FFmpegBackendHandler::openInputDevice(const QString& devicePath, const QSize& resolution, int framerate)
{
    qCDebug(log_ffmpeg_backend) << "Opening input device:" << devicePath;
    
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
    system(bufferCommand.toUtf8().constData()); // Don't check result - optional optimization
    
    if (configResult == 0 && framerateResult == 0) {
        qCDebug(log_ffmpeg_backend) << "Device pre-configured successfully for low-latency MJPEG" << resolution << "at" << framerate << "fps";
    } else {
        qCWarning(log_ffmpeg_backend) << "Device pre-configuration failed, continuing with FFmpeg initialization";
    }
    
    // Reduce delay to minimize latency
    QThread::msleep(100); // Reduced from 200ms to 100ms
    
    // Allocate format context
    m_formatContext = avformat_alloc_context();
    if (!m_formatContext) {
        qCCritical(log_ffmpeg_backend) << "Failed to allocate format context";
        return false;
    }
    
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
    
    // Find stream info
    ret = avformat_find_stream_info(m_formatContext, nullptr);
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
        qCCritical(log_ffmpeg_backend) << "Failed to find stream info:" << QString::fromUtf8(errbuf);
        return false;
    }
    
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
    
    // Find decoder
    const AVCodec* codec = avcodec_find_decoder(codecpar->codec_id);
    if (!codec) {
        qCCritical(log_ffmpeg_backend) << "Decoder not found for codec ID:" << codecpar->codec_id;
        return false;
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
    
    // Open codec
    ret = avcodec_open2(m_codecContext, codec, nullptr);
    if (ret < 0) {
        qCCritical(log_ffmpeg_backend) << "Failed to open codec";
        return false;
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
                                << "codec_id=" << codecpar->codec_id
                                << "resolution=" << codecpar->width << "x" << codecpar->height
                                << "pixel_format=" << codecpar->format;
    
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

#ifdef HAVE_FFMPEG
void FFmpegBackendHandler::processFrame()
{
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
    int frameDropThreshold = isRecording ? 8 : 12; // 125 FPS when recording, 83 FPS when not recording
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
    
    // Check if this is MJPEG/JPEG stream for direct libjpeg-turbo decoding
    if (m_codecContext->codec_id == AV_CODEC_ID_MJPEG) {
#ifdef HAVE_LIBJPEG_TURBO
        // Use TurboJPEG for significant performance improvement on MJPEG
        static int turbojpegFrameCount = 0;
        turbojpegFrameCount++;
        
        // RESPONSIVENESS: Reduce logging overhead even more
        if (turbojpegFrameCount % 5000 == 1) { // Every 5000 frames instead of 1000
            qCDebug(log_ffmpeg_backend) << "Using TurboJPEG acceleration (frame" << turbojpegFrameCount << ")";
        }
        
        // Additional validation for JPEG data
        if (m_packet->size < 10) { // Minimum JPEG header size
            if (turbojpegFrameCount % 5000 == 1) { // Reduce warning spam
                qCWarning(log_ffmpeg_backend) << "JPEG packet too small:" << m_packet->size << "bytes, falling back to FFmpeg decoder";
            }
            pixmap = decodeFrame(m_packet);
        } else {
            pixmap = decodeJpegFrame(m_packet->data, m_packet->size);
            
            // If TurboJPEG failed, fall back to FFmpeg decoder
            if (pixmap.isNull()) {
                if (turbojpegFrameCount % 5000 == 1) { // Reduce fallback spam even more
                    qCDebug(log_ffmpeg_backend) << "TurboJPEG failed, falling back to FFmpeg decoder";
                }
                pixmap = decodeFrame(m_packet);
            }
        }
#else
        // RESPONSIVENESS: Only log this occasionally to reduce overhead
        static int noTurboLogCount = 0;
        if (++noTurboLogCount % 5000 == 1) {
            qCDebug(log_ffmpeg_backend) << "Using FFmpeg decoder for MJPEG frame (TurboJPEG not available)";
        }
        pixmap = decodeFrame(m_packet);
#endif
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
        static int startupFramesToSkip = -1;
        if (startupFramesToSkip == -1) {
            // Check environment variable first, otherwise use default
            const char* envSkipFrames = qgetenv("OPENTERFACE_SKIP_FRAMES").constData();
            if (envSkipFrames && strlen(envSkipFrames) > 0) {
                startupFramesToSkip = QString(envSkipFrames).toInt();
                qCDebug(log_ffmpeg_backend) << "Using environment variable OPENTERFACE_SKIP_FRAMES:" << startupFramesToSkip;
            } else {
                startupFramesToSkip = 5; // Default: skip first 5 frames
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
        
        // CRITICAL: Use QueuedConnection to ensure UI thread handles frame updates properly
        // This prevents blocking the capture thread and ensures smooth frame delivery
        emit frameReady(pixmap);
        
        // Write frame to recording file if recording is active
#ifdef HAVE_FFMPEG
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
#endif
        
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
#endif

#ifdef HAVE_FFMPEG

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
#endif // HAVE_FFMPEG

#ifdef HAVE_FFMPEG
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
        qCWarning(log_ffmpeg_backend) << "Error receiving frame from decoder:" << QString::fromUtf8(errbuf);
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
    
    // PERFORMANCE: Skip success logging for every frame
    return convertFrameToPixmap(m_frame);
}
#endif

#ifdef HAVE_FFMPEG
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
    // Only log critical conversion details every 1000th frame
    static int conversionLogCounter = 0;
    conversionLogCounter++;
    
    if (conversionLogCounter % 1000 == 1) {
        qCDebug(log_ffmpeg_backend) << "convertFrameToPixmap: frame" << width << "x" << height 
                                   << "format:" << format << "linesize:" << frame->linesize[0];
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
        
        m_swsContext = sws_getContext(
            width, height, format,
            width, height, AV_PIX_FMT_RGB24,
            SWS_BILINEAR, nullptr, nullptr, nullptr
        );
        
        if (!m_swsContext) {
            qCWarning(log_ffmpeg_backend) << "Failed to create scaling context for format:" << format;
            return QPixmap();
        }
        
        lastWidth = width;
        lastHeight = height;
        lastFormat = format;
        
        // Only log scaling context creation every 1000th time for performance
        static int scalingContextLogCounter = 0;
        scalingContextLogCounter++;
        if (scalingContextLogCounter % 1000 == 1) {
            qCDebug(log_ffmpeg_backend) << "Created new scaling context for format:" << format;
        }
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
    
    // Convert frame to RGB
    int scaleResult = sws_scale(m_swsContext, frame->data, frame->linesize, 0, height, rgbData, rgbLinesize);
    if (scaleResult < 0) {
        qCWarning(log_ffmpeg_backend) << "sws_scale failed with result:" << scaleResult;
        return QPixmap();
    }
    
    if (scaleResult != height) {
        qCWarning(log_ffmpeg_backend) << "sws_scale converted" << scaleResult << "lines, expected" << height;
        // Continue anyway, as partial conversion might still be useful
    }
    
    // PERFORMANCE: Skip sws_scale result logging for better performance
    // The scale operation success/failure is already validated above
    
    // PERFORMANCE: Skip pixel analysis completely - trust the conversion process
    // This eliminates unnecessary CPU overhead from pixel sampling
    QPixmap result = QPixmap::fromImage(image);
    // PERFORMANCE: Skip pixmap creation logging for better performance
    return result;
}
#endif

// Stub implementations when FFmpeg is not available
#ifndef HAVE_FFMPEG
bool FFmpegBackendHandler::startDirectCapture(const QString& devicePath, const QSize& resolution, int framerate)
{
    Q_UNUSED(devicePath);
    Q_UNUSED(resolution);
    Q_UNUSED(framerate);
    qCWarning(log_ffmpeg_backend) << "FFmpeg not available: cannot start direct capture";
    return false;
}

void FFmpegBackendHandler::stopDirectCapture()
{
    qCDebug(log_ffmpeg_backend) << "FFmpeg not available: no capture to stop";
}

void FFmpegBackendHandler::processFrame()
{
    qCDebug(log_ffmpeg_backend) << "FFmpeg not available: processFrame() called but no implementation";
}
#endif

void FFmpegBackendHandler::setVideoOutput(QGraphicsVideoItem* videoItem)
{
#ifdef HAVE_FFMPEG
    QMutexLocker locker(&m_mutex);
#endif
    
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
#ifdef HAVE_FFMPEG
    QMutexLocker locker(&m_mutex);
#endif
    
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
    
    // Check if device file exists and is accessible
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
    
    // Additional check: try to briefly open with FFmpeg to verify V4L2 compatibility
    // Skip this intrusive check if device is currently being used for capture
    if (device == m_currentDevice && m_captureRunning) {
        qCDebug(log_ffmpeg_backend) << "Device is currently in use for capture, skipping FFmpeg compatibility check";
        return true;
    }
    
#ifdef HAVE_FFMPEG
    AVFormatContext* testContext = avformat_alloc_context();
    if (!testContext) {
        qCDebug(log_ffmpeg_backend) << "Failed to allocate test format context";
        return false;
    }
    
    const AVInputFormat* inputFormat = av_find_input_format("v4l2");
    if (!inputFormat) {
        qCDebug(log_ffmpeg_backend) << "V4L2 input format not available";
        avformat_free_context(testContext);
        return false;
    }
    
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
#else
    qCDebug(log_ffmpeg_backend) << "FFmpeg not available, basic file check passed for:" << device;
    return true;
#endif
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
    
#ifdef HAVE_FFMPEG
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
#endif
}

bool FFmpegBackendHandler::restartCaptureWithDevice(const QString& devicePath, const QSize& resolution, int framerate)
{
    qCDebug(log_ffmpeg_backend) << "Attempting to restart capture with device:" << devicePath;
    
#ifdef HAVE_FFMPEG
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
#else
    qCWarning(log_ffmpeg_backend) << "FFmpeg not available, cannot restart capture";
    return false;
#endif
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
                qCDebug(log_ffmpeg_backend) << "FFmpeg: Device unplugged:" << device.portChain;
                
                // Only handle if device has camera component
                if (!device.hasCameraDevice()) {
                    return;
                }
                
                // Check if this affects our current device
                if (m_captureRunning && !isCurrentDeviceAvailable()) {
                    qCInfo(log_ffmpeg_backend) << "Current camera device became unavailable, handling disconnection";
                    handleDeviceDeactivation(m_currentDevice);
                }
            });
            
    // Connect to new device plugged in signal
    connect(m_hotplugMonitor, &HotplugMonitor::newDevicePluggedIn,
            this, [this](const DeviceInfo& device) {
                qCDebug(log_ffmpeg_backend) << "FFmpeg: New device plugged in:" << device.portChain;
                
                // Only handle if device has camera component
                if (!device.hasCameraDevice()) {
                    return;
                }
                
                // If we're waiting for a device, check if this could be it
                if (m_waitingForDevice) {
                    QString devicePath = device.cameraDevicePath;
                    if (!devicePath.isEmpty() && 
                        (m_expectedDevicePath.isEmpty() || devicePath == m_expectedDevicePath)) {
                        qCInfo(log_ffmpeg_backend) << "Found expected device, attempting activation:" << devicePath;
                        handleDeviceActivation(devicePath);
                    }
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

void FFmpegBackendHandler::handleDeviceActivation(const QString& devicePath)
{
    qCInfo(log_ffmpeg_backend) << "Handling device activation:" << devicePath;
    
    m_waitingForDevice = false;
    m_deviceWaitTimer->stop();
    
    if (!devicePath.isEmpty()) {
        m_currentDevice = devicePath;
    }
    
    // Start capture with current settings
    QSize resolution = m_currentResolution.isValid() ? m_currentResolution : QSize(1920, 1080);
    int framerate = m_currentFramerate > 0 ? m_currentFramerate : 30;
    
    qCDebug(log_ffmpeg_backend) << "Starting capture on activated device:" << m_currentDevice
                                << "resolution:" << resolution << "framerate:" << framerate;
    
    if (startDirectCapture(m_currentDevice, resolution, framerate)) {
        qCInfo(log_ffmpeg_backend) << "Successfully started capture on activated device";
        emit deviceActivated(m_currentDevice);
    } else {
        qCWarning(log_ffmpeg_backend) << "Failed to start capture on activated device";
        emit captureError("Failed to start capture on activated device: " + m_currentDevice);
    }
}

void FFmpegBackendHandler::handleDeviceDeactivation(const QString& devicePath)
{
    qCInfo(log_ffmpeg_backend) << "Handling device deactivation:" << devicePath;
    
    QString deactivatedDevice = devicePath.isEmpty() ? m_currentDevice : devicePath;
    
    // Suppress further error messages to avoid V4L2 spam
    m_suppressErrors = true;
    
    // Temporarily suppress FFmpeg logs to avoid V4L2 ioctl spam
    av_log_set_level(AV_LOG_QUIET);
    
    // Stop capture if running
    if (m_captureRunning) {
        qCDebug(log_ffmpeg_backend) << "Stopping capture due to device deactivation";
        stopDirectCapture();
    }
    
    emit deviceDeactivated(deactivatedDevice);
    
    // Start waiting for device to come back
    qCInfo(log_ffmpeg_backend) << "Starting to wait for device reconnection";
    waitForDeviceActivation(m_currentDevice, 30000); // Wait up to 30 seconds
}

// ============================================================================
// Video Recording Implementation
// ============================================================================

bool FFmpegBackendHandler::startRecording(const QString& outputPath, const QString& format, int videoBitrate)
{
#ifdef HAVE_FFMPEG
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
#else
    qCWarning(log_ffmpeg_backend) << "FFmpeg not available: startRecording() called but no implementation";
    emit recordingError("FFmpeg not available");
    return false;
#endif
}

void FFmpegBackendHandler::stopRecording()
{
#ifdef HAVE_FFMPEG
    qCDebug(log_ffmpeg_backend) << "Stopping recording";
    
    // First, mark recording as inactive to prevent new frames from being processed
    {
        QMutexLocker locker(&m_recordingMutex);
        if (!m_recordingActive) {
            qCDebug(log_ffmpeg_backend) << "Recording is not active";
            return;
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
#else
    qCDebug(log_ffmpeg_backend) << "FFmpeg not available: stopRecording() called but no implementation";
#endif
}

void FFmpegBackendHandler::pauseRecording()
{
#ifdef HAVE_FFMPEG
    QMutexLocker locker(&m_recordingMutex);
    
    if (!m_recordingActive || m_recordingPaused) {
        qCDebug(log_ffmpeg_backend) << "Recording is not active or already paused";
        return;
    }
    
    m_recordingPaused = true;
    m_recordingPausedTime = QDateTime::currentMSecsSinceEpoch();
    
    qCDebug(log_ffmpeg_backend) << "Recording paused";
    emit recordingPaused();
#else
    qCDebug(log_ffmpeg_backend) << "FFmpeg not available: pauseRecording() called but no implementation";
#endif
}

void FFmpegBackendHandler::resumeRecording()
{
#ifdef HAVE_FFMPEG
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
#else
    qCDebug(log_ffmpeg_backend) << "FFmpeg not available: resumeRecording() called but no implementation";
#endif
}

bool FFmpegBackendHandler::isRecording() const
{
    QMutexLocker locker(&m_recordingMutex);
    return m_recordingActive && !m_recordingPaused;
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

#ifdef HAVE_FFMPEG
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
#endif // HAVE_FFMPEG


#include "ffmpegbackendhandler.moc"
