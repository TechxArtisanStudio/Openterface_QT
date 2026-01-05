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
    , last_target_width_(-1)
    , last_target_height_(-1)
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
    return latest_frame_.copy();
}

QImage FFmpegFrameProcessor::GetLatestOriginalFrame() const
{
    QMutexLocker locker(&mutex_);
    return latest_original_frame_.copy();
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


QImage FFmpegFrameProcessor::ProcessPacketToImage(AVPacket* packet, AVCodecContext* codec_context, 
                                                   bool is_recording, const QSize& targetSize)
{
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
    
    // First convert to original resolution (without targetSize scaling)
    QImage originalResult = ConvertFrameToImage(frame_to_convert, QSize());
    
    // Then convert with scaling if targetSize is specified
    QImage result = originalResult;
    if (targetSize.isValid() && !targetSize.isEmpty()) {
        result = ConvertFrameToImage(frame_to_convert, targetSize);
    }
    
    if (sw_frame) {
        AV_FRAME_RESET(sw_frame);
    }
    
    // Update statistics and store latest frames
    if (!result.isNull() && !originalResult.isNull()) {
        frame_count_++;
        
        // Skip startup frames if configured
        if (frame_count_ <= startup_frames_to_skip_) {
            return QImage();
        }
        
        // Store both original and scaled frames for image capture
        {
            QMutexLocker locker(&mutex_);
            latest_frame_ = result.copy();  // Scaled frame for display
            latest_original_frame_ = originalResult.copy();  // Original frame for screenshots
        }
    }
    
    return result.copy();  // Deep copy for thread safety
}

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
        return ConvertRgbFrameDirectlyToImage(frame);  // Already returns deep copy
    }

    // Use scaling for other formats or when target size is specified
    return ConvertWithScalingToImage(frame, effectiveTarget);  // Already returns deep copy
}

QImage FFmpegFrameProcessor::ConvertRgbFrameDirectlyToImage(AVFrame* frame) {
    AVPixelFormat format = static_cast<AVPixelFormat>(frame->format);
    int width = frame->width;
    int height = frame->height;

    // Allocate a new QImage with its own memory
    QImage image(width, height, QImage::Format_RGB888);
    if (image.isNull()) {
        return QImage();
    }

    if (format == AV_PIX_FMT_RGB24) {
        // Direct copy
        for (int y = 0; y < height; ++y) {
            memcpy(image.scanLine(y), frame->data[0] + y * frame->linesize[0], width * 3);
        }
    } else if (format == AV_PIX_FMT_BGR24) {
        // BGR to RGB conversion during copy
        for (int y = 0; y < height; ++y) {
            const uint8_t* src = frame->data[0] + y * frame->linesize[0];
            uint8_t* dst = image.scanLine(y);
            for (int x = 0; x < width; ++x) {
                dst[x*3+0] = src[x*3+2]; // R = B
                dst[x*3+1] = src[x*3+1]; // G = G
                dst[x*3+2] = src[x*3+0]; // B = R
            }
        }
    }
    return image; // Fully independent
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
    
    // CRITICAL FIX: Allocate image BEFORE locking mutex to reduce lock time
    QImage image(targetWidth, targetHeight, QImage::Format_ARGB32);
    if (image.isNull()) {
        return QImage();
    }
    
    // Prepare output buffers
    uint8_t* rgb_data[1] = { image.bits() };
    int rgb_linesize[1] = { static_cast<int>(image.bytesPerLine()) };
    
    // CRITICAL FIX: Hold mutex for entire sws_scale operation to prevent context from being freed
    int scale_result;
    {
        QMutexLocker locker(&mutex_);
        
        // CRITICAL FIX: Check again after acquiring lock (context might have been freed)
        if (!sws_context_) {
            qCWarning(log_ffmpeg_backend) << "Scaling context was freed during operation";
            return QImage();
        }
        
        scale_result = sws_scale(sws_context_, frame->data, frame->linesize, 
                                 0, height, rgb_data, rgb_linesize);
    }
    
    if (scale_result < 0 || scale_result != targetHeight) {
        qCWarning(log_ffmpeg_backend) << "sws_scale failed: result=" << scale_result 
                                     << "expected=" << targetHeight;
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
    
    // CRITICAL FIX: Use member variables instead of static to prevent race conditions
    if (sws_context_ && width == last_width_ && height == last_height_ && format == last_format_ && 
        scaling_algorithm_ == last_scaling_algorithm_ && targetWidth == last_target_width_ && targetHeight == last_target_height_) {
        return; // Context is still valid
    }
    
    CleanupScalingContext();
    
    // CRITICAL FIX: Update member variables (protected by mutex)
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
