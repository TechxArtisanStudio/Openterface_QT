/*
* ========================================================================== *
*                                                                            *
*    This file is part of the Openterface Mini KVM App QT version            *
*                                                                            *
*    Copyright (C) 2024   <info@openterface.com>                             *
*                                                                            *
*    This program is free software: you can redistribute it and/or modify    *
*    it under the terms of the GNU General Public License as published by    *
*    the Free Software Foundation version 3.                                 *
*                                                                            *
*    This program is distributed in the hope that it will be useful, but     *
*    WITHOUT ANY WARRANTY; without even the implied warranty of              *
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU        *
*    General Public License for more details.                                *
*                                                                            *
*    You should have received a copy of the GNU General Public License       *
*    along with this program. If not, see <http://www.gnu.org/licenses/>.    *
*                                                                            *
* ========================================================================== *
*/

#include "recording_example.h"
#include <QDebug>
#include <QDateTime>
#include <QDir>
#include <QStandardPaths>

RecordingExample::RecordingExample(QObject *parent)
    : QObject(parent)
    , m_ffmpegBackend(nullptr)
    , m_autoStopTimer(new QTimer(this))
    , m_isRecording(false)
{
    // Create FFmpeg backend instance
    m_ffmpegBackend = new FFmpegBackendHandler(this);
    
    // Setup recording configuration
    setupRecordingConfiguration();
    
    // Connect signals
    connectSignals();
    
    // Configure auto-stop timer
    m_autoStopTimer->setSingleShot(true);
    connect(m_autoStopTimer, &QTimer::timeout, this, &RecordingExample::autoStopRecording);
}

RecordingExample::~RecordingExample()
{
    if (m_isRecording) {
        stopRecordingSession();
    }
}

void RecordingExample::setupRecordingConfiguration()
{
    FFmpegBackendHandler::RecordingConfig config;
    
    // Set video codec and quality
    config.videoCodec = "libx264";      // H.264 codec
    config.videoBitrate = 2000000;      // 2 Mbps
    config.keyframeInterval = 30;       // Keyframe every 30 frames
    config.pixelFormat = "yuv420p";     // Standard pixel format
    
    // Set audio codec (if audio recording is needed)
    config.audioCodec = "aac";
    config.audioBitrate = 128000;       // 128 kbps
    config.audioSampleRate = 44100;     // 44.1 kHz
    
    // Set container format
    config.outputFormat = "mp4";        // MP4 container
    
    // Apply configuration to backend
    m_ffmpegBackend->setRecordingConfig(config);
    
}

void RecordingExample::connectSignals()
{
    // Connect recording events
    connect(m_ffmpegBackend, &FFmpegBackendHandler::recordingStarted,
            this, &RecordingExample::onRecordingStarted);
    
    connect(m_ffmpegBackend, &FFmpegBackendHandler::recordingStopped,
            this, &RecordingExample::onRecordingStopped);
    
    connect(m_ffmpegBackend, &FFmpegBackendHandler::recordingPaused,
            this, &RecordingExample::onRecordingPaused);
    
    connect(m_ffmpegBackend, &FFmpegBackendHandler::recordingResumed,
            this, &RecordingExample::onRecordingResumed);
    
    connect(m_ffmpegBackend, &FFmpegBackendHandler::recordingError,
            this, &RecordingExample::onRecordingError);
    
    connect(m_ffmpegBackend, &FFmpegBackendHandler::recordingDurationChanged,
            this, &RecordingExample::onRecordingDurationChanged);
}

void RecordingExample::startRecordingSession(const QString& outputPath, int duration)
{
    if (m_isRecording) {
        qWarning() << "Recording already in progress. Stop current recording first.";
        return;
    }
    
    // Generate output path if not provided
    QString finalOutputPath = outputPath;
    if (finalOutputPath.isEmpty()) {
        QString videosDir = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
        if (videosDir.isEmpty()) {
            videosDir = QDir::homePath();
        }
        
        QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd_hh-mm-ss");
        finalOutputPath = QDir(videosDir).filePath(QString("openterface_recording_%1.mp4").arg(timestamp));
    }
    
    // Ensure output directory exists
    QDir().mkpath(QFileInfo(finalOutputPath).dir().absolutePath());
    
    // Start recording
    if (m_ffmpegBackend->startRecording(finalOutputPath, "mp4", 2000000)) {
        m_currentOutputPath = finalOutputPath;
        m_isRecording = true;
        
        // Set auto-stop timer if duration is specified
        if (duration > 0) {
            m_autoStopTimer->start(duration * 1000); // Convert to milliseconds
        }
    } else {
        qWarning() << "Failed to start recording";
    }
}

void RecordingExample::stopRecordingSession()
{
    if (!m_isRecording) {
        qWarning() << "No recording in progress";
        return;
    }
    
    // Stop auto-stop timer
    m_autoStopTimer->stop();
    
    // Stop recording
    m_ffmpegBackend->stopRecording();
}

void RecordingExample::pauseRecording()
{
    if (!m_isRecording) {
        qWarning() << "No recording in progress";
        return;
    }
    
    m_ffmpegBackend->pauseRecording();
}

void RecordingExample::resumeRecording()
{
    if (!m_isRecording) {
        qWarning() << "No recording in progress";
        return;
    }
    
    m_ffmpegBackend->resumeRecording();
}

void RecordingExample::onRecordingStarted(const QString& outputPath)
{
}

void RecordingExample::onRecordingStopped()
{
    
    // Get final duration
    qint64 finalDuration = m_ffmpegBackend->getRecordingDuration();
    
    m_isRecording = false;
    m_currentOutputPath.clear();
    m_autoStopTimer->stop();
}

void RecordingExample::onRecordingPaused()
{
}

void RecordingExample::onRecordingResumed()
{
}

void RecordingExample::onRecordingError(const QString& error)
{
    qWarning() << "Recording error:" << error;
    m_isRecording = false;
    m_currentOutputPath.clear();
    m_autoStopTimer->stop();
}

void RecordingExample::onRecordingDurationChanged(qint64 duration)
{
    // Update every 5 seconds to avoid spam
    static qint64 lastReported = 0;
    if (duration - lastReported >= 5000) {
        lastReported = duration;
    }
}

void RecordingExample::autoStopRecording()
{
    stopRecordingSession();
}

// Example usage in main function or test
/*
#include <QCoreApplication>
#include "recording_example.h"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    
    RecordingExample example;
    
    // Start a 30-second recording
    example.startRecordingSession("/path/to/output/video.mp4", 30);
    
    // Or start unlimited recording
    // example.startRecordingSession("/path/to/output/video.mp4");
    
    return app.exec();
}
*/
