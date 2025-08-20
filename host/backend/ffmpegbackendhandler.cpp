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
#include "../ui/videopane.h"
#include "../global.h"

#include <QThread>
#include <QDebug>
#include <QLoggingCategory>
#include <QApplication>
#include <QElapsedTimer>
#include <QImage>
#include <QGraphicsVideoItem>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>

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
        frameTimer.start();
        
        while (isRunning()) {
            if (m_handler && m_handler->readFrame()) {
                // Control frame rate - aim for the configured framerate
                qint64 targetInterval = 1000 / qMax(1, m_handler->m_currentFramerate); // ms per frame
                qint64 elapsed = frameTimer.elapsed();
                
                if (elapsed < targetInterval) {
                    msleep(targetInterval - elapsed);
                }
                frameTimer.restart();
                
                // Process frame directly in capture thread to avoid packet invalidation
                // This ensures packet data remains valid during processing
                m_handler->processFrame();
            } else {
                // No frame available, sleep briefly to avoid busy waiting
                msleep(1);
            }
        }
        
        qCDebug(log_ffmpeg_backend) << "FFmpeg capture thread finished";
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
      m_lastFrameTime(0)
#endif
#ifdef HAVE_LIBJPEG_TURBO
      , m_turboJpegHandle(nullptr)
#endif
      , m_graphicsVideoItem(nullptr),
      m_videoPane(nullptr)
{
    m_config = getDefaultConfig();
    
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
#ifdef HAVE_FFMPEG
    stopDirectCapture();
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
    qCDebug(log_ffmpeg_backend) << "FFmpeg: Configuring camera device:" << device.description();
    
#ifdef HAVE_FFMPEG
    // Extract device path for direct FFmpeg usage
    QString deviceId = QString::fromUtf8(device.id());
    
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
            // Complex device ID - default to video0 but this could be enhanced
            qCDebug(log_ffmpeg_backend) << "Complex device ID detected:" << deviceId << "- using fallback /dev/video0";
            m_currentDevice = "/dev/video0";
        }
    } else {
        // Already a proper V4L2 device path
        m_currentDevice = deviceId;
        qCDebug(log_ffmpeg_backend) << "Using direct device path:" << m_currentDevice;
    }
    
    qCDebug(log_ffmpeg_backend) << "FFmpeg device path configured as:" << m_currentDevice;
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
    
    // Use direct FFmpeg capture instead of Qt's camera
    if (!m_currentDevice.isEmpty()) {
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
        }
    } else {
        qCWarning(log_ffmpeg_backend) << "FFmpeg: No valid device configured";
        emit captureError("No video device configured for FFmpeg capture");
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

bool FFmpegBackendHandler::startDirectCapture(const QString& devicePath, const QSize& resolution, int framerate)
{
    QMutexLocker locker(&m_mutex);
    
    if (m_captureRunning) {
        qCDebug(log_ffmpeg_backend) << "Capture already running, stopping first";
        stopDirectCapture();
    }
    
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
        m_captureThread->wait(3000); // Wait up to 3 seconds
        if (m_captureThread->isRunning()) {
            qCWarning(log_ffmpeg_backend) << "Capture thread did not stop gracefully, terminating";
            m_captureThread->terminate();
            m_captureThread->wait(1000);
        }
        m_captureThread.reset();
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
    
    // Allocate format context
    m_formatContext = avformat_alloc_context();
    if (!m_formatContext) {
        qCCritical(log_ffmpeg_backend) << "Failed to allocate format context";
        return false;
    }
    
    // Find input format (V4L2)
    const AVInputFormat* inputFormat = av_find_input_format("v4l2");
    if (!inputFormat) {
        qCCritical(log_ffmpeg_backend) << "V4L2 input format not found";
        return false;
    }
    
    // Set input options
    AVDictionary* options = nullptr;
    av_dict_set(&options, "video_size", QString("%1x%2").arg(resolution.width()).arg(resolution.height()).toUtf8().constData(), 0);
    av_dict_set(&options, "framerate", QString::number(framerate).toUtf8().constData(), 0);
    av_dict_set(&options, "input_format", "mjpeg", 0); // Prefer MJPEG for better performance
    
    qCDebug(log_ffmpeg_backend) << "Opening device with MJPEG format first";
    
    // Open input
    int ret = avformat_open_input(&m_formatContext, devicePath.toUtf8().constData(), inputFormat, &options);
    av_dict_free(&options);
    
    // If MJPEG fails, try without specifying input format
    if (ret < 0) {
        qCWarning(log_ffmpeg_backend) << "MJPEG format failed, trying default format";
        
        // Reset format context
        if (m_formatContext) {
            avformat_close_input(&m_formatContext);
        }
        m_formatContext = avformat_alloc_context();
        
        // Try again without input_format specification
        AVDictionary* fallbackOptions = nullptr;
        av_dict_set(&fallbackOptions, "video_size", QString("%1x%2").arg(resolution.width()).arg(resolution.height()).toUtf8().constData(), 0);
        av_dict_set(&fallbackOptions, "framerate", QString::number(framerate).toUtf8().constData(), 0);
        
        ret = avformat_open_input(&m_formatContext, devicePath.toUtf8().constData(), inputFormat, &fallbackOptions);
        av_dict_free(&fallbackOptions);
    }
    
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
        qCCritical(log_ffmpeg_backend) << "Failed to open input device:" << QString::fromUtf8(errbuf);
        return false;
    }
    
    // Find stream info
    ret = avformat_find_stream_info(m_formatContext, nullptr);
    if (ret < 0) {
        qCCritical(log_ffmpeg_backend) << "Failed to find stream info";
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
        return false;
    }
    
    // Read packet from input
    int ret = av_read_frame(m_formatContext, m_packet);
    if (ret < 0) {
        if (ret == AVERROR(EAGAIN)) {
            return false; // Try again later
        }
        qCWarning(log_ffmpeg_backend) << "Error reading frame";
        return false;
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
    if (!m_packet || !m_codecContext) {
        return;
    }
    
    // Validate packet data before processing
    if (!m_packet->data || m_packet->size <= 0) {
        qCWarning(log_ffmpeg_backend) << "Invalid packet: data=" << (void*)m_packet->data 
                                     << "size=" << m_packet->size;
        av_packet_unref(m_packet);
        return;
    }
    
    qCDebug(log_ffmpeg_backend) << "Processing frame: packet size=" << m_packet->size 
                               << "codec=" << m_codecContext->codec_id;
    
    QPixmap pixmap;
    
    // Check if this is MJPEG/JPEG stream for direct libjpeg-turbo decoding
    if (m_codecContext->codec_id == AV_CODEC_ID_MJPEG) {
#ifdef HAVE_LIBJPEG_TURBO
        qCDebug(log_ffmpeg_backend) << "Using TurboJPEG acceleration for MJPEG frame, data size:" << m_packet->size;
        
        // Additional validation for JPEG data
        if (m_packet->size < 10) { // Minimum JPEG header size
            qCWarning(log_ffmpeg_backend) << "JPEG packet too small:" << m_packet->size << "bytes, falling back to FFmpeg decoder";
            pixmap = decodeFrame(m_packet);
        } else {
            pixmap = decodeJpegFrame(m_packet->data, m_packet->size);
            
            // If TurboJPEG failed, fall back to FFmpeg decoder
            if (pixmap.isNull()) {
                qCDebug(log_ffmpeg_backend) << "TurboJPEG failed, falling back to FFmpeg decoder";
                pixmap = decodeFrame(m_packet);
            }
        }
#else
        qCDebug(log_ffmpeg_backend) << "Using FFmpeg decoder for MJPEG frame (TurboJPEG not available)";
        pixmap = decodeFrame(m_packet);
#endif
    } else {
        qCDebug(log_ffmpeg_backend) << "Using FFmpeg decoder for non-MJPEG frame, codec:" << m_codecContext->codec_id;
        pixmap = decodeFrame(m_packet);
    }
    
    // Clean up packet
    av_packet_unref(m_packet);
    
    if (!pixmap.isNull()) {
        m_frameCount++;
        qCDebug(log_ffmpeg_backend) << "Emitting frame of size:" << pixmap.size() << "Frame count:" << m_frameCount;
        
        // Check if pixmap has actual content (not all black)
        QImage testImage = pixmap.toImage();
        bool hasContent = false;
        if (!testImage.isNull() && testImage.sizeInBytes() > 0) {
            const uchar* bits = testImage.constBits();
            for (int i = 0; i < testImage.sizeInBytes() && !hasContent; i++) {
                if (bits[i] != 0) {
                    hasContent = true;
                }
            }
        }
        
        if (hasContent) {
            qCDebug(log_ffmpeg_backend) << "Frame contains non-black pixels";
        } else {
            qCWarning(log_ffmpeg_backend) << "Frame appears to be all black!";
        }
        
        emit frameReady(pixmap);
    } else {
        qCWarning(log_ffmpeg_backend) << "Failed to decode frame - pixmap is null";
    }
}

#ifdef HAVE_LIBJPEG_TURBO
QPixmap FFmpegBackendHandler::decodeJpegFrame(const uint8_t* data, int size)
{
    if (!m_turboJpegHandle) {
        qCWarning(log_ffmpeg_backend) << "TurboJPEG handle not initialized, falling back to FFmpeg decoder";
        return QPixmap(); // Will trigger fallback to FFmpeg decoder
    }
    
    if (!data || size <= 0) {
        qCWarning(log_ffmpeg_backend) << "Invalid JPEG data: data=" << (void*)data << "size=" << size;
        return QPixmap();
    }
    
    // Additional validation: check for JPEG magic bytes
    if (size < 2 || data[0] != 0xFF || data[1] != 0xD8) {
        qCWarning(log_ffmpeg_backend) << "Invalid JPEG header: first bytes are" 
                                     << QString::number(data[0], 16) << QString::number(data[1], 16)
                                     << "expected FF D8";
        return QPixmap();
    }
    
    int width, height, subsamp, colorspace;
    
    // Get JPEG header information
    if (tjDecompressHeader3(m_turboJpegHandle, data, size, &width, &height, &subsamp, &colorspace) < 0) {
        qCWarning(log_ffmpeg_backend) << "TurboJPEG: Failed to read JPEG header:" << tjGetErrorStr()
                                     << "data size:" << size;
        return QPixmap();
    }
    
    qCDebug(log_ffmpeg_backend) << "TurboJPEG header: width=" << width << "height=" << height 
                               << "subsamp=" << subsamp << "colorspace=" << colorspace;
    
    // Validate dimensions
    if (width <= 0 || height <= 0 || width > 8192 || height > 8192) {
        qCWarning(log_ffmpeg_backend) << "Invalid JPEG dimensions:" << width << "x" << height;
        return QPixmap();
    }
    
    // Allocate buffer for RGB data
    QImage image(width, height, QImage::Format_RGB888);
    if (image.isNull()) {
        qCWarning(log_ffmpeg_backend) << "Failed to allocate QImage for" << width << "x" << height;
        return QPixmap();
    }
    
    // Decompress JPEG directly to RGB888 format for Qt
    if (tjDecompress2(m_turboJpegHandle, data, size, image.bits(), width, 0, height, TJPF_RGB, TJFLAG_FASTDCT) < 0) {
        qCWarning(log_ffmpeg_backend) << "TurboJPEG: Failed to decompress JPEG:" << tjGetErrorStr()
                                     << "dimensions:" << width << "x" << height;
        return QPixmap();
    }
    
    qCDebug(log_ffmpeg_backend) << "TurboJPEG: Successfully decoded" << width << "x" << height << "MJPEG frame";
    
    // Verify the image has valid data
    if (image.isNull()) {
        qCWarning(log_ffmpeg_backend) << "TurboJPEG: Decoded image is null";
        return QPixmap();
    }
    
    // Check if the image has actual pixel data (not all black)
    bool hasValidData = false;
    const uchar* bits = image.constBits();
    int totalBytes = image.sizeInBytes();
    for (int i = 0; i < totalBytes && !hasValidData; i++) {
        if (bits[i] != 0) {
            hasValidData = true;
        }
    }
    
    if (!hasValidData) {
        qCWarning(log_ffmpeg_backend) << "TurboJPEG: Decoded image appears to be all black!";
        // Don't return null - maybe it's actually a black frame from the source
    } else {
        qCDebug(log_ffmpeg_backend) << "TurboJPEG: Image contains valid pixel data";
    }
    
    QPixmap result = QPixmap::fromImage(image);
    qCDebug(log_ffmpeg_backend) << "TurboJPEG: Created pixmap of size:" << result.size();
    return result;
}
#endif // HAVE_LIBJPEG_TURBO

QPixmap FFmpegBackendHandler::decodeFrame(AVPacket* packet)
{
    if (!m_codecContext || !m_frame) {
        return QPixmap();
    }
    
    // Send packet to decoder
    int ret = avcodec_send_packet(m_codecContext, packet);
    if (ret < 0) {
        qCWarning(log_ffmpeg_backend) << "Error sending packet to decoder";
        return QPixmap();
    }
    
    // Receive frame from decoder
    ret = avcodec_receive_frame(m_codecContext, m_frame);
    if (ret < 0) {
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            return QPixmap(); // Not an error, just no frame ready
        }
        qCWarning(log_ffmpeg_backend) << "Error receiving frame from decoder";
        return QPixmap();
    }
    
    return convertFrameToPixmap(m_frame);
}

QPixmap FFmpegBackendHandler::convertFrameToPixmap(AVFrame* frame)
{
    if (!frame) {
        qCWarning(log_ffmpeg_backend) << "convertFrameToPixmap: frame is null";
        return QPixmap();
    }
    
    int width = frame->width;
    int height = frame->height;
    
    qCDebug(log_ffmpeg_backend) << "convertFrameToPixmap: frame" << width << "x" << height 
                               << "format:" << frame->format;
    
    // Initialize scaling context if needed
    if (!m_swsContext) {
        m_swsContext = sws_getContext(
            width, height, static_cast<AVPixelFormat>(frame->format),
            width, height, AV_PIX_FMT_RGB24,
            SWS_BILINEAR, nullptr, nullptr, nullptr
        );
        
        if (!m_swsContext) {
            qCWarning(log_ffmpeg_backend) << "Failed to create scaling context";
            return QPixmap();
        }
        qCDebug(log_ffmpeg_backend) << "Created scaling context for format:" << frame->format;
    }
    
    // Allocate buffer for RGB frame
    QImage image(width, height, QImage::Format_RGB888);
    
    // Set up frame data for RGB output
    uint8_t* rgbData[1] = { image.bits() };
    int rgbLinesize[1] = { static_cast<int>(image.bytesPerLine()) };
    
    // Convert frame to RGB
    int scaleResult = sws_scale(m_swsContext, frame->data, frame->linesize, 0, height, rgbData, rgbLinesize);
    if (scaleResult < 0) {
        qCWarning(log_ffmpeg_backend) << "sws_scale failed with result:" << scaleResult;
        return QPixmap();
    }
    
    qCDebug(log_ffmpeg_backend) << "sws_scale converted" << scaleResult << "lines";
    
    // Check if the converted image has actual data
    bool hasContent = false;
    const uchar* bits = image.constBits();
    int totalBytes = image.sizeInBytes();
    for (int i = 0; i < totalBytes && !hasContent; i++) {
        if (bits[i] != 0) {
            hasContent = true;
        }
    }
    
    if (hasContent) {
        qCDebug(log_ffmpeg_backend) << "convertFrameToPixmap: Converted image has content";
    } else {
        qCWarning(log_ffmpeg_backend) << "convertFrameToPixmap: Converted image appears to be all black!";
    }
    
    QPixmap result = QPixmap::fromImage(image);
    qCDebug(log_ffmpeg_backend) << "convertFrameToPixmap: Created pixmap of size:" << result.size();
    return result;
}

#endif // HAVE_FFMPEG

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
    
    m_videoPane = videoPane;
    m_graphicsVideoItem = nullptr;
    
    qCDebug(log_ffmpeg_backend) << "VideoPane set for FFmpeg direct rendering";
}
    


#include "ffmpegbackendhandler.moc"
