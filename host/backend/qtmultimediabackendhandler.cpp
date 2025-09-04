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

#include "qtmultimediabackendhandler.h"
#include "../ui/videopane.h"
#include "../global.h"


#include <QThread>
#include <QDebug>
#include <QLoggingCategory>
#include <QCamera>
#include <QMediaCaptureSession>
#include <QCameraDevice>
#include <QCameraFormat>
#include <QGraphicsVideoItem>
#include <QVideoFrameFormat>

// Logging category for Qt Multimedia backend
Q_LOGGING_CATEGORY(log_qtmultimedia_backend, "opf.backend.qtmultimedia")

QtMultimediaBackendHandler::QtMultimediaBackendHandler(QObject *parent)
    : MultimediaBackendHandler(parent),
      m_graphicsVideoItem(nullptr),
      m_videoPane(nullptr)
{
    m_config = getDefaultConfig();
    qCDebug(log_qtmultimedia_backend) << "Qt Multimedia backend handler initialized";
}

QtMultimediaBackendHandler::~QtMultimediaBackendHandler()
{
    qCDebug(log_qtmultimedia_backend) << "Qt Multimedia backend handler destroyed";
}

MultimediaBackendType QtMultimediaBackendHandler::getBackendType() const
{
    return MultimediaBackendType::QtMultimedia;
}

QString QtMultimediaBackendHandler::getBackendName() const
{
    return "Qt Multimedia";
}

MultimediaBackendConfig QtMultimediaBackendHandler::getDefaultConfig() const
{
    MultimediaBackendConfig config;
    // Qt Multimedia typically works well with standard delays
    config.cameraInitDelay = 500;          // Slightly longer init delay for device setup
    config.captureSessionDelay = 100;      // Allow time for session setup
    config.useConservativeFrameRates = true;   // Be conservative with frame rates
    config.requireVideoOutputReset = false;    // Qt handles output well
    config.useGradualVideoOutputSetup = true;  // Use gradual setup for smoother transitions
    config.deviceSwitchDelay = 300;            // Allow time for device switching
    return config;
}

void QtMultimediaBackendHandler::prepareCameraCreation(QCamera* oldCamera)
{
    if (oldCamera) {
        qCDebug(log_qtmultimedia_backend) << "Qt Multimedia: Stopping old camera before creating new one";
        oldCamera->stop();
        QThread::msleep(m_config.deviceSwitchDelay);
    }
    qCDebug(log_qtmultimedia_backend) << "Qt Multimedia: Camera creation prepared";
}

void QtMultimediaBackendHandler::configureCameraDevice(QCamera* camera, const QCameraDevice& device)
{
    qCDebug(log_qtmultimedia_backend) << "Qt Multimedia: Configuring camera device:" << device.description();
    
    if (camera) {
        // Standard Qt camera configuration
        qCDebug(log_qtmultimedia_backend) << "Camera configured for device:" << device.id();
    }
}

void QtMultimediaBackendHandler::setupCaptureSession(QMediaCaptureSession* session, QCamera* camera)
{
    qCDebug(log_qtmultimedia_backend) << "Qt Multimedia: Setting up capture session";
    
    if (session && camera) {
        // Standard Qt capture session setup
        session->setCamera(camera);
        qCDebug(log_qtmultimedia_backend) << "Capture session configured with camera";
        
        // Allow time for session setup
        if (m_config.captureSessionDelay > 0) {
            QThread::msleep(m_config.captureSessionDelay);
        }
    }
}

void QtMultimediaBackendHandler::prepareVideoOutputConnection(QMediaCaptureSession* session, QObject* videoOutput)
{
    qCDebug(log_qtmultimedia_backend) << "Qt Multimedia: Preparing video output connection";
    
    if (!session || !videoOutput) {
        qCWarning(log_qtmultimedia_backend) << "Invalid session or video output";
        return;
    }
    
    // Check if videoOutput is a VideoPane
    if (VideoPane* videoPane = qobject_cast<VideoPane*>(videoOutput)) {
        setVideoOutput(videoPane);
        qCDebug(log_qtmultimedia_backend) << "Qt Multimedia: VideoPane detected and set";
    }
    
    // Check if videoOutput is a QGraphicsVideoItem
    if (QGraphicsVideoItem* videoItem = qobject_cast<QGraphicsVideoItem*>(videoOutput)) {
        setVideoOutput(videoItem);
        qCDebug(log_qtmultimedia_backend) << "Qt Multimedia: QGraphicsVideoItem detected and set";
    }
}

void QtMultimediaBackendHandler::finalizeVideoOutputConnection(QMediaCaptureSession* session, QObject* videoOutput)
{
    qCDebug(log_qtmultimedia_backend) << "Qt Multimedia: Finalizing video output connection";
    
    if (session && videoOutput) {
        // For Qt Multimedia, set the video output on the session
        if (QGraphicsVideoItem* videoItem = qobject_cast<QGraphicsVideoItem*>(videoOutput)) {
            session->setVideoOutput(videoItem);
            qCDebug(log_qtmultimedia_backend) << "Video output set to QGraphicsVideoItem";
        } else if (VideoPane* videoPane = qobject_cast<VideoPane*>(videoOutput)) {
            // VideoPane should handle Qt multimedia internally
            session->setVideoOutput(videoPane);
            qCDebug(log_qtmultimedia_backend) << "Video output set to VideoPane";
        }
    }
}

void QtMultimediaBackendHandler::startCamera(QCamera* camera)
{
    qCDebug(log_qtmultimedia_backend) << "Qt Multimedia: Starting camera";
    
    if (camera) {
        camera->start();
        qCDebug(log_qtmultimedia_backend) << "Camera started successfully";
        
        // Allow time for camera initialization
        if (m_config.cameraInitDelay > 0) {
            QThread::msleep(m_config.cameraInitDelay);
        }
    } else {
        qCWarning(log_qtmultimedia_backend) << "No camera to start";
    }
}

void QtMultimediaBackendHandler::stopCamera(QCamera* camera)
{
    qCDebug(log_qtmultimedia_backend) << "Qt Multimedia: Stopping camera";
    
    if (camera) {
        camera->stop();
        qCDebug(log_qtmultimedia_backend) << "Camera stopped";
    }
}

QCameraFormat QtMultimediaBackendHandler::selectOptimalFormat(const QList<QCameraFormat>& formats,
                                                            const QSize& resolution,
                                                            int desiredFrameRate,
                                                            QVideoFrameFormat::PixelFormat pixelFormat) const
{
    qCDebug(log_qtmultimedia_backend) << "Qt Multimedia: Selecting optimal format";
    qCDebug(log_qtmultimedia_backend) << "Requested resolution:" << resolution;
    qCDebug(log_qtmultimedia_backend) << "Desired frame rate:" << desiredFrameRate;
    qCDebug(log_qtmultimedia_backend) << "Available formats:" << formats.size();

    if (formats.isEmpty()) {
        qCWarning(log_qtmultimedia_backend) << "No camera formats available";
        return QCameraFormat();
    }

    QCameraFormat bestFormat;
    int bestScore = -1;

    for (const auto& format : formats) {
        int score = 0;
        
        // Resolution matching (higher priority)
        QSize formatResolution = format.resolution();
        if (formatResolution == resolution) {
            score += 1000; // Exact match
        } else {
            // Prefer formats close to the desired resolution
            int resolutionDiff = qAbs(formatResolution.width() * formatResolution.height() - 
                                    resolution.width() * resolution.height());
            score += qMax(0, 500 - resolutionDiff / 1000);
        }
        
        // Frame rate matching
        qreal formatFrameRate = format.maxFrameRate();
        if (qAbs(formatFrameRate - desiredFrameRate) < 1.0) {
            score += 300; // Close frame rate match
        } else if (formatFrameRate >= desiredFrameRate) {
            score += 200; // Higher frame rate is acceptable
        } else {
            score += 100; // Lower frame rate is less desirable
        }
        
        // Pixel format preference
        if (format.pixelFormat() == pixelFormat) {
            score += 100;
        } else if (format.pixelFormat() == QVideoFrameFormat::Format_YUV420P ||
                   format.pixelFormat() == QVideoFrameFormat::Format_NV12) {
            score += 50; // Common formats
        }

        qCDebug(log_qtmultimedia_backend) << "Format:" << formatResolution << "@" << formatFrameRate 
                                         << "fps, score:" << score;

        if (score > bestScore) {
            bestScore = score;
            bestFormat = format;
        }
    }

    qCDebug(log_qtmultimedia_backend) << "Selected format:" << bestFormat.resolution() 
                                     << "@" << bestFormat.maxFrameRate() << "fps";
    return bestFormat;
}

void QtMultimediaBackendHandler::setVideoOutput(QGraphicsVideoItem* videoItem)
{
    m_graphicsVideoItem = videoItem;
    m_videoPane = nullptr;
    
    if (videoItem) {
        qCDebug(log_qtmultimedia_backend) << "Graphics video item set for Qt Multimedia";
    }
}

void QtMultimediaBackendHandler::setVideoOutput(VideoPane* videoPane)
{
    m_videoPane = videoPane;
    m_graphicsVideoItem = nullptr;
    
    if (videoPane) {
        qCDebug(log_qtmultimedia_backend) << "VideoPane set for Qt Multimedia";
    }
}
