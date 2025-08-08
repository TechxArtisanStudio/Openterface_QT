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

#ifndef FFMPEGBACKENDHANDLER_H
#define FFMPEGBACKENDHANDLER_H

#include "../multimediabackend.h"

/**
 * @brief FFmpeg backend handler implementation
 */
class FFmpegBackendHandler : public MultimediaBackendHandler
{
    Q_OBJECT

public:
    explicit FFmpegBackendHandler(QObject *parent = nullptr);

    MultimediaBackendType getBackendType() const override;
    QString getBackendName() const override;
    MultimediaBackendConfig getDefaultConfig() const override;

    void startCamera(QCamera* camera) override;
    void stopCamera(QCamera* camera) override;
    
    QCameraFormat selectOptimalFormat(const QList<QCameraFormat>& formats, 
                                    const QSize& resolution, 
                                    int desiredFrameRate,
                                    QVideoFrameFormat::PixelFormat pixelFormat) const override;
};

#endif // FFMPEGBACKENDHANDLER_H
