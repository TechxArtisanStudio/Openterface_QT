#include "imagecapturer.h"
#include "cameramanager.h"
#include "../server/tcpServer.h"
#include <QDateTime>
#include <QStandardPaths>
#include <QDir>
#include <QThread>
#include <QImage>
#include <QLoggingCategory>
#include <QFileInfo>


ImageCapturer::ImageCapturer(QObject *parent)
    : QObject(parent)
    , m_captureTimer(nullptr)
    , m_tcpServer(nullptr)
    , m_cameraManager(nullptr)
    , m_isCapturing(false)
    , m_interval(1000) // default 1 second
    , m_fileName("real_time.jpg")
    , m_captureCount(0)
{
}

ImageCapturer::~ImageCapturer()
{
    if (m_captureTimer) {
        m_captureTimer->stop();
        delete m_captureTimer;
    }
}

void ImageCapturer::startCapturing(CameraManager* cameraManager, TcpServer* tcpServer, const QString& savePath, int intervalSeconds)
{
    if (!cameraManager) {
        qCWarning(log_ui_camera) << "Invalid parameters for image capturer: cameraManager is null";
        return;
    }
    
    // Save state information
    m_cameraManager = cameraManager;
    m_tcpServer = tcpServer;
    m_savePath = savePath;
    m_interval = intervalSeconds * 1000;
    
    // create save directory if it doesn't exist
    QFileInfo fileInfo(m_savePath);
    QDir dir = fileInfo.dir();
    if (!dir.exists()) {
        if (!dir.mkpath(dir.absolutePath())) {
            qCWarning(log_ui_camera) << "Failed to create directory:" << m_savePath;
            return;
        }
    }
    
    // set timer for periodic capture
    m_captureTimer = new QTimer(this);
    connect(m_captureTimer, &QTimer::timeout, this, &ImageCapturer::captureImage);
    m_captureTimer->start(m_interval);
    
    m_isCapturing = true;
    qCDebug(log_ui_camera) << "Image capturing started with interval:" << intervalSeconds << "seconds";
    qCDebug(log_ui_camera) << "Saving images to:" << m_savePath;
}

// Additional method to start capturing with default parameters
void ImageCapturer::startCapturingAuto(CameraManager* cameraManager, TcpServer* tcpServer, const QString& savePath, int intervalSeconds)
{
    // use default save path if none provided
    QString path = savePath;
    if (path.isEmpty()) {
        path = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation) + "/openterface";
    }
    
    startCapturing(cameraManager, tcpServer, path, intervalSeconds);
    
    qCDebug(log_ui_camera) << "Image capturing automatically started with interval:" << intervalSeconds << "seconds";
    qCDebug(log_ui_camera) << "Saving images to:" << path;
}

void ImageCapturer::stopCapturing()
{
    if (m_captureTimer) {
        m_captureTimer->stop();
        m_isCapturing = false;
        qCDebug(log_ui_camera) << "Image capturing stopped";
    }
}

void ImageCapturer::captureImage()
{
    if (!m_cameraManager || !m_isCapturing) {
        qCDebug(log_ui_camera) << "Cannot capture image: camera manager is null or not capturing";
        return;
    }
    
    // Construct full file path
    QString fullPath = m_savePath + "/" + m_fileName;
    
    // make sure directory exists
    QFileInfo fileInfo(fullPath);
    QDir dir = fileInfo.dir();
    if (!dir.exists()) {
        if (!dir.mkpath(dir.absolutePath())) {
            qCWarning(log_ui_camera) << "Failed to create directory for image capture:" << dir.absolutePath();
            return;
        }
    }
    
    try {
        // Use the CameraManager to take the image
        m_cameraManager->takeImage(fullPath);
        
        // update TCP server if available
        if (m_tcpServer) {
            m_tcpServer->handleImgPath(fullPath);
        }
        
        // update capture count and timestamp
        m_captureCount++;
        m_lastCaptureTime = QDateTime::currentDateTime();
        
        qCDebug(log_ui_camera) << "Image captured successfully to:" << fullPath;
    } catch (const std::exception& e) {
        qCWarning(log_ui_camera) << "Exception during image capture:" << e.what();
    } catch (...) {
        qCWarning(log_ui_camera) << "Unknown exception during image capture";
    }
}