#include "cameramanager.h"

#include <QLoggingCategory>
#include <QSettings>
#include <QMediaDevices>
#include <QRegularExpression>
#include "global.h"
#include "video/videohid.h"
#include "../ui/globalsetting.h"
#include "../device/DeviceManager.h"
#include <QVideoWidget>
#include <QGraphicsVideoItem>
#include <QTimer>
#include <QThread>

Q_LOGGING_CATEGORY(log_ui_camera, "opf.ui.camera")

CameraManager::CameraManager(QObject *parent)
    : QObject(parent), m_videoOutput(nullptr), m_graphicsVideoOutput(nullptr), m_video_width(0), m_video_height(0)
{
    qDebug() << "CameraManager init...";
    
    // Initialize camera device to null state
    m_currentCameraDevice = QCameraDevice();
    m_currentCameraDeviceId.clear();
    m_currentCameraPortChain.clear();
    
    m_imageCapture = std::make_unique<QImageCapture>();
    m_mediaRecorder = std::make_unique<QMediaRecorder>();
    connect(m_imageCapture.get(), &QImageCapture::imageCaptured, this, &CameraManager::onImageCaptured);

    // Initialize available camera devices
    m_availableCameraDevices = getAvailableCameraDevices();
    qDebug() << "Found" << m_availableCameraDevices.size() << "available camera devices";
    
    // Display all camera device IDs for debugging
    displayAllCameraDeviceIds();
}

CameraManager::~CameraManager() = default;

void CameraManager::setCamera(const QCameraDevice &cameraDevice, QVideoWidget* videoOutput)
{
    qDebug() << "Set Camera to videoOutput: " << videoOutput << ", device name: " << cameraDevice.description();
    setCameraDevice(cameraDevice);

    setVideoOutput(videoOutput);

    queryResolutions();

    // Set camera format
    startCamera();
}

void CameraManager::setCamera(const QCameraDevice &cameraDevice, QGraphicsVideoItem* videoOutput)
{
    qDebug() << "Set Camera to graphics videoOutput: " << videoOutput << ", device name: " << cameraDevice.description();
    setCameraDevice(cameraDevice);

    setVideoOutput(videoOutput);

    queryResolutions();

    // Set camera format
    startCamera();
}

void CameraManager::setCameraDevice(const QCameraDevice &cameraDevice)
{
    try {
        qDebug() << "Setting camera device to:" << cameraDevice.description();
        
        // Validate the camera device
        if (!isCameraDeviceValid(cameraDevice)) {
            qCWarning(log_ui_camera) << "Cannot set invalid camera device";
            return;
        }
        
        // Create new camera instance
        m_camera.reset(new QCamera(cameraDevice));
        
        if (!m_camera) {
            qCritical() << "Failed to create camera instance for device:" << cameraDevice.description();
            return;
        }
        
        // Setup connections before setting up capture session
        setupConnections();
        
        // Set up capture session
        m_captureSession.setCamera(m_camera.get());
        m_captureSession.setImageCapture(m_imageCapture.get());
        
        // Update current device tracking
        m_currentCameraDevice = cameraDevice;
        m_currentCameraDeviceId = QString::fromUtf8(cameraDevice.id());
        
        qDebug() << "Camera device successfully set to:" << cameraDevice.description();
        
    } catch (const std::exception& e) {
        qCritical() << "Exception in setCameraDevice:" << e.what();
        m_camera.reset();
    } catch (...) {
        qCritical() << "Unknown exception in setCameraDevice";
        m_camera.reset();
    }
}

void CameraManager::setVideoOutput(QVideoWidget* videoOutput)
{
    if (videoOutput) {
        m_videoOutput = videoOutput;
        m_graphicsVideoOutput = nullptr; // Clear graphics output
        qDebug() << "Setting video output to: " << videoOutput->objectName();
        m_captureSession.setVideoOutput(videoOutput);
        
        // Verify the connection was successful
        if (m_captureSession.videoOutput() == videoOutput) {
            qDebug() << "Widget video output successfully connected to capture session";
        } else {
            qCWarning(log_ui_camera) << "Failed to connect widget video output to capture session";
        }
    } else {
        qCWarning(log_ui_camera) << "Attempted to set null video output";
    }
}

void CameraManager::setVideoOutput(QGraphicsVideoItem* videoOutput)
{
    if (videoOutput) {
        m_graphicsVideoOutput = videoOutput;
        m_videoOutput = nullptr; // Clear widget output
        qDebug() << "Setting graphics video output";
        m_captureSession.setVideoOutput(videoOutput);
        
        // Verify the connection was successful
        if (m_captureSession.videoOutput() == videoOutput) {
            qDebug() << "Graphics video output successfully connected to capture session";
        } else {
            qCWarning(log_ui_camera) << "Failed to connect graphics video output to capture session";
        }
    } else {
        qCWarning(log_ui_camera) << "Attempted to set null graphics video output";
    }
}

void CameraManager::startCamera()
{
    qDebug() << "Camera start..";
    
    try {
        if (m_camera) {
            // Check if camera is already active to avoid redundant starts
            if (m_camera->isActive()) {
                qDebug() << "Camera is already active, skipping start";
                return;
            }
            
            qDebug() << "Starting camera:" << m_camera->cameraDevice().description();
            
            // Ensure video output is connected before starting camera
            if (m_videoOutput) {
                qDebug() << "Ensuring widget video output is connected before starting camera";
                m_captureSession.setVideoOutput(m_videoOutput);
            } else if (m_graphicsVideoOutput) {
                qDebug() << "Ensuring graphics video output is connected before starting camera";
                m_captureSession.setVideoOutput(m_graphicsVideoOutput);
            }
            
            m_camera->start();
            
            // Minimal wait time to reduce transition delay
            QThread::msleep(25);
            
            // Verify camera started
            if (m_camera->isActive()) {
                qDebug() << "Camera started successfully and is active";
                // Emit active state change as soon as camera starts
                emit cameraActiveChanged(true);
            } else {
                qCWarning(log_ui_camera) << "Camera start command sent but camera is not active";
            }
            
        } else {
            qCWarning(log_ui_camera) << "Camera is null, cannot start";
            return;
        }
        
        // Start VideoHid after camera is active to ensure proper synchronization
        VideoHid::getInstance().start();
        
    } catch (const std::exception& e) {
        qCritical() << "Exception starting camera:" << e.what();
    } catch (...) {
        qCritical() << "Unknown exception starting camera";
    }
}

void CameraManager::stopCamera()
{
    qDebug() << "Stopping camera..";
    
    try {
        // Stop VideoHid first
        VideoHid::getInstance().stop();

        if (m_camera) {
            // Check if camera is already stopped to avoid redundant stops
            if (!m_camera->isActive()) {
                qDebug() << "Camera is already stopped";
                return;
            }
            
            qDebug() << "Stopping camera:" << m_camera->cameraDevice().description();
            m_camera->stop();
            
            // Wait for camera to fully stop
            QThread::msleep(100);
            
            qDebug() << "Camera stopped successfully";
        } else {
            qCWarning(log_ui_camera) << "Camera is null, cannot stop";
        }
        
    } catch (const std::exception& e) {
        qCritical() << "Exception stopping camera:" << e.what();
    } catch (...) {
        qCritical() << "Unknown exception stopping camera";
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
        qDebug() << "Directory do not exist";
        if (!dir.mkpath(".")) {
            qDebug() << "Failed to create directory: " << customFolderPath;
            return;
        }
    }
    
    QString saveName = customFolderPath + "/" + timestamp + ".png";

    QImage coayImage = img.copy(copyRect);
    if(coayImage.save(saveName)){
        qDebug() << "succefully save img to : " << saveName;
        emit lastImagePath(saveName);
    }else{
        qDebug() << "fail save img to : " << saveName;
    }
    copyRect = QRect(0, 0, m_video_width, m_video_height);
}

void CameraManager::takeImage(const QString& file)
{
    if (m_imageCapture && m_camera && m_camera->isActive()) {
        if (m_imageCapture->isReadyForCapture()) {
            filePath = file;
            m_imageCapture->capture();
            qDebug() << "captured .....................";
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
            qDebug() << "captured .....................";
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
    try {
        if (m_camera) {
            // Disconnect any existing connections first to prevent duplicate connections
            disconnect(m_camera.get(), nullptr, this, nullptr);
            
            connect(m_camera.get(), &QCamera::activeChanged, this, [this](bool active) {
                qDebug() << "Camera active state changed to:" << active;
                
                if (active) {
                    try {
                        configureResolutionAndFormat();
                    } catch (...) {
                        qCritical() << "Exception in configureResolutionAndFormat";
                    }
                }

                emit cameraActiveChanged(active);
            });
            
            connect(m_camera.get(), &QCamera::errorOccurred, this, [this](QCamera::Error error, const QString &errorString) {
                qCritical() << "Camera error occurred:" << static_cast<int>(error) << errorString;
                emit cameraError(errorString);
            });
            
            qDebug() << "Camera connections set up successfully";
        } else {
            qCWarning(log_ui_camera) << "Camera is null, cannot set up connections";
        }

        if (m_imageCapture) {
            // Disconnect any existing connections first
            disconnect(m_imageCapture.get(), nullptr, this, nullptr);
            
            connect(m_imageCapture.get(), &QImageCapture::imageCaptured, this, &CameraManager::imageCaptured);
        } else {
            qCWarning(log_ui_camera) << "Image capture is null";
        }

        if (m_mediaRecorder) {
            // Disconnect any existing connections first
            disconnect(m_mediaRecorder.get(), nullptr, this, nullptr);
            
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
        
    } catch (const std::exception& e) {
        qCritical() << "Exception in setupConnections:" << e.what();
    } catch (...) {
        qCritical() << "Unknown exception in setupConnections";
    }
}

void CameraManager::configureResolutionAndFormat()
{
    // Get resolution directly from camera format if available
    QCameraFormat currentFormat = m_camera->cameraFormat();
    QSize resolution;
    
    if (currentFormat.isNull() || currentFormat.resolution().isEmpty()) {
        // If camera format is not yet available, use stored values
        resolution = QSize(m_video_width > 0 ? m_video_width : 1920, 
                          m_video_height > 0 ? m_video_height : 1080);
        qDebug() << "Using stored/default resolution:" << resolution;
    } else {
        resolution = currentFormat.resolution();
        qDebug() << "Got resolution from camera format:" << resolution;
        
        // Update our stored values
        m_video_width = resolution.width();
        m_video_height = resolution.height();
    }
    
    int fps = GlobalVar::instance().getCaptureFps() > 0 ? 
        GlobalVar::instance().getCaptureFps() : 30;
    
    QCameraFormat format = getVideoFormat(resolution, fps, QVideoFrameFormat::Format_Jpeg);
    setCameraFormat(format);
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

void CameraManager::queryResolutions()
{
    QPair<int, int> resolution = VideoHid::getInstance().getResolution();

    qDebug() << "Input resolution: " << resolution;

    GlobalVar::instance().setInputWidth(resolution.first);
    GlobalVar::instance().setInputHeight(resolution.second);

    m_video_width = GlobalVar::instance().getCaptureWidth();
    m_video_height = GlobalVar::instance().getCaptureHeight();

    float input_fps = VideoHid::getInstance().getFps();
    float pixelClk = VideoHid::getInstance().getPixelclk();

    emit resolutionsUpdated(resolution.first, resolution.second, input_fps, m_video_width, m_video_height, GlobalVar::instance().getCaptureFps(), pixelClk);
}


QList<QVideoFrameFormat> CameraManager::getSupportedPixelFormats() const {
    QList<QVideoFrameFormat> pixelFormats;

    QSize defaultSize(1920, 1080); // Set a default resolution, adjust as needed

    pixelFormats.append(QVideoFrameFormat(defaultSize, QVideoFrameFormat::Format_Jpeg));
    pixelFormats.append(QVideoFrameFormat(defaultSize, QVideoFrameFormat::Format_YUV420P));

    return pixelFormats;
}


QCameraFormat CameraManager::getVideoFormat(const QSize &resolution, int desiredFrameRate, QVideoFrameFormat::PixelFormat pixelFormat) const {
    QCameraFormat bestMatch;
    int closestFrameRate = INT_MAX;


    for (const QCameraFormat &format : getCameraFormats()) {
        QSize formatResolution = format.resolution();
        int minFrameRate = format.minFrameRate();
        int maxFrameRate = format.maxFrameRate();
        QVideoFrameFormat::PixelFormat formatPixelFormat = format.pixelFormat();

        VideoFormatKey key = {formatResolution, minFrameRate, maxFrameRate, formatPixelFormat};
        // Use const_cast here to avoid the const issue
        const_cast<std::map<VideoFormatKey, QCameraFormat>&>(videoFormatMap)[key] = format;

        if (formatResolution == resolution && formatPixelFormat == pixelFormat) {
            if (desiredFrameRate >= minFrameRate && desiredFrameRate <= maxFrameRate) {
                // If we find an exact match, return it immediately
                qDebug() << "Exact match found" << format.minFrameRate() << format.maxFrameRate();
                return format;
            }

            // Find the closest frame rate within the supported range
            int midFrameRate = (minFrameRate + maxFrameRate) / 2;
            int frameDiff = qAbs(midFrameRate - desiredFrameRate);
            if (frameDiff < closestFrameRate) {
                qDebug() << "Closest match found";
                closestFrameRate = frameDiff;
                bestMatch = format;
            }
        }
    }

    return bestMatch;
}

std::map<VideoFormatKey, QCameraFormat> CameraManager::getVideoFormatMap(){
    return videoFormatMap;
}

// Camera device management and switching functionality

QList<QCameraDevice> CameraManager::getAvailableCameraDevices() const
{
    return QMediaDevices::videoInputs();
}

QCameraDevice CameraManager::getCurrentCameraDevice() const
{
    return m_currentCameraDevice;
}

bool CameraManager::switchToCameraDevice(const QCameraDevice &cameraDevice)
{
    if (!isCameraDeviceValid(cameraDevice)) {
        qCWarning(log_ui_camera) << "Cannot switch to invalid camera device:" << cameraDevice.description();
        return false;
    }
    
    qDebug() << "Switching to camera device:" << cameraDevice.description();
    
    QString newCameraID;
    try {
        newCameraID = QString::fromUtf8(cameraDevice.id());
        qDebug() << "New camera ID:" << newCameraID;
    } catch (...) {
        qCritical() << "Failed to get new camera device ID";
        return false;
    }
    
    QString currentCameraID;
    bool hasCurrentDevice = false;
    
    if (!m_currentCameraDevice.isNull()) {
        try {
            currentCameraID = QString::fromUtf8(m_currentCameraDevice.id());
            hasCurrentDevice = true;
            qDebug() << "Current camera ID:" << currentCameraID;
        } catch (...) {
            qCWarning(log_ui_camera) << "Failed to get current camera device ID, treating as no current device";
            hasCurrentDevice = false;
        }
    } else {
        qDebug() << "No current camera device (null)";
    }
    
    // Check if we're already using this device - avoid unnecessary switching
    if (hasCurrentDevice && currentCameraID == newCameraID) {
        qDebug() << "Already using camera device:" << cameraDevice.description() 
                              << "- skipping switch";
        return true;
    }

    QCameraDevice previousDevice = m_currentCameraDevice;
    bool wasActive = m_camera && m_camera->isActive();
    
        QString previousDeviceDescription = previousDevice.isNull() ? "None" : previousDevice.description();
        qDebug() << "Switching camera from" << previousDeviceDescription 
                         << "to" << cameraDevice.description();
        
        // Emit switching signal for UI feedback (this will preserve the last frame)
        emit cameraDeviceSwitching(previousDeviceDescription, cameraDevice.description());
        
        try {
        // Prepare new camera device first to minimize transition time
        std::unique_ptr<QCamera> newCamera;
        try {
            qDebug() << "Creating new camera for device:" << cameraDevice.description();
            newCamera.reset(new QCamera(cameraDevice));
            if (!newCamera) {
                qCritical() << "Failed to create new camera instance";
                return false;
            }
        } catch (...) {
            qCritical() << "Exception creating new camera instance";
            return false;
        }
        
        // Stop current camera if active while preserving last frame on video output
        if (wasActive && m_camera) {
            qDebug() << "Stopping current camera before switch (preserving last frame)";
            m_camera->stop();
            // Brief wait to ensure camera stops cleanly
            QThread::msleep(30);
        }
        
        // Disconnect existing camera connections to prevent crashes
        if (m_camera) {
            qDebug() << "Disconnecting existing camera connections";
            disconnect(m_camera.get(), nullptr, this, nullptr);
        }
        
        // Replace camera object and update tracking immediately
        m_camera = std::move(newCamera);
        m_currentCameraDevice = cameraDevice;
        m_currentCameraDeviceId = QString::fromUtf8(cameraDevice.id());
        // Don't clear port chain here, it will be set by caller if needed
        
        // Set up connections for the new camera
        setupConnections();
        
        // Set up capture session with new camera (keep video output to preserve last frame)
        qDebug() << "Setting up capture session with new camera (preserving video output)";
        m_captureSession.setCamera(m_camera.get());
        m_captureSession.setImageCapture(m_imageCapture.get());
        
        // IMPORTANT: Always re-establish video output connection after camera change
        // This ensures the new camera feed is properly displayed
        if (m_videoOutput) {
            qDebug() << "Re-establishing widget video output connection after camera switch";
            m_captureSession.setVideoOutput(m_videoOutput);
        } else if (m_graphicsVideoOutput) {
            qDebug() << "Re-establishing graphics video output connection after camera switch";
            m_captureSession.setVideoOutput(m_graphicsVideoOutput);
        } else {
            qCWarning(log_ui_camera) << "No video output available to connect new camera";
        }
        
        // Restart camera if it was previously active
        if (wasActive) {
            qDebug() << "Starting new camera after switch";
            startCamera();
            
            // Give a brief moment for the camera to start before declaring success
            QThread::msleep(25);
            
            // Force refresh of video output to ensure new camera feed is displayed
            refreshVideoOutput();
        }
        
        // Update settings to remember the new device
        QSettings settings("Techxartisan", "Openterface");
        settings.setValue("camera/device", cameraDevice.description());
        settings.setValue("camera/deviceId", newCameraID);
        
        // Emit signals with proper error handling
        emit cameraDeviceChanged(cameraDevice, previousDevice);
        
        QString previousDeviceId;
        if (!previousDevice.isNull()) {
            try {
                previousDeviceId = QString::fromUtf8(previousDevice.id());
            } catch (...) {
                qCWarning(log_ui_camera) << "Failed to get previous device ID for signal";
            }
        }
        
        emit cameraDeviceSwitched(previousDeviceId, newCameraID);
        emit cameraDeviceConnected(cameraDevice);
        
        if (!previousDevice.isNull()) {
            emit cameraDeviceDisconnected(previousDevice);
        }
        
        // Emit completion signal for UI feedback with slight delay to ensure camera is ready
        QTimer::singleShot(100, this, [this, cameraDevice]() {
            emit cameraDeviceSwitchComplete(cameraDevice.description());
            qDebug() << "Camera switch completion signal sent for:" << cameraDevice.description();
        });
        
        qDebug() << "Camera device switch successful to:" << newCameraID << cameraDevice.description();
        return true;
        
    } catch (const std::exception& e) {
        qCritical() << "Exception during camera switch:" << e.what();
        // Reset to clean state on error
        m_camera.reset();
        m_currentCameraDevice = QCameraDevice();
        m_currentCameraDeviceId.clear();
        m_currentCameraPortChain.clear();
        
        // Emit failure signal to clear switching state
        emit cameraDeviceSwitchComplete("Switch Failed");
        return false;
    } catch (...) {
        qCritical() << "Unknown exception during camera switch";
        // Reset to clean state on error
        m_camera.reset();
        m_currentCameraDevice = QCameraDevice();
        m_currentCameraDeviceId.clear();
        m_currentCameraPortChain.clear();
        
        // Emit failure signal to clear switching state
        emit cameraDeviceSwitchComplete("Switch Failed");
        return false;
    }
}

bool CameraManager::switchToCameraDevice(const QCameraDevice &cameraDevice, const QString& portChain)
{
    // Call the main switch method first
    bool success = switchToCameraDevice(cameraDevice);
    
    if (success) {
        // Update port chain tracking after successful switch
        m_currentCameraPortChain = portChain;
        qDebug() << "Camera device switch successful with port chain tracking:" << portChain;
    }
    
    return success;
}

bool CameraManager::switchToCameraDeviceById(const QString& deviceId)
{
    QList<QCameraDevice> devices = getAvailableCameraDevices();
    for (const QCameraDevice& device : devices) {
        if (QString::fromUtf8(device.id()) == deviceId) {
            qDebug() << "Found camera device by ID:" << device.description() 
                              << "ID:" << deviceId;
            return switchToCameraDevice(device);
        }
    }
    
    qCWarning(log_ui_camera) << "Camera device not found with ID:" << deviceId;
    return false;
}


QString CameraManager::getCurrentCameraDeviceId() const
{
    if (m_currentCameraDeviceId.isEmpty()) {
        qDebug() << "Current camera device ID is empty";
        return QString();
    }
    
    qDebug() << "Current camera device ID:" << m_currentCameraDeviceId;
    return m_currentCameraDeviceId;
}

QString CameraManager::getCurrentCameraDeviceDescription() const
{
    if (m_currentCameraDevice.isNull()) {
        qDebug() << "Current camera device is null, returning empty string";
        return QString();
    }
    
    try {
        QString description = m_currentCameraDevice.description();
        qDebug() << "Current camera device description:" << description;
        return description;
    } catch (const std::exception& e) {
        qCritical() << "Exception getting camera device description:" << e.what();
        return QString();
    } catch (...) {
        qCritical() << "Unknown exception getting camera device description";
        return QString();
    }
}





bool CameraManager::isCameraDeviceValid(const QCameraDevice &cameraDevice) const
{
    return !cameraDevice.isNull() && !cameraDevice.id().isEmpty();
}

bool CameraManager::isCameraDeviceAvailable(const QString& deviceId) const
{
    QList<QCameraDevice> devices = getAvailableCameraDevices();
    for (const QCameraDevice& device : devices) {
        if (QString::fromUtf8(device.id()) == deviceId) {
            return true;
        }
    }
    return false;
}

QStringList CameraManager::getAvailableCameraDeviceDescriptions() const
{
    QStringList descriptions;
    QList<QCameraDevice> devices = getAvailableCameraDevices();
    for (const QCameraDevice& device : devices) {
        descriptions.append(device.description());
    }
    return descriptions;
}

QStringList CameraManager::getAvailableCameraDeviceIds() const
{
    QStringList ids;
    QList<QCameraDevice> devices = getAvailableCameraDevices();
    for (const QCameraDevice& device : devices) {
        ids.append(QString::fromUtf8(device.id()));
    }
    return ids;
}

QCameraDevice CameraManager::findBestAvailableCamera() const
{
    QList<QCameraDevice> devices = getAvailableCameraDevices();
    
    if (devices.isEmpty()) {
        qDebug() << "No camera devices available";
        return QCameraDevice();
    }
    
    if (devices.size() == 1) {
        // Only one device available - connect to it
        qDebug() << "Single camera device found:" << devices.first().description();
        return devices.first();
    } else {
        // Multiple devices available - just select the first one
        qDebug() << "Multiple camera devices found (" << devices.size() << "), selecting first available:" << devices.first().description();
        return devices.first();
    }
}

QStringList CameraManager::getAllCameraDescriptions() const
{
    QStringList descriptions;
    QList<QCameraDevice> devices = getAvailableCameraDevices();
    
    for (const QCameraDevice& device : devices) {
        descriptions.append(device.description());
    }
    
    return descriptions;
}

void CameraManager::refreshAvailableCameraDevices()
{
    QList<QCameraDevice> previousDevices = m_availableCameraDevices;
    m_availableCameraDevices = getAvailableCameraDevices();
    
    qDebug() << "Refreshed camera devices, now have" << m_availableCameraDevices.size() << "devices";
    
    // Display all camera device IDs after refresh
    displayAllCameraDeviceIds();
    
    // Emit signal if device count changed
    if (previousDevices.size() != m_availableCameraDevices.size()) {
        emit availableCameraDevicesChanged(m_availableCameraDevices.size());
        qDebug() << "Camera device count changed from" << previousDevices.size() 
                               << "to" << m_availableCameraDevices.size();
    }
}

// Note: Automatic device coordination methods have been disabled
// These methods previously handled automatic camera switching when devices changed

QString CameraManager::extractShortIdentifier(const QString& fullId) const
{
    // Extract patterns like "7&1FF4451E&2&0000" from full device IDs
    // This pattern appears in both camera device IDs and device details
    
    // Look for patterns with format: digit&hexdigits&digit&hexdigits
    // Examples: "7&1FF4451E&2&0000", "6&2ABC123F&1&0001", etc.
    QRegularExpression regex(R"((\d+&[A-F0-9]+&\d+&[A-F0-9]+))", QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch match = regex.match(fullId);
    
    if (match.hasMatch()) {
        QString shortId = match.captured(1);
        qDebug() << "Extracted short identifier:" << shortId << "from:" << fullId;
        return shortId;
    }
    
    qDebug() << "No short identifier pattern found in:" << fullId;
    return QString();
}

void CameraManager::displayAllCameraDeviceIds() const
{
    try {
        QList<QCameraDevice> devices = getAvailableCameraDevices();
        
        qDebug() << "=== Available Camera Devices ===";
        qDebug() << "Total devices found:" << devices.size();
        
        if (devices.isEmpty()) {
            qDebug() << "No camera devices available";
            return;
        }
        
        for (int i = 0; i < devices.size(); ++i) {
            const QCameraDevice& device = devices[i];
            
            try {
                QByteArray deviceId = device.id();
                QString deviceIdStr = QString::fromUtf8(deviceId);
                QString deviceDescription = device.description();
                
                qDebug() << "Device" << (i + 1) << ":";
                qDebug() << "  Description:" << deviceDescription;
                qDebug() << "  ID (raw QByteArray):" << deviceId;
                qDebug() << "  ID (as QString):" << deviceIdStr;
                qDebug() << "  ID (hex representation):" << deviceId.toHex();
                qDebug() << "  Is Default:" << device.isDefault();
                qDebug() << "  Position:" << static_cast<int>(device.position());
                qDebug() << "  ---";
                
            } catch (const std::exception& e) {
                qCWarning(log_ui_camera) << "Exception accessing device" << (i + 1) << "details:" << e.what();
            } catch (...) {
                qCWarning(log_ui_camera) << "Unknown exception accessing device" << (i + 1) << "details";
            }
        }
        
        qDebug() << "=== End Camera Device List ===";
        
    } catch (const std::exception& e) {
        qCritical() << "Exception in displayAllCameraDeviceIds:" << e.what();
    } catch (...) {
        qCritical() << "Unknown exception in displayAllCameraDeviceIds";
    }
}

void CameraManager::handleCameraTimeout()
{
    qDebug() << "Camera timeout occurred, attempting to recover connection";
    
    if (m_camera && m_camera->isActive()) {
        qDebug() << "Camera is still active, stopping and restarting";
        stopCamera();
        
        // Brief delay before restart
        QTimer::singleShot(500, this, [this]() {
            startCamera();
            qDebug() << "Camera restart attempted after timeout";
        });
    } else {
        qCWarning(log_ui_camera) << "Camera timeout: camera is not active";
        
        // Try to reinitialize camera if available
        if (!m_currentCameraDevice.isNull()) {
            qDebug() << "Attempting to reinitialize camera device";
            setCameraDevice(m_currentCameraDevice);
            startCamera();
        } else {
            qCWarning(log_ui_camera) << "No camera device available for timeout recovery";
        }
    }
}

QCameraDevice CameraManager::findMatchingCameraDevice(const QString& portChain) const
{
    if (portChain.isEmpty()) {
        qDebug() << "Empty port chain provided";
        return QCameraDevice();
    }

    qDebug() << "Finding camera device matching port chain:" << portChain;

    // Use DeviceManager to look up device information by port chain
    DeviceManager& deviceManager = DeviceManager::getInstance();
    QList<DeviceInfo> devices = deviceManager.getDevicesByPortChain(portChain);
    
    if (devices.isEmpty()) {
        qCWarning(log_ui_camera) << "No devices found for port chain:" << portChain;
        return QCameraDevice();
    }

    qDebug() << "Found" << devices.size() << "device(s) for port chain:" << portChain;

    // Look for a device that has camera information
    DeviceInfo selectedDevice;
    for (const DeviceInfo& device : devices) {
        if (!device.cameraDeviceId.isEmpty() || !device.cameraDevicePath.isEmpty()) {
            selectedDevice = device;
            qDebug() << "Found device with camera info:" 
                     << "cameraDeviceId:" << device.cameraDeviceId
                     << "cameraDevicePath:" << device.cameraDevicePath;
            break;
        }
    }

    if (!selectedDevice.isValid() || (selectedDevice.cameraDeviceId.isEmpty() && selectedDevice.cameraDevicePath.isEmpty())) {
        qCWarning(log_ui_camera) << "No device with camera information found for port chain:" << portChain;
        return QCameraDevice();
    }

    // Extract short identifier from target camera ID for better matching
    QString targetShortId;
    if (!selectedDevice.cameraDeviceId.isEmpty()) {
        targetShortId = extractShortIdentifier(selectedDevice.cameraDeviceId);
        qDebug() << "Extracted target short identifier:" << targetShortId;
    }

    QList<QCameraDevice> availableCameras = getAvailableCameraDevices();

    for (const QCameraDevice& camera : availableCameras) {
        QString cameraId = QString::fromUtf8(camera.id());
        QString cameraDescription = camera.description();

        qDebug() << "Checking camera device:" << cameraDescription 
                 << "ID:" << cameraId;

        // Try multiple matching strategies
        // Strategy 1: Short identifier match (preferred method)
        if (!targetShortId.isEmpty() && cameraId.contains(targetShortId, Qt::CaseInsensitive)) {
            qDebug() << "Matched camera by short identifier:" << targetShortId;
            deviceManager.setCurrentSelectedDevice(selectedDevice);
            return camera;
        }
        // Strategy 2: Direct ID match
        if (!selectedDevice.cameraDeviceId.isEmpty() && cameraId == selectedDevice.cameraDeviceId) {
            qDebug() << "Matched camera by exact ID:" << selectedDevice.cameraDeviceId;
            deviceManager.setCurrentSelectedDevice(selectedDevice);
            return camera;
        }
        // Strategy 3: Path match (if applicable)
        if (!selectedDevice.cameraDevicePath.isEmpty() && cameraId.contains(selectedDevice.cameraDevicePath, Qt::CaseInsensitive)) {
            qDebug() << "Matched camera by path:" << selectedDevice.cameraDevicePath;
            deviceManager.setCurrentSelectedDevice(selectedDevice);
            return camera;
        }
    }

    qCWarning(log_ui_camera) << "Could not find matching Qt camera device for port chain:" << portChain;
    return QCameraDevice();
}

bool CameraManager::initializeCameraWithVideoOutput(QVideoWidget* videoOutput)
{
    qDebug() << "Initializing camera with video output";
    
    if (!videoOutput) {
        qCWarning(log_ui_camera) << "Cannot initialize camera with null video output";
        return false;
    }
    
    // Set the video output first if it's different from current
    if (m_videoOutput != videoOutput) {
        setVideoOutput(videoOutput);
    }
    
    // Check if we already have an active camera device
    if (hasActiveCameraDevice()) {
        qDebug() << "Camera already active with device:" << m_currentCameraDevice.description() 
                 << "at port chain:" << m_currentCameraPortChain;
        return true;
    }
    
    bool switchSuccess = false;
    
    // First priority: Check for port chain in global settings
    QString portChain = GlobalSetting::instance().getOpenterfacePortChain();
    
    if (!portChain.isEmpty()) {
        qDebug() << "Found port chain in global settings:" << portChain;
        
        QCameraDevice matchedCamera = findMatchingCameraDevice(portChain);
        
        if (!matchedCamera.isNull()) {
            switchSuccess = switchToCameraDevice(matchedCamera, portChain);
            if (switchSuccess) {
                qDebug() << "✓ Successfully switched to camera using port chain:" << portChain;
                qDebug() << "✓ Selected camera:" << matchedCamera.description();
            } else {
                qCWarning(log_ui_camera) << "Failed to switch to matched camera device:" << matchedCamera.description();
            }
        } else {
            qCDebug(log_ui_camera) << "No matching camera device found for port chain:" << portChain;
        }
    } else {
        qDebug() << "No port chain found in global settings, using fallback methods";
    }
    
    // Fallback: Traditional camera selection logic (without port chain tracking)
    if (!switchSuccess) {
        // Enforce camera device description to be "Openterface"
        QList<QCameraDevice> devices = getAvailableCameraDevices();
        QCameraDevice openterfaceDevice;
        for (const QCameraDevice& device : devices) {
            if (device.description() == "Openterface") {
                openterfaceDevice = device;
                break;
            }
        }

        if (!openterfaceDevice.isNull()) {
            switchSuccess = switchToCameraDevice(openterfaceDevice);  // No port chain available for fallback
            if (switchSuccess) {
                qDebug() << "Camera switched to device with description 'Openterface' (no port chain tracked)";
            }
        } else {
            qCWarning(log_ui_camera) << "No camera device with description 'Openterface' found";
        }
    }

    // Start camera if switch was successful
    if (switchSuccess) {
        startCamera();
    }

    // If we still don't have a camera device, return false
    if (m_currentCameraDevice.isNull()) {
        qCWarning(log_ui_camera) << "No camera device available for initialization";
        return false;
    }

    return switchSuccess;
}

bool CameraManager::initializeCameraWithVideoOutput(QGraphicsVideoItem* videoOutput)
{
    qDebug() << "Initializing camera with graphics video output";
    
    if (!videoOutput) {
        qCWarning(log_ui_camera) << "Cannot initialize camera with null graphics video output";
        return false;
    }
    
    // Set the video output first if it's different from current
    if (m_graphicsVideoOutput != videoOutput) {
        setVideoOutput(videoOutput);
    }
    
    // Check if we already have an active camera device
    if (hasActiveCameraDevice()) {
        qDebug() << "Camera already active with device:" << m_currentCameraDevice.description() 
                 << "at port chain:" << m_currentCameraPortChain;
        return true;
    }
    
    bool switchSuccess = false;
    
    // First priority: Check for port chain in global settings
    QString portChain = GlobalSetting::instance().getOpenterfacePortChain();
    
    if (!portChain.isEmpty()) {
        qDebug() << "Found port chain in global settings:" << portChain;
        
        QCameraDevice matchedCamera = findMatchingCameraDevice(portChain);
        
        if (!matchedCamera.isNull()) {
            switchSuccess = switchToCameraDevice(matchedCamera, portChain);
            if (switchSuccess) {
                qDebug() << "✓ Successfully switched to camera using port chain:" << portChain;
                qDebug() << "✓ Selected camera:" << matchedCamera.description();
            } else {
                qCWarning(log_ui_camera) << "Failed to switch to matched camera device:" << matchedCamera.description();
            }
        } else {
            qCDebug(log_ui_camera) << "No matching camera device found for port chain:" << portChain;
        }
    } else {
        qDebug() << "No port chain found in global settings, using fallback methods";
    }
    
    // Fallback: Traditional camera selection logic (without port chain tracking)
    if (!switchSuccess) {
        // Enforce camera device description to be "Openterface"
        QList<QCameraDevice> devices = getAvailableCameraDevices();
        QCameraDevice openterfaceDevice;
        for (const QCameraDevice& device : devices) {
            if (device.description() == "Openterface") {
                openterfaceDevice = device;
                break;
            }
        }

        if (!openterfaceDevice.isNull()) {
            switchSuccess = switchToCameraDevice(openterfaceDevice);  // No port chain available for fallback
            if (switchSuccess) {
                qDebug() << "Camera switched to device with description 'Openterface' (no port chain tracked)";
            }
        } else {
            qCWarning(log_ui_camera) << "No camera device with description 'Openterface' found";
        }
    }

    // Start camera if switch was successful
    if (switchSuccess) {
        startCamera();
    }

    // If we still don't have a camera device, return false
    if (m_currentCameraDevice.isNull()) {
        qCWarning(log_ui_camera) << "No camera device available for initialization";
        return false;
    }

    return switchSuccess;
}

bool CameraManager::hasActiveCameraDevice() const
{
    return !m_currentCameraDevice.isNull() && 
           m_camera && 
           m_camera->isActive();
}

QString CameraManager::getCurrentCameraPortChain() const
{
    return m_currentCameraPortChain;
}

bool CameraManager::deactivateCameraByPortChain(const QString& portChain)
{
    if (portChain.isEmpty()) {
        qDebug() << "Cannot deactivate camera with empty port chain";
        return false;
    }
    
    // Check if we have an active camera and if its port chain matches
    if (m_currentCameraPortChain.isEmpty()) {
        qDebug() << "No current camera port chain tracked, cannot compare for deactivation";
        return false;
    }
    
    if (m_currentCameraPortChain != portChain) {
        qDebug() << "Current camera port chain" << m_currentCameraPortChain 
                 << "does not match unplugged device port chain" << portChain;
        return false;
    }
    
    qCInfo(log_ui_camera) << "Deactivating camera for unplugged device at port chain:" << portChain;
    
    try {
        // Stop and reset the camera
        if (m_camera && m_camera->isActive()) {
            qDebug() << "Stopping active camera due to device unplugging";
            stopCamera();
        }
        
        // Clear current device tracking
        m_currentCameraDevice = QCameraDevice();
        m_currentCameraDeviceId.clear();
        m_currentCameraPortChain.clear();
        
        // Reset camera objects
        if (m_camera) {
            disconnect(m_camera.get(), nullptr, this, nullptr);
            m_camera.reset();
        }
        
        // Clear capture session but keep video output to prevent flashing
        m_captureSession.setCamera(nullptr);
        m_captureSession.setImageCapture(nullptr);
        // Note: NOT clearing video output to prevent video pane flashing during device switches
        // m_captureSession.setVideoOutput(nullptr);
        
        qCInfo(log_ui_camera) << "Camera successfully deactivated for unplugged device";
        return true;
        
    } catch (const std::exception& e) {
        qCritical() << "Exception in deactivateCameraByPortChain:" << e.what();
        return false;
    } catch (...) {
        qCritical() << "Unknown exception in deactivateCameraByPortChain";
        return false;
    }
}

bool CameraManager::tryAutoSwitchToNewDevice(const QString& portChain)
{
    qDebug() << "Attempting auto-switch to new device with port chain:" << portChain;
    
    // Check if we currently have an active camera device
    if (hasActiveCameraDevice()) {
        qDebug() << "Active camera device detected, skipping auto-switch to preserve user selection";
        return false;
    }
    
    qDebug() << "No active camera device found, attempting to switch to new device";
    
    // Try to find a matching camera device for the port chain
    QCameraDevice matchedCamera = findMatchingCameraDevice(portChain);
    
    if (matchedCamera.isNull()) {
        qDebug() << "No matching camera device found for port chain:" << portChain;
        return false;
    }
    
    qDebug() << "Found matching camera device:" << matchedCamera.description() << "for port chain:" << portChain;
    
    // Switch to the new camera device
    bool switchSuccess = switchToCameraDevice(matchedCamera, portChain);
    
    if (switchSuccess) {
        qDebug() << "Successfully auto-switched to new camera device:" << matchedCamera.description() << "at port chain:" << portChain;
        
        // Start the camera if video output is available
        if (m_videoOutput) {
            startCamera();
        }
        
        emit newDeviceAutoConnected(matchedCamera, portChain);
    } else {
        qCWarning(log_ui_camera) << "Failed to auto-switch to new camera device:" << matchedCamera.description();
    }
    
    return switchSuccess;
}


bool CameraManager::switchToCameraDeviceByPortChain(const QString &portChain)
{
    if (portChain.isEmpty()) {
        qCWarning(log_ui_camera) << "Cannot switch to camera with empty port chain";
        return false;
    }
    
    qDebug() << "Attempting to switch to camera by port chain:" << portChain;
    
    try {
        QCameraDevice targetCamera = findMatchingCameraDevice(portChain);
        
        if (targetCamera.isNull()) {
            qCWarning(log_ui_camera) << "No matching camera found for port chain:" << portChain;
            return false;
        }
        
        qDebug() << "Found matching camera device:" << targetCamera.description() << "for port chain:" << portChain;
        
        bool switchSuccess = switchToCameraDevice(targetCamera, portChain);
        if (switchSuccess) {
            qDebug() << "Successfully switched to camera device:" << targetCamera.description() << "with port chain:" << portChain;
        } else {
            qCWarning(log_ui_camera) << "Failed to switch to camera device:" << targetCamera.description();
        }
        
        return switchSuccess;
        
    } catch (const std::exception& e) {
        qCritical() << "Exception in switchToCameraDeviceByPortChain:" << e.what();
        return false;
    } catch (...) {
        qCritical() << "Unknown exception in switchToCameraDeviceByPortChain";
        return false;
    }
}

void CameraManager::refreshVideoOutput()
{
    qDebug() << "Refreshing video output connection";
    
    try {
        // Force re-establishment of video output connection to ensure new camera feed is displayed
        if (m_videoOutput) {
            qDebug() << "Forcing widget video output refresh";
            // Temporarily disconnect and reconnect to force refresh
            m_captureSession.setVideoOutput(nullptr);
            QThread::msleep(10); // Brief pause
            m_captureSession.setVideoOutput(m_videoOutput);
            
            // Verify reconnection
            if (m_captureSession.videoOutput() == m_videoOutput) {
                qDebug() << "Widget video output refresh successful";
            } else {
                qCWarning(log_ui_camera) << "Widget video output refresh failed";
            }
        } else if (m_graphicsVideoOutput) {
            qDebug() << "Forcing graphics video output refresh";
            // Temporarily disconnect and reconnect to force refresh
            m_captureSession.setVideoOutput(nullptr);
            QThread::msleep(10); // Brief pause
            m_captureSession.setVideoOutput(m_graphicsVideoOutput);
            
            // Verify reconnection
            if (m_captureSession.videoOutput() == m_graphicsVideoOutput) {
                qDebug() << "Graphics video output refresh successful";
            } else {
                qCWarning(log_ui_camera) << "Graphics video output refresh failed";
            }
        } else {
            qCWarning(log_ui_camera) << "No video output available to refresh";
        }
        
        qDebug() << "Video output refresh completed";
        
    } catch (const std::exception& e) {
        qCritical() << "Exception refreshing video output:" << e.what();
    } catch (...) {
        qCritical() << "Unknown exception refreshing video output";
    }
}