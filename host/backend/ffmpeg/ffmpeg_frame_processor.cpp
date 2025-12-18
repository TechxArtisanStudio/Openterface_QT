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

#include "ffmpeg_frame_processor.h"
#include <QLoggingCategory>
#include <QDebug>
#include <cstring>
#include <vector>
#include <algorithm>

extern "C" {
#include <libavutil/imgutils.h>
#include <libavutil/pixdesc.h>
}

Q_DECLARE_LOGGING_CATEGORY(log_ffmpeg_backend)

FFmpegFrameProcessor::FFmpegFrameProcessor()
    : sws_context_(nullptr)
    , last_width_(-1)
    , last_height_(-1)
    , last_format_(AV_PIX_FMT_NONE)
    , last_scaling_algorithm_(-1)
    , scaling_algorithm_(SWS_BILINEAR)
    , frame_drop_threshold_display_(16)    // Restore: ~60fps capable (drop if >16ms processing)
    , frame_drop_threshold_recording_(33)  // Restore: ~30fps capable (drop if >33ms processing)
    , last_process_time_(0)
    , dropped_frames_(0)
    , frame_count_(0)
    , startup_frames_to_skip_(0)           // Don't skip startup frames for MJPEG
    , stop_requested_(false)
#ifdef HAVE_LIBJPEG_TURBO
    , turbojpeg_handle_(nullptr)
#endif
{
#ifdef HAVE_LIBJPEG_TURBO
    // Initialize TurboJPEG decompressor
    turbojpeg_handle_ = tjInitDecompress();
    if (!turbojpeg_handle_) {
        qCWarning(log_ffmpeg_backend) << "Failed to initialize TurboJPEG decompressor";
    } else {
        qCDebug(log_ffmpeg_backend) << "TurboJPEG decompressor initialized successfully";
    }
#endif

    // Initialize startup frame skip from environment variable
    if (startup_frames_to_skip_ == -1) {
        QByteArray skipFramesEnv = qgetenv("OPENTERFACE_SKIP_STARTUP_FRAMES");
        if (!skipFramesEnv.isEmpty()) {
            bool ok = false;
            int skipCount = skipFramesEnv.toInt(&ok);
            if (ok && skipCount >= 0) {
                startup_frames_to_skip_ = skipCount;
                qCDebug(log_ffmpeg_backend) << "Startup frames to skip set to" << startup_frames_to_skip_ 
                                           << "from environment variable";
            }
        }
        
        // Default to 0 if not set (hardware acceleration doesn't need this)
        if (startup_frames_to_skip_ == -1) {
            startup_frames_to_skip_ = 0;
        }
    }
}

FFmpegFrameProcessor::~FFmpegFrameProcessor()
{
    Cleanup();
}

void FFmpegFrameProcessor::Cleanup()
{
    QMutexLocker locker(&mutex_);
    CleanupScalingContext();
    
#ifdef HAVE_LIBJPEG_TURBO
    if (turbojpeg_handle_) {
        tjDestroy(turbojpeg_handle_);
        turbojpeg_handle_ = nullptr;
    }
#endif
    
    if (temp_frame_) {
        AV_FRAME_RESET(temp_frame_);
    }
    
    if (frame_rgb_) {
        AV_FRAME_RESET(frame_rgb_);
    }
}

void FFmpegFrameProcessor::StopCaptureGracefully()
{
    stop_requested_ = true;
}

void FFmpegFrameProcessor::StartCapture()
{
    stop_requested_ = false;
}

void FFmpegFrameProcessor::SetFrameDropThreshold(int display_threshold_ms, int recording_threshold_ms)
{
    frame_drop_threshold_display_ = display_threshold_ms;
    frame_drop_threshold_recording_ = recording_threshold_ms;
}

void FFmpegFrameProcessor::SetScalingQuality(const QString &quality)
{
    int new_algorithm;
    if (quality == "fast") {
        new_algorithm = SWS_FAST_BILINEAR;
    } else if (quality == "balanced") {
        new_algorithm = SWS_SPLINE;  // Better quality than bilinear
    } else if (quality == "quality") {
        new_algorithm = SWS_LANCZOS;  // High quality
    } else if (quality == "best") {
        new_algorithm = SWS_LANCZOS;  // Best quality
    } else {
        new_algorithm = SWS_LANCZOS; // Default to high quality
    }
    
    // Thread-safe update of scaling algorithm
    {
        QMutexLocker locker(&mutex_);
        if (scaling_algorithm_ != new_algorithm) {
            scaling_algorithm_ = new_algorithm;
            // Don't cleanup scaling context immediately - let it be recreated lazily
            // when UpdateScalingContext is called next time. This prevents crashes
            // when changing settings while capture is running.
            last_width_ = -1;  // Force recreation on next use
            last_height_ = -1;
            last_format_ = AV_PIX_FMT_NONE;
            last_scaling_algorithm_ = -1;
            qCDebug(log_ffmpeg_backend) << "Scaling quality changed to:" << quality 
                                       << "(algorithm:" << scaling_algorithm_ << ")";
        }
    }
}

void FFmpegFrameProcessor::ResetFrameCount()
{
    frame_count_ = 0;
    dropped_frames_ = 0;
    last_process_time_ = 0;
}

QImage FFmpegFrameProcessor::GetLatestFrame() const
{
    QMutexLocker locker(&mutex_);
    return latest_frame_;
}

bool FFmpegFrameProcessor::ShouldDropFrame(bool is_recording)
{
    qint64 current_time = QDateTime::currentMSecsSinceEpoch();
    
    // Choose threshold based on whether we're recording
    int threshold = is_recording ? frame_drop_threshold_recording_ : frame_drop_threshold_display_;
    
    if (current_time - last_process_time_ < threshold) {
        dropped_frames_++;
        return true;
    }
    
    last_process_time_ = current_time;
    
    // Log dropped frames occasionally
    if (dropped_frames_ > 0 && frame_count_ % 1000 == 0) {
        qCDebug(log_ffmpeg_backend) << "Dropped" << dropped_frames_ 
                                   << "frames for responsiveness (last 1000 frames)";
        dropped_frames_ = 0;
    }
    
    return false;
}

bool FFmpegFrameProcessor::IsHardwareDecoder(const AVCodecContext* codec_context) const
{
    if (!codec_context || !codec_context->codec) {
        return false;
    }
    
    const char* codec_name = codec_context->codec->name;
    return (strstr(codec_name, "_cuvid") != nullptr || 
            strstr(codec_name, "_qsv") != nullptr ||
            strstr(codec_name, "_nvdec") != nullptr);
}

QPixmap FFmpegFrameProcessor::ProcessPacket(AVPacket* packet, AVCodecContext* codec_context, 
                                            bool is_recording)
{
    if (stop_requested_) {
        return QPixmap();
    }
    
    if (!packet || !codec_context) {
        return QPixmap();
    }
    
    // Validate packet data
    if (packet->size <= 0 || !packet->data) {
        if (packet->size > 0 && !packet->data) {
            qCWarning(log_ffmpeg_backend) << "Packet has size but no data pointer";
        }
        return QPixmap();
    }
    
    // Frame dropping for responsiveness
    if (ShouldDropFrame(is_recording)) {
        return QPixmap();
    }
    
    QPixmap pixmap;
    
    // Determine decoder type
    bool using_hardware_decoder = IsHardwareDecoder(codec_context);
    
    // PRIORITY ORDER for MJPEG decoding:
    // 1. Hardware decoder (CUVID/NVDEC, QSV)
    // 2. TurboJPEG
    // 3. FFmpeg software decoder
    
    if (codec_context->codec_id == AV_CODEC_ID_MJPEG) {
        if (using_hardware_decoder) {
            static int hw_decode_count = 0;
            hw_decode_count++;
            
            if (hw_decode_count % 1000 == 1) {
                qCDebug(log_ffmpeg_backend) << "Using hardware MJPEG decoder:" 
                                           << codec_context->codec->name 
                                           << "(frame" << hw_decode_count << ")";
            }
            
            pixmap = DecodeWithHardware(packet, codec_context);
            
            if (pixmap.isNull()) {
                qCWarning(log_ffmpeg_backend) << "Hardware decoder failed, falling back to software";
                pixmap = DecodeWithFFmpeg(packet, codec_context);
            }
        } else {
#ifdef HAVE_LIBJPEG_TURBO
            static int turbojpeg_count = 0;
            turbojpeg_count++;
            
            if (turbojpeg_count % 2000 == 1) {
                qCDebug(log_ffmpeg_backend) << "Using TurboJPEG for MJPEG decoding (frame" 
                                           << turbojpeg_count << ")";
            }
            
            pixmap = DecodeWithTurboJpeg(packet->data, packet->size);
            
            if (pixmap.isNull()) {
                static int turbojpeg_fallback_count = 0;
                if (++turbojpeg_fallback_count <= 5) {
                    qCDebug(log_ffmpeg_backend) << "TurboJPEG failed, falling back to FFmpeg decoder";
                }
                pixmap = DecodeWithFFmpeg(packet, codec_context);
            }
#else
            pixmap = DecodeWithFFmpeg(packet, codec_context);
#endif
        }
    } else {
        // Non-MJPEG codecs use FFmpeg decoder
        static int ffmpeg_frame_count = 0;
        ffmpeg_frame_count++;
        
        if (ffmpeg_frame_count % 1000 == 1) {
            qCDebug(log_ffmpeg_backend) << "Using FFmpeg decoder for codec" 
                                       << codec_context->codec_id;
        }
        pixmap = DecodeWithFFmpeg(packet, codec_context);
    }
    
    if (!pixmap.isNull()) {
        frame_count_++;
        
        if (frame_count_ % 1000 == 1) {
            qCDebug(log_ffmpeg_backend) << "Processed frame" << frame_count_;
        }
        
        // Skip startup frames if configured
        if (frame_count_ <= startup_frames_to_skip_) {
            qCDebug(log_ffmpeg_backend) << "Skipping startup frame" << frame_count_ 
                                       << "/" << startup_frames_to_skip_;
            return QPixmap();
        }
        
        // Store latest frame for image capture
        QImage img = pixmap.toImage();
        {
            QMutexLocker locker(&mutex_);
            latest_frame_ = img;
        }
    }
    
    return pixmap;
}

QImage FFmpegFrameProcessor::ProcessPacketToImage(AVPacket* packet, AVCodecContext* codec_context, 
                                                   bool is_recording, const QSize& targetSize)
{
    // OPTIMIZATION: Decode directly to QImage to avoid QPixmap creation on worker thread
    if (stop_requested_) {
        return QImage();
    }
    
    if (!packet || !codec_context) {
        return QImage();
    }
    
    // Validate packet data
    if (packet->size <= 0 || !packet->data) {
        return QImage();
    }
    
    // Frame dropping for responsiveness
    if (ShouldDropFrame(is_recording)) {
        return QImage();
    }
    
    // Decode packet to frame (reuse existing decode logic)
    if (!temp_frame_) {
        temp_frame_ = make_av_frame();
        if (!temp_frame_) {
            return QImage();
        }
    }
    
    // Send packet to decoder
    int ret = avcodec_send_packet(codec_context, packet);
    if (ret < 0) {
        return QImage();
    }
    
    // Receive frame from decoder
    ret = avcodec_receive_frame(codec_context, AV_FRAME_RAW(temp_frame_));
    if (ret < 0) {
        return QImage();
    }
    
    // Validate frame data
    if (!AV_FRAME_RAW(temp_frame_)->data[0] || 
        AV_FRAME_RAW(temp_frame_)->width <= 0 || 
        AV_FRAME_RAW(temp_frame_)->height <= 0) {
        return QImage();
    }
    
    // Handle hardware frames
    AVFrame* frame_to_convert = AV_FRAME_RAW(temp_frame_);
    AvFramePtr sw_frame;
    
    bool is_hardware_frame = (AV_FRAME_RAW(temp_frame_)->format == AV_PIX_FMT_QSV ||
                             AV_FRAME_RAW(temp_frame_)->format == AV_PIX_FMT_CUDA);
    
    if (is_hardware_frame) {
        sw_frame = make_av_frame();
        if (!sw_frame) {
            return QImage();
        }
        
        ret = av_hwframe_transfer_data(AV_FRAME_RAW(sw_frame), AV_FRAME_RAW(temp_frame_), 0);
        if (ret < 0) {
            return QImage();
        }
        
        av_frame_copy_props(AV_FRAME_RAW(sw_frame), AV_FRAME_RAW(temp_frame_));
        frame_to_convert = AV_FRAME_RAW(sw_frame);
    }
    
    // Convert frame directly to QImage (bypassing QPixmap)
    QImage result = ConvertFrameToImage(frame_to_convert, targetSize);
    
    if (sw_frame) {
        AV_FRAME_RESET(sw_frame);
    }
    
    // Update statistics and store latest frame
    if (!result.isNull()) {
        frame_count_++;
        
        // Skip startup frames if configured
        if (frame_count_ <= startup_frames_to_skip_) {
            return QImage();
        }
        
        // Store latest frame for image capture
        {
            QMutexLocker locker(&mutex_);
            latest_frame_ = result;
        }
    }
    
    return result;
}

QPixmap FFmpegFrameProcessor::DecodeWithHardware(AVPacket* packet, AVCodecContext* codec_context)
{
    return DecodeWithFFmpeg(packet, codec_context);
}

#ifdef HAVE_LIBJPEG_TURBO
QPixmap FFmpegFrameProcessor::DecodeWithTurboJpeg(const uint8_t* data, int size)
{
    if (!turbojpeg_handle_) {
        return QPixmap();
    }
    
    if (!data || size <= 10) {
        return QPixmap();
    }
    
    // Quick JPEG magic bytes check
    if (data[0] != 0xFF || data[1] != 0xD8) {
        return QPixmap();
    }
    
    int width, height, subsamp, colorspace;
    
    // Get JPEG header information
    if (tjDecompressHeader3(turbojpeg_handle_, data, size, &width, &height, &subsamp, &colorspace) < 0) {
        return QPixmap();
    }
    
    // Dimension validation
    if (width <= 0 || height <= 0 || width > 4096 || height > 4096) {
        return QPixmap();
    }
    
    // Allocate buffer for RGB data
    QImage image(width, height, QImage::Format_RGB888);
    if (image.isNull()) {
        return QPixmap();
    }
    
    // Decompress JPEG to RGB888
    if (tjDecompress2(turbojpeg_handle_, data, size, image.bits(), width, 0, height, 
                     TJPF_RGB, TJFLAG_FASTDCT) < 0) {
        return QPixmap();
    }
    
    static int turbojpeg_success_count = 0;
    turbojpeg_success_count++;
    
    if (turbojpeg_success_count % 2000 == 1) {
        qCDebug(log_ffmpeg_backend) << "TurboJPEG decode successful, frame" 
                                   << turbojpeg_success_count 
                                   << "size:" << width << "x" << height;
    }
    
    return QPixmap::fromImage(image);
}
#else
QPixmap FFmpegFrameProcessor::DecodeWithTurboJpeg(const uint8_t* data, int size)
{
    Q_UNUSED(data);
    Q_UNUSED(size);
    return QPixmap();
}
#endif

QPixmap FFmpegFrameProcessor::DecodeWithFFmpeg(AVPacket* packet, AVCodecContext* codec_context)
{
    if (!codec_context || !packet) {
        qCWarning(log_ffmpeg_backend) << "DecodeWithFFmpeg: Missing codec context or packet";
        return QPixmap();
    }
    
    // Allocate frame if needed
    if (!temp_frame_) {
        temp_frame_ = make_av_frame();
        if (!temp_frame_) {
            qCCritical(log_ffmpeg_backend) << "Failed to allocate temp frame";
            return QPixmap();
        }
    }
    
    // Send packet to decoder
    int ret = avcodec_send_packet(codec_context, packet);
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
        qCWarning(log_ffmpeg_backend) << "Error sending packet to decoder:" 
                                     << QString::fromUtf8(errbuf);
        return QPixmap();
    }
    
    // Receive frame from decoder
    ret = avcodec_receive_frame(codec_context, AV_FRAME_RAW(temp_frame_));
    if (ret < 0) {
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            return QPixmap();
        }
        
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
        
        static int error_count = 0;
        if (++error_count <= 5) {
            const char* codec_name = codec_context->codec ? codec_context->codec->name : "unknown";
            qCWarning(log_ffmpeg_backend) << "Error receiving frame from decoder:" 
                                         << QString::fromUtf8(errbuf)
                                         << "Codec:" << codec_name
                                         << "Error count:" << error_count;
        }
        return QPixmap();
    }
    
    // Validate frame data
    if (!AV_FRAME_RAW(temp_frame_)->data[0]) {
        qCWarning(log_ffmpeg_backend) << "DecodeWithFFmpeg: Frame data is null";
        return QPixmap();
    }
    
    if (AV_FRAME_RAW(temp_frame_)->width <= 0 || AV_FRAME_RAW(temp_frame_)->height <= 0) {
        qCWarning(log_ffmpeg_backend) << "DecodeWithFFmpeg: Invalid frame dimensions:" 
                                     << AV_FRAME_RAW(temp_frame_)->width << "x" 
                                     << AV_FRAME_RAW(temp_frame_)->height;
        return QPixmap();
    }
    
    // Log frame format for debugging (first few frames and periodically)
    static int frame_format_log_count = 0;
    if (++frame_format_log_count <= 5 || frame_format_log_count % 1000 == 1) {
        const char* codec_name = codec_context->codec ? codec_context->codec->name : "unknown";
        const char* format_name = av_get_pix_fmt_name(
            static_cast<AVPixelFormat>(AV_FRAME_RAW(temp_frame_)->format));
        qCInfo(log_ffmpeg_backend) << "Received frame from" << codec_name 
                                   << "- format:" << AV_FRAME_RAW(temp_frame_)->format 
                                   << "(" << (format_name ? format_name : "unknown") << ")"
                                   << "size:" << AV_FRAME_RAW(temp_frame_)->width << "x" 
                                   << AV_FRAME_RAW(temp_frame_)->height;
    }
    
    // Handle hardware frames that need transfer to system memory
    AVFrame* frame_to_convert = AV_FRAME_RAW(temp_frame_);
    AvFramePtr sw_frame;
    
    bool is_hardware_frame = (AV_FRAME_RAW(temp_frame_)->format == AV_PIX_FMT_QSV ||
                             AV_FRAME_RAW(temp_frame_)->format == AV_PIX_FMT_CUDA);
    
    if (is_hardware_frame) {
        static int hw_transfer_attempts = 0;
        hw_transfer_attempts++;
        
        if (hw_transfer_attempts <= 5) {
            qCDebug(log_ffmpeg_backend) << "Hardware frame detected, transferring to system memory"
                                       << "(attempt" << hw_transfer_attempts << ")";
        }
        
        sw_frame = make_av_frame();
        if (!sw_frame) {
            qCWarning(log_ffmpeg_backend) << "Failed to allocate software frame for hardware transfer";
            return QPixmap();
        }
        
        ret = av_hwframe_transfer_data(AV_FRAME_RAW(sw_frame), AV_FRAME_RAW(temp_frame_), 0);
        if (ret < 0) {
            char errbuf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
            qCWarning(log_ffmpeg_backend) << "Failed to transfer hardware frame to system memory:" 
                                         << QString::fromUtf8(errbuf);
            
            static int hw_transfer_fallback = 0;
            if (++hw_transfer_fallback <= 5) {
                qCWarning(log_ffmpeg_backend) << "Hardware frame transfer failed, this may cause issues";
            }
            return QPixmap();
        }
        
        av_frame_copy_props(AV_FRAME_RAW(sw_frame), AV_FRAME_RAW(temp_frame_));
        frame_to_convert = AV_FRAME_RAW(sw_frame);
        
        static int hw_transfer_count = 0;
        if (++hw_transfer_count % 1000 == 1) {
            qCDebug(log_ffmpeg_backend) << "Successfully transferred hardware frame to system memory"
                                       << "(count:" << hw_transfer_count << ")";
        }
    }
    
    QPixmap result = ConvertFrameToPixmap(frame_to_convert);
    
    if (sw_frame) {
        AV_FRAME_RESET(sw_frame);
    }
    
    return result;
}

QPixmap FFmpegFrameProcessor::ConvertFrameToPixmap(AVFrame* frame)
{
    if (!frame) {
        qCWarning(log_ffmpeg_backend) << "ConvertFrameToPixmap: frame is null";
        return QPixmap();
    }
    
    int width = frame->width;
    int height = frame->height;
    AVPixelFormat format = static_cast<AVPixelFormat>(frame->format);
    
    // Validate frame
    if (width <= 0 || height <= 0 || !frame->data[0] || frame->linesize[0] <= 0) {
        qCWarning(log_ffmpeg_backend) << "ConvertFrameToPixmap: Invalid frame data";
        return QPixmap();
    }
    
    // Try fast path for RGB formats
    if (format == AV_PIX_FMT_RGB24 || format == AV_PIX_FMT_BGR24 || 
        format == AV_PIX_FMT_RGBA || format == AV_PIX_FMT_BGRA ||
        format == AV_PIX_FMT_BGR0 || format == AV_PIX_FMT_RGB0) {
        return ConvertRgbFrameDirectly(frame);
    }
    
    // Use scaling for other formats
    return ConvertWithScaling(frame);
}

QPixmap FFmpegFrameProcessor::ConvertRgbFrameDirectly(AVFrame* frame)
{
    AVPixelFormat format = static_cast<AVPixelFormat>(frame->format);
    const char* format_name = av_get_pix_fmt_name(format);
    
    qCDebug(log_ffmpeg_backend) << "Using RGB fast path for format:" << format_name;
    
    QImage::Format qt_format;
    if (format == AV_PIX_FMT_RGB24) {
        qt_format = QImage::Format_RGB888;
    } else if (format == AV_PIX_FMT_BGR24) {
        qt_format = QImage::Format_RGB888; // Will need RGB swap
    } else {
        qt_format = QImage::Format_RGB32; // Fallback
    }
    
    QImage image(frame->data[0], frame->width, frame->height, frame->linesize[0], qt_format);
    
    if (qt_format == QImage::Format_RGB888 && format == AV_PIX_FMT_BGR24) {
        image = image.rgbSwapped();
    }
    
    QPixmap pixmap = QPixmap::fromImage(image);
    qCDebug(log_ffmpeg_backend) << "Created pixmap from RGB fast path, size:" 
                               << pixmap.size() << "isNull:" << pixmap.isNull();
    return pixmap;
}

QPixmap FFmpegFrameProcessor::ConvertWithScaling(AVFrame* frame)
{
    int width = frame->width;
    int height = frame->height;
    AVPixelFormat format = static_cast<AVPixelFormat>(frame->format);
    
    // Update scaling context if needed (this method handles its own mutex locking)
    UpdateScalingContext(width, height, format, QSize());
    
    // Check if scaling context is available (use mutex to protect sws_context_ access)
    {
        QMutexLocker locker(&mutex_);
        if (!sws_context_) {
            qCWarning(log_ffmpeg_backend) << "Failed to create scaling context";
            return QPixmap();
        }
    }
    
    // Allocate RGB image (using ARGB32 for better compatibility and quality)
    QImage image(width, height, QImage::Format_ARGB32);
    if (image.isNull()) {
        qCWarning(log_ffmpeg_backend) << "Failed to allocate QImage for" << width << "x" << height;
        return QPixmap();
    }
    
    // Prepare output buffers
    uint8_t* rgb_data[1] = { image.bits() };
    int rgb_linesize[1] = { static_cast<int>(image.bytesPerLine()) };
    
    // Convert frame to RGB (protect sws_context_ usage with mutex)
    int scale_result;
    {
        QMutexLocker locker(&mutex_);
        scale_result = sws_scale(sws_context_, frame->data, frame->linesize, 
                                 0, height, rgb_data, rgb_linesize);
    }
    
    if (scale_result < 0) {
        qCCritical(log_ffmpeg_backend) << "sws_scale failed with result:" << scale_result;
        return QPixmap();
    }
    
    if (scale_result != height) {
        qCWarning(log_ffmpeg_backend) << "sws_scale converted" << scale_result 
                                     << "lines, expected" << height;
        return QPixmap();
    }
    
    return QPixmap::fromImage(image);
}

// Thread-safe QImage conversion methods (avoid QPixmap on worker thread)
QImage FFmpegFrameProcessor::ConvertFrameToImage(AVFrame* frame, const QSize& targetSize)
{
    if (!frame) {
        return QImage();
    }
    
    int width = frame->width;
    int height = frame->height;
    AVPixelFormat format = static_cast<AVPixelFormat>(frame->format);
    
    // Validate frame
    if (width <= 0 || height <= 0 || !frame->data[0] || frame->linesize[0] <= 0) {
        return QImage();
    }
    
    // OPTIMIZATION: Skip conversion entirely if frame already matches target
    // If requested target is larger than source, clamp it to source size
    QSize effectiveTarget = targetSize;
    if (effectiveTarget.isValid()) {
        if (effectiveTarget.width() > width || effectiveTarget.height() > height) {
            qCDebug(log_ffmpeg_backend) << "Requested target" << targetSize << "is larger than source"
                                       << width << "x" << height << "- clamping to source size";
            effectiveTarget = QSize(width, height);
        }
    }

    // OPTIMIZATION: Skip conversion entirely if frame already matches (effective) target
    if (effectiveTarget.isValid() && 
        frame->width == effectiveTarget.width() && 
        frame->height == effectiveTarget.height() &&
        (format == AV_PIX_FMT_RGB24 || format == AV_PIX_FMT_BGR24)) {
        // Direct copy without scaling - FASTEST PATH
        return ConvertRgbFrameDirectlyToImage(frame);
    }

    // Try fast path for RGB formats only if no scaling needed
    if ((format == AV_PIX_FMT_RGB24 || format == AV_PIX_FMT_BGR24 || 
         format == AV_PIX_FMT_RGBA || format == AV_PIX_FMT_BGRA ||
         format == AV_PIX_FMT_BGR0 || format == AV_PIX_FMT_RGB0) &&
        (!effectiveTarget.isValid() || (effectiveTarget.width() == width && effectiveTarget.height() == height))) {
        return ConvertRgbFrameDirectlyToImage(frame);
    }

    // Use scaling for other formats or when target size is specified
    return ConvertWithScalingToImage(frame, effectiveTarget);
}

QImage FFmpegFrameProcessor::ConvertRgbFrameDirectlyToImage(AVFrame* frame)
{
    AVPixelFormat format = static_cast<AVPixelFormat>(frame->format);
    
    QImage::Format qt_format;
    if (format == AV_PIX_FMT_RGB24) {
        qt_format = QImage::Format_RGB888;
    } else if (format == AV_PIX_FMT_BGR24) {
        qt_format = QImage::Format_RGB888; // Will need RGB swap
    } else {
        qt_format = QImage::Format_RGB32; // Fallback
    }
    
    QImage image(frame->data[0], frame->width, frame->height, frame->linesize[0], qt_format);
    
    if (qt_format == QImage::Format_RGB888 && format == AV_PIX_FMT_BGR24) {
        image = image.rgbSwapped();
    }
    
    // Create a deep copy to ensure thread safety
    return image.copy();
}

QImage FFmpegFrameProcessor::ConvertWithScalingToImage(AVFrame* frame, const QSize& targetSize)
{
    int width = frame->width;
    int height = frame->height;
    AVPixelFormat format = static_cast<AVPixelFormat>(frame->format);
    
    // Determine target dimensions
    int targetWidth = targetSize.isValid() ? targetSize.width() : width;
    int targetHeight = targetSize.isValid() ? targetSize.height() : height;

    // If requested target is larger than source, clamp to source (default) size
    if (targetWidth > width || targetHeight > height) {
        qCDebug(log_ffmpeg_backend) << "Requested scaling to" << targetWidth << "x" << targetHeight
                                   << "is larger than source" << width << "x" << height
                                   << "- clamping to source size";
        targetWidth = width;
        targetHeight = height;
    }
    
    // Update scaling context if needed
    UpdateScalingContext(width, height, format, QSize(targetWidth, targetHeight));
    
    // Check if scaling context is available
    {
        QMutexLocker locker(&mutex_);
        if (!sws_context_) {
            return QImage();
        }
    }
    
    // Allocate RGB image with target dimensions
    QImage image(targetWidth, targetHeight, QImage::Format_ARGB32);
    if (image.isNull()) {
        return QImage();
    }
    
    // Prepare output buffers
    uint8_t* rgb_data[1] = { image.bits() };
    int rgb_linesize[1] = { static_cast<int>(image.bytesPerLine()) };
    
    // Convert frame to RGB
    int scale_result;
    {
        QMutexLocker locker(&mutex_);
        scale_result = sws_scale(sws_context_, frame->data, frame->linesize, 
                                 0, height, rgb_data, rgb_linesize);
    }
    
    if (scale_result < 0 || scale_result != targetHeight) {
        return QImage();
    }
    
    return image.copy();  // Ensure thread-safe copy
}

void FFmpegFrameProcessor::UpdateScalingContext(int width, int height, AVPixelFormat format, const QSize& targetSize)
{
    QMutexLocker locker(&mutex_);
    
    // Determine target dimensions
    int targetWidth = targetSize.isValid() ? targetSize.width() : width;
    int targetHeight = targetSize.isValid() ? targetSize.height() : height;

    // If requested target is larger than source, clamp to source size
    if (targetWidth > width || targetHeight > height) {
        qCDebug(log_ffmpeg_backend) << "Clamping requested scaling target" << targetSize
                                   << "to source size" << width << "x" << height;
        targetWidth = width;
        targetHeight = height;
    }
    
    // Check if context needs update (including target dimensions)
    static int last_target_width_ = -1;
    static int last_target_height_ = -1;
    
    if (sws_context_ && width == last_width_ && height == last_height_ && format == last_format_ && 
        scaling_algorithm_ == last_scaling_algorithm_ && targetWidth == last_target_width_ && targetHeight == last_target_height_) {
        return; // Context is still valid
    }
    
    CleanupScalingContext();
    
    // Update cached target dimensions
    last_target_width_ = targetWidth;
    last_target_height_ = targetHeight;
    
    const char* format_name = av_get_pix_fmt_name(format);
    const char* algorithm_name = "unknown";
    if (scaling_algorithm_ == SWS_LANCZOS) algorithm_name = "LANCZOS (high quality)";
    else if (scaling_algorithm_ == SWS_SPLINE) algorithm_name = "SPLINE (balanced)";
    else if (scaling_algorithm_ == SWS_BICUBIC) algorithm_name = "BICUBIC (standard)";
    else if (scaling_algorithm_ == SWS_BILINEAR) algorithm_name = "BILINEAR (fast)";
    
    qCInfo(log_ffmpeg_backend) << "Creating scaling context:" 
                               << width << "x" << height 
                               << "to" << targetWidth << "x" << targetHeight
                               << "from format" << format << "(" << (format_name ? format_name : "unknown") << ")"
                               << "to RGB32 (32-bit ARGB)"
                               << "with algorithm" << algorithm_name;
    
    // OPTIMIZATION: Choose scaling algorithm based on performance needs
    int scaling_flags;
    
    if (targetSize.isValid() && (targetSize.width() == width && targetSize.height() == height)) {
        // No scaling needed - use fastest point sampling
        scaling_flags = SWS_POINT;
        qCDebug(log_ffmpeg_backend) << "Using point sampling (no scaling needed)";
    } else {
        // Real scaling needed - use fast bilinear for low latency
        scaling_flags = SWS_FAST_BILINEAR;
        qCDebug(log_ffmpeg_backend) << "Using fast bilinear scaling for low latency";
    }
    
    sws_context_ = sws_getContext(
        width, height, format,
        targetWidth, targetHeight, AV_PIX_FMT_BGRA,
        scaling_flags,  // Optimized for performance
        nullptr, nullptr, nullptr
    );
    
    if (!sws_context_) {
        qCCritical(log_ffmpeg_backend) << "Failed to create scaling context";
        return;
    }
    
    qCInfo(log_ffmpeg_backend) << "Scaling context created successfully";
    
    // ============ CRITICAL: Set color space for MJPEG quality ============
    // MJPEG uses YUVJ (full range YUV), not limited range YUV
    int src_range = 1;  // Full range input (1 = full, 0 = limited)
    int dst_range = 1;  // Full range output
    
    // Get color coefficients for ITU709
    const int* coeffs = sws_getCoefficients(SWS_CS_ITU709);
    
    int ret = sws_setColorspaceDetails(
        sws_context_,
        coeffs,              // Input coefficients
        src_range,           // Input range
        coeffs,              // Output coefficients
        dst_range,           // Output range
        0,                   // Brightness
        1 << 16,             // Contrast (1.0 in fixed point)
        1 << 16              // Saturation (1.0 in fixed point)
    );
    
    if (ret < 0) {
        qCWarning(log_ffmpeg_backend) << "Failed to set color space details for scaling context";
    } else {
        qCInfo(log_ffmpeg_backend) << "Color space: ITU709, Full range input→output";
    }
    
    last_width_ = width;
    last_height_ = height;
    last_format_ = format;
    last_scaling_algorithm_ = scaling_algorithm_;
    
    qCInfo(log_ffmpeg_backend) << "Successfully created scaling context with quality color space";
}

void FFmpegFrameProcessor::ApplySharpeningFilter(uint8_t *buffer, int width, int height)
{
    // DISABLED: Sharpening filter is TOO EXPENSIVE for real-time processing
    // Per-pixel operations on 1920x1080 = ~6M pixel operations per frame
    // At 30fps this becomes 180M operations/sec = PERFORMANCE KILLER!
    // This was causing FPS to drop from 30fps to 15fps
    //
    // Quality improvements come from decoder optimization:
    // 1. Multi-threaded MJPEG decompression (12 threads on 12-core CPU)
    // 2. Disabled fast mode (full IDCT + loop filters)
    // 3. Full-range YUV output (YUVJ420P)
    // 4. Large buffer (256MB for 60fps streams)
    
    (void)buffer;
    (void)width;
    (void)height;
}

void FFmpegFrameProcessor::CleanupScalingContext()
{
    if (sws_context_) {
        sws_freeContext(sws_context_);
        sws_context_ = nullptr;
    }
    
    last_width_ = -1;
    last_height_ = -1;
    last_format_ = AV_PIX_FMT_NONE;
    last_scaling_algorithm_ = -1;
}
