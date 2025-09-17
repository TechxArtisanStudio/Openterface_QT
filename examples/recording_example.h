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

#ifndef RECORDING_EXAMPLE_H
#define RECORDING_EXAMPLE_H

#include <QObject>
#include <QTimer>
#include <QString>
#include "../host/backend/ffmpegbackendhandler.h"

/**
 * @brief Example class demonstrating video recording with FFmpeg backend
 * 
 * This example shows how to:
 * - Start video recording
 * - Configure recording parameters
 * - Handle recording events
 * - Monitor recording duration
 * - Stop recording
 */
class RecordingExample : public QObject
{
    Q_OBJECT

public:
    explicit RecordingExample(QObject *parent = nullptr);
    ~RecordingExample();

public slots:
    /**
     * @brief Start a recording session
     * @param outputPath Path where the video file will be saved
     * @param duration Recording duration in seconds (0 = unlimited)
     */
    void startRecordingSession(const QString& outputPath, int duration = 0);
    
    /**
     * @brief Stop the current recording session
     */
    void stopRecordingSession();
    
    /**
     * @brief Pause the current recording
     */
    void pauseRecording();
    
    /**
     * @brief Resume the paused recording
     */
    void resumeRecording();

private slots:
    /**
     * @brief Handle recording started event
     */
    void onRecordingStarted(const QString& outputPath);
    
    /**
     * @brief Handle recording stopped event
     */
    void onRecordingStopped();
    
    /**
     * @brief Handle recording paused event
     */
    void onRecordingPaused();
    
    /**
     * @brief Handle recording resumed event
     */
    void onRecordingResumed();
    
    /**
     * @brief Handle recording error
     */
    void onRecordingError(const QString& error);
    
    /**
     * @brief Handle recording duration changes
     */
    void onRecordingDurationChanged(qint64 duration);
    
    /**
     * @brief Auto-stop recording after specified duration
     */
    void autoStopRecording();

private:
    void setupRecordingConfiguration();
    void connectSignals();

private:
    FFmpegBackendHandler* m_ffmpegBackend;
    QTimer* m_autoStopTimer;
    QString m_currentOutputPath;
    bool m_isRecording;
};

#endif // RECORDING_EXAMPLE_H
