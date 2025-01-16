#ifndef CAMERAMANAGER_H
#define CAMERAMANAGER_H

#include <QObject>
#include <QCamera>
#include <QMediaCaptureSession>
#include <QImageCapture>
#include <QMediaRecorder>
#include <QVideoWidget>  // Add this include
#include <QDir>
#include <QImageCapture>
#include <QStandardPaths>
#include <QRect>

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
    void takeImage(const QString& file);
    void takeAreaImage(const QString& file, const QRect& captureArea);
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
    void recordingStarted();
    void recordingStopped();
    void cameraError(const QString &errorString);
    void resolutionsUpdated(int input_width, int input_height, float input_fps, int capture_width, int capture_height, int capture_fps);
    void imageCaptured(int id, const QImage& img);
    
private slots:
    void onImageCaptured(int id, const QImage& img);
    
private:
    std::unique_ptr<QCamera> m_camera;
    QMediaCaptureSession m_captureSession;
    std::unique_ptr<QImageCapture> m_imageCapture;
    std::unique_ptr<QMediaRecorder> m_mediaRecorder;
    QVideoWidget* m_videoOutput;
    int m_video_width;
    int m_video_height;
    QString filePath;
    void setupConnections();
    QRect copyRect;
};

#endif // CAMERAMANAGER_H
