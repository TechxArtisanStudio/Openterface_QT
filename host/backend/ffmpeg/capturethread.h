/*
 * CaptureThread - extraction from FFmpegBackendHandler
 * Handles the capture loop and device loss detection in a background thread.
 */

#ifndef CAPTURETHREAD_H
#define CAPTURETHREAD_H

#include <QThread>
#include <QMutex>
#include <QString>
#include <QPointer>

class FFmpegBackendHandler;

class CaptureThread : public QThread
{
    Q_OBJECT

public:
    explicit CaptureThread(FFmpegBackendHandler* handler, QObject* parent = nullptr);
    void setRunning(bool running);
    bool isRunning() const;

protected:
    void run() override;

signals:
    void frameAvailable();               // Trigger handler to decode/display a frame
    void deviceDisconnected();           // Prompt handler to deactivate device
    void readError(const QString& msg);  // Report detailed read errors

private:
    QPointer<FFmpegBackendHandler> m_handler;
    mutable QMutex m_mutex;
    bool m_running;
};

#endif // CAPTURETHREAD_H
