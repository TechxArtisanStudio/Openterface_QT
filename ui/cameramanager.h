#ifndef CAMERAMANAGER_H
#define CAMERAMANAGER_H

#include <QObject>
#include <QCamera>
#include <QMediaCaptureSession>
#include <QImageCapture>
#include <QMediaRecorder>
#include <QVideoWidget>  // Add this include

class CameraManager : public QObject
{
    Q_OBJECT

public:
    explicit CameraManager(QObject *parent = nullptr);
    ~CameraManager();

    void setCamera(const QCameraDevice &cameraDevice);
    void startCamera();
    void stopCamera();
    void takeImage();
    void startRecording();
    void stopRecording();
    QCamera* getCamera() const { return m_camera.get(); }
    void setVideoOutput(QVideoWidget* videoOutput);  // Add this method

signals:
    void cameraActiveChanged(bool active);
    void imageCaptured(int requestId, const QImage &img);
    void recordingStarted();
    void recordingStopped();
    void cameraError(const QString &errorString);

private:
    std::unique_ptr<QCamera> m_camera;
    QMediaCaptureSession m_captureSession;
    std::unique_ptr<QImageCapture> m_imageCapture;
    std::unique_ptr<QMediaRecorder> m_mediaRecorder;

    void setupConnections();
};

#endif // CAMERAMANAGER_H