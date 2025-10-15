#include "cameramanager.h"
#include "host/multimediabackend.h"

#ifndef Q_OS_WIN
// Include FFmpeg and GStreamer backends for non-Windows platforms
#include "host/backend/ffmpegbackendhandler.h"
#include "host/backend/gstreamerbackendhandler.h"
#else
// Include only Qt backend for Windows
#include "host/backend/qtbackendhandler.h"
#endif

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
    
    // Check for required permissions
    #ifdef Q_OS_WIN
    // On Windows 10+, check for webcam and microphone permissions
    // These are typically handled through app capabilities in the manifest
    qCDebug(log_ui_camera) << "On Windows, ensure app has webcam and microphone permissions in OS settings";
    #elif defined(Q_OS_MACOS)
    qCDebug(log_ui_camera) << "On macOS, ensure app has camera and microphone permissions";
    #endif
    
    // Verify if QMediaDevices is available
    if (QMediaDevices::videoInputs().isEmpty()) {
        qCWarning(log_ui_camera) << "No video input devices detected - recording may not work";
    } else {
        qCDebug(log_ui_camera) << "Available video devices:" << QMediaDevices::videoInputs().count();
    }
    
    // Verify audio devices for recording with sound
    if (QMediaDevices::audioInputs().isEmpty()) {
        qCWarning(log_ui_camera) << "No audio input devices detected - recordings may not have sound";
    } else {
        qCDebug(log_ui_camera) << "Available audio devices:" << QMediaDevices::audioInputs().count();
    }
    
    // Initialize backend handler only if not on Windows
    if (!isWindowsPlatform()) {
        initializeBackendHandler();
    } else {
        qCDebug(log_ui_camera) << "Windows platform detected - using Qt backend with recording support";
        // Initialize Qt backend for Windows with recording support
        initializeBackendHandler();
        qCDebug(log_ui_camera) << "Windows platform detected - using Qt backend with recording support";
        // Initialize Qt backend for Windows with recording support
        initializeBackendHandler();
        // Setup Windows-specific hotplug monitoring
        setupWindowsHotplugMonitoring();
    }
    
    // Connect to hotplug monitor for all platforms
    connectToHotplugMonitor();
    
    // Connect to hotplug monitor for all platforms
    connectToHotplugMonitor();
    
    m_imageCapture = std::make_unique<QImageCapture>();
    m_mediaRecorder = std::make_unique<QMediaRecorder>();
    
    // Connect image and recorder signals
    connect(m_imageCapture.get(), &QImageCapture::imageCaptured, this, &CameraManager::onImageCaptured);
    
    // Connect media recorder error signals
    connect(m_mediaRecorder.get(), &QMediaRecorder::recorderStateChanged, this, [this](QMediaRecorder::RecorderState state) {
        qCDebug(log_ui_camera) << "Recorder state changed to:" << state;
        if (state == QMediaRecorder::RecordingState) {
            qCInfo(log_ui_camera) << "Recording started with media recorder";
            emit recordingStarted();
        } else if (state == QMediaRecorder::StoppedState) {
            qCInfo(log_ui_camera) << "Recording stopped with media recorder";
            emit recordingStopped();
        }
    });
    
    connect(m_mediaRecorder.get(), &QMediaRecorder::errorOccurred, this, [this](QMediaRecorder::Error error, const QString &errorString) {
        qCWarning(log_ui_camera) << "Media recorder error:" << error << "-" << errorString;
        
        QString detailedError = QString("%1 (%2)").arg(errorString).arg(getMediaRecorderErrorInfo(error));
        
        // Dump recording system state for debugging
        dumpRecordingSystemState();
        
        emit recordingError(detailedError);
    });

    // Initialize available camera devices
    m_availableCameraDevices = getAvailableCameraDevices();
    qCDebug(log_ui_camera) << "Found" << m_availableCameraDevices.size() << "available camera devices";
    
    // Display all camera device IDs for debugging
    displayAllCameraDeviceIds();
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
#ifndef Q_OS_WIN
    if (isFFmpegBackend() && m_backendHandler) {
        try {
            // Use dynamic_cast for safer type checking
            return dynamic_cast<FFmpegBackendHandler*>(m_backendHandler.get());
        } catch (const std::exception& e) {
            qCCritical(log_ui_camera) << "Exception during FFmpeg backend cast:" << e.what();
        }
    }
#endif
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
            
            // Connect FFmpeg-specific signals if this is an FFmpeg backend (non-Windows only)
#ifndef Q_OS_WIN
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
#endif
            
            // Connect Qt backend-specific setup if this is a Qt backend (Windows)
#ifdef Q_OS_WIN
            qCDebug(log_ui_camera) << "Checking if backend handler is Qt type. Handler pointer:" << (void*)m_backendHandler.get();
            qCDebug(log_ui_camera) << "Backend type:" << static_cast<int>(m_backendHandler->getBackendType());
            if (auto qtHandler = qobject_cast<QtBackendHandler*>(m_backendHandler.get())) {
                qCDebug(log_ui_camera) << "Setting up Qt backend specific configuration";
                qCDebug(log_ui_camera) << "CameraManager m_mediaRecorder pointer:" << (void*)m_mediaRecorder.get();
                
                // Set the media recorder for the Qt backend
                qtHandler->setMediaRecorder(m_mediaRecorder.get());
                
                qCDebug(log_ui_camera) << "Qt backend configuration completed";
            } else {
                qCDebug(log_ui_camera) << "Backend handler is not Qt type or cast failed";
                qCDebug(log_ui_camera) << "Handler type name:" << m_backendHandler->metaObject()->className();
            }
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
void CameraManager::setCamera(const QCameraDevice &cameraDevice, QGraphicsVideoItem* videoOutput)
{
    if (isWindowsPlatform()) {
        qCDebug(log_ui_camera) << "Windows: Set Camera to graphics videoOutput using direct QCamera approach: " << videoOutput << ", device name: " << cameraDevice.description();
        setCameraDevice(cameraDevice);
        setVideoOutput(videoOutput);
        queryResolutions();
        startCamera();
    } else {
        qCDebug(log_ui_camera) << "Non-Windows: Using backend approach for setCamera";
        // For non-Windows, fall back to the existing backend implementation
        setCameraDevice(cameraDevice);
        setVideoOutput(videoOutput);
        queryResolutions();
        startCamera();
    }
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
        
        // Use backend handler for camera preparation if available (not for Windows Qt backend)
        if (m_backendHandler && !isQtBackend()) {
            qCDebug(log_ui_camera) << "Using backend handler for camera device setup";
            m_backendHandler->prepareCameraCreation(m_camera.get());
        }
        
        // Create new camera instance
        m_camera.reset(new QCamera(cameraDevice));
        
        if (!m_camera) {
            qCritical() << "Failed to create camera instance for device:" << cameraDevice.description();
            return;
        }
        
        // Configure camera device with backend handler (except for Qt backend which uses standard setup)
        if (m_backendHandler && !isQtBackend()) {
            qCDebug(log_ui_camera) << "Calling configureCameraDevice on backend handler:" << m_backendHandler->getBackendName();
            m_backendHandler->configureCameraDevice(m_camera.get(), cameraDevice);
            qCDebug(log_ui_camera) << "configureCameraDevice call completed";
        } else {
            if (isQtBackend()) {
                qCDebug(log_ui_camera) << "Qt backend: Using standard camera configuration";
            } else {
                qCWarning(log_ui_camera) << "No backend handler available for configureCameraDevice";
            }
        }
        
        // Setup connections before setting up capture session
        setupConnections();
        
        // Set up capture session - Qt backend uses standard setup, others use custom setup
        if (m_backendHandler && !isQtBackend()) {
            m_backendHandler->setupCaptureSession(&m_captureSession, m_camera.get());
            
            // For GStreamer direct pipeline, skip image capture setup to avoid device conflicts
            if (isGStreamerBackend()) {
                qCDebug(log_ui_camera) << "GStreamer backend detected - skipping image capture setup to avoid device conflicts";
                // Don't connect image capture for GStreamer direct pipeline to prevent device access
                // The GStreamer pipeline will handle video directly without Qt camera/capture
            } else {
                m_captureSession.setImageCapture(m_imageCapture.get());
            }
        } else {
            // Qt backend or fallback: standard setup
            if (isQtBackend()) {
                qCDebug(log_ui_camera) << "Qt backend: Using standard Qt capture session setup";
            }
            m_captureSession.setCamera(m_camera.get());
            m_captureSession.setImageCapture(m_imageCapture.get());
            
            // For Qt backend, also set the media recorder for recording functionality
            if (isQtBackend()) {
                m_captureSession.setRecorder(m_mediaRecorder.get());
                qCDebug(log_ui_camera) << "Qt backend: Media recorder added to capture session";
                
                // Configure QtBackendHandler with media recorder
                if (auto qtHandler = dynamic_cast<QtBackendHandler*>(m_backendHandler.get())) {
                    qtHandler->setMediaRecorder(m_mediaRecorder.get());
                    qtHandler->setCaptureSession(&m_captureSession);
                    qCDebug(log_ui_camera) << "Qt backend: Configured QtBackendHandler with MediaRecorder";
                }
            }
        }
        
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

// Deprecated method for initializing camera with video output
// This method is kept for compatibility but should be replaced with the new methods
// that handle port chain tracking and improved device management
void CameraManager::setVideoOutput(QGraphicsVideoItem* videoOutput)
{
    if (videoOutput) {
        m_graphicsVideoOutput = videoOutput;
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
    qCDebug(log_ui_camera) << "Camera start..";
    
    try {
        if (m_camera) {
            // Check if camera is already active to avoid redundant starts
            if (m_camera->isActive()) {
                qCDebug(log_ui_camera) << "Camera is already active, skipping start";
                return;
            }

            qCDebug(log_ui_camera) << "Starting camera:" << m_camera->cameraDevice().description();
            
            // Use simple direct QCamera approach on Windows
            if (isWindowsPlatform()) {
                qCDebug(log_ui_camera) << "Windows: Using direct QCamera approach";
                
                // Ensure video output is connected before starting camera
                if (m_graphicsVideoOutput) {
                    qCDebug(log_ui_camera) << "Windows: Ensuring graphics video output is connected before starting camera";
                    m_captureSession.setVideoOutput(m_graphicsVideoOutput);
                }
                
                m_camera->start();
                
                // Extended wait time for better camera stabilization on Windows
                QThread::msleep(50);
                
                // Verify camera started
                if (m_camera->isActive()) {
                    qCDebug(log_ui_camera) << "Windows: Camera started successfully and is active";
                    emit cameraActiveChanged(true);
                } else {
                    qCWarning(log_ui_camera) << "Windows: Camera start command sent but camera is not active";
                }
            }
            // Use backend handler for video output setup and camera start on non-Windows
            else if (!isWindowsPlatform() && m_backendHandler) {
                // Ensure device is configured with backend handler
                if (m_backendHandler) {
                    qCDebug(log_ui_camera) << "Re-configuring camera device with backend handler";
                    m_backendHandler->configureCameraDevice(m_camera.get(), m_currentCameraDevice);
                }
                
                // For GStreamer backend, ensure resolution and framerate are set before starting
                if (isGStreamerBackend()) {
                    qCDebug(log_ui_camera) << "Ensuring GStreamer backend has resolution and framerate before starting";
                    
                    // Make sure we have resolution information
                    if (m_video_width <= 0 || m_video_height <= 0) {
                        qCDebug(log_ui_camera) << "Resolution not set, querying resolutions first";
                        queryResolutions();
                    }
                    
                    // Get current resolution and framerate
                    QSize resolution = QSize(m_video_width > 0 ? m_video_width : 1920, 
                                            m_video_height > 0 ? m_video_height : 1080);
                    int framerate = GlobalVar::instance().getCaptureFps() > 0 ? 
                                   GlobalVar::instance().getCaptureFps() : 30;
                    
                    qCDebug(log_ui_camera) << "Setting GStreamer resolution:" << resolution << "framerate:" << framerate;
                    
                    // Cast to GStreamer backend handler to set resolution (non-Windows only)
#ifndef Q_OS_WIN
                    auto* gstreamerHandler = qobject_cast<GStreamerBackendHandler*>(m_backendHandler.get());
                    if (gstreamerHandler) {
                        gstreamerHandler->setResolutionAndFramerate(resolution, framerate);
                    }
#endif
                }
                
                // For GStreamer backend, let it handle the entire video pipeline
                if (isGStreamerBackend()) {
                    qCDebug(log_ui_camera) << "Using GStreamer backend - delegating to direct pipeline";
                    
                    // Prepare video output connection for GStreamer
                    if (m_graphicsVideoOutput) {
                        m_backendHandler->prepareVideoOutputConnection(&m_captureSession, m_graphicsVideoOutput);
                        m_backendHandler->finalizeVideoOutputConnection(&m_captureSession, m_graphicsVideoOutput);
                    }
                    
                    // Let GStreamer backend handle camera startup (will use direct pipeline if available)
                    m_backendHandler->startCamera(m_camera.get());
                    
                    // For GStreamer backend with direct pipeline, we consider it "active" if the backend says so
                    // The Qt camera might not report as active since we're bypassing it
                    emit cameraActiveChanged(true);
                    qCDebug(log_ui_camera) << "GStreamer backend camera startup delegated";
                    
                } else {
                    // For other backends (FFmpeg, etc.), use standard Qt camera approach
                    qCDebug(log_ui_camera) << "Using standard backend approach with Qt camera";
                    
                    // Prepare video output connection
                    if (m_graphicsVideoOutput) {
                        m_backendHandler->prepareVideoOutputConnection(&m_captureSession, m_graphicsVideoOutput);
                        m_backendHandler->finalizeVideoOutputConnection(&m_captureSession, m_graphicsVideoOutput);
                    }
                    
                    // Start camera using backend handler (standard Qt approach)
                    m_backendHandler->startCamera(m_camera.get());
                    
                    // Verify camera started for non-GStreamer backends
                    if (m_camera->isActive()) {
                        qDebug() << "Camera started successfully and is active";
                        emit cameraActiveChanged(true);
                        qCDebug(log_ui_camera) << "Camera started successfully";
                    } else {
                        qCWarning(log_ui_camera) << "Camera start command sent but camera is not active";
                    }
                }
            } else {
                // Fallback: standard connection and start when no backend handler
                qCDebug(log_ui_camera) << "No backend handler available, using fallback approach";
                if (m_graphicsVideoOutput) {
                    m_captureSession.setVideoOutput(m_graphicsVideoOutput);
                }
                
                // Only start Qt camera if not using GStreamer backend to avoid device conflicts
                if (!isGStreamerBackend()) {
                    m_camera->start();
                    
                    // Verify camera started
                    if (m_camera->isActive()) {
                        qDebug() << "Camera started successfully and is active (fallback)";
                        emit cameraActiveChanged(true);
                        qCDebug(log_ui_camera) << "Camera started successfully (fallback)";
                    } else {
                        qCWarning(log_ui_camera) << "Camera start command sent but camera is not active (fallback)";
                    }
                } else {
                    qCDebug(log_ui_camera) << "Skipping Qt camera start in fallback - GStreamer backend will handle camera";
                    // For GStreamer, we consider it active if the backend handled it
                    emit cameraActiveChanged(true);
                }
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
    qCDebug(log_ui_camera) << "Stopping camera..";
    
    try {
        // Stop VideoHid first
        VideoHid::getInstance().stop();

        if (m_camera) {
            // Check if camera is already stopped to avoid redundant stops
            if (!m_camera->isActive() && (isWindowsPlatform() || !m_backendHandler || !isGStreamerBackend())) {
                qCDebug(log_ui_camera) << "Camera is already stopped";
                return;
            }
            
            qCDebug(log_ui_camera) << "Stopping camera:" << m_camera->cameraDevice().description();
            
            // Use backend handler for camera shutdown (only on non-Windows)
            if (!isWindowsPlatform() && m_backendHandler) {
                if (isGStreamerBackend()) {
                    qCDebug(log_ui_camera) << "Using GStreamer backend - stopping direct pipeline";
                    // GStreamer backend will handle stopping both direct pipeline and Qt camera
                    m_backendHandler->stopCamera(m_camera.get());
                    emit cameraActiveChanged(false);
                    qCDebug(log_ui_camera) << "GStreamer backend camera shutdown completed";
                } else {
                    qCDebug(log_ui_camera) << "Using standard backend camera shutdown";
                    m_backendHandler->stopCamera(m_camera.get());
                    emit cameraActiveChanged(false);
                    qCDebug(log_ui_camera) << "Standard backend camera shutdown completed";
                }
            } else {
                // Windows or fallback: direct camera stop
                if (isWindowsPlatform()) {
                    qCDebug(log_ui_camera) << "Windows: Using direct camera stop";
                } else {
                    qCDebug(log_ui_camera) << "No backend handler, using direct camera stop";
                }
                m_camera->stop();
                emit cameraActiveChanged(false);
            }
            
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
    qCInfo(log_ui_camera) << "=== START RECORDING PROCESS INITIATED ===";
    qCDebug(log_ui_camera) << "Camera state: " << (m_camera ? (m_camera->isActive() ? "Active" : "Inactive") : "NULL");
    qCDebug(log_ui_camera) << "Recorder state: " << (m_mediaRecorder ? 
                                                  QString("Valid (State: %1, Error: %2)").arg(m_mediaRecorder->recorderState())
                                                                                       .arg(m_mediaRecorder->error()) : 
                                                  "NULL");
    
    // Check if recording is already in progress
    if (isRecording()) {
        qCWarning(log_ui_camera) << "Recording already in progress, not starting a new one";
        qCDebug(log_ui_camera) << "Current recording path: " << m_currentRecordingPath;
        qCDebug(log_ui_camera) << "=== START RECORDING ABORTED - ALREADY RECORDING ===";
        return;
    }
    
    // Check if the recording system needs to be reset before starting
    if (!m_mediaRecorder || m_mediaRecorder->error() != QMediaRecorder::NoError) {
        qCWarning(log_ui_camera) << "Recording system in error state, resetting before starting new recording";
        if (m_mediaRecorder) {
            qCDebug(log_ui_camera) << "Recorder error before reset: " << m_mediaRecorder->error() 
                                  << " - " << m_mediaRecorder->errorString();
        }
        resetRecordingSystem();
        
        // Verify we have a valid recorder after reset
        if (!m_mediaRecorder) {
            qCCritical(log_ui_camera) << "Failed to reset recording system - media recorder is still null";
            qCDebug(log_ui_camera) << "=== START RECORDING FAILED - RECORDER NULL AFTER RESET ===";
            emit recordingError("Failed to initialize recording system");
            return;
        }
        
        qCDebug(log_ui_camera) << "Recording system reset complete. New recorder state: " 
                              << m_mediaRecorder->recorderState() << " Error: " << m_mediaRecorder->error();
    }
    
    // Check camera state
    if (!m_camera) {
        qCWarning(log_ui_camera) << "Cannot start recording - camera is null";
        emit recordingError("Camera not initialized");
        return;
    }
    
    if (!m_camera->isActive()) {
        qCWarning(log_ui_camera) << "Cannot start recording - camera is not active";
        qCWarning(log_ui_camera) << "Camera active:" << m_camera->isActive() << "Error:" << m_camera->error();
        emit recordingError("Camera is not active - please start camera first");
        return;
    }
    
    // Check if media recorder exists
    if (!m_mediaRecorder) {
        qCWarning(log_ui_camera) << "Cannot start recording - media recorder not initialized";
        emit recordingError("Recording system not ready");
        return;
    }

    // Generate output path
    QString outputPath = generateRecordingFilePath();
    qCInfo(log_ui_camera) << "Starting recording to:" << outputPath;
    
    // Store for reference
    m_currentRecordingPath = outputPath;

    // Make sure the output directory exists
    QFileInfo fileInfo(outputPath);
    QDir outputDir = fileInfo.dir();
    if (!outputDir.exists()) {
        bool created = outputDir.mkpath(".");
        qCDebug(log_ui_camera) << "Creating output directory" << outputDir.absolutePath() << "- Success:" << created;
        if (!created) {
            qCWarning(log_ui_camera) << "Failed to create output directory";
            emit recordingError("Failed to create output directory");
            m_currentRecordingPath.clear();
            return;
        }
    } else {
        qCDebug(log_ui_camera) << "Output directory exists:" << outputDir.absolutePath();
    }

#ifdef Q_OS_WIN
    // Windows: Use QMediaRecorder via QtBackendHandler for better control
    bool qtHandlerSucceeded = false;
    
    if (isQtBackend() && m_backendHandler) {
        try {
            // Use dynamic_cast for type safety instead of static_cast
            QtBackendHandler* qtHandler = dynamic_cast<QtBackendHandler*>(m_backendHandler.get());
            
            if (qtHandler) {
                // Get settings from global preferences or use defaults
                QString format = "mp4";
                int bitrate = 2000000; // Default 2 Mbps
                
                // Configure media recorder before starting
                configureMediaRecorderForRecording(outputPath);
                
                qtHandlerSucceeded = qtHandler->startRecording(outputPath, format, bitrate);
            } else {
                qCWarning(log_ui_camera) << "Failed to cast to QtBackendHandler despite isQtBackend() returning true";
            }
        } catch (const std::exception& e) {
            qCCritical(log_ui_camera) << "Exception during QtBackendHandler start recording:" << e.what();
        }
        
        bool success = qtHandlerSucceeded;
        if (success) {
            qCDebug(log_ui_camera) << "Successfully started recording via QtBackendHandler";
            emit recordingStarted();
        } else {
            qCWarning(log_ui_camera) << "Failed to start recording via QtBackendHandler";
            
            // Fallback to direct QMediaRecorder
            if (m_mediaRecorder) {
                // Configure QMediaRecorder directly
                m_mediaRecorder->record();
                
                // Check if recording actually started
                if (m_mediaRecorder->recorderState() == QMediaRecorder::RecordingState) {
                    qCDebug(log_ui_camera) << "Started recording via direct QMediaRecorder call";
                    emit recordingStarted();
                } else {
                    qCWarning(log_ui_camera) << "Failed to start recording with QMediaRecorder";
                    emit recordingError("Failed to start recording");
                    m_currentRecordingPath.clear();
                }
            }
        }
    } else if (m_mediaRecorder) {
        // Direct QMediaRecorder usage if no QtBackendHandler
        qCDebug(log_ui_camera) << "Using direct QMediaRecorder for recording";
        
        // Verify camera state before proceeding
        if (!m_camera->isActive()) {
            qCWarning(log_ui_camera) << "Camera not in active state. Active:" << m_camera->isActive();
            qCInfo(log_ui_camera) << "Attempting to start camera before recording";
            m_camera->start();
            
            // Give the camera a moment to start
            QThread::msleep(500);
            
            if (!m_camera->isActive()) {
                qCWarning(log_ui_camera) << "Failed to activate camera for recording. Active:" << m_camera->isActive();
                emit recordingError("Failed to activate camera for recording");
                m_currentRecordingPath.clear();
                return;
            }
        }
        
        configureMediaRecorderForRecording(outputPath);
        
        // Check media recorder state before setting up capture session
        if (m_mediaRecorder->recorderState() == QMediaRecorder::RecordingState) {
            qCWarning(log_ui_camera) << "Media recorder already in recording state - stopping first";
            m_mediaRecorder->stop();
            QThread::msleep(500); // Give it time to stop
        }
        
        // Make sure capture session is properly set up
        if (m_captureSession.camera() != m_camera.get()) {
            qCDebug(log_ui_camera) << "Setting camera in capture session";
            m_captureSession.setCamera(m_camera.get());
        } else {
            qCDebug(log_ui_camera) << "Capture session already has the current camera";
        }
        
        if (m_captureSession.recorder() != m_mediaRecorder.get()) {
            qCDebug(log_ui_camera) << "Setting recorder in capture session";
            m_captureSession.setRecorder(m_mediaRecorder.get());
        } else {
            qCDebug(log_ui_camera) << "Capture session already has the current recorder";
        }
        
        // Log current setup before starting
        qCInfo(log_ui_camera) << "Recording setup:"
                             << "Camera active:" << m_camera->isActive()
                             << "Recorder state:" << m_mediaRecorder->recorderState()
                             << "Output path:" << outputPath;
        
        // Start recording
        qCInfo(log_ui_camera) << "Starting recording with QMediaRecorder";
        m_mediaRecorder->record();
        
        // Check if recording actually started - wait a bit longer for it to initialize
        QTimer::singleShot(1000, this, [this]() {
            if (m_mediaRecorder->recorderState() == QMediaRecorder::RecordingState) {
                qCInfo(log_ui_camera) << "Recording confirmed as started successfully";
                emit recordingStarted();
            } else {
                qCWarning(log_ui_camera) << "Recording failed to start properly";
                QString errorMessage;
                
                // Check for specific error conditions
                if (m_mediaRecorder->error() != QMediaRecorder::NoError) {
                    QString errorStr = m_mediaRecorder->errorString();
                    int errorCode = static_cast<int>(m_mediaRecorder->error());
                    qCWarning(log_ui_camera) << "Media recorder error:" << errorCode << "-" << errorStr;
                    errorMessage = "Error " + QString::number(errorCode) + ": " + errorStr;
                } else {
                    // No explicit error, check if we can determine why
                    if (!m_camera->isActive()) {
                        errorMessage = "Camera is no longer active";
                    } else if (!QFile::exists(m_currentRecordingPath)) {
                        errorMessage = "Cannot create output file";
                    } else {
                        errorMessage = "Unknown recording error";
                    }
                    qCWarning(log_ui_camera) << "Recording failed without error code:" << errorMessage;
                }
                
                // Log additional diagnostic info
                qCInfo(log_ui_camera) << "Diagnostics:"
                                     << "Camera active:" << m_camera->isActive()
                                     << "Recorder state:" << m_mediaRecorder->recorderState();
                
                emit recordingError("Failed to start recording: " + errorMessage);
                m_currentRecordingPath.clear();
            }
        });
        
        qCDebug(log_ui_camera) << "Started recording via direct QMediaRecorder call (no QtBackendHandler)";
        qCDebug(log_ui_camera) << "Media recorder state:" << m_mediaRecorder->recorderState();
    }
#else
    // Linux/macOS: Use both QMediaRecorder and FFmpeg backend if available
    bool recordingStarted = false;
    
    // If we have FFmpeg backend, use it as primary
    if (FFmpegBackendHandler* ffmpeg = getFFmpegBackend()) {
        bool success = ffmpeg->startRecording(outputPath);
        if (success) {
            qCDebug(log_ui_camera) << "Started recording via FFmpegBackendHandler";
            recordingStarted = true;
        } else {
            qCWarning(log_ui_camera) << "Failed to start recording via FFmpegBackendHandler";
        }
    }
    
    // Also try QMediaRecorder (as backup or primary if FFmpeg failed)
    if (m_mediaRecorder) {
        configureMediaRecorderForRecording(outputPath);
        
        // Make sure capture session is properly set up
        if (m_captureSession.camera() != m_camera.get()) {
            qCDebug(log_ui_camera) << "Setting camera in capture session";
            m_captureSession.setCamera(m_camera.get());
        }
        
        if (m_captureSession.recorder() != m_mediaRecorder.get()) {
            qCDebug(log_ui_camera) << "Setting recorder in capture session";
            m_captureSession.setRecorder(m_mediaRecorder.get());
        }
        
        m_mediaRecorder->record();
        
        if (m_mediaRecorder->recorderState() == QMediaRecorder::RecordingState) {
            qCDebug(log_ui_camera) << "Started recording via QMediaRecorder on non-Windows platform";
            recordingStarted = true;
        } else {
            qCWarning(log_ui_camera) << "Failed to start recording via QMediaRecorder";
            qCWarning(log_ui_camera) << "Media recorder error:" << m_mediaRecorder->errorString();
        }
    }
    
    if (recordingStarted) {
        emit recordingStarted();
    } else {
        qCWarning(log_ui_camera) << "Failed to start recording with any available backend";
        
        // Generate more specific error message based on recorder state
        QString errorMsg = "Failed to start recording";
        
        if (m_mediaRecorder) {
            QMediaRecorder::Error recError = m_mediaRecorder->error();
            if (recError != QMediaRecorder::NoError) {
                errorMsg = QString("Recording failed: %1").arg(getMediaRecorderErrorInfo(recError));
                qCWarning(log_ui_camera) << "Media recorder error details:" << getMediaRecorderErrorInfo(recError);
            }
        }
        
        // Dump full diagnostics to log for debugging
        dumpRecordingSystemState();
        
        emit recordingError(errorMsg);
        m_currentRecordingPath.clear();
    }
#endif
}

void CameraManager::stopRecording()
{
    qCInfo(log_ui_camera) << "=== STOP RECORDING PROCESS INITIATED ===";
    
    // Check if we're actually recording before attempting to stop
    if (!isRecording()) {
        qCWarning(log_ui_camera) << "No active recording to stop";
        qCDebug(log_ui_camera) << "Camera state: " << (m_camera ? (m_camera->isActive() ? "Active" : "Inactive") : "NULL");
        qCDebug(log_ui_camera) << "Recorder state: " << (m_mediaRecorder ? 
                                                      QString("Valid (State: %1, Error: %2)").arg(m_mediaRecorder->recorderState())
                                                                                           .arg(m_mediaRecorder->error()) : 
                                                      "NULL");
        qCDebug(log_ui_camera) << "=== STOP RECORDING ABORTED - NOT RECORDING ===";
        emit recordingStopped(); // Emit signal to ensure UI stays in sync
        return;
    }
    
    QString recordingPath = m_currentRecordingPath;
    qCDebug(log_ui_camera) << "Stopping recording:" << recordingPath;
    qCDebug(log_ui_camera) << "Backend type: " << (m_backendHandler ? 
                                                static_cast<int>(m_backendHandler->getBackendType()) : -1);
    qCDebug(log_ui_camera) << "Camera state: " << (m_camera ? (m_camera->isActive() ? "Active" : "Inactive") : "NULL");
    
#ifdef Q_OS_WIN
    // Windows: Use QMediaRecorder via QtBackendHandler
    if (isQtBackend() && m_backendHandler) {
        try {
            // Use dynamic_cast for type safety instead of static_cast
            QtBackendHandler* qtHandler = dynamic_cast<QtBackendHandler*>(m_backendHandler.get());
            
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
    
    // Also call QMediaRecorder directly to ensure it's stopped
    if (m_mediaRecorder) {
        qCDebug(log_ui_camera) << "Recorder state before stop:" << m_mediaRecorder->recorderState();
        m_mediaRecorder->stop();
        qCDebug(log_ui_camera) << "Stopped recording via direct QMediaRecorder call";
        qCDebug(log_ui_camera) << "Recorder state after stop:" << m_mediaRecorder->recorderState();
    }
#else
    // Linux/macOS: Use both QMediaRecorder and FFmpeg backend if available
    if (m_mediaRecorder) {
        qCDebug(log_ui_camera) << "Recorder state before stop:" << m_mediaRecorder->recorderState();
        m_mediaRecorder->stop();
        qCDebug(log_ui_camera) << "Stopped recording via QMediaRecorder on non-Windows platform";
        qCDebug(log_ui_camera) << "Recorder state after stop:" << m_mediaRecorder->recorderState();
    }
    
    // If we have FFmpeg backend, also stop its recording
    if (FFmpegBackendHandler* ffmpeg = getFFmpegBackend()) {
        ffmpeg->stopRecording();
        qCDebug(log_ui_camera) << "Stopped recording via FFmpegBackendHandler";
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
                            
                            // Reset recording system since we encountered an issue
                            QTimer::singleShot(0, this, &CameraManager::resetRecordingSystem);
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
                
                // Reset recording system since we encountered an issue
                QTimer::singleShot(0, this, &CameraManager::resetRecordingSystem);
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
#ifdef Q_OS_WIN
    // Windows: QMediaRecorder doesn't support pause, so no action
    // Future enhancement: Could add Windows-specific pause code here
#else
    // Linux/macOS: Use FFmpeg backend for pause functionality
    if (FFmpegBackendHandler* ffmpeg = getFFmpegBackend()) {
        ffmpeg->pauseRecording();
    }
#endif
}

void CameraManager::resumeRecording()
{
#ifdef Q_OS_WIN
    // Windows: QMediaRecorder doesn't support resume, so no action
    // Future enhancement: Could add Windows-specific resume code here
#else
    // Linux/macOS: Use FFmpeg backend for resume functionality
    if (FFmpegBackendHandler* ffmpeg = getFFmpegBackend()) {
        ffmpeg->resumeRecording();
    }
#endif
}

bool CameraManager::isRecording() const
{
    bool recording = false;
    
    // First check if we have an active recording path
    if (!m_currentRecordingPath.isEmpty()) {
        qCDebug(log_ui_camera) << "Recording path is set:" << m_currentRecordingPath;
        
        // Path is set, but validate with actual recorder state
        QFileInfo fileInfo(m_currentRecordingPath);
        if (fileInfo.exists()) {
            qCDebug(log_ui_camera) << "Recording file exists, size:" << fileInfo.size() << "bytes";
        }
    }

#ifdef Q_OS_WIN
    // Windows: Check QtBackendHandler first, then fall back to QMediaRecorder
    if (isQtBackend() && m_backendHandler) {
        QtBackendHandler* qtHandler = static_cast<QtBackendHandler*>(m_backendHandler.get());
        if (qtHandler->isRecording()) {
            qCDebug(log_ui_camera) << "QtBackendHandler reports recording active";
            recording = true;
        } else {
            qCDebug(log_ui_camera) << "QtBackendHandler reports no active recording";
        }
    } else {
        qCDebug(log_ui_camera) << "No QtBackendHandler available";
    }
    
    if (m_mediaRecorder) {
        if (m_mediaRecorder->recorderState() == QMediaRecorder::RecordingState) {
            qCDebug(log_ui_camera) << "QMediaRecorder reports active recording";
            recording = true;
        } else {
            qCDebug(log_ui_camera) << "QMediaRecorder reports not recording, state:" << m_mediaRecorder->recorderState();
            if (m_mediaRecorder->error() != QMediaRecorder::NoError) {
                qCDebug(log_ui_camera) << "QMediaRecorder has error:" << m_mediaRecorder->errorString();
            }
        }
    } else {
        qCDebug(log_ui_camera) << "No QMediaRecorder available";
    }
#else
    // Linux/macOS: Prefer FFmpeg backend status, fall back to QMediaRecorder
    if (FFmpegBackendHandler* ffmpeg = getFFmpegBackend()) {
        if (ffmpeg->isRecording()) {
            qCDebug(log_ui_camera) << "FFmpeg backend reports recording active";
            recording = true;
        } else {
            qCDebug(log_ui_camera) << "FFmpeg backend reports no active recording";
        }
    } else {
        qCDebug(log_ui_camera) << "No FFmpeg backend available";
    }
    
    if (m_mediaRecorder) {
        if (m_mediaRecorder->recorderState() == QMediaRecorder::RecordingState) {
            qCDebug(log_ui_camera) << "QMediaRecorder reports active recording";
            recording = true;
        } else {
            qCDebug(log_ui_camera) << "QMediaRecorder reports not recording, state:" << m_mediaRecorder->recorderState();
            if (m_mediaRecorder->error() != QMediaRecorder::NoError) {
                qCDebug(log_ui_camera) << "QMediaRecorder has error:" << m_mediaRecorder->errorString();
            }
        }
    } else {
        qCDebug(log_ui_camera) << "No QMediaRecorder available";
    }
#endif
    
    qCDebug(log_ui_camera) << "Final recording status:" << (recording ? "ACTIVE" : "NOT ACTIVE");
    return recording;
}

bool CameraManager::isPaused() const
{
#ifdef Q_OS_WIN
    // Windows: QMediaRecorder doesn't support pause state tracking
    return false;
#else
    // Linux/macOS: Use FFmpeg backend for pause state
    if (FFmpegBackendHandler* ffmpeg = getFFmpegBackend()) {
        return ffmpeg->isRecording() && ffmpeg->isPaused();
    }
#endif
    
    return false;
}

// Helper method to generate a file path for recording
QString CameraManager::generateRecordingFilePath() const
{
    // First check if we have a path stored in GlobalSettings
    QString configuredPath = GlobalSetting::instance().getRecordingOutputPath();
    
    if (!configuredPath.isEmpty()) {
        qCDebug(log_ui_camera) << "Using configured recording path from settings:" << configuredPath;
        
        // Extract just the directory part and ensure it exists
        QFileInfo fileInfo(configuredPath);
        QString outputDir = fileInfo.dir().absolutePath();
        
        if (!QDir(outputDir).exists()) {
            QDir().mkpath(outputDir);
        }
        
        // Use the configured directory but with a timestamp for the filename to avoid overwriting
        QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd_hh-mm-ss");
        QString extension = fileInfo.suffix().isEmpty() ? "mp4" : fileInfo.suffix();
        QString filePath = QString("%1/openterface_recording_%2.%3").arg(outputDir).arg(timestamp).arg(extension);
        
        qCDebug(log_ui_camera) << "Generated recording file path from configured directory:" << filePath;
        return filePath;
    }
    
    // If no path is configured, use the default behavior
    // Try multiple locations in order of preference
    QStringList potentialLocations = {
        QStandardPaths::writableLocation(QStandardPaths::MoviesLocation),
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
        QStandardPaths::writableLocation(QStandardPaths::HomeLocation),
        QDir::homePath(),
        QStandardPaths::writableLocation(QStandardPaths::DesktopLocation)
    };
    
    QString outputDir;
    for (const QString& location : potentialLocations) {
        if (!location.isEmpty()) {
            outputDir = location;
            qCDebug(log_ui_camera) << "Trying location for recordings:" << outputDir;
            break;
        }
    }
    
    if (outputDir.isEmpty()) {
        qCWarning(log_ui_camera) << "Could not find a valid location for recordings, using temp";
        outputDir = QDir::tempPath();
    }
    
    // Create a specific directory for Openterface recordings
    QString appOutputDir = outputDir + QDir::separator() + "OpenterfaceRecordings";
    QDir dir(appOutputDir);
    
    if (!dir.exists()) {
        if (!dir.mkpath(".")) {
            qCWarning(log_ui_camera) << "Failed to create recording directory:" << appOutputDir << "- falling back to:" << outputDir;
            appOutputDir = outputDir;
        } else {
            qCDebug(log_ui_camera) << "Created recording directory:" << appOutputDir;
        }
    }
    
    // Test if we can write to the directory
    QString testPath = appOutputDir + QDir::separator() + "test_write_permission.tmp";
    QFile testFile(testPath);
    if (!testFile.open(QIODevice::WriteOnly)) {
        qCWarning(log_ui_camera) << "Cannot write to" << appOutputDir << ":" << testFile.errorString();
        qCWarning(log_ui_camera) << "Falling back to temp directory";
        appOutputDir = QDir::tempPath() + QDir::separator() + "OpenterfaceRecordings";
        
        // Create the temp directory if needed
        QDir tempDir(appOutputDir);
        if (!tempDir.exists()) {
            tempDir.mkpath(".");
        }
    } else {
        // Cleanup test file
        testFile.close();
        testFile.remove();
    }
    
    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd_hh-mm-ss");
    QString filePath = QString("%1/openterface_recording_%2.mp4").arg(appOutputDir).arg(timestamp);
    
    qCDebug(log_ui_camera) << "Generated recording file path:" << filePath;
    return filePath;
}

void CameraManager::configureMediaRecorderForRecording(const QString& outputPath)
{
    qCInfo(log_ui_camera) << "=== CONFIGURING MEDIA RECORDER ===";
    qCDebug(log_ui_camera) << "Output path: " << outputPath;
    
    if (!m_mediaRecorder) {
        qCWarning(log_ui_camera) << "Cannot configure media recorder - it is null";
        qCDebug(log_ui_camera) << "=== MEDIA RECORDER CONFIGURATION FAILED - NULL RECORDER ===";
        return;
    }
    
    // Log detailed recorder state before configuration
    qCDebug(log_ui_camera) << "Initial recorder state: " << m_mediaRecorder->recorderState()
                          << " Error code: " << m_mediaRecorder->error()
                          << " Error string: " << m_mediaRecorder->errorString();
    // Create a QMediaFormat object to query supported formats
    QMediaFormat tempFormat;
    qCDebug(log_ui_camera) << "Available media formats: " << tempFormat.supportedFileFormats(QMediaFormat::Encode);
    
    // Check for recorder errors and reset if needed
    if (m_mediaRecorder->error() != QMediaRecorder::NoError) {
        qCWarning(log_ui_camera) << "Media recorder has error state:" << m_mediaRecorder->errorString() 
                                << "- Resetting recording system";
        resetRecordingSystem();
        
        // Verify we have a valid recorder after reset
        if (!m_mediaRecorder || m_mediaRecorder->error() != QMediaRecorder::NoError) {
            qCCritical(log_ui_camera) << "Failed to reset recording system - media recorder is still in error state";
            qCDebug(log_ui_camera) << "=== MEDIA RECORDER CONFIGURATION FAILED - RESET FAILED ===";
            return;
        }
        
        qCDebug(log_ui_camera) << "Recorder state after reset: " << m_mediaRecorder->recorderState() 
                              << " Error: " << m_mediaRecorder->error();
    }

    qCDebug(log_ui_camera) << "Configuring media recorder for recording to:" << outputPath;
    
    // Ensure the output directory exists
    QFileInfo fileInfo(outputPath);
    QDir outputDir = fileInfo.dir();
    if (!outputDir.exists()) {
        bool created = outputDir.mkpath(".");
        qCDebug(log_ui_camera) << "Creating output directory" << outputDir.absolutePath() << "- Success:" << created;
    }
    
    // On Windows, check file write permissions
    QFile testFile(outputPath + ".test");
    if (testFile.open(QIODevice::WriteOnly)) {
        testFile.close();
        testFile.remove();
        qCDebug(log_ui_camera) << "Output directory has write permissions";
    } else {
        qCWarning(log_ui_camera) << "Output directory doesn't have write permissions:" << testFile.errorString();
        
        // Try Documents folder as fallback
        QString docsPath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) 
                         + QDir::separator() + "OpenterfaceRecordings";
        QDir docsDir(docsPath);
        if (!docsDir.exists()) {
            if (!docsDir.mkpath(".")) {
                qCWarning(log_ui_camera) << "Failed to create fallback directory";
            }
        }
        
        // Generate a new path in the fallback directory
        QString newPath = docsPath + QDir::separator() + fileInfo.fileName();
        qCWarning(log_ui_camera) << "Falling back to:" << newPath;
        
        // Check if we can write to the fallback location
        QFile testFallbackFile(newPath + ".test");
        if (testFallbackFile.open(QIODevice::WriteOnly)) {
            testFallbackFile.close();
            testFallbackFile.remove();
            
            // Use the fallback path directly
            qCInfo(log_ui_camera) << "Using fallback path:" << newPath;
            // We'll use the fallback path below when creating the QUrl
        } else {
            qCWarning(log_ui_camera) << "Fallback location is also not writable:" << testFallbackFile.errorString();
        }
    }
    
    // Set output location
    QUrl outputUrl = QUrl::fromLocalFile(outputPath);
    
    // If there were problems with the path, we would have handled it above
    // and already set a log message with the fallback path
    
    m_mediaRecorder->setOutputLocation(outputUrl);
    qCDebug(log_ui_camera) << "Set output location:" << outputUrl.toString();
    qCDebug(log_ui_camera) << "Set output location:" << m_mediaRecorder->outputLocation().toString();
    
    // Check for recorder errors before starting
    if (m_mediaRecorder->error() != QMediaRecorder::NoError) {
        qCWarning(log_ui_camera) << "Media recorder has error before configuration:" 
                                << m_mediaRecorder->error() << "-" << m_mediaRecorder->errorString();
    }
    
    // Configure media format
    QMediaFormat mediaFormat;
    
    // Check supported formats - use the existing tempFormat variable
    auto supportedFileFormats = tempFormat.supportedFileFormats(QMediaFormat::Encode);
    qCDebug(log_ui_camera) << "Available file formats:" << supportedFileFormats;
    
    // Only set MPEG4 if it's supported
    if (supportedFileFormats.contains(QMediaFormat::FileFormat::MPEG4)) {
        mediaFormat.setFileFormat(QMediaFormat::FileFormat::MPEG4);
        qCDebug(log_ui_camera) << "Setting MP4 file format";
    } else if (!supportedFileFormats.isEmpty()) {
        // Fallback to first available format
        mediaFormat.setFileFormat(supportedFileFormats.first());
        qCWarning(log_ui_camera) << "MP4 not supported, using" << supportedFileFormats.first() << "instead";
    } else {
        qCWarning(log_ui_camera) << "No supported file formats available!";
    }
    
    // Check supported video codecs
    auto supportedVideoCodecs = tempFormat.supportedVideoCodecs(QMediaFormat::Encode);
    qCDebug(log_ui_camera) << "Available video codecs:" << supportedVideoCodecs;
    
    // Only set H264 if it's supported
    if (supportedVideoCodecs.contains(QMediaFormat::VideoCodec::H264)) {
        mediaFormat.setVideoCodec(QMediaFormat::VideoCodec::H264);
        qCDebug(log_ui_camera) << "Setting H.264 video codec";
    } else if (!supportedVideoCodecs.isEmpty()) {
        // Fallback to first available codec
        mediaFormat.setVideoCodec(supportedVideoCodecs.first());
        qCWarning(log_ui_camera) << "H.264 not supported, using" << supportedVideoCodecs.first() << "instead";
    }
    
    // Check supported audio codecs
    auto supportedAudioCodecs = tempFormat.supportedAudioCodecs(QMediaFormat::Encode);
    qCDebug(log_ui_camera) << "Available audio codecs:" << supportedAudioCodecs;
    
    // Only set AAC if it's supported
    if (supportedAudioCodecs.contains(QMediaFormat::AudioCodec::AAC)) {
        mediaFormat.setAudioCodec(QMediaFormat::AudioCodec::AAC);
        qCDebug(log_ui_camera) << "Setting AAC audio codec";
    } else if (!supportedAudioCodecs.isEmpty()) {
        // Fallback to first available codec
        mediaFormat.setAudioCodec(supportedAudioCodecs.first());
        qCWarning(log_ui_camera) << "AAC not supported, using" << supportedAudioCodecs.first() << "instead";
    }
    
    // Apply the media format
    m_mediaRecorder->setMediaFormat(mediaFormat);
    qCDebug(log_ui_camera) << "Applied media format:" 
                          << "File format:" << mediaFormat.fileFormat()
                          << "Video codec:" << mediaFormat.videoCodec()
                          << "Audio codec:" << mediaFormat.audioCodec();
    
    // Set quality
    m_mediaRecorder->setQuality(QMediaRecorder::Quality::HighQuality);
    qCDebug(log_ui_camera) << "Set quality to High";
    
    // Set encoding mode
    m_mediaRecorder->setEncodingMode(QMediaRecorder::EncodingMode::ConstantQualityEncoding);
    qCDebug(log_ui_camera) << "Set encoding mode to ConstantQuality";
    
    // Video frame rate
    int desiredFrameRate = 30;
    qCDebug(log_ui_camera) << "Desired frame rate:" << desiredFrameRate << "fps";
    
    // Set video resolution from camera format
    if (m_camera) {
        QCameraFormat format = m_camera->cameraFormat();
        QSize resolution = format.resolution();
        if (!resolution.isEmpty() && resolution.width() > 0 && resolution.height() > 0) {
            qCDebug(log_ui_camera) << "Using camera resolution for recording:" << resolution;
        } else {
            qCDebug(log_ui_camera) << "Camera format doesn't have a valid resolution";
        }
    }
    
    // Check available codecs and formats
    // Create a temporary QMediaFormat object to call the instance methods
    QMediaFormat format;
    qCDebug(log_ui_camera) << "Available file formats:" << format.supportedFileFormats(QMediaFormat::Encode);
    qCDebug(log_ui_camera) << "Available video codecs:" << format.supportedVideoCodecs(QMediaFormat::Encode);
    qCDebug(log_ui_camera) << "Available audio codecs:" << format.supportedAudioCodecs(QMediaFormat::Encode);
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
                        // Use simple approach on Windows, backend approach on others
                        if (isWindowsPlatform()) {
                            // Windows: Add delay to prevent broken frames during camera initialization
                            // This delay allows the camera hardware to stabilize before format configuration
                            qCDebug(log_ui_camera) << "Windows: Adding stabilization delay before format configuration...";
                            QTimer::singleShot(250, this, [this]() {
                                // Ensure camera is still active before configuring format
                                if (!m_camera || !m_camera->isActive()) {
                                    qCDebug(log_ui_camera) << "Windows: Camera no longer active, skipping format configuration";
                                    return;
                                }
                                
                                // Windows: Simple direct QCamera format configuration with delay
                                QCameraFormat currentFormat = m_camera->cameraFormat();
                                QSize resolution;
                                
                                if (currentFormat.isNull() || currentFormat.resolution().isEmpty()) {
                                    resolution = QSize(m_video_width > 0 ? m_video_width : 1920, 
                                                      m_video_height > 0 ? m_video_height : 1080);
                                    qCDebug(log_ui_camera) << "Windows: Using stored/default resolution:" << resolution;
                                } else {
                                    resolution = currentFormat.resolution();
                                    qCDebug(log_ui_camera) << "Windows: Got resolution from camera format:" << resolution;
                                    m_video_width = resolution.width();
                                    m_video_height = resolution.height();
                                }
                                
                                int fps = GlobalVar::instance().getCaptureFps() > 0 ? 
                                    GlobalVar::instance().getCaptureFps() : 30;
                                
                                QCameraFormat format = getVideoFormat(resolution, fps, QVideoFrameFormat::Format_Jpeg);
                                if (m_camera) {
                                    qCDebug(log_ui_camera) << "Windows: Setting camera format after stabilization delay";
                                    m_camera->setCameraFormat(format);
                                }
                            });
                        } else {
                            // Non-Windows: Use backend handler approach
                            configureResolutionAndFormat();
                        }
                    } catch (...) {
                        qCritical() << "Exception in configureResolutionAndFormat";
                    }
                }

                emit cameraActiveChanged(active);
            });
            
            connect(m_camera.get(), &QCamera::errorOccurred, this, [this](QCamera::Error error, const QString &errorString) {
                qCritical() << "Camera error occurred:" << static_cast<int>(error) << errorString;
                
                // Use backend handler for error handling if available (non-Windows)
                if (!isWindowsPlatform() && m_backendHandler) {
                    m_backendHandler->handleCameraError(error, errorString);
                } else {
                    qCDebug(log_ui_camera) << "Windows: Using simple error handling";
                }
                
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
            
            // Make sure capture session has recorder connected
            if (m_captureSession.recorder() != m_mediaRecorder.get()) {
                qCDebug(log_ui_camera) << "Setting media recorder in capture session during setup";
                m_captureSession.setRecorder(m_mediaRecorder.get());
            }
            
            // Set default audio input if available for recording with sound
            if (!QMediaDevices::audioInputs().isEmpty()) {
                // In Qt 6.5, audio inputs are handled differently
                // The capture session manages audio inputs
                m_captureSession.setAudioInput(new QAudioInput(QMediaDevices::defaultAudioInput(), this));
                qCDebug(log_ui_camera) << "Set default audio input:" << QMediaDevices::defaultAudioInput().description();
            } else {
                qCWarning(log_ui_camera) << "No audio input devices available for recording";
            }
            
            // Connect media recorder signals
            connect(m_mediaRecorder.get(), &QMediaRecorder::recorderStateChanged, this, [this](QMediaRecorder::RecorderState state) {
                qCDebug(log_ui_camera) << "Media recorder state changed to:" << static_cast<int>(state);
                if (state == QMediaRecorder::RecordingState) {
                    qCDebug(log_ui_camera) << "Recording started to:" << m_mediaRecorder->outputLocation().toLocalFile();
                    emit recordingStarted();
                } else if (state == QMediaRecorder::StoppedState) {
                    qCDebug(log_ui_camera) << "Recording stopped";
                    emit recordingStopped();
                }
            });
            
            // Add error handler for media recorder
            connect(m_mediaRecorder.get(), &QMediaRecorder::errorOccurred, this, [this](QMediaRecorder::Error error, const QString &errorString) {
                qCritical() << "Media recorder error occurred:" << static_cast<int>(error) << errorString;
                emit recordingError(errorString);
            });
            
            // Connect duration signal for monitoring
            connect(m_mediaRecorder.get(), &QMediaRecorder::durationChanged, this, [this](qint64 duration) {
                if (duration > 0 && duration % 5000 == 0) {  // Log every 5 seconds
                    qCDebug(log_ui_camera) << "Recording duration:" << duration / 1000 << "seconds";
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
    // On Windows, use simple Qt approach with Qt backend support
    // On Windows, use simple Qt approach with Qt backend support
    if (isWindowsPlatform()) {
        qCDebug(log_ui_camera) << "Windows platform: Using Qt camera format configuration";
        qCDebug(log_ui_camera) << "Windows platform: Using Qt camera format configuration";
        
        QSize resolution = QSize(m_video_width > 0 ? m_video_width : 1920, 
                                m_video_height > 0 ? m_video_height : 1080);
        int desiredFps = GlobalVar::instance().getCaptureFps() > 0 ? 
            GlobalVar::instance().getCaptureFps() : 30;
        
        qCDebug(log_ui_camera) << "Windows: Setting resolution:" << resolution << "fps:" << desiredFps;
        
        // Use Qt backend if available for format optimization
        if (isQtBackend()) {
            int optimalFps = getOptimalFrameRate(desiredFps);
            QCameraFormat format = getVideoFormat(resolution, optimalFps, QVideoFrameFormat::Format_Jpeg);
            setCameraFormat(format);
        } else {
            // Fallback to basic format selection
            QCameraFormat format = getVideoFormat(resolution, desiredFps, QVideoFrameFormat::Format_Jpeg);
            setCameraFormat(format);
        }
        // Use Qt backend if available for format optimization
        if (isQtBackend()) {
            int optimalFps = getOptimalFrameRate(desiredFps);
            QCameraFormat format = getVideoFormat(resolution, optimalFps, QVideoFrameFormat::Format_Jpeg);
            setCameraFormat(format);
        } else {
            // Fallback to basic format selection
            QCameraFormat format = getVideoFormat(resolution, desiredFps, QVideoFrameFormat::Format_Jpeg);
            setCameraFormat(format);
        }
        return;
    }
    
    // For GStreamer backend using direct pipeline, avoid all Qt camera interactions
    if (m_backendHandler && isGStreamerBackend()) {
        qCDebug(log_ui_camera) << "GStreamer backend detected - skipping all Qt camera format operations to avoid device conflicts";
        
        // Just set GStreamer backend configuration without accessing Qt camera
        QSize resolution = QSize(m_video_width > 0 ? m_video_width : 1920, 
                                m_video_height > 0 ? m_video_height : 1080);
        int desiredFps = GlobalVar::instance().getCaptureFps() > 0 ? 
            GlobalVar::instance().getCaptureFps() : 30;
        
        qCDebug(log_ui_camera) << "Configuring GStreamer backend with resolution:" << resolution << "fps:" << desiredFps;
        
        // Cast to GStreamer backend handler to access specific methods (non-Windows only)
#ifndef Q_OS_WIN
        auto* gstreamerHandler = qobject_cast<GStreamerBackendHandler*>(m_backendHandler.get());
        if (gstreamerHandler) {
            gstreamerHandler->setResolutionAndFramerate(resolution, desiredFps);
        } else {
            qCWarning(log_ui_camera) << "Failed to cast to GStreamer backend handler";
        }
#endif
        
        return; // Exit early for GStreamer to avoid all Qt camera access
    }
    
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
    
    // Get desired frame rate and optimize it using backend handler
    int desiredFps = GlobalVar::instance().getCaptureFps() > 0 ? 
        GlobalVar::instance().getCaptureFps() : 30;
    
    int optimalFps = getOptimalFrameRate(desiredFps);
    if (optimalFps != desiredFps) {
        qCDebug(log_ui_camera) << "Frame rate adjusted from" << desiredFps << "to" << optimalFps 
                               << "for backend compatibility";
    }
    
    // For GStreamer backend, pass resolution and framerate information
    if (m_backendHandler && isGStreamerBackend()) {
        qCDebug(log_ui_camera) << "Configuring GStreamer backend with resolution:" << resolution << "fps:" << optimalFps;
        
        // Cast to GStreamer backend handler to access specific methods (non-Windows only)
#ifndef Q_OS_WIN
        auto* gstreamerHandler = qobject_cast<GStreamerBackendHandler*>(m_backendHandler.get());
        if (gstreamerHandler) {
            gstreamerHandler->setResolutionAndFramerate(resolution, optimalFps);
        } else {
            qCWarning(log_ui_camera) << "Failed to cast to GStreamer backend handler";
        }
#endif
        
        // For GStreamer direct pipeline mode, skip Qt camera format setting to avoid device conflicts
        qCDebug(log_ui_camera) << "Skipping Qt camera format setting for GStreamer direct pipeline";
        return;
    }
    
    // For non-GStreamer backends, set Qt camera format for compatibility
    QCameraFormat format = getVideoFormat(resolution, optimalFps, QVideoFrameFormat::Format_Jpeg);
    setCameraFormat(format);
}

void CameraManager::setCameraFormat(const QCameraFormat &format) {
    if (m_camera) {
        qCDebug(log_ui_camera) << "Setting camera format:" 
                               << "resolution=" << format.resolution()
                               << "frameRate=" << format.minFrameRate() << "-" << format.maxFrameRate();
        
        // Validate format with backend handler if available (only on non-Windows)
        if (!isWindowsPlatform() && m_backendHandler && !format.isNull()) {
            validateCameraFormat(format);
        }
        
        m_camera->setCameraFormat(format);
        
        // Log the actual format that was set
        QCameraFormat actualFormat = m_camera->cameraFormat();
        if (!actualFormat.isNull()) {
            qCDebug(log_ui_camera) << "Actual format set:" 
                                   << "resolution=" << actualFormat.resolution()
                                   << "frameRate=" << actualFormat.minFrameRate() << "-" << actualFormat.maxFrameRate();
        }
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

    // Get optimal capture frame rate using backend handler
    int captureFrameRate = GlobalVar::instance().getCaptureFps();
    if (captureFrameRate > 0) {
        int optimalFrameRate = getOptimalFrameRate(captureFrameRate);
        if (optimalFrameRate != captureFrameRate) {
            qCDebug(log_ui_camera) << "Optimized capture frame rate from" << captureFrameRate 
                                   << "to" << optimalFrameRate << "for backend compatibility";
            // Note: We don't update GlobalVar here to preserve user preference,
            // but the optimal rate will be used during format selection
        }
    }

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
    // Use backend handler for format selection only on non-Windows platforms
    if (!isWindowsPlatform() && m_backendHandler) {
        qCDebug(log_ui_camera) << "Using backend handler for format selection:"
                               << "resolution=" << resolution
                               << "frameRate=" << desiredFrameRate
                               << "pixelFormat=" << static_cast<int>(pixelFormat);
        
        QCameraFormat selectedFormat = m_backendHandler->selectOptimalFormat(getCameraFormats(), resolution, desiredFrameRate, pixelFormat);
        
        if (!selectedFormat.isNull()) {
            qCDebug(log_ui_camera) << "Backend handler selected format:"
                                   << "resolution=" << selectedFormat.resolution()
                                   << "frameRate=" << selectedFormat.minFrameRate() << "-" << selectedFormat.maxFrameRate()
                                   << "pixelFormat=" << static_cast<int>(selectedFormat.pixelFormat());
            return selectedFormat;
        } else {
            qCWarning(log_ui_camera) << "Backend handler failed to select format, falling back to manual selection";
        }
    } else {
        if (isWindowsPlatform()) {
            qCDebug(log_ui_camera) << "Windows platform: Using fallback format selection";
        } else {
            qCDebug(log_ui_camera) << "No backend handler available, using fallback format selection";
        }
    }
    
    // Fallback to basic format selection if no backend handler or backend selection failed
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

QList<int> CameraManager::getSupportedFrameRates(const QCameraFormat& format) const
{
    if (!isWindowsPlatform() && m_backendHandler) {
        return m_backendHandler->getSupportedFrameRates(format);
    }
    
    // Fallback: return basic frame rate range
    QList<int> frameRates;
    int minRate = format.minFrameRate();
    int maxRate = format.maxFrameRate();
    
    if (minRate > 0 && maxRate > 0) {
        // Common frame rates within the supported range
        QList<int> commonRates = {5, 10, 15, 24, 25, 30, 50, 60};
        for (int rate : commonRates) {
            if (rate >= minRate && rate <= maxRate) {
                frameRates.append(rate);
            }
        }
        
        // Ensure min and max are included if not already
        if (!frameRates.contains(minRate)) {
            frameRates.prepend(minRate);
        }
        if (!frameRates.contains(maxRate)) {
            frameRates.append(maxRate);
        }
    }
    
    return frameRates;
}

bool CameraManager::isFrameRateSupported(const QCameraFormat& format, int frameRate) const
{
    if (!isWindowsPlatform() && m_backendHandler) {
        return m_backendHandler->isFrameRateSupported(format, frameRate);
    }
    
    // Fallback: basic range check
    return frameRate >= format.minFrameRate() && frameRate <= format.maxFrameRate();
}

int CameraManager::getOptimalFrameRate(int desiredFrameRate) const
{
    if (!m_camera) {
        qCWarning(log_ui_camera) << "No camera available for frame rate optimization";
        return desiredFrameRate;
    }
    
    QCameraFormat currentFormat = m_camera->cameraFormat();
    if (currentFormat.isNull()) {
        qCWarning(log_ui_camera) << "No camera format available for frame rate optimization";
        return desiredFrameRate;
    }
    
    if (!isWindowsPlatform() && m_backendHandler) {
        // Use backend handler to get the optimal frame rate
        QList<int> supportedRates = m_backendHandler->getSupportedFrameRates(currentFormat);
        
        if (supportedRates.isEmpty()) {
            return desiredFrameRate;
        }
        
        // Find the closest supported frame rate
        int closestRate = supportedRates.first();
        int minDiff = qAbs(closestRate - desiredFrameRate);
        
        for (int rate : supportedRates) {
            int diff = qAbs(rate - desiredFrameRate);
            if (diff < minDiff) {
                minDiff = diff;
                closestRate = rate;
            }
        }
        
        qCDebug(log_ui_camera) << "Optimal frame rate for desired" << desiredFrameRate << "is" << closestRate;
        return closestRate;
    }
    
    // Fallback: basic range validation
    int minRate = currentFormat.minFrameRate();
    int maxRate = currentFormat.maxFrameRate();
    
    if (desiredFrameRate < minRate) {
        return minRate;
    } else if (desiredFrameRate > maxRate) {
        return maxRate;
    } else {
        return desiredFrameRate;
    }
}

QList<int> CameraManager::getAllSupportedFrameRates() const
{
    QList<int> allFrameRates;
    QSet<int> uniqueRates; // To avoid duplicates
    
    if (!m_camera) {
        qCWarning(log_ui_camera) << "No camera available for frame rate enumeration";
        return allFrameRates;
    }
    
    QList<QCameraFormat> formats = getCameraFormats();
    
    for (const QCameraFormat& format : formats) {
        QList<int> formatRates = getSupportedFrameRates(format);
        for (int rate : formatRates) {
            if (!uniqueRates.contains(rate)) {
                uniqueRates.insert(rate);
                allFrameRates.append(rate);
            }
        }
    }
    
    // Sort frame rates
    std::sort(allFrameRates.begin(), allFrameRates.end());
    
    qCDebug(log_ui_camera) << "All supported frame rates across formats:" << allFrameRates;
    return allFrameRates;
}

void CameraManager::validateCameraFormat(const QCameraFormat& format) const
{
    if (format.isNull()) {
        qCWarning(log_ui_camera) << "Camera format validation: format is null";
        return;
    }
    
    qCDebug(log_ui_camera) << "=== Camera Format Validation ===";
    qCDebug(log_ui_camera) << "Resolution:" << format.resolution();
    qCDebug(log_ui_camera) << "Frame rate range:" << format.minFrameRate() << "-" << format.maxFrameRate();
    qCDebug(log_ui_camera) << "Pixel format:" << static_cast<int>(format.pixelFormat());
    
    if (!isWindowsPlatform() && m_backendHandler) {
        QList<int> supportedRates = m_backendHandler->getSupportedFrameRates(format);
        qCDebug(log_ui_camera) << "Backend supported frame rates:" << supportedRates;
        
        // Test some common frame rates
        QList<int> testRates = {24, 25, 30, 60};
        for (int rate : testRates) {
            bool supported = m_backendHandler->isFrameRateSupported(format, rate);
            qCDebug(log_ui_camera) << "Frame rate" << rate << "supported:" << supported;
        }
    } else {
        if (isWindowsPlatform()) {
            qCDebug(log_ui_camera) << "Windows platform: Skipping backend format validation";
        } else {
            qCDebug(log_ui_camera) << "No backend handler available for format validation";
        }
    }
    
    qCDebug(log_ui_camera) << "=== End Format Validation ===";
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
        if (newCameraID.toInt() != 0 || newCameraID == "0") {
            newCameraID = "/dev/video" + newCameraID;
        }

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
            if (m_backendHandler) {
                m_backendHandler->stopCamera(m_camera.get());
            } else {
                m_camera->stop();
            }
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
        
        // Set up capture session with new camera using backend handler (keep video output to preserve last frame)
        qCDebug(log_ui_camera) << "Setting up capture session with new camera (preserving video output)";
        
        // Use backend handler for capture session setup to respect GStreamer direct pipeline mode
        if (m_backendHandler) {
            qCDebug(log_ui_camera) << "Using backend handler for capture session setup during camera switch";
            m_backendHandler->setupCaptureSession(&m_captureSession, m_camera.get());
            
            // For GStreamer direct pipeline, skip image capture setup to avoid device conflicts
            if (isGStreamerBackend()) {
                qCDebug(log_ui_camera) << "GStreamer backend detected - skipping image capture setup during switch to avoid device conflicts";
                // Don't set image capture for GStreamer to prevent Qt from accessing the V4L2 device
            } else {
                m_captureSession.setImageCapture(m_imageCapture.get());
            }
        } else {
            // Fallback: direct setup only if no backend handler
            qCDebug(log_ui_camera) << "No backend handler available, using direct capture session setup";
            m_captureSession.setCamera(m_camera.get());
            m_captureSession.setImageCapture(m_imageCapture.get());
        }
        
        // Video output should already be set and preserved from previous session
        // Only restore if it's somehow lost
        if (m_graphicsVideoOutput) {
            qDebug() << "Re-establishing graphics video output connection after camera switch";
            m_captureSession.setVideoOutput(m_graphicsVideoOutput);
        } else {
            qCWarning(log_ui_camera) << "No video output available to connect new camera";
        }
        
        // Restart camera if it was previously active
        if (wasActive) {
            qCDebug(log_ui_camera) << "Starting new camera after switch";
            startCamera();
            
            
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

bool CameraManager::initializeCameraWithVideoOutput(VideoPane* videoPane)
{
    qDebug() << "Initializing camera with VideoPane output";
    
    if (!videoPane) {
        qCWarning(log_ui_camera) << "Cannot initialize camera with null VideoPane";
        return false;
    }
    
    // Check if we're using FFmpeg backend for direct capture (only on non-Windows)
    if (!isWindowsPlatform() && isFFmpegBackend() && m_backendHandler) {
        qDebug() << "Using FFmpeg backend for direct capture";
        
        // Cast to FFmpegBackendHandler to access direct capture methods
#ifndef Q_OS_WIN
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
            
            // Start direct capture with Openterface device
            QString devicePath = "/dev/video0"; // Default fallback
            QSize resolution(1920, 1080); // Default resolution
            int framerate = 30; // Default framerate
            
            // Get the actual device path from DeviceManager
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
                        QString deviceId = device.id();
                        if (deviceId.startsWith("/dev/video")) {
                            devicePath = deviceId;
                        } else {
                            // Try to extract device number and construct path
                            QRegularExpression re("(\\d+)");
                            QRegularExpressionMatch match = re.match(deviceId);
                            if (match.hasMatch()) {
                                devicePath = "/dev/video" + match.captured(1);
                            }
                        }
                        qDebug() << "Found Openterface device via Qt detection:" << devicePath;
                        break;
                    }
                }
            }
            
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
            qCWarning(log_ui_camera) << "Failed to cast to FFmpegBackendHandler";
        }
#endif
    }
    
    // Fall back to standard Qt camera approach with QGraphicsVideoItem
    qDebug() << "Using standard Qt camera approach";
    videoPane->enableDirectFFmpegMode(false);
    return initializeCameraWithVideoOutput(videoPane->getVideoItem());
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
        if (m_graphicsVideoOutput) {
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

void CameraManager::refreshVideoOutput()
{
    qDebug() << "Refreshing video output connection";
    
    try {
        // Force re-establishment of video output connection to ensure new camera feed is displayed
        if (m_graphicsVideoOutput) {
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
                
                emit cameraDeviceDisconnected(prevDevice);
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
            emit cameraDeviceConnected(currentDevice);
            
            // Auto-switch to new Openterface device if no current device is active
            if (currentDevice.description().contains("Openterface", Qt::CaseInsensitive) && 
                !hasActiveCameraDevice()) {
                qCInfo(log_ui_camera) << "Auto-switching to new Openterface camera device:" << currentDevice.description();
                
                bool switchSuccess = switchToCameraDevice(currentDevice);
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
                qCDebug(log_ui_camera) << "CameraManager: Attempting camera deactivation for unplugged device port:" << device.portChain;
                
                // Only deactivate camera if the device has a camera component
                if (!device.hasCameraDevice()) {
                    qCDebug(log_ui_camera) << "Device at port" << device.portChain << "has no camera component, skipping camera deactivation";
                    return;
                }
                
                // Check if the unplugged device matches the current camera device port chain
                if (!m_currentCameraPortChain.isEmpty() && m_currentCameraPortChain == device.portChain) {
                    qCInfo(log_ui_camera) << "Deactivating camera for unplugged device at port:" << device.portChain;
                    bool deactivated = deactivateCameraByPortChain(device.portChain);
                    if (deactivated) {
                        qCInfo(log_ui_camera) << "✓ Camera deactivated for unplugged device at port:" << device.portChain;
                    }
                } else {
                    qCDebug(log_ui_camera) << "Camera deactivation skipped - port chain mismatch or no current camera. Current:" << m_currentCameraPortChain << "Unplugged:" << device.portChain;
                }
                
                // For Windows: Also manually check for Qt camera device changes
                if (isWindowsPlatform()) {
                    onVideoInputsChanged();
                }
            });
            
    // Connect to new device plugged in signal
    connect(hotplugMonitor, &HotplugMonitor::newDevicePluggedIn,
            this, [this](const DeviceInfo& device) {
                qCDebug(log_ui_camera) << "CameraManager: Attempting camera auto-switch for new device port:" << device.portChain;
                
                // Only attempt auto-switch if the device has a camera component
                if (!device.hasCameraDevice()) {
                    qCDebug(log_ui_camera) << "Device at port" << device.portChain << "has no camera component, skipping camera auto-switch";
                    return;
                }
                
                // Check if there's currently an active camera device
                if (hasActiveCameraDevice()) {
                    qCDebug(log_ui_camera) << "Camera device already active, skipping auto-switch to port:" << device.portChain;
                    return;
                }
                
                qCDebug(log_ui_camera) << "No active camera device found, attempting to switch to new device";
                
                // Try to auto-switch to the new camera device
                bool switchSuccess = tryAutoSwitchToNewDevice(device.portChain);
                if (switchSuccess) {
                    qCInfo(log_ui_camera) << "✓ Camera auto-switched to new device at port:" << device.portChain;
                } else {
                    qCDebug(log_ui_camera) << "Camera auto-switch failed for port:" << device.portChain;
                }
                
                // For Windows: Also manually check for Qt camera device changes
                if (isWindowsPlatform()) {
                    onVideoInputsChanged();
                }
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
    // On Windows, FFmpeg backend is not used, so skip this handling
    if (isWindowsPlatform()) {
        qCDebug(log_ui_camera) << "Windows platform: Skipping FFmpeg device disconnection handling";
        return;
    }
    
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
            if (m_camera) {
                m_camera->stop();
            }
            
            // Switch to the new device
            if (switchToCameraDevice(replacementDevice)) {
                qCInfo(log_ui_camera) << "Successfully switched to replacement device:" << replacementDevice.description();
                
                // Restart the camera
                if (m_camera) {
                    startCamera();
                }
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

void CameraManager::resetRecordingSystem()
{
    qCInfo(log_ui_camera) << "Resetting recording system";
    
    // Clear recording path
    m_currentRecordingPath.clear();
    
    // Stop any active recording
    if (isRecording()) {
        qCDebug(log_ui_camera) << "Stopping active recording before reset";
        stopRecording();
        QThread::msleep(500); // Wait for the recording to stop
    }
    
    // Disconnect recorder from capture session
    if (m_captureSession.recorder() == m_mediaRecorder.get()) {
        qCDebug(log_ui_camera) << "Disconnecting recorder from capture session";
        m_captureSession.setRecorder(nullptr);
    }
    
    // Re-create the media recorder
    qCDebug(log_ui_camera) << "Re-creating media recorder";
    m_mediaRecorder.reset(new QMediaRecorder());
    
    // Re-connect the recorder to capture session
    qCDebug(log_ui_camera) << "Re-connecting recorder to capture session";
    m_captureSession.setRecorder(m_mediaRecorder.get());
    
    // Re-connect all signals
    setupConnections();
    
    qCInfo(log_ui_camera) << "Recording system reset complete";
    
    // Dump recording system state after reset
    dumpRecordingSystemState();
}

QString CameraManager::getMediaRecorderErrorInfo(QMediaRecorder::Error error) const
{
    switch (error) {
        case QMediaRecorder::NoError:
            return "No error";
        case QMediaRecorder::ResourceError:
            return "Resource error (device or resource is not available)";
        case QMediaRecorder::FormatError:
            return "Format error (specified container format is not supported)";
        case QMediaRecorder::OutOfSpaceError:
            return "Out of disk space error";
        case QMediaRecorder::LocationNotWritable:
            return "Location not writable (insufficient permissions or read-only location)";
        default:
            return QString("Unknown error code: %1").arg(error);
    }
}

QString CameraManager::getRecordingSystemDiagnostics() const
{
    QString diagnostics;
    QTextStream stream(&diagnostics);
    
    stream << "====== RECORDING SYSTEM DIAGNOSTICS ======\n";
    
    // Camera state
    stream << "CAMERA:\n";
    if (m_camera) {
        stream << " - Status: Active = " << (m_camera->isActive() ? "YES" : "NO") << "\n";
        stream << " - Error code: " << m_camera->error() << "\n";
        
        if (!m_currentCameraDevice.isNull()) {
            stream << " - Device ID: " << QString::fromUtf8(m_currentCameraDevice.id()) << "\n";
            stream << " - Device description: " << m_currentCameraDevice.description() << "\n";
            
            // Current format
            QCameraFormat format = m_camera->cameraFormat();
            if (!format.isNull()) {
                stream << " - Resolution: " << format.resolution().width() << "x" << format.resolution().height() << "\n";
                stream << " - Frame rate: " << format.maxFrameRate() << " FPS\n";
                stream << " - Pixel format: " << format.pixelFormat() << "\n";
            } else {
                stream << " - No camera format set\n";
            }
        } else {
            stream << " - No camera device selected\n";
        }
    } else {
        stream << " - Camera is NULL\n";
    }
    
    // Media recorder state
    stream << "\nMEDIA RECORDER:\n";
    if (m_mediaRecorder) {
        stream << " - State: " << m_mediaRecorder->recorderState() << "\n";
        stream << " - Error: " << m_mediaRecorder->error() << " (" << getMediaRecorderErrorInfo(m_mediaRecorder->error()) << ")\n";
        stream << " - Error string: " << m_mediaRecorder->errorString() << "\n";
        stream << " - Actual location: " << m_mediaRecorder->actualLocation().toString() << "\n";
        stream << " - Duration: " << m_mediaRecorder->duration() << " ms\n";
        
        // Media format
        QMediaFormat format = m_mediaRecorder->mediaFormat();
        stream << " - File format: " << static_cast<int>(format.fileFormat()) << "\n";
        stream << " - Audio codec: " << static_cast<int>(format.audioCodec()) << "\n";
        stream << " - Video codec: " << static_cast<int>(format.videoCodec()) << "\n";
        stream << " - Video resolution: " << m_mediaRecorder->videoResolution().width() << "x" 
               << m_mediaRecorder->videoResolution().height() << "\n";
        stream << " - Video frame rate: " << m_mediaRecorder->videoFrameRate() << "\n";
    } else {
        stream << " - Media recorder is NULL\n";
    }
    
    // Capture session state
    stream << "\nCAPTURE SESSION:\n";
    // Since recorder() is not const-qualified in Qt 6.5.3, we'll just indicate if it's expected to be connected
    stream << " - Camera connected: " << (m_camera != nullptr ? "YES" : "NO") << "\n";
    stream << " - Recorder connected: " << (m_mediaRecorder != nullptr ? "YES" : "NO") << "\n";
    stream << " - Video output connected: " << "Unknown in const context" << "\n";
    
    // Backend handler info
    stream << "\nBACKEND HANDLER:\n";
    if (m_backendHandler) {
        stream << " - Type: ";
        if (isQtBackend()) {
            stream << "Qt\n";
        } else if (isGStreamerBackend()) {
            stream << "GStreamer\n";
        } else if (isFFmpegBackend()) {
            stream << "FFmpeg\n";
        } else {
            stream << "Unknown\n";
        }
    } else {
        stream << " - No backend handler available\n";
    }
    
    // Recording file info
    stream << "\nRECORDING PATH:\n";
    if (!m_currentRecordingPath.isEmpty()) {
        stream << " - Current recording path: " << m_currentRecordingPath << "\n";
        QFileInfo fileInfo(m_currentRecordingPath);
        stream << " - Directory exists: " << (fileInfo.dir().exists() ? "YES" : "NO") << "\n";
        stream << " - File exists: " << (fileInfo.exists() ? "YES" : "NO") << "\n";
        if (fileInfo.exists()) {
            stream << " - File size: " << fileInfo.size() << " bytes\n";
            stream << " - File writeable: " << (fileInfo.isWritable() ? "YES" : "NO") << "\n";
        }
    } else {
        stream << " - No active recording path\n";
    }
    
    // Supported codecs and formats
    stream << "\nSUPPORTED FORMATS:\n";
    stream << " - File formats: ";
    // Create a QMediaFormat object to query supported formats
    QMediaFormat mediaFormat;
    const auto fileFormats = mediaFormat.supportedFileFormats(QMediaFormat::Encode);
    for (const auto& format : fileFormats) {
        stream << mediaFormat.fileFormatName(format) << " ";
    }
    stream << "\n";
    
    stream << " - Video codecs: ";
    const auto videoCodecs = mediaFormat.supportedVideoCodecs(QMediaFormat::Encode);
    for (const auto& codec : videoCodecs) {
        stream << mediaFormat.videoCodecName(codec) << " ";
    }
    stream << "\n";
    
    stream << "=======================================\n";
    
    return diagnostics;
}

void CameraManager::dumpRecordingSystemState() const
{
    QString diagnostics = getRecordingSystemDiagnostics();
    qCInfo(log_ui_camera) << "\n" << diagnostics;
}

void CameraManager::recoverRecordingSystem()
{
    qCInfo(log_ui_camera) << "=== MANUAL RECORDING SYSTEM RECOVERY INITIATED ===";
    
    // First log the current state
    dumpRecordingSystemState();
    
    // Stop any active recording first
    if (isRecording()) {
        qCInfo(log_ui_camera) << "Active recording detected - stopping first";
        stopRecording();
        QThread::msleep(1000); // Give it time to clean up
    }
    
    // Reset the recording system
    resetRecordingSystem();
    
    // Verify the system is in a good state
    if (m_mediaRecorder && m_mediaRecorder->error() == QMediaRecorder::NoError) {
        qCInfo(log_ui_camera) << "Recording system successfully recovered";
        emit recordingError("Recording system was reset successfully");
    } else {
        qCWarning(log_ui_camera) << "Recording system recovery failed";
        if (m_mediaRecorder) {
            qCWarning(log_ui_camera) << "Media recorder still has error:" << m_mediaRecorder->errorString();
        } else {
            qCWarning(log_ui_camera) << "Media recorder is null after reset attempt";
        }
        emit recordingError("Failed to recover recording system - please restart the application");
    }
    
    // Log the final state
    dumpRecordingSystemState();
}

QString CameraManager::getRecordingDiagnosticsReport() const
{
    // Get the full diagnostics
    QString report = getRecordingSystemDiagnostics();
    
    // Add system information
    report += "\n====== SYSTEM INFORMATION ======\n";
    report += "OS: " + QSysInfo::prettyProductName() + "\n";
    report += "Kernel: " + QSysInfo::kernelVersion() + "\n";
    report += "Architecture: " + QSysInfo::currentCpuArchitecture() + "\n";
    report += "Qt Version: " + QString(QT_VERSION_STR) + "\n";
    
    // Add camera devices information
    report += "\n====== CAMERA DEVICES ======\n";
    auto devices = getAvailableCameraDevices();
    report += QString("Found %1 camera devices:\n").arg(devices.size());
    
    int deviceIndex = 0;
    for (const auto& device : devices) {
        report += QString("Device %1:\n").arg(++deviceIndex);
        report += " - ID: " + QString::fromUtf8(device.id()) + "\n";
        report += " - Description: " + device.description() + "\n";
        report += " - Position: " + QString::number(static_cast<int>(device.position())) + "\n";
        
        // Available video formats
        auto formats = device.videoFormats();
        report += QString(" - Available formats: %1\n").arg(formats.size());
        
        int formatIndex = 0;
        for (const auto& format : formats) {
            if (formatIndex++ < 5) { // Limit to 5 formats to avoid huge reports
                report += QString("   * %1x%2 @ %3fps, PixelFormat: %4\n")
                           .arg(format.resolution().width())
                           .arg(format.resolution().height())
                           .arg(format.maxFrameRate())
                           .arg(format.pixelFormat());
            }
        }
        if (formatIndex > 5) {
            report += QString("   * (and %1 more formats...)\n").arg(formatIndex - 5);
        }
    }
    
    return report;
}