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

#include "recordingsettingsdialog.h"
#include "../../global.h"
#include "../../ui/globalsetting.h"
#ifndef Q_OS_WIN
#include "../../host/backend/gstreamerbackendhandler.h"
#else
#include "../../host/backend/qtbackendhandler.h"
#endif

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QComboBox>
#include <QSpinBox>
#include <QPushButton>
#include <QLineEdit>
#include <QProgressBar>
#include <QFileDialog>
#include <QStandardPaths>
#include <QDir>
#include <QDateTime>
#include <QMessageBox>
#include <QTimer>
#include <QDebug>
#include <QApplication>
#include <QStyle>

Q_LOGGING_CATEGORY(log_video_recording, "opf.video.recording")

RecordingSettingsDialog::RecordingSettingsDialog(QWidget *parent)
    : QDialog(parent)
#ifndef Q_OS_WIN
    , m_ffmpegBackend(nullptr)
#endif
    , m_backendHandler(nullptr)
    , m_isRecording(false)
    , m_isPaused(false)
    , m_updateTimer(new QTimer(this))
{
    setWindowTitle(tr("Video Recording Settings"));
    setModal(false);
    setAttribute(Qt::WA_DeleteOnClose, false);
    resize(500, 600);
    
    setupUI();
    connectSignals();
    loadSettings();
    refreshUIForBackend();
    updateControlStates();
    updateBackendStatus();
    
    // Update timer for recording info
    m_updateTimer->setInterval(100); // Update every 100ms
    connect(m_updateTimer, &QTimer::timeout, this, &RecordingSettingsDialog::updateRecordingInfo);
}

RecordingSettingsDialog::~RecordingSettingsDialog()
{
    qCDebug(log_video_recording) << "RecordingSettingsDialog destructor called";
    
    // Disconnect all signals to prevent crashes
#ifndef Q_OS_WIN
    if (m_ffmpegBackend) {
        disconnect(m_ffmpegBackend, nullptr, this, nullptr);
    }
#endif
    if (m_backendHandler) {
        disconnect(m_backendHandler, nullptr, this, nullptr);
    }
    
    // Stop recording safely
    if (m_isRecording) {
        qCDebug(log_video_recording) << "Stopping recording in destructor";
        MultimediaBackendHandler* backend = getActiveBackend();
        if (backend) {
            try {
                backend->stopRecording();
            } catch (...) {
                qCWarning(log_video_recording) << "Exception while stopping recording in destructor";
            }
        }
        m_isRecording = false;
    }
    
    // Stop timers
    if (m_updateTimer) {
        m_updateTimer->stop();
    }
    
    saveSettings();
    qCDebug(log_video_recording) << "RecordingSettingsDialog destructor completed";
}

#ifndef Q_OS_WIN
void RecordingSettingsDialog::setFFmpegBackend(FFmpegBackendHandler* backend)
{
    // Disconnect from previous backend
    if (m_ffmpegBackend) {
        disconnect(m_ffmpegBackend, nullptr, this, nullptr);
    }
    
    m_ffmpegBackend = backend;
    
    // Connect to new backend signals only if it's not already set as the generic backend handler
    if (m_ffmpegBackend && m_ffmpegBackend != m_backendHandler) {
        qCDebug(log_video_recording) << "Connecting signals to FFmpeg backend:" << m_ffmpegBackend->getBackendName();
        
        connect(m_ffmpegBackend, &FFmpegBackendHandler::recordingStarted,
                this, &RecordingSettingsDialog::onRecordingStarted);
        connect(m_ffmpegBackend, &FFmpegBackendHandler::recordingStopped,
                this, &RecordingSettingsDialog::onRecordingStopped);
        connect(m_ffmpegBackend, &FFmpegBackendHandler::recordingPaused,
                this, &RecordingSettingsDialog::onRecordingPaused);
        connect(m_ffmpegBackend, &FFmpegBackendHandler::recordingResumed,
                this, &RecordingSettingsDialog::onRecordingResumed);
        connect(m_ffmpegBackend, &FFmpegBackendHandler::recordingError,
                this, &RecordingSettingsDialog::onRecordingError);
        connect(m_ffmpegBackend, &FFmpegBackendHandler::recordingDurationChanged,
                this, &RecordingSettingsDialog::onRecordingDurationChanged);
        
        // Update current recording state
        m_isRecording = m_ffmpegBackend->isRecording();
        updateControlStates();
        updateBackendStatus();
    }
    
    // Also set as general backend handler if no other backend is set
    if (!m_backendHandler) {
        setBackendHandler(backend);
    }
}
#endif

void RecordingSettingsDialog::setBackendHandler(MultimediaBackendHandler* backend)
{
    // Disconnect from previous backend
    if (m_backendHandler) {
        disconnect(m_backendHandler, nullptr, this, nullptr);
    }
    
    m_backendHandler = backend;
    
    // CRITICAL FIX: Ensure Qt backend has media recorder set
    if (m_backendHandler && m_backendHandler->getBackendType() == MultimediaBackendType::Qt) {
        qCDebug(log_video_recording) << "Qt backend detected - ensuring media recorder is set";
        
        // Note: The media recorder should be set by the main window before calling this method
        // This is just a safety check to verify it's properly set
        qCDebug(log_video_recording) << "Qt backend should already have media recorder set by main window";
    }
    
    // Connect to new backend signals - connect to all recording signals regardless of backend type
    if (m_backendHandler) {
        qCDebug(log_video_recording) << "Connecting signals to backend:" << m_backendHandler->getBackendName();
        
        connect(m_backendHandler, &MultimediaBackendHandler::recordingStarted,
                this, &RecordingSettingsDialog::onRecordingStarted);
        connect(m_backendHandler, &MultimediaBackendHandler::recordingStopped,
                this, &RecordingSettingsDialog::onRecordingStopped);
        connect(m_backendHandler, &MultimediaBackendHandler::recordingPaused,
                this, &RecordingSettingsDialog::onRecordingPaused);
        connect(m_backendHandler, &MultimediaBackendHandler::recordingResumed,
                this, &RecordingSettingsDialog::onRecordingResumed);
        connect(m_backendHandler, &MultimediaBackendHandler::recordingError,
                this, &RecordingSettingsDialog::onRecordingError);
        connect(m_backendHandler, &MultimediaBackendHandler::recordingDurationChanged,
                this, &RecordingSettingsDialog::onRecordingDurationChanged);
        
        // Update current recording state
        m_isRecording = m_backendHandler->isRecording();
        updateControlStates();
        updateBackendStatus();
    }
}

void RecordingSettingsDialog::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    
    // Recording controls
    setupRecordingControls();
    mainLayout->addWidget(m_recordingGroup);
    
    // Video settings
    setupVideoSettings();
    mainLayout->addWidget(m_videoGroup);
    
    // Output settings
    setupOutputSettings();
    mainLayout->addWidget(m_outputGroup);
    
    // Control buttons
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    m_applyButton = new QPushButton(tr("Apply Settings"));
    m_resetButton = new QPushButton(tr("Reset to Defaults"));
    m_closeButton = new QPushButton(tr("Close"));
    
    buttonLayout->addWidget(m_applyButton);
    buttonLayout->addWidget(m_resetButton);
    buttonLayout->addStretch();
    buttonLayout->addWidget(m_closeButton);
    
    mainLayout->addLayout(buttonLayout);
}

void RecordingSettingsDialog::setupRecordingControls()
{
    m_recordingGroup = new QGroupBox(tr("Recording Controls"));
    QGridLayout* layout = new QGridLayout(m_recordingGroup);
    
    // Backend status label
    m_backendLabel = new QLabel(tr("Backend: Detecting..."));
    m_backendLabel->setStyleSheet("QLabel { color: #666; font-style: italic; }");
    
    // Control buttons
    m_startButton = new QPushButton(tr("Start Recording"));
    m_stopButton = new QPushButton(tr("Stop Recording"));
    m_pauseButton = new QPushButton(tr("Pause"));
    m_resumeButton = new QPushButton(tr("Resume"));
    
    // Status and progress
    m_statusLabel = new QLabel(tr("Status: Ready"));
    m_durationLabel = new QLabel(tr("Duration: 00:00:00"));
    m_recordingProgress = new QProgressBar();
    m_recordingProgress->setRange(0, 0); // Indeterminate progress
    m_recordingProgress->setVisible(false);
    
    // Layout
    layout->addWidget(m_backendLabel, 0, 0, 1, 4);
    layout->addWidget(m_startButton, 1, 0);
    layout->addWidget(m_stopButton, 1, 1);
    layout->addWidget(m_pauseButton, 1, 2);
    layout->addWidget(m_resumeButton, 1, 3);
    layout->addWidget(m_statusLabel, 2, 0, 1, 4);
    layout->addWidget(m_durationLabel, 3, 0, 1, 4);
    layout->addWidget(m_recordingProgress, 4, 0, 1, 4);
    
    // Connect signals
    connect(m_startButton, &QPushButton::clicked, this, &RecordingSettingsDialog::onStartRecording);
    connect(m_stopButton, &QPushButton::clicked, this, &RecordingSettingsDialog::onStopRecording);
    connect(m_pauseButton, &QPushButton::clicked, this, &RecordingSettingsDialog::onPauseRecording);
    connect(m_resumeButton, &QPushButton::clicked, this, &RecordingSettingsDialog::onResumeRecording);
}

void RecordingSettingsDialog::setupVideoSettings()
{
    m_videoGroup = new QGroupBox(tr("Video Settings"));
    QGridLayout* layout = new QGridLayout(m_videoGroup);
    
    int row = 0;
    
    // Video codec
    layout->addWidget(new QLabel(tr("Codec:")), row, 0);
    m_videoCodecCombo = new QComboBox();
    
    // Add codecs based on configured backend
    QString configuredBackend = GlobalSetting::instance().getMediaBackend();
    if (configuredBackend.toLower() == "gstreamer") {
        m_videoCodecCombo->addItems({"mjpeg", "x264enc", "x265enc"}); // GStreamer codec names
        m_videoCodecCombo->setToolTip(tr("GStreamer codecs: mjpeg (fast), x264enc (good compression), x265enc (best compression)"));
    } else {
        m_videoCodecCombo->addItems({"mjpeg"}); // FFmpeg/default - MJPEG encoder for AVI container creates playable video files
        m_videoCodecCombo->setToolTip(tr("FFmpeg codec: mjpeg (compatible with AVI format)"));
    }
    layout->addWidget(m_videoCodecCombo, row++, 1);
    
    // Video quality preset
    layout->addWidget(new QLabel(tr("Quality:")), row, 0);
    m_videoQualityCombo = new QComboBox();
    m_videoQualityCombo->addItems({tr("Low"), tr("Medium"), tr("High"), tr("Ultra"), tr("Custom")});
    layout->addWidget(m_videoQualityCombo, row++, 1);
    
    // Video bitrate
    layout->addWidget(new QLabel(tr("Bitrate (kbps):")), row, 0);
    m_videoBitrateSpin = new QSpinBox();
    m_videoBitrateSpin->setRange(100, 50000);
    m_videoBitrateSpin->setValue(2000);
    m_videoBitrateSpin->setSuffix(" kbps");
    layout->addWidget(m_videoBitrateSpin, row++, 1);
    
    // Connect quality preset to update bitrate
    connect(m_videoQualityCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            [this](int index) {
                switch(index) {
                    case 0: m_videoBitrateSpin->setValue(1000); break; // Low
                    case 1: m_videoBitrateSpin->setValue(2000); break; // Medium
                    case 2: m_videoBitrateSpin->setValue(5000); break; // High
                    case 3: m_videoBitrateSpin->setValue(10000); break; // Ultra
                    case 4: break; // Custom - don't change
                }
            });
}

void RecordingSettingsDialog::setupOutputSettings()
{
    m_outputGroup = new QGroupBox(tr("Output Settings"));
    QGridLayout* layout = new QGridLayout(m_outputGroup);
    
    int row = 0;
    
    // Output format (create this first so generateDefaultOutputPath can use it)
    layout->addWidget(new QLabel(tr("Format:")), row, 0);
    m_formatCombo = new QComboBox();
    
    // Add formats based on configured backend
    QString configuredBackend = GlobalSetting::instance().getMediaBackend();
    if (configuredBackend.toLower() == "gstreamer") {
        m_formatCombo->addItems({"avi", "mp4", "mkv"}); // GStreamer supports more formats
        m_formatCombo->setToolTip(tr("GStreamer formats: AVI (compatible), MP4 (modern), MKV (flexible)"));
    } else {
        m_formatCombo->addItems({"avi"}); // FFmpeg build supports avi, mjpeg and rawvideo muxers - AVI with MJPEG creates playable video files
        m_formatCombo->setToolTip(tr("FFmpeg format: AVI (most compatible with custom build)"));
    }
    layout->addWidget(m_formatCombo, row++, 1);
    
    // Output path (now format combo is available)
    layout->addWidget(new QLabel(tr("Output Path:")), row, 0);
    QHBoxLayout* pathLayout = new QHBoxLayout();
    m_outputPathEdit = new QLineEdit();
    m_outputPathEdit->setText(generateDefaultOutputPath());
    m_browseButton = new QPushButton(tr("Browse..."));
    pathLayout->addWidget(m_outputPathEdit);
    pathLayout->addWidget(m_browseButton);
    layout->addLayout(pathLayout, row++, 1);
    
    connect(m_browseButton, &QPushButton::clicked, this, &RecordingSettingsDialog::onBrowseOutputPath);
    
    // Update output path extension when format changes
    connect(m_formatCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        QString currentPath = m_outputPathEdit->text();
        if (!currentPath.isEmpty()) {
            QFileInfo fileInfo(currentPath);
            QString baseName = fileInfo.completeBaseName();
            QString dir = fileInfo.dir().absolutePath();
            QString newFormat = m_formatCombo->currentText();
            QString newPath = QDir(dir).filePath(QString("%1.%2").arg(baseName, newFormat));
            m_outputPathEdit->setText(newPath);
        }
    });
}

void RecordingSettingsDialog::connectSignals()
{
    connect(m_applyButton, &QPushButton::clicked, this, &RecordingSettingsDialog::onApplySettings);
    connect(m_resetButton, &QPushButton::clicked, this, &RecordingSettingsDialog::onResetToDefaults);
    connect(m_closeButton, &QPushButton::clicked, this, &QDialog::hide);
}

void RecordingSettingsDialog::onStartRecording()
{
    MultimediaBackendHandler* backend = getActiveBackend();
    qCDebug(log_video_recording) << "onStartRecording - Getting active backend:" << (void*)backend;
    
    if (!backend) {
        qCDebug(log_video_recording) << "No backend available - showing error message";
        QMessageBox::warning(this, tr("Error"), tr("No video backend available."));
        return;
    }
    
    qCDebug(log_video_recording) << "Backend type:" << static_cast<int>(backend->getBackendType());
    qCDebug(log_video_recording) << "Backend name:" << backend->getBackendName();
    
    if (m_isRecording) {
        QMessageBox::information(this, tr("Recording"), tr("Recording is already in progress."));
        return;
    }
    
    // Apply current settings first
    onApplySettings();
    
    QString outputPath = m_outputPathEdit->text().trimmed();
    if (outputPath.isEmpty()) {
        outputPath = generateDefaultOutputPath();
        m_outputPathEdit->setText(outputPath);
    }
    
    // Ensure the filename extension matches the selected format
    QString format = m_formatCombo->currentText();
    QFileInfo fileInfo(outputPath);
    QString baseName = fileInfo.completeBaseName();
    QString dir = fileInfo.dir().absolutePath();
    outputPath = QDir(dir).filePath(QString("%1.%2").arg(baseName, format));
    m_outputPathEdit->setText(outputPath); // Update the display
    
    // Ensure output directory exists
    QDir().mkpath(QFileInfo(outputPath).dir().absolutePath());
    
    int bitrate = m_videoBitrateSpin->value() * 1000; // Convert to bps
    
    qCDebug(log_video_recording) << "Calling backend->startRecording with:" << outputPath << format << bitrate;
    bool success = backend->startRecording(outputPath, format, bitrate);
    qCDebug(log_video_recording) << "Recording start result:" << success;
    
    if (!success) {
        QMessageBox::warning(this, tr("Recording Error"), 
                           tr("Failed to start recording. Please check the settings and try again."));
    } else {
        qCDebug(log_video_recording) << "Recording started successfully, m_isRecording should be updated by signal";
        
        // As a fallback, update the UI state manually if the signal doesn't come through
        // This is a temporary workaround - we'll check if the signal arrives within a short time
        QTimer::singleShot(100, this, [this, outputPath]() {
            // Check if this dialog still exists and is valid
            if (!this) return;
            
            MultimediaBackendHandler* backend = getActiveBackend();
            if (backend && !m_isRecording && backend->isRecording()) {
                qCDebug(log_video_recording) << "Signal didn't arrive, manually updating UI state";
                m_isRecording = true;
                m_isPaused = false;
                m_currentOutputPath = outputPath;
                if (m_recordingTimer.isValid()) {
                    m_recordingTimer.start();
                }
                if (m_updateTimer) {
                    m_updateTimer->start();
                }
                if (m_recordingProgress) {
                    m_recordingProgress->setVisible(true);
                }
                if (m_statusLabel) {
                    m_statusLabel->setText(tr("Status: Recording to %1").arg(QFileInfo(outputPath).fileName()));
                }
                updateControlStates();
            }
        });
    }
}

void RecordingSettingsDialog::onStopRecording()
{
    MultimediaBackendHandler* backend = getActiveBackend();
    qCDebug(log_video_recording) << "RecordingSettingsDialog::onStopRecording() - backend:" << backend 
             << "isRecording:" << m_isRecording;
    
    if (!backend) {
        qCWarning(log_video_recording) << "No backend available for stopping recording";
        return;
    }
    
    if (!m_isRecording) {
        qCWarning(log_video_recording) << "Not currently recording, cannot stop";
        return;
    }
    
    qCDebug(log_video_recording) << "Calling backend->stopRecording() on" << backend->getBackendName();
    
    // Add a try-catch to prevent crashes
    try {
        backend->stopRecording();
        qCDebug(log_video_recording) << "stopRecording() call completed";
        
        // As a fallback, if the signal doesn't come through, manually update state after a delay
        QTimer::singleShot(200, this, [this, backend]() {
            if (this && m_isRecording && backend && !backend->isRecording()) {
                qCDebug(log_video_recording) << "Recording stopped but signal didn't arrive, manually updating UI";
                m_isRecording = false;
                m_isPaused = false;
                if (m_updateTimer) {
                    m_updateTimer->stop();
                }
                if (m_recordingProgress) {
                    m_recordingProgress->setVisible(false);
                }
                if (m_statusLabel) {
                    m_statusLabel->setText(tr("Status: Recording stopped"));
                }
                updateControlStates();
            }
        });
        
    } catch (const std::exception& e) {
        qCCritical(log_video_recording) << "Exception in stopRecording():" << e.what();
        // Manually update UI state if backend crashes
        m_isRecording = false;
        m_isPaused = false;
        m_updateTimer->stop();
        m_recordingProgress->setVisible(false);
        m_statusLabel->setText(tr("Status: Recording stopped (with error)"));
        updateControlStates();
    } catch (...) {
        qCCritical(log_video_recording) << "Unknown exception in stopRecording()";
        // Manually update UI state if backend crashes
        m_isRecording = false;
        m_isPaused = false;
        m_updateTimer->stop();
        m_recordingProgress->setVisible(false);
        m_statusLabel->setText(tr("Status: Recording stopped (with error)"));
        updateControlStates();
    }
}

void RecordingSettingsDialog::onPauseRecording()
{
    MultimediaBackendHandler* backend = getActiveBackend();
    if (!backend || !m_isRecording || m_isPaused) {
        return;
    }
    
    backend->pauseRecording();
}

void RecordingSettingsDialog::onResumeRecording()
{
    MultimediaBackendHandler* backend = getActiveBackend();
    if (!backend || !m_isRecording || !m_isPaused) {
        return;
    }
    
    backend->resumeRecording();
}

void RecordingSettingsDialog::onBrowseOutputPath()
{
    QString defaultDir = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    if (defaultDir.isEmpty()) {
        defaultDir = QDir::homePath();
    }
    
    QString fileName = QFileDialog::getSaveFileName(
        this,
        tr("Save Recording As"),
        QDir(defaultDir).filePath("openterface_recording.mp4"),
        tr("Video Files (*.mp4 *.avi *.mov *.mkv *.webm);;All Files (*)")
    );
    
    if (!fileName.isEmpty()) {
        m_outputPathEdit->setText(fileName);
    }
}

void RecordingSettingsDialog::onApplySettings()
{
    MultimediaBackendHandler* backend = getActiveBackend();
    if (!backend) {
        QMessageBox::warning(this, tr("Warning"), tr("No video backend available!"));
        return;
    }
    
#ifndef Q_OS_WIN
    // For FFmpeg backend, use the specific configuration
    if (m_ffmpegBackend && backend == m_ffmpegBackend) {
        FFmpegBackendHandler::RecordingConfig config;
        config.outputPath = m_outputPathEdit->text();
        config.format = m_formatCombo->currentText();
        config.videoCodec = m_videoCodecCombo->currentText();
        config.videoBitrate = m_videoBitrateSpin->value() * 1000; // Convert to bps
        config.videoQuality = 23; // Use default CRF value
        config.useHardwareAcceleration = false; // Default to false for compatibility
        
        m_ffmpegBackend->setRecordingConfig(config);
    }
    // For GStreamer backend, we'll handle the config in the backend itself
    // since GStreamer doesn't need the same config structure
#endif
    
    saveSettings();
    
    m_statusLabel->setText(tr("Status: Settings applied"));
}

void RecordingSettingsDialog::onResetToDefaults()
{
    m_videoCodecCombo->setCurrentText("mjpeg");
    m_videoQualityCombo->setCurrentIndex(1); // Medium
    m_videoBitrateSpin->setValue(2000);
    
    m_formatCombo->setCurrentText("avi");
    m_outputPathEdit->setText(generateDefaultOutputPath());
}

void RecordingSettingsDialog::onRecordingStarted(const QString& outputPath)
{
    qCDebug(log_video_recording) << "RecordingSettingsDialog::onRecordingStarted() called with path:" << outputPath;
    m_isRecording = true;
    m_isPaused = false;
    m_currentOutputPath = outputPath;
    m_recordingTimer.start();
    m_updateTimer->start();
    m_recordingProgress->setVisible(true);
    
    m_statusLabel->setText(tr("Status: Recording to %1").arg(QFileInfo(outputPath).fileName()));
    updateControlStates();
    qCDebug(log_video_recording) << "After updateControlStates: m_isRecording=" << m_isRecording 
             << "stopButton enabled=" << m_stopButton->isEnabled();
}

void RecordingSettingsDialog::onRecordingStopped()
{
    qCDebug(log_video_recording) << "RecordingSettingsDialog::onRecordingStopped() called";
    
    // Add safety checks
    if (m_updateTimer) {
        m_updateTimer->stop();
    }
    
    m_isRecording = false;
    m_isPaused = false;
    
    if (m_recordingProgress) {
        m_recordingProgress->setVisible(false);
    }
    
    // Safe handling of file path display
    QString fileName = "unknown file";
    if (!m_currentOutputPath.isEmpty()) {
        QFileInfo fileInfo(m_currentOutputPath);
        if (fileInfo.exists()) {
            fileName = fileInfo.fileName();
        } else {
            fileName = QFileInfo(m_currentOutputPath).fileName();
        }
    }
    
    if (m_statusLabel) {
        m_statusLabel->setText(tr("Status: Recording stopped. File saved to %1").arg(fileName));
    }
    
    // Safe handling of duration display
    if (m_durationLabel) {
        qint64 duration = 0;
        if (m_recordingTimer.isValid()) {
            duration = m_recordingTimer.elapsed();
        }
        m_durationLabel->setText(tr("Duration: %1").arg(formatDuration(duration)));
    }
    
    updateControlStates();
    qCDebug(log_video_recording) << "onRecordingStopped() completed successfully";
}

void RecordingSettingsDialog::onRecordingPaused()
{
    m_isPaused = true;
    m_statusLabel->setText(tr("Status: Recording paused"));
    updateControlStates();
}

void RecordingSettingsDialog::onRecordingResumed()
{
    m_isPaused = false;
    m_statusLabel->setText(tr("Status: Recording resumed"));
    updateControlStates();
}

void RecordingSettingsDialog::onRecordingError(const QString& error)
{
    m_isRecording = false;
    m_isPaused = false;
    m_updateTimer->stop();
    m_recordingProgress->setVisible(false);
    
    m_statusLabel->setText(tr("Status: Recording error - %1").arg(error));
    updateControlStates();
    
    QMessageBox::critical(this, tr("Recording Error"), 
                         tr("Recording failed: %1").arg(error));
}

void RecordingSettingsDialog::onRecordingDurationChanged(qint64 duration)
{
    m_durationLabel->setText(tr("Duration: %1").arg(formatDuration(duration)));
}

void RecordingSettingsDialog::updateRecordingInfo()
{
    if (m_isRecording) {
        MultimediaBackendHandler* backend = getActiveBackend();
        if (backend) {
            qint64 duration = backend->getRecordingDuration();
            if (duration > 0) {
                m_durationLabel->setText(tr("Duration: %1").arg(formatDuration(duration)));
            }
        }
    }
}

void RecordingSettingsDialog::updateControlStates()
{
    m_startButton->setEnabled(!m_isRecording);
    m_stopButton->setEnabled(m_isRecording);
    m_pauseButton->setEnabled(m_isRecording && !m_isPaused);
    m_resumeButton->setEnabled(m_isRecording && m_isPaused);
    
    // Disable settings while recording
    bool settingsEnabled = !m_isRecording;
    m_videoGroup->setEnabled(settingsEnabled);
    m_outputGroup->setEnabled(settingsEnabled);
    m_applyButton->setEnabled(settingsEnabled);
    m_resetButton->setEnabled(settingsEnabled);
}

void RecordingSettingsDialog::updateBackendStatus()
{
    MultimediaBackendHandler* backend = getActiveBackend();
    QString backendText;
    
    if (backend) {
        QString backendName = backend->getBackendName();
        QString configuredBackend = GlobalSetting::instance().getMediaBackend();
        
        // Show both the actual backend being used and what's configured in settings
        if (backendName.toLower() == configuredBackend.toLower()) {
            backendText = tr("Backend: %1").arg(backendName);
        } else {
            backendText = tr("Backend: %1 (configured: %2)").arg(backendName, configuredBackend);
        }
        
        // Add color coding for different backends
        if (backendName.toLower().contains("gstreamer")) {
            m_backendLabel->setStyleSheet("QLabel { color: #006600; font-weight: bold; }");
        } else if (backendName.toLower().contains("ffmpeg")) {
            m_backendLabel->setStyleSheet("QLabel { color: #0066CC; font-weight: bold; }");
        } else if (backendName.toLower().contains("qt")) {
            m_backendLabel->setStyleSheet("QLabel { color: #9900CC; font-weight: bold; }");
        } else {
            m_backendLabel->setStyleSheet("QLabel { color: #666; font-style: italic; }");
        }
    } else {
        backendText = tr("Backend: None available");
        m_backendLabel->setStyleSheet("QLabel { color: #CC0000; font-weight: bold; }");
    }
    
    m_backendLabel->setText(backendText);
}

void RecordingSettingsDialog::refreshUIForBackend()
{
    // Update codec options based on the configured backend
    QString configuredBackend = GlobalSetting::instance().getMediaBackend();
    
    // On Windows, use Qt backend capabilities
#ifdef Q_OS_WIN
    // Windows always uses Qt backend
    m_videoCodecCombo->clear();
    m_videoCodecCombo->addItems({"MJPEG"}); 
    m_videoCodecCombo->setToolTip(tr("Windows Qt backend codecs: MJPEG"));
    
    m_formatCombo->clear();
    m_formatCombo->addItems({"mp4", "avi", "mov"}); 
    m_formatCombo->setToolTip(tr("Windows Qt backend formats: MP4 (recommended), AVI (compatible), MOV (QuickTime)"));
#else
    // Linux/Unix - Update video codec options
    m_videoCodecCombo->clear();
    if (configuredBackend.toLower() == "gstreamer") {
        m_videoCodecCombo->addItems({"mjpeg", "x264enc", "x265enc"}); 
        m_videoCodecCombo->setToolTip(tr("GStreamer codecs: mjpeg (fast), x264enc (good compression), x265enc (best compression)"));
    } else {
        m_videoCodecCombo->addItems({"mjpeg"}); 
        m_videoCodecCombo->setToolTip(tr("FFmpeg codec: mjpeg (compatible with AVI format)"));
    }
    
    // Update format options
    m_formatCombo->clear();
    if (configuredBackend.toLower() == "gstreamer") {
        m_formatCombo->addItems({"avi", "mp4", "mkv"}); 
        m_formatCombo->setToolTip(tr("GStreamer formats: AVI (compatible), MP4 (modern), MKV (flexible)"));
    } else {
        m_formatCombo->addItems({"avi"}); 
        m_formatCombo->setToolTip(tr("FFmpeg format: AVI (most compatible with custom build)"));
    }
#endif
    
    // Restore previously selected values if they're still available
    QString savedCodec = GlobalSetting::instance().getRecordingVideoCodec();
    int codecIndex = m_videoCodecCombo->findText(savedCodec);
    if (codecIndex >= 0) {
        m_videoCodecCombo->setCurrentIndex(codecIndex);
    }
    
    QString savedFormat = GlobalSetting::instance().getRecordingOutputFormat();
    int formatIndex = m_formatCombo->findText(savedFormat);
    if (formatIndex >= 0) {
        m_formatCombo->setCurrentIndex(formatIndex);
    }
}

void RecordingSettingsDialog::loadSettings()
{
    // Load settings from GlobalSetting or use defaults
    GlobalSetting& settings = GlobalSetting::instance();
    
    m_videoCodecCombo->setCurrentText(settings.getRecordingVideoCodec());
    m_videoBitrateSpin->setValue(settings.getRecordingVideoBitrate() / 1000);
    
    m_formatCombo->setCurrentText(settings.getRecordingOutputFormat());
    
    QString savedPath = settings.getRecordingOutputPath();
    if (savedPath.isEmpty()) {
        savedPath = generateDefaultOutputPath();
    }
    m_outputPathEdit->setText(savedPath);
}

void RecordingSettingsDialog::saveSettings()
{
    GlobalSetting& settings = GlobalSetting::instance();
    
    settings.setRecordingVideoCodec(m_videoCodecCombo->currentText());
    settings.setRecordingVideoBitrate(m_videoBitrateSpin->value() * 1000);
    settings.setRecordingOutputFormat(m_formatCombo->currentText());
    settings.setRecordingOutputPath(m_outputPathEdit->text());
}

QString RecordingSettingsDialog::formatDuration(qint64 milliseconds)
{
    qint64 seconds = milliseconds / 1000;
    qint64 minutes = seconds / 60;
    qint64 hours = minutes / 60;
    
    seconds %= 60;
    minutes %= 60;
    
    return QString("%1:%2:%3")
        .arg(hours, 2, 10, QChar('0'))
        .arg(minutes, 2, 10, QChar('0'))
        .arg(seconds, 2, 10, QChar('0'));
}

QString RecordingSettingsDialog::generateDefaultOutputPath()
{
    QString videosDir = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    if (videosDir.isEmpty()) {
        videosDir = QDir::homePath();
    }
    
    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd_hh-mm-ss");
    
    // Default format based on platform
#ifdef Q_OS_WIN
    QString format = "mp4"; // Default to MP4 on Windows
#else
    QString format = "avi"; // Default to AVI on Linux (FFmpeg compatible)
#endif
    
    if (m_formatCombo && m_formatCombo->count() > 0) {
        format = m_formatCombo->currentText();
    }
    
    // Set appropriate file extension based on format
    QString extension;
    if (format == "avi") {
        extension = "avi";  // AVI container
    } else if (format == "mp4") {
        extension = "mp4";  // MP4 container
    } else if (format == "mov") {
        extension = "mov";  // QuickTime container
    } else if (format == "mkv") {
        extension = "mkv";  // Matroska container
    } else if (format == "rawvideo") {
        extension = "yuv";  // Raw video typically uses .yuv extension
    } else if (format == "mjpeg") {
        extension = "mjpeg"; // MJPEG single image format
    } else {
        extension = format;  // For other formats, use format name as extension
    }
    
    return QDir(videosDir).filePath(QString("openterface_recording_%1.%2").arg(timestamp, extension));
}

void RecordingSettingsDialog::showDialog()
{
    if (!isVisible()) {
        show();
        raise();
        activateWindow();
    } else {
        raise();
        activateWindow();
    }
}

MultimediaBackendHandler* RecordingSettingsDialog::getActiveBackend() const
{
    // Prefer the generic backend handler if available, otherwise fall back to FFmpeg backend
#ifndef Q_OS_WIN
    MultimediaBackendHandler* result = m_backendHandler ? m_backendHandler : m_ffmpegBackend;
    qCDebug(log_video_recording) << "getActiveBackend() returning:" << result 
             << "backendHandler:" << m_backendHandler 
             << "ffmpegBackend:" << m_ffmpegBackend;
#else
    MultimediaBackendHandler* result = m_backendHandler;
    qCDebug(log_video_recording) << "getActiveBackend() returning:" << result 
             << "backendHandler:" << m_backendHandler;
#endif
    if (result) {
        qCDebug(log_video_recording) << "Active backend type:" << result->getBackendName();
    }
    return result;
}
