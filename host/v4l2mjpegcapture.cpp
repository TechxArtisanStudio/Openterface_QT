#include "v4l2mjpegcapture.h"
#include <QDebug>
#include <QFile>
#include <QElapsedTimer>

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

Q_LOGGING_CATEGORY(log_v4l2_mjpeg, "opf.v4l2_mjpeg")

V4L2MjpegCapture::V4L2MjpegCapture(QObject *parent)
    : QObject(parent),
      m_captureThread(nullptr),
      m_captureTimer(nullptr),
      m_running(false),
      m_shouldStop(false),
      m_v4l2Fd(-1),
      m_buffers(nullptr),
      m_bufferMaps(nullptr),
      m_bufferCount(0),
      m_codec(nullptr),
      m_codecCtx(nullptr),
      m_frame(nullptr),
      m_packet(nullptr),
      m_swsCtx(nullptr),
      m_resolution(1920, 1080),
      m_frameRate(30)
{
    qCDebug(log_v4l2_mjpeg) << "V4L2MjpegCapture created";
}

V4L2MjpegCapture::~V4L2MjpegCapture()
{
    stop();
    qCDebug(log_v4l2_mjpeg) << "V4L2MjpegCapture destroyed";
}

bool V4L2MjpegCapture::isRaspberryPi() const
{
    QFile f("/proc/cpuinfo");
    if (f.open(QIODevice::ReadOnly)) {
        if (f.readAll().contains("Raspberry Pi")) {
            return true;
        }
    }
    return false;
}

bool V4L2MjpegCapture::start(const QString& devicePath)
{
    QMutexLocker locker(&m_mutex);
    if (m_running) {
        m_lastError = "Capture is already running.";
        qCWarning(log_v4l2_mjpeg) << m_lastError;
        return false;
    }

    if (!isRaspberryPi()) {
        m_lastError = "Not running on a Raspberry Pi, V4L2 MJPEG capture with hardware acceleration is not supported.";
        qCWarning(log_v4l2_mjpeg) << m_lastError;
        return false;
    }

    m_devicePath = devicePath;
    m_shouldStop = false;

    m_captureThread = new QThread();
    V4L2CaptureWorker* worker = new V4L2CaptureWorker(this);
    worker->moveToThread(m_captureThread);

    connect(m_captureThread, &QThread::started, worker, &V4L2CaptureWorker::startCapture);
    connect(worker, &V4L2CaptureWorker::finished, m_captureThread, &QThread::quit);
    connect(worker, &V4L2CaptureWorker::finished, worker, &V4L2CaptureWorker::deleteLater);
    connect(m_captureThread, &QThread::finished, m_captureThread, &QThread::deleteLater);
    
    connect(worker, &V4L2CaptureWorker::frameReady, this, &V4L2MjpegCapture::frameReady);
    connect(worker, &V4L2CaptureWorker::errorOccurred, this, &V4L2MjpegCapture::errorOccurred);

    m_captureThread->start();
    m_running = true;
    emit started();
    qCDebug(log_v4l2_mjpeg) << "Capture thread started.";
    return true;
}

void V4L2MjpegCapture::stop()
{
    QMutexLocker locker(&m_mutex);
    if (!m_running) {
        return;
    }

    m_shouldStop = true;
    m_waitCondition.wakeAll();

    if (m_captureThread && m_captureThread->isRunning()) {
        m_captureThread->quit();
        m_captureThread->wait(5000); // Wait for 5 seconds
    }
    m_captureThread = nullptr;
    m_running = false;
    emit stopped();
    qCDebug(log_v4l2_mjpeg) << "Capture stopped.";
}

bool V4L2MjpegCapture::isRunning() const
{
    QMutexLocker locker(&m_mutex);
    return m_running;
}

void V4L2MjpegCapture::setResolution(int width, int height)
{
    QMutexLocker locker(&m_mutex);
    m_resolution.setWidth(width);
    m_resolution.setHeight(height);
}

void V4L2MjpegCapture::setFrameRate(int fps)
{
    QMutexLocker locker(&m_mutex);
    m_frameRate = fps;
}

QSize V4L2MjpegCapture::getResolution() const
{
    QMutexLocker locker(&m_mutex);
    return m_resolution;
}

int V4L2MjpegCapture::getFrameRate() const
{
    QMutexLocker locker(&m_mutex);
    return m_frameRate;
}

QString V4L2MjpegCapture::getLastError() const
{
    QMutexLocker locker(&m_mutex);
    return m_lastError;
}

void V4L2MjpegCapture::captureLoop()
{
    if (!initializeV4L2Device(m_devicePath)) {
        emit errorOccurred(m_lastError);
        return;
    }

    if (!initializeFFmpegDecoder()) {
        cleanupV4L2Device();
        emit errorOccurred(m_lastError);
        return;
    }

    while (!m_shouldStop) {
        QByteArray mjpegData;
        if (!readMJPEGFrame(mjpegData)) {
            if (m_shouldStop) break;
            m_lastError = "Failed to read MJPEG frame.";
            qCWarning(log_v4l2_mjpeg) << m_lastError;
            emit errorOccurred(m_lastError);
            break;
        }

        QImage decodedImage;
        if (decodeFrame(mjpegData, decodedImage)) {
            emit frameReady(decodedImage);
        } else {
             if (m_shouldStop) break;
            qCWarning(log_v4l2_mjpeg) << "Failed to decode frame.";
        }
    }

    cleanupFFmpegDecoder();
    cleanupV4L2Device();
}

bool V4L2MjpegCapture::initializeV4L2Device(const QString& devicePath)
{
    m_v4l2Fd = open(devicePath.toStdString().c_str(), O_RDWR);
    if (m_v4l2Fd < 0) {
        m_lastError = "Failed to open V4L2 device: " + devicePath;
        qCritical(log_v4l2_mjpeg) << m_lastError;
        return false;
    }

    if (!configureV4L2Format()) {
        close(m_v4l2Fd);
        m_v4l2Fd = -1;
        return false;
    }

    if (!startV4L2Streaming()) {
        close(m_v4l2Fd);
        m_v4l2Fd = -1;
        return false;
    }
    
    qCDebug(log_v4l2_mjpeg) << "V4L2 device initialized successfully.";
    return true;
}

void V4L2MjpegCapture::cleanupV4L2Device()
{
    if (m_v4l2Fd >= 0) {
        stopV4L2Streaming();
        close(m_v4l2Fd);
        m_v4l2Fd = -1;
        qCDebug(log_v4l2_mjpeg) << "V4L2 device cleaned up.";
    }
}

bool V4L2MjpegCapture::configureV4L2Format()
{
    memset(&m_format, 0, sizeof(m_format));
    m_format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    m_format.fmt.pix.width = m_resolution.width();
    m_format.fmt.pix.height = m_resolution.height();
    m_format.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
    m_format.fmt.pix.field = V4L2_FIELD_ANY;

    if (ioctl(m_v4l2Fd, VIDIOC_S_FMT, &m_format) < 0) {
        m_lastError = "Failed to set V4L2 format.";
        qCritical(log_v4l2_mjpeg) << m_lastError;
        return false;
    }

    struct v4l2_streamparm streamparm;
    memset(&streamparm, 0, sizeof(streamparm));
    streamparm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    streamparm.parm.capture.timeperframe.numerator = 1;
    streamparm.parm.capture.timeperframe.denominator = m_frameRate;
    if (ioctl(m_v4l2Fd, VIDIOC_S_PARM, &streamparm) < 0) {
        m_lastError = "Failed to set frame rate.";
        qWarning(log_v4l2_mjpeg) << m_lastError;
        // Continue even if framerate setting fails
    }

    qCDebug(log_v4l2_mjpeg) << "V4L2 format configured to" << m_resolution.width() << "x" << m_resolution.height() << "@" << m_frameRate << "fps";
    return true;
}

bool V4L2MjpegCapture::startV4L2Streaming()
{
    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count = 4;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    if (ioctl(m_v4l2Fd, VIDIOC_REQBUFS, &req) < 0) {
        m_lastError = "Failed to request V4L2 buffers.";
        qCritical(log_v4l2_mjpeg) << m_lastError;
        return false;
    }
    m_bufferCount = req.count;

    m_buffers = (struct v4l2_buffer*)calloc(m_bufferCount, sizeof(*m_buffers));
    m_bufferMaps = (void**)calloc(m_bufferCount, sizeof(void*));

    for (unsigned int i = 0; i < m_bufferCount; ++i) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (ioctl(m_v4l2Fd, VIDIOC_QUERYBUF, &buf) < 0) {
            m_lastError = "Failed to query V4L2 buffer.";
            qCritical(log_v4l2_mjpeg) << m_lastError;
            return false;
        }

        m_buffers[i] = buf;
        m_bufferMaps[i] = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, m_v4l2Fd, buf.m.offset);

        if (m_bufferMaps[i] == MAP_FAILED) {
            m_lastError = "Failed to mmap V4L2 buffer.";
            qCritical(log_v4l2_mjpeg) << m_lastError;
            return false;
        }
    }

    for (unsigned int i = 0; i < m_bufferCount; ++i) {
        if (ioctl(m_v4l2Fd, VIDIOC_QBUF, &m_buffers[i]) < 0) {
            m_lastError = "Failed to queue V4L2 buffer.";
            qCritical(log_v4l2_mjpeg) << m_lastError;
            return false;
        }
    }

    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(m_v4l2Fd, VIDIOC_STREAMON, &type) < 0) {
        m_lastError = "Failed to start V4L2 streaming.";
        qCritical(log_v4l2_mjpeg) << m_lastError;
        return false;
    }

    qCDebug(log_v4l2_mjpeg) << "V4L2 streaming started.";
    return true;
}

void V4L2MjpegCapture::stopV4L2Streaming()
{
    if (m_v4l2Fd < 0) return;

    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(m_v4l2Fd, VIDIOC_STREAMOFF, &type);

    if (m_bufferMaps) {
        for (unsigned int i = 0; i < m_bufferCount; ++i) {
            if (m_bufferMaps[i] != MAP_FAILED) {
                munmap(m_bufferMaps[i], m_buffers[i].length);
            }
        }
        free(m_bufferMaps);
        m_bufferMaps = nullptr;
    }

    if (m_buffers) {
        free(m_buffers);
        m_buffers = nullptr;
    }
    qCDebug(log_v4l2_mjpeg) << "V4L2 streaming stopped.";
}

bool V4L2MjpegCapture::readMJPEGFrame(QByteArray& frameData)
{
    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    if (ioctl(m_v4l2Fd, VIDIOC_DQBUF, &buf) < 0) {
        m_lastError = "Failed to dequeue V4L2 buffer.";
        return false;
    }

    frameData = QByteArray((char*)m_bufferMaps[buf.index], buf.bytesused);

    if (ioctl(m_v4l2Fd, VIDIOC_QBUF, &buf) < 0) {
        m_lastError = "Failed to re-queue V4L2 buffer.";
        // Not fatal, but log it
        qCWarning(log_v4l2_mjpeg) << m_lastError;
    }

    return true;
}

bool V4L2MjpegCapture::initializeFFmpegDecoder()
{
    m_codec = avcodec_find_decoder_by_name("mjpeg_v4l2m2m");
    if (!m_codec) {
        m_lastError = "Hardware accelerated MJPEG decoder (mjpeg_v4l2m2m) not found. Falling back to software decoder.";
        qCWarning(log_v4l2_mjpeg) << m_lastError;
        m_codec = avcodec_find_decoder(AV_CODEC_ID_MJPEG);
        if (!m_codec) {
            m_lastError = "MJPEG decoder not found.";
            qCritical(log_v4l2_mjpeg) << m_lastError;
            return false;
        }
    }

    m_codecCtx = avcodec_alloc_context3(m_codec);
    if (!m_codecCtx) {
        m_lastError = "Failed to allocate AVCodecContext.";
        qCritical(log_v4l2_mjpeg) << m_lastError;
        return false;
    }

    if (avcodec_open2(m_codecCtx, m_codec, nullptr) < 0) {
        m_lastError = "Failed to open codec.";
        qCritical(log_v4l2_mjpeg) << m_lastError;
        return false;
    }

    m_frame = av_frame_alloc();
    m_packet = av_packet_alloc();
    if (!m_frame || !m_packet) {
        m_lastError = "Failed to allocate AVFrame or AVPacket.";
        qCritical(log_v4l2_mjpeg) << m_lastError;
        return false;
    }

    qCDebug(log_v4l2_mjpeg) << "FFmpeg decoder initialized with codec:" << m_codec->name;
    return true;
}

void V4L2MjpegCapture::cleanupFFmpegDecoder()
{
    if (m_swsCtx) {
        sws_freeContext(m_swsCtx);
        m_swsCtx = nullptr;
    }
    if (m_codecCtx) {
        avcodec_free_context(&m_codecCtx);
        m_codecCtx = nullptr;
    }
    if (m_frame) {
        av_frame_free(&m_frame);
        m_frame = nullptr;
    }
    if (m_packet) {
        av_packet_free(&m_packet);
        m_packet = nullptr;
    }
    qCDebug(log_v4l2_mjpeg) << "FFmpeg decoder cleaned up.";
}

bool V4L2MjpegCapture::decodeFrame(const QByteArray& mjpegData, QImage& outputImage)
{
    m_packet->data = (uint8_t*)mjpegData.data();
    m_packet->size = mjpegData.size();

    if (avcodec_send_packet(m_codecCtx, m_packet) < 0) {
        m_lastError = "Error sending packet to decoder.";
        qCWarning(log_v4l2_mjpeg) << m_lastError;
        return false;
    }

    int ret = avcodec_receive_frame(m_codecCtx, m_frame);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
        return false; // Need more data
    } else if (ret < 0) {
        m_lastError = "Error receiving frame from decoder.";
        qCWarning(log_v4l2_mjpeg) << m_lastError;
        return false;
    }

    outputImage = convertAVFrameToQImage(m_frame);
    return !outputImage.isNull();
}

QImage V4L2MjpegCapture::convertAVFrameToQImage(AVFrame* frame)
{
    AVPixelFormat dstFormat = AV_PIX_FMT_RGB24;
    m_swsCtx = sws_getCachedContext(m_swsCtx,
                                  frame->width, frame->height, (AVPixelFormat)frame->format,
                                  frame->width, frame->height, dstFormat,
                                  SWS_BILINEAR, NULL, NULL, NULL);
    if (!m_swsCtx) {
        m_lastError = "Failed to get SwsContext.";
        qCWarning(log_v4l2_mjpeg) << m_lastError;
        return QImage();
    }

    QImage image(frame->width, frame->height, QImage::Format_RGB888);
    uint8_t* dstData[1] = { image.bits() };
    int dstLinesize[1] = { image.bytesPerLine() };

    sws_scale(m_swsCtx, (const uint8_t* const*)frame->data, frame->linesize, 0, frame->height,
              dstData, dstLinesize);

    return image;
}

// --- V4L2CaptureWorker Implementation ---

V4L2CaptureWorker::V4L2CaptureWorker(V4L2MjpegCapture* capture, QObject* parent)
    : QObject(parent), m_capture(capture), m_shouldStop(false)
{
}

void V4L2CaptureWorker::startCapture()
{
    if (!m_capture) return;
    
    connect(this, &V4L2CaptureWorker::frameReady, m_capture, &V4L2MjpegCapture::frameReady, Qt::QueuedConnection);
    connect(this, &V4L2CaptureWorker::errorOccurred, m_capture, &V4L2MjpegCapture::errorOccurred, Qt::QueuedConnection);

    m_capture->captureLoop();
    
    emit finished();
}

void V4L2CaptureWorker::stopCapture()
{
    m_shouldStop = true;
}
