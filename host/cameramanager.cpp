#include "cameramanager.h"

#include <QLoggingCategory>
#include <QSettings>
#include <QMediaDevices>
#include <QRegularExpression>
#include "global.h"
#include "video/videohid.h"
#include "serial/SerialPortManager.h"
#include <QVideoWidget>



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
    qCDebug(log_ui_camera) << "Found" << m_availableCameraDevices.size() << "available camera devices";
    
    // Display all camera device IDs for debugging
    displayAllCameraDeviceIds();
    
    // Connect to SerialPortManager signals for device coordination
    SerialPortManager& serialManager = SerialPortManager::getInstance();
    connect(&serialManager, &SerialPortManager::cameraDeviceAvailable, 
            this, &CameraManager::onCameraDevicePathAvailable);
    connect(&serialManager, &SerialPortManager::completeDeviceSelected,
            this, &CameraManager::onDeviceChanged);
    connect(&serialManager, &SerialPortManager::deviceInterfacesActivated,
            this, &CameraManager::onDeviceChanged);
    
    qCDebug(log_ui_camera) << "Connected to SerialPortManager signals for device coordination";
    
    // Connect to device changes - QMediaDevices in Qt 6 uses static signals
    // Note: In Qt 6, connecting to static signals may require a different approach
    // For now, we'll rely on manual refresh or external notifications for device changes
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
    m_camera.reset(new QCamera(cameraDevice));
    setupConnections();
    m_captureSession.setCamera(m_camera.get());
    m_captureSession.setImageCapture(m_imageCapture.get());
    
    // Update current device tracking
    m_currentCameraDevice = cameraDevice;
    m_currentCameraDeviceId = QString::fromUtf8(cameraDevice.id());
    
    qCDebug(log_ui_camera) << "Camera device set to:" << cameraDevice.description();
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
    VideoHid::getInstance().start();
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
    if (m_camera) {
        connect(m_camera.get(), &QCamera::activeChanged, this, [this](bool active) {
            qCDebug(log_ui_camera) << "Camera active state changed to:" << active;
            
            if (active) {
                configureResolutionAndFormat();
            }

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

void CameraManager::loadCameraSettingAndSetCamera()
{
    qCDebug(log_ui_camera) << "Load camera setting and set camera";
    QSettings settings("Techxartisan", "Openterface");
    QString configDeviceDescription = settings.value("camera/device", "").toString();
    QString configDeviceId = settings.value("camera/deviceId", "").toString();
    
    // Get the current camera description
    QString currentDeviceDescription = m_currentCameraDevice.description();
    QString currentDeviceId = m_currentCameraDeviceId;
    
    qCDebug(log_ui_camera) << "Config device description:" << configDeviceDescription;
    qCDebug(log_ui_camera) << "Config device ID:" << configDeviceId;
    qCDebug(log_ui_camera) << "Current device description:" << currentDeviceDescription;
    qCDebug(log_ui_camera) << "Current device ID:" << currentDeviceId;

    // Check if we need to switch cameras
    bool needsSwitch = false;
    
    // First try by device ID if available
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
        bool switchSuccess = false;
        
        // Try switching by device ID first
        if (!configDeviceId.isEmpty()) {
            switchSuccess = switchToCameraDeviceById(configDeviceId);
            if (switchSuccess) {
                qCDebug(log_ui_camera) << "Camera switched by ID to:" << configDeviceId;
            }
        }
        
        // If ID switching failed, try by description
        if (!switchSuccess && !configDeviceDescription.isEmpty()) {
            switchSuccess = switchToCameraDeviceByDescription(configDeviceDescription);
            if (switchSuccess) {
                qCDebug(log_ui_camera) << "Camera switched by description to:" << configDeviceDescription;
            }
        }
        
        // If configured device switching failed or no config available, use port chain coordination
        if (!switchSuccess) {
            qCDebug(log_ui_camera) << "Falling back to port chain coordinated camera selection";
            switchSuccess = ensurePortChainCoordination();
            if (!switchSuccess) {
                qCDebug(log_ui_camera) << "Port chain coordination failed, using automatic selection";
                switchSuccess = switchToAvailableCamera();
            }
            if (switchSuccess) {
                qDebug() << "Selected camera:" << m_currentCameraDevice.description();
            }
        }
        
        if (!switchSuccess) {
            qCWarning(log_ui_camera) << "No suitable camera found";
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
    
    // Emit signals
    emit cameraDeviceChanged(cameraDevice, previousDevice);
    emit cameraDeviceSwitched(QString::fromUtf8(previousDevice.id()), QString::fromUtf8(cameraDevice.id()));
    emit cameraDeviceConnected(cameraDevice);
    
    if (previousDevice.isNull() == false) {
        emit cameraDeviceDisconnected(previousDevice);
    }
    
    qDebug() << "Camera device switch successful to:" << cameraDevice.description();
    return true;
}

bool CameraManager::switchToCameraDeviceById(const QString& deviceId)
{
    QList<QCameraDevice> devices = getAvailableCameraDevices();
    for (const QCameraDevice& device : devices) {
        if (QString::fromUtf8(device.id()) == deviceId) {
            return switchToCameraDevice(device);
        }
    }
    
    qCWarning(log_ui_camera) << "Camera device not found with ID:" << deviceId;
    return false;
}

bool CameraManager::switchToCameraDeviceByDescription(const QString& description)
{
    QList<QCameraDevice> devices = getAvailableCameraDevices();
    for (const QCameraDevice& device : devices) {
        if (device.description() == description) {
            return switchToCameraDevice(device);
        }
    }
    
    qCWarning(log_ui_camera) << "Camera device not found with description:" << description;
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

void CameraManager::setCameraDeviceFromDeviceInfo(const DeviceInfo& deviceInfo)
{
    if (!deviceInfo.hasCameraDevice()) {
        qCWarning(log_ui_camera) << "DeviceInfo does not have camera device information";
        return;
    }
    
    QString cameraDevicePath = deviceInfo.cameraDevicePath;
    QString cameraDeviceId = deviceInfo.cameraDeviceId;
    
    qCDebug(log_ui_camera) << "Setting camera from DeviceInfo - Path:" << cameraDevicePath 
                          << "ID:" << cameraDeviceId;
    
    // Try to find the camera device by ID or path
    QList<QCameraDevice> devices = getAvailableCameraDevices();
    QCameraDevice targetDevice;
    
    for (const QCameraDevice& device : devices) {
        QString deviceIdString = QString::fromUtf8(device.id());
        
        // Extract the shorter identifier pattern from camera device ID
        // Pattern: extract something like "7&1FF4451E&2&0000" from the full camera ID
        QString cameraShortId = extractShortIdentifier(deviceIdString);
        QString deviceShortId = extractShortIdentifier(cameraDeviceId);
        
        qCDebug(log_ui_camera) << "Camera short ID:" << cameraShortId << "Device short ID:" << deviceShortId;
        
        // Try matching by the extracted short identifier first
        if (!deviceShortId.isEmpty() && !cameraShortId.isEmpty() && 
            cameraShortId.contains(deviceShortId, Qt::CaseInsensitive)) {
            targetDevice = device;
            qCDebug(log_ui_camera) << "Found match using short identifier:" << deviceShortId;
            break;
        }
        
        // Try matching by device ID first
        if (!cameraDeviceId.isEmpty() && deviceIdString.contains(cameraDeviceId, Qt::CaseInsensitive)) {
            targetDevice = device;
            qCDebug(log_ui_camera) << "Found match using full device ID";
            break;
        }
        
        // Fallback: try matching by device path
        if (!cameraDevicePath.isEmpty() && deviceIdString.contains(cameraDevicePath, Qt::CaseInsensitive)) {
            targetDevice = device;
            qCDebug(log_ui_camera) << "Found match using device path";
            break;
        }
    }
    
    // If no specific match found, fall back to automatic selection
    if (targetDevice.isNull()) {
        qCDebug(log_ui_camera) << "No specific device match found, falling back to automatic selection";
        targetDevice = findBestAvailableCamera();
    }
    
    if (targetDevice.isNull()) {
        qCWarning(log_ui_camera) << "Could not find any camera device";
        return;
    }
    
    qDebug() << "Found camera device:" << targetDevice.description();
    switchToCameraDevice(targetDevice);
}

bool CameraManager::switchToCameraFromDeviceInfo(const DeviceInfo& deviceInfo)
{
    if (!deviceInfo.hasCameraDevice()) {
        qCWarning(log_ui_camera) << "DeviceInfo does not have camera device information";
        return false;
    }
    
    setCameraDeviceFromDeviceInfo(deviceInfo);
    return m_currentCameraDevice.isNull() == false;
}

DeviceInfo CameraManager::getCurrentCameraAsDeviceInfo() const
{
    DeviceInfo deviceInfo;
    
    if (!m_currentCameraDevice.isNull()) {
        deviceInfo.cameraDevicePath = QString::fromUtf8(m_currentCameraDevice.id());
        deviceInfo.cameraDeviceId = QString::fromUtf8(m_currentCameraDevice.id());
        deviceInfo.lastSeen = QDateTime::currentDateTime();
        
        // Add some additional info to platformSpecific
        QVariantMap cameraData;
        cameraData["description"] = m_currentCameraDevice.description();
        cameraData["id"] = QString::fromUtf8(m_currentCameraDevice.id());
        cameraData["position"] = static_cast<int>(m_currentCameraDevice.position());
        cameraData["isDefault"] = m_currentCameraDevice.isDefault();
        deviceInfo.platformSpecific["camera"] = cameraData;
    }
    
    return deviceInfo;
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
        // Multiple devices available - connect to the first one
        qCDebug(log_ui_camera) << "Multiple camera devices found (" << devices.size() << "), selecting first:" << devices.first().description();
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

void CameraManager::onDeviceChanged(const DeviceInfo& newDevice)
{
    qCDebug(log_ui_camera) << "Device changed signal received - Port chain:" << newDevice.portChain;
    
    if (newDevice.hasCameraDevice()) {
        qDebug() << "New device has camera, switching camera device with port chain coordination";
        
        // First try to switch using the DeviceInfo directly
        bool switchSuccess = switchToCameraFromDeviceInfo(newDevice);
        
        // If that fails, try port chain coordination
        if (!switchSuccess && !newDevice.portChain.isEmpty()) {
            switchSuccess = switchToCameraFromPortChain(newDevice.portChain);
        }
        
        if (!switchSuccess) {
            qCWarning(log_ui_camera) << "Failed to switch camera for device change";
        }
    } else {
        qCDebug(log_ui_camera) << "New device does not have camera information";
        
        // Even if this device doesn't have a camera, ensure coordination with serial port
        ensurePortChainCoordination();
    }
}

void CameraManager::onCameraDevicePathAvailable(const QString& cameraDevicePath)
{
    qCDebug(log_ui_camera) << "Camera device path available:" << cameraDevicePath;
    
    if (cameraDevicePath.isEmpty()) {
        qCWarning(log_ui_camera) << "Empty camera device path received";
        return;
    }
    
    // Refresh available devices first
    refreshAvailableCameraDevices();
    
    // First try to find the camera device that matches the specific path
    QList<QCameraDevice> devices = getAvailableCameraDevices();
    QCameraDevice targetDevice;
    
    for (const QCameraDevice& device : devices) {
        QString deviceIdString = QString::fromUtf8(device.id());
        
        // Extract the shorter identifier pattern from both IDs
        QString cameraShortId = extractShortIdentifier(deviceIdString);
        QString pathShortId = extractShortIdentifier(cameraDevicePath);
        
        qCDebug(log_ui_camera) << "Camera short ID:" << cameraShortId << "Path short ID:" << pathShortId;
        
        // Try matching by the extracted short identifier first
        if (!pathShortId.isEmpty() && !cameraShortId.isEmpty() && 
            cameraShortId.contains(pathShortId, Qt::CaseInsensitive)) {
            targetDevice = device;
            qCDebug(log_ui_camera) << "Found exact short identifier match:" << pathShortId;
            break;
        }
        
        // Check for direct path matching as fallback
        if (deviceIdString.contains(cameraDevicePath, Qt::CaseInsensitive)) {
            targetDevice = device;
            qCDebug(log_ui_camera) << "Found exact path match:" << device.description();
            break;
        }
    }
    
    // If no exact match, first try port chain coordination before fallback
    if (targetDevice.isNull()) {
        qCDebug(log_ui_camera) << "No exact path match, trying port chain coordination first";
        
        // Try to coordinate with SerialPortManager's current device
        if (ensurePortChainCoordination()) {
            qCDebug(log_ui_camera) << "Port chain coordination successful";
            return;  // Successfully coordinated with current serial device
        }
        
        // If coordination fails, use the new logic to find the best available camera
        qCDebug(log_ui_camera) << "Port chain coordination failed, trying best available camera";
        targetDevice = findBestAvailableCamera();
    }
    
    if (!targetDevice.isNull()) {
        qDebug() << "Switching to camera device:" << targetDevice.description();
        switchToCameraDevice(targetDevice);
    } else {
        qCWarning(log_ui_camera) << "Could not find any suitable camera device for path:" << cameraDevicePath;
    }
}

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

// Port chain coordination with SerialPortManager
bool CameraManager::switchToCameraFromPortChain(const QString& portChain)
{
    qCDebug(log_ui_camera) << "Switching camera based on port chain:" << portChain;
    
    // Get the device info from SerialPortManager for this port chain
    SerialPortManager& serialManager = SerialPortManager::getInstance();
    QList<DeviceInfo> availableDevices = serialManager.getAvailableDevices();
    
    DeviceInfo targetDeviceInfo;
    for (const DeviceInfo& device : availableDevices) {
        if (device.portChain == portChain && device.hasCameraDevice()) {
            targetDeviceInfo = device;
            break;
        }
    }
    
    if (!targetDeviceInfo.isValid() || !targetDeviceInfo.hasCameraDevice()) {
        qCWarning(log_ui_camera) << "No camera device found for port chain:" << portChain;
        return false;
    }
    
    // Use existing method to switch to camera from device info
    return switchToCameraFromDeviceInfo(targetDeviceInfo);
}

bool CameraManager::ensurePortChainCoordination()
{
    qCDebug(log_ui_camera) << "Ensuring camera and serial devices share the same port chain";
    
    SerialPortManager& serialManager = SerialPortManager::getInstance();
    DeviceInfo currentSelectedDevice = serialManager.getCurrentSelectedDevice();
    
    if (!currentSelectedDevice.isValid()) {
        qCDebug(log_ui_camera) << "No device selected in SerialPortManager";
        return false;
    }
    
    QString currentPortChain = currentSelectedDevice.portChain;
    qCDebug(log_ui_camera) << "Current SerialPortManager port chain:" << currentPortChain;
    
    // Check if current camera device is from the same port chain
    DeviceInfo currentCameraDevice = getCurrentCameraAsDeviceInfo();
    if (currentCameraDevice.isValid()) {
        // Extract port chain from camera device ID using same logic as SerialPortManager
        QString cameraPortChain = extractPortChainFromDeviceId(currentCameraDevice.cameraDeviceId);
        if (cameraPortChain == currentPortChain) {
            qCDebug(log_ui_camera) << "Camera and serial devices already share the same port chain:" << currentPortChain;
            return true;
        }
    }
    
    // Switch camera to match the serial device's port chain
    return switchToCameraFromPortChain(currentPortChain);
}

QString CameraManager::extractPortChainFromDeviceId(const QString& deviceId) const
{
    // Extract port chain identifier from device ID
    // This should match the logic used in DeviceManager/SerialPortManager
    QString shortId = extractShortIdentifier(deviceId);
    if (!shortId.isEmpty()) {
        return shortId;
    }
    
    // Fallback: use a portion of the device ID as port chain identifier
    if (deviceId.length() > 20) {
        return deviceId.mid(deviceId.length() - 20, 15);
    }
    
    return deviceId;
}

bool CameraManager::coordinateWithSerialDevice()
{
    qCDebug(log_ui_camera) << "Manual port chain coordination requested";
    return ensurePortChainCoordination();
}
