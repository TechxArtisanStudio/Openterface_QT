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

#include "qtbackendhandler.h"
#include <QLoggingCategory>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <QUrl>
#include <QDebug>

Q_LOGGING_CATEGORY(log_qt_backend, "opf.backend.qt")

QtBackendHandler::QtBackendHandler(QObject *parent)
    : MultimediaBackendHandler(parent)
    , m_mediaRecorder(nullptr)
    , m_captureSession(nullptr)
    , m_recordingActive(false)
    , m_recordingPaused(false)
    , m_recordingStartTime(0)
    , m_totalPausedDuration(0)
    , m_lastPauseTime(0)
{
    qCDebug(log_qt_backend) << "QtBackendHandler initialized for Windows platform";
    
    // Setup duration update timer
    m_durationUpdateTimer = new QTimer(this);
    m_durationUpdateTimer->setInterval(100); // Update every 100ms
    connect(m_durationUpdateTimer, &QTimer::timeout, this, &QtBackendHandler::updateRecordingDuration);
}

QtBackendHandler::~QtBackendHandler()
{
    qCDebug(log_qt_backend) << "QtBackendHandler destructor";
    
    if (m_recordingActive && m_mediaRecorder) {
        // Stop recording gracefully
        try {
            m_mediaRecorder->stop();
        } catch (...) {
            qCWarning(log_qt_backend) << "Exception while stopping recording in destructor";
        }
    }
    
    if (m_durationUpdateTimer) {
        m_durationUpdateTimer->stop();
    }
}

MultimediaBackendType QtBackendHandler::getBackendType() const
{
    return MultimediaBackendType::Qt;
}

QString QtBackendHandler::getBackendName() const
{
    return "Qt Multimedia (Windows)";
}

bool QtBackendHandler::isBackendAvailable() const
{
    return true; // Qt Multimedia is always available on Windows
}

void QtBackendHandler::configureCameraDevice(QCamera* camera, const QCameraDevice& device)
{
    Q_UNUSED(camera)
    Q_UNUSED(device)
    qCDebug(log_qt_backend) << "configureCameraDevice - Qt backend uses standard camera configuration";
}

void QtBackendHandler::prepareCameraCreation(QCamera* camera)
{
    Q_UNUSED(camera)
    qCDebug(log_qt_backend) << "prepareCameraCreation - Qt backend uses standard camera creation";
}

void QtBackendHandler::setupCaptureSession(QMediaCaptureSession* session, QCamera* camera)
{
    qCDebug(log_qt_backend) << "setupCaptureSession - Using Qt standard setup";
    if (session && camera) {
        session->setCamera(camera);
        
        // Set the recorder on the capture session if available
        if (m_mediaRecorder) {
            session->setRecorder(m_mediaRecorder);
            qCDebug(log_qt_backend) << "Media recorder set on capture session";
        } else {
            qCDebug(log_qt_backend) << "No media recorder available to set on session";
        }
    }
}

QCameraFormat QtBackendHandler::selectOptimalFormat(const QList<QCameraFormat>& formats, 
                                                   const QSize& preferredResolution, 
                                                   int preferredFrameRate, 
                                                   QVideoFrameFormat::PixelFormat preferredPixelFormat) const
{
    Q_UNUSED(preferredPixelFormat)
    
    if (formats.isEmpty()) {
        qCWarning(log_qt_backend) << "No camera formats available";
        return QCameraFormat();
    }
    
    QCameraFormat bestFormat;
    int bestScore = -1;
    
    for (const QCameraFormat& format : formats) {
        int score = 0;
        
        // Resolution score
        QSize resolution = format.resolution();
        if (resolution == preferredResolution) {
            score += 100;
        } else {
            // Prefer resolution close to preferred
            int resolutionDiff = qAbs(resolution.width() - preferredResolution.width()) + 
                               qAbs(resolution.height() - preferredResolution.height());
            score += qMax(0, 50 - resolutionDiff / 10);
        }
        
        // Frame rate score
        int minRate = format.minFrameRate();
        int maxRate = format.maxFrameRate();
        if (preferredFrameRate >= minRate && preferredFrameRate <= maxRate) {
            score += 50;
        }
        
        if (score > bestScore) {
            bestScore = score;
            bestFormat = format;
        }
    }
    
    qCDebug(log_qt_backend) << "Selected optimal format:" << bestFormat.resolution() 
                           << "fps:" << bestFormat.minFrameRate() << "-" << bestFormat.maxFrameRate();
    
    return bestFormat;
}

QList<int> QtBackendHandler::getSupportedFrameRates(const QCameraFormat& format) const
{
    QList<int> frameRates;
    int minRate = format.minFrameRate();
    int maxRate = format.maxFrameRate();
    
    if (minRate > 0 && maxRate > 0) {
        // Generate common frame rates within the supported range
        QList<int> commonRates = {15, 24, 25, 30, 60};
        for (int rate : commonRates) {
            if (rate >= minRate && rate <= maxRate) {
                frameRates.append(rate);
            }
        }
        
        // Add the min and max rates if they're not already included
        if (!frameRates.contains(minRate)) {
            frameRates.prepend(minRate);
        }
        if (!frameRates.contains(maxRate)) {
            frameRates.append(maxRate);
        }
    }
    
    return frameRates;
}

bool QtBackendHandler::isFrameRateSupported(const QCameraFormat& format, int frameRate) const
{
    return frameRate >= format.minFrameRate() && frameRate <= format.maxFrameRate();
}

int QtBackendHandler::getOptimalFrameRate(const QCameraFormat& format, int desiredFrameRate) const
{
    int minRate = format.minFrameRate();
    int maxRate = format.maxFrameRate();
    
    if (desiredFrameRate < minRate) {
        return minRate;
    } else if (desiredFrameRate > maxRate) {
        return maxRate;
    } else {
        return desiredFrameRate;
    }
}

void QtBackendHandler::validateCameraFormat(const QCameraFormat& format) const
{
    qCDebug(log_qt_backend) << "Validating camera format:";
    qCDebug(log_qt_backend) << "  Resolution:" << format.resolution();
    qCDebug(log_qt_backend) << "  Frame rate range:" << format.minFrameRate() << "-" << format.maxFrameRate();
    qCDebug(log_qt_backend) << "  Pixel format:" << static_cast<int>(format.pixelFormat());
}

bool QtBackendHandler::startRecording(const QString& outputPath, const QString& format, int videoBitrate)
{
    qCDebug(log_qt_backend) << "Starting recording:" << outputPath << "format:" << format << "bitrate:" << videoBitrate;
    qCDebug(log_qt_backend) << "Current media recorder pointer:" << (void*)m_mediaRecorder;
    qCDebug(log_qt_backend) << "Current capture session pointer:" << (void*)m_captureSession;
    
    if (!m_mediaRecorder) {
        qCWarning(log_qt_backend) << "No media recorder available. The Camera just the mjpeg.";
        emit recordingError("No media recorder available. The Camera just the mjpeg.");
        return false;
    }
    
    if (!m_captureSession) {
        qCWarning(log_qt_backend) << "No capture session available";
        emit recordingError("No capture session available");
        return false;
    }
    
    // Ensure the media recorder is connected to the capture session
    if (m_captureSession->recorder() != m_mediaRecorder) {
        qCDebug(log_qt_backend) << "Media recorder not connected to capture session, connecting now";
        m_captureSession->setRecorder(m_mediaRecorder);
        qCDebug(log_qt_backend) << "Media recorder connected to capture session";
    } else {
        qCDebug(log_qt_backend) << "Media recorder already connected to capture session";
    }
    
    if (m_recordingActive) {
        qCWarning(log_qt_backend) << "Recording is already active";
        emit recordingError("Recording is already active");
        return false;
    }
    
    // Ensure output directory exists
    QFileInfo fileInfo(outputPath);
    QDir outputDir = fileInfo.dir();
    if (!outputDir.exists()) {
        if (!outputDir.mkpath(".")) {
            QString error = QString("Failed to create output directory: %1").arg(outputDir.absolutePath());
            qCWarning(log_qt_backend) << error;
            emit recordingError(error);
            return false;
        }
    }
    
    // Configure recorder settings
    setupRecorderSettings(outputPath, format, videoBitrate);
    
    // Start recording
    try {
        m_mediaRecorder->record();
        m_currentOutputPath = outputPath;
        m_recordingStartTime = QDateTime::currentMSecsSinceEpoch();
        m_totalPausedDuration = 0;
        m_recordingTimer.start();
        m_durationUpdateTimer->start();
        
        // Check recorder state immediately
        qCDebug(log_qt_backend) << "Recorder state after record() call:" << m_mediaRecorder->recorderState();
        
        // Set our internal state and emit signal immediately
        // The state change callback will also trigger, but this ensures the signal is sent
        if (!m_recordingActive) {
            m_recordingActive = true;
            m_recordingPaused = false;
            qCDebug(log_qt_backend) << "Emitting recordingStarted signal immediately";
            emit recordingStarted(m_currentOutputPath);
        }
        
        qCInfo(log_qt_backend) << "Recording started successfully to:" << outputPath;
        return true;
        
    } catch (const std::exception& e) {
        QString error = QString("Failed to start recording: %1").arg(e.what());
        qCWarning(log_qt_backend) << error;
        emit recordingError(error);
        return false;
    }
}

void QtBackendHandler::stopRecording()
{
    qCDebug(log_qt_backend) << "Stopping recording";
    qCDebug(log_qt_backend) << "Current state - m_recordingActive:" << m_recordingActive << "m_mediaRecorder:" << m_mediaRecorder;
    
    if (!m_recordingActive) {
        qCWarning(log_qt_backend) << "Not currently recording (m_recordingActive is false)";
        return;
    }
    
    if (!m_mediaRecorder) {
        qCWarning(log_qt_backend) << "No media recorder available";
        return;
    }
    
    qCDebug(log_qt_backend) << "Media recorder state before stop():" << m_mediaRecorder->recorderState();
    
    try {
        m_mediaRecorder->stop();
        qCDebug(log_qt_backend) << "Called mediaRecorder->stop()";
        qCDebug(log_qt_backend) << "Media recorder state after stop():" << m_mediaRecorder->recorderState();
        
        // Add a fallback mechanism - if state doesn't change within reasonable time, force stop
        QTimer::singleShot(2000, this, [this]() {
            if (m_recordingActive) {
                qCWarning(log_qt_backend) << "Recorder didn't stop after 2 seconds, forcing stop";
                m_recordingActive = false;
                m_recordingPaused = false;
                m_durationUpdateTimer->stop();
                emit recordingStopped();
            }
        });
        
        // The onRecorderStateChanged slot will handle the rest
    } catch (const std::exception& e) {
        qCWarning(log_qt_backend) << "Exception while stopping recording:" << e.what();
        // Manually clean up state
        m_recordingActive = false;
        m_recordingPaused = false;
        m_durationUpdateTimer->stop();
        emit recordingStopped();
    }
}

void QtBackendHandler::pauseRecording()
{
    qCDebug(log_qt_backend) << "Pausing recording";
    
    if (!m_recordingActive || m_recordingPaused || !m_mediaRecorder) {
        qCWarning(log_qt_backend) << "Cannot pause recording - not recording or already paused";
        return;
    }
    
    try {
        m_mediaRecorder->pause();
        // The onRecorderStateChanged slot will handle the rest
    } catch (const std::exception& e) {
        qCWarning(log_qt_backend) << "Exception while pausing recording:" << e.what();
        emit recordingError(QString("Failed to pause recording: %1").arg(e.what()));
    }
}

void QtBackendHandler::resumeRecording()
{
    qCDebug(log_qt_backend) << "Resuming recording";
    
    if (!m_recordingActive || !m_recordingPaused || !m_mediaRecorder) {
        qCWarning(log_qt_backend) << "Cannot resume recording - not recording or not paused";
        return;
    }
    
    try {
        m_mediaRecorder->record();
        // The onRecorderStateChanged slot will handle the rest
    } catch (const std::exception& e) {
        qCWarning(log_qt_backend) << "Exception while resuming recording:" << e.what();
        emit recordingError(QString("Failed to resume recording: %1").arg(e.what()));
    }
}

bool QtBackendHandler::isRecording() const
{
    return m_recordingActive;
}

QString QtBackendHandler::getCurrentRecordingPath() const
{
    return m_currentOutputPath;
}

qint64 QtBackendHandler::getRecordingDuration() const
{
    if (!m_recordingActive || !m_recordingTimer.isValid()) {
        return 0;
    }
    
    qint64 elapsed = m_recordingTimer.elapsed();
    return elapsed - m_totalPausedDuration;
}

void QtBackendHandler::setMediaRecorder(QMediaRecorder* recorder)
{
    qCDebug(log_qt_backend) << "setMediaRecorder called with recorder:" << (void*)recorder;
    
    if (m_mediaRecorder == recorder) {
        qCDebug(log_qt_backend) << "Same recorder already set, skipping";
        return;
    }
    
    // Disconnect from previous recorder
    if (m_mediaRecorder) {
        qCDebug(log_qt_backend) << "Disconnecting from previous recorder:" << (void*)m_mediaRecorder;
        disconnect(m_mediaRecorder, nullptr, this, nullptr);
    }
    
    m_mediaRecorder = recorder;
    
    if (m_mediaRecorder) {
        setupRecorderConnections();
        qCDebug(log_qt_backend) << "Media recorder set and connected:" << (void*)m_mediaRecorder;
    } else {
        qCDebug(log_qt_backend) << "Media recorder set to nullptr";
    }
}

void QtBackendHandler::setCaptureSession(QMediaCaptureSession* captureSession)
{
    qCDebug(log_qt_backend) << "setCaptureSession called with session:" << (void*)captureSession;
    
    m_captureSession = captureSession;
    
    if (m_captureSession) {
        qCDebug(log_qt_backend) << "Capture session set successfully:" << (void*)m_captureSession;
    } else {
        qCDebug(log_qt_backend) << "Capture session set to nullptr";
    }
}

void QtBackendHandler::onRecorderStateChanged(QMediaRecorder::RecorderState state)
{
    qCDebug(log_qt_backend) << "Recorder state changed to:" << state << "(" << 
        (state == QMediaRecorder::StoppedState ? "Stopped" :
         state == QMediaRecorder::RecordingState ? "Recording" :
         state == QMediaRecorder::PausedState ? "Paused" : "Unknown") << ")";
    
    switch (state) {
    case QMediaRecorder::RecordingState:
        qCDebug(log_qt_backend) << "State change: RecordingState - m_recordingActive:" << m_recordingActive << "m_recordingPaused:" << m_recordingPaused;
        if (!m_recordingActive) {
            m_recordingActive = true;
            m_recordingPaused = false;
            qCDebug(log_qt_backend) << "Emitting recordingStarted signal from state change";
            emit recordingStarted(m_currentOutputPath);
        } else if (m_recordingPaused) {
            // Resume from pause
            m_totalPausedDuration += QDateTime::currentMSecsSinceEpoch() - m_lastPauseTime;
            m_recordingPaused = false;
            qCDebug(log_qt_backend) << "Emitting recordingResumed signal";
            emit recordingResumed();
        }
        break;
        
    case QMediaRecorder::PausedState:
        if (m_recordingActive && !m_recordingPaused) {
            m_recordingPaused = true;
            m_lastPauseTime = QDateTime::currentMSecsSinceEpoch();
            emit recordingPaused();
        }
        break;
        
    case QMediaRecorder::StoppedState:
        qCDebug(log_qt_backend) << "State change: StoppedState - m_recordingActive:" << m_recordingActive;
        if (m_recordingActive) {
            m_recordingActive = false;
            m_recordingPaused = false;
            m_durationUpdateTimer->stop();
            qCDebug(log_qt_backend) << "Emitting recordingStopped signal";
            emit recordingStopped();
        }
        break;
    }
}

void QtBackendHandler::onRecorderError(QMediaRecorder::Error error, const QString& errorString)
{
    Q_UNUSED(error)
    qCWarning(log_qt_backend) << "Recorder error:" << errorString;
    
    // Clean up state on error
    m_recordingActive = false;
    m_recordingPaused = false;
    m_durationUpdateTimer->stop();
    
    emit recordingError(errorString);
}

void QtBackendHandler::onRecorderDurationChanged(qint64 duration)
{
    Q_UNUSED(duration)
    // Use our own duration calculation for consistency with other backends
    emit recordingDurationChanged(getRecordingDuration());
}

void QtBackendHandler::updateRecordingDuration()
{
    if (m_recordingActive && !m_recordingPaused) {
        emit recordingDurationChanged(getRecordingDuration());
    }
}

void QtBackendHandler::setupRecorderConnections()
{
    if (!m_mediaRecorder) {
        qCWarning(log_qt_backend) << "Cannot setup recorder connections - no media recorder";
        return;
    }
    
    qCDebug(log_qt_backend) << "Setting up recorder signal connections";
    
    // Connect state change signal
    bool stateConnected = connect(m_mediaRecorder, &QMediaRecorder::recorderStateChanged,
            this, &QtBackendHandler::onRecorderStateChanged);
    qCDebug(log_qt_backend) << "State change signal connected:" << stateConnected;
    
    // Connect error signal  
    bool errorConnected = connect(m_mediaRecorder, &QMediaRecorder::errorOccurred,
            this, &QtBackendHandler::onRecorderError);
    qCDebug(log_qt_backend) << "Error signal connected:" << errorConnected;
    
    // Connect duration signal
    bool durationConnected = connect(m_mediaRecorder, &QMediaRecorder::durationChanged,
            this, &QtBackendHandler::onRecorderDurationChanged);
    qCDebug(log_qt_backend) << "Duration signal connected:" << durationConnected;
    
    qCDebug(log_qt_backend) << "All recorder connections setup complete";
}

void QtBackendHandler::setupRecorderSettings(const QString& outputPath, const QString& format, int videoBitrate)
{
    if (!m_mediaRecorder) {
        return;
    }
    
    // Set output location
    m_mediaRecorder->setOutputLocation(QUrl::fromLocalFile(outputPath));
    
    // Configure media format
    QMediaFormat mediaFormat;
    mediaFormat.setFileFormat(getFileFormatFromString(format));
    mediaFormat.setVideoCodec(getVideoCodecFromFormat(format));
    mediaFormat.setAudioCodec(QMediaFormat::AudioCodec::AAC);
    
    m_mediaRecorder->setMediaFormat(mediaFormat);
    
    // Set quality and encoding mode
    m_mediaRecorder->setQuality(QMediaRecorder::Quality::HighQuality);
    m_mediaRecorder->setEncodingMode(QMediaRecorder::EncodingMode::ConstantQualityEncoding);
    
    // Note: Individual video settings like bitrate are handled differently in Qt6
    // and may not be directly configurable depending on the platform
    
    qCDebug(log_qt_backend) << "Recorder configured:"
                           << "format:" << format
                           << "bitrate:" << videoBitrate
                           << "output:" << outputPath;
}

QMediaFormat::FileFormat QtBackendHandler::getFileFormatFromString(const QString& format)
{
    QString lowerFormat = format.toLower();
    
    if (lowerFormat == "mp4") {
        return QMediaFormat::MPEG4;
    } else if (lowerFormat == "avi") {
        return QMediaFormat::AVI;
    } else if (lowerFormat == "mov") {
        return QMediaFormat::QuickTime;
    } else if (lowerFormat == "mkv") {
        return QMediaFormat::Matroska;
    } else if (lowerFormat == "webm") {
        return QMediaFormat::WebM;
    } else {
        // Default to MP4 for unknown formats
        qCWarning(log_qt_backend) << "Unknown format:" << format << "defaulting to MP4";
        return QMediaFormat::MPEG4;
    }
}

QMediaFormat::VideoCodec QtBackendHandler::getVideoCodecFromFormat(const QString& format)
{
    QString lowerFormat = format.toLower();
    
    // Choose appropriate codec based on format
    if (lowerFormat == "webm") {
        return QMediaFormat::VideoCodec::VP8; // WebM typically uses VP8/VP9
    } else if (lowerFormat == "avi") {
        return QMediaFormat::VideoCodec::H264; // AVI can use H.264
    } else {
        return QMediaFormat::VideoCodec::H264; // Default to H.264 for most formats
    }
}