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

#ifndef FFMPEG_RECORDER_H
#define FFMPEG_RECORDER_H

#include <QString>
#include <QSize>
#include <QMutex>
#include <memory>
#include <QImage>
#include <QRect>

// Forward declarations for FFmpeg types
extern "C" {
struct AVFormatContext;
struct AVCodecContext;
struct AVStream;
struct SwsContext;
struct AVFrame;
struct AVPacket;
}

// FFmpeg unique_ptr helpers
#include "ffmpegutils.h"

// RecordingConfig definition
struct RecordingConfig {
    QString output_path;
    QString format = "mp4";
    QString video_codec = "libx264";
    int video_bitrate = 2000000;
    int video_quality = 23;
    bool use_hardware_acceleration = false;
};

/**
 * @brief FFmpeg video recorder - handles video recording functionality
 * 
 * This class encapsulates all video recording logic extracted from FFmpegBackendHandler.
 * It manages FFmpeg recording contexts, encoding, and file writing.
 * Follows Google C++ style guide with lowercase_with_underscores_ naming.
 */
class FFmpegRecorder
{
public:
    FFmpegRecorder();
    ~FFmpegRecorder();

    // Recording control
    bool StartRecording(const QString& output_path, const QString& format, int video_bitrate, 
                       const QSize& resolution, int framerate);
    bool StopRecording();
    void PauseRecording();
    void ResumeRecording();
    bool ForceStopRecording();
    
    // Recording state
    bool IsRecording() const;
    bool IsPaused() const;
    QString GetCurrentRecordingPath() const;
    qint64 GetRecordingDuration() const;
    qint64 GetRecordingFileSize() const;
    
    // Frame writing
    bool WriteFrame(const QImage& image);  // Thread-safe overload
    bool ShouldWriteFrame(qint64 current_time_ms);
    
    // Configuration
    void SetRecordingConfig(const RecordingConfig& config);
    RecordingConfig GetRecordingConfig() const;
    
    // Advanced features
    bool SupportsAdvancedRecording() const;
    bool SupportsRecordingStats() const;
    
    // Image capture
    void TakeImage(const QString& file_path, const QImage& image);  // Thread-safe overload
    void TakeAreaImage(const QString& file_path, const QImage& image, const QRect& capture_area);  // Thread-safe overload

private:
    // Initialization and cleanup
    bool InitializeRecording(const QSize& resolution, int framerate);
    void CleanupRecording();
    void FinalizeRecording();
    
    // Encoder configuration
    bool ConfigureEncoder(const QSize& resolution, int framerate);
    
    // Frame writing
    bool WriteFrameToFile(AVFrame* frame);
    
    // Recording contexts
    AVFormatContext* format_context_;
    AVCodecContext* codec_context_;
    AVStream* video_stream_;
    SwsContext* sws_context_;
    
#ifdef HAVE_FFMPEG
    AvFramePtr recording_frame_;
    AvPacketPtr recording_packet_;
#else
    AVFrame* recording_frame_;
    AVPacket* recording_packet_;
#endif
    
    // Recording state
    bool recording_active_;
    bool recording_paused_;
    QString recording_output_path_;
    RecordingConfig recording_config_;
    
    // Timing information
    qint64 recording_start_time_;
    qint64 recording_paused_time_;
    qint64 total_paused_duration_;
    qint64 last_recorded_frame_time_;
    
    // Frame tracking
    int recording_target_framerate_;
    int64_t recording_frame_number_;
    
    // Thread safety
    mutable QMutex mutex_;
};

#endif // FFMPEG_RECORDER_H
