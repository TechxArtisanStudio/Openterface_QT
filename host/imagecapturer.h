#ifndef IMAGECAPTURER_H
#define IMAGECAPTURER_H

#include <QObject>
#include <QTimer>
#include <QImage>
#include <QDateTime>
#include <QStandardPaths>
#include <QDir>
#include <QThread>

class TcpServer;
class CameraManager;

class ImageCapturer : public QObject
{
    Q_OBJECT

public:
    explicit ImageCapturer(QObject *parent = nullptr);
    ~ImageCapturer();
    
    // 自动启动捕获，带默认参数
    void startCapturingAuto(CameraManager* cameraManager, TcpServer* tcpServer, const QString& savePath = QString(), int intervalSeconds = 1);
    void startCapturing(CameraManager* cameraManager, TcpServer* tcpServer, const QString& savePath, int intervalSeconds = 1);
    void stopCapturing();
    bool isCapturing() const { return m_isCapturing; }

private slots:
    void captureImage();

private:
    QTimer* m_captureTimer;
    TcpServer* m_tcpServer;
    CameraManager* m_cameraManager;
    bool m_isCapturing;
    int m_interval;
    QString m_savePath;
    QString m_fileName;
    int m_captureCount;
    QDateTime m_lastCaptureTime;
};

#endif // IMAGECAPTURER_H