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

#ifndef FFMPEGBACKENDHANDLER_H
#define FFMPEGBACKENDHANDLER_H

#include "../multimediabackend.h"
#include <QThread>
#include <QLoggingCategory>
Q_DECLARE_LOGGING_CATEGORY(log_ffmpeg_backend)
#include <QTimer>
#include <QMutex>
#include <QWaitCondition>
#include <QPixmap>
#include <memory>

// Forward declarations for Qt types
class QGraphicsVideoItem;
class VideoPane;
class HotplugMonitor;
struct DeviceInfo;
struct DeviceChangeEvent;

// Forward declare CaptureThread class used by this backend
class CaptureThread;

// Forward declarations for FFmpeg types (conditional compilation)
#ifdef HAVE_FFMPEG
extern "C" {
    #include <libavutil/hwcontext.h>
}
// AVFrame/AVPacket unique_ptr helpers
#include "ffmpeg/ffmpegutils.h"
struct AVFormatContext;
struct AVCodecContext;
struct AVCodecParameters;
struct AVCodec;
struct AVFrame;
struct AVPacket;
struct SwsContext;
struct AVStream;
struct AVOutputFormat;
struct AVBufferRef;
#else
// If FFmpeg headers are not available, still provide minimal forward decls
struct AVFrame;
struct AVPacket;
#endif

// Forward declarations for libjpeg-turbo (conditional compilation)
#ifdef HAVE_LIBJPEG_TURBO
typedef void* tjhandle;
#endif

/**
 * @brief FFmpeg backend handler implementation with direct video decoding
 */
class FFmpegBackendHandler : public MultimediaBackendHandler
{
    Q_OBJECT

public:
    explicit FFmpegBackendHandler(QObject *parent = nullptr);
    ~FFmpegBackendHandler();

    MultimediaBackendType getBackendType() const override;
    QString getBackendName() const override;
    QStringList getAvailableHardwareAccelerations() const override;
    MultimediaBackendConfig getDefaultConfig() const override;

    void prepareCameraCreation() override;
    void configureCameraDevice() override;
    void setupCaptureSession(QMediaCaptureSession* session) override;
    void prepareVideoOutputConnection(QMediaCaptureSession* session, QObject* videoOutput) override;
    void finalizeVideoOutputConnection(QMediaCaptureSession* session, QObject* videoOutput) override;
    void startCamera() override;
    void stopCamera() override;
    
    QCameraFormat selectOptimalFormat(const QList<QCameraFormat>& formats, 
                                    const QSize& resolution, 
                                    int desiredFrameRate,
                                    QVideoFrameFormat::PixelFormat pixelFormat) const override;

    // Direct FFmpeg capture methods
    bool startDirectCapture(const QString& devicePath, const QSize& resolution, int framerate);
    void stopDirectCapture();
public slots:
    void processFrame();
    bool isDirectCaptureRunning() const;

public:

    // Recording configuration
    struct RecordingConfig {
        QString outputPath;
        QString format = "mp4";          // mp4, avi, mov
        QString videoCodec = "libx264";   // libx264, libx265, mpeg4
        int videoBitrate = 2000000;       // 2 Mbps default
        int videoQuality = 23;            // CRF value for x264 (lower = better quality)
        bool useHardwareAcceleration = false;
    };

    // Video recording methods
    bool startRecording(const QString& outputPath, const QString& format = "mp4", int videoBitrate = 2000000);
    bool stopRecording();
    void pauseRecording();
    void resumeRecording();
    bool isRecording() const;
    bool isPaused() const;
    QString getCurrentRecordingPath() const;
    qint64 getRecordingDuration() const; // in milliseconds
    
    // Advanced recording methods
    bool isCameraReady() const;
    bool supportsAdvancedRecording() const;
    bool startRecordingAdvanced(const QString& outputPath, const RecordingConfig& config);
    bool forceStopRecording();
    QString getLastError() const;
    
    // Recording statistics
    bool supportsRecordingStats() const;
    qint64 getRecordingFileSize() const;
    
    void setRecordingConfig(const RecordingConfig& config);
    RecordingConfig getRecordingConfig() const;

    // Image capture methods
    void takeImage(const QString& filePath);
    void takeAreaImage(const QString& filePath, const QRect& captureArea);

    // Update preferred hardware acceleration from settings
    void updatePreferredHardwareAcceleration();

    // Device availability and hotplug support
    bool checkCameraAvailable(const QString& devicePath = "");
    bool isCurrentDeviceAvailable() const;
    void handleDeviceDisconnection();
    bool restartCaptureWithDevice(const QString& devicePath, const QSize& resolution, int framerate);
    
    // Enhanced hotplug support with waiting capabilities
    void connectToHotplugMonitor();
    void disconnectFromHotplugMonitor();
    void waitForDeviceActivation(const QString& devicePath = "", int timeoutMs = 30000);
    void handleDeviceActivation(const QString& devicePath, const QString& portChain = QString());
    void handleDeviceDeactivation(const QString& devicePath);
    void setCurrentDevicePortChain(const QString& portChain);  // Set port chain for current device
    void setCurrentDevice(const QString& devicePath);  // Set current device path
    
    // Stub for MOC compatibility (might be leftover from autocomplete)
    void checkDeviceReconnection() { /* stub */ }

    bool readFrame();
    QPixmap convertFrameToPixmap(AVFrame* frame);
    QPixmap decodeFrame(AVPacket* packet);
    
#ifdef HAVE_LIBJPEG_TURBO
    QPixmap decodeJpegFrame(const uint8_t* data, int size);
#endif

    void setVideoOutput(QGraphicsVideoItem* videoItem);
    void setVideoOutput(VideoPane* videoPane);

signals:
    void frameReady(const QPixmap& frame);
    void captureError(const QString& error);
    void deviceConnectionChanged(const QString& devicePath, bool connected);
    void deviceActivated(const QString& devicePath);
    void deviceDeactivated(const QString& devicePath);
    void waitingForDevice(const QString& devicePath);
    
    // Recording signals
    void recordingStarted(const QString& outputPath);
    void recordingStopped();
    void recordingPaused();
    void recordingResumed();
    void recordingError(const QString& error);
    void recordingDurationChanged(qint64 duration);

private:
    // FFmpeg interrupt callback (needs access to private members)
    static int interruptCallback(void* ctx);
    
    // FFmpeg context management
    bool initializeFFmpeg();
    void cleanupFFmpeg();
    void cleanupFFmpegResources();
    bool openInputDevice(const QString& devicePath, const QSize& resolution, int framerate);
    void closeInputDevice();
    
    // Hardware acceleration
    bool initializeHardwareAcceleration();
    void cleanupHardwareAcceleration();
    bool tryHardwareDecoder(const AVCodecParameters* codecpar, const AVCodec** outCodec, bool* outUsingHwDecoder);
    
    struct HwDecoderInfo {
        const char* name;
        const char* decoderName;
        AVHWDeviceType deviceType;
        bool needsDeviceContext;
        QString settingName;
    };
    bool tryInitializeHwDecoder(const HwDecoderInfo& decoder);
    
    // Device capability detection
    struct CameraCapability {
        QSize resolution;
        int framerate;
        CameraCapability() : resolution(0, 0), framerate(0) {}
        CameraCapability(const QSize& res, int fps) : resolution(res), framerate(fps) {}
    };
    bool getMaxCameraCapability(const QString& devicePath, CameraCapability& capability);
    
    // Interrupt handling for FFmpeg operations
    volatile bool m_interruptRequested;
    qint64 m_operationStartTime;
    static constexpr qint64 FFMPEG_OPERATION_TIMEOUT_MS = 5000; // 5 second timeout
    
    // Recording-specific methods
    bool initializeRecording();
    void cleanupRecording();
    bool writeFrameToFile(AVFrame* frame);
    void finalizeRecording();
    bool configureEncoder(const QSize& resolution, int framerate);

#ifdef HAVE_LIBJPEG_TURBO
    // Note: decodeJpegFrame is declared in public section above
#endif
    
    // Threading
    friend class CaptureThread; // Allow capture thread to access private members (safe - it needs access for device/state)
    std::unique_ptr<CaptureThread> m_captureThread;
    
    // FFmpeg components
#ifdef HAVE_FFMPEG
    AVFormatContext* m_formatContext;
    AVCodecContext* m_codecContext;
    // Use RAII-managed smart pointers for AVFrame/AVPacket
    AvFramePtr m_frame;
    AvFramePtr m_frameRGB;
    AvPacketPtr m_packet;
    SwsContext* m_swsContext;
#else
    AVFormatContext* m_formatContext;
    AVCodecContext* m_codecContext;
    AVFrame* m_frame;
    AVFrame* m_frameRGB;
    AVPacket* m_packet;
    SwsContext* m_swsContext;
#endif
    
    // Hardware acceleration (QSV)
    AVBufferRef* m_hwDeviceContext;
    enum AVHWDeviceType m_hwDeviceType;
    
    // Recording components
#ifdef HAVE_FFMPEG
    AVFormatContext* m_recordingFormatContext;
    AVCodecContext* m_recordingCodecContext;
    AVStream* m_recordingVideoStream;
    SwsContext* m_recordingSwsContext;
    AvFramePtr m_recordingFrame;
    AvPacketPtr m_recordingPacket;
#else
    AVFormatContext* m_recordingFormatContext;
    AVCodecContext* m_recordingCodecContext;
    AVStream* m_recordingVideoStream;
    SwsContext* m_recordingSwsContext;
    AVFrame* m_recordingFrame;
    AVPacket* m_recordingPacket;
#endif

    
#ifdef HAVE_LIBJPEG_TURBO
    // TurboJPEG components
    tjhandle m_turboJpegHandle;
#endif
    
    // State management
    QString m_currentDevice;
    QString m_currentDevicePortChain;  // Track port chain for hotplug detection
    QSize m_currentResolution;
    int m_currentFramerate;
    bool m_captureRunning;
    int m_videoStreamIndex;
    
    // Hardware acceleration preference
    QString m_preferredHwAccel;
    
    // Recording state
    bool m_recordingActive;
    bool m_recordingPaused;
    QString m_recordingOutputPath;
    RecordingConfig m_recordingConfig;
    qint64 m_recordingStartTime;
    qint64 m_recordingPausedTime;
    qint64 m_totalPausedDuration;
    qint64 m_lastRecordedFrameTime; // Time when last frame was written to recording
    int m_recordingTargetFramerate; // Target framerate for recording (cached for thread safety)
    int64_t m_recordingFrameNumber;
    
    // Hotplug monitoring
    HotplugMonitor* m_hotplugMonitor;
    QString m_expectedDevicePath;
    bool m_waitingForDevice;
    QTimer* m_deviceWaitTimer;
    bool m_suppressErrors;
    
    QImage m_latestFrame;
    
    // Output management
    QGraphicsVideoItem* m_graphicsVideoItem;
    VideoPane* m_videoPane;
    
    // Error tracking
    QString m_lastError;
    

    // Thread safety
    mutable QMutex m_mutex;
    QWaitCondition m_frameCondition;
    
    // Recording thread safety
    mutable QMutex m_recordingMutex;
    
    // Performance monitoring
    QTimer* m_performanceTimer;
    int m_frameCount;
    qint64 m_lastFrameTime;
};

#endif // FFMPEGBACKENDHANDLER_H
