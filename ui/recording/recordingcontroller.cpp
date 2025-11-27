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

#include "recordingcontroller.h"
#include "../../host/cameramanager.h"
#include "../mainwindow.h"
#include <QMessageBox>
#include <QApplication>
#include <QStyle>
#include <QDateTime>

#ifndef Q_OS_WIN
// Include FFmpeg backend for non-Windows platforms
#include "../../host/backend/ffmpegbackendhandler.h"
#endif

#include <QIcon>
#include <QStyle>
#include <QApplication>
#include <QDateTime>
#include <QDebug>

Q_LOGGING_CATEGORY(log_ui_recordingcontroller, "opf.ui.recordingcontroller")

RecordingController::RecordingController(QWidget *parent, CameraManager *cameraManager)
    : QWidget(parent)
    , m_cameraManager(cameraManager)
#ifndef Q_OS_WIN
    , m_ffmpegBackend(cameraManager ? cameraManager->getFFmpegBackend() : nullptr)
#else
    , m_ffmpegBackend(nullptr) // Windows doesn't use FFmpeg backend
#endif
    , m_isRecording(false)
    , m_isPaused(false)
    , m_pausedDuration(0)
    , m_lastPauseTime(0)
    , m_startButton(nullptr)
    , m_stopButton(nullptr)
    , m_pauseButton(nullptr)
    , m_resumeButton(nullptr)
    , m_settingsButton(nullptr)
    , m_durationLabel(nullptr)
    , m_layout(nullptr)
    , m_controlsWidget(nullptr)
     , m_floatingWidget(nullptr)
     , m_floatingDurationLabel(nullptr)
{
    qCDebug(log_ui_recordingcontroller) << "Creating RecordingController";
    
    setupUI();
    connectSignals();
    
    // Set up timer for recording duration updates
    m_updateTimer = new QTimer(this);
    m_updateTimer->setInterval(100); // Update every 100ms
    connect(m_updateTimer, &QTimer::timeout, this, &RecordingController::updateRecordingTime);
}

RecordingController::~RecordingController()
{
    qCDebug(log_ui_recordingcontroller) << "Destroying RecordingController";
    
    // Stop recording if still active
    if (m_isRecording) {
        stopRecording();
    }
    
    // Ensure floating widget is cleaned up if it was created
    if (m_floatingWidget) {
        // If it's parented elsewhere, delete it explicitly
        m_floatingWidget->deleteLater();
        m_floatingWidget = nullptr;
    }
}

QWidget* RecordingController::createControlsWidget()
{
    if (!m_controlsWidget) {
        // Create the controls widget if it doesn't exist
        m_controlsWidget = new QWidget(this);
        m_layout = new QHBoxLayout(m_controlsWidget);
        m_layout->setContentsMargins(4, 0, 4, 0);
        m_layout->setSpacing(4);
        
        // Add controls to layout
        m_layout->addWidget(m_startButton);
        m_layout->addWidget(m_stopButton);
        m_layout->addWidget(m_pauseButton);
        m_layout->addWidget(m_resumeButton);
        m_layout->addWidget(m_durationLabel);
        m_layout->addStretch();
        m_layout->addWidget(m_resetButton);
        m_layout->addWidget(m_diagnosticsButton);
        m_layout->addWidget(m_settingsButton);
        
        // Update button states
        updateUIStates();
    }
    
    return m_controlsWidget;
}

    QWidget* RecordingController::createFloatingDurationWidget(QWidget* parent)
    {
        if (!m_floatingWidget) {
            QWidget* wParent = parent ? parent : nullptr;
            m_floatingWidget = new QWidget(wParent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
            m_floatingWidget->setAttribute(Qt::WA_ShowWithoutActivating);
            m_floatingWidget->setWindowTitle(tr("Recording"));
            QHBoxLayout* layout = new QHBoxLayout(m_floatingWidget);
            layout->setContentsMargins(6, 4, 6, 4);
            layout->setSpacing(4);
            // Create a label dedicated to the floating widget to avoid reparenting
            m_floatingDurationLabel = new QLabel("00:00:00", m_floatingWidget);
            m_floatingDurationLabel->setAlignment(Qt::AlignCenter);
            layout->addWidget(m_floatingDurationLabel);
            m_floatingWidget->setLayout(layout);
            m_floatingWidget->hide();
        }
        return m_floatingWidget;
    }

bool RecordingController::isRecording() const
{
    return m_isRecording;
}

bool RecordingController::isPaused() const
{
    return m_isPaused;
}

void RecordingController::startRecording()
{
    qCDebug(log_ui_recordingcontroller) << "Start recording requested";
    
    if (m_isRecording) {
        qCDebug(log_ui_recordingcontroller) << "Recording already in progress";
        return;
    }
    
    if (m_cameraManager) {
        // First check camera active status
        if (!m_cameraManager->hasActiveCameraDevice()) {
            qCWarning(log_ui_recordingcontroller) << "No active camera device for recording";
            QMessageBox::warning(this, tr("Recording Error"),
                tr("No active camera device for recording. Please ensure a camera is connected."));
            return;
        }
        
        m_cameraManager->startRecording();
        
        // Start local timer
        m_recordingTimer.start();
        m_pausedDuration = 0;
        m_updateTimer->start();
        
        // Update state
        m_isRecording = true;
        m_isPaused = false;
        
        // Update UI
        updateUIStates();
        
        qCDebug(log_ui_recordingcontroller) << "Recording started";
    } else {
        qCWarning(log_ui_recordingcontroller) << "Cannot start recording - no camera manager";
        QMessageBox::warning(this, tr("Recording Error"),
            tr("Cannot start recording - camera system not initialized."));
    }
}

void RecordingController::stopRecording()
{
    qCDebug(log_ui_recordingcontroller) << "Stop recording requested";
    
    if (!m_isRecording) {
        qCDebug(log_ui_recordingcontroller) << "No recording in progress";
        return;
    }
    
    if (m_cameraManager) {
        m_cameraManager->stopRecording();
        
        // Stop timer
        m_updateTimer->stop();
        
        // Update state
        m_isRecording = false;
        m_isPaused = false;
        
        // Update UI
        m_durationLabel->setText("00:00:00");
        updateUIStates();
        
        qCDebug(log_ui_recordingcontroller) << "Recording stopped";
    } else {
        qCWarning(log_ui_recordingcontroller) << "Cannot stop recording - no camera manager";
    }
}

void RecordingController::pauseRecording()
{
    qCDebug(log_ui_recordingcontroller) << "Pause recording requested";
    
    if (!m_isRecording || m_isPaused) {
        qCDebug(log_ui_recordingcontroller) << "Cannot pause: not recording or already paused";
        return;
    }
    
    if (m_cameraManager) {
        m_cameraManager->pauseRecording();
        
        // Record when we paused
        m_lastPauseTime = m_recordingTimer.elapsed();
        
        // Update state
        m_isPaused = true;
        
        // Update UI
        updateUIStates();
        
        qCDebug(log_ui_recordingcontroller) << "Recording paused";
    } else {
        qCWarning(log_ui_recordingcontroller) << "Cannot pause recording - no camera manager";
    }
}

void RecordingController::resumeRecording()
{
    qCDebug(log_ui_recordingcontroller) << "Resume recording requested";
    
    if (!m_isRecording || !m_isPaused) {
        qCDebug(log_ui_recordingcontroller) << "Cannot resume: not recording or not paused";
        return;
    }
    
    if (m_cameraManager) {
        m_cameraManager->resumeRecording();
        
        // Account for pause duration
        m_pausedDuration += (m_recordingTimer.elapsed() - m_lastPauseTime);
        
        // Update state
        m_isPaused = false;
        
        // Update UI
        updateUIStates();
        
        qCDebug(log_ui_recordingcontroller) << "Recording resumed";
    } else {
        qCWarning(log_ui_recordingcontroller) << "Cannot resume recording - no camera manager";
    }
}

void RecordingController::showRecordingSettings()
{
    qCDebug(log_ui_recordingcontroller) << "Show recording settings requested";
    
    // Get parent MainWindow
    if (MainWindow* mainWindow = qobject_cast<MainWindow*>(parent())) {
        mainWindow->showRecordingSettings();
    } else {
        qCWarning(log_ui_recordingcontroller) << "Cannot show settings - parent is not MainWindow";
    }
}

void RecordingController::updateRecordingTime()
{
    if (!m_isRecording) {
        return;
    }
    
    qint64 elapsedTime;
    
    if (m_isPaused) {
        // When paused, show the time at which we paused
        elapsedTime = m_lastPauseTime - m_pausedDuration;
    } else {
        // When recording normally, show current time minus paused time
        elapsedTime = m_recordingTimer.elapsed() - m_pausedDuration;
    }
    
    // Update duration label
    m_durationLabel->setText(formatDuration(elapsedTime));
    if (m_floatingDurationLabel) {
        m_floatingDurationLabel->setText(formatDuration(elapsedTime));
    }
}

void RecordingController::onRecordingStarted(const QString& outputPath)
{
    qCDebug(log_ui_recordingcontroller) << "Recording started signal received:" << outputPath;
    
    // Start local timer
    m_recordingTimer.start();
    m_pausedDuration = 0;
    m_updateTimer->start();
    
    // Update state
    m_isRecording = true;
    m_isPaused = false;
    
    // Update UI
    updateUIStates();
    // Show floating widget if present
    if (m_floatingWidget) {
        m_floatingWidget->show();
    }
}

void RecordingController::onRecordingStopped()
{
    qCDebug(log_ui_recordingcontroller) << "Recording stopped signal received";
    
    // Stop timer
    m_updateTimer->stop();
    
    // Update state
    m_isRecording = false;
    m_isPaused = false;
    
    // Update UI
    m_durationLabel->setText("00:00:00");
    updateUIStates();
    if (m_floatingWidget) {
        m_floatingWidget->hide();
    }
}

void RecordingController::onRecordingPaused()
{
    qCDebug(log_ui_recordingcontroller) << "Recording paused signal received";
    
    // Record when we paused
    m_lastPauseTime = m_recordingTimer.elapsed();
    
    // Update state
    m_isPaused = true;
    
    // Update UI
    updateUIStates();
}

void RecordingController::onRecordingResumed()
{
    qCDebug(log_ui_recordingcontroller) << "Recording resumed signal received";
    
    // Account for pause duration
    m_pausedDuration += (m_recordingTimer.elapsed() - m_lastPauseTime);
    
    // Update state
    m_isPaused = false;
    
    // Update UI
    updateUIStates();
}

void RecordingController::setupUI()
{
    // Create buttons with icons from standard theme
    m_startButton = new QPushButton(this);
    m_startButton->setIcon(QIcon::fromTheme("media-record", QApplication::style()->standardIcon(QStyle::SP_MediaPlay)));
    m_startButton->setToolTip(tr("Start Recording"));
    m_startButton->setMaximumWidth(32);
    
    m_stopButton = new QPushButton(this);
    m_stopButton->setIcon(QIcon::fromTheme("media-playback-stop", QApplication::style()->standardIcon(QStyle::SP_MediaStop)));
    m_stopButton->setToolTip(tr("Stop Recording"));
    m_stopButton->setMaximumWidth(32);
    
    m_pauseButton = new QPushButton(this);
    m_pauseButton->setIcon(QIcon::fromTheme("media-playback-pause", QApplication::style()->standardIcon(QStyle::SP_MediaPause)));
    m_pauseButton->setToolTip(tr("Pause Recording"));
    m_pauseButton->setMaximumWidth(32);
    
    m_resumeButton = new QPushButton(this);
    m_resumeButton->setIcon(QIcon::fromTheme("media-playback-start", QApplication::style()->standardIcon(QStyle::SP_MediaPlay)));
    m_resumeButton->setToolTip(tr("Resume Recording"));
    m_resumeButton->setMaximumWidth(32);
    
    m_settingsButton = new QPushButton(this);
    m_settingsButton->setIcon(QIcon::fromTheme("preferences-system", QApplication::style()->standardIcon(QStyle::SP_FileDialogDetailedView)));
    m_settingsButton->setToolTip(tr("Recording Settings"));
    m_settingsButton->setMaximumWidth(32);
    
    // Add reset button for recording system recovery
    m_resetButton = new QPushButton(this);
    m_resetButton->setIcon(QIcon::fromTheme("view-refresh", QApplication::style()->standardIcon(QStyle::SP_BrowserReload)));
    m_resetButton->setToolTip(tr("Reset Recording System"));
    m_resetButton->setMaximumWidth(32);
    
    // Add diagnostics button
    m_diagnosticsButton = new QPushButton(this);
    m_diagnosticsButton->setIcon(QIcon::fromTheme("dialog-information", QApplication::style()->standardIcon(QStyle::SP_MessageBoxInformation)));
    m_diagnosticsButton->setToolTip(tr("Recording Diagnostics"));
    m_diagnosticsButton->setMaximumWidth(32);
    
    m_durationLabel = new QLabel("00:00:00", this);
    m_durationLabel->setMinimumWidth(60);
    m_durationLabel->setAlignment(Qt::AlignCenter);
    
    // Set initial states
    updateUIStates();
}

void RecordingController::connectSignals()
{
    // Connect UI buttons
    connect(m_startButton, &QPushButton::clicked, this, &RecordingController::startRecording);
    connect(m_stopButton, &QPushButton::clicked, this, &RecordingController::stopRecording);
    connect(m_pauseButton, &QPushButton::clicked, this, &RecordingController::pauseRecording);
    connect(m_resumeButton, &QPushButton::clicked, this, &RecordingController::resumeRecording);
    connect(m_settingsButton, &QPushButton::clicked, this, &RecordingController::showRecordingSettings);
    connect(m_resetButton, &QPushButton::clicked, this, &RecordingController::resetRecordingSystem);
    connect(m_diagnosticsButton, &QPushButton::clicked, this, &RecordingController::showRecordingDiagnostics);
    
    // Connect to CameraManager signals for recording state and errors
    if (m_cameraManager) {
        connect(m_cameraManager, &CameraManager::recordingStarted, this, &RecordingController::onCameraRecordingStarted);
        connect(m_cameraManager, &CameraManager::recordingStopped, this, &RecordingController::onRecordingStopped);
        connect(m_cameraManager, &CameraManager::recordingError, this, &RecordingController::onRecordingError);
        qCDebug(log_ui_recordingcontroller) << "Connected to CameraManager signals";
    }
    
#ifndef Q_OS_WIN
    // Connect to FFmpeg backend signals (non-Windows platforms only)
    if (m_ffmpegBackend) {
        connect(m_ffmpegBackend, &FFmpegBackendHandler::recordingStarted, this, &RecordingController::onRecordingStarted);
        connect(m_ffmpegBackend, &FFmpegBackendHandler::recordingStopped, this, &RecordingController::onRecordingStopped);
        connect(m_ffmpegBackend, &FFmpegBackendHandler::recordingPaused, this, &RecordingController::onRecordingPaused);
        connect(m_ffmpegBackend, &FFmpegBackendHandler::recordingResumed, this, &RecordingController::onRecordingResumed);
    } else {
        qCWarning(log_ui_recordingcontroller) << "No FFmpeg backend available, some signals won't be connected";
    }
#else
    // Windows platform uses QMediaRecorder, not FFmpeg
    qCDebug(log_ui_recordingcontroller) << "Using Qt backend for recording on Windows platform";
#endif
}

void RecordingController::updateUIStates()
{
#ifdef Q_OS_WIN
    // Windows: Simpler UI without pause/resume (not supported by QMediaRecorder)
    if (m_isRecording) {
        m_startButton->setVisible(false);
        m_stopButton->setVisible(true);
        // Hide pause/resume buttons on Windows
        m_pauseButton->setVisible(false);
        m_resumeButton->setVisible(false);
    } else {
        m_startButton->setVisible(true);
        m_stopButton->setVisible(false);
        m_pauseButton->setVisible(false);
        m_resumeButton->setVisible(false);
    }
#else
    // Linux/macOS: Full UI with pause/resume support
    if (m_isRecording) {
        if (m_isPaused) {
            m_startButton->setVisible(false);
            m_stopButton->setVisible(true);
            m_pauseButton->setVisible(false);
            m_resumeButton->setVisible(true);
        } else {
            m_startButton->setVisible(false);
            m_stopButton->setVisible(true);
            m_pauseButton->setVisible(true);
            m_resumeButton->setVisible(false);
        }
    } else {
        m_startButton->setVisible(true);
        m_stopButton->setVisible(false);
        m_pauseButton->setVisible(false);
        m_resumeButton->setVisible(false);
    }
#endif
    
    // Settings button is always visible
    m_settingsButton->setVisible(true);
    
    // Recovery buttons should be visible when not recording
    bool showRecoveryButtons = !m_isRecording;
    m_resetButton->setVisible(showRecoveryButtons);
    m_diagnosticsButton->setVisible(showRecoveryButtons);
}

QString RecordingController::formatDuration(qint64 milliseconds) const
{
    int seconds = (milliseconds / 1000) % 60;
    int minutes = (milliseconds / (1000 * 60)) % 60;
    int hours = (milliseconds / (1000 * 60 * 60));
    
    return QString("%1:%2:%3")
        .arg(hours, 2, 10, QChar('0'))
        .arg(minutes, 2, 10, QChar('0'))
        .arg(seconds, 2, 10, QChar('0'));
}

// This slot handles the parameterless recordingStarted signal from CameraManager
void RecordingController::onCameraRecordingStarted()
{
    qCDebug(log_ui_recordingcontroller) << "Recording started signal received from CameraManager";
    
    // Start local timer
    m_recordingTimer.start();
    m_pausedDuration = 0;
    m_updateTimer->start();
    
    // Update state
    m_isRecording = true;
    m_isPaused = false;
    
    // Update UI
    updateUIStates();
    if (m_floatingWidget) {
        m_floatingWidget->show();
    }
}

void RecordingController::onRecordingError(const QString &errorString)
{
    qCWarning(log_ui_recordingcontroller) << "Recording error received:" << errorString;
    
    // If we were recording, stop the recording UI
    if (m_isRecording) {
        m_updateTimer->stop();
        m_isRecording = false;
        m_isPaused = false;
        updateUIStates();
        m_durationLabel->setText("00:00:00");
        qCDebug(log_ui_recordingcontroller) << "Stopping recording due to error";
    }
    
    // Try to provide more user-friendly error messages based on common issues
    QString userMessage;
    if (errorString.contains("Failed to start", Qt::CaseInsensitive)) {
        userMessage = tr("Failed to start recording.\n\nPossible causes:"
                       "\n- Insufficient disk space"
                       "\n- Permission issues with output folder"
                       "\n- Camera device is busy or disconnected"
                       "\n- Codec not supported on this system"
                       "\n\nTechnical details: %1").arg(errorString);
    } else if (errorString.contains("Failed to save", Qt::CaseInsensitive)) {
        userMessage = tr("Failed to save recording.\n\nPossible causes:"
                       "\n- Insufficient disk space"
                       "\n- Permission issues with output folder"
                       "\n- Drive disconnected during recording"
                       "\n\nTechnical details: %1").arg(errorString);
    } else if (errorString.contains("corrupted", Qt::CaseInsensitive)) {
        userMessage = tr("The recording file may be corrupted.\n\nPossible causes:"
                       "\n- Recording stopped unexpectedly"
                       "\n- System resource issues"
                       "\n- Hardware acceleration problems"
                       "\n\nTechnical details: %1").arg(errorString);
    } else {
        userMessage = tr("An error occurred with the recording:\n%1").arg(errorString);
    }
    
    // Show error to user
    QMessageBox msgBox(QMessageBox::Warning, tr("Recording Error"), userMessage, QMessageBox::Ok, this);
    msgBox.setIcon(QMessageBox::Warning);
    
    // Add retry button if we have a camera manager
    QPushButton *retryButton = nullptr;
    if (m_cameraManager) {
        retryButton = msgBox.addButton(tr("Retry"), QMessageBox::ActionRole);
    }
    
    msgBox.exec();
    
    // Handle retry if clicked
    if (retryButton && msgBox.clickedButton() == retryButton) {
        QTimer::singleShot(500, this, &RecordingController::startRecording);
    }
}

void RecordingController::resetRecordingSystem()
{
    qCInfo(log_ui_recordingcontroller) << "Manual recording system reset requested";
    
    if (!m_cameraManager) {
        QMessageBox::warning(this, tr("Reset Failed"), 
            tr("Cannot reset recording system - camera manager is not available."));
        return;
    }
    
    // Check if recording is in progress
    if (m_isRecording) {
        QMessageBox::StandardButton response = QMessageBox::question(
            this,
            tr("Recording in Progress"),
            tr("A recording is currently in progress. Stop it and reset the recording system?"),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No
        );
        
        if (response != QMessageBox::Yes) {
            return;
        }
        
        // Stop current recording
        stopRecording();
    }
    
    // Show a progress dialog
    // Note: recoverRecordingSystem removed with QCamera migration to FFmpeg
    // FFmpeg backend handles recovery automatically
    
    QMessageBox::information(this, tr("System Reset"),
        tr("FFmpeg backend automatically handles recovery. Please try recording again."));
}

void RecordingController::showRecordingDiagnostics()
{
    qCInfo(log_ui_recordingcontroller) << "Recording diagnostics requested";
    
    // Note: getRecordingDiagnosticsReport removed with QCamera migration to FFmpeg
    // Basic diagnostics for FFmpeg backend
    
    QString diagnostics;
    if (!m_cameraManager) {
        diagnostics = "Camera manager not available";
    } else {
        diagnostics = QString("Recording System Diagnostics\n\n");
        diagnostics += QString("Backend: FFmpeg\n");
        diagnostics += QString("Current Device: %1\n").arg(m_cameraManager->getCurrentCameraDeviceDescription());
        diagnostics += QString("Is Recording: %1\n").arg(m_cameraManager->isRecording() ? "Yes" : "No");
        diagnostics += QString("Is Paused: %1\n").arg(m_cameraManager->isPaused() ? "Yes" : "No");
        diagnostics += QString("\nFFmpeg backend handles device access automatically.");
    }
    
    // Create a dialog with a text browser
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Recording System Diagnostics"));
    dialog.resize(800, 600);
    
    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    QTextBrowser *textBrowser = new QTextBrowser(&dialog);
    textBrowser->setPlainText(diagnostics);
    textBrowser->setReadOnly(true);
    layout->addWidget(textBrowser);
    
    // Add buttons
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    QPushButton *copyButton = new QPushButton(tr("Copy to Clipboard"), &dialog);
    QPushButton *closeButton = new QPushButton(tr("Close"), &dialog);
    buttonLayout->addStretch();
    buttonLayout->addWidget(copyButton);
    buttonLayout->addWidget(closeButton);
    layout->addLayout(buttonLayout);
    
    // Connect signals
    connect(copyButton, &QPushButton::clicked, [&diagnostics]() {
        QApplication::clipboard()->setText(diagnostics);
    });
    connect(closeButton, &QPushButton::clicked, &dialog, &QDialog::accept);
    
    // Show dialog
    dialog.exec();
}
