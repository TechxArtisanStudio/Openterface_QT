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
#include "backend/ffmpegbackendhandler.h"
#ifndef Q_OS_WIN
#include "backend/gstreamerbackendhandler.h"
#endif
#include "backend/qtbackendhandler.h"
#include "backend/qtmultimediabackendhandler.h"
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

void MultimediaBackendHandler::prepareCameraCreation()
{
    // Default implementation: No special preparation needed
    logBackendMessage("Default: Preparing camera creation.");
}

void MultimediaBackendHandler::configureCameraDevice()
{
    // Default implementation: No special configuration needed
    logBackendMessage("Default: Configuring camera device.");
}

void MultimediaBackendHandler::setupCaptureSession(QMediaCaptureSession* session)
{
    // Default implementation: Standard setup
    logBackendMessage("Default: Setting up capture session.");
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

void MultimediaBackendHandler::startCamera()
{
    logBackendMessage("Default: Starting camera.");
}

void MultimediaBackendHandler::stopCamera()
{
    logBackendMessage("Default: Stopping camera.");
}

void MultimediaBackendHandler::cleanupCamera()
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

int MultimediaBackendHandler::getOptimalFrameRate(const QCameraFormat& format, int desiredFrameRate) const
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

void MultimediaBackendHandler::validateCameraFormat(const QCameraFormat& format) const
{
    logBackendMessage(QString("Validating camera format: %1x%2, fps: %3-%4")
                     .arg(format.resolution().width())
                     .arg(format.resolution().height())
                     .arg(format.minFrameRate())
                     .arg(format.maxFrameRate()));
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

void MultimediaBackendHandler::handleCameraError(int errorCode, const QString& errorString)
{
    logBackendError(QString("Camera error occurred: %1 - %2").arg(errorCode).arg(errorString));
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

MultimediaBackendConfig MultimediaBackendHandler::getDefaultConfig() const
{
    // Default config matches FFmpegBackendHandler
    MultimediaBackendConfig config;
    config.cameraInitDelay = 10;
    config.captureSessionDelay = 10;
    config.useConservativeFrameRates = false;
    // Other fields use their struct defaults
    return config;
}


// ==========================================================================
// MultimediaBackendFactory
// ==========================================================================

MultimediaBackendType MultimediaBackendFactory::detectBackendType()
{
    QString backendName = GlobalSetting::instance().getMediaBackend();
    MultimediaBackendType type = parseBackendType(backendName);
    
    // If no specific backend is configured or unknown, auto-detect FFmpeg
    if (type == MultimediaBackendType::Unknown) {
        qCDebug(log_multimedia_backend) << "Auto-detected FFmpeg backend";
        return MultimediaBackendType::FFmpeg;
    }
    
    return type;
}

MultimediaBackendType MultimediaBackendFactory::parseBackendType(const QString& backendName)
{
    if (backendName.compare("qtmultimedia", Qt::CaseInsensitive) == 0) {
        return MultimediaBackendType::QtMultimedia;
    }
    if (backendName.compare("qt", Qt::CaseInsensitive) == 0) {
        return MultimediaBackendType::Qt;
    }
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
        case MultimediaBackendType::QtMultimedia:
            return "Qt Multimedia (Legacy)";
        case MultimediaBackendType::Qt:
            return "Qt Multimedia (Windows)";
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
#ifndef Q_OS_WIN
        case MultimediaBackendType::GStreamer:
            return std::make_unique<GStreamerBackendHandler>(parent);
#endif
        case MultimediaBackendType::FFmpeg:
            return std::make_unique<FFmpegBackendHandler>(parent);
        case MultimediaBackendType::Qt:
            return std::make_unique<QtBackendHandler>(parent);
        case MultimediaBackendType::QtMultimedia:
            return std::make_unique<QtMultimediaBackendHandler>(parent);
        default:
            qCWarning(log_multimedia_backend) << "Unknown backend type requested, falling back to FFmpeg backend.";
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
