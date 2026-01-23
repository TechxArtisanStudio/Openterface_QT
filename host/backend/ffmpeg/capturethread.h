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
#include "icapture_frame_reader.h"

// Forward declarations
class FFmpegBackendHandler;
class FFmpegCaptureManager;

class CaptureThread : public QThread
{
    Q_OBJECT

public:
    // Constructor for FFmpegBackendHandler (legacy)
    explicit CaptureThread(FFmpegBackendHandler* handler, QObject* parent = nullptr);
    
    // Constructor for FFmpegCaptureManager
    explicit CaptureThread(FFmpegCaptureManager* manager, QObject* parent = nullptr);
    
    void setRunning(bool running);
    bool isRunning() const;

protected:
    void run() override;

signals:
    void frameAvailable();               // Trigger handler to decode/display a frame
    void deviceDisconnected();           // Prompt handler to deactivate device
    void readError(const QString& msg);  // Report detailed read errors

private:
    QPointer<QObject> m_handler;  // Can be either FFmpegBackendHandler or FFmpegCaptureManager
    ICaptureFrameReader* m_frameReader;
    mutable QMutex m_mutex;
    bool m_running;
};

#endif // CAPTURETHREAD_H
