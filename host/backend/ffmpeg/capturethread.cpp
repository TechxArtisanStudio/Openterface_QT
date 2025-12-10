/*
 * CaptureThread - implementation extracted from FFmpegBackendHandler
 */

#include "capturethread.h"
#include <QPointer>
#include "../ffmpegbackendhandler.h"
#include "ffmpeg_capture_manager.h"

#include <QDebug>
#include <QTimer>
#include <QElapsedTimer>
#include <QString>

Q_DECLARE_LOGGING_CATEGORY(log_ffmpeg_backend)

// Make FFmpegBackendHandler and FFmpegCaptureManager implement the interface
// by adding readFrame() method delegation

CaptureThread::CaptureThread(FFmpegBackendHandler* handler, QObject* parent)
    : QThread(parent), m_handler(handler), m_frameReader(handler), m_running(false)
{
}

CaptureThread::CaptureThread(FFmpegCaptureManager* manager, QObject* parent)
    : QThread(parent), m_handler(manager), m_frameReader(manager), m_running(false)
{
}

void CaptureThread::setRunning(bool running) {
    QMutexLocker locker(&m_mutex);
    m_running = running;
}

bool CaptureThread::isRunning() const {
    QMutexLocker locker(&m_mutex);
    return m_running;
}

void CaptureThread::run()
{
    qCDebug(log_ffmpeg_backend) << "FFmpeg capture thread started";
    
    QElapsedTimer performanceTimer;
    performanceTimer.start();
    
    int consecutiveFailures = 0;
    int framesProcessed = 0;
    const int maxConsecutiveFailures = 20; // Reduced from 100 - stop faster on device disconnect
    
    while (isRunning()) {
        // Check for interruption request
        if (isInterruptionRequested()) {
            qCDebug(log_ffmpeg_backend) << "Capture thread interrupted";
            break;
        }
        
        if (m_frameReader && m_frameReader->readFrame()) {
            // Reset failure counter on successful read
            consecutiveFailures = 0;
            
            // Process frame directly in capture thread to avoid packet invalidation
            // This ensures packet data remains valid during processing
            // Notify main handler that a frame is available for processing
            emit frameAvailable();
            framesProcessed++;
            
            // Log performance periodically (less frequently to reduce overhead)
            if (performanceTimer.elapsed() > 15000) { // Every 15 seconds (increased from 10)
                double actualFps = (framesProcessed * 1000.0) / performanceTimer.elapsed();
                qCDebug(log_ffmpeg_backend) << "Capture thread performance:" << actualFps << "FPS, processed" << framesProcessed << "frames";
                performanceTimer.restart();
                framesProcessed = 0;
            }
        } else {
            // Track consecutive failures
            consecutiveFailures++;
            
            // Be more aggressive about detecting device disconnections
            // Check device availability after fewer failures, especially for I/O errors
            if (consecutiveFailures >= 10 && consecutiveFailures % 10 == 0) { // Reduced from 50/25 - check every 10 failures
                qCDebug(log_ffmpeg_backend) << "Checking device availability due to consecutive failures:" << consecutiveFailures;
                // If the handler can still be queried for availability, skip here and let the handler decide;
                // at minimum emit deviceDisconnected so the handler can decide how to react.
                qCWarning(log_ffmpeg_backend) << "Device availability check failed - notifying handler";
                emit readError(QString("Consecutive frame read failures: %1").arg(consecutiveFailures));
                emit deviceDisconnected();
                break; // Exit the capture loop
            }
            
            if (consecutiveFailures >= maxConsecutiveFailures) {
                qCWarning(log_ffmpeg_backend) << "Too many consecutive frame read failures (" << consecutiveFailures << "), may indicate device issue";
                // Also trigger device disconnection handling asynchronously
                if (m_frameReader) {
                    qCWarning(log_ffmpeg_backend) << "Triggering device disconnection due to persistent failures";
                    // Notify the handler asynchronously: it will evaluate and deactivate device if necessary
                    emit readError(QString("Persistent frame read failures: %1").arg(consecutiveFailures));
                    emit deviceDisconnected();
                    break;
                }
                consecutiveFailures = 0; // Reset to avoid spam
            }
            
            // Adaptive sleep - longer sleep for repeated failures
            if (consecutiveFailures < 10) {
                msleep(1); // Short sleep for occasional failures
            } else if (consecutiveFailures < 50) {
                msleep(5); // Medium sleep for frequent failures
            } else {
                msleep(10); // Longer sleep for persistent failures
            }
        }
    }
    
    qCDebug(log_ffmpeg_backend) << "FFmpeg capture thread finished, processed" << framesProcessed << "frames total";
}
