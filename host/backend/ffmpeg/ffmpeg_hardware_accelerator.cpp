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

#include "ffmpeg_hardware_accelerator.h"
#include <QLoggingCategory>
#include <cstring>

extern "C" {
#include <libavcodec/avcodec.h>
}

Q_DECLARE_LOGGING_CATEGORY(log_ffmpeg_backend)

FFmpegHardwareAccelerator::FFmpegHardwareAccelerator()
    : hw_device_context_(nullptr)
    , hw_device_type_(AV_HWDEVICE_TYPE_NONE)
    , preferred_hw_accel_("auto")
{
}

FFmpegHardwareAccelerator::~FFmpegHardwareAccelerator()
{
    Cleanup();
}

bool FFmpegHardwareAccelerator::Initialize(const QString& preferred_hw_accel)
{
    qCDebug(log_ffmpeg_backend) << "Initializing hardware acceleration, preferred:" << preferred_hw_accel;
    
    preferred_hw_accel_ = preferred_hw_accel;
    
    // Explicitly handle CPU-only mode
    if (preferred_hw_accel_ == "none") {
        qCInfo(log_ffmpeg_backend) << "Hardware acceleration disabled - using CPU decoding";
        hw_device_context_ = nullptr;
        hw_device_type_ = AV_HWDEVICE_TYPE_NONE;
        return true;
    }
    
    // For MJPEG decoding on Windows, CUVID decoders work differently than on Linux
    // They can be used directly without creating a hardware device context first
    // We just need to verify the decoder is available
    
    // Priority order for MJPEG hardware decoders:
    // 1. NVIDIA CUVID (mjpeg_cuvid) - works on Windows without device context
    // 2. Intel QSV (mjpeg_qsv) - may need device context on some platforms
    
    HwDecoderInfo hw_decoders[] = {
        {"CUDA/NVDEC", "mjpeg_cuvid", AV_HWDEVICE_TYPE_CUDA, false, "cuda"},
        {"Intel QSV", "mjpeg_qsv", AV_HWDEVICE_TYPE_QSV, false, "qsv"},
        {nullptr, nullptr, AV_HWDEVICE_TYPE_NONE, false, ""}
    };
    
    // If not auto, try the preferred one first
    if (preferred_hw_accel_ != "auto") {
        for (int i = 0; hw_decoders[i].name != nullptr; i++) {
            if (hw_decoders[i].setting_name == preferred_hw_accel_) {
                if (TryInitializeHwDecoder(hw_decoders[i])) {
                    return true;
                }
            }
        }
        qCWarning(log_ffmpeg_backend) << "Preferred hardware acceleration" << preferred_hw_accel_ 
                                       << "not available, falling back to auto";
    }
    
    // Auto mode or fallback: try all available
    for (int i = 0; hw_decoders[i].name != nullptr; i++) {
        if (TryInitializeHwDecoder(hw_decoders[i])) {
            return true;
        }
    }
    
    qCWarning(log_ffmpeg_backend) << "No MJPEG-capable hardware acceleration found - using software decoding";
    qCInfo(log_ffmpeg_backend) << "  - For NVIDIA GPU: Ensure latest drivers are installed and FFmpeg is built with --enable-cuda --enable-cuvid --enable-nvdec";
    qCInfo(log_ffmpeg_backend) << "  - For Intel GPU: Ensure QSV drivers are installed and FFmpeg is built with --enable-libmfx";
    hw_device_context_ = nullptr;
    hw_device_type_ = AV_HWDEVICE_TYPE_NONE;
    return false;
}

bool FFmpegHardwareAccelerator::TryInitializeHwDecoder(const HwDecoderInfo& decoder)
{
    qCInfo(log_ffmpeg_backend) << "Checking for" << decoder.name << "hardware decoder...";
    
    // First check if the decoder itself is available
    const AVCodec* test_codec = avcodec_find_decoder_by_name(decoder.decoder_name);
    if (!test_codec) {
        qCInfo(log_ffmpeg_backend) << "  ✗" << decoder.decoder_name << "decoder not found in this FFmpeg build";
        return false;
    }
    
    qCInfo(log_ffmpeg_backend) << "  ✓ Found" << decoder.decoder_name << "decoder";
    
    // For decoders that need a device context, try to create it
    if (decoder.needs_device_context) {
        hw_device_type_ = decoder.device_type;
        
        int ret = av_hwdevice_ctx_create(&hw_device_context_, decoder.device_type, nullptr, nullptr, 0);
        if (ret < 0) {
            char errbuf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, errbuf, sizeof(errbuf));
            qCInfo(log_ffmpeg_backend) << "  ✗ Failed to create hardware device context:" << errbuf;
            hw_device_context_ = nullptr;
            hw_device_type_ = AV_HWDEVICE_TYPE_NONE;
            return false;
        }
        qCInfo(log_ffmpeg_backend) << "  ✓ Hardware device context created";
    } else {
        // For CUVID on Windows, we don't need a device context
        qCInfo(log_ffmpeg_backend) << "  ℹ This decoder doesn't require a device context";
        hw_device_type_ = decoder.device_type;
        hw_device_context_ = nullptr;  // Explicitly set to nullptr for CUVID
    }
    
    qCInfo(log_ffmpeg_backend) << "✓✓✓ Successfully initialized" << decoder.name 
                               << "hardware acceleration for MJPEG decoding ✓✓✓";
    return true;
}

void FFmpegHardwareAccelerator::Cleanup()
{
    if (hw_device_context_) {
        qCDebug(log_ffmpeg_backend) << "Cleaning up hardware device context";
        av_buffer_unref(&hw_device_context_);
        hw_device_context_ = nullptr;
    }
    hw_device_type_ = AV_HWDEVICE_TYPE_NONE;
}

void FFmpegHardwareAccelerator::UpdatePreferredAcceleration(const QString& preferred_hw_accel)
{
    preferred_hw_accel_ = preferred_hw_accel;
    qCDebug(log_ffmpeg_backend) << "Updated preferred hardware acceleration to:" << preferred_hw_accel_;
}

bool FFmpegHardwareAccelerator::TryHardwareDecoder(const AVCodecParameters* codec_params, 
                                                    const AVCodec** out_codec, 
                                                    bool* out_using_hw_decoder)
{
    // Allow trying hardware decoder even without device context (needed for CUVID on Windows)
    if (hw_device_type_ == AV_HWDEVICE_TYPE_NONE || !codec_params || !out_codec || !out_using_hw_decoder) {
        return false;
    }
    
    *out_codec = nullptr;
    *out_using_hw_decoder = false;
    
    // Only try hardware acceleration for MJPEG
    if (codec_params->codec_id != AV_CODEC_ID_MJPEG) {
        return false;
    }
    
    // Map hardware device types to MJPEG decoder names
    const char* hw_decoder_name = nullptr;
    const char* hw_device_type_name = av_hwdevice_get_type_name(hw_device_type_);
    
    if (strcmp(hw_device_type_name, "cuda") == 0) {
        hw_decoder_name = "mjpeg_cuvid";
        qCInfo(log_ffmpeg_backend) << "Attempting to use NVIDIA NVDEC/CUVID for MJPEG decoding";
    } else if (strcmp(hw_device_type_name, "qsv") == 0) {
        hw_decoder_name = "mjpeg_qsv";
        qCInfo(log_ffmpeg_backend) << "Attempting to use Intel QSV for MJPEG decoding";
    } else {
        qCWarning(log_ffmpeg_backend) << "Unknown hardware device type:" << hw_device_type_name;
        qCWarning(log_ffmpeg_backend) << "No MJPEG hardware decoder available for this device type";
        return false;
    }
    
    qCInfo(log_ffmpeg_backend) << "Looking for hardware decoder:" << hw_decoder_name;
    *out_codec = avcodec_find_decoder_by_name(hw_decoder_name);
    
    if (*out_codec) {
        qCInfo(log_ffmpeg_backend) << "✓ Found" << hw_decoder_name << "hardware decoder";
        qCInfo(log_ffmpeg_backend) << "  - Codec long name:" << (*out_codec)->long_name;
        qCInfo(log_ffmpeg_backend) << "  - This will offload MJPEG decoding to GPU";
        *out_using_hw_decoder = true;
        return true;
    } else {
        qCWarning(log_ffmpeg_backend) << "✗ Hardware decoder" << hw_decoder_name << "not found";
        qCWarning(log_ffmpeg_backend) << "  - Your FFmpeg build may not include" << hw_decoder_name << "support";
        return false;
    }
}
