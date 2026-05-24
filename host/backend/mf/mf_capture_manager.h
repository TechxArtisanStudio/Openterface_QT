#ifndef MF_CAPTURE_MANAGER_H
#define MF_CAPTURE_MANAGER_H

#include <QObject>
#include <QThread>
#include <QImage>
#include <QSize>
#include <QString>
#include <atomic>

// Forward declarations
struct IMFMediaSource;
struct IMFSourceReader;
class MfFrameProcessor;

class MfCaptureThread : public QThread {
    Q_OBJECT

public:
    explicit MfCaptureThread(QObject* parent = nullptr);
    void setRunning(bool running);
    void setSourceReader(IMFSourceReader* reader);
    void setFrameProcessor(MfFrameProcessor* processor);

signals:
    void frameReady(const QImage& frame);
    void captureError(const QString& error);
    void deviceDisconnected();

protected:
    void run() override;

private:
    IMFSourceReader* sourceReader_;
    MfFrameProcessor* frameProcessor_;
    std::atomic<bool> running_;
};

class MfCaptureManager : public QObject {
    Q_OBJECT

public:
    explicit MfCaptureManager(QObject* parent = nullptr);
    ~MfCaptureManager();

    bool initialize(const QString& deviceSymbolicLink, const QSize& resolution, int framerate);
    bool startCapture();
    void stopCapture();
    bool isCapturing() const;

signals:
    void frameReady(const QImage& frame);
    void captureError(const QString& error);
    void deviceDisconnected();

private:
    void cleanupMediaFoundation();

    IMFMediaSource* mediaSource_;
    IMFSourceReader* sourceReader_;
    MfCaptureThread* captureThread_;
    MfFrameProcessor* frameProcessor_;
    QSize resolution_;
    int framerate_;
    bool initialized_;
};

#endif // MF_CAPTURE_MANAGER_H
