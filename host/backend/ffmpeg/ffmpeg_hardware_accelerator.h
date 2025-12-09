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

#ifndef FFMPEG_HARDWARE_ACCELERATOR_H
#define FFMPEG_HARDWARE_ACCELERATOR_H

#include <QString>

#ifdef HAVE_FFMPEG
extern "C" {
#include <libavutil/hwcontext.h>
#include <libavcodec/avcodec.h>
}
#endif

/**
 * @brief Manages FFmpeg hardware acceleration for video decoding
 * 
 * This class encapsulates all hardware acceleration logic including:
 * - Hardware decoder detection and initialization
 * - Hardware device context management
 * - Fallback to software decoding when hardware is unavailable
 */
class FFmpegHardwareAccelerator {
public:
    FFmpegHardwareAccelerator();
    ~FFmpegHardwareAccelerator();

    // Initialization
    bool Initialize(const QString& preferred_hw_accel);
    void Cleanup();
    
    // Hardware decoder selection
    bool TryHardwareDecoder(const AVCodecParameters* codec_params,
                           const AVCodec** out_codec,
                           bool* out_using_hw_decoder);
    
    // Hardware context management
    AVBufferRef* GetHardwareDeviceContext() { return hw_device_context_; }
    AVHWDeviceType GetHardwareDeviceType() const { return hw_device_type_; }
    bool IsHardwareAccelEnabled() const { return hw_device_type_ != AV_HWDEVICE_TYPE_NONE; }
    
    // Update settings
    void UpdatePreferredAcceleration(const QString& preferred_hw_accel);

private:
    struct HwDecoderInfo {
        const char* name;
        const char* decoder_name;
        AVHWDeviceType device_type;
        bool needs_device_context;
        QString setting_name;
    };
    
    bool TryInitializeHwDecoder(const HwDecoderInfo& decoder);
    
    AVBufferRef* hw_device_context_;
    AVHWDeviceType hw_device_type_;
    QString preferred_hw_accel_;
};

#endif // FFMPEG_HARDWARE_ACCELERATOR_H
