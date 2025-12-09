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

#ifndef FFMPEG_DEVICE_VALIDATOR_H
#define FFMPEG_DEVICE_VALIDATOR_H

#include <QString>
#include <QSize>

#ifdef HAVE_FFMPEG
extern "C" {
#include <libavformat/avformat.h>
#include <libavdevice/avdevice.h>
}
#endif

/**
 * @brief Handles device availability checking and capability detection for FFmpeg backend
 * 
 * This class encapsulates device validation logic including:
 * - OS-specific device path validation
 * - FFmpeg compatibility checking
 * - Device capability detection (resolution, framerate)
 * - Integration with GlobalSetting for stored preferences
 */
class FFmpegDeviceValidator {
public:
    struct CameraCapability {
        QSize resolution;
        int framerate;
        CameraCapability() : resolution(0, 0), framerate(0) {}
        CameraCapability(const QSize& res, int fps) : resolution(res), framerate(fps) {}
    };

    FFmpegDeviceValidator();
    ~FFmpegDeviceValidator();

    /**
     * @brief Check if a camera device is available and accessible
     * @param devicePath The device path to check (e.g., "/dev/video0" or "video=Openterface")
     * @param currentDevice The currently active device path (to avoid intrusive checks on active device)
     * @param captureRunning Whether capture is currently active
     * @param waitingForDevice Whether we're waiting for device activation (skip intrusive checks)
     * @return true if device is available, false otherwise
     */
    bool CheckCameraAvailable(const QString& devicePath, 
                             const QString& currentDevice = QString(),
                             bool captureRunning = false,
                             bool waitingForDevice = false);

    /**
     * @brief Get maximum camera capability from GlobalSetting
     * @param devicePath The device path (for logging purposes)
     * @param capability Output parameter for the detected capability
     * @return true if capability was successfully retrieved
     */
    bool GetMaxCameraCapability(const QString& devicePath, CameraCapability& capability);

private:
    bool CheckOSSpecificDeviceAccess(const QString& devicePath, 
                                    const QString& currentDevice,
                                    bool captureRunning);
    bool CheckFFmpegCompatibility(const QString& devicePath);
    const AVInputFormat* GetInputFormat();
};

#endif // FFMPEG_DEVICE_VALIDATOR_H
