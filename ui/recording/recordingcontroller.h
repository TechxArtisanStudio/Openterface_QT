/*
* ==========================================================================
*    This file is part of the Openterface Mini KVM App QT version            
*                                                                            
*    Copyright (C) 2024   info@openterface.com                             
*                                                                            
*    This program is free software: you can redistribute it and/or modify    
*    it under the terms of the GNU General Public License as published by    
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

#ifndef RECORDINGCONTROLLER_H
#define RECORDINGCONTROLLER_H

#include <QObject>
#include <QWidget>
#include <QPushButton>
#include <QLabel>
#include <QHBoxLayout>
#include <QTimer>
#include <QElapsedTimer>
#include <QLoggingCategory>

// Forward declarations
class CameraManager;
class FFmpegBackendHandler;

Q_DECLARE_LOGGING_CATEGORY(log_ui_recordingcontroller)

/**
 * @brief Controller for recording video with start/stop/pause functionality
 * 
 * Provides a unified interface for recording with either QMediaRecorder or FFmpegBackendHandler
 * and displays recording controls in the UI.
 */
class RecordingController : public QWidget
{
    Q_OBJECT

public:
    /**
     * @brief Constructor for the recording controller
     * @param parent Parent widget
     * @param cameraManager Pointer to the application's CameraManager
     */
    explicit RecordingController(QWidget *parent = nullptr, CameraManager *cameraManager = nullptr);
    
    /**
     * @brief Destructor
     */
    ~RecordingController();
    
    /**
     * @brief Creates and returns the recording control widget
     * @return QWidget* The recording control widget
     */
    QWidget* createControlsWidget();
    
    /**
     * @brief Get the current recording status
     * @return bool True if recording is active
     */
    bool isRecording() const;
    
    /**
     * @brief Get the current pause status
     * @return bool True if recording is paused
     */
    bool isPaused() const;
    
public slots:
    /**
     * @brief Start recording
     */
    void startRecording();
    
    /**
     * @brief Stop recording
     */
    void stopRecording();
    
    /**
     * @brief Pause recording
     */
    void pauseRecording();
    
    /**
     * @brief Resume recording
     */
    void resumeRecording();
    
    /**
     * @brief Show the recording settings dialog
     */
    void showRecordingSettings();
    
    /**
     * @brief Reset the recording system when experiencing issues
     * Shows diagnostic information and attempts to recover from errors
     */
    void resetRecordingSystem();
    
    /**
     * @brief Show detailed diagnostics about the recording system
     * Useful for troubleshooting recording issues
     */
    void showRecordingDiagnostics();
    
private slots:
    /**
     * @brief Update recording duration display
     */
    void updateRecordingTime();
    
    /**
     * @brief Handle recording started event from backend with output path
     * @param outputPath Path where recording is saved
     */
    void onRecordingStarted(const QString& outputPath);
    
    /**
     * @brief Handle recording started event from CameraManager (no path parameter)
     */
    void onCameraRecordingStarted();
    
    /**
     * @brief Handle recording stopped event from backend
     */
    void onRecordingStopped();
    
    /**
     * @brief Handle recording paused event from backend
     */
    void onRecordingPaused();
    
    /**
     * @brief Handle recording resumed event from backend
     */
    void onRecordingResumed();
    
    /**
     * @brief Handle recording error event from camera manager
     * @param errorString Error message from camera manager
     */
    void onRecordingError(const QString &errorString);
    
private:
    /**
     * @brief Set up the UI components
     */
    void setupUI();
    
    /**
     * @brief Connect signals and slots
     */
    void connectSignals();
    
    /**
     * @brief Update the UI states based on recording state
     */
    void updateUIStates();
    
    /**
     * @brief Format milliseconds to HH:MM:SS format
     * @param milliseconds Time in milliseconds
     * @return QString Formatted time string
     */
    QString formatDuration(qint64 milliseconds) const;
    
private:
    // Backend reference (not owned)
    CameraManager *m_cameraManager;
    FFmpegBackendHandler *m_ffmpegBackend;
    
    // Recording state
    bool m_isRecording;
    bool m_isPaused;
    QElapsedTimer m_recordingTimer;
    QTimer *m_updateTimer;
    qint64 m_pausedDuration;
    qint64 m_lastPauseTime;
    
    // UI components
    QPushButton *m_startButton;
    QPushButton *m_stopButton;
    QPushButton *m_pauseButton;
    QPushButton *m_resumeButton;
    QPushButton *m_settingsButton;
    QPushButton *m_resetButton;       // Button for resetting recording system
    QPushButton *m_diagnosticsButton; // Button for showing diagnostics
    QLabel *m_durationLabel;
    QHBoxLayout *m_layout;
    QWidget *m_controlsWidget;
};

#endif // RECORDINGCONTROLLER_H