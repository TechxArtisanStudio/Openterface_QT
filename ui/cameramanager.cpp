#include "cameramanager.h"

#include <QLoggingCategory>

Q_LOGGING_CATEGORY(log_ui_camera, "opf.ui.camera")

CameraManager::CameraManager(QObject *parent)
    : QObject(parent)
{
    m_imageCapture = std::make_unique<QImageCapture>();
    m_mediaRecorder = std::make_unique<QMediaRecorder>();
    setupConnections();
}

CameraManager::~CameraManager() = default;

void CameraManager::setCamera(const QCameraDevice &cameraDevice)
{
    qCDebug(log_ui_camera) << "Set Camera, device name: " << cameraDevice.description();
    m_camera.reset(new QCamera(cameraDevice));
    m_captureSession.setCamera(m_camera.get());
}

void CameraManager::setVideoOutput(QVideoWidget* videoOutput)
{
    if (videoOutput) {
        qCDebug(log_ui_camera) << "Setting video output";
        m_captureSession.setVideoOutput(videoOutput);
    } else {
        qCWarning(log_ui_camera) << "Attempted to set null video output";
    }
}

void CameraManager::startCamera()
{
    qCDebug(log_ui_camera) << "Camera start..";
    if (m_camera) {
        m_camera->start();
    }
}

void CameraManager::stopCamera()
{
    if (m_camera) {
        m_camera->stop();
    }
}

void CameraManager::takeImage()
{
    if (m_imageCapture) {
        m_imageCapture->capture();
    }
}

void CameraManager::startRecording()
{
    if (m_mediaRecorder) {
        m_mediaRecorder->record();
    }
}

void CameraManager::stopRecording()
{
    if (m_mediaRecorder) {
        m_mediaRecorder->stop();
    }
}

void CameraManager::setupConnections()
{
    connect(m_camera.get(), &QCamera::activeChanged, this, &CameraManager::cameraActiveChanged);
    connect(m_camera.get(), &QCamera::errorOccurred, this, [this](QCamera::Error error, const QString &errorString) {
        Q_UNUSED(error);
        emit cameraError(errorString);
    });

    // Update this connection
    connect(m_imageCapture.get(), &QImageCapture::imageCaptured, this, &CameraManager::imageCaptured);

    connect(m_mediaRecorder.get(), &QMediaRecorder::recorderStateChanged, this, [this](QMediaRecorder::RecorderState state) {
        if (state == QMediaRecorder::RecordingState) {
            emit recordingStarted();
        } else if (state == QMediaRecorder::StoppedState) {
            emit recordingStopped();
        }
    });
}
