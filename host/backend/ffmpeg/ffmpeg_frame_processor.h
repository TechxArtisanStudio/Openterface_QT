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

#ifndef FFMPEG_FRAME_PROCESSOR_H
#define FFMPEG_FRAME_PROCESSOR_H

#include <QPixmap>
#include <QImage>
#include <QMutex>
#include <QDateTime>
#include "ffmpegutils.h"

#ifdef HAVE_FFMPEG
extern "C" {
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
}
#endif

#ifdef HAVE_LIBJPEG_TURBO
#include <turbojpeg.h>
#endif

/**
 * @brief Manages FFmpeg frame processing and decoding operations
 * 
 * This class encapsulates all frame processing logic including:
 * - Hardware-accelerated decoding
 * - TurboJPEG fast MJPEG decoding
 * - FFmpeg software decoding
 * - Frame format conversion
 * - Frame dropping for responsiveness
 * - Latest frame storage for image capture
 */
class FFmpegFrameProcessor {
public:
    FFmpegFrameProcessor();
    ~FFmpegFrameProcessor();

    // Frame processing pipeline
    QPixmap ProcessPacket(AVPacket* packet, AVCodecContext* codec_context, 
                         bool is_recording);
    
    // Frame processing pipeline (returns QImage for better thread safety)
    QImage ProcessPacketToImage(AVPacket* packet, AVCodecContext* codec_context, 
                                bool is_recording);
    
    // Latest frame access (thread-safe)
    QImage GetLatestFrame() const;
    
    // Configuration
    void SetFrameDropThreshold(int display_threshold_ms, int recording_threshold_ms);
    void SetScalingQuality(const QString &quality);
    void StopCaptureGracefully();
    void StartCapture();
    void ResetFrameCount();
    
    // Statistics
    int GetFrameCount() const { return frame_count_; }
    int GetDroppedFrames() const { return dropped_frames_; }
    
    // Cleanup
    void Cleanup();

private:
    // Decoding methods
    QPixmap DecodeWithHardware(AVPacket* packet, AVCodecContext* codec_context);
    QPixmap DecodeWithTurboJpeg(const uint8_t* data, int size);
    QPixmap DecodeWithFFmpeg(AVPacket* packet, AVCodecContext* codec_context);
    
    // Frame conversion
    QPixmap ConvertFrameToPixmap(AVFrame* frame);
    QPixmap ConvertRgbFrameDirectly(AVFrame* frame);
    QPixmap ConvertWithScaling(AVFrame* frame);
    
    // Thread-safe frame conversion (returns QImage directly)
    QImage ConvertFrameToImage(AVFrame* frame);
    QImage ConvertRgbFrameDirectlyToImage(AVFrame* frame);
    QImage ConvertWithScalingToImage(AVFrame* frame);
    
    // Frame dropping logic
    bool ShouldDropFrame(bool is_recording);
    
    // Helper methods
    bool IsHardwareDecoder(const AVCodecContext* codec_context) const;
    void UpdateScalingContext(int width, int height, AVPixelFormat format);
    void CleanupScalingContext();
    
    // Frame conversion resources
    SwsContext* sws_context_;
    AvFramePtr temp_frame_;
    AvFramePtr frame_rgb_;
    
    // Scaling context tracking
    int last_width_;
    int last_height_;
    AVPixelFormat last_format_;
    int last_scaling_algorithm_;
    int scaling_algorithm_;
    
    // Frame dropping configuration
    int frame_drop_threshold_display_;
    int frame_drop_threshold_recording_;
    qint64 last_process_time_;
    int dropped_frames_;
    
    // Statistics
    int frame_count_;
    int startup_frames_to_skip_;
    
    // Latest frame storage (thread-safe)
    mutable QMutex mutex_;
    QImage latest_frame_;
    
    // Thread control
    bool stop_requested_;
    
#ifdef HAVE_LIBJPEG_TURBO
    tjhandle turbojpeg_handle_;
#endif
};

#endif // FFMPEG_FRAME_PROCESSOR_H
