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

#ifndef FFMPEG_CAPTURE_MANAGER_H
#define FFMPEG_CAPTURE_MANAGER_H

#include <QObject>
#include <QString>
#include <QSize>
#include <QMutex>
#include <QTimer>
#include <memory>
#include "icapture_frame_reader.h"

// Forward declarations
class CaptureThread;
class FFmpegDeviceManager;
class FFmpegHardwareAccelerator;
class FFmpegDeviceValidator;

#ifdef HAVE_FFMPEG
#include "ffmpegutils.h"
extern "C" {
struct AVPacket;
}
#else
struct AVPacket;
#endif

/**
 * @brief Manages FFmpeg capture lifecycle including thread management, resource cleanup, and interrupt handling
 * 
 * This class encapsulates capture lifecycle logic including:
 * - Starting and stopping capture with thread management
 * - Interrupt handling for FFmpeg operations
 * - Capture thread lifecycle management
 * - Frame reading coordination
 * - Resource cleanup on capture stop
 * - Performance monitoring integration
 */
class FFmpegCaptureManager : public QObject, public ICaptureFrameReader
{
    Q_OBJECT

public:
    explicit FFmpegCaptureManager(FFmpegDeviceManager* deviceManager,
                                  FFmpegHardwareAccelerator* hardwareAccelerator,
                                  FFmpegDeviceValidator* deviceValidator,
                                  QObject* parent = nullptr);
    ~FFmpegCaptureManager();

    // Capture lifecycle
    bool StartCapture(const QString& devicePath, const QSize& resolution, int framerate);
    void StopCapture();
    bool IsRunning() const { return capture_running_; }
    
    // ICaptureFrameReader interface implementation
    bool readFrame() override { return ReadFrame(); }
    
    // Frame reading
    bool ReadFrame();
    
    // Packet access (for frame processing)
#ifdef HAVE_FFMPEG
    AVPacket* GetPacket() { return AV_PACKET_RAW(packet_); }
#else
    AVPacket* GetPacket() { return packet_; }
#endif
    
    // Video stream info
    int GetVideoStreamIndex() const { return video_stream_index_; }
    
    // Current device info
    QString GetCurrentDevice() const { return current_device_; }
    QSize GetCurrentResolution() const { return current_resolution_; }
    int GetCurrentFramerate() const { return current_framerate_; }
    
    // Interrupt handling (static callback for FFmpeg)
    static int InterruptCallback(void* ctx);
    
    // Performance monitoring
    void SetPerformanceTimer(QTimer* timer) { performance_timer_ = timer; }

signals:
    void FrameAvailable();
    void DeviceDisconnected();
    void CaptureError(const QString& error);
    void CaptureStarted(const QString& devicePath);
    void CaptureStopped();

private:
    bool OpenInputDevice(const QString& devicePath, const QSize& resolution, int framerate);
    void CloseInputDevice();
    void CleanupResources();
    bool InitializeCaptureThread();
    void StopCaptureThread();
    
    // Component references (not owned)
    FFmpegDeviceManager* device_manager_;
    FFmpegHardwareAccelerator* hardware_accelerator_;
    FFmpegDeviceValidator* device_validator_;
    
    // Capture thread
    std::unique_ptr<CaptureThread> capture_thread_;
    
    // Packet handling
#ifdef HAVE_FFMPEG
    AvPacketPtr packet_;
#else
    AVPacket* packet_;
#endif
    
    // Capture state
    bool capture_running_;
    int video_stream_index_;
    QString current_device_;
    QSize current_resolution_;
    int current_framerate_;
    
    // Interrupt handling
    volatile bool interrupt_requested_;
    qint64 operation_start_time_;
    static constexpr qint64 kOperationTimeoutMs = 5000;
    
    // Performance monitoring (not owned)
    QTimer* performance_timer_;
    
    // Thread safety
    mutable QMutex mutex_;
};

#endif // FFMPEG_CAPTURE_MANAGER_H
