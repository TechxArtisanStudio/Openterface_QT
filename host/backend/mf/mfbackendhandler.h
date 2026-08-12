#ifndef MFBACKENDHANDLER_H
#define MFBACKENDHANDLER_H

#include "host/multimediabackend.h"
#include <QMediaCaptureSession>
#include <QGraphicsVideoItem>
#include <QCameraFormat>
#include <QSize>
#include <QMetaObject>
#include <memory>

// Forward declarations
class MfCaptureManager;
class MfDeviceEnumerator;
class VideoPane;

class MfBackendHandler : public MultimediaBackendHandler
{
    Q_OBJECT

public:
    explicit MfBackendHandler(QObject* parent = nullptr);
    ~MfBackendHandler() override;

    MultimediaBackendType getBackendType() const override { return MultimediaBackendType::MediaFoundation; }
    QString getBackendName() const override { return "Media Foundation"; }
    bool isBackendAvailable() const override;

    void startCamera() override;
    void stopCamera() override;
    void cleanupCamera() override;

    bool startRecording(const QString& outputPath, const QString& format = "mp4", int videoBitrate = 2000000) override;
    bool stopRecording() override;
    bool isRecording() const override;

    QStringList getAvailableHardwareAccelerations() const override;

    void setVideoOutput(QGraphicsVideoItem* videoOutput);
    void setVideoOutput(VideoPane* videoPane);
    void setDevicePath(const QString& devicePath);
    void setResolution(const QSize& resolution);
    void setFramerate(int framerate);

signals:
    void frameReadyImage(const QImage& frame);

private slots:
    void onFrameReady(const QImage& frame);
    void onCaptureError(const QString& error);
    void onDeviceDisconnected();

private:
    bool setupCaptureSession();
    void connectToVideoOutput();

    MfCaptureManager* captureManager_;
    MfDeviceEnumerator* deviceEnumerator_;
    QGraphicsVideoItem* graphicsVideoItem_;
    VideoPane* videoPane_;
    QMetaObject::Connection m_videoOutputConnection;
    QString devicePath_;
    QString deviceSymbolicLink_;
    QSize resolution_;
    int framerate_;
    bool isRecording_;
};

#endif // MFBACKENDHANDLER_H
