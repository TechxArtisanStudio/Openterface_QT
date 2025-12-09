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

extern "C" {
#include <libavdevice/avdevice.h>
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
    
    // CRITICAL LOW-LATENCY OPTIMIZATIONS for KVM responsiveness:
    av_dict_set(&options, "rtbufsize", "10000000", 0);        // 10MB buffer to prevent overflow
    av_dict_set(&options, "fflags", "discardcorrupt", 0); // Discard corrupt frames only
    av_dict_set(&options, "flags", "low_delay", 0);       // Enable low delay mode
    av_dict_set(&options, "max_delay", "2000", 0);           // Allow 3ms delay for stability
    av_dict_set(&options, "probesize", "32", 0);          // Minimal probe size for fastest start (minimum allowed)
    av_dict_set(&options, "analyzeduration", "0", 0);     // Skip analysis to reduce startup delay
    
    // CRITICAL FIX: Add timeout to prevent blocking on device reconnection
    av_dict_set(&options, "timeout", "5000000", 0);       // 5 second timeout in microseconds
    
    // Try to set video format - DirectShow supports various formats
    // Try MJPEG first for best performance
    av_dict_set(&options, "vcodec", "mjpeg", 0);
    
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
        av_dict_set(&fallbackOptions, "rtbufsize", "100M", 0);
        
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
    
    // CRITICAL FIX: Set strict timeout for stream info to prevent blocking on device reconnection
    // This prevents the app from hanging when a device is unplugged and replugged
    format_context_->max_analyze_duration = 1000000; // 1 second max (in microseconds)
    format_context_->probesize = 5000000; // 5MB max probe size
    
    // Find stream info with timeout protection
    qCDebug(log_ffmpeg_backend) << "Finding stream info (max 1 second)...";
    int stream_ret = avformat_find_stream_info(format_context_, nullptr);
    if (stream_ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(stream_ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
        qCCritical(log_ffmpeg_backend) << "Failed to find stream info:" << QString::fromUtf8(errbuf);
        return false;
    }
    qCDebug(log_ffmpeg_backend) << "Stream info found successfully";
    
    // Set real-time flags to prevent buffer overflow
    format_context_->max_analyze_duration = 50000; // 0.05 seconds
    format_context_->probesize = 1000000; // 1MB
    
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
    
    // CRITICAL: Set low-latency codec options before opening
    codec_context_->flags |= AV_CODEC_FLAG_LOW_DELAY;      // Enable low-delay decoding
    codec_context_->flags2 |= AV_CODEC_FLAG2_FAST;         // Prioritize speed over quality
    
    // Use single-threading for lower latency
    codec_context_->thread_count = 1;
    
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
            
            // CRITICAL: Set low-latency codec options for software decoder
            codec_context_->flags |= AV_CODEC_FLAG_LOW_DELAY;
            codec_context_->flags2 |= AV_CODEC_FLAG2_FAST;
            codec_context_->thread_count = 1;
            
            usingHwDecoder = false;
            qCInfo(log_ffmpeg_backend) << "Falling back to software decoder:" << codec->name;
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
