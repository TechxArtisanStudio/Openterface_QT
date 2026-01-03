/*
* ========================================================================== *
*                                                                            *
*    Example: FFmpeg Video Recording Usage                                   *
*                                                                            *
*    This file demonstrates how to use the new video recording functionality *
*    added to the FFmpeg backend handler.                                    *
*                                                                            *
* ========================================================================== *
*/

#ifndef FFMPEG_RECORDING_EXAMPLE_H
#define FFMPEG_RECORDING_EXAMPLE_H

#include <QObject>
#include <QTimer>
#include <QDebug>
#include <QStandardPaths>
#include <QDateTime>
#include <QDir>
#include "host/backend/ffmpegbackendhandler.h"

/**
 * @brief Example implementation showing how to use FFmpeg recording feature
 */
class FFmpegRecordingExample : public QObject
{
    Q_OBJECT

public:
    explicit FFmpegRecordingExample(FFmpegBackendHandler* ffmpegHandler, QObject *parent = nullptr)
        : QObject(parent)
        , m_ffmpegHandler(ffmpegHandler)
        , m_statusTimer(new QTimer(this))
    {
        // Connect recording signals
        if (m_ffmpegHandler) {
            connect(m_ffmpegHandler, &FFmpegBackendHandler::recordingStarted,
                    this, &FFmpegRecordingExample::onRecordingStarted);
            connect(m_ffmpegHandler, &FFmpegBackendHandler::recordingStopped,
                    this, &FFmpegRecordingExample::onRecordingStopped);
            connect(m_ffmpegHandler, &FFmpegBackendHandler::recordingPaused,
                    this, &FFmpegRecordingExample::onRecordingPaused);
            connect(m_ffmpegHandler, &FFmpegBackendHandler::recordingResumed,
                    this, &FFmpegRecordingExample::onRecordingResumed);
            connect(m_ffmpegHandler, &FFmpegBackendHandler::recordingError,
                    this, &FFmpegRecordingExample::onRecordingError);
            connect(m_ffmpegHandler, &FFmpegBackendHandler::recordingDurationChanged,
                    this, &FFmpegRecordingExample::onRecordingDurationChanged);
        }
        
        // Setup status update timer
        m_statusTimer->setInterval(1000); // Update every second
        connect(m_statusTimer, &QTimer::timeout, this, &FFmpegRecordingExample::updateStatus);
    }

    // Example: Start recording with default settings
    bool startBasicRecording()
    {
        if (!m_ffmpegHandler) {
            qWarning() << "FFmpeg handler not available";
            return false;
        }
        
        // Generate output filename
        QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
        QString documentsPath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        QString outputPath = QDir(documentsPath).absoluteFilePath(QString("recording_%1.mp4").arg(timestamp));
        
        qDebug() << "Starting basic recording to:" << outputPath;
        
        // Start recording with default settings (MP4, 2 Mbps)
        bool success = m_ffmpegHandler->startRecording(outputPath);
        if (success) {
            m_statusTimer->start();
        }
        return success;
    }
    
    // Example: Start recording with custom settings
    bool startHighQualityRecording()
    {
        if (!m_ffmpegHandler) {
            qWarning() << "FFmpeg handler not available";
            return false;
        }
        
        // Configure high quality recording
        FFmpegBackendHandler::RecordingConfig config;
        config.format = "mp4";
        config.videoCodec = "libx264";
        config.videoBitrate = 5000000; // 5 Mbps for higher quality
        config.videoQuality = 18; // Lower CRF = better quality
        
        m_ffmpegHandler->setRecordingConfig(config);
        
        // Generate output filename
        QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
        QString documentsPath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        QString outputPath = QDir(documentsPath).absoluteFilePath(QString("hq_recording_%1.mp4").arg(timestamp));
        
        qDebug() << "Starting high quality recording to:" << outputPath;
        
        bool success = m_ffmpegHandler->startRecording(outputPath, config.format, config.videoBitrate);
        if (success) {
            m_statusTimer->start();
        }
        return success;
    }
    
    // Example: Stop recording
    void stopRecording()
    {
        if (m_ffmpegHandler) {
            m_ffmpegHandler->stopRecording();
            m_statusTimer->stop();
        }
    }
    
    // Example: Pause/Resume recording
    void pauseRecording()
    {
        if (m_ffmpegHandler) {
            m_ffmpegHandler->pauseRecording();
        }
    }
    
    void resumeRecording()
    {
        if (m_ffmpegHandler) {
            m_ffmpegHandler->resumeRecording();
        }
    }
    
    // Check recording status
    bool isRecording() const
    {
        return m_ffmpegHandler ? m_ffmpegHandler->isRecording() : false;
    }

public slots:
    void onRecordingStarted(const QString& outputPath)
    {
        qDebug() << "Recording started successfully:" << outputPath;
    }
    
    void onRecordingStopped()
    {
        qDebug() << "Recording stopped successfully";
        m_statusTimer->stop();
    }
    
    void onRecordingPaused()
    {
        qDebug() << "Recording paused";
    }
    
    void onRecordingResumed()
    {
        qDebug() << "Recording resumed";
    }
    
    void onRecordingError(const QString& error)
    {
        qWarning() << "Recording error:" << error;
        m_statusTimer->stop();
    }
    
    void onRecordingDurationChanged(qint64 duration)
    {
        // Convert to human readable format
        int totalSeconds = duration / 1000;
        int hours = totalSeconds / 3600;
        int minutes = (totalSeconds % 3600) / 60;
        int seconds = totalSeconds % 60;
        
        QString durationStr = QString("%1:%2:%3")
                             .arg(hours, 2, 10, QChar('0'))
                             .arg(minutes, 2, 10, QChar('0'))
                             .arg(seconds, 2, 10, QChar('0'));
        
        qDebug() << "Recording duration:" << durationStr;
    }
    
    void updateStatus()
    {
        if (m_ffmpegHandler && m_ffmpegHandler->isRecording()) {
            qint64 duration = m_ffmpegHandler->getRecordingDuration();
            QString currentFile = m_ffmpegHandler->getCurrentRecordingPath();
            qDebug() << "Recording status - File:" << currentFile << "Duration:" << duration << "ms";
        }
    }

private:
    FFmpegBackendHandler* m_ffmpegHandler;
    QTimer* m_statusTimer;
};

/* 
 * Usage Example:
 * 
 * // Assuming you have an FFmpegBackendHandler instance
 * FFmpegBackendHandler* ffmpegHandler = getFFmpegHandler();
 * 
 * // Create the example helper
 * FFmpegRecordingExample recorder(ffmpegHandler);
 * 
 * // Start basic recording
 * recorder.startBasicRecording();
 * 
 * // Or start high quality recording
 * recorder.startHighQualityRecording();
 * 
 * // Pause recording
 * recorder.pauseRecording();
 * 
 * // Resume recording
 * recorder.resumeRecording();
 * 
 * // Stop recording
 * recorder.stopRecording();
 */

#endif // FFMPEG_RECORDING_EXAMPLE_H
