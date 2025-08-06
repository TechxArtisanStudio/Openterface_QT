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
#include <QTimer>
#include <QThread>

Q_LOGGING_CATEGORY(log_ui_camera, "opf.ui.camera")

CameraManager::CameraManager(QObject *parent)
    : QObject(parent), m_videoOutput(nullptr), m_video_width(0), m_video_height(0)
{
    qCDebug(log_ui_camera) << "CameraManager init...";
    
    // Initialize camera device to null state
    m_currentCameraDevice = QCameraDevice();
    m_currentCameraDeviceId.clear();
    m_currentCameraPortChain.clear();
    
    m_imageCapture = std::make_unique<QImageCapture>();
    m_mediaRecorder = std::make_unique<QMediaRecorder>();
    connect(m_imageCapture.get(), &QImageCapture::imageCaptured, this, &CameraManager::onImageCaptured);

    // Initialize available camera devices
    m_availableCameraDevices = getAvailableCameraDevices();
    qCDebug(log_ui_camera) << "Found" << m_availableCameraDevices.size() << "available camera devices";
    
    // Display all camera device IDs for debugging
    displayAllCameraDeviceIds();
}

CameraManager::~CameraManager() = default;

void CameraManager::setCamera(const QCameraDevice &cameraDevice, QVideoWidget* videoOutput)
{
    qCDebug(log_ui_camera) << "Set Camera to videoOutput: " << videoOutput << ", device name: " << cameraDevice.description();
    setCameraDevice(cameraDevice);

    setVideoOutput(videoOutput);

    queryResolutions();

    // Set camera format
    startCamera();
}

void CameraManager::setCameraDevice(const QCameraDevice &cameraDevice)
{
    try {
        qCDebug(log_ui_camera) << "Setting camera device to:" << cameraDevice.description();
        
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
        
        qCDebug(log_ui_camera) << "Camera device successfully set to:" << cameraDevice.description();
        
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
        qCDebug(log_ui_camera) << "Setting video output to: " << videoOutput->objectName();
        m_captureSession.setVideoOutput(videoOutput);
    } else {
        qCWarning(log_ui_camera) << "Attempted to set null video output";
    }
}

void CameraManager::startCamera()
{
    qCDebug(log_ui_camera) << "Camera start..";
    
    try {
        if (m_camera) {
            // Check if camera is already active to avoid redundant starts
            if (m_camera->isActive()) {
                qCDebug(log_ui_camera) << "Camera is already active, skipping start";
                return;
            }
            
            qCDebug(log_ui_camera) << "Starting camera:" << m_camera->cameraDevice().description();
            m_camera->start();
            
            // Minimal wait time to reduce transition delay
            QThread::msleep(25);
            
            // Emit active state change as soon as camera starts
            emit cameraActiveChanged(true);
            
            qCDebug(log_ui_camera) << "Camera started successfully";
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
    qCDebug(log_ui_camera) << "Stopping camera..";
    
    try {
        // Stop VideoHid first
        VideoHid::getInstance().stop();

        if (m_camera) {
            // Check if camera is already stopped to avoid redundant stops
            if (!m_camera->isActive()) {
                qCDebug(log_ui_camera) << "Camera is already stopped";
                return;
            }
            
            qCDebug(log_ui_camera) << "Stopping camera:" << m_camera->cameraDevice().description();
            m_camera->stop();
            
            // Wait for camera to fully stop
            QThread::msleep(100);
            
            qCDebug(log_ui_camera) << "Camera stopped successfully";
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
        emit lastImagePath(saveName);
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
    try {
        if (m_camera) {
            // Disconnect any existing connections first to prevent duplicate connections
            disconnect(m_camera.get(), nullptr, this, nullptr);
            
            connect(m_camera.get(), &QCamera::activeChanged, this, [this](bool active) {
                qCDebug(log_ui_camera) << "Camera active state changed to:" << active;
                
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
            
            qCDebug(log_ui_camera) << "Camera connections set up successfully";
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
        qCDebug(log_ui_camera) << "Using stored/default resolution:" << resolution;
    } else {
        resolution = currentFormat.resolution();
        qCDebug(log_ui_camera) << "Got resolution from camera format:" << resolution;
        
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

    qCDebug(log_ui_camera) << "Input resolution: " << resolution;

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
                qCDebug(log_ui_camera) << "Exact match found" << format.minFrameRate() << format.maxFrameRate();
                return format;
            }

            // Find the closest frame rate within the supported range
            int midFrameRate = (minFrameRate + maxFrameRate) / 2;
            int frameDiff = qAbs(midFrameRate - desiredFrameRate);
            if (frameDiff < closestFrameRate) {
                qCDebug(log_ui_camera) << "Closest match found";
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
    
    qCDebug(log_ui_camera) << "Switching to camera device:" << cameraDevice.description();
    
    QString newCameraID;
    try {
        newCameraID = QString::fromUtf8(cameraDevice.id());
        qCDebug(log_ui_camera) << "New camera ID:" << newCameraID;
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
            qCDebug(log_ui_camera) << "Current camera ID:" << currentCameraID;
        } catch (...) {
            qCWarning(log_ui_camera) << "Failed to get current camera device ID, treating as no current device";
            hasCurrentDevice = false;
        }
    } else {
        qCDebug(log_ui_camera) << "No current camera device (null)";
    }
    
    // Check if we're already using this device - avoid unnecessary switching
    if (hasCurrentDevice && currentCameraID == newCameraID) {
        qCDebug(log_ui_camera) << "Already using camera device:" << cameraDevice.description() 
                              << "- skipping switch";
        return true;
    }

    QCameraDevice previousDevice = m_currentCameraDevice;
    bool wasActive = m_camera && m_camera->isActive();
    
        QString previousDeviceDescription = previousDevice.isNull() ? "None" : previousDevice.description();
        qCDebug(log_ui_camera) << "Switching camera from" << previousDeviceDescription 
                         << "to" << cameraDevice.description();
        
        // Emit switching signal for UI feedback (this will preserve the last frame)
        emit cameraDeviceSwitching(previousDeviceDescription, cameraDevice.description());
        
        try {
        // Prepare new camera device first to minimize transition time
        std::unique_ptr<QCamera> newCamera;
        try {
            qCDebug(log_ui_camera) << "Creating new camera for device:" << cameraDevice.description();
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
            qCDebug(log_ui_camera) << "Stopping current camera before switch (preserving last frame)";
            m_camera->stop();
            // Brief wait to ensure camera stops cleanly
            QThread::msleep(30);
        }
        
        // Disconnect existing camera connections to prevent crashes
        if (m_camera) {
            qCDebug(log_ui_camera) << "Disconnecting existing camera connections";
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
        qCDebug(log_ui_camera) << "Setting up capture session with new camera (preserving video output)";
        m_captureSession.setCamera(m_camera.get());
        m_captureSession.setImageCapture(m_imageCapture.get());
        
        // Video output should already be set and preserved from previous session
        // Only restore if it's somehow lost
        if (m_videoOutput && m_captureSession.videoOutput() != m_videoOutput) {
            qCDebug(log_ui_camera) << "Restoring video output";
            m_captureSession.setVideoOutput(m_videoOutput);
        }
        
        // Restart camera if it was previously active
        if (wasActive) {
            qCDebug(log_ui_camera) << "Starting new camera after switch";
            startCamera();
            
            // Give a brief moment for the camera to start before declaring success
            QThread::msleep(25);
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
        
        // Emit completion signal for UI feedback
        emit cameraDeviceSwitchComplete(cameraDevice.description());
        
        qCDebug(log_ui_camera) << "Camera device switch successful to:" << newCameraID << cameraDevice.description();
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
        qCDebug(log_ui_camera) << "Camera device switch successful with port chain tracking:" << portChain;
    }
    
    return success;
}

bool CameraManager::switchToCameraDeviceById(const QString& deviceId)
{
    QList<QCameraDevice> devices = getAvailableCameraDevices();
    for (const QCameraDevice& device : devices) {
        if (QString::fromUtf8(device.id()) == deviceId) {
            qCDebug(log_ui_camera) << "Found camera device by ID:" << device.description() 
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
        qCDebug(log_ui_camera) << "Current camera device ID is empty";
        return QString();
    }
    
    qCDebug(log_ui_camera) << "Current camera device ID:" << m_currentCameraDeviceId;
    return m_currentCameraDeviceId;
}

QString CameraManager::getCurrentCameraDeviceDescription() const
{
    if (m_currentCameraDevice.isNull()) {
        qCDebug(log_ui_camera) << "Current camera device is null, returning empty string";
        return QString();
    }
    
    try {
        QString description = m_currentCameraDevice.description();
        qCDebug(log_ui_camera) << "Current camera device description:" << description;
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
        qCDebug(log_ui_camera) << "No camera devices available";
        return QCameraDevice();
    }
    
    if (devices.size() == 1) {
        // Only one device available - connect to it
        qCDebug(log_ui_camera) << "Single camera device found:" << devices.first().description();
        return devices.first();
    } else {
        // Multiple devices available - just select the first one
        qCDebug(log_ui_camera) << "Multiple camera devices found (" << devices.size() << "), selecting first available:" << devices.first().description();
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
    
    qCDebug(log_ui_camera) << "Refreshed camera devices, now have" << m_availableCameraDevices.size() << "devices";
    
    // Display all camera device IDs after refresh
    displayAllCameraDeviceIds();
    
    // Emit signal if device count changed
    if (previousDevices.size() != m_availableCameraDevices.size()) {
        emit availableCameraDevicesChanged(m_availableCameraDevices.size());
        qCDebug(log_ui_camera) << "Camera device count changed from" << previousDevices.size() 
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
        qCDebug(log_ui_camera) << "Extracted short identifier:" << shortId << "from:" << fullId;
        return shortId;
    }
    
    qCDebug(log_ui_camera) << "No short identifier pattern found in:" << fullId;
    return QString();
}

void CameraManager::displayAllCameraDeviceIds() const
{
    try {
        QList<QCameraDevice> devices = getAvailableCameraDevices();
        
        qCDebug(log_ui_camera) << "=== Available Camera Devices ===";
        qCDebug(log_ui_camera) << "Total devices found:" << devices.size();
        
        if (devices.isEmpty()) {
            qCDebug(log_ui_camera) << "No camera devices available";
            return;
        }
        
        for (int i = 0; i < devices.size(); ++i) {
            const QCameraDevice& device = devices[i];
            
            try {
                QByteArray deviceId = device.id();
                QString deviceIdStr = QString::fromUtf8(deviceId);
                QString deviceDescription = device.description();
                
                qCDebug(log_ui_camera) << "Device" << (i + 1) << ":";
                qCDebug(log_ui_camera) << "  Description:" << deviceDescription;
                qCDebug(log_ui_camera) << "  ID (raw QByteArray):" << deviceId;
                qCDebug(log_ui_camera) << "  ID (as QString):" << deviceIdStr;
                qCDebug(log_ui_camera) << "  ID (hex representation):" << deviceId.toHex();
                qCDebug(log_ui_camera) << "  Is Default:" << device.isDefault();
                qCDebug(log_ui_camera) << "  Position:" << static_cast<int>(device.position());
                qCDebug(log_ui_camera) << "  ---";
                
            } catch (const std::exception& e) {
                qCWarning(log_ui_camera) << "Exception accessing device" << (i + 1) << "details:" << e.what();
            } catch (...) {
                qCWarning(log_ui_camera) << "Unknown exception accessing device" << (i + 1) << "details";
            }
        }
        
        qCDebug(log_ui_camera) << "=== End Camera Device List ===";
        
    } catch (const std::exception& e) {
        qCritical() << "Exception in displayAllCameraDeviceIds:" << e.what();
    } catch (...) {
        qCritical() << "Unknown exception in displayAllCameraDeviceIds";
    }
}

void CameraManager::handleCameraTimeout()
{
    qCDebug(log_ui_camera) << "Camera timeout occurred, attempting to recover connection";
    
    if (m_camera && m_camera->isActive()) {
        qCDebug(log_ui_camera) << "Camera is still active, stopping and restarting";
        stopCamera();
        
        // Brief delay before restart
        QTimer::singleShot(500, this, [this]() {
            startCamera();
            qCDebug(log_ui_camera) << "Camera restart attempted after timeout";
        });
    } else {
        qCWarning(log_ui_camera) << "Camera timeout: camera is not active";
        
        // Try to reinitialize camera if available
        if (!m_currentCameraDevice.isNull()) {
            qCDebug(log_ui_camera) << "Attempting to reinitialize camera device";
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
        qCDebug(log_ui_camera) << "Empty port chain provided";
        return QCameraDevice();
    }

    qCDebug(log_ui_camera) << "Finding camera device matching port chain:" << portChain;

    // Use DeviceManager to look up device information by port chain
    DeviceManager& deviceManager = DeviceManager::getInstance();
    QList<DeviceInfo> devices = deviceManager.getDevicesByPortChain(portChain);
    
    if (devices.isEmpty()) {
        qCWarning(log_ui_camera) << "No devices found for port chain:" << portChain;
        return QCameraDevice();
    }

    qCDebug(log_ui_camera) << "Found" << devices.size() << "device(s) for port chain:" << portChain;

    // Look for a device that has camera information
    DeviceInfo selectedDevice;
    for (const DeviceInfo& device : devices) {
        if (!device.cameraDeviceId.isEmpty() || !device.cameraDevicePath.isEmpty()) {
            selectedDevice = device;
            qCDebug(log_ui_camera) << "Found device with camera info:" 
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
        qCDebug(log_ui_camera) << "Extracted target short identifier:" << targetShortId;
    }

    QList<QCameraDevice> availableCameras = getAvailableCameraDevices();

    for (const QCameraDevice& camera : availableCameras) {
        QString cameraId = QString::fromUtf8(camera.id());
        QString cameraDescription = camera.description();

        qCDebug(log_ui_camera) << "Checking camera device:" << cameraDescription 
                 << "ID:" << cameraId;

        // Try multiple matching strategies
        // Strategy 1: Short identifier match (preferred method)
        if (!targetShortId.isEmpty() && cameraId.contains(targetShortId, Qt::CaseInsensitive)) {
            qCDebug(log_ui_camera) << "Matched camera by short identifier:" << targetShortId;
            deviceManager.setCurrentSelectedDevice(selectedDevice);
            return camera;
        }
        // Strategy 2: Direct ID match
        if (!selectedDevice.cameraDeviceId.isEmpty() && cameraId == selectedDevice.cameraDeviceId) {
            qCDebug(log_ui_camera) << "Matched camera by exact ID:" << selectedDevice.cameraDeviceId;
            deviceManager.setCurrentSelectedDevice(selectedDevice);
            return camera;
        }
        // Strategy 3: Path match (if applicable)
        if (!selectedDevice.cameraDevicePath.isEmpty() && cameraId.contains(selectedDevice.cameraDevicePath, Qt::CaseInsensitive)) {
            qCDebug(log_ui_camera) << "Matched camera by path:" << selectedDevice.cameraDevicePath;
            deviceManager.setCurrentSelectedDevice(selectedDevice);
            return camera;
        }
    }

    qCWarning(log_ui_camera) << "Could not find matching Qt camera device for port chain:" << portChain;
    return QCameraDevice();
}

bool CameraManager::initializeCameraWithVideoOutput(QVideoWidget* videoOutput)
{
    qCDebug(log_ui_camera) << "Initializing camera with video output";
    
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
        qCDebug(log_ui_camera) << "Camera already active with device:" << m_currentCameraDevice.description() 
                 << "at port chain:" << m_currentCameraPortChain;
        return true;
    }
    
    bool switchSuccess = false;
    
    // First priority: Check for port chain in global settings
    QString portChain = GlobalSetting::instance().getOpenterfacePortChain();
    
    if (!portChain.isEmpty()) {
        qCDebug(log_ui_camera) << "Found port chain in global settings:" << portChain;
        
        QCameraDevice matchedCamera = findMatchingCameraDevice(portChain);
        
        if (!matchedCamera.isNull()) {
            switchSuccess = switchToCameraDevice(matchedCamera, portChain);
            if (switchSuccess) {
                qCDebug(log_ui_camera) << "✓ Successfully switched to camera using port chain:" << portChain;
                qCDebug(log_ui_camera) << "✓ Selected camera:" << matchedCamera.description();
            } else {
                qCWarning(log_ui_camera) << "Failed to switch to matched camera device:" << matchedCamera.description();
            }
        } else {
            qCDebug(log_ui_camera) << "No matching camera device found for port chain:" << portChain;
        }
    } else {
        qCDebug(log_ui_camera) << "No port chain found in global settings, using fallback methods";
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
                qCDebug(log_ui_camera) << "Camera switched to device with description 'Openterface' (no port chain tracked)";
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
        qCDebug(log_ui_camera) << "Cannot deactivate camera with empty port chain";
        return false;
    }
    
    // Check if we have an active camera and if its port chain matches
    if (m_currentCameraPortChain.isEmpty()) {
        qCDebug(log_ui_camera) << "No current camera port chain tracked, cannot compare for deactivation";
        return false;
    }
    
    if (m_currentCameraPortChain != portChain) {
        qCDebug(log_ui_camera) << "Current camera port chain" << m_currentCameraPortChain 
                 << "does not match unplugged device port chain" << portChain;
        return false;
    }
    
    qCInfo(log_ui_camera) << "Deactivating camera for unplugged device at port chain:" << portChain;
    
    try {
        // Stop and reset the camera
        if (m_camera && m_camera->isActive()) {
            qCDebug(log_ui_camera) << "Stopping active camera due to device unplugging";
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
    qCDebug(log_ui_camera) << "Attempting auto-switch to new device with port chain:" << portChain;
    
    // Check if we currently have an active camera device
    if (hasActiveCameraDevice()) {
        qCDebug(log_ui_camera) << "Active camera device detected, skipping auto-switch to preserve user selection";
        return false;
    }
    
    qCDebug(log_ui_camera) << "No active camera device found, attempting to switch to new device";
    
    // Try to find a matching camera device for the port chain
    QCameraDevice matchedCamera = findMatchingCameraDevice(portChain);
    
    if (matchedCamera.isNull()) {
        qCDebug(log_ui_camera) << "No matching camera device found for port chain:" << portChain;
        return false;
    }
    
    qCDebug(log_ui_camera) << "Found matching camera device:" << matchedCamera.description() << "for port chain:" << portChain;
    
    // Switch to the new camera device
    bool switchSuccess = switchToCameraDevice(matchedCamera, portChain);
    
    if (switchSuccess) {
        qCDebug(log_ui_camera) << "✓ Successfully auto-switched to new camera device:" << matchedCamera.description() << "at port chain:" << portChain;
        
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
    
    qCDebug(log_ui_camera) << "Attempting to switch to camera by port chain:" << portChain;
    
    try {
        QCameraDevice targetCamera = findMatchingCameraDevice(portChain);
        
        if (targetCamera.isNull()) {
            qCWarning(log_ui_camera) << "No matching camera found for port chain:" << portChain;
            return false;
        }
        
        qCDebug(log_ui_camera) << "Found matching camera device:" << targetCamera.description() << "for port chain:" << portChain;
        
        bool switchSuccess = switchToCameraDevice(targetCamera, portChain);
        if (switchSuccess) {
            qCDebug(log_ui_camera) << "Successfully switched to camera device:" << targetCamera.description() << "with port chain:" << portChain;
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