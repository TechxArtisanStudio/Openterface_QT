#include "ChatScreenCapture.h"
#include "../host/cameramanager.h"
#include <QFile>
#include <QBuffer>
#include <QDir>
#include <QStandardPaths>
#include <QDateTime>
#include <QLoggingCategory>
#include <QPainter>

Q_DECLARE_LOGGING_CATEGORY(log_ai_chat)
Q_LOGGING_CATEGORY(log_ai_chat, "openterface.ai.chat")

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
    QString filename = QString("openterface_chat_%1.jpg")
        .arg(QDateTime::currentMSecsSinceEpoch());
    return QDir(dir).filePath(filename);
}

QString ChatScreenCapture::captureScreen()
{
    if (!m_cameraManager) {
        qCWarning(log_ai_chat) << "AI screen capture failed: no CameraManager set";
        return QString();
    }

    QImage frame = m_cameraManager->getLatestOriginalFrame();
    if (frame.isNull()) {
        qCWarning(log_ai_chat) << "AI screen capture failed: no frame available";
        return QString();
    }

    QString path = tempFilePath();
    if (frame.save(path, "JPEG", 85)) {
        qCDebug(log_ai_chat) << "AI screen capture success:" << path
                             << "size=" << frame.width() << "x" << frame.height();
        emit screenshotCaptured(path);
        return path;
    } else {
        qCWarning(log_ai_chat) << "AI screen capture failed: could not save to" << path;
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
