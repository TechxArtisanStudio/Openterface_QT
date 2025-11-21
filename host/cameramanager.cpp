#include "cameramanager.h"
#include "host/multimediabackend.h"

// Include FFmpeg backend for all platforms (Windows now supported via DirectShow)
#include "host/backend/ffmpegbackendhandler.h"

// Include GStreamer backend for non-Windows platforms only
#ifndef Q_OS_WIN
#include "host/backend/gstreamerbackendhandler.h"
#endif

// Include Qt backend for all platforms
#include "host/backend/qtbackendhandler.h"
#include "host/backend/qtmultimediabackendhandler.h"

#include "ui/videopane.h"

#include <QLoggingCategory>
#include <QSettings>
#include <QMediaDevices>
#include <QRegularExpression>
#include <QDateTime>
#include "global.h"
#include "video/videohid.h"
#include "../ui/globalsetting.h"
#include "../device/DeviceManager.h"
#include "../device/HotplugMonitor.h"
#include "../device/HotplugMonitor.h"
#include <QGraphicsVideoItem>
#include <QTimer>
#include <QThread>
#include <algorithm>
#include <QSet>

Q_LOGGING_CATEGORY(log_ui_camera, "opf.ui.camera")

CameraManager::CameraManager(QObject *parent)
    : QObject(parent), m_graphicsVideoOutput(nullptr), m_video_width(0), m_video_height(0)
{
    qCDebug(log_ui_camera) << "CameraManager init...";
    
    // Initialize camera device to null state
    m_currentCameraDevice = QCameraDevice();
    m_currentCameraDeviceId.clear();
    m_currentCameraPortChain.clear();
    m_currentRecordingPath.clear();

    initializeBackendHandler();
    // Setup Windows-specific hotplug monitoring
    setupWindowsHotplugMonitoring();
    
    
    // Connect to hotplug monitor for all platforms
    connectToHotplugMonitor();  // Disabled to avoid clash with MainWindow camera initialization
    
    // Initialize available camera devices
    m_availableCameraDevices = getAvailableCameraDevices();
    qCDebug(log_ui_camera) << "Found" << m_availableCameraDevices.size() << "available camera devices";
}

CameraManager::~CameraManager() {
    // Disconnect from hotplug monitoring
    disconnectFromHotplugMonitor();
}

bool CameraManager::isWindowsPlatform()
{
#ifdef Q_OS_WIN
    return true;
#else
    return false;
#endif
}

bool CameraManager::isGStreamerBackend() const
{
    return m_backendHandler && m_backendHandler->getBackendType() == MultimediaBackendType::GStreamer;
}

bool CameraManager::isFFmpegBackend() const
{
    return m_backendHandler && m_backendHandler->getBackendType() == MultimediaBackendType::FFmpeg;
}

bool CameraManager::isQtBackend() const
{
    return m_backendHandler && m_backendHandler->getBackendType() == MultimediaBackendType::Qt;
}

FFmpegBackendHandler* CameraManager::getFFmpegBackend() const
{
    // FFmpeg backend now supported on all platforms (Windows via DirectShow)
    if (isFFmpegBackend() && m_backendHandler) {
        try {
            // Use dynamic_cast for safer type checking
            return dynamic_cast<FFmpegBackendHandler*>(m_backendHandler.get());
        } catch (const std::exception& e) {
            qCCritical(log_ui_camera) << "Exception during FFmpeg backend cast:" << e.what();
        }
    }
    return nullptr;
}

MultimediaBackendHandler* CameraManager::getBackendHandler() const
{
    return m_backendHandler.get();
}

void CameraManager::initializeBackendHandler()
{
    qCDebug(log_ui_camera) << "Initializing multimedia backend handler";
    try {
        m_backendHandler = MultimediaBackendFactory::createAutoDetectedHandler(this);
        if (m_backendHandler) {
            qCDebug(log_ui_camera) << "Backend handler initialized:" << m_backendHandler->getBackendName();
            qCDebug(log_ui_camera) << "Backend handler type:" << static_cast<int>(m_backendHandler->getBackendType());
            qCDebug(log_ui_camera) << "Backend handler pointer:" << m_backendHandler.get();
            
            // Connect backend signals
            connect(m_backendHandler.get(), &MultimediaBackendHandler::backendMessage,
                    this, [](const QString& message) {
                        qCDebug(log_ui_camera) << "Backend message:" << message;
                    });
            
            connect(m_backendHandler.get(), &MultimediaBackendHandler::backendWarning,
                    this, [](const QString& warning) {
                        qCWarning(log_ui_camera) << "Backend warning:" << warning;
                    });
            
            connect(m_backendHandler.get(), &MultimediaBackendHandler::backendError,
                    this, [this](const QString& error) {
                        qCCritical(log_ui_camera) << "Backend error:" << error;
                        emit cameraError(error);
                    });
            
            // Connect FFmpeg-specific signals if this is an FFmpeg backend
            if (auto ffmpegHandler = qobject_cast<FFmpegBackendHandler*>(m_backendHandler.get())) {
                qCDebug(log_ui_camera) << "Setting up FFmpeg backend specific signal connections";
                
                connect(ffmpegHandler, &FFmpegBackendHandler::deviceConnectionChanged,
                        this, [this](const QString& devicePath, bool connected) {
                            qCDebug(log_ui_camera) << "FFmpeg device connection changed:" << devicePath << "connected:" << connected;
                            if (!connected) {
                                // Handle device disconnection
                                qCWarning(log_ui_camera) << "FFmpeg backend reports device disconnected:" << devicePath;
                                // Try to find and switch to an available camera device
                                handleFFmpegDeviceDisconnection(devicePath);
                            }
                        });
                
                // Connect to new enhanced hotplug signals
                connect(ffmpegHandler, &FFmpegBackendHandler::deviceActivated,
                        this, [this](const QString& devicePath) {
                            qCInfo(log_ui_camera) << "FFmpeg device activated:" << devicePath;
                            emit cameraActiveChanged(true);
                        });
                        
                connect(ffmpegHandler, &FFmpegBackendHandler::deviceDeactivated,
                        this, [this](const QString& devicePath) {
                            qCInfo(log_ui_camera) << "FFmpeg device deactivated:" << devicePath;
                            emit cameraActiveChanged(false);
                        });
                        
                connect(ffmpegHandler, &FFmpegBackendHandler::waitingForDevice,
                        this, [this](const QString& devicePath) {
                            qCInfo(log_ui_camera) << "FFmpeg waiting for device:" << devicePath;
                            emit cameraActiveChanged(false);
                        });
                
                connect(ffmpegHandler, &FFmpegBackendHandler::captureError,
                        this, [this](const QString& error) {
                            qCWarning(log_ui_camera) << "FFmpeg capture error:" << error;
                            emit cameraError("FFmpeg: " + error);
                        });
                
                qCDebug(log_ui_camera) << "FFmpeg backend signal connections established";
            }
            
            // Qt backend setup - no longer needed for FFmpeg-only approach
#ifdef Q_OS_WIN
            qCDebug(log_ui_camera) << "Windows platform - using FFmpeg backend";
#endif
        } else {
            qCCritical(log_ui_camera) << "Failed to create backend handler - returned nullptr";
        }
    } catch (const std::exception& e) {
        qCCritical(log_ui_camera) << "Exception initializing backend handler:" << e.what();
    } catch (...) {
        qCCritical(log_ui_camera) << "Unknown exception initializing backend handler";
    }
}

void CameraManager::updateBackendHandler()
{
    qCDebug(log_ui_camera) << "Updating multimedia backend handler";
    
    // Store the current backend type for comparison
    MultimediaBackendType currentType = m_backendHandler ? m_backendHandler->getBackendType() : MultimediaBackendType::Unknown;
    MultimediaBackendType newType = MultimediaBackendFactory::detectBackendType();
    
    // Only recreate if the backend type has changed
    if (currentType != newType) {
        qCDebug(log_ui_camera) << "Backend type changed from" << MultimediaBackendFactory::backendTypeToString(currentType)
                               << "to" << MultimediaBackendFactory::backendTypeToString(newType);
        
        // Disconnect old handler signals
        if (m_backendHandler) {
            disconnect(m_backendHandler.get(), nullptr, this, nullptr);
        }
        
        // Create new handler
        initializeBackendHandler();
    } else {
        qCDebug(log_ui_camera) << "Backend type unchanged, keeping current handler";
    }
}

// void CameraManager::setCamera(const QCameraDevice &cameraDevice, QGraphicsVideoItem* videoOutput)
// {
//     qCDebug(log_ui_camera) << "Set Camera to graphics videoOutput: " << videoOutput << ", device name: " << cameraDevice.description();
//     setCameraDevice(cameraDevice);

//     setVideoOutput(videoOutput);

//     queryResolutions();

//     // Set camera format
//     startCamera();
// }

// Windows-specific direct QCamera approach
// REMOVED: setCamera() - QCamera-dependent method


// REMOVED: setCameraDevice() - QCamera-dependent method


// Deprecated method for initializing camera with video output
// This method is kept for compatibility but should be replaced with the new methods
// that handle port chain tracking and improved device management
void CameraManager::setVideoOutput(QGraphicsVideoItem* videoOutput)
{
    if (videoOutput) {
        m_graphicsVideoOutput = videoOutput;
        qDebug() << "Setting graphics video output for FFmpeg backend";
        
        // Connect video output to FFmpeg backend if available
        if (m_backendHandler && isFFmpegBackend()) {
            FFmpegBackendHandler* ffmpeg = getFFmpegBackend();
            if (ffmpeg) {
                ffmpeg->setVideoOutput(videoOutput);
                qDebug() << "Graphics video output successfully connected to FFmpeg backend";
            }
        } else {
            qCWarning(log_ui_camera) << "FFmpeg backend not available for video output";
        }
    } else {
        qCWarning(log_ui_camera) << "Attempted to set null graphics video output";
    }
}

void CameraManager::startCamera()
{
    qCDebug(log_ui_camera) << "Starting camera with FFmpeg backend";
    
    try {
        // FFmpeg backend only - no QCamera
        if (!m_backendHandler) {
            qCWarning(log_ui_camera) << "No backend handler available, cannot start camera";
            return;
        }
        
        if (!isFFmpegBackend()) {
            qCWarning(log_ui_camera) << "Only FFmpeg backend is supported";
            return;
        }
        
        // Start FFmpeg backend camera
        m_backendHandler->startCamera();
        
        emit cameraActiveChanged(true);
        qCDebug(log_ui_camera) << "FFmpeg backend camera started successfully";
        
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
    qCDebug(log_ui_camera) << "Stopping camera with FFmpeg backend";
    
    try {
        // Stop VideoHid first
        VideoHid::getInstance().stop();

        if (m_backendHandler) {
            qCDebug(log_ui_camera) << "Stopping FFmpeg backend camera";
            m_backendHandler->stopCamera();
            emit cameraActiveChanged(false);
            qCDebug(log_ui_camera) << "FFmpeg backend camera stopped successfully";
        } else {
            qCWarning(log_ui_camera) << "No backend handler available";
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
    if (m_backendHandler && isFFmpegBackend()) {
        FFmpegBackendHandler* ffmpeg = getFFmpegBackend();
        if (ffmpeg) {
            QString actualFile = file;
            if (actualFile.isEmpty()) {
                // Generate path like original Qt backend
                QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
                QString picturesPath = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
                QString customFolderPath;
                if (picturesPath.isEmpty()) {
                    customFolderPath = QDir::homePath() + "/Pictures";
                } else {
                    customFolderPath = picturesPath + "/openterface";
                }
                QDir dir(customFolderPath);
                if (!dir.exists() && !dir.mkpath(customFolderPath)) {
                    qCWarning(log_ui_camera) << "Failed to create directory:" << customFolderPath;
                    return;
                }
                actualFile = customFolderPath + "/" + timestamp + ".png";
            }
            ffmpeg->takeImage(actualFile);
            emit lastImagePath(actualFile);
        }
    } else {
        qCWarning(log_ui_camera) << "Image capture not supported for current backend";
    }
}

void CameraManager::takeAreaImage(const QString& file, const QRect& captureArea)
{
    if (m_backendHandler && isFFmpegBackend()) {
        FFmpegBackendHandler* ffmpeg = getFFmpegBackend();
        if (ffmpeg) {
            QString actualFile = file;
            if (actualFile.isEmpty()) {
                // Generate path like original Qt backend
                QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
                QString picturesPath = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
                QString customFolderPath;
                if (picturesPath.isEmpty()) {
                    customFolderPath = QDir::homePath() + "/Pictures";
                } else {
                    customFolderPath = picturesPath + "/openterface";
                }
                QDir dir(customFolderPath);
                if (!dir.exists() && !dir.mkpath(customFolderPath)) {
                    qCWarning(log_ui_camera) << "Failed to create directory:" << customFolderPath;
                    return;
                }
                actualFile = customFolderPath + "/" + timestamp + ".png";
            }
            ffmpeg->takeAreaImage(actualFile, captureArea);
            emit lastImagePath(actualFile);
        }
    } else {
        qCWarning(log_ui_camera) << "Area image capture not supported for current backend";
    }
}

void CameraManager::startRecording()
{
    qCInfo(log_ui_camera) << "=== START RECORDING (FFmpeg Backend) ===";
    
    // Check if recording is already in progress
    if (isRecording()) {
        qCWarning(log_ui_camera) << "Recording already in progress";
        return;
    }
    
    // Check FFmpeg backend availability
    if (!m_backendHandler || !isFFmpegBackend()) {
        qCWarning(log_ui_camera) << "FFmpeg backend not available for recording";
        emit recordingError("FFmpeg backend not available");
        return;
    }
    
    FFmpegBackendHandler* ffmpeg = getFFmpegBackend();
    if (!ffmpeg) {
        qCWarning(log_ui_camera) << "Failed to get FFmpeg backend handler";
        emit recordingError("FFmpeg backend not initialized");
        return;
    }
    
    // Generate output path with timestamp
    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QString picturesPath = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    if (picturesPath.isEmpty()) {
        picturesPath = QDir::currentPath();
    }
    QString customFolderPath = picturesPath + "/openterfaceRecordings";
    QString outputPath = customFolderPath + "/recording_" + timestamp + ".mp4";
    
    // Ensure output directory exists
    QFileInfo fileInfo(outputPath);
    QDir outputDir = fileInfo.dir();
    if (!outputDir.exists()) {
        if (!outputDir.mkpath(".")) {
            qCWarning(log_ui_camera) << "Failed to create output directory:" << outputDir.absolutePath();
            emit recordingError("Failed to create output directory");
            return;
        }
    }
    
    // Get recording settings
    QString format = GlobalSetting::instance().getRecordingOutputFormat();
    int bitrate = GlobalSetting::instance().getRecordingVideoBitrate();
    
    qCInfo(log_ui_camera) << "Starting recording to:" << outputPath 
                          << "Format:" << format << "Bitrate:" << bitrate;
    
    // Start FFmpeg recording
    bool success = ffmpeg->startRecording(outputPath, format, bitrate);
    
    if (success) {
        m_currentRecordingPath = outputPath;
        qCInfo(log_ui_camera) << "=== RECORDING STARTED SUCCESSFULLY ===";
        emit recordingStarted();
    } else {
        qCWarning(log_ui_camera) << "=== RECORDING START FAILED ===";
        emit recordingError("Failed to start FFmpeg recording");
    }
}

void CameraManager::stopRecording()
{
    qCInfo(log_ui_camera) << "=== STOP RECORDING PROCESS INITIATED ===";
    
    // Check if we're actually recording before attempting to stop
    if (!isRecording()) {
        qCWarning(log_ui_camera) << "No active recording to stop";
        qCDebug(log_ui_camera) << "=== STOP RECORDING ABORTED - NOT RECORDING ===";
        emit recordingStopped(); // Emit signal to ensure UI stays in sync
        return;
    }
    
    QString recordingPath = m_currentRecordingPath;
    qCDebug(log_ui_camera) << "Stopping recording:" << recordingPath;
    qCDebug(log_ui_camera) << "Backend type: " << (m_backendHandler ? 
                                                static_cast<int>(m_backendHandler->getBackendType()) : -1);
    // REMOVED: qCDebug(log_ui_camera) << "Camera state: " << (m_camera ? (m_camera->isActive() ? "Active" : "Inactive") : "NULL");
    
#ifdef Q_OS_WIN
    // Windows: Use QMediaRecorder via QtBackendHandler
    if (isQtBackend() && m_backendHandler) {
        try {
            // Use qobject_cast for Qt objects which is more robust in Qt environment
            QtBackendHandler* qtHandler = qobject_cast<QtBackendHandler*>(m_backendHandler.get());
            
            if (qtHandler) {
                qtHandler->stopRecording();
                qCDebug(log_ui_camera) << "Stopped recording via QtBackendHandler";
            } else {
                qCWarning(log_ui_camera) << "Failed to cast to QtBackendHandler despite isQtBackend() returning true";
            }
        } catch (const std::exception& e) {
            qCCritical(log_ui_camera) << "Exception during backend handler stop:" << e.what();
        }
    }
#else
    // Linux/macOS: Stop ONLY the active backend (FFmpeg or GStreamer)
    if (!m_backendHandler) {
        qCWarning(log_ui_camera) << "No multimedia backend handler available on non-Windows platform";
        emit recordingError("No multimedia backend available to stop recording.");
    } else {
        bool stopSuccess = false;

#ifndef Q_OS_WIN
        // Linux-specific recording stop code
        switch (m_backendHandler->getBackendType()) {
            case MultimediaBackendType::FFmpeg: {
                if (FFmpegBackendHandler* ffmpeg = qobject_cast<FFmpegBackendHandler*>(m_backendHandler.get())) {
                    // Stop the actual recording (void return type)
                    ffmpeg->stopRecording();
                    stopSuccess = true;
                    
                    qCInfo(log_ui_camera) << "Stopped recording via FFmpegBackendHandler";
                } else {
                    qCWarning(log_ui_camera) << "Backend type is FFmpeg but cast failed";
                }
                break;
            }
            case MultimediaBackendType::GStreamer: {
                if (GStreamerBackendHandler* gst = qobject_cast<GStreamerBackendHandler*>(m_backendHandler.get())) {
                    // Stop the actual recording (void return type)
                    gst->stopRecording();
                    stopSuccess = true;
                    
                    qCInfo(log_ui_camera) << "Stopped recording via GStreamerBackendHandler";
                } else {
                    qCWarning(log_ui_camera) << "Backend type is GStreamer but cast failed";
                }
                break;
            }
            default:
                qCWarning(log_ui_camera) << "Unsupported backend for Linux recording stop:" << static_cast<int>(m_backendHandler->getBackendType());
                emit recordingError("Unsupported backend for stopping recording on Linux");
                break;
        }
#else
        // Windows-specific recording stop code using QtBackendHandler
        // This code block is never executed on Windows due to the outer #ifdef
        // It's here for completeness in case someone moves the #else/#endif structure
        if (QtBackendHandler* qtHandler = qobject_cast<QtBackendHandler*>(m_backendHandler.get())) {
            stopSuccess = qtHandler->stopRecording();
            if (stopSuccess) {
                qCInfo(log_ui_camera) << "Successfully stopped recording via QtBackendHandler";
            } else {
                qCWarning(log_ui_camera) << "QtBackendHandler failed to stop recording gracefully";
            }
        } else {
            qCWarning(log_ui_camera) << "Failed to cast to QtBackendHandler for recording stop";
        }
#endif
        
        // Log the final result of the stop operation
        qCInfo(log_ui_camera) << "Recording stop result: " << (stopSuccess ? "Successful" : "Failed");
    }
#endif

    // Check if the file exists after recording is stopped
    if (!recordingPath.isEmpty()) {
        QFileInfo fileInfo(recordingPath);
        QTimer::singleShot(2000, this, [this, recordingPath, fileInfo]() {
            if (fileInfo.exists()) {
                qCInfo(log_ui_camera) << "Recording saved successfully to:" << recordingPath
                                     << "Size:" << fileInfo.size() << "bytes";
                
                if (fileInfo.size() < 1024) {  // If file is smaller than 1KB
                    qCWarning(log_ui_camera) << "Recording file is suspiciously small, may be corrupted";
                    
                    // Try to check if the file is actually a valid video
                    QFile checkFile(recordingPath);
                    if (checkFile.open(QIODevice::ReadOnly)) {
                        QByteArray header = checkFile.read(16); // Read first 16 bytes
                        checkFile.close();
                        
                        // Basic check for some common video formats
                        if (header.isEmpty() || 
                            !(header.startsWith("\x00\x00\x00") || // MP4
                              header.startsWith("RIFF") ||         // AVI
                              header.startsWith("\x1A\x45\xDF\xA3"))) { // MKV
                            
                            qCWarning(log_ui_camera) << "Recording file doesn't appear to have a valid header";
                            emit recordingError("Recording failed - output file appears to be invalid");
                            return;
                        }
                    }
                    
                    emit recordingError("Recording file may be corrupted (very small size)");
                } else {
                    // Show a success notification or open the folder
                    qCInfo(log_ui_camera) << "Recording completed successfully";
                    emit recordingStopped();
                }
            } else {
                qCWarning(log_ui_camera) << "Recording file does not exist after stopping:" << recordingPath;
                emit recordingError("Failed to save recording file");
            }
        });
    } else {
        // No path was set, but still emit the signal to update the UI
        emit recordingStopped();
    }
    
    // Clear current recording path
    m_currentRecordingPath.clear();
}

void CameraManager::pauseRecording()
{
    qCDebug(log_ui_camera) << "Pause recording (FFmpeg backend)";
    
    if (!m_backendHandler || !isFFmpegBackend()) {
        qCWarning(log_ui_camera) << "FFmpeg backend not available for pause";
        return;
    }
    
    FFmpegBackendHandler* ffmpeg = getFFmpegBackend();
    if (ffmpeg) {
        ffmpeg->pauseRecording();
        qCDebug(log_ui_camera) << "Paused recording via FFmpeg backend";
    }
}

void CameraManager::resumeRecording()
{
    qCDebug(log_ui_camera) << "Resume recording (FFmpeg backend)";
    
    if (!m_backendHandler || !isFFmpegBackend()) {
        qCWarning(log_ui_camera) << "FFmpeg backend not available for resume";
        return;
    }
    
    FFmpegBackendHandler* ffmpeg = getFFmpegBackend();
    if (ffmpeg) {
        ffmpeg->resumeRecording();
        qCDebug(log_ui_camera) << "Resumed recording via FFmpeg backend";
    }
}

bool CameraManager::isRecording() const
{
    // Check if we have an active recording path
    if (!m_currentRecordingPath.isEmpty()) {
        qCDebug(log_ui_camera) << "Recording path is set:" << m_currentRecordingPath;
        return true;
    }
    
    // Check FFmpeg backend
    if (m_backendHandler && isFFmpegBackend()) {
        FFmpegBackendHandler* ffmpeg = const_cast<CameraManager*>(this)->getFFmpegBackend();
        if (ffmpeg && ffmpeg->isRecording()) {
            qCDebug(log_ui_camera) << "FFmpeg backend reports recording active";
            return true;
        }
    }
    
    qCDebug(log_ui_camera) << "Final recording status: NOT ACTIVE";
    return false;
}

bool CameraManager::isPaused() const
{
    // Check FFmpeg backend
    if (m_backendHandler && isFFmpegBackend()) {
        FFmpegBackendHandler* ffmpeg = const_cast<CameraManager*>(this)->getFFmpegBackend();
        if (ffmpeg && ffmpeg->isRecording() && ffmpeg->isPaused()) {
            qCDebug(log_ui_camera) << "FFmpeg backend pause status: PAUSED";
            return true;
        }
    }
    
    qCDebug(log_ui_camera) << "Pause status: NOT PAUSED";
    return false;
}

// Helper methods removed - QCamera-dependent
// REMOVED: generateRecordingFilePath(), configureMediaRecorderForRecording(), 
// REMOVED: setupConnections(), configureResolutionAndFormat(),
// REMOVED: setCameraFormat(), getCameraFormat(), getCameraFormats()


// REMOVED: queryResolutions() - QCamera-dependent method



// REMOVED: getSupportedPixelFormats() - QCamera-dependent method



// REMOVED: getVideoFormat() - QCamera-dependent method


// REMOVED: getSupportedFrameRates() - QCamera-dependent method


// REMOVED: isFrameRateSupported() - QCamera-dependent method


// REMOVED: getOptimalFrameRate() - QCamera-dependent method


// REMOVED: getAllSupportedFrameRates() - QCamera-dependent method


// REMOVED: validateCameraFormat() - QCamera-dependent method


// REMOVED: getVideoFormatMap() - QCamera-dependent method


// Camera device management and switching functionality

QList<QCameraDevice> CameraManager::getAvailableCameraDevices() const
{
    QList<QCameraDevice> devices = QMediaDevices::videoInputs();
    
    // Deduplicate camera devices based on device ID
    // Windows sometimes lists the same camera twice with different names
    QMap<QByteArray, QCameraDevice> uniqueDevices;
    
    for (const QCameraDevice& device : devices) {
        QByteArray deviceId = device.id();
        QString deviceDescription = device.description();
        
        // Skip "USB2.0 HD UVC WebCam" - it's a duplicate of the Openterface device
        if (deviceDescription == "USB2.0 HD UVC WebCam") {
            qCDebug(log_ui_camera) << "Filtering out USB2.0 HD UVC WebCam device (duplicate)";
            continue;
        }
        
        if (uniqueDevices.contains(deviceId)) {
            qCDebug(log_ui_camera) << "Duplicate camera device detected:"
                                   << "'" << device.description() << "'"
                                   << "vs"
                                   << "'" << uniqueDevices[deviceId].description() << "'"
                                   << "with same ID:" << deviceId;
            qCDebug(log_ui_camera) << "Keeping first detected device:" << uniqueDevices[deviceId].description();
            // Keep the first device detected (no preference for specific names)
        } else {
            uniqueDevices[deviceId] = device;
        }
    }
    
    QList<QCameraDevice> deduplicatedDevices = uniqueDevices.values();
    
    if (deduplicatedDevices.size() < devices.size()) {
        qCDebug(log_ui_camera) << "Filtered/deduplicated" << devices.size() << "camera devices down to" << deduplicatedDevices.size();
    }
    
    return deduplicatedDevices;
}

QCameraDevice CameraManager::getCurrentCameraDevice() const
{
    return m_currentCameraDevice;
}

// REMOVED: Old single-parameter switchToCameraDevice() method
// Now using switchToCameraDevice(const QCameraDevice&, const QString& portChain) only

bool CameraManager::switchToCameraDevice(const QCameraDevice &cameraDevice, const QString& portChain)
{
    if (!isCameraDeviceValid(cameraDevice)) {
        qCWarning(log_ui_camera) << "Cannot switch to invalid camera device:" << cameraDevice.description();
        return false;
    }
    
    qCDebug(log_ui_camera) << "Switching to camera device:" << cameraDevice.description() << "with port chain:" << portChain;
    
    // Check if switching to the same device with the same port chain
    QString targetDevicePath = convertCameraDeviceToPath(cameraDevice);
    bool isSameDevice = (m_currentCameraDevice.isNull() == false) && 
                       (QString::fromUtf8(m_currentCameraDevice.id()) == QString::fromUtf8(cameraDevice.id()));
    bool isSamePortChain = (m_currentCameraPortChain == portChain);
    
    if (isSameDevice && isSamePortChain) {
        qCDebug(log_ui_camera) << "Switching to same device with same port chain, doing nothing";
        return true;
    }
    
    // Update current device tracking
    m_currentCameraDevice = cameraDevice;
    m_currentCameraDeviceId = QString::fromUtf8(cameraDevice.id());
    m_currentCameraPortChain = portChain;
    
    if (isSameDevice) {
        qCDebug(log_ui_camera) << "Switching to same device, updating port chain only";
        // Just update the port chain in the backend
        if (FFmpegBackendHandler* ffmpegHandler = dynamic_cast<FFmpegBackendHandler*>(m_backendHandler.get())) {
            ffmpegHandler->setCurrentDevicePortChain(portChain);
        }
        emit cameraDeviceSwitchComplete(cameraDevice.description());
        return true;
    }
    
    // Stop current camera if running
    bool wasRunning = false;
    if (m_backendHandler && isFFmpegBackend()) {
        FFmpegBackendHandler* ffmpegHandler = dynamic_cast<FFmpegBackendHandler*>(m_backendHandler.get());
        if (ffmpegHandler) {
            wasRunning = ffmpegHandler->isDirectCaptureRunning();
        }
    }
    
    stopCamera();
    
    // Add delay to allow device to be properly released (Windows needs this)
    if (wasRunning) {
        QThread::msleep(500); // Wait for device to be fully released
        qCDebug(log_ui_camera) << "Waited 500ms for device to be released";
    }
    
    // Configure backend with new device
    if (m_backendHandler) {
        m_backendHandler->configureCameraDevice();
        
        // Pass port chain to backend for hotplug tracking
        if (FFmpegBackendHandler* ffmpegHandler = dynamic_cast<FFmpegBackendHandler*>(m_backendHandler.get())) {
            ffmpegHandler->setCurrentDevicePortChain(portChain);
            // Set the current device path for FFmpeg backend
            QString devicePath = convertCameraDeviceToPath(cameraDevice);
            ffmpegHandler->setCurrentDevice(devicePath);
            qCDebug(log_ui_camera) << "Set device path in FFmpeg backend:" << devicePath;
        }
        
        // Start camera with new device
        startCamera();
        
        emit cameraDeviceSwitchComplete(cameraDevice.description());
        return true;
    }
    
    qCWarning(log_ui_camera) << "No backend handler available for device switch";
    return false;
}

bool CameraManager::isCameraDeviceValid(const QCameraDevice& device) const
{
    return !device.isNull() && !device.id().isEmpty();
}

// REMOVED: switchToCameraDeviceById() - QCamera-dependent method

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
    
    QString description = m_currentCameraDevice.description();
    qCDebug(log_ui_camera) << "Current camera device description:" << description;
    return description;
}

void CameraManager::refreshAvailableCameraDevices()
{
    QList<QCameraDevice> previousDevices = m_availableCameraDevices;
    m_availableCameraDevices = getAvailableCameraDevices();
    
    qCDebug(log_ui_camera) << "Refreshed camera devices, now have" << m_availableCameraDevices.size() << "devices";
    
    // Emit signal if device count changed
    if (previousDevices.size() != m_availableCameraDevices.size()) {
        emit availableCameraDevicesChanged(m_availableCameraDevices.size());
    }
}

// REMOVED: findBestAvailableCamera(), getAllCameraDescriptions(), switchToCameraDeviceById() - QCamera-dependent methods

// Removed duplicate broken getCurrentCameraDeviceDescription - keeping only correct version below





// REMOVED: isCameraDeviceValid() - QCamera-dependent method


// REMOVED: isCameraDeviceAvailable() - QCamera-dependent method


// REMOVED: getAvailableCameraDeviceDescriptions() - QCamera-dependent method


// REMOVED: getAvailableCameraDeviceIds() - QCamera-dependent method


// REMOVED: findBestAvailableCamera() - QCamera-dependent method


// REMOVED: getAllCameraDescriptions() - QCamera-dependent method

// Removed duplicate refreshAvailableCameraDevices - keeping only version above

// Note: Automatic device coordination methods have been disabled
// These methods previously handled automatic camera switching when devices changed

QString CameraManager::extractShortIdentifier(const QString& fullId) const
{
    // Extract patterns like "7&1FF4451E&2&0000" from full device IDs (Windows)
    // or video device numbers from /dev/video paths (Linux)
    
    // First, check for Linux V4L device pattern: /dev/video<number>
    QRegularExpression linuxRegex(R"(/dev/video(\d+))", QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch linuxMatch = linuxRegex.match(fullId);
    
    if (linuxMatch.hasMatch()) {
        QString shortId = linuxMatch.captured(1);
        qCDebug(log_ui_camera) << "Extracted Linux V4L short identifier:" << shortId << "from:" << fullId;
        return shortId;
    }
    
    // Look for Windows patterns with format: digit&hexdigits&digit&hexdigits
    // Examples: "7&1FF4451E&2&0000", "6&2ABC123F&1&0001", etc.
    QRegularExpression windowsRegex(R"((\d+&[A-F0-9]+&\d+&[A-F0-9]+))", QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch windowsMatch = windowsRegex.match(fullId);
    
    if (windowsMatch.hasMatch()) {
        QString shortId = windowsMatch.captured(1);
        qCDebug(log_ui_camera) << "Extracted Windows short identifier:" << shortId << "from:" << fullId;
        return shortId;
    }
    
    qCDebug(log_ui_camera) << "No short identifier pattern found in:" << fullId;
    return fullId;
    // return QString();
}

QString CameraManager::convertCameraDeviceToPath(const QCameraDevice& device) const
{
    QString deviceId = QString::fromUtf8(device.id());
    QString deviceDescription = device.description();
    
#ifdef Q_OS_WIN
    // Windows: DirectShow uses the friendly device name directly
    // Just use "video=<device_description>" format
    QString dshowDeviceName = QString("video=%1").arg(deviceDescription);
    qCDebug(log_ui_camera) << "DirectShow device:" << dshowDeviceName;
    return dshowDeviceName;
#else
    // Linux/macOS: Use V4L2 device path (usually /dev/video0, /dev/video1, etc.)
    // If the device ID is already a /dev/video path, use it directly
    if (deviceId.startsWith("/dev/video")) {
        qCDebug(log_ui_camera) << "Using V4L2 device path:" << deviceId;
        return deviceId;
    }
    
    // Try to extract video device number and construct path
    QRegularExpression re("(\\d+)");
    QRegularExpressionMatch match = re.match(deviceId);
    if (match.hasMatch()) {
        QString videoPath = "/dev/video" + match.captured(1);
        qCDebug(log_ui_camera) << "Constructed V4L2 device path:" << videoPath << "from ID:" << deviceId;
        return videoPath;
    }
    
    // Fallback: assume /dev/video0 if we can't parse the ID
    qCWarning(log_ui_camera) << "Could not parse device ID:" << deviceId << "- defaulting to /dev/video0";
    return "/dev/video0";
#endif
}

// REMOVED: displayAllCameraDeviceIds() - QCamera-dependent method


// REMOVED: handleCameraTimeout() - QCamera-dependent method


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
        qCInfo(log_ui_camera) << "Device info may not be populated yet - camera switch will fail, needs retry";
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
        // if cameraId is a number, append /dev/video as prefix
        if (cameraId.toInt() != 0 || cameraId == "0") {
            cameraId = "/dev/video" + cameraId;
        }

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

QCameraDevice CameraManager::findCameraByDeviceInfo(const DeviceInfo& deviceInfo) const
{
    if (!deviceInfo.hasCameraDevice()) {
        qCDebug(log_ui_camera) << "Device has no camera component";
        return QCameraDevice();
    }
    
    qCDebug(log_ui_camera) << "Finding Qt camera device for DeviceInfo:";
    qCDebug(log_ui_camera) << "  Camera device ID:" << deviceInfo.cameraDeviceId;
    qCDebug(log_ui_camera) << "  Camera device path:" << deviceInfo.cameraDevicePath;
    
    QList<QCameraDevice> availableCameras = getAvailableCameraDevices();
    
    for (const QCameraDevice& camera : availableCameras) {
        QString cameraId = QString::fromUtf8(camera.id());
        QString cameraDescription = camera.description();
        
        qCDebug(log_ui_camera) << "  Checking camera:" << cameraDescription << "ID:" << cameraId;
        
        // Strategy 1: Match by device ID
        if (!deviceInfo.cameraDeviceId.isEmpty()) {
            // Try exact match
            if (cameraId.compare(deviceInfo.cameraDeviceId, Qt::CaseInsensitive) == 0) {
                qCDebug(log_ui_camera) << "  ✓ Matched by exact device ID";
                return camera;
            }
            
            // Try partial match (device ID contains camera ID or vice versa)
            if (deviceInfo.cameraDeviceId.contains(cameraId, Qt::CaseInsensitive) ||
                cameraId.contains(deviceInfo.cameraDeviceId, Qt::CaseInsensitive)) {
                qCDebug(log_ui_camera) << "  ✓ Matched by partial device ID";
                return camera;
            }
        }
        
        // Strategy 2: Match by device path
        if (!deviceInfo.cameraDevicePath.isEmpty()) {
            if (cameraId.contains(deviceInfo.cameraDevicePath, Qt::CaseInsensitive) ||
                deviceInfo.cameraDevicePath.contains(cameraId, Qt::CaseInsensitive)) {
                qCDebug(log_ui_camera) << "  ✓ Matched by device path";
                return camera;
            }
        }
        
        // Strategy 3: Match by hardware identifiers (for Openterface devices)
        if (cameraDescription.contains("345F", Qt::CaseInsensitive) ||
            cameraId.contains("345F", Qt::CaseInsensitive) ||
            cameraDescription.contains("Openterface", Qt::CaseInsensitive)) {
            qCDebug(log_ui_camera) << "  ✓ Matched by Openterface hardware identifier";
            return camera;
        }
    }
    
    qCDebug(log_ui_camera) << "  ✗ No matching Qt camera device found";
    return QCameraDevice();
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
    
    // Windows: Use enhanced approach with better device detection
    if (isWindowsPlatform()) {
        qDebug() << "Windows: Using enhanced camera initialization";
        
        // First, try to find camera using device manager information
        DeviceManager& deviceManager = DeviceManager::getInstance();
        QList<DeviceInfo> devices = deviceManager.getCurrentDevices();
        
        QCameraDevice openterfaceDevice;
        QString targetPortChain;
        
        // Look for devices with camera components
        for (const DeviceInfo& device : devices) {
            if (device.hasCameraDevice()) {
                qDebug() << "Found device with camera at port chain:" << device.portChain;
                qDebug() << "  Camera device ID:" << device.cameraDeviceId;
                qDebug() << "  Camera device path:" << device.cameraDevicePath;
                
                // Try to find this camera in Qt's camera list
                QCameraDevice matchedCamera = findCameraByDeviceInfo(device);
                if (!matchedCamera.isNull()) {
                    openterfaceDevice = matchedCamera;
                    targetPortChain = device.portChain;
                    qDebug() << "Windows: Found matching Qt camera device:" << matchedCamera.description();
                    break;
                }
            }
        }
        
        // Fallback: Look for any camera with "Openterface" in the description
        if (openterfaceDevice.isNull()) {
            qDebug() << "Windows: Fallback - searching for Openterface camera by description";
            QList<QCameraDevice> allDevices = getAvailableCameraDevices();
            
            qDebug() << "Available camera devices:";
            for (const QCameraDevice& device : allDevices) {
                qDebug() << "  -" << device.description() << "ID:" << QString::fromUtf8(device.id());
                
                if (device.description().contains("Openterface", Qt::CaseInsensitive) ||
                    device.description().contains("345F", Qt::CaseInsensitive) ||
                    device.description() == "Openterface") {
                    openterfaceDevice = device;
                    qDebug() << "Windows: Found Openterface-like device:" << device.description();
                    break;
                }
            }
        }

        if (!openterfaceDevice.isNull()) {
            switchSuccess = switchToCameraDevice(openterfaceDevice, targetPortChain);
            if (switchSuccess) {
                qDebug() << "Windows: Camera switched to device:" << openterfaceDevice.description();
                startCamera();
            }
        } else {
            qCWarning(log_ui_camera) << "Windows: No Openterface camera device found";
            
            // Additional debugging: list all available cameras
            QList<QCameraDevice> allDevices = getAvailableCameraDevices();
            qDebug() << "All available camera devices:";
            for (const QCameraDevice& device : allDevices) {
                qDebug() << "  Camera:" << device.description() << "ID:" << QString::fromUtf8(device.id());
            }
        }
        
        return switchSuccess && !m_currentCameraDevice.isNull();
    }
    
    // Non-Windows: Use existing complex backend logic
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
            switchSuccess = switchToCameraDevice(openterfaceDevice, QString());  // No port chain available for fallback
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

bool CameraManager::initializeCameraWithVideoOutput(VideoPane* videoPane, bool startCapture)
{
    qDebug() << "Initializing camera with VideoPane output, startCapture:" << startCapture;
    
    if (!videoPane) {
        qCWarning(log_ui_camera) << "Cannot initialize camera with null VideoPane";
        return false;
    }
    
    // Check if we're using FFmpeg backend for direct capture
    if (isFFmpegBackend() && m_backendHandler) {
        qDebug() << "Using FFmpeg backend for direct capture";
        
        // Cast to FFmpegBackendHandler to access direct capture methods
        auto* ffmpegHandler = dynamic_cast<FFmpegBackendHandler*>(m_backendHandler.get());
        if (ffmpegHandler) {
            // Enable direct FFmpeg mode in VideoPane
            videoPane->enableDirectFFmpegMode(true);
            
            // Set VideoPane as the output for the FFmpeg backend
            // This will automatically connect the frameReady signal
            ffmpegHandler->setVideoOutput(videoPane);
            
            // Connect error signal
            connect(ffmpegHandler, &FFmpegBackendHandler::captureError,
                    this, [this](const QString& error) {
                        qCWarning(log_ui_camera) << "FFmpeg capture error:" << error;
                        emit cameraError(error);
                    });
            
            // Connect enhanced hotplug signals for VideoPane integration
            connect(ffmpegHandler, &FFmpegBackendHandler::deviceActivated,
                    this, [this](const QString& devicePath) {
                        qCInfo(log_ui_camera) << "FFmpeg device activated (VideoPane):" << devicePath;
                        emit cameraActiveChanged(true);
                    });
                    
            connect(ffmpegHandler, &FFmpegBackendHandler::deviceDeactivated,
                    this, [this](const QString& devicePath) {
                        qCInfo(log_ui_camera) << "FFmpeg device deactivated (VideoPane):" << devicePath;
                        emit cameraActiveChanged(false);
                    });
                    
            connect(ffmpegHandler, &FFmpegBackendHandler::waitingForDevice,
                    this, [this](const QString& devicePath) {
                        qCInfo(log_ui_camera) << "FFmpeg waiting for device (VideoPane):" << devicePath;
                        emit cameraActiveChanged(false);
                    });
            
            // Connect camera active changed to VideoPane
            connect(this, &CameraManager::cameraActiveChanged, videoPane, &VideoPane::onCameraActiveChanged);
            
            // Get device path and configuration
            QString devicePath;
            QSize resolution(0, 0); // Auto-detect maximum resolution
            int framerate = 0; // Auto-detect maximum framerate
            
#ifdef Q_OS_WIN
            // Windows: Use DirectShow device name from Qt camera device
            DeviceManager& deviceManager = DeviceManager::getInstance();
            DeviceInfo selectedDevice = deviceManager.getCurrentSelectedDevice();
            
            if (selectedDevice.isValid()) {
                // Try to get camera device from available devices
                QList<QCameraDevice> devices = getAvailableCameraDevices();
                for (const QCameraDevice& device : devices) {
                    if (device.description().contains("Openterface", Qt::CaseInsensitive) || 
                        device.description().contains("MACROSILICON", Qt::CaseInsensitive)) {
                        // Convert Qt camera device to DirectShow format
                        devicePath = convertCameraDeviceToPath(device);
                        qDebug() << "Found Openterface device via Qt detection (Windows):" << devicePath;
                        break;
                    }
                }
            }
            
            if (devicePath.isEmpty()) {
                qCWarning(log_ui_camera) << "No Openterface device found, searching for any available camera";
                QList<QCameraDevice> devices = getAvailableCameraDevices();
                if (!devices.isEmpty()) {
                    devicePath = convertCameraDeviceToPath(devices.first());
                    qCDebug(log_ui_camera) << "Using first available camera:" << devicePath;
                } else {
                    qCCritical(log_ui_camera) << "No camera devices available";
                    return false;
                }
            }
#else
            // Linux/macOS: Use V4L2 device path
            DeviceManager& deviceManager = DeviceManager::getInstance();
            DeviceInfo selectedDevice = deviceManager.getCurrentSelectedDevice();
            
            if (selectedDevice.isValid() && !selectedDevice.cameraDevicePath.isEmpty()) {
                devicePath = selectedDevice.cameraDevicePath;
                qDebug() << "Using detected camera device path:" << devicePath;
            } else {
                qCWarning(log_ui_camera) << "No valid camera device path found in selected device, trying Qt camera detection";
                
                // Fallback: Try to detect Openterface device path from Qt cameras
                QList<QCameraDevice> devices = getAvailableCameraDevices();
                for (const QCameraDevice& device : devices) {
                    if (device.description().contains("Openterface", Qt::CaseInsensitive) || 
                        device.description().contains("MACROSILICON", Qt::CaseInsensitive)) {
                        // Convert Qt device ID to V4L2 device path
                        devicePath = convertCameraDeviceToPath(device);
                        qDebug() << "Found Openterface device via Qt detection:" << devicePath;
                        break;
                    }
                }
                
                if (devicePath.isEmpty()) {
                    devicePath = "/dev/video0"; // Final fallback
                    qCWarning(log_ui_camera) << "Using default device path:" << devicePath;
                }
            }
#endif
            
            // Only start capture if requested (otherwise just set up the pipeline)
            if (startCapture) {
                qDebug() << "Starting FFmpeg direct capture with device:" << devicePath;
                bool captureStarted = ffmpegHandler->startDirectCapture(devicePath, resolution, framerate);
                
                if (captureStarted) {
                    qDebug() << "✓ FFmpeg direct capture started successfully";
                    qDebug() << "✓ Camera successfully initialized with video output";
                    m_currentCameraPortChain = devicePath; // Store device path as port chain
                    
                    // Emit camera active signal to trigger UI updates (e.g., switch to VideoPane)
                    emit cameraActiveChanged(true);
                    
                    return true;
                } else {
                    qCWarning(log_ui_camera) << "Failed to start FFmpeg direct capture";
                    // Fall back to standard Qt camera approach
                }
            } else {
                qDebug() << "✓ FFmpeg video pipeline set up (capture will start on device switch)";
                return true;
            }
        } else {
            qCWarning(log_ui_camera) << "Failed to cast to FFmpegBackendHandler";
        }
    }
    
    // Fall back to standard Qt camera approach with QGraphicsVideoItem
    qDebug() << "Using standard Qt camera approach";
    videoPane->enableDirectFFmpegMode(false);
    return initializeCameraWithVideoOutput(videoPane->getVideoItem());
}

bool CameraManager::hasActiveCameraDevice() const
{
    // Check if we have a valid device tracked
    return !m_currentCameraDevice.isNull() && !m_currentCameraDeviceId.isEmpty();
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
        // Stop the camera via FFmpeg backend
        qCDebug(log_ui_camera) << "Stopping active camera due to device unplugging";
        stopCamera();
        
        // Clear current device tracking
        m_currentCameraDevice = QCameraDevice();
        m_currentCameraDeviceId.clear();
        m_currentCameraPortChain.clear();
        
        // Clear the video output to show blank instead of frozen frame
        if (m_graphicsVideoOutput && m_backendHandler) {
            qCDebug(log_ui_camera) << "Clearing video output";
            FFmpegBackendHandler* ffmpeg = qobject_cast<FFmpegBackendHandler*>(m_backendHandler.get());
            if (ffmpeg) {
                ffmpeg->setVideoOutput(static_cast<QGraphicsVideoItem*>(nullptr));
                QThread::msleep(50); // Brief delay
                ffmpeg->setVideoOutput(m_graphicsVideoOutput);
            }
        }
        
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
    qCDebug(log_ui_camera) << "========================================";
    qCDebug(log_ui_camera) << "tryAutoSwitchToNewDevice called";
    qCDebug(log_ui_camera) << "  Target port chain:" << portChain;
    qCDebug(log_ui_camera) << "========================================";
    
    // Check if we currently have an active camera device
    if (hasActiveCameraDevice()) {
        qCWarning(log_ui_camera) << "!!! Active camera device detected, skipping auto-switch to preserve user selection";
        qCWarning(log_ui_camera) << "!!! Current device:" << m_currentCameraDevice.description();
        qCWarning(log_ui_camera) << "!!! Current port chain:" << m_currentCameraPortChain;
        return false;
    }
    
    qCDebug(log_ui_camera) << "✓ No active camera device found, attempting to switch to new device";
    
    // IMPORTANT: Refresh available camera devices to ensure the list is up-to-date
    // This is critical for hotplug scenarios where QMediaDevices::videoInputs() might not
    // be immediately updated when a device is plugged in
    qCDebug(log_ui_camera) << "Refreshing available camera devices before auto-switch (1st refresh)";
    refreshAvailableCameraDevices();
    qCDebug(log_ui_camera) << "  Available cameras after 1st refresh:" << m_availableCameraDevices.size();
    
    // Add a small delay to allow the system to fully enumerate the new camera device
    // This is especially important on Windows where device enumeration can take time
    qCDebug(log_ui_camera) << "Waiting 500ms for system to fully enumerate camera device...";
    QThread::msleep(500);
    
    // Refresh again after the delay to ensure we have the latest device list
    qCDebug(log_ui_camera) << "Refreshing available camera devices before auto-switch (2nd refresh)";
    refreshAvailableCameraDevices();
    qCDebug(log_ui_camera) << "  Available cameras after 2nd refresh:" << m_availableCameraDevices.size();
    
    // Try to find a matching camera device for the port chain
    qCDebug(log_ui_camera) << "Attempting to find matching camera device for port chain:" << portChain;
    QCameraDevice matchedCamera = findMatchingCameraDevice(portChain);
    
    if (matchedCamera.isNull()) {
        qCWarning(log_ui_camera) << "✗ No matching camera device found for port chain:" << portChain;
        qCWarning(log_ui_camera) << "  This could mean:";
        qCWarning(log_ui_camera) << "  1. QMediaDevices hasn't updated yet";
        qCWarning(log_ui_camera) << "  2. Device info doesn't match Qt camera device";
        qCWarning(log_ui_camera) << "  3. Camera device path/ID mismatch";
        return false;
    }
    
    qCDebug(log_ui_camera) << "✓ Found matching camera device:" << matchedCamera.description() << "for port chain:" << portChain;
    
    // Ensure video output is connected before switching
    if (m_graphicsVideoOutput) {
        qCDebug(log_ui_camera) << "Video output available for camera switch";
    } else {
        qCWarning(log_ui_camera) << "!!! No graphics video output available";
    }
    
    // Switch to the new camera device
    qCDebug(log_ui_camera) << "Calling switchToCameraDevice...";
    bool switchSuccess = switchToCameraDevice(matchedCamera, portChain);
    
    if (switchSuccess) {
        qCInfo(log_ui_camera) << "✓ Successfully auto-switched to new camera device:" << matchedCamera.description() << "at port chain:" << portChain;
        
        // Start the camera if video output is available
        if (m_graphicsVideoOutput) {
            qCDebug(log_ui_camera) << "Starting camera after successful switch";
            startCamera();
        } else {
            qCWarning(log_ui_camera) << "!!! Cannot start camera - no video output available";
        }
        
        emit newDeviceAutoConnected(matchedCamera, portChain);
    } else {
        qCWarning(log_ui_camera) << "✗ Failed to auto-switch to new camera device:" << matchedCamera.description();
    }
    
    qCDebug(log_ui_camera) << "========================================";
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

void CameraManager::refreshVideoOutput()
{
    qDebug() << "Refreshing video output connection";
    
    try {
        // Force re-establishment of video output connection to ensure new camera feed is displayed
        if (m_graphicsVideoOutput) {
            qDebug() << "Forcing graphics video output refresh";
            // Temporarily disconnect and reconnect to force refresh
    // REMOVED: m_captureSession.setVideoOutput(nullptr);
            QThread::msleep(10); // Brief pause
    // REMOVED: m_captureSession.setVideoOutput(m_graphicsVideoOutput);
            
            // Verify reconnection
    // REMOVED: if (m_captureSession.videoOutput() == m_graphicsVideoOutput) {
                qDebug() << "Graphics video output refresh successful";
            } else {
                qCWarning(log_ui_camera) << "Graphics video output refresh failed";
            }
    qDebug() << "Video output refresh completed";
    
    } catch (const std::exception& e) {
        qCritical() << "Exception refreshing video output:" << e.what();
    } catch (...) {
        qCritical() << "Unknown exception refreshing video output";
    }
}

void CameraManager::setupWindowsHotplugMonitoring()
{
    qCDebug(log_ui_camera) << "Setting up Windows hotplug monitoring";
    
    // For Windows, we rely on the DeviceManager's hotplug monitor instead of QMediaDevices
    // since QMediaDevices::videoInputsChanged is not reliably available as a signal
    // The DeviceManager hotplug monitor will handle device detection and call our handlers
    
    qCDebug(log_ui_camera) << "Windows hotplug monitoring enabled (using DeviceManager)";
}

void CameraManager::onVideoInputsChanged()
{
    qCDebug(log_ui_camera) << "Video inputs changed - refreshing camera device list";
    
    QList<QCameraDevice> previousDevices = m_availableCameraDevices;
    refreshAvailableCameraDevices();
    
    // Check for disconnected devices
    for (const QCameraDevice& prevDevice : previousDevices) {
        bool stillExists = false;
        for (const QCameraDevice& currentDevice : m_availableCameraDevices) {
            if (QString::fromUtf8(prevDevice.id()) == QString::fromUtf8(currentDevice.id())) {
                stillExists = true;
                break;
            }
        }
        
        if (!stillExists) {
            qCDebug(log_ui_camera) << "Camera device disconnected:" << prevDevice.description();
            
            // Check if this was our current device
            if (!m_currentCameraDevice.isNull() && 
                QString::fromUtf8(m_currentCameraDevice.id()) == QString::fromUtf8(prevDevice.id())) {
                qCInfo(log_ui_camera) << "Current camera device disconnected, stopping camera";
                stopCamera();
                
                // Reset current device tracking
                m_currentCameraDevice = QCameraDevice();
                m_currentCameraDeviceId.clear();
                m_currentCameraPortChain.clear();
                
                QString prevDeviceId = QString::fromUtf8(prevDevice.id());
                emit cameraDeviceDisconnected(prevDeviceId, QString());  // No port chain available
            }
        }
    }
    
    // Check for newly connected devices
    for (const QCameraDevice& currentDevice : m_availableCameraDevices) {
        bool isNew = true;
        for (const QCameraDevice& prevDevice : previousDevices) {
            if (QString::fromUtf8(currentDevice.id()) == QString::fromUtf8(prevDevice.id())) {
                isNew = false;
                break;
            }
        }
        
        if (isNew) {
            qCDebug(log_ui_camera) << "New camera device detected:" << currentDevice.description();
            QString deviceId = QString::fromUtf8(currentDevice.id());
            emit cameraDeviceConnected(deviceId, QString());  // No port chain available
            
            // Auto-switch to new Openterface device if no current device is active
            if (currentDevice.description().contains("Openterface", Qt::CaseInsensitive) && 
                !hasActiveCameraDevice()) {
                qCInfo(log_ui_camera) << "Auto-switching to new Openterface camera device:" << currentDevice.description();
                
                bool switchSuccess = switchToCameraDevice(currentDevice, QString());  // No port chain
                if (switchSuccess && m_graphicsVideoOutput) {
                    startCamera();
                    qCInfo(log_ui_camera) << "✓ Successfully auto-switched to new Openterface camera device";
                } else {
                    qCWarning(log_ui_camera) << "Failed to auto-switch to new Openterface camera device";
                }
            }
        }
    }
}

void CameraManager::connectToHotplugMonitor()
{
    qCDebug(log_ui_camera) << "Connecting CameraManager to hotplug monitor";
    
    // Get the hotplug monitor from DeviceManager
    DeviceManager& deviceManager = DeviceManager::getInstance();
    HotplugMonitor* hotplugMonitor = deviceManager.getHotplugMonitor();
    
    if (!hotplugMonitor) {
        qCWarning(log_ui_camera) << "Failed to get hotplug monitor from device manager";
        return;
    }
    
    // Connect to device unplugging signal
    connect(hotplugMonitor, &HotplugMonitor::deviceUnplugged,
            this, [this](const DeviceInfo& device) {
                qCDebug(log_ui_camera) << "========================================";
                qCDebug(log_ui_camera) << "CameraManager: DEVICE UNPLUGGED EVENT";
                qCDebug(log_ui_camera) << "  Device port chain:" << device.portChain;
                qCDebug(log_ui_camera) << "========================================";
                
                // Check if device has camera info from DeviceManager
                bool hasCameraInfoFromDeviceManager = device.hasCameraDevice();
                qCDebug(log_ui_camera) << "Device camera info check:";
                qCDebug(log_ui_camera) << "  Has camera from DeviceManager:" << hasCameraInfoFromDeviceManager;
                
                // CRITICAL FIX: Even if DeviceManager doesn't have camera info,
                // we should still deactivate if:
                // 1. We have an active Openterface camera
                // 2. The port chain matches (or is empty since DeviceManager might not track it properly)
                bool shouldDeactivate = false;
                
                if (hasCameraInfoFromDeviceManager) {
                    qCDebug(log_ui_camera) << "Device has camera component - checking if it matches current camera";
                    qCDebug(log_ui_camera) << "  Current camera port chain:" << m_currentCameraPortChain;
                    qCDebug(log_ui_camera) << "  Unplugged device port chain:" << device.portChain;
                    
                    // Check if the unplugged device matches the current camera device port chain
                    if (!m_currentCameraPortChain.isEmpty() && m_currentCameraPortChain == device.portChain) {
                        shouldDeactivate = true;
                        qCInfo(log_ui_camera) << ">>> Port chains MATCH - Will deactivate camera";
                    }
                } else {
                    // Workaround: If DeviceManager has no camera info, but we have an active camera,
                    // deactivate it anyway when ANY device at the expected port is unplugged
                    qCDebug(log_ui_camera) << "DeviceManager has no camera info for unplugged device";
                    
                    if (hasActiveCameraDevice()) {
                        qCDebug(log_ui_camera) << "We have an active camera - checking if we should deactivate it";
                        
                        // Check if current device is Openterface
                        QString currentDesc = m_currentCameraDevice.description();
                        if (currentDesc.contains("Openterface", Qt::CaseInsensitive)) {
                            qCDebug(log_ui_camera) << "Current camera is Openterface:" << currentDesc;
                            
                            // If port chain matches OR is empty (not tracked), deactivate
                            if (m_currentCameraPortChain.isEmpty() || m_currentCameraPortChain == device.portChain) {
                                shouldDeactivate = true;
                                qCWarning(log_ui_camera) << ">>> Deactivating Openterface camera (fallback - DeviceManager has no camera info)";
                            }
                        }
                    }
                }
                
                // Perform deactivation if needed
                if (shouldDeactivate) {
                    qCInfo(log_ui_camera) << "Deactivating camera for unplugged device at port:" << device.portChain;
                    bool deactivated = deactivateCameraByPortChain(device.portChain);
                    if (deactivated) {
                        qCInfo(log_ui_camera) << "✓ Camera deactivated for unplugged device at port:" << device.portChain;
                    } else {
                        qCWarning(log_ui_camera) << "✗ Camera deactivation FAILED for port:" << device.portChain;
                    }
                } else {
                    qCDebug(log_ui_camera) << "Camera deactivation skipped - no match found";
                    if (m_currentCameraPortChain.isEmpty()) {
                        qCDebug(log_ui_camera) << "  Reason: No current camera port chain tracked";
                    } else {
                        qCDebug(log_ui_camera) << "  Reason: Port chain or device type mismatch";
                    }
                }
                
                // For Windows: Also manually check for Qt camera device changes
                if (isWindowsPlatform()) {
                    onVideoInputsChanged();
                }
            });
            
    // Connect to new device plugged in signal
    connect(hotplugMonitor, &HotplugMonitor::newDevicePluggedIn,
            this, [this](const DeviceInfo& device) {
                qCDebug(log_ui_camera) << "========================================";
                qCDebug(log_ui_camera) << "CameraManager: NEW DEVICE PLUGGED IN EVENT";
                qCDebug(log_ui_camera) << "  Device port chain:" << device.portChain;
                qCDebug(log_ui_camera) << "========================================";
                
                // For Windows: Refresh Qt camera device list FIRST to ensure we have the latest devices
                if (isWindowsPlatform()) {
                    qCDebug(log_ui_camera) << "Windows: Refreshing video inputs before checking for camera device";
                    onVideoInputsChanged();
                }
                
                // Check if device has camera information from DeviceManager
                bool hasCameraInfoFromDeviceManager = device.hasCameraDevice();
                qCDebug(log_ui_camera) << "Device camera info check:";
                qCDebug(log_ui_camera) << "  Has camera from DeviceManager:" << hasCameraInfoFromDeviceManager;
                qCDebug(log_ui_camera) << "  Camera device ID:" << device.cameraDeviceId;
                qCDebug(log_ui_camera) << "  Camera device path:" << device.cameraDevicePath;
                
                // WORKAROUND: Even if DeviceManager didn't populate camera info,
                // check if QMediaDevices has an Openterface camera available
                bool hasOpenterfaceCameraInQt = false;
                if (!hasCameraInfoFromDeviceManager) {
                    qCDebug(log_ui_camera) << "DeviceManager has no camera info - checking QMediaDevices for Openterface camera";
                    for (const QCameraDevice& cam : m_availableCameraDevices) {
                        if (cam.description().contains("Openterface", Qt::CaseInsensitive)) {
                            hasOpenterfaceCameraInQt = true;
                            qCDebug(log_ui_camera) << "  ✓ Found Openterface camera in Qt:" << cam.description();
                            break;
                        }
                    }
                }
                
                // Only attempt auto-switch if the device has a camera component OR we found Openterface in Qt
                if (!hasCameraInfoFromDeviceManager && !hasOpenterfaceCameraInQt) {
                    qCDebug(log_ui_camera) << "Device at port" << device.portChain << "has no camera component, skipping camera auto-switch";
                    return;
                }
                
                if (hasOpenterfaceCameraInQt) {
                    qCDebug(log_ui_camera) << "Using Qt-detected Openterface camera (DeviceManager camera info not available)";
                } else {
                    qCDebug(log_ui_camera) << "Device has camera component:";
                    qCDebug(log_ui_camera) << "  Camera device ID:" << device.cameraDeviceId;
                    qCDebug(log_ui_camera) << "  Camera device path:" << device.cameraDevicePath;
                }
                
                // Check current camera state before attempting auto-switch
                qCDebug(log_ui_camera) << "Current camera state check:";
                qCDebug(log_ui_camera) << "  m_currentCameraDevice.isNull():" << m_currentCameraDevice.isNull();
                qCDebug(log_ui_camera) << "  m_currentCameraPortChain:" << m_currentCameraPortChain;
                qCDebug(log_ui_camera) << "  hasActiveCameraDevice():" << hasActiveCameraDevice();
                
                // Check if there's currently an active camera device
                if (hasActiveCameraDevice()) {
                    qCWarning(log_ui_camera) << "!!! Camera device already active, skipping auto-switch to port:" << device.portChain;
                    qCWarning(log_ui_camera) << "!!! This might be a BUG - camera should have been deactivated on unplug";
                    return;
                }
                
                qCDebug(log_ui_camera) << "No active camera device found, attempting to switch to new device";
                
                // If we're using the Qt-detected camera workaround, try to auto-switch to it directly
                if (hasOpenterfaceCameraInQt && !hasCameraInfoFromDeviceManager) {
                    qCDebug(log_ui_camera) << "Using fallback: switching to Qt-detected Openterface camera";
                    
                    // Find the Openterface camera in Qt devices
                    for (const QCameraDevice& cam : m_availableCameraDevices) {
                        if (cam.description().contains("Openterface", Qt::CaseInsensitive)) {
                            qCDebug(log_ui_camera) << "Switching to Openterface camera:" << cam.description();
                            bool switchSuccess = switchToCameraDevice(cam, device.portChain);
                            if (switchSuccess && m_graphicsVideoOutput) {
                                startCamera();
                                qCInfo(log_ui_camera) << "✓ Camera auto-switched to Openterface device (fallback method)";
                            } else {
                                qCWarning(log_ui_camera) << "✗ Camera auto-switch FAILED (fallback method)";
                            }
                            qCDebug(log_ui_camera) << "========================================";
                            return;
                        }
                    }
                }
                
                // Try to auto-switch to the new camera device using normal method
                bool switchSuccess = tryAutoSwitchToNewDevice(device.portChain);
                if (switchSuccess) {
                    qCInfo(log_ui_camera) << "✓ Camera auto-switched to new device at port:" << device.portChain;
                } else {
                    qCWarning(log_ui_camera) << "✗ Camera auto-switch FAILED for port:" << device.portChain;
                }
                qCDebug(log_ui_camera) << "========================================";
            });
            
    qCDebug(log_ui_camera) << "CameraManager successfully connected to hotplug monitor";
}

void CameraManager::disconnectFromHotplugMonitor()
{
    qCDebug(log_ui_camera) << "Disconnecting CameraManager from hotplug monitor";
    
    // Get the hotplug monitor from DeviceManager
    DeviceManager& deviceManager = DeviceManager::getInstance();
    HotplugMonitor* hotplugMonitor = deviceManager.getHotplugMonitor();
    
    if (hotplugMonitor) {
        disconnect(hotplugMonitor, nullptr, this, nullptr);
        qCDebug(log_ui_camera) << "CameraManager disconnected from hotplug monitor";
    }
}

void CameraManager::handleFFmpegDeviceDisconnection(const QString& devicePath)
{   
    qCDebug(log_ui_camera) << "Handling FFmpeg device disconnection for:" << devicePath;
    
    // Check if the disconnected device is our current device
    QString currentDeviceId = getCurrentCameraDeviceId();
    if (!currentDeviceId.isEmpty() && 
        (currentDeviceId == devicePath || currentDeviceId.contains(devicePath))) {
        
        qCWarning(log_ui_camera) << "Current FFmpeg device disconnected, attempting recovery";
        
        // Try to find an alternative available camera device
        QList<QCameraDevice> availableDevices = getAvailableCameraDevices();
        QCameraDevice replacementDevice;
        
        for (const QCameraDevice& device : availableDevices) {
            // Skip the disconnected device
            QString deviceId = QString::fromUtf8(device.id());
            if (deviceId == devicePath || deviceId.contains(devicePath)) {
                continue;
            }
            
            // Check if this device is available
#ifndef Q_OS_WIN
            if (auto ffmpegHandler = qobject_cast<FFmpegBackendHandler*>(m_backendHandler.get())) {
                // Convert device ID to device path
                QString testDevicePath;
                if (!deviceId.startsWith("/dev/video")) {
                    bool isNumber = false;
                    int deviceNumber = deviceId.toInt(&isNumber);
                    if (isNumber) {
                        testDevicePath = QString("/dev/video%1").arg(deviceNumber);
                    } else {
                        // Skip devices with unparseable IDs
                        qCDebug(log_ui_camera) << "Skipping device with unparseable ID:" << deviceId;
                        continue;
                    }
                } else {
                    testDevicePath = deviceId;
                }
                
                if (ffmpegHandler->checkCameraAvailable(testDevicePath)) {
                    replacementDevice = device;
                    qCDebug(log_ui_camera) << "Found replacement device:" << device.description();
                    break;
                }
            }
#endif
        }
        
        if (!replacementDevice.isNull()) {
            qCDebug(log_ui_camera) << "Attempting to switch to replacement device";
            
            // Stop current camera first
            stopCamera();
            
            // Switch to the new device
            if (switchToCameraDevice(replacementDevice, QString())) {
                qCInfo(log_ui_camera) << "Successfully switched to replacement device:" << replacementDevice.description();
                
                // Restart the camera
                startCamera();
            } else {
                qCWarning(log_ui_camera) << "Failed to switch to replacement device";
                emit cameraError("Camera device disconnected and no suitable replacement found");
            }
        } else {
            qCWarning(log_ui_camera) << "No suitable replacement device found for disconnected FFmpeg device";
            emit cameraError("Camera device disconnected: " + devicePath);
        }
    } else {
        qCDebug(log_ui_camera) << "Disconnected device is not our current device, ignoring";
    }
}

// REMOVED: resetRecordingSystem() - QCamera-dependent method


// REMOVED: getMediaRecorderErrorInfo() - QCamera-dependent method


// REMOVED: getRecordingSystemDiagnostics() - QCamera-dependent method


// REMOVED: dumpRecordingSystemState() - QCamera-dependent method


// REMOVED: recoverRecordingSystem() - QCamera-dependent method


// REMOVED: getRecordingDiagnosticsReport() - QCamera-dependent method


// REMOVED: startRecordingMonitoring() - QCamera-dependent method


// REMOVED: stopRecordingMonitoring() - QCamera-dependent method


// REMOVED: updateRecordingStatus() - QCamera-dependent method
