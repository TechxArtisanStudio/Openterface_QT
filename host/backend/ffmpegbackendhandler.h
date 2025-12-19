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
#include "ffmpeg/icapture_frame_reader.h"
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

// Forward declare CaptureThread class and interface
class CaptureThread;

// Forward declare FFmpegHardwareAccelerator class
class FFmpegHardwareAccelerator;

// Forward declare FFmpegDeviceManager class
class FFmpegDeviceManager;

// Forward declare FFmpegFrameProcessor class
class FFmpegFrameProcessor;

// Forward declare FFmpegRecorder class
class FFmpegRecorder;
struct RecordingConfig; // Defined in ffmpeg_recorder.h

// Forward declare FFmpegDeviceValidator class
class FFmpegDeviceValidator;

// Forward declare FFmpegHotplugHandler class
class FFmpegHotplugHandler;

// Forward declare FFmpegCaptureManager class
class FFmpegCaptureManager;

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
class FFmpegBackendHandler : public MultimediaBackendHandler, public ICaptureFrameReader
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

    void setVideoOutput(QGraphicsVideoItem* videoItem);
    void setVideoOutput(VideoPane* videoPane);

signals:
    void frameReady(const QPixmap& frame);
    void frameReadyImage(const QImage& frame);  // Thread-safe QImage signal for better performance
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
    
    // Hardware acceleration - delegated to FFmpegHardwareAccelerator
    bool initializeHardwareAcceleration();
    void cleanupHardwareAcceleration();
    bool tryHardwareDecoder(const AVCodecParameters* codecpar, const AVCodec** outCodec, bool* outUsingHwDecoder);
    
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
    
    // Threading
    friend class CaptureThread; // Allow capture thread to access private members (safe - it needs access for device/state)
    std::unique_ptr<CaptureThread> m_captureThread;
    
    // FFmpeg components - managed by FFmpegDeviceManager
    std::unique_ptr<FFmpegDeviceManager> m_deviceManager;
    
    // Hardware acceleration - managed by dedicated class
    std::unique_ptr<FFmpegHardwareAccelerator> m_hardwareAccelerator;
    
    // Frame processing - managed by dedicated class
    std::unique_ptr<FFmpegFrameProcessor> m_frameProcessor;
    
    // Video recording - managed by dedicated class
    std::unique_ptr<FFmpegRecorder> m_recorder;
    
    // Device validation - managed by dedicated class
    std::unique_ptr<FFmpegDeviceValidator> m_deviceValidator;
    
    // Hotplug monitoring - managed by dedicated class
    std::unique_ptr<FFmpegHotplugHandler> m_hotplugHandler;
    
    // Capture management - managed by dedicated class
    std::unique_ptr<FFmpegCaptureManager> m_captureManager;
    
    // Packet handling
#ifdef HAVE_FFMPEG
    AvPacketPtr m_packet;
#else
    AVPacket* m_packet;
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
    
    // Recording state (for compatibility - actual state managed by m_recorder)
    bool m_recordingActive;
    
    // Note: Hotplug monitoring state now managed by m_hotplugHandler
    // Keeping m_suppressErrors here as it's used by multiple components
    bool m_suppressErrors;
    
    // Output management
    QGraphicsVideoItem* m_graphicsVideoItem;
    VideoPane* m_videoPane;
    
    // Error tracking
    QString m_lastError;
    

    // Thread safety
    mutable QMutex m_mutex;
    QWaitCondition m_frameCondition;
    
    // Performance monitoring
    QTimer* m_performanceTimer;
    int m_frameCount;
    qint64 m_lastFrameTime;
};

#endif // FFMPEGBACKENDHANDLER_H
