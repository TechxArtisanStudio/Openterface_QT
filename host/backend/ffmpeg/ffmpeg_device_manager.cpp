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

#include "ffmpeg_device_manager.h"
#include "ffmpeg_hardware_accelerator.h"
#include "global.h"
#include "ui/globalsetting.h"

#include <QLoggingCategory>
#include <QDateTime>
#include <QDebug>
#include <QThread>
#include <QElapsedTimer>
#include <thread>

extern "C" {
#include <libavdevice/avdevice.h>
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}

Q_DECLARE_LOGGING_CATEGORY(log_ffmpeg_backend)

FFmpegDeviceManager::FFmpegDeviceManager()
    : format_context_(nullptr)
    , codec_context_(nullptr)
    , video_stream_index_(-1)
    , interrupt_requested_(false)
    , operation_start_time_(0)
{
}

FFmpegDeviceManager::~FFmpegDeviceManager()
{
    CloseDevice();
}

bool FFmpegDeviceManager::OpenDevice(const QString& device_path, const QSize& resolution, 
                                     int framerate, FFmpegHardwareAccelerator* hw_accelerator)
{
    qCDebug(log_ffmpeg_backend) << "Opening input device:" << device_path;
    
    // Reset interrupt state for this new operation
    interrupt_requested_ = false;
    operation_start_time_ = QDateTime::currentMSecsSinceEpoch();
    
    if (!InitializeInputStream(device_path, resolution, framerate)) {
        qCWarning(log_ffmpeg_backend) << "Failed to initialize input stream";
        return false;
    }
    
    if (!FindVideoStream()) {
        qCWarning(log_ffmpeg_backend) << "Failed to find video stream";
        return false;
    }
    
    if (!SetupDecoder(hw_accelerator)) {
        qCWarning(log_ffmpeg_backend) << "Failed to setup decoder";
        return false;
    }
    
    bool usingHw = (hw_accelerator != nullptr && hw_accelerator->IsHardwareAccelEnabled());
    
    // WARM-UP: Force hardware codec (QSV/CUDA) to complete its deferred session
    // initialisation NOW, before we drain the ring buffer.
    //
    // Background: mjpeg_qsv (and mjpeg_cuvid) create their internal GPU/MFX session
    // lazily – only when avcodec_send_packet() is first called.  Session creation takes
    // 200–800 ms.  Without warm-up:
    //   1. DrainBufferedPackets finishes (buffer is now live)
    //   2. Capture thread calls avcodec_send_packet on its first real frame
    //   3. QSV session init blocks for 200–800 ms inside that call
    //   4. Camera fills the ring buffer again with stale frames during that pause
    //   5. User sees 200–800 ms of additional latency on every session start
    //
    // With warm-up the GPU session is ready before DrainBufferedPackets, so the
    // drain really does put us at the live edge of the stream.
    if (usingHw) {
        WarmUpHardwareDecoder();
    }
    
    // Now drain packets that accumulated during codec init + warm-up.
    DrainBufferedPackets(usingHw);
    
    // Reset operation timer - device opened successfully
    operation_start_time_ = 0;
    
    qCDebug(log_ffmpeg_backend) << "Input device opened successfully";
    return true;
}

void FFmpegDeviceManager::CloseDevice()
{
    // Close codec context
    if (codec_context_) {
        avcodec_free_context(&codec_context_);
        codec_context_ = nullptr;
    }
    
    // Close format context
    if (format_context_) {
        avformat_close_input(&format_context_);
        format_context_ = nullptr;
    }
    
    video_stream_index_ = -1;
}

bool FFmpegDeviceManager::IsDeviceOpen() const
{
    return format_context_ != nullptr && codec_context_ != nullptr;
}

bool FFmpegDeviceManager::GetMaxCameraCapability(const QString& device_path, CameraCapability& capability)
{
    qCInfo(log_ffmpeg_backend) << "Loading video settings from GlobalSetting for:" << device_path;
    
    // Load video settings from GlobalSetting into GlobalVar
    GlobalSetting::instance().loadVideoSettings();
    
    // Get the stored resolution and framerate
    int width = GlobalVar::instance().getCaptureWidth();
    int height = GlobalVar::instance().getCaptureHeight();
    int fps = GlobalVar::instance().getCaptureFps();
    
    capability.resolution = QSize(width, height);
    capability.framerate = fps;
    
    qCInfo(log_ffmpeg_backend) << "✓ Maximum capability from GlobalSetting:" 
                              << capability.resolution << "@" << capability.framerate << "FPS";
    return true;
}

void FFmpegDeviceManager::SetInterruptRequested(bool requested)
{
    interrupt_requested_ = requested;
}

int FFmpegDeviceManager::InterruptCallback(void* ctx)
{
    FFmpegDeviceManager* manager = static_cast<FFmpegDeviceManager*>(ctx);
    if (!manager) {
        return 0;
    }
    
    // Check if interrupt was explicitly requested
    if (manager->interrupt_requested_) {
        qCDebug(log_ffmpeg_backend) << "FFmpeg operation interrupted by request";
        return 1; // Interrupt the operation
    }
    
    // Check if operation has timed out
    if (manager->operation_start_time_ > 0) {
        qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - manager->operation_start_time_;
        if (elapsed > kOperationTimeoutMs) {
            qCWarning(log_ffmpeg_backend) << "FFmpeg operation timed out after" << elapsed << "ms";
            return 1; // Interrupt the operation
        }
    }
    
    return 0; // Continue the operation
}

bool FFmpegDeviceManager::InitializeInputStream(const QString& device_path, const QSize& resolution, int framerate)
{
#ifdef Q_OS_WIN
    // WINDOWS: Use DirectShow for video capture
    qCDebug(log_ffmpeg_backend) << "Windows platform detected - using DirectShow input";
    
    // Allocate format context
    format_context_ = avformat_alloc_context();
    if (!format_context_) {
        qCCritical(log_ffmpeg_backend) << "Failed to allocate format context";
        return false;
    }
    
    // CRITICAL FIX: Set interrupt callback to prevent blocking operations
    format_context_->interrupt_callback.callback = FFmpegDeviceManager::InterruptCallback;
    format_context_->interrupt_callback.opaque = this;
    
    // Find DirectShow input format
    const AVInputFormat* inputFormat = av_find_input_format("dshow");
    if (!inputFormat) {
        qCCritical(log_ffmpeg_backend) << "DirectShow input format not found - FFmpeg may not be built with dshow support";
        return false;
    }
    
    // RESPONSIVENESS: Set low-latency input options for DirectShow
    AVDictionary* options = nullptr;
    av_dict_set(&options, "video_size", QString("%1x%2").arg(resolution.width()).arg(resolution.height()).toUtf8().constData(), 0);
    av_dict_set(&options, "framerate", QString::number(framerate).toUtf8().constData(), 0);
    
    // ============ MJPEG QUALITY OPTIMIZATIONS ============
    
    // 1. Small ring buffer for low latency (KVM use case prioritizes freshness over smoothness)
    // 256M was causing 2-3s latency with QSV: during HW init frames pile up and are replayed stale.
    // At 1080p MJPEG ~500KB/frame: 8M ≈ 16 frames ≈ 530ms max buffer. DirectShow drops oldest
    // frames when full, so we always get near-live frames once the buffer is saturated.
    av_dict_set(&options, "rtbufsize", "8M", 0);
    
    // 2. Disable frame dropping at input to preserve quality
    av_dict_set(&options, "fflags", "discardcorrupt", 0);
    
    // 3. Low-latency and quality balance
    av_dict_set(&options, "flags", "low_delay", 0);
    av_dict_set(&options, "max_delay", "2000", 0);
    
    // 4. Minimal probe for faster startup without sacrificing MJPEG decompression
    av_dict_set(&options, "probesize", "32", 0);
    av_dict_set(&options, "analyzeduration", "0", 0);
    
    // 5. Critical timeout to prevent blocking
    av_dict_set(&options, "timeout", "5000000", 0);
    
    qCDebug(log_ffmpeg_backend) << "Trying DirectShow with MJPEG format, resolution" << resolution << "and framerate" << framerate;
    qCDebug(log_ffmpeg_backend) << "DirectShow device string:" << device_path;
    
    // Open input - device_path should be in format "video=Device Name"
    int ret = avformat_open_input(&format_context_, device_path.toUtf8().constData(), inputFormat, &options);
    av_dict_free(&options);
    
    // If MJPEG fails, try without specifying codec (auto-detect)
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
        qCWarning(log_ffmpeg_backend) << "MJPEG format failed:" << QString::fromUtf8(errbuf) << "- trying auto-detection";
        
        // Reset format context
        if (format_context_) {
            avformat_close_input(&format_context_);
        }
        format_context_ = avformat_alloc_context();
        
        // Re-set interrupt callback
        format_context_->interrupt_callback.callback = FFmpegDeviceManager::InterruptCallback;
        format_context_->interrupt_callback.opaque = this;
        
        // Try without codec specification
        AVDictionary* fallbackOptions = nullptr;
        av_dict_set(&fallbackOptions, "video_size", QString("%1x%2").arg(resolution.width()).arg(resolution.height()).toUtf8().constData(), 0);
        av_dict_set(&fallbackOptions, "framerate", QString::number(framerate).toUtf8().constData(), 0);
        av_dict_set(&fallbackOptions, "rtbufsize", "8M", 0);  // Keep low-latency buffer for KVM
        
        ret = avformat_open_input(&format_context_, device_path.toUtf8().constData(), inputFormat, &fallbackOptions);
        av_dict_free(&fallbackOptions);
    }
    
    // If that fails, try minimal options
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
        qCWarning(log_ffmpeg_backend) << "Auto-detection failed:" << QString::fromUtf8(errbuf) << "- trying minimal options";
        
        // Reset format context
        if (format_context_) {
            avformat_close_input(&format_context_);
        }
        format_context_ = avformat_alloc_context();
        
        // Re-set interrupt callback
        format_context_->interrupt_callback.callback = FFmpegDeviceManager::InterruptCallback;
        format_context_->interrupt_callback.opaque = this;
        
        // Try with just the device path
        ret = avformat_open_input(&format_context_, device_path.toUtf8().constData(), inputFormat, nullptr);
    }
    
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
        qCCritical(log_ffmpeg_backend) << "Failed to open DirectShow device:" << QString::fromUtf8(errbuf);
        qCCritical(log_ffmpeg_backend) << "Device path:" << device_path;
        qCCritical(log_ffmpeg_backend) << "Make sure the device name is correct and the camera is not in use by another application";
        return false;
    }
    
    qCDebug(log_ffmpeg_backend) << "Successfully opened DirectShow device" << device_path;
    
#else
    // LINUX/MACOS: Use V4L2 for video capture
    // RESPONSIVENESS OPTIMIZATION: Configure device for minimal latency
    qCDebug(log_ffmpeg_backend) << "Pre-configuring device for low-latency MJPEG capture...";
    
    QString configCommand = QString("v4l2-ctl --device=%1 --set-fmt-video=width=%2,height=%3,pixelformat=MJPG")
                           .arg(device_path).arg(resolution.width()).arg(resolution.height());
    int configResult = system(configCommand.toUtf8().constData());
    
    QString framerateCommand = QString("v4l2-ctl --device=%1 --set-parm=%2")
                              .arg(device_path).arg(framerate);
    int framerateResult = system(framerateCommand.toUtf8().constData());
    
    // RESPONSIVENESS: Try to configure minimal buffering for lower latency
    QString bufferCommand = QString("v4l2-ctl --device=%1")
                           .arg(device_path);
    [[maybe_unused]] auto bufferResult = system(bufferCommand.toUtf8().constData()); // Don't check result - optional optimization
    
    if (configResult == 0 && framerateResult == 0) {
        qCDebug(log_ffmpeg_backend) << "Device pre-configured successfully for low-latency MJPEG" << resolution << "at" << framerate << "fps";
    } else {
        qCWarning(log_ffmpeg_backend) << "Device pre-configuration failed, continuing with FFmpeg initialization";
    }
    
    // Allocate format context
    format_context_ = avformat_alloc_context();
    if (!format_context_) {
        qCCritical(log_ffmpeg_backend) << "Failed to allocate format context";
        return false;
    }
    
    // CRITICAL FIX: Set interrupt callback to prevent blocking operations
    format_context_->interrupt_callback.callback = FFmpegDeviceManager::InterruptCallback;
    format_context_->interrupt_callback.opaque = this;
    
    // Find input format (V4L2) - try multiple format names
    const AVInputFormat* inputFormat = av_find_input_format("v4l2");
    if (!inputFormat) {
        qCCritical(log_ffmpeg_backend) << "V4L2 input format not found";
        return false;
    }
    
    // RESPONSIVENESS: Set low-latency input options for MJPEG
    AVDictionary* options = nullptr;
    av_dict_set(&options, "video_size", QString("%1x%2").arg(resolution.width()).arg(resolution.height()).toUtf8().constData(), 0);
    av_dict_set(&options, "framerate", QString::number(framerate).toUtf8().constData(), 0);
    av_dict_set(&options, "input_format", "mjpeg", 0); // Should work due to pre-configuration
    
    // CRITICAL LOW-LATENCY OPTIMIZATIONS for KVM responsiveness:
    av_dict_set(&options, "fflags", "nobuffer", 0);        // Disable input buffering
    av_dict_set(&options, "flags", "low_delay", 0);        // Enable low delay mode  
    av_dict_set(&options, "framedrop", "1", 0);            // Allow frame dropping
    av_dict_set(&options, "use_wallclock_as_timestamps", "1", 0); // Use wall clock for timestamps
    av_dict_set(&options, "probesize", "32", 0);           // Minimal probe size for faster start
    av_dict_set(&options, "analyzeduration", "0", 0);      // Skip analysis to reduce startup delay
    
    qCDebug(log_ffmpeg_backend) << "Trying low-latency MJPEG format with resolution" << resolution << "and framerate" << framerate;
    
    // Open input
    int ret = avformat_open_input(&format_context_, device_path.toUtf8().constData(), inputFormat, &options);
    av_dict_free(&options);
    
    // If MJPEG fails, try YUYV422
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
        qCWarning(log_ffmpeg_backend) << "MJPEG format failed:" << QString::fromUtf8(errbuf) << "- trying YUYV422";
        
        // Reset format context
        if (format_context_) {
            avformat_close_input(&format_context_);
        }
        format_context_ = avformat_alloc_context();
        
        // Re-set interrupt callback
        format_context_->interrupt_callback.callback = FFmpegDeviceManager::InterruptCallback;
        format_context_->interrupt_callback.opaque = this;
        
        // Try YUYV422 format
        AVDictionary* yuvOptions = nullptr;
        av_dict_set(&yuvOptions, "video_size", QString("%1x%2").arg(resolution.width()).arg(resolution.height()).toUtf8().constData(), 0);
        av_dict_set(&yuvOptions, "framerate", QString::number(framerate).toUtf8().constData(), 0);
        av_dict_set(&yuvOptions, "input_format", "yuyv422", 0);
        
        ret = avformat_open_input(&format_context_, device_path.toUtf8().constData(), inputFormat, &yuvOptions);
        av_dict_free(&yuvOptions);
    }
    
    // If that fails, try without specifying input format (auto-detect)
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
        qCWarning(log_ffmpeg_backend) << "YUYV422 format failed:" << QString::fromUtf8(errbuf) << "- trying auto-detection";
        
        // Reset format context
        if (format_context_) {
            avformat_close_input(&format_context_);
        }
        format_context_ = avformat_alloc_context();
        
        // Re-set interrupt callback
        format_context_->interrupt_callback.callback = FFmpegDeviceManager::InterruptCallback;
        format_context_->interrupt_callback.opaque = this;
        
        // Try again without input_format specification (auto-detect)
        AVDictionary* fallbackOptions = nullptr;
        av_dict_set(&fallbackOptions, "video_size", QString("%1x%2").arg(resolution.width()).arg(resolution.height()).toUtf8().constData(), 0);
        av_dict_set(&fallbackOptions, "framerate", QString::number(framerate).toUtf8().constData(), 0);
        
        ret = avformat_open_input(&format_context_, device_path.toUtf8().constData(), inputFormat, &fallbackOptions);
        av_dict_free(&fallbackOptions);
    }
    
    // If everything fails, try minimal options
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
        qCWarning(log_ffmpeg_backend) << "Auto-detection failed:" << QString::fromUtf8(errbuf) << "- trying minimal options";
        
        // Reset format context
        if (format_context_) {
            avformat_close_input(&format_context_);
        }
        format_context_ = avformat_alloc_context();
        
        // Re-set interrupt callback
        format_context_->interrupt_callback.callback = FFmpegDeviceManager::InterruptCallback;
        format_context_->interrupt_callback.opaque = this;
        
        // Try with minimal options (just the device path)
        ret = avformat_open_input(&format_context_, device_path.toUtf8().constData(), inputFormat, nullptr);
    }
    
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
        qCCritical(log_ffmpeg_backend) << "Failed to open input device with all attempts:" << QString::fromUtf8(errbuf);
        return false;
    }
    
    qCDebug(log_ffmpeg_backend) << "Successfully opened device" << device_path;
#endif // Q_OS_WIN
    
    // LOW-LATENCY: Disable internal demuxer buffering right after open so av_read_frame
    // always returns the freshest packet rather than buffering ahead.
    format_context_->flags |= AVFMT_FLAG_NOBUFFER;
    
    // MINIMAL PROBE: For a live DirectShow/V4L2 MJPEG camera the codec parameters are
    // fully declared in the first packet.  Restrict find_stream_info to reading at most
    // one frame's worth of data so it does not consume real live frames (any frame read
    // by find_stream_info is buffered internally and replayed later → stale display).
    format_context_->probesize        = 1024 * 32;  // 32 KB – enough for one MJPEG frame header
    format_context_->max_analyze_duration = 0;       // No time-based analysis delay
    
    // Find stream info with minimal probe to avoid consuming live frames
    qCDebug(log_ffmpeg_backend) << "Finding stream info (minimal probe)...";
    int stream_ret = avformat_find_stream_info(format_context_, nullptr);
    if (stream_ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(stream_ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
        qCCritical(log_ffmpeg_backend) << "Failed to find stream info:" << QString::fromUtf8(errbuf);
        return false;
    }
    qCDebug(log_ffmpeg_backend) << "Stream info found successfully";
    
    return true;
}

bool FFmpegDeviceManager::FindVideoStream()
{
    // Find video stream
    video_stream_index_ = -1;
    for (unsigned int i = 0; i < format_context_->nb_streams; i++) {
        if (format_context_->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_index_ = i;
            break;
        }
    }
    
    if (video_stream_index_ == -1) {
        qCCritical(log_ffmpeg_backend) << "No video stream found";
        return false;
    }
    
    return true;
}

bool FFmpegDeviceManager::SetupDecoder(FFmpegHardwareAccelerator* hw_accelerator)
{
    // Get codec parameters
    AVCodecParameters* codecpar = format_context_->streams[video_stream_index_]->codecpar;
    
    // Find decoder - try hardware decoder for MJPEG if available
    const AVCodec* codec = nullptr;
    bool usingHwDecoder = false;
    
    // Try hardware decoder first if hardware acceleration is available
    if (hw_accelerator && hw_accelerator->IsHardwareAccelEnabled()) {
        bool hwDecoderFound = hw_accelerator->TryHardwareDecoder(codecpar, &codec, &usingHwDecoder);
        if (hwDecoderFound && codec) {
            qCInfo(log_ffmpeg_backend) << "✓✓✓ Successfully selected hardware decoder:" << codec->name << "✓✓✓";
        }
    }
    
    // Fallback to software decoder if hardware decoder not found
    if (!codec) {
        codec = avcodec_find_decoder(codecpar->codec_id);
        if (!codec) {
            qCCritical(log_ffmpeg_backend) << "Decoder not found for codec ID:" << codecpar->codec_id;
            return false;
        }
        const char* codecName = codec->name ? codec->name : "unknown";
        qCDebug(log_ffmpeg_backend) << "Using software decoder:" << codecName;
    }
    
    // Allocate codec context
    codec_context_ = avcodec_alloc_context3(codec);
    if (!codec_context_) {
        qCCritical(log_ffmpeg_backend) << "Failed to allocate codec context";
        return false;
    }
    
    // Copy codec parameters
    int ret = avcodec_parameters_to_context(codec_context_, codecpar);
    if (ret < 0) {
        qCCritical(log_ffmpeg_backend) << "Failed to copy codec parameters";
        return false;
    }
    
    // CRITICAL: Set codec options for quality - DISABLE FAST mode for better quality
    codec_context_->flags |= AV_CODEC_FLAG_LOW_DELAY;      // Enable low-delay decoding
    codec_context_->flags2 &= ~AV_CODEC_FLAG2_FAST;        // DISABLE fast decoding - prioritize quality over speed
    
    // Ensure full frame decoding (no frame skipping)
    codec_context_->skip_frame = AVDISCARD_NONE;           // Decode all frames
    codec_context_->skip_idct = AVDISCARD_NONE;            // Full IDCT
    codec_context_->skip_loop_filter = AVDISCARD_NONE;     // Full loop filter
    
    // ============ MJPEG-SPECIFIC QUALITY SETTINGS ============
    
    // Use optimal thread count based on CPU cores (max 8 for stability)
    // Dynamic threading for better CPU utilization
    int optimal_threads = std::min(8, std::max(2, QThread::idealThreadCount()));
    codec_context_->thread_count = optimal_threads;
    codec_context_->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;  // Enable both frame and slice threading
    
    // LATENCY FIX: Hardware decoders (QSV, CUDA) manage their own internal parallelism.
    // Setting FF_THREAD_FRAME at the FFmpeg host level forces it to buffer thread_count frames
    // before yielding the first decoded frame — that is thread_count/fps seconds of added latency.
    // Disable frame-level threading for hardware decoders to eliminate this buffering.
    if (usingHwDecoder) {
        codec_context_->thread_count = 1;
        codec_context_->thread_type = 0;  // No host-side frame threading for hardware decoders
        qCInfo(log_ffmpeg_backend) << "Hardware decoder: thread_count set to 1 (no frame-level host threading) to eliminate codec pipeline latency";
    }
    
    // Request full-range YUV output (better quality than limited range)
    codec_context_->pix_fmt = AV_PIX_FMT_YUVJ420P;
    
    // Disable experimental codec features, use normal compliance
    codec_context_->strict_std_compliance = FF_COMPLIANCE_NORMAL;
    
    // Preserve frame metadata for accurate rendering
    codec_context_->flags |= AV_CODEC_FLAG_COPY_OPAQUE;
    
    qCInfo(log_ffmpeg_backend) << "=== CODEC CONFIGURATION FOR QUALITY ===";
    qCInfo(log_ffmpeg_backend) << "Codec:" << (codec->name ? codec->name : "unknown");
    qCInfo(log_ffmpeg_backend) << "Thread count:" << optimal_threads << "(CPU cores:" << QThread::idealThreadCount() << ")";
    qCInfo(log_ffmpeg_backend) << "Thread type: FRAME + SLICE (hybrid threading)"; 
    qCInfo(log_ffmpeg_backend) << "Output pixel format:" << av_get_pix_fmt_name(codec_context_->pix_fmt);
    qCInfo(log_ffmpeg_backend) << "Skip frame: NONE (decode all)";
    qCInfo(log_ffmpeg_backend) << "Fast decoding: DISABLED (quality over speed)";
    qCInfo(log_ffmpeg_backend) << "Compliance: NORMAL";
    
    // Set hardware device context if using hardware decoder AND context is available
    AVBufferRef* hw_device_ctx = hw_accelerator ? hw_accelerator->GetHardwareDeviceContext() : nullptr;
    AVHWDeviceType hw_device_type = hw_accelerator ? hw_accelerator->GetHardwareDeviceType() : AV_HWDEVICE_TYPE_NONE;
    
    if (usingHwDecoder && hw_device_ctx) {
        codec_context_->hw_device_ctx = av_buffer_ref(hw_device_ctx);
        if (!codec_context_->hw_device_ctx) {
            qCWarning(log_ffmpeg_backend) << "Failed to reference hardware device context";
            // Continue with software fallback
            avcodec_free_context(&codec_context_);
            
            // Try again with software decoder
            codec = avcodec_find_decoder(codecpar->codec_id);
            if (!codec) {
                qCCritical(log_ffmpeg_backend) << "Software decoder not found for codec ID:" << codecpar->codec_id;
                return false;
            }
            
            codec_context_ = avcodec_alloc_context3(codec);
            if (!codec_context_) {
                qCCritical(log_ffmpeg_backend) << "Failed to allocate codec context for software decoder";
                return false;
            }
            
            ret = avcodec_parameters_to_context(codec_context_, codecpar);
            if (ret < 0) {
                qCCritical(log_ffmpeg_backend) << "Failed to copy codec parameters to software decoder";
                return false;
            }
            
            // CRITICAL: Set low-latency codec options for software decoder - quality focused
            codec_context_->flags |= AV_CODEC_FLAG_LOW_DELAY;
            codec_context_->flags2 &= ~AV_CODEC_FLAG2_FAST;  // DISABLE fast - quality over speed
            codec_context_->skip_frame = AVDISCARD_NONE;
            codec_context_->skip_idct = AVDISCARD_NONE;
            codec_context_->skip_loop_filter = AVDISCARD_NONE;
            
            // ============ MJPEG-SPECIFIC QUALITY SETTINGS FOR FALLBACK ============
            int fallback_threads = std::min(6, std::max(2, QThread::idealThreadCount() - 1));  // Leave 1 core for system
            codec_context_->thread_count = fallback_threads;
            codec_context_->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
            codec_context_->pix_fmt = AV_PIX_FMT_YUVJ420P;
            codec_context_->strict_std_compliance = FF_COMPLIANCE_NORMAL;
            codec_context_->flags |= AV_CODEC_FLAG_COPY_OPAQUE;
            
            usingHwDecoder = false;
            qCInfo(log_ffmpeg_backend) << "Falling back to software decoder:" << codec->name << "(" << fallback_threads << "threads, CPU cores:" << QThread::idealThreadCount() << ")"; 
        } else {
            const char* hwType = (hw_device_type == AV_HWDEVICE_TYPE_CUDA) ? "CUDA/NVDEC" : "QSV";
            qCInfo(log_ffmpeg_backend) << "✓" << hwType << "hardware device context set successfully";
        }
    } else if (usingHwDecoder && !hw_device_ctx) {
        // CUVID on Windows case - no device context needed
        const char* hwType = (hw_device_type == AV_HWDEVICE_TYPE_CUDA) ? "CUDA/NVDEC" : "Hardware";
        qCInfo(log_ffmpeg_backend) << "✓" << hwType << "decoder will be used without device context (normal for CUVID on Windows)";
    }
    
    // Prepare decoder options for CUDA/NVDEC optimization
    AVDictionary* codecOptions = nullptr;
    if (usingHwDecoder && hw_device_type == AV_HWDEVICE_TYPE_CUDA) {
        // CUDA/NVDEC specific options for ultra-low latency
        av_dict_set(&codecOptions, "gpu", "0", 0);  // Use first GPU
        av_dict_set(&codecOptions, "surfaces", "1", 0);  // Minimal surfaces for lowest latency
        av_dict_set(&codecOptions, "low_latency", "1", 0);  // Enable low latency mode
        av_dict_set(&codecOptions, "delay", "0", 0);  // No delay
        av_dict_set(&codecOptions, "rgb_mode", "1", 0);  // Output RGB directly from GPU
        
        qCInfo(log_ffmpeg_backend) << "Setting CUDA/NVDEC decoder options: gpu=0, surfaces=1, low_latency=1, delay=0, rgb_mode=1";
    }
    
    if (usingHwDecoder && hw_device_type == AV_HWDEVICE_TYPE_QSV) {
        // QSV (Intel Quick Sync) specific options for minimal latency.
        // async_depth=1 puts MFX into synchronous mode: only 1 frame is ever inside the
        // MFX async pipeline at a time.  The default async_depth is 4 which adds ~4/fps
        // seconds of internal pipeline latency (≈133ms at 30fps, worse at lower fps).
        av_dict_set(&codecOptions, "async_depth", "1", 0);
        qCInfo(log_ffmpeg_backend) << "Setting QSV decoder options: async_depth=1 (synchronous low-latency mode)";
    }
    
    // Open codec
    qCInfo(log_ffmpeg_backend) << "Attempting to open codec:" << codec->name;
    ret = avcodec_open2(codec_context_, codec, &codecOptions);
    
    // Log any unused options (helps debug configuration issues)
    if (codecOptions) {
        AVDictionaryEntry* entry = nullptr;
        while ((entry = av_dict_get(codecOptions, "", entry, AV_DICT_IGNORE_SUFFIX))) {
            qCWarning(log_ffmpeg_backend) << "Unused codec option:" << entry->key << "=" << entry->value;
        }
    }
    av_dict_free(&codecOptions);
    
    // Set low delay flags to prevent buffer overflow
    codec_context_->flags |= AV_CODEC_FLAG_LOW_DELAY;
    codec_context_->delay = 0;
    // Set video delay to 0 for the stream
    if (format_context_->streams[video_stream_index_]) {
        format_context_->streams[video_stream_index_]->codecpar->video_delay = 0;
    }
    
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
        
        // If hardware decoder fails, try software fallback
        if (usingHwDecoder) {
            AVHWDeviceType hw_type = hw_accelerator ? hw_accelerator->GetHardwareDeviceType() : AV_HWDEVICE_TYPE_NONE;
            const char* hwType = (hw_type == AV_HWDEVICE_TYPE_CUDA) ? "CUDA/NVDEC" : "QSV";
            qCWarning(log_ffmpeg_backend) << "✗ Failed to open" << hwType << "hardware codec:" 
                                          << QString::fromUtf8(errbuf);
            qCWarning(log_ffmpeg_backend) << "  - Falling back to software decoder...";
            
            avcodec_free_context(&codec_context_);
            
            // Try software decoder
            codec = avcodec_find_decoder(codecpar->codec_id);
            if (!codec) {
                qCCritical(log_ffmpeg_backend) << "Software decoder not found for codec ID:" << codecpar->codec_id;
                return false;
            }
            
            codec_context_ = avcodec_alloc_context3(codec);
            if (!codec_context_) {
                qCCritical(log_ffmpeg_backend) << "Failed to allocate codec context for software decoder";
                return false;
            }
            
            ret = avcodec_parameters_to_context(codec_context_, codecpar);
            if (ret < 0) {
                qCCritical(log_ffmpeg_backend) << "Failed to copy codec parameters to software decoder";
                return false;
            }
            
            // CRITICAL: Set low-latency codec options for software decoder fallback
            codec_context_->flags |= AV_CODEC_FLAG_LOW_DELAY;
            codec_context_->flags2 |= AV_CODEC_FLAG2_FAST;
            codec_context_->thread_count = 1;
            
            ret = avcodec_open2(codec_context_, codec, nullptr);
            if (ret < 0) {
                av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
                qCCritical(log_ffmpeg_backend) << "Failed to open software codec:" << QString::fromUtf8(errbuf);
                return false;
            }
            
            usingHwDecoder = false;
            qCInfo(log_ffmpeg_backend) << "✓ Successfully opened software decoder:" << codec->name;
        } else {
            qCCritical(log_ffmpeg_backend) << "Failed to open codec:" << QString::fromUtf8(errbuf);
            return false;
        }
    } else if (usingHwDecoder) {
        // Hardware codec opened successfully - log details
        qCInfo(log_ffmpeg_backend) << "✓✓✓ Successfully opened hardware codec:" << codec->name << "✓✓✓";
        qCInfo(log_ffmpeg_backend) << "  - Codec pixel format:" << codec_context_->pix_fmt 
                                   << "(" << av_get_pix_fmt_name(codec_context_->pix_fmt) << ")";
        qCInfo(log_ffmpeg_backend) << "  - Codec capabilities:" << codec->capabilities;
        
        // Check if this codec outputs hardware frames
        if (codec->capabilities & AV_CODEC_CAP_HARDWARE) {
            qCInfo(log_ffmpeg_backend) << "  - Codec has AV_CODEC_CAP_HARDWARE capability";
        }
    }
    
    qCDebug(log_ffmpeg_backend) << "Decoder setup completed successfully:"
                                << "codec=" << codec->name
                                << "hw_accel=" << (usingHwDecoder && hw_accelerator ? 
                                    av_hwdevice_get_type_name(hw_accelerator->GetHardwareDeviceType()) : "none")
                                << "codec_id=" << codecpar->codec_id
                                << "resolution=" << codecpar->width << "x" << codecpar->height
                                << "pixel_format=" << codecpar->format;
    
    return true;
}

void FFmpegDeviceManager::WarmUpHardwareDecoder()
{
    // Read and decode one real frame to force the hardware codec (QSV/CUDA) to create
    // its internal GPU session synchronously here, inside OpenDevice, rather than lazily
    // on the first call from the capture thread.
    //
    // Without this, mjpeg_qsv allocates its MFX session on the very first
    // avcodec_send_packet call in the capture thread (~200-800 ms), during which the
    // camera fills the ring buffer again — exactly the stale-frame latency bug we fixed
    // with DrainBufferedPackets.  By forcing init here, DrainBufferedPackets runs
    // AFTER the session is ready and its drain truly reaches the live edge.

    if (!format_context_ || !codec_context_ || video_stream_index_ < 0) {
        return;
    }

    qCInfo(log_ffmpeg_backend) << "Warm-up: forcing hardware codec session init...";
    QElapsedTimer warmUpTimer;
    warmUpTimer.start();

    // Use a per-read timeout identical to DrainBufferedPackets.
    static constexpr qint64 kWarmUpReadTimeoutMs = 120;
    qint64 savedStart = operation_start_time_;
    bool savedInterrupt = interrupt_requested_;

    AVPacket* pkt   = av_packet_alloc();
    AVFrame*  frame = av_frame_alloc();

    if (!pkt || !frame) {
        av_packet_free(&pkt);
        av_frame_free(&frame);
        operation_start_time_  = savedStart;
        interrupt_requested_   = savedInterrupt;
        return;
    }

    bool sent = false;
    const int maxAttempts = 30; // limit read attempts (~1 sec at 30fps)
    for (int attempt = 0; attempt < maxAttempts && !sent; ++attempt) {
        operation_start_time_ = QDateTime::currentMSecsSinceEpoch()
                                 - (kOperationTimeoutMs - kWarmUpReadTimeoutMs);
        interrupt_requested_  = false;

        int ret = av_read_frame(format_context_, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EXIT) {
            continue;
        }
        if (ret < 0) {
            break;
        }

        if (pkt->stream_index != video_stream_index_) {
            av_packet_unref(pkt);
            continue;
        }

        // Send packet — this is the call that triggers QSV session creation
        ret = avcodec_send_packet(codec_context_, pkt);
        av_packet_unref(pkt);

        if (ret == 0 || ret == AVERROR(EAGAIN)) {
            sent = true;
            // Drain the decoded frame so the codec is in a clean state
            avcodec_receive_frame(codec_context_, frame);
        }
    }

    // Flush any partial decode state
    avcodec_flush_buffers(codec_context_);

    av_frame_free(&frame);
    av_packet_free(&pkt);

    operation_start_time_ = savedStart;
    interrupt_requested_  = savedInterrupt;

    qCInfo(log_ffmpeg_backend) << "Warm-up complete in" << warmUpTimer.elapsed() << "ms"
                               << "(sent:" << sent << ")";
}

void FFmpegDeviceManager::DrainBufferedPackets(bool using_hw_decoder)
{
    // Discard packets that accumulated in the DirectShow ring buffer while the codec
    // (especially QSV/CUDA) was initialising.  Without this the capture thread would
    // replay those stale frames, producing the 2-3 second display lag.
    //
    // Key insight about timing each av_read_frame call:
    //
    //   • Buffered (stale) packet — DirectShow ring buffer has data ready:
    //       av_read_frame returns in < 2 ms (simple ring buffer memcpy, no camera wait).
    //
    //   • Live packet — ring buffer empty, must wait for next camera frame:
    //       av_read_frame blocks for ≥ 1/fps seconds:
    //         30 fps → ~33 ms,  60 fps → ~16 ms,  120 fps → ~8 ms.
    //
    // The previous threshold was kDrainReadTimeoutMs/2 = 60 ms.  At 60 fps a LIVE
    // frame takes only ~16 ms → 16 < 60 → drain never terminates on the live edge
    // and instead consumes the full totalBudgetMs (600 ms) worth of real live frames,
    // introducing ≈600 ms of startup latency.
    //
    // New threshold: 8 ms.  A buffered memcpy is ≤1-2 ms.  The slowest plausible
    // camera (120 fps) delivers a live frame every ~8 ms.  Using 8 ms as the cutpoint
    // correctly stops the drain the moment the ring buffer is empty at any frame rate
    // up to 120 fps, without being fooled by OS jitter on buffered reads.

    if (!format_context_) {
        return;
    }

    // Safety cap: if codec took an unusually long time to init (e.g. slow QSV driver
    // probe), allow up to 10 s of draining.  With the 8 ms threshold we will stop
    // naturally at the live edge long before this budget is exhausted.
    const qint64 totalBudgetMs = 10000;
    const int    maxPackets    = 5000;

    // Live-edge threshold: stop draining when any single av_read_frame takes ≥ 8 ms
    // (means it had to wait for the camera = we are at the live edge).
    static constexpr qint64 kLiveEdgeThresholdMs = 8;

    // Use a generous per-read interrupt timeout so the InterruptCallback does not
    // fire during a normal ~33 ms camera wait at the live edge.
    static constexpr qint64 kDrainInterruptTimeoutMs = 200;

    qint64 savedOperationStartTime = operation_start_time_;
    bool   savedInterruptRequested = interrupt_requested_;

    AVPacket* pkt = av_packet_alloc();
    if (!pkt) {
        return;
    }

    QElapsedTimer totalTimer;
    totalTimer.start();
    int drained = 0;

    while (drained < maxPackets && totalTimer.elapsed() < totalBudgetMs) {
        // Back-date start time so InterruptCallback fires after kDrainInterruptTimeoutMs.
        operation_start_time_ = QDateTime::currentMSecsSinceEpoch()
                                 - (kOperationTimeoutMs - kDrainInterruptTimeoutMs);
        interrupt_requested_  = false;

        QElapsedTimer readTimer;
        readTimer.start();

        int ret = av_read_frame(format_context_, pkt);

        qint64 readMs = readTimer.elapsed();

        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EXIT || ret < 0) {
            // EAGAIN / error — DirectShow says "no data yet".  We are at live edge.
            break;
        }

        av_packet_unref(pkt);
        drained++;

        // If this read took >= kLiveEdgeThresholdMs the ring buffer was empty and
        // we had to wait for the camera → we are now at the live edge.  Stop here.
        if (readMs >= kLiveEdgeThresholdMs) {
            qCDebug(log_ffmpeg_backend) << "Drain: read blocked for" << readMs
                                        << "ms — reached live edge after draining" << drained << "packets";
            break;
        }
    }

    av_packet_free(&pkt);

    // Restore interrupt state for normal operation.
    operation_start_time_ = savedOperationStartTime;
    interrupt_requested_  = savedInterruptRequested;

    if (drained > 0) {
        qCInfo(log_ffmpeg_backend) << "Drained" << drained
                                   << "stale buffered packets after codec init ("
                                   << totalTimer.elapsed() << "ms,"
                                   << "hw_decoder:" << using_hw_decoder << ")";
    }
}
