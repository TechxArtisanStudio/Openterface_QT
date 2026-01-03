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
#include "../statusbar/statusbarmanager.h"
#include <QMessageBox>
#include <QApplication>
#include <QDateTime>
#include <QDebug>
#include <QTextBrowser>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QDialog>
#include <QClipboard>

#ifndef Q_OS_WIN
// Include FFmpeg backend for non-Windows platforms
#include "../../host/backend/ffmpegbackendhandler.h"
#endif

Q_LOGGING_CATEGORY(log_ui_recordingcontroller, "opf.ui.recordingcontroller")

RecordingController::RecordingController(CameraManager *cameraManager, StatusBarManager *statusBarManager, QObject *parent)
    : QObject(parent)
    , m_cameraManager(cameraManager)
#ifndef Q_OS_WIN
    , m_ffmpegBackend(cameraManager ? cameraManager->getFFmpegBackend() : nullptr)
#else
    , m_ffmpegBackend(nullptr) // Windows doesn't use FFmpeg backend
#endif
    , m_statusBarManager(statusBarManager)
    , m_isRecording(false)
    , m_isPaused(false)
    , m_pausedDuration(0)
    , m_lastPauseTime(0)
{
    qCDebug(log_ui_recordingcontroller) << "Creating RecordingController";
    
    // Set up timer for recording duration updates
    m_updateTimer = new QTimer(this);
    m_updateTimer->setInterval(100); // Update every 100ms
    connect(m_updateTimer, &QTimer::timeout, this, &RecordingController::updateRecordingTime);
    
    connectSignals();
}

RecordingController::~RecordingController()
{
    qCDebug(log_ui_recordingcontroller) << "Destroying RecordingController";
    
    // Stop recording if still active
    if (m_isRecording) {
        stopRecording();
    }
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
            QMessageBox::warning(nullptr, tr("Recording Error"),
                tr("No active camera device for recording. Please ensure a camera is connected."));
            return;
        }
        
        qCDebug(log_ui_recordingcontroller) << "Calling camera manager to start recording";
        m_cameraManager->startRecording();
        
        // Note: Don't update state here - wait for onCameraRecordingStarted() signal
        // to ensure the recording backend has properly initialized
    } else {
        qCWarning(log_ui_recordingcontroller) << "Cannot start recording - no camera manager";
        QMessageBox::warning(nullptr, tr("Recording Error"),
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
        qCDebug(log_ui_recordingcontroller) << "Calling camera manager to stop recording";
        m_cameraManager->stopRecording();
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
    
    // Get MainWindow from parent
    MainWindow* mainWindow = qobject_cast<MainWindow*>(parent());
    if (mainWindow) {
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
    
    // Update status bar recording time
    if (m_statusBarManager) {
        m_statusBarManager->setRecordingTime(formatDuration(elapsedTime));
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
    
    // Show recording indicator in status bar
    if (m_statusBarManager) {
        m_statusBarManager->showRecordingIndicator(true);
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
    updateUIStates();
    
    // Hide recording indicator in status bar
    if (m_statusBarManager) {
        m_statusBarManager->showRecordingIndicator(false);
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

void RecordingController::connectSignals()
{
    // Connect to CameraManager signals for recording state and errors
    if (m_cameraManager) {
        connect(m_cameraManager, &CameraManager::recordingStarted, this, &RecordingController::onCameraRecordingStarted);
        connect(m_cameraManager, &CameraManager::recordingStopped, this, &RecordingController::onRecordingStopped);
        connect(m_cameraManager, &CameraManager::recordingError, this, &RecordingController::onRecordingError);
        qCDebug(log_ui_recordingcontroller) << "Connected to CameraManager signals";
    }
}

void RecordingController::updateUIStates()
{
    // UI state management removed - recording state shown in status bar only
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
    
    // Show recording indicator in status bar
    if (m_statusBarManager) {
        m_statusBarManager->showRecordingIndicator(true);
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
        
        // Hide recording indicator in status bar
        if (m_statusBarManager) {
            m_statusBarManager->showRecordingIndicator(false);
        }
        
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
    QMessageBox msgBox(QMessageBox::Warning, tr("Recording Error"), userMessage, QMessageBox::Ok, nullptr);
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
        QMessageBox::warning(nullptr, tr("Reset Failed"), 
            tr("Cannot reset recording system - camera manager is not available."));
        return;
    }
    
    // Check if recording is in progress
    if (m_isRecording) {
        QMessageBox::StandardButton response = QMessageBox::question(
            nullptr,
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
    
    QMessageBox::information(nullptr, tr("System Reset"),
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
    QDialog *dialog = new QDialog(nullptr);
    dialog->setWindowTitle(tr("Recording System Diagnostics"));
    dialog->resize(800, 600);
    
    QVBoxLayout *layout = new QVBoxLayout(dialog);
    QTextBrowser *textBrowser = new QTextBrowser(dialog);
    textBrowser->setPlainText(diagnostics);
    textBrowser->setReadOnly(true);
    layout->addWidget(textBrowser);
    
    // Add buttons
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    QPushButton *copyButton = new QPushButton(tr("Copy to Clipboard"), dialog);
    QPushButton *closeButton = new QPushButton(tr("Close"), dialog);
    buttonLayout->addStretch();
    buttonLayout->addWidget(copyButton);
    buttonLayout->addWidget(closeButton);
    layout->addLayout(buttonLayout);
    
    // Connect signals
    connect(copyButton, &QPushButton::clicked, [diagnostics]() {
        QApplication::clipboard()->setText(diagnostics);
    });
    connect(closeButton, &QPushButton::clicked, dialog, &QDialog::accept);
    
    // Show dialog and auto-delete when closed
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->exec();
}
