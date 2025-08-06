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

#include "multimediabackend.h"
#include <QLoggingCategory>
#include <QThread>
#include "../ui/globalsetting.h"

Q_LOGGING_CATEGORY(log_multimedia_backend, "opf.multimedia.backend")

// ==========================================================================
// MultimediaBackendHandler (Base Class)
// ==========================================================================

MultimediaBackendHandler::MultimediaBackendHandler(QObject *parent)
    : QObject(parent)
{
    m_config = getDefaultConfig();
}

void MultimediaBackendHandler::prepareCameraCreation(QCamera* oldCamera)
{
    // Default implementation: No special preparation needed
    logBackendMessage("Default: Preparing camera creation.");
}

void MultimediaBackendHandler::configureCameraDevice(QCamera* camera, const QCameraDevice& device)
{
    // Default implementation: No special configuration needed
    logBackendMessage(QString("Default: Configuring camera device to %1.").arg(device.description()));
}

void MultimediaBackendHandler::setupCaptureSession(QMediaCaptureSession* session, QCamera* camera)
{
    // Default implementation: Standard setup
    logBackendMessage("Default: Setting up capture session.");
    session->setCamera(camera);
}

void MultimediaBackendHandler::prepareVideoOutputConnection(QMediaCaptureSession* session, QObject* videoOutput)
{
    // Default implementation: No special preparation
    logBackendMessage("Default: Preparing video output connection.");
}

void MultimediaBackendHandler::finalizeVideoOutputConnection(QMediaCaptureSession* session, QObject* videoOutput)
{
    // Default implementation: Direct connection
    logBackendMessage("Default: Finalizing video output connection.");
    session->setVideoOutput(videoOutput);
}

void MultimediaBackendHandler::startCamera(QCamera* camera)
{
    logBackendMessage("Default: Starting camera.");
    camera->start();
}

void MultimediaBackendHandler::stopCamera(QCamera* camera)
{
    logBackendMessage("Default: Stopping camera.");
    camera->stop();
}

void MultimediaBackendHandler::cleanupCamera(QCamera* camera)
{
    // Default implementation: No special cleanup
    logBackendMessage("Default: Cleaning up camera.");
}

QList<int> MultimediaBackendHandler::getSupportedFrameRates(const QCameraFormat& format) const
{
    // Default implementation: Return a list of standard frame rates within the format's range
    QList<int> rates;
    std::vector<int> standardRates = {5, 10, 15, 20, 24, 25, 30, 50, 60};
    for (int rate : standardRates) {
        if (rate >= format.minFrameRate() && rate <= format.maxFrameRate()) {
            rates.append(rate);
        }
    }
    return rates;
}

bool MultimediaBackendHandler::isFrameRateSupported(const QCameraFormat& format, int frameRate) const
{
    return frameRate >= format.minFrameRate() && frameRate <= format.maxFrameRate();
}

QCameraFormat MultimediaBackendHandler::selectOptimalFormat(const QList<QCameraFormat>& formats,
                                                          const QSize& resolution,
                                                          int desiredFrameRate,
                                                          QVideoFrameFormat::PixelFormat pixelFormat) const
{
    QCameraFormat bestMatch;
    int closestFrameRateDiff = INT_MAX;

    for (const auto& format : formats) {
        if (format.resolution() == resolution && format.pixelFormat() == pixelFormat) {
            if (isFrameRateSupported(format, desiredFrameRate)) {
                int midFrameRate = (format.minFrameRate() + format.maxFrameRate()) / 2;
                int diff = qAbs(desiredFrameRate - midFrameRate);
                if (diff < closestFrameRateDiff) {
                    closestFrameRateDiff = diff;
                    bestMatch = format;
                }
            }
        }
    }
    return bestMatch;
}

void MultimediaBackendHandler::handleCameraError(QCamera::Error error, const QString& errorString)
{
    logBackendError(QString("Camera error occurred: %1 - %2").arg(error).arg(errorString));
}

bool MultimediaBackendHandler::shouldRetryOperation(int attemptCount) const
{
    return attemptCount < m_config.maxRetryAttempts;
}

void MultimediaBackendHandler::logBackendMessage(const QString& message) const
{
    if (m_config.enableVerboseLogging) {
        qCDebug(log_multimedia_backend) << message;
    }
}

void MultimediaBackendHandler::logBackendWarning(const QString& warning) const
{
    qCWarning(log_multimedia_backend) << warning;
}

void MultimediaBackendHandler::logBackendError(const QString& error) const
{
    qCCritical(log_multimedia_backend) << error;
}


// ==========================================================================
// FFmpegBackendHandler
// ==========================================================================

FFmpegBackendHandler::FFmpegBackendHandler(QObject *parent)
    : MultimediaBackendHandler(parent)
{
    m_config = getDefaultConfig();
}

MultimediaBackendType FFmpegBackendHandler::getBackendType() const
{
    return MultimediaBackendType::FFmpeg;
}

QString FFmpegBackendHandler::getBackendName() const
{
    return "FFmpeg";
}

MultimediaBackendConfig FFmpegBackendHandler::getDefaultConfig() const
{
    MultimediaBackendConfig config;
    config.cameraInitDelay = 10;
    config.captureSessionDelay = 10;
    config.useConservativeFrameRates = false;
    return config;
}

void FFmpegBackendHandler::startCamera(QCamera* camera)
{
    logBackendMessage("FFmpeg: Starting camera with minimal delay.");
    camera->start();
    QThread::msleep(25);
}

void FFmpegBackendHandler::stopCamera(QCamera* camera)
{
    logBackendMessage("FFmpeg: Stopping camera with standard procedure.");
    camera->stop();
    QThread::msleep(100);
}

QCameraFormat FFmpegBackendHandler::selectOptimalFormat(const QList<QCameraFormat>& formats,
                                                      const QSize& resolution,
                                                      int desiredFrameRate,
                                                      QVideoFrameFormat::PixelFormat pixelFormat) const
{
    logBackendMessage("FFmpeg: Selecting optimal format with flexible frame rate matching.");
    return MultimediaBackendHandler::selectOptimalFormat(formats, resolution, desiredFrameRate, pixelFormat);
}


// ==========================================================================
// GStreamerBackendHandler
// ==========================================================================

GStreamerBackendHandler::GStreamerBackendHandler(QObject *parent)
    : MultimediaBackendHandler(parent)
{
    m_config = getDefaultConfig();
}

MultimediaBackendType GStreamerBackendHandler::getBackendType() const
{
    return MultimediaBackendType::GStreamer;
}

QString GStreamerBackendHandler::getBackendName() const
{
    return "GStreamer";
}

MultimediaBackendConfig GStreamerBackendHandler::getDefaultConfig() const
{
    MultimediaBackendConfig config;
    config.cameraInitDelay = 25;
    config.deviceSwitchDelay = 25;
    config.videoOutputSetupDelay = 25;
    config.captureSessionDelay = 25;
    config.useConservativeFrameRates = true;
    config.requireVideoOutputReset = true;
    config.useGradualVideoOutputSetup = true;
    config.useStandardFrameRatesOnly = true;
    return config;
}

void GStreamerBackendHandler::prepareCameraCreation(QCamera* oldCamera)
{
    if (oldCamera) {
        logBackendMessage("GStreamer: Disconnecting old camera from capture session before creating new one.");
        // This assumes the capture session is accessible or passed in, for now, we just log
        // In the final implementation, CameraManager will call session->setCamera(nullptr)
        QThread::msleep(m_config.deviceSwitchDelay);
    }
}

void GStreamerBackendHandler::setupCaptureSession(QMediaCaptureSession* session, QCamera* camera)
{
    logBackendMessage("GStreamer: Setting up capture session with delay.");
    session->setCamera(camera);
    QThread::msleep(m_config.captureSessionDelay);
}

void GStreamerBackendHandler::prepareVideoOutputConnection(QMediaCaptureSession* session, QObject* videoOutput)
{
    if (m_config.requireVideoOutputReset) {
        logBackendMessage("GStreamer: Temporarily disconnecting video output before final connection.");
        session->setVideoOutput(nullptr);
        QThread::msleep(m_config.videoOutputSetupDelay);
    }
}

void GStreamerBackendHandler::finalizeVideoOutputConnection(QMediaCaptureSession* session, QObject* videoOutput)
{
    logBackendMessage("GStreamer: Finalizing video output connection with delay.");
    session->setVideoOutput(videoOutput);
    QThread::msleep(m_config.videoOutputSetupDelay);
}

void GStreamerBackendHandler::startCamera(QCamera* camera)
{
    logBackendMessage("GStreamer: Starting camera with extra delay for pipeline setup.");
    camera->start();
    QThread::msleep(50);
}

void GStreamerBackendHandler::stopCamera(QCamera* camera)
{
    logBackendMessage("GStreamer: Stopping camera with careful shutdown procedure.");
    // In CameraManager, video output will be disconnected first
    camera->stop();
    QThread::msleep(100);
}

QList<int> GStreamerBackendHandler::getSupportedFrameRates(const QCameraFormat& format) const
{
    if (m_config.useStandardFrameRatesOnly) {
        logBackendMessage("GStreamer: Providing only standard, safe frame rates.");
        QList<int> rates;
        std::vector<int> safeRates = {5, 10, 15, 20, 24, 25, 30, 50, 60};
        for (int rate : safeRates) {
            if (rate >= format.minFrameRate() && rate <= format.maxFrameRate()) {
                rates.append(rate);
            }
        }
        return rates;
    }
    return MultimediaBackendHandler::getSupportedFrameRates(format);
}

QCameraFormat GStreamerBackendHandler::selectOptimalFormat(const QList<QCameraFormat>& formats,
                                                         const QSize& resolution,
                                                         int desiredFrameRate,
                                                         QVideoFrameFormat::PixelFormat pixelFormat) const
{
    logBackendMessage("GStreamer: Selecting optimal format with conservative frame rate matching.");
    // GStreamer can be strict, prefer exact matches
    for (const auto& format : formats) {
        if (format.resolution() == resolution && format.pixelFormat() == pixelFormat) {
            if (desiredFrameRate >= format.minFrameRate() && desiredFrameRate <= format.maxFrameRate()) {
                 // For GStreamer, an exact match or being in a tight range is better
                if (desiredFrameRate == format.minFrameRate() || desiredFrameRate == format.maxFrameRate()) {
                    return format;
                }
            }
        }
    }
    // Fallback to default
    return MultimediaBackendHandler::selectOptimalFormat(formats, resolution, desiredFrameRate, pixelFormat);
}

void GStreamerBackendHandler::handleCameraError(QCamera::Error error, const QString& errorString)
{
    logBackendError(QString("GStreamer Camera Error: %1 - %2").arg(error).arg(errorString));
    if (errorString.contains("GStreamer")) {
        emit backendWarning("A GStreamer-specific error occurred. Please check GStreamer installation and plugins.");
    }
}


// ==========================================================================
// MultimediaBackendFactory
// ==========================================================================

MultimediaBackendType MultimediaBackendFactory::detectBackendType()
{
    QString backendName = GlobalSetting::instance().getMediaBackend();
    return parseBackendType(backendName);
}

MultimediaBackendType MultimediaBackendFactory::parseBackendType(const QString& backendName)
{
    if (backendName.compare("gstreamer", Qt::CaseInsensitive) == 0) {
        return MultimediaBackendType::GStreamer;
    }
    if (backendName.compare("ffmpeg", Qt::CaseInsensitive) == 0) {
        return MultimediaBackendType::FFmpeg;
    }
    return MultimediaBackendType::Unknown;
}

QString MultimediaBackendFactory::backendTypeToString(MultimediaBackendType type)
{
    switch (type) {
        case MultimediaBackendType::FFmpeg:
            return "FFmpeg";
        case MultimediaBackendType::GStreamer:
            return "GStreamer";
        default:
            return "Unknown";
    }
}

std::unique_ptr<MultimediaBackendHandler> MultimediaBackendFactory::createHandler(MultimediaBackendType type, QObject* parent)
{
    switch (type) {
        case MultimediaBackendType::GStreamer:
            return std::make_unique<GStreamerBackendHandler>(parent);
        case MultimediaBackendType::FFmpeg:
            return std::make_unique<FFmpegBackendHandler>(parent);
        default:
            qCWarning(log_multimedia_backend) << "Unknown or unsupported backend type requested, falling back to FFmpeg.";
            return std::make_unique<FFmpegBackendHandler>(parent);
    }
}

std::unique_ptr<MultimediaBackendHandler> MultimediaBackendFactory::createHandler(const QString& backendName, QObject* parent)
{
    MultimediaBackendType type = parseBackendType(backendName);
    return createHandler(type, parent);
}

std::unique_ptr<MultimediaBackendHandler> MultimediaBackendFactory::createAutoDetectedHandler(QObject* parent)
{
    MultimediaBackendType type = detectBackendType();
    return createHandler(type, parent);
}
