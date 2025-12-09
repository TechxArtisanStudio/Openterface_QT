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

#ifndef FFMPEG_DEVICE_MANAGER_H
#define FFMPEG_DEVICE_MANAGER_H

#include <QString>
#include <QSize>

#ifdef HAVE_FFMPEG
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}
#endif

// Forward declaration
class FFmpegHardwareAccelerator;

/**
 * @brief Manages FFmpeg device/stream operations
 * 
 * This class encapsulates all device and stream management logic including:
 * - Opening and closing video capture devices
 * - Finding and setting up video streams
 * - Configuring decoders (hardware or software)
 * - Device capability detection
 * - Interrupt handling for timeout protection
 */
class FFmpegDeviceManager {
public:
    FFmpegDeviceManager();
    ~FFmpegDeviceManager();

    // Device operations
    bool OpenDevice(const QString& device_path, const QSize& resolution, int framerate,
                   FFmpegHardwareAccelerator* hw_accelerator);
    void CloseDevice();
    bool IsDeviceOpen() const;
    
    // Stream access
    AVFormatContext* GetFormatContext() { return format_context_; }
    AVCodecContext* GetCodecContext() { return codec_context_; }
    int GetVideoStreamIndex() const { return video_stream_index_; }
    
    // Device capability detection
    struct CameraCapability {
        QSize resolution;
        int framerate;
        CameraCapability() : resolution(0, 0), framerate(0) {}
        CameraCapability(const QSize& res, int fps) : resolution(res), framerate(fps) {}
    };
    bool GetMaxCameraCapability(const QString& device_path, CameraCapability& capability);
    
    // Interrupt handling
    void SetInterruptRequested(bool requested);
    static int InterruptCallback(void* ctx);

private:
    bool InitializeInputStream(const QString& device_path, const QSize& resolution, int framerate);
    bool FindVideoStream();
    bool SetupDecoder(FFmpegHardwareAccelerator* hw_accelerator);
    
    AVFormatContext* format_context_;
    AVCodecContext* codec_context_;
    int video_stream_index_;
    
    volatile bool interrupt_requested_;
    qint64 operation_start_time_;
    static constexpr qint64 kOperationTimeoutMs = 5000;
};

#endif // FFMPEG_DEVICE_MANAGER_H
