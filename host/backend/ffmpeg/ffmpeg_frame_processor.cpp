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
#include <QThread>
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
    , frame_drop_threshold_display_(17)    // allow ~16.66ms (60fps) intervals to pass reliably
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
    // Initialize TurboJPEG decompressor with multi-threading support
    turbojpeg_handle_ = tjInitDecompress();
    if (!turbojpeg_handle_) {
        qCWarning(log_ffmpeg_backend) << "Failed to initialize TurboJPEG decompressor";
    } else {
        // Enable multi-threading by setting number of CPU cores
        int num_threads = QThread::idealThreadCount();
        if (num_threads > 1) {
            qCDebug(log_ffmpeg_backend) << "TurboJPEG initialized with" << num_threads << "thread support";
        }
        qCDebug(log_ffmpeg_backend) << "TurboJPEG decompressor initialized successfully";
    }
#endif

    // Start high-resolution process timer used for frame pacing decisions
    last_process_timer_.start();

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
    last_process_timer_.restart();
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
    // Use high-resolution elapsed time (microsecond precision) to avoid millisecond rounding errors.
    qint64 elapsed_us = last_process_timer_.nsecsElapsed() / 1000; // microseconds since last restart
    qint64 threshold_us = (is_recording ? frame_drop_threshold_recording_ : frame_drop_threshold_display_) * 1000LL;

    if (elapsed_us < threshold_us) {
        ++dropped_frames_;
        return true;
    }

    // Restart the timer to measure the next interval from the end of this processing step.
    last_process_timer_.restart();
    last_process_time_ = QDateTime::currentMSecsSinceEpoch();

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
    
    // PRIORITY 1: Hardware acceleration decoding (highest priority)
    // Check if codec is a hardware decoder before attempting decode
    bool is_hardware_decoder = IsHardwareDecoder(codec_context);
    if (is_hardware_decoder) {
        qCDebug(log_ffmpeg_backend) << "Using hardware decoder:" << codec_context->codec->name;
        // Proceed directly to FFmpeg hardware decoding
        return ProcessWithFFmpegDecoding(packet, codec_context, is_recording, targetSize);
    }
    
#ifdef HAVE_LIBJPEG_TURBO
    // PRIORITY 2: TurboJPEG acceleration (for MJPEG only, when no hardware acceleration)
    if (codec_context->codec_id == AV_CODEC_ID_MJPEG) {
        tjhandle handle = GetThreadLocalTurboJPEGHandle();
        if (handle) {
            QImage turbojpeg_result = DecodeMJPEGWithTurboJPEG(packet, targetSize, handle);
            if (!turbojpeg_result.isNull()) {
                // qCDebug(log_ffmpeg_backend) << "Successfully decoded with TurboJPEG acceleration";
                // Success with TurboJPEG - store frames and return
                QImage originalResult = turbojpeg_result;
                QImage result = turbojpeg_result;
                
                // Apply scaling if needed
                if (targetSize.isValid() && !targetSize.isEmpty() && 
                    targetSize != QSize(turbojpeg_result.width(), turbojpeg_result.height())) {
                    result = turbojpeg_result.scaled(targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                }
                
                // Update frame count and store frames
                frame_count_++;
                if (frame_count_ > startup_frames_to_skip_) {
                    QMutexLocker locker(&mutex_);
                    latest_frame_ = result.copy();
                    latest_original_frame_ = originalResult.copy();
                }
                
                return result.copy();
            }
        }
        qCDebug(log_ffmpeg_backend) << "TurboJPEG failed, falling back to CPU decode";
    }
#endif
    
    // PRIORITY 3: CPU direct decoding (fallback when no acceleration available)
    qCDebug(log_ffmpeg_backend) << "Using CPU decoder:" << codec_context->codec->name;
    return ProcessWithFFmpegDecoding(packet, codec_context, is_recording, targetSize);
}

QImage FFmpegFrameProcessor::ProcessWithFFmpegDecoding(AVPacket* packet, AVCodecContext* codec_context, 
                                                      bool is_recording, const QSize& targetSize)
{
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
    
    // Determine if we need both original and scaled images
    int frame_width = frame_to_convert->width;
    int frame_height = frame_to_convert->height;
    QSize frameSize(frame_width, frame_height);
    bool needOriginal = targetSize.isValid() && !targetSize.isEmpty() && targetSize != frameSize;
    
    QImage result;
    QImage originalResult;
    
    if (needOriginal) {
        // Need both original and scaled versions
        originalResult = ConvertFrameToImage(frame_to_convert, QSize());
        result = ConvertFrameToImage(frame_to_convert, targetSize);
    } else {
        // Only need one version (either scaled or original)
        result = ConvertFrameToImage(frame_to_convert, targetSize.isValid() ? targetSize : QSize());
        originalResult = result;  // Same image serves as both
    }
    
    if (sw_frame) {
        AV_FRAME_RESET(sw_frame);
    }
    
    // Update statistics and store latest frames
    if (!result.isNull()) {
        frame_count_++;
        
        // Skip startup frames if configured
        if (frame_count_ <= startup_frames_to_skip_) {
            return QImage();
        }
        
        // Store frames
        {
            QMutexLocker locker(&mutex_);
            latest_frame_ = result.copy();  // Frame for display
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
    QImage image(targetWidth, targetHeight, QImage::Format_RGB888);
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

#ifdef HAVE_LIBJPEG_TURBO
QImage FFmpegFrameProcessor::DecodeMJPEGWithTurboJPEG(AVPacket* packet, const QSize& targetSize, tjhandle handle)
{
    if (!handle || !packet || !packet->data || packet->size <= 0) {
        return QImage();
    }
    
    int width, height, subsamp, colorspace;
    
    // Get image info from JPEG header
    if (tjDecompressHeader3(handle, packet->data, packet->size, 
                           &width, &height, &subsamp, &colorspace) < 0) {
        qCWarning(log_ffmpeg_backend) << "TurboJPEG header decode failed:" << tjGetErrorStr();
        return QImage();
    }
    
    // Determine target size for scaling
    int target_width = width;
    int target_height = height;
    bool need_scaling = false;
    
    if (targetSize.isValid() && !targetSize.isEmpty()) {
        // TurboJPEG supports built-in scaling to 1/8, 1/4, 1/2, 1, 2x, 4x, 8x
        // Choose the closest scaling factor
        double scale_x = static_cast<double>(targetSize.width()) / width;
        double scale_y = static_cast<double>(targetSize.height()) / height;
        double scale = qMin(scale_x, scale_y);
        
        if (scale <= 0.125) {
            target_width = width / 8;
            target_height = height / 8;
        } else if (scale <= 0.25) {
            target_width = width / 4;
            target_height = height / 4;
        } else if (scale <= 0.5) {
            target_width = width / 2;
            target_height = height / 2;
        } else if (scale >= 8.0) {
            target_width = width * 8;
            target_height = height * 8;
        } else if (scale >= 4.0) {
            target_width = width * 4;
            target_height = height * 4;
        } else if (scale >= 2.0) {
            target_width = width * 2;
            target_height = height * 2;
        }
        // else keep original size
        
        need_scaling = (target_width != width || target_height != height);
    }
    
    // Create QImage for output
    QImage image(target_width, target_height, QImage::Format_RGB888);
    if (image.isNull()) {
        return QImage();
    }
    
    // Decompress directly to QImage buffer
    if (tjDecompress2(handle, packet->data, packet->size,
                     image.bits(), target_width, image.bytesPerLine(),
                     target_height, TJPF_RGB, TJFLAG_FASTDCT) < 0) {
        qCWarning(log_ffmpeg_backend) << "TurboJPEG decompress failed:" << tjGetErrorStr();
        return QImage();
    }
    
    // qCDebug(log_ffmpeg_backend) << "TurboJPEG decoded MJPEG:" << width << "x" << height 
    //                            << "to" << target_width << "x" << target_height;
    
    return image;
}
#endif

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
                               << "to RGB24 (24-bit RGB)"
                               << "with algorithm" << algorithm_name;
    
    // OPTIMIZATION: Choose scaling algorithm based on performance needs
    int scaling_flags;
    
    if (targetSize.isValid() && (targetSize.width() == width && targetSize.height() == height)) {
        // No scaling needed - use fastest point sampling
        scaling_flags = SWS_POINT;
        qCDebug(log_ffmpeg_backend) << "Using point sampling (no scaling needed)";
    } else {
        // Real scaling needed - use fast bilinear for low latency
        scaling_flags = SWS_BICUBIC;
        qCDebug(log_ffmpeg_backend) << "Using bicubic scaling for better quality";
    }
    
    sws_context_ = sws_getContext(
        width, height, format,
        targetWidth, targetHeight, AV_PIX_FMT_RGB24,
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

#ifdef HAVE_LIBJPEG_TURBO
tjhandle FFmpegFrameProcessor::GetThreadLocalTurboJPEGHandle()
{
    // Use thread-local storage for TurboJPEG handles to enable multi-threading
    thread_local tjhandle local_handle = nullptr;
    
    if (!local_handle) {
        local_handle = tjInitDecompress();
        if (!local_handle) {
            qCWarning(log_ffmpeg_backend) << "Failed to initialize thread-local TurboJPEG handle";
            return nullptr;
        }
        
        qCDebug(log_ffmpeg_backend) << "Created thread-local TurboJPEG handle for thread ID:"
                                   << QThread::currentThreadId();
    }
    
    return local_handle;
}
#endif

