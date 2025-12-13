#include "imagecapturer.h"
#include "cameramanager.h"
#include "../server/tcpServer.h"
#include <QDateTime>
#include <QStandardPaths>
#include <QDir>
#include <QThread>
#include <QImage>
#include <QLoggingCategory>

// 注意：log_ui_camera 日志分类已在 cameramanager.h 中声明，在 cameramanager.cpp 中定义
// 因此这里不需要重新定义

ImageCapturer::ImageCapturer(QObject *parent)
    : QObject(parent)
    , m_captureTimer(nullptr)
    , m_tcpServer(nullptr)
    , m_cameraManager(nullptr)
    , m_isCapturing(false)
    , m_interval(1000) // 默认1秒
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
    if (!cameraManager || !tcpServer) {
        qCWarning(log_ui_camera) << "Invalid parameters for image capturer";
        return;
    }
    
    // 保存状态信息
    m_cameraManager = cameraManager;
    m_tcpServer = tcpServer;
    m_savePath = savePath;
    m_interval = intervalSeconds * 1000;
    
    // 创建保存目录（如果不存在）
    QFileInfo fileInfo(m_savePath);
    QDir dir = fileInfo.dir();
    if (!dir.exists()) {
        if (!dir.mkpath(".")) {
            qCWarning(log_ui_camera) << "Failed to create directory:" << m_savePath;
            return;
        }
    }
    
    // 设置定时器
    m_captureTimer = new QTimer(this);
    connect(m_captureTimer, &QTimer::timeout, this, &ImageCapturer::captureImage);
    m_captureTimer->start(m_interval);
    
    m_isCapturing = true;
    qCDebug(log_ui_camera) << "Image capturing started with interval:" << intervalSeconds << "seconds";
}

// 新增的自动启动方法
void ImageCapturer::startCapturingAuto(CameraManager* cameraManager, TcpServer* tcpServer, const QString& savePath, int intervalSeconds)
{
    // 使用默认路径如果未提供保存路径
    QString path = savePath;
    if (path.isEmpty()) {
        path = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation) + "/openterface";
    }
    
    startCapturing(cameraManager, tcpServer, path, intervalSeconds);
    
    // 添加日志记录
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
        return;
    }
    
    try {
        // 构造完整文件路径
        QString fullPath = m_savePath + "/" + m_fileName;
        
        // 调用相机管理器的图像捕获方法
        m_cameraManager->takeImage(fullPath);
        
        // 通知TCP服务器图像路径已更新
        if (m_tcpServer) {
            m_tcpServer->handleImgPath(fullPath);
        }
        
        // 更新统计信息
        m_captureCount++;
        m_lastCaptureTime = QDateTime::currentDateTime();
        
        qCDebug(log_ui_camera) << "Image captured successfully";
    } catch (const std::exception& e) {
        qCWarning(log_ui_camera) << "Exception during image capture:" << e.what();
    } catch (...) {
        qCWarning(log_ui_camera) << "Unknown exception during image capture";
    }
}