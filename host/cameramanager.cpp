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
    : QObject(parent)
{
    qDebug() << "CameraManager init...";
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

void CameraManager::setCameraDevice(const QCameraDevice &cameraDevice)
{
    m_camera.reset(new QCamera(cameraDevice));
    setupConnections();
    m_captureSession.setCamera(m_camera.get());
    m_captureSession.setImageCapture(m_imageCapture.get());
    
    // Update current device tracking
    m_currentCameraDevice = cameraDevice;
    m_currentCameraDeviceId = QString::fromUtf8(cameraDevice.id());
    
    qDebug() << "Camera device set to:" << cameraDevice.description();
}

void CameraManager::setVideoOutput(QVideoWidget* videoOutput)
{
    if (videoOutput) {
        m_videoOutput = videoOutput;
        qDebug() << "Setting video output to: " << videoOutput->objectName();
        m_captureSession.setVideoOutput(videoOutput);
    } else {
        qCWarning(log_ui_camera) << "Attempted to set null video output";
    }
}

void CameraManager::startCamera()
{
    qDebug() << "Camera start..";
    if (m_camera) {
        m_camera->start();
        qDebug() << "Camera started";
    } else {
        qCWarning(log_ui_camera) << "Camera is null, cannot start";
    }
    VideoHid::getInstance().start();
}

void CameraManager::stopCamera()
{
    VideoHid::getInstance().stop();

    if (m_camera) {
        m_camera->stop();
        qDebug() << "Camera stopped";
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
    if (m_camera) {
        connect(m_camera.get(), &QCamera::activeChanged, this, [this](bool active) {
            qDebug() << "Camera active state changed to:" << active;
            
            if (active) {
                configureResolutionAndFormat();
            }

            emit cameraActiveChanged(active);
        });
        connect(m_camera.get(), &QCamera::errorOccurred, this, [this](QCamera::Error error, const QString &errorString) {
            Q_UNUSED(error);
            emit cameraError(errorString);
        });
        qDebug() << "Camera connections set up";
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
    
    // Check if we're already using this device - avoid unnecessary switching
    if (!m_currentCameraDevice.isNull() && 
        QString::fromUtf8(m_currentCameraDevice.id()) == QString::fromUtf8(cameraDevice.id())) {
        qDebug() << "Already using camera device:" << cameraDevice.description() 
                              << "- skipping switch" << cameraDevice.id();
        return true;
    }
    
    QCameraDevice previousDevice = m_currentCameraDevice;
    bool wasActive = m_camera && m_camera->isActive();
    
    qDebug() << "Switching camera from" << previousDevice.description() 
                         << "to" << cameraDevice.description();
    
    // Stop current camera if active
    if (wasActive) {
        stopCamera();
    }
    
    // Switch to new camera device
    setCameraDevice(cameraDevice);
    m_currentCameraDevice = cameraDevice;
    m_currentCameraDeviceId = QString::fromUtf8(cameraDevice.id());
    
    // Restart camera if it was previously active
    if (wasActive) {
        startCamera();
    }
    
    // Update settings to remember the new device
    QSettings settings("Techxartisan", "Openterface");
    settings.setValue("camera/device", cameraDevice.description());
    settings.setValue("camera/deviceId", QString::fromUtf8(cameraDevice.id()));
    
    // Note: Automatic port chain logging has been disabled
    // Port chains will only be managed manually through the UI
    
    // Emit signals
    emit cameraDeviceChanged(cameraDevice, previousDevice);
    emit cameraDeviceSwitched(QString::fromUtf8(previousDevice.id()), QString::fromUtf8(cameraDevice.id()));
    emit cameraDeviceConnected(cameraDevice);
    
    if (previousDevice.isNull() == false) {
        emit cameraDeviceDisconnected(previousDevice);
    }
    
    qDebug() << "Camera device switch successful to:" << cameraDevice.id() << cameraDevice.description();
    return true;
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
    return m_currentCameraDeviceId;
}

QString CameraManager::getCurrentCameraDeviceDescription() const
{
    return m_currentCameraDevice.description();
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

bool CameraManager::switchToAvailableCamera()
{
    QCameraDevice availableCamera = findBestAvailableCamera();
    if (!availableCamera.isNull()) {
        return switchToCameraDevice(availableCamera);
    }
    
    qCWarning(log_ui_camera) << "No camera devices available to switch to";
    return false;
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
    QList<QCameraDevice> devices = getAvailableCameraDevices();
    
    qDebug() << "=== Available Camera Devices ===";
    qDebug() << "Total devices found:" << devices.size();
    
    if (devices.isEmpty()) {
        qDebug() << "No camera devices available";
        return;
    }
    
    for (int i = 0; i < devices.size(); ++i) {
        const QCameraDevice& device = devices[i];
        QByteArray deviceId = device.id();
        QString deviceIdStr = QString::fromUtf8(deviceId);
        
        qDebug() << "Device" << (i + 1) << ":";
        qDebug() << "  Description:" << device.description();
        qDebug() << "  ID (raw QByteArray):" << deviceId;
        qDebug() << "  ID (as QString):" << deviceIdStr;
        qDebug() << "  ID (hex representation):" << deviceId.toHex();
        qDebug() << "  Is Default:" << device.isDefault();
        qDebug() << "  Position:" << static_cast<int>(device.position());
        qDebug() << "  ---";
    }
    
    qDebug() << "=== End Camera Device List ===";
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

bool CameraManager::initializeCameraWithVideoOutput(QVideoWidget* videoOutput)
{
    qDebug() << "Initializing camera with video output";
    
    if (!videoOutput) {
        qCWarning(log_ui_camera) << "Cannot initialize camera with null video output";
        return false;
    }
    
    // Set the video output first
    setVideoOutput(videoOutput);
    
    // Load camera settings and try to find the best camera device
    QSettings settings("Techxartisan", "Openterface");
    QString configDeviceDescription = settings.value("camera/device", "").toString();
    QString configDeviceId = settings.value("camera/deviceId", "").toString();
    
    qDebug() << "Config device description:" << configDeviceDescription;
    qDebug() << "Config device ID:" << configDeviceId;
    
    bool switchSuccess = false;
    
    // First priority: Check for port chain in global settings
    QString portChain = GlobalSetting::instance().getOpenterfacePortChain();
    
    if (!portChain.isEmpty()) {
        qDebug() << "Found port chain in global settings:" << portChain;
        
        // Use DeviceManager to look up device information by port chain
        DeviceManager& deviceManager = DeviceManager::getInstance();
        QList<DeviceInfo> devices = deviceManager.getDevicesByPortChain(portChain);
        
        if (!devices.isEmpty()) {
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
            
            if (selectedDevice.isValid() && (!selectedDevice.cameraDeviceId.isEmpty() || !selectedDevice.cameraDevicePath.isEmpty())) {
                // Try to match this device info with available Qt camera devices
                QString targetCameraId = selectedDevice.cameraDeviceId;
                QString targetCameraPath = selectedDevice.cameraDevicePath;
                
                qDebug() << "Looking for camera device with ID:" << targetCameraId << "or Path:" << targetCameraPath;
                
                // Extract short identifier from target camera ID for better matching
                QString targetShortId;
                if (!targetCameraId.isEmpty()) {
                    targetShortId = extractShortIdentifier(targetCameraId);
                    qDebug() << "Extracted target short identifier:" << targetShortId;
                }
                
                QList<QCameraDevice> availableCameras = getAvailableCameraDevices();
                QCameraDevice matchedCamera;
                
                for (const QCameraDevice& camera : availableCameras) {
                    QString cameraId = QString::fromUtf8(camera.id());
                    QString cameraDescription = camera.description();
                    
                    // Try multiple matching strategies
                    bool matched = false;
                    
                    // Strategy 1: Short identifier match (preferred method)
                    if (!targetShortId.isEmpty()) {
                        QString cameraShortId = extractShortIdentifier(cameraId);
                        if (!cameraShortId.isEmpty() && cameraShortId == targetShortId) {
                            qDebug() << "Matched camera by short identifier:" << targetShortId;
                            matched = true;
                        }
                    }
                    
                    // Strategy 2: Direct ID match (fallback)
                    if (!matched && !targetCameraId.isEmpty() && cameraId.contains(targetCameraId, Qt::CaseInsensitive)) {
                        qDebug() << "Matched camera by direct ID match";
                        matched = true;
                    }
                    
                    // Strategy 3: Path-based matching
                    if (!matched && !targetCameraPath.isEmpty()) {
                        if (cameraId.contains(targetCameraPath, Qt::CaseInsensitive) ||
                            cameraDescription.contains(targetCameraPath, Qt::CaseInsensitive)) {
                            qDebug() << "Matched camera by path reference";
                            matched = true;
                        }
                    }
                    
                    // Strategy 4: Hardware-specific patterns (for Openterface devices)
                    if (!matched && (cameraDescription.contains("MacroSilicon", Qt::CaseInsensitive) ||
                                    cameraId.contains("534D", Qt::CaseInsensitive))) {
                        qDebug() << "Matched camera by hardware pattern (MacroSilicon/534D)";
                        matched = true;
                    }
                    
                    if (matched) {
                        matchedCamera = camera;
                        qDebug() << "✓ Successfully matched camera device:" << cameraDescription;
                        break;
                    }
                }
                
                if (!matchedCamera.isNull()) {
                    switchSuccess = switchToCameraDevice(matchedCamera);
                    if (switchSuccess) {
                        qDebug() << "✓ Successfully switched to camera using port chain:" << portChain;
                        qDebug() << "✓ Selected camera:" << matchedCamera.description();
                        
                        // Update the selected device in DeviceManager
                        deviceManager.setCurrentSelectedDevice(selectedDevice);
                    } else {
                        qCWarning(log_ui_camera) << "Failed to switch to matched camera device:" << matchedCamera.description();
                    }
                } else {
                    qCWarning(log_ui_camera) << "Could not find matching Qt camera device for port chain:" << portChain;
                }
            } else {
                qCWarning(log_ui_camera) << "No device with camera information found for port chain:" << portChain;
            }
        } else {
            qCWarning(log_ui_camera) << "No devices found for port chain:" << portChain;
        }
    } else {
        qDebug() << "No port chain found in global settings, using fallback methods";
    }
    
    // Fallback: Traditional camera selection logic
    if (!switchSuccess) {
        // Get current device info for comparison
        QString currentDeviceDescription = m_currentCameraDevice.description();
        QString currentDeviceId = m_currentCameraDeviceId;
        
        bool needsSwitch = false;
        
        // Try by device ID if available
        if (!configDeviceId.isEmpty() && configDeviceId != currentDeviceId) {
            needsSwitch = true;
        }
        // Fallback to description matching
        else if (!configDeviceDescription.isEmpty() && currentDeviceDescription != configDeviceDescription) {
            needsSwitch = true;
        }
        // If no configuration is available or current device is null, switch to best available
        else if ((configDeviceDescription.isEmpty() && configDeviceId.isEmpty()) || m_currentCameraDevice.isNull()) {
            needsSwitch = true;
        }
        
        if (needsSwitch) {
            // Try switching by device ID if available
            if (!configDeviceId.isEmpty()) {
                switchSuccess = switchToCameraDeviceById(configDeviceId);
                if (switchSuccess) {
                    qDebug() << "Camera switched by ID to:" << configDeviceId;
                }
            }
            
            // If configured device switching failed, use automatic selection as last resort
            if (!switchSuccess) {
                qDebug() << "Configured camera not found, using automatic selection";
                switchSuccess = switchToAvailableCamera();
                if (switchSuccess) {
                    qDebug() << "Selected camera:" << m_currentCameraDevice.description();
                }
            }
        } else {
            qDebug() << "Current camera matches configuration. No change needed.";
            switchSuccess = true; // Consider this a success since no change is needed
        }
    }
    
    // If we still don't have a camera device, return false
    if (m_currentCameraDevice.isNull()) {
        qCWarning(log_ui_camera) << "No camera device available for initialization";
        return false;
    }
    
    // If we have a camera device but no camera object, create it
    if (!m_camera) {
        qDebug() << "Creating camera object for device:" << m_currentCameraDevice.description();
        setCameraDevice(m_currentCameraDevice);
    }
    
    // Start the camera
    startCamera();
    
    qDebug() << "✓ Camera initialized and started with video output:" << m_currentCameraDevice.description();
    return true;
}