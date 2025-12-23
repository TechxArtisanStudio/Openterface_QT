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

#include "ffmpeg_recorder.h"

#include <QDebug>
#include <QLoggingCategory>
#include <QDateTime>
#include <QFileInfo>
#include <QImage>
#include <QThread>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

Q_DECLARE_LOGGING_CATEGORY(log_ffmpeg_backend)

FFmpegRecorder::FFmpegRecorder()
    : format_context_(nullptr),
      codec_context_(nullptr),
      video_stream_(nullptr),
      sws_context_(nullptr),
      recording_frame_(nullptr),
      recording_packet_(nullptr),
      recording_active_(false),
      recording_paused_(false),
      recording_start_time_(0),
      recording_paused_time_(0),
      total_paused_duration_(0),
      last_recorded_frame_time_(0),
      recording_target_framerate_(30),
      recording_frame_number_(0)
{
}

FFmpegRecorder::~FFmpegRecorder()
{
    if (recording_active_) {
        ForceStopRecording();
    }
    CleanupRecording();
}

bool FFmpegRecorder::StartRecording(const QString& output_path, const QString& format, 
                                    int video_bitrate, const QSize& resolution, int framerate)
{
    QMutexLocker locker(&mutex_);
    
    if (recording_active_) {
        qCWarning(log_ffmpeg_backend) << "Recording is already active";
        return false;
    }
    
    // Update recording config
    recording_config_.output_path = output_path;
    recording_config_.format = format;
    recording_config_.video_bitrate = video_bitrate;
    recording_output_path_ = output_path;
    
    qCDebug(log_ffmpeg_backend) << "Starting recording to:" << output_path << "format:" << format;
    
    if (!InitializeRecording(resolution, framerate)) {
        return false;
    }
    
    recording_active_ = true;
    recording_paused_ = false;
    recording_start_time_ = QDateTime::currentMSecsSinceEpoch();
    recording_paused_time_ = 0;
    total_paused_duration_ = 0;
    last_recorded_frame_time_ = 0;
    recording_frame_number_ = 0;
    
    qCInfo(log_ffmpeg_backend) << "Recording started successfully";
    return true;
}

bool FFmpegRecorder::StopRecording()
{
    qCDebug(log_ffmpeg_backend) << "Stopping recording";
    
    // First, mark recording as inactive to prevent new frames from being processed
    {
        QMutexLocker locker(&mutex_);
        if (!recording_active_) {
            qCDebug(log_ffmpeg_backend) << "Recording is not active";
            return false;
        }
        recording_active_ = false;
        recording_paused_ = false;
    }
    
    // Small delay to ensure no frames are being processed
    QThread::msleep(10);
    
    {
        QMutexLocker locker(&mutex_);
        FinalizeRecording();
        CleanupRecording();
    }
    
    qCInfo(log_ffmpeg_backend) << "Recording stopped successfully";
    return true;
}

void FFmpegRecorder::PauseRecording()
{
    QMutexLocker locker(&mutex_);
    
    if (!recording_active_ || recording_paused_) {
        qCDebug(log_ffmpeg_backend) << "Recording is not active or already paused";
        return;
    }
    
    recording_paused_ = true;
    recording_paused_time_ = QDateTime::currentMSecsSinceEpoch();
    
    qCDebug(log_ffmpeg_backend) << "Recording paused";
}

void FFmpegRecorder::ResumeRecording()
{
    QMutexLocker locker(&mutex_);
    
    if (!recording_active_ || !recording_paused_) {
        qCDebug(log_ffmpeg_backend) << "Recording is not active or not paused";
        return;
    }
    
    if (recording_paused_time_ > 0) {
        total_paused_duration_ += QDateTime::currentMSecsSinceEpoch() - recording_paused_time_;
    }
    
    recording_paused_ = false;
    recording_paused_time_ = 0;
    
    qCDebug(log_ffmpeg_backend) << "Recording resumed";
}

bool FFmpegRecorder::ForceStopRecording()
{
    qCDebug(log_ffmpeg_backend) << "Force stopping recording";
    recording_active_ = false;
    recording_paused_ = false;
    CleanupRecording();
    return true;
}

bool FFmpegRecorder::IsRecording() const
{
    QMutexLocker locker(&mutex_);
    return recording_active_;
}

bool FFmpegRecorder::IsPaused() const
{
    QMutexLocker locker(&mutex_);
    return recording_active_ && recording_paused_;
}

QString FFmpegRecorder::GetCurrentRecordingPath() const
{
    QMutexLocker locker(&mutex_);
    return recording_output_path_;
}

qint64 FFmpegRecorder::GetRecordingDuration() const
{
    QMutexLocker locker(&mutex_);
    
    if (!recording_active_) {
        return 0;
    }
    
    qint64 current_time = QDateTime::currentMSecsSinceEpoch();
    qint64 total_duration = current_time - recording_start_time_ - total_paused_duration_;
    
    if (recording_paused_ && recording_paused_time_ > 0) {
        total_duration -= (current_time - recording_paused_time_);
    }
    
    return qMax(0LL, total_duration);
}

qint64 FFmpegRecorder::GetRecordingFileSize() const
{
    if (recording_output_path_.isEmpty() || !recording_active_) {
        return 0;
    }
    
    QFileInfo file_info(recording_output_path_);
    return file_info.size();
}

bool FFmpegRecorder::WriteFrame(const QImage& image)
{
    // Quick check without lock
    if (!recording_active_ || recording_paused_ || image.isNull()) {
        return false;
    }
    
    // Convert image to RGB888 format if needed
    QImage sourceImage = image;
    if (image.format() != QImage::Format_RGB888) {
        sourceImage = image.convertToFormat(QImage::Format_RGB888);
        if (sourceImage.isNull()) {
            return false;
        }
    }
    
    // Debug logging for recording frame processing
    static int recording_debug_count = 0;
    qint64 current_time = QDateTime::currentMSecsSinceEpoch();
    if (++recording_debug_count <= 10 || recording_debug_count % 30 == 0) {
        QMutexLocker locker(&mutex_);
        qCDebug(log_ffmpeg_backend) << "Writing recording frame" << recording_debug_count 
                                   << "- image size:" << sourceImage.size()
                                   << "recording frame size:" << AV_FRAME_RAW(recording_frame_)->width << "x" << AV_FRAME_RAW(recording_frame_)->height
                                   << "frame interval:" << (current_time - last_recorded_frame_time_) << "ms";
    }
    
    bool can_write_frame = false;
    {
        QMutexLocker locker(&mutex_);
        can_write_frame = recording_active_ && !recording_paused_ && recording_frame_ && sws_context_;
    }
    
    if (!can_write_frame) {
        return false;
    }
    
    // Fill frame with image data
    const uint8_t* src_data[1] = { sourceImage.constBits() };
    int src_linesize[1] = { static_cast<int>(sourceImage.bytesPerLine()) };
    
    // Convert RGB to target pixel format for encoding
    int scale_result = sws_scale(sws_context_, src_data, src_linesize, 0, sourceImage.height(),
                              AV_FRAME_RAW(recording_frame_)->data, AV_FRAME_RAW(recording_frame_)->linesize);
    
    if (scale_result != sourceImage.height()) {
        qCWarning(log_ffmpeg_backend) << "sws_scale conversion warning: converted" << scale_result << "lines, expected" << sourceImage.height();
    }
    
    // Write frame to file
    return WriteFrameToFile(AV_FRAME_RAW(recording_frame_));
}

bool FFmpegRecorder::WriteFrame(const QPixmap& pixmap)
{
    // Deprecated: Convert to QImage and use primary implementation
    if (pixmap.isNull()) {
        return false;
    }
    QImage image = pixmap.toImage();
    return WriteFrame(image);
}

bool FFmpegRecorder::ShouldWriteFrame(qint64 current_time_ms)
{
    QMutexLocker locker(&mutex_);
    
    if (!recording_active_ || recording_paused_) {
        return false;
    }
    
    // Calculate expected frame number based on actual elapsed recording time
    qint64 elapsed_ms = current_time_ms - recording_start_time_ - total_paused_duration_;
    int64_t expected_frame_number = (elapsed_ms * recording_target_framerate_) / 1000;
    
    // Write frame if we're behind or at the expected frame count
    if (recording_frame_number_ <= expected_frame_number) {
        last_recorded_frame_time_ = current_time_ms;
        return true;
    }
    
    // We're ahead of schedule - skip this frame to maintain timing
    static int skip_log_count = 0;
    if (++skip_log_count % 100 == 0) {
        qCDebug(log_ffmpeg_backend) << "Skipping frame - ahead of schedule:" 
                                   << "recorded:" << recording_frame_number_ 
                                   << "expected:" << expected_frame_number;
    }
    return false;
}

void FFmpegRecorder::SetRecordingConfig(const RecordingConfig& config)
{
    QMutexLocker locker(&mutex_);
    recording_config_ = config;
}

RecordingConfig FFmpegRecorder::GetRecordingConfig() const
{
    QMutexLocker locker(&mutex_);
    return recording_config_;
}

bool FFmpegRecorder::SupportsAdvancedRecording() const
{
    return true;
}

bool FFmpegRecorder::SupportsRecordingStats() const
{
    return true;
}

void FFmpegRecorder::TakeImage(const QString& file_path, const QImage& image)
{
    if (image.isNull()) {
        qCWarning(log_ffmpeg_backend) << "No frame available for image capture";
        return;
    }
    
    if (image.save(file_path)) {
        qCDebug(log_ffmpeg_backend) << "Image saved to:" << file_path;
    } else {
        qCWarning(log_ffmpeg_backend) << "Failed to save image to:" << file_path;
    }
}

void FFmpegRecorder::TakeAreaImage(const QString& file_path, const QImage& image, const QRect& capture_area)
{
    if (image.isNull()) {
        qCWarning(log_ffmpeg_backend) << "No frame available for area image capture";
        return;
    }
    
    QImage cropped = image.copy(capture_area);
    if (cropped.save(file_path)) {
        qCDebug(log_ffmpeg_backend) << "Cropped image saved to:" << file_path;
    } else {
        qCWarning(log_ffmpeg_backend) << "Failed to save cropped image to:" << file_path;
    }
}

void FFmpegRecorder::TakeAreaImage(const QString& file_path, const QPixmap& pixmap, const QRect& capture_area)
{
    // Deprecated: Convert to QImage and use primary implementation
    if (pixmap.isNull()) {
        qCWarning(log_ffmpeg_backend) << "No frame available for area image capture";
        return;
    }
    TakeAreaImage(file_path, pixmap.toImage(), capture_area);
}

bool FFmpegRecorder::InitializeRecording(const QSize& resolution, int framerate)
{
    // Clean up any existing recording context
    CleanupRecording();
    
    // Allocate output format context
    const char* format_name = nullptr;
    if (recording_config_.format == "avi") {
        format_name = "avi";
    } else if (recording_config_.format == "rawvideo") {
        format_name = "rawvideo";
    } else if (recording_config_.format == "mjpeg") {
        format_name = "mjpeg";
    } else {
        format_name = nullptr; // Auto-detect from filename
    }
    
    // Try to allocate output context
    int ret = avformat_alloc_output_context2(&format_context_, nullptr, format_name, 
                                             recording_config_.output_path.toUtf8().data());
    if (ret < 0 || !format_context_) {
        // Try auto-detection from filename if format name failed
        qCWarning(log_ffmpeg_backend) << "Failed with format" << format_name << ", trying auto-detection from filename";
        ret = avformat_alloc_output_context2(&format_context_, nullptr, nullptr, 
                                             recording_config_.output_path.toUtf8().data());
        if (ret < 0 || !format_context_) {
            qCWarning(log_ffmpeg_backend) << "Failed to allocate output context for recording";
            return false;
        }
    }
    
    qCDebug(log_ffmpeg_backend) << "Configuring encoder with resolution:" << resolution << "framerate:" << framerate;
    
    if (!ConfigureEncoder(resolution, framerate)) {
        return false;
    }
    
    // Open output file
    if (!(format_context_->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&format_context_->pb, recording_config_.output_path.toUtf8().data(), AVIO_FLAG_WRITE);
        if (ret < 0) {
            char errbuf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
            qCWarning(log_ffmpeg_backend) << "Failed to open output file:" << QString::fromUtf8(errbuf);
            return false;
        }
    }
    
    // Write file header
    ret = avformat_write_header(format_context_, nullptr);
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
        qCWarning(log_ffmpeg_backend) << "Failed to write header:" << QString::fromUtf8(errbuf);
        return false;
    }
    
    qCDebug(log_ffmpeg_backend) << "Recording initialized successfully";
    return true;
}

bool FFmpegRecorder::ConfigureEncoder(const QSize& resolution, int framerate)
{
    // Helper lambda to try finding an encoder
    auto tryEncoder = [](AVCodecID codec_id, const char* name) -> const AVCodec* {
        const AVCodec* codec = avcodec_find_encoder(codec_id);
        if (codec) {
            qCDebug(log_ffmpeg_backend) << "Using" << name << "encoder";
        }
        return codec;
    };
    
    // Find encoder - try requested codec first
    const AVCodec* codec = nullptr;
    if (!recording_config_.video_codec.isEmpty()) {
        codec = avcodec_find_encoder_by_name(recording_config_.video_codec.toUtf8().data());
        if (codec) {
            qCDebug(log_ffmpeg_backend) << "Found requested encoder:" << recording_config_.video_codec;
        } else {
            qCWarning(log_ffmpeg_backend) << "Requested encoder not found:" << recording_config_.video_codec << "- trying fallbacks";
        }
    }
    
    // Fallback chain for available encoders
    if (!codec) codec = tryEncoder(AV_CODEC_ID_MJPEG, "MJPEG");
    if (!codec) codec = tryEncoder(AV_CODEC_ID_RAWVIDEO, "rawvideo");
    if (!codec) codec = tryEncoder(AV_CODEC_ID_H264, "H264");
    
    if (!codec) {
        qCWarning(log_ffmpeg_backend) << "Failed to find any video encoder (tried mjpeg, rawvideo, h264)";
        return false;
    }
    
    // Add video stream
    video_stream_ = avformat_new_stream(format_context_, codec);
    if (!video_stream_) {
        qCWarning(log_ffmpeg_backend) << "Failed to create video stream";
        return false;
    }
    
    // Allocate codec context
    codec_context_ = avcodec_alloc_context3(codec);
    if (!codec_context_) {
        qCWarning(log_ffmpeg_backend) << "Failed to allocate codec context";
        return false;
    }
    
    // Set codec parameters
    codec_context_->width = resolution.width();
    codec_context_->height = resolution.height();
    codec_context_->time_base = {1, framerate};
    codec_context_->framerate = {framerate, 1};
    
    // Cache target framerate for thread-safe access
    recording_target_framerate_ = framerate;
    
    // Set pixel format based on codec
    if (codec->id == AV_CODEC_ID_MJPEG) {
        codec_context_->pix_fmt = AV_PIX_FMT_YUVJ420P;
        codec_context_->bit_rate = recording_config_.video_bitrate;
        codec_context_->qmin = 1;
        codec_context_->qmax = 10; // Good quality
    } else if (codec->id == AV_CODEC_ID_RAWVIDEO) {
        codec_context_->pix_fmt = AV_PIX_FMT_RGB24;
    } else {
        // H264 or other codecs
        codec_context_->pix_fmt = AV_PIX_FMT_YUV420P;
        codec_context_->bit_rate = recording_config_.video_bitrate;
    }
    
    // Set codec-specific options
    if (codec->id == AV_CODEC_ID_H264) {
        av_opt_set_int(codec_context_->priv_data, "crf", recording_config_.video_quality, 0);
        av_opt_set(codec_context_->priv_data, "preset", "ultrafast", 0);
        av_opt_set(codec_context_->priv_data, "tune", "zerolatency", 0);
        
        codec_context_->rc_max_rate = recording_config_.video_bitrate;
        codec_context_->rc_buffer_size = recording_config_.video_bitrate * 2;
    } else if (codec->id == AV_CODEC_ID_MJPEG) {
        av_opt_set_int(codec_context_->priv_data, "q:v", recording_config_.video_quality, 0);
    }
    
    // Global header flag for MP4
    if (format_context_->oformat->flags & AVFMT_GLOBALHEADER) {
        codec_context_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }
    
    // Open codec
    int ret = avcodec_open2(codec_context_, codec, nullptr);
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
        qCWarning(log_ffmpeg_backend) << "Failed to open codec:" << QString::fromUtf8(errbuf);
        return false;
    }
    
    // Copy codec parameters to stream
    ret = avcodec_parameters_from_context(video_stream_->codecpar, codec_context_);
    if (ret < 0) {
        qCWarning(log_ffmpeg_backend) << "Failed to copy codec parameters";
        return false;
    }
    
    // Set video stream time base
    video_stream_->time_base = codec_context_->time_base;
    qCDebug(log_ffmpeg_backend) << "Set video stream time base to" << video_stream_->time_base.num << "/" << video_stream_->time_base.den;
    
    // Allocate frame for encoding
    recording_frame_ = make_av_frame();
    if (!recording_frame_) {
        qCWarning(log_ffmpeg_backend) << "Failed to allocate recording frame";
        return false;
    }
    
    AV_FRAME_RAW(recording_frame_)->format = codec_context_->pix_fmt;
    AV_FRAME_RAW(recording_frame_)->width = codec_context_->width;
    AV_FRAME_RAW(recording_frame_)->height = codec_context_->height;
    
    ret = av_frame_get_buffer(AV_FRAME_RAW(recording_frame_), 0);
    if (ret < 0) {
        qCWarning(log_ffmpeg_backend) << "Failed to allocate frame buffer";
        return false;
    }
    
    // Allocate packet
    recording_packet_ = make_av_packet();
    if (!recording_packet_) {
        qCWarning(log_ffmpeg_backend) << "Failed to allocate recording packet";
        return false;
    }
    
    // Initialize scaling context for color space conversion
    AVPixelFormat output_format = static_cast<AVPixelFormat>(codec_context_->pix_fmt);
    sws_context_ = sws_getContext(
        resolution.width(), resolution.height(), AV_PIX_FMT_RGB24,
        resolution.width(), resolution.height(), output_format,
        SWS_BICUBIC, nullptr, nullptr, nullptr);
    
    if (!sws_context_) {
        qCWarning(log_ffmpeg_backend) << "Failed to initialize scaling context for recording (output format:" << output_format << ")";
        return false;
    }
    
    qCDebug(log_ffmpeg_backend) << "Encoder configured successfully" 
                               << "Resolution:" << resolution
                               << "Framerate:" << framerate
                               << "Bitrate:" << recording_config_.video_bitrate;
    
    return true;
}

bool FFmpegRecorder::WriteFrameToFile(AVFrame* frame)
{
    QMutexLocker locker(&mutex_);
    
    // Quick check recording state
    if (!recording_active_ || recording_paused_ || !codec_context_ || !frame 
        || !format_context_ || !video_stream_ || !recording_packet_) {
        return false;
    }
    
    // Calculate PTS based on actual elapsed time for proper A/V synchronization
    qint64 current_time = QDateTime::currentMSecsSinceEpoch();
    qint64 elapsed_ms = current_time - recording_start_time_ - total_paused_duration_;
    
    // Convert elapsed milliseconds to PTS using the codec's time base
    int64_t pts = av_rescale_q(elapsed_ms, AVRational{1, 1000}, codec_context_->time_base);
    frame->pts = pts;
    
    // Debug logging for first few frames
    static int debug_frame_count = 0;
    if (++debug_frame_count <= 5 || debug_frame_count % 100 == 0) {
        qCDebug(log_ffmpeg_backend) << "Writing frame" << recording_frame_number_ 
                                   << "with PTS" << pts 
                                   << "(elapsed:" << elapsed_ms << "ms)"
                                   << "time_base" << codec_context_->time_base.num << "/" << codec_context_->time_base.den;
    }
    
    recording_frame_number_++;
    
    // Send frame to encoder
    int ret = avcodec_send_frame(codec_context_, frame);
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
        qCWarning(log_ffmpeg_backend) << "Error sending frame to encoder:" << QString::fromUtf8(errbuf);
        return false;
    }
    
    // Receive encoded packets
    while (ret >= 0) {
        ret = avcodec_receive_packet(codec_context_, AV_PACKET_RAW(recording_packet_));
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        } else if (ret < 0) {
            char errbuf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
            qCWarning(log_ffmpeg_backend) << "Error receiving packet from encoder:" << QString::fromUtf8(errbuf);
            return false;
        }
        
        // Check if we're still recording
        if (!recording_active_ || !format_context_) {
            qCDebug(log_ffmpeg_backend) << "Recording stopped during packet processing, discarding packet";
            av_packet_unref(AV_PACKET_RAW(recording_packet_));
            return false;
        }
        
        // Scale packet timestamp
        av_packet_rescale_ts(AV_PACKET_RAW(recording_packet_), codec_context_->time_base, video_stream_->time_base);
        AV_PACKET_RAW(recording_packet_)->stream_index = video_stream_->index;
        
        // Write packet to output file
        ret = av_interleaved_write_frame(format_context_, AV_PACKET_RAW(recording_packet_));
        if (ret < 0) {
            char errbuf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
            qCWarning(log_ffmpeg_backend) << "Error writing frame to file:" << QString::fromUtf8(errbuf);
            av_packet_unref(AV_PACKET_RAW(recording_packet_));
            return false;
        }
        
        av_packet_unref(AV_PACKET_RAW(recording_packet_));
    }
    
    return true;
}

void FFmpegRecorder::FinalizeRecording()
{
    if (!format_context_ || !codec_context_) {
        qCDebug(log_ffmpeg_backend) << "Recording context already cleaned up, skipping finalization";
        return;
    }
    
    qCDebug(log_ffmpeg_backend) << "Finalizing recording...";
    
    // Flush encoder - send NULL frame to signal end of input
    if (codec_context_) {
        int ret = avcodec_send_frame(codec_context_, nullptr);
        if (ret < 0 && ret != AVERROR_EOF) {
            char errbuf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
            qCWarning(log_ffmpeg_backend) << "Error flushing encoder:" << QString::fromUtf8(errbuf);
        } else {
            // Receive remaining packets from encoder
            while (ret >= 0) {
                ret = avcodec_receive_packet(codec_context_, AV_PACKET_RAW(recording_packet_));
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    break;
                } else if (ret < 0) {
                    char errbuf[AV_ERROR_MAX_STRING_SIZE];
                    av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
                    qCWarning(log_ffmpeg_backend) << "Error receiving final packets:" << QString::fromUtf8(errbuf);
                    break;
                }
                
                // Scale packet timestamp and write to file
                if (video_stream_ && format_context_) {
                    av_packet_rescale_ts(AV_PACKET_RAW(recording_packet_), codec_context_->time_base, video_stream_->time_base);
                    AV_PACKET_RAW(recording_packet_)->stream_index = video_stream_->index;
                    av_interleaved_write_frame(format_context_, AV_PACKET_RAW(recording_packet_));
                }
                av_packet_unref(AV_PACKET_RAW(recording_packet_));
            }
        }
    }
    
    // Write trailer
    if (format_context_) {
        int ret = av_write_trailer(format_context_);
        if (ret < 0) {
            char errbuf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
            qCWarning(log_ffmpeg_backend) << "Error writing trailer:" << QString::fromUtf8(errbuf);
        }
    }
    
    qCDebug(log_ffmpeg_backend) << "Recording finalized, total frames:" << recording_frame_number_;
}

void FFmpegRecorder::CleanupRecording()
{
    if (sws_context_) {
        sws_freeContext(sws_context_);
        sws_context_ = nullptr;
    }
    
    if (recording_frame_) {
        AV_FRAME_RESET(recording_frame_);
    }
    
    if (recording_packet_) {
        AV_PACKET_RESET(recording_packet_);
    }
    
    if (codec_context_) {
        avcodec_free_context(&codec_context_);
    }
    
    if (format_context_) {
        if (!(format_context_->oformat->flags & AVFMT_NOFILE)) {
            avio_closep(&format_context_->pb);
        }
        avformat_free_context(format_context_);
        format_context_ = nullptr;
    }
    
    video_stream_ = nullptr;
    
    qCDebug(log_ffmpeg_backend) << "Recording cleanup completed";
}
