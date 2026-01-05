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

#ifndef RECORDINGSETTINGSDIALOG_H
#define RECORDINGSETTINGSDIALOG_H

#include <QDialog>
#include <QLabel>
#include <QComboBox>
#include <QSpinBox>
#include <QPushButton>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QGridLayout>
#include <QFileDialog>
#include <QStandardPaths>
#include <QProgressBar>
#include <QTimer>
#include <QElapsedTimer>
#include "../../host/backend/ffmpegbackendhandler.h"

QT_BEGIN_NAMESPACE
class QComboBox;
class QSpinBox;
class QPushButton;
class QLineEdit;
class QProgressBar;
class QLabel;
QT_END_NAMESPACE

/**
 * @brief Dialog for configuring video recording settings and controlling recording
 */
class RecordingSettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit RecordingSettingsDialog(QWidget *parent = nullptr);
    ~RecordingSettingsDialog();

    void setFFmpegBackend(FFmpegBackendHandler* backend);

public slots:
    void showDialog();

private slots:
    void onStartRecording();
    void onStopRecording();
    void onPauseRecording();
    void onResumeRecording();
    void onBrowseOutputPath();
    void onApplySettings();
    void onResetToDefaults();
    
    // Recording event handlers
    void onRecordingStarted(const QString& outputPath);
    void onRecordingStopped();
    void onRecordingPaused();
    void onRecordingResumed();
    void onRecordingError(const QString& error);
    void onRecordingDurationChanged(qint64 duration);
    
    // Timer update
    void updateRecordingInfo();

private:
    void setupUI();
    void setupRecordingControls();
    void setupVideoSettings();
    void setupAudioSettings();
    void setupOutputSettings();
    void connectSignals();
    void updateControlStates();
    void loadSettings();
    void saveSettings();
    QString formatDuration(qint64 milliseconds);
    QString generateDefaultOutputPath();

private:
    // Backend
    FFmpegBackendHandler* m_backend;
    
    // Recording controls
    QGroupBox* m_recordingGroup;
    QPushButton* m_startButton;
    QPushButton* m_stopButton;
    QPushButton* m_pauseButton;
    QPushButton* m_resumeButton;
    QLabel* m_statusLabel;
    QLabel* m_durationLabel;
    QProgressBar* m_recordingProgress;
    
    // Video settings
    QGroupBox* m_videoGroup;
    QComboBox* m_videoCodecCombo;
    QComboBox* m_videoQualityCombo;
    QSpinBox* m_videoBitrateSpin;
    QComboBox* m_pixelFormatCombo;
    QSpinBox* m_keyframeIntervalSpin;
    
    // Audio settings
    QGroupBox* m_audioGroup;
    QComboBox* m_audioCodecCombo;
    QSpinBox* m_audioBitrateSpin;
    QComboBox* m_sampleRateCombo;
    
    // Output settings
    QGroupBox* m_outputGroup;
    QLineEdit* m_outputPathEdit;
    QPushButton* m_browseButton;
    QComboBox* m_formatCombo;
    
    // Control buttons
    QPushButton* m_applyButton;
    QPushButton* m_resetButton;
    QPushButton* m_closeButton;
    
    // Recording state
    bool m_isRecording;
    bool m_isPaused;
    QElapsedTimer m_recordingTimer;
    QTimer* m_updateTimer;
    QString m_currentOutputPath;
};

#endif // RECORDINGSETTINGSDIALOG_H
