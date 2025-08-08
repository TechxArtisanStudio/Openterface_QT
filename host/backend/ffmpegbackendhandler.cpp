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

#include "ffmpegbackendhandler.h"
#include <QThread>

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
