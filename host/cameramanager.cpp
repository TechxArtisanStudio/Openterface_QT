#include "cameramanager.h"

#include <QLoggingCategory>
#include <QSettings>
#include <QMediaDevices>
#include "global.h"
#include "video/videohid.h"
#include <QVideoWidget>



Q_LOGGING_CATEGORY(log_ui_camera, "opf.ui.camera")

CameraManager::CameraManager(QObject *parent)
    : QObject(parent)
{
    qDebug() << "CameraManager init...";
    m_imageCapture = std::make_unique<QImageCapture>();
    m_mediaRecorder = std::make_unique<QMediaRecorder>();
    connect(m_imageCapture.get(), &QImageCapture::imageCaptured, this, &CameraManager::onImageCaptured);

}

CameraManager::~CameraManager() = default;

void CameraManager::setCamera(const QCameraDevice &cameraDevice, QVideoWidget* videoOutput)
{
    qCDebug(log_ui_camera) << "Set Camera to videoOutput: " << videoOutput << ", device name: " << cameraDevice.description();
    setCameraDevice(cameraDevice);

    setVideoOutput(videoOutput);

    queryResolutions();

    startCamera();

    VideoHid::getInstance().start();
}



void CameraManager::setCameraDevice(const QCameraDevice &cameraDevice)
{
    m_camera.reset(new QCamera(cameraDevice));
    setupConnections();
    m_captureSession.setCamera(m_camera.get());
    m_captureSession.setImageCapture(m_imageCapture.get());
}

void CameraManager::setVideoOutput(QVideoWidget* videoOutput)
{
    if (videoOutput) {
        m_videoOutput = videoOutput;
        qCDebug(log_ui_camera) << "Setting video output to: " << videoOutput->objectName();
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
        qCDebug(log_ui_camera) << "Camera started";
    } else {
        qCWarning(log_ui_camera) << "Camera is null, cannot start";
    }
}

void CameraManager::stopCamera()
{
    VideoHid::getInstance().stop();

    if (m_camera) {
        m_camera->stop();
        qCDebug(log_ui_camera) << "Camera stopped";
    } else {
        qCWarning(log_ui_camera) << "Camera is null, cannot stop";
    }
}

void CameraManager::onImageCaptured(int id, const QImage& img){
    Q_UNUSED(id);
    
    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QString picturesPath = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    QString customFolderPath;
    if (picturesPath.isEmpty()) {
        picturesPath = QDir::currentPath();
    }
    if(filePath==""){
        customFolderPath = picturesPath + "/" + "openterfaceCaptureImg";
    }else{
        customFolderPath = filePath + "/";
        customFolderPath = customFolderPath.trimmed();
    }
    
    QDir dir(customFolderPath);
    if (!dir.exists() && filePath=="") {
        qCDebug(log_ui_camera) << "Directory do not exist";
        if (!dir.mkpath(".")) {
            qCDebug(log_ui_camera) << "Failed to create directory: " << customFolderPath;
            return;
        }
    }
    
    QString saveName = customFolderPath + "/" + timestamp + ".png";

    QImage coayImage = img.copy(copyRect);
    if(coayImage.save(saveName)){
        qCDebug(log_ui_camera) << "succefully save img to : " << saveName;
    }else{
        qCDebug(log_ui_camera) << "fail save img to : " << saveName;
    }
    copyRect = QRect(0, 0, m_video_width, m_video_height);
}

void CameraManager::takeImage(const QString& file)
{
    if (m_imageCapture && m_camera && m_camera->isActive()) {
        if (m_imageCapture->isReadyForCapture()) {
            filePath = file;
            m_imageCapture->capture();
            qCDebug(log_ui_camera) << "captured .....................";
        } else {
            qCWarning(log_ui_camera) << "Image capture is not ready";
        }
    } else {
        qCWarning(log_ui_camera) << "Camera or image capture is not ready";
    }
}

void CameraManager::takeAreaImage(const QString& file, const QRect& captureArea){
    if (m_imageCapture && m_camera && m_camera->isActive()) {
        if (m_imageCapture->isReadyForCapture()) {
            filePath = file;
            copyRect = captureArea;
            m_imageCapture->capture();
            qCDebug(log_ui_camera) << "captured .....................";
        } else {
            qCWarning(log_ui_camera) << "Image capture is not ready";
        }
    } else {
        qCWarning(log_ui_camera) << "Camera or image capture is not ready";
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
    if (m_camera) {
        connect(m_camera.get(), &QCamera::activeChanged, this, [this](bool active) {
            qCDebug(log_ui_camera) << "Camera active state changed to:" << active;
            emit cameraActiveChanged(active);
        });
        connect(m_camera.get(), &QCamera::errorOccurred, this, [this](QCamera::Error error, const QString &errorString) {
            Q_UNUSED(error);
            emit cameraError(errorString);
        });
        qCDebug(log_ui_camera) << "Camera connections set up";
    } else {
        qCWarning(log_ui_camera) << "Camera is null, cannot set up connections";
    }

    if (m_imageCapture) {
        connect(m_imageCapture.get(), &QImageCapture::imageCaptured, this, &CameraManager::imageCaptured);
    } else {
        qCWarning(log_ui_camera) << "Image capture is null";
    }

    if (m_mediaRecorder) {
        connect(m_mediaRecorder.get(), &QMediaRecorder::recorderStateChanged, this, [this](QMediaRecorder::RecorderState state) {
            if (state == QMediaRecorder::RecordingState) {
                emit recordingStarted();
            } else if (state == QMediaRecorder::StoppedState) {
                emit recordingStopped();
            }
        });
    } else {
        qCWarning(log_ui_camera) << "Media recorder is null";
    }
}

void CameraManager::setCameraFormat(const QCameraFormat &format) {
    if (m_camera) {
        m_camera->setCameraFormat(format);
    }
}

QCameraFormat CameraManager::getCameraFormat() const {
    return m_camera ? m_camera->cameraFormat() : QCameraFormat();
}

QList<QCameraFormat> CameraManager::getCameraFormats() const {
    return m_camera ? m_camera->cameraDevice().videoFormats() : QList<QCameraFormat>();
}

void CameraManager::loadCameraSettingAndSetCamera()
{
    qCDebug(log_ui_camera) << "Load camera setting and set camera";
    QSettings settings("Techxartisan", "Openterface");
    QString configDeviceDescription = settings.value("camera/device", "Openterface").toString();
    
    // Get the current camera description
    QString currentDeviceDescription = m_camera ? m_camera->cameraDevice().description() : QString();
    
    qCDebug(log_ui_camera) << "Config device description:" << configDeviceDescription;
    qCDebug(log_ui_camera) << "Current device description:" << currentDeviceDescription;

    // If the descriptions don't match, we need to set a new camera
    if (currentDeviceDescription != configDeviceDescription) {
        const QList<QCameraDevice> devices = QMediaDevices::videoInputs();
        if (devices.isEmpty()) {
            qCWarning(log_ui_camera) << "No video input devices found.";
        } else {
            bool cameraFound = false;
            for (const QCameraDevice &cameraDevice : devices) {
                if (cameraDevice.description() == configDeviceDescription) {
                    setCamera(cameraDevice, m_videoOutput);
                    cameraFound = true;
                    qCDebug(log_ui_camera) << "Camera set to:" << configDeviceDescription;
                    break;
                }
            }
            if (!cameraFound) {
                qCWarning(log_ui_camera) << "Configured camera not found:" << configDeviceDescription;
            }
        }
    } else {
        qCDebug(log_ui_camera) << "Current camera matches configuration. No change needed.";
    }
}

void CameraManager::queryResolutions()
{
    QPair<int, int> resolution = VideoHid::getInstance().getResolution();
    qCDebug(log_ui_camera) << "Input resolution: " << resolution;
    GlobalVar::instance().setInputWidth(resolution.first);
    GlobalVar::instance().setInputHeight(resolution.second);
    m_video_width = GlobalVar::instance().getCaptureWidth();
    m_video_height = GlobalVar::instance().getCaptureHeight();

    float input_fps = VideoHid::getInstance().getFps();
    updateResolutions(resolution.first, resolution.second, input_fps, m_video_width, m_video_height, GlobalVar::instance().getCaptureFps());
}

void CameraManager::updateResolutions(int input_width, int input_height, float input_fps, int capture_width, int capture_height, int capture_fps)
{
    emit resolutionsUpdated(input_width, input_height, input_fps, capture_width, capture_height, capture_fps);
}
