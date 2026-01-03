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

#ifndef QTMULTIMEDIABACKENDHANDLER_H
#define QTMULTIMEDIABACKENDHANDLER_H

#include "../multimediabackend.h"
#include <QObject>

class QCamera;
class QMediaCaptureSession;
class QGraphicsVideoItem;
class VideoPane;

/**
 * @brief Qt Multimedia backend handler implementation using Qt's native multimedia framework
 * 
 * This backend provides standard Qt multimedia functionality for platforms where
 * FFmpeg and GStreamer are not available or desired (primarily Windows).
 */
class QtMultimediaBackendHandler : public MultimediaBackendHandler
{
    Q_OBJECT

public:
    explicit QtMultimediaBackendHandler(QObject *parent = nullptr);
    ~QtMultimediaBackendHandler() override;

    // Backend identification
    MultimediaBackendType getBackendType() const override;
    QString getBackendName() const override;
    MultimediaBackendConfig getDefaultConfig() const override;

    // Camera lifecycle management
    void prepareCameraCreation() override;
    void configureCameraDevice() override;
    void setupCaptureSession(QMediaCaptureSession* session) override;
    void prepareVideoOutputConnection(QMediaCaptureSession* session, QObject* videoOutput) override;
    void finalizeVideoOutputConnection(QMediaCaptureSession* session, QObject* videoOutput) override;
    void startCamera() override;
    void stopCamera() override;
    
    // Format selection
    QCameraFormat selectOptimalFormat(const QList<QCameraFormat>& formats, 
                                    const QSize& resolution, 
                                    int desiredFrameRate,
                                    QVideoFrameFormat::PixelFormat pixelFormat) const override;

    // Video output management (for compatibility)
    void setVideoOutput(QGraphicsVideoItem* videoItem);
    void setVideoOutput(VideoPane* videoPane);

private:
    // Current video output references (for potential future use)
    QGraphicsVideoItem* m_graphicsVideoItem;
    VideoPane* m_videoPane;
};

#endif // QTMULTIMEDIABACKENDHANDLER_H
