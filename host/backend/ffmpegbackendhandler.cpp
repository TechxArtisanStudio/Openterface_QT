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

// FFmpeg includes
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
#include <libavdevice/avdevice.h>
}

// libjpeg-turbo includes
#include <jpeglib.h>
#include <turbojpeg.h>

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
                
                // Signal main thread to process the frame
                QMetaObject::invokeMethod(m_handler, "processFrame", Qt::QueuedConnection);
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

FFmpegBackendHandler::FFmpegBackendHandler(QObject *parent)
    : MultimediaBackendHandler(parent),
      m_formatContext(nullptr),
      m_codecContext(nullptr),
      m_frame(nullptr),
      m_frameRGB(nullptr),
      m_packet(nullptr),
      m_swsContext(nullptr),
      m_jpegDecompressor(nullptr),
      m_jpegErrorManager(nullptr),
      m_captureRunning(false),
      m_videoStreamIndex(-1),
      m_graphicsVideoItem(nullptr),
      m_videoPane(nullptr),
      m_frameCount(0),
      m_lastFrameTime(0)
{
    m_config = getDefaultConfig();
    
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
}

FFmpegBackendHandler::~FFmpegBackendHandler()
{
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
}

void FFmpegBackendHandler::stopCamera(QCamera* camera)
{
    qCDebug(log_ffmpeg_backend) << "FFmpeg: Stopping camera";
    
    // Stop direct capture
    stopDirectCapture();
    
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
    
    // Initialize libjpeg-turbo
    m_jpegErrorManager = new jpeg_error_mgr;
    m_jpegDecompressor = new jpeg_decompress_struct;
    
    m_jpegDecompressor->err = jpeg_std_error(m_jpegErrorManager);
    jpeg_create_decompress(m_jpegDecompressor);
    
    qCDebug(log_ffmpeg_backend) << "FFmpeg and libjpeg-turbo initialized successfully";
    return true;
}

void FFmpegBackendHandler::cleanupFFmpeg()
{
    qCDebug(log_ffmpeg_backend) << "Cleaning up FFmpeg";
    
    closeInputDevice();
    
    // Cleanup libjpeg-turbo
    if (m_jpegDecompressor) {
        jpeg_destroy_decompress(m_jpegDecompressor);
        delete m_jpegDecompressor;
        m_jpegDecompressor = nullptr;
    }
    
    if (m_jpegErrorManager) {
        delete m_jpegErrorManager;
        m_jpegErrorManager = nullptr;
    }
    
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
    
    // Open input
    int ret = avformat_open_input(&m_formatContext, devicePath.toUtf8().constData(), inputFormat, &options);
    av_dict_free(&options);
    
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
                                << "resolution=" << codecpar->width << "x" << codecpar->height;
    
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
    
    QPixmap pixmap;
    
    // Check if this is MJPEG/JPEG stream for direct libjpeg-turbo decoding
    if (m_codecContext->codec_id == AV_CODEC_ID_MJPEG) {
        pixmap = decodeJpegFrame(m_packet->data, m_packet->size);
    } else {
        pixmap = decodeFrame(m_packet);
    }
    
    // Clean up packet
    av_packet_unref(m_packet);
    
    if (!pixmap.isNull()) {
        m_frameCount++;
        emit frameReady(pixmap);
    }
}

QPixmap FFmpegBackendHandler::decodeJpegFrame(const uint8_t* data, int size)
{
    if (!m_jpegDecompressor || !data || size <= 0) {
        return QPixmap();
    }
    
    try {
        // Set up libjpeg-turbo for this frame
        jpeg_mem_src(m_jpegDecompressor, data, size);
        
        // Read JPEG header
        if (jpeg_read_header(m_jpegDecompressor, TRUE) != JPEG_HEADER_OK) {
            qCWarning(log_ffmpeg_backend) << "Failed to read JPEG header";
            return QPixmap();
        }
        
        // Start decompression
        if (!jpeg_start_decompress(m_jpegDecompressor)) {
            qCWarning(log_ffmpeg_backend) << "Failed to start JPEG decompression";
            return QPixmap();
        }
        
        // Get image dimensions
        int width = m_jpegDecompressor->output_width;
        int height = m_jpegDecompressor->output_height;
        int channels = m_jpegDecompressor->output_components;
        
        // Allocate buffer for RGB data
        QImage image(width, height, QImage::Format_RGB888);
        
        // Read scanlines
        while (m_jpegDecompressor->output_scanline < height) {
            unsigned char* row = image.scanLine(m_jpegDecompressor->output_scanline);
            jpeg_read_scanlines(m_jpegDecompressor, &row, 1);
        }
        
        // Finish decompression
        jpeg_finish_decompress(m_jpegDecompressor);
        
        return QPixmap::fromImage(image);
        
    } catch (...) {
        qCWarning(log_ffmpeg_backend) << "Exception during JPEG decoding";
        return QPixmap();
    }
}

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
        return QPixmap();
    }
    
    int width = frame->width;
    int height = frame->height;
    
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
    }
    
    // Allocate buffer for RGB frame
    QImage image(width, height, QImage::Format_RGB888);
    
    // Set up frame data for RGB output
    uint8_t* rgbData[1] = { image.bits() };
    int rgbLinesize[1] = { static_cast<int>(image.bytesPerLine()) };
    
    // Convert frame to RGB
    sws_scale(m_swsContext, frame->data, frame->linesize, 0, height, rgbData, rgbLinesize);
    
    return QPixmap::fromImage(image);
}

void FFmpegBackendHandler::setVideoOutput(QGraphicsVideoItem* videoItem)
{
    QMutexLocker locker(&m_mutex);
    m_graphicsVideoItem = videoItem;
    m_videoPane = nullptr;
    
    if (videoItem) {
        qCDebug(log_ffmpeg_backend) << "Graphics video item set for FFmpeg direct rendering";
        
        // Connect frame ready signal to update graphics item
        connect(this, &FFmpegBackendHandler::frameReady, this, [this](const QPixmap& frame) {
            if (m_graphicsVideoItem && m_graphicsVideoItem->scene()) {
                // Create or update pixmap item for display
                static QGraphicsPixmapItem* pixmapItem = nullptr;
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
    m_videoPane = videoPane;
    m_graphicsVideoItem = nullptr;
    
    if (videoPane) {
        qCDebug(log_ffmpeg_backend) << "VideoPane set for FFmpeg direct rendering";
        
        // Connect frame ready signal to update VideoPane
        connect(this, &FFmpegBackendHandler::frameReady, this, [this](const QPixmap& frame) {
            if (m_videoPane) {
                // Update VideoPane with new frame
                m_videoPane->updateVideoFrame(frame);
            }
        }, Qt::QueuedConnection);
    }
}

#include "ffmpegbackendhandler.moc"
