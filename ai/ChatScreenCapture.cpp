#include "ChatScreenCapture.h"
#include "../host/cameramanager.h"
#include <QFile>
#include <QFileInfo>
#include <QBuffer>
#include <QDir>
#include <QStandardPaths>
#include <QDateTime>
#include <QLoggingCategory>
#include <QPainter>
#include <QThread>
#include <QMetaObject>

Q_DECLARE_LOGGING_CATEGORY(log_ai_chat)

ChatScreenCapture::ChatScreenCapture(QObject *parent)
    : QObject(parent)
{
}

ChatScreenCapture &ChatScreenCapture::instance()
{
    static ChatScreenCapture inst;
    return inst;
}

void ChatScreenCapture::setCameraManager(CameraManager *cam)
{
    m_cameraManager = cam;
}

QString ChatScreenCapture::tempFilePath() const
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    // Use a subdirectory to keep /tmp clean
    QString openterfaceDir = QDir(dir).filePath("openterface");
    QDir().mkpath(openterfaceDir);
    QString filename = QString("openterface_chat_%1.jpg")
        .arg(QDateTime::currentMSecsSinceEpoch());
    return QDir(openterfaceDir).filePath(filename);
}

QString ChatScreenCapture::captureScreen()
{
    if (!m_cameraManager) {
        qCWarning(log_ai_chat) << "AI screen capture: no CameraManager set";
        return QString();
    }

    qint64 startTime = QDateTime::currentMSecsSinceEpoch();

    // The GStreamer pipeline is NOT thread-safe — the capture MUST happen on
    // the thread that owns m_cameraManager (the main thread). If we're being
    // called from a worker thread (e.g. ChatManager's QtConcurrent::run),
    // marshal the call to the main thread with BlockingQueuedConnection.
    if (QThread::currentThread() != m_cameraManager->thread()) {
        qCDebug(log_ai_chat) << "AI screen capture: marshaling to main thread via BlockingQueuedConnection";
        QString result;
        bool ok = QMetaObject::invokeMethod(this, [this, &result]() {
            result = doCaptureScreen();
        }, Qt::BlockingQueuedConnection);
        if (!ok) {
            qCWarning(log_ai_chat) << "AI screen capture: failed to marshal to main thread";
            return QString();
        }
        qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - startTime;
        qCDebug(log_ai_chat) << "AI screen capture: total time (including marshal) =" << elapsed << "ms";
        return result;
    }

    QString result = doCaptureScreen();
    qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - startTime;
    qCDebug(log_ai_chat) << "AI screen capture: total time =" << elapsed << "ms";
    return result;
}

QString ChatScreenCapture::doCaptureScreen()
{
    if (!m_cameraManager) {
        qCWarning(log_ai_chat) << "AI screen capture: no CameraManager set";
        return QString();
    }

    qCDebug(log_ai_chat) << "AI screen capture: requesting frame from CameraManager"
                         << "(backend GStreamer:" << m_cameraManager->isGStreamerBackend()
                         << "FFmpeg:" << m_cameraManager->isFFmpegBackend() << ")";

    QImage frame = m_cameraManager->getLatestOriginalFrame();
    if (frame.isNull()) {
        qCWarning(log_ai_chat) << "AI screen capture: frame is null/empty (no frame available from backend)";
        return QString();
    }

    qCDebug(log_ai_chat) << "AI screen capture: got frame"
                         << frame.width() << "x" << frame.height()
                         << "format=" << frame.format();

    QString path = tempFilePath();
    if (frame.save(path, "JPEG", 85)) {
        QFileInfo fi(path);
        qCDebug(log_ai_chat) << "AI screen capture success:" << path
                             << "bytes=" << fi.size()
                             << "size=" << frame.width() << "x" << frame.height();
        emit screenshotCaptured(path);
        return path;
    } else {
        qCWarning(log_ai_chat) << "AI screen capture: QImage.save() failed for" << path;
        return QString();
    }
}

QString ChatScreenCapture::dataURLForImage(const QString &filePath) const
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qCWarning(log_ai_chat) << "Failed to open image for data URL:" << filePath;
        return QString();
    }

    QByteArray data = file.readAll();
    file.close();

    QString base64 = QString::fromLatin1(data.toBase64());
    return QString("data:image/jpeg;base64,%1").arg(base64);
}

QString ChatScreenCapture::captureAnnotatedClick(int absX, int absY, const QString &actionName)
{
    // First capture the screen
    QString path = captureScreen();
    if (path.isEmpty()) return QString();

    // Load the image and draw an annotation
    QImage img(path);
    if (img.isNull()) return path;

    // Draw a crosshair/circle at the click position
    // Convert from 0-4096 to image coordinates
    int imgX = static_cast<int>(static_cast<double>(absX) / 4096.0 * img.width());
    int imgY = static_cast<int>(static_cast<double>(absY) / 4096.0 * img.height());

    QPainter painter(&img);
    painter.setPen(QPen(Qt::red, 3));
    int radius = 20;
    painter.drawEllipse(QPoint(imgX, imgY), radius, radius);
    painter.drawLine(imgX - radius - 5, imgY, imgX + radius + 5, imgY);
    painter.drawLine(imgX, imgY - radius - 5, imgX, imgY + radius + 5);
    painter.end();

    // Save annotated image
    QString annotatedPath = path;
    annotatedPath.replace(".jpg", "_annotated.jpg");
    img.save(annotatedPath, "JPEG", 85);

    return annotatedPath;
}
