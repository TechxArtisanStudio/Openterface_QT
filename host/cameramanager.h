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

    void setCamera(const QCameraDevice &cameraDevice, QVideoWidget* videoOutput);
    void setCameraDevice(const QCameraDevice &cameraDevice);
    void startCamera();
    void stopCamera();
    void takeImage();
    void startRecording();
    void stopRecording();
    QCamera* getCamera() const { return m_camera.get(); }
    void setVideoOutput(QVideoWidget* videoOutput);
    void setCameraFormat(const QCameraFormat &format);
    QCameraFormat getCameraFormat() const;
    QList<QCameraFormat> getCameraFormats() const;
    void loadCameraSettingAndSetCamera();
    void queryResolutions();
    void updateResolutions(int input_width, int input_height, float input_fps, int capture_width, int capture_height, int capture_fps);

signals:
    void cameraActiveChanged(bool active);
    void cameraSettingsApplied();
    void imageCaptured(int requestId, const QImage &img);
    void recordingStarted();
    void recordingStopped();
    void cameraError(const QString &errorString);
    void resolutionsUpdated(int input_width, int input_height, float input_fps, int capture_width, int capture_height, int capture_fps);

private:
    std::unique_ptr<QCamera> m_camera;
    QMediaCaptureSession m_captureSession;
    std::unique_ptr<QImageCapture> m_imageCapture;
    std::unique_ptr<QMediaRecorder> m_mediaRecorder;
    QVideoWidget* m_videoOutput;
    int m_video_width;
    int m_video_height;

    void setupConnections();
};

#endif // CAMERAMANAGER_H
