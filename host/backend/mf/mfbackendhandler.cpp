#include "mfbackendhandler.h"
#include "mf_capture_manager.h"
#include "mf_device_enumerator.h"
#include "ui/videopane.h"
#include "ui/globalsetting.h"

#include <QLoggingCategory>
#include <QThread>

Q_DECLARE_LOGGING_CATEGORY(log_multimedia_backend)

MfBackendHandler::MfBackendHandler(QObject* parent)
    : MultimediaBackendHandler(parent)
    , captureManager_(nullptr)
    , deviceEnumerator_(nullptr)
    , graphicsVideoItem_(nullptr)
    , videoPane_(nullptr)
    , resolution_(1920, 1080)
    , framerate_(30)
    , isRecording_(false)
{
    deviceEnumerator_ = new MfDeviceEnumerator();
}

MfBackendHandler::~MfBackendHandler()
{
    cleanupCamera();
}

bool MfBackendHandler::isBackendAvailable() const
{
#ifdef Q_OS_WIN
    return true;
#else
    return false;
#endif
}

void MfBackendHandler::setVideoOutput(QGraphicsVideoItem* videoOutput)
{
    disconnect(m_videoOutputConnection);
    m_videoOutputConnection = QMetaObject::Connection{};

    graphicsVideoItem_ = videoOutput;
    videoPane_ = nullptr;

    if (graphicsVideoItem_) {
        qCWarning(log_multimedia_backend) << "QGraphicsVideoItem not supported for Media Foundation backend; use VideoPane instead";
    }
}

void MfBackendHandler::setVideoOutput(VideoPane* videoPane)
{
    disconnect(m_videoOutputConnection);
    m_videoOutputConnection = QMetaObject::Connection{};

    videoPane_ = videoPane;
    graphicsVideoItem_ = nullptr;

    if (videoPane_) {
        qCDebug(log_multimedia_backend) << "VideoPane set for Media Foundation direct rendering";
        m_videoOutputConnection = connect(this, &MfBackendHandler::frameReadyImage,
                videoPane_, &VideoPane::updateVideoFrameFromImage,
                Qt::QueuedConnection);
    }
}

void MfBackendHandler::setDevicePath(const QString& devicePath)
{
    devicePath_ = devicePath;
}

void MfBackendHandler::setResolution(const QSize& resolution)
{
    resolution_ = resolution;
}

void MfBackendHandler::setFramerate(int framerate)
{
    framerate_ = framerate;
}

void MfBackendHandler::startCamera()
{
    qCInfo(log_multimedia_backend) << "Media Foundation backend: starting camera";

    if (devicePath_.isEmpty()) {
        // Auto-select first available device
        auto devices = deviceEnumerator_->enumerateVideoDevices();
        if (devices.isEmpty()) {
            emit backendError("No Media Foundation video capture devices found");
            return;
        }
        deviceSymbolicLink_ = devices[0].symbolicLink;
        qCInfo(log_multimedia_backend) << "Auto-selected device:" << devices[0].friendlyName;
    } else if (devicePath_.startsWith("video=")) {
        // Device path is a DirectShow-style name -- look up by friendly name
        QString targetName = devicePath_.mid(6); // Remove "video=" prefix
        auto devices = deviceEnumerator_->enumerateVideoDevices();
        for (const auto& dev : devices) {
            if (dev.friendlyName == targetName) {
                deviceSymbolicLink_ = dev.symbolicLink;
                qCInfo(log_multimedia_backend) << "Resolved device name to symbolic link:" << deviceSymbolicLink_;
                break;
            }
        }
        if (deviceSymbolicLink_.isEmpty()) {
            // Fall back to using the first device
            if (!devices.isEmpty()) {
                deviceSymbolicLink_ = devices[0].symbolicLink;
                qCWarning(log_multimedia_backend) << "Could not match device name, using first available device";
            } else {
                emit backendError("No Media Foundation devices available");
                return;
            }
        }
    } else {
        // Assume it's already a symbolic link
        deviceSymbolicLink_ = devicePath_;
    }

    if (!setupCaptureSession()) {
        emit backendError("Failed to set up Media Foundation capture session");
        return;
    }

    if (!captureManager_->startCapture()) {
        emit backendError("Failed to start Media Foundation capture");
        return;
    }

    qCInfo(log_multimedia_backend) << "Media Foundation camera started successfully";
}

void MfBackendHandler::stopCamera()
{
    qCInfo(log_multimedia_backend) << "Media Foundation backend: stopping camera";

    if (captureManager_) {
        captureManager_->stopCapture();
    }
}

void MfBackendHandler::cleanupCamera()
{
    qCInfo(log_multimedia_backend) << "Media Foundation backend: cleaning up camera";

    if (captureManager_) {
        captureManager_->stopCapture();
        captureManager_->deleteLater();
        captureManager_ = nullptr;
    }

    disconnect(m_videoOutputConnection);
    m_videoOutputConnection = QMetaObject::Connection{};
    graphicsVideoItem_ = nullptr;
    videoPane_ = nullptr;
}

bool MfBackendHandler::setupCaptureSession()
{
    if (captureManager_) {
        cleanupCamera();
    }

    captureManager_ = new MfCaptureManager(this);

    connect(captureManager_, &MfCaptureManager::frameReady,
            this, &MfBackendHandler::onFrameReady);
    connect(captureManager_, &MfCaptureManager::captureError,
            this, &MfBackendHandler::onCaptureError);
    connect(captureManager_, &MfCaptureManager::deviceDisconnected,
            this, &MfBackendHandler::onDeviceDisconnected);

    if (!captureManager_->initialize(deviceSymbolicLink_, resolution_, framerate_)) {
        qCCritical(log_multimedia_backend) << "Failed to initialize capture manager";
        return false;
    }

    return true;
}

bool MfBackendHandler::startRecording(const QString& outputPath, const QString& format, int videoBitrate)
{
    Q_UNUSED(outputPath);
    Q_UNUSED(format);
    Q_UNUSED(videoBitrate);
    qCWarning(log_multimedia_backend) << "Recording not yet implemented for Media Foundation backend";
    return false;
}

bool MfBackendHandler::stopRecording()
{
    if (isRecording_) {
        isRecording_ = false;
        return true;
    }
    return false;
}

bool MfBackendHandler::isRecording() const
{
    return isRecording_;
}

QStringList MfBackendHandler::getAvailableHardwareAccelerations() const
{
    return QStringList() << "cpu";
}

void MfBackendHandler::onFrameReady(const QImage& frame)
{
    emit frameReadyImage(frame);
}

void MfBackendHandler::onCaptureError(const QString& error)
{
    qCWarning(log_multimedia_backend) << "Media Foundation capture error:" << error;
    emit backendError(error);
}

void MfBackendHandler::onDeviceDisconnected()
{
    qCWarning(log_multimedia_backend) << "Media Foundation device disconnected";
    emit backendWarning("Device disconnected");
    stopCamera();
}
