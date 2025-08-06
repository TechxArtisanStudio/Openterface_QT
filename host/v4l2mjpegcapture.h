#ifndef V4L2MJPEGCAPTURE_H
#define V4L2MJPEGCAPTURE_H

#include <QObject>
#include <QThread>
#include <QImage>
#include <QLoggingCategory>
#include <QMutex>
#include <QWaitCondition>
#include <QSize>

// Forward declaration for FFmpeg structs
struct AVCodec;
struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct SwsContext;

// Forward declaration for V4L2 structs
struct v4l2_buffer;

Q_DECLARE_LOGGING_CATEGORY(log_v4l2_mjpeg)

class V4L2CaptureWorker;

/**
 * @brief V4L2 MJPEG capture class for Raspberry Pi hardware acceleration
 *
 * This class captures MJPEG from /dev/video0 using V4L2 and decodes it
 * using the hardware-accelerated mjpeg_v4l2m2m decoder on Raspberry Pi.
 */
class V4L2MjpegCapture : public QObject
{
    Q_OBJECT
    friend class V4L2CaptureWorker;

public:
    explicit V4L2MjpegCapture(QObject *parent = nullptr);
    ~V4L2MjpegCapture();

    bool start(const QString& devicePath = "/dev/video0");
    void stop();
    bool isRunning() const;

    void setResolution(int width, int height);
    void setFrameRate(int fps);

    QSize getResolution() const;
    int getFrameRate() const;
    QString getLastError() const;
    static bool isRaspberryPi();

signals:
    void frameReady(const QImage& frame);
    void errorOccurred(const QString& error);
    void started();
    void stopped();

private:
    void captureLoop();
    bool initializeV4L2Device(const QString& devicePath);
    void cleanupV4L2Device();
    bool configureV4L2Format();
    bool startV4L2Streaming();
    void stopV4L2Streaming();
    bool readMJPEGFrame(QByteArray& frameData);

    bool initializeFFmpegDecoder();
    void cleanupFFmpegDecoder();
    bool decodeFrame(const QByteArray& mjpegData, QImage& outputImage);
    QImage convertAVFrameToQImage(AVFrame* frame);

    QThread* m_captureThread;
    V4L2CaptureWorker* m_worker;
    
    volatile bool m_running;
    volatile bool m_shouldStop;

    int m_v4l2Fd;
    QString m_devicePath;
    struct v4l2_buffer* m_buffers;
    void** m_bufferMaps;
    unsigned int m_bufferCount;
    struct v4l2_format m_format;

    const AVCodec* m_codec;
    AVCodecContext* m_codecCtx;
    AVFrame* m_frame;
    AVPacket* m_packet;
    SwsContext* m_swsCtx;

    QSize m_resolution;
    int m_frameRate;
    QString m_lastError;

    mutable QMutex m_mutex;
    QWaitCondition m_waitCondition;
};

class V4L2CaptureWorker : public QObject
{
    Q_OBJECT
public:
    V4L2CaptureWorker(V4L2MjpegCapture* capture, QObject* parent = nullptr);
public slots:
    void startCapture();
    void stopCapture();
signals:
    void frameReady(const QImage& frame);
    void errorOccurred(const QString& error);
    void finished();
private:
    V4L2MjpegCapture* m_capture;
    volatile bool m_shouldStop;
};

#endif // V4L2MJPEGCAPTURE_H
