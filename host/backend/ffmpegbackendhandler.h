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

// Forward declarations for FFmpeg types (conditional compilation)
#ifdef HAVE_FFMPEG
struct AVFormatContext;
struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct SwsContext;
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
    MultimediaBackendConfig getDefaultConfig() const override;

    void prepareCameraCreation(QCamera* oldCamera = nullptr) override;
    void configureCameraDevice(QCamera* camera, const QCameraDevice& device) override;
    void setupCaptureSession(QMediaCaptureSession* session, QCamera* camera) override;
    void prepareVideoOutputConnection(QMediaCaptureSession* session, QObject* videoOutput) override;
    void finalizeVideoOutputConnection(QMediaCaptureSession* session, QObject* videoOutput) override;
    void startCamera(QCamera* camera) override;
    void stopCamera(QCamera* camera) override;
    
    QCameraFormat selectOptimalFormat(const QList<QCameraFormat>& formats, 
                                    const QSize& resolution, 
                                    int desiredFrameRate,
                                    QVideoFrameFormat::PixelFormat pixelFormat) const override;

    // Direct FFmpeg capture methods
    bool startDirectCapture(const QString& devicePath, const QSize& resolution, int framerate);
    void stopDirectCapture();
    void processFrame();
    bool isDirectCaptureRunning() const;

    // Device availability and hotplug support
    bool checkCameraAvailable(const QString& devicePath = "");
    bool isCurrentDeviceAvailable() const;
    void handleDeviceDisconnection();
    bool restartCaptureWithDevice(const QString& devicePath, const QSize& resolution, int framerate);
    
    // Enhanced hotplug support with waiting capabilities
    void connectToHotplugMonitor();
    void disconnectFromHotplugMonitor();
    void waitForDeviceActivation(const QString& devicePath = "", int timeoutMs = 30000);
    void handleDeviceActivation(const QString& devicePath);
    void handleDeviceDeactivation(const QString& devicePath);
    
    // Stub for MOC compatibility (might be leftover from autocomplete)
    void checkDeviceReconnection() { /* stub */ }

#ifdef HAVE_FFMPEG
    bool readFrame();
    QPixmap convertFrameToPixmap(AVFrame* frame);
    QPixmap decodeFrame(AVPacket* packet);
#ifdef HAVE_LIBJPEG_TURBO
    QPixmap decodeJpegFrame(const uint8_t* data, int size);
#endif
#endif

public slots:
    void setVideoOutput(QGraphicsVideoItem* videoItem);
    void setVideoOutput(VideoPane* videoPane);

signals:
    void frameReady(const QPixmap& frame);
    void captureError(const QString& error);
    void deviceConnectionChanged(const QString& devicePath, bool connected);
    void deviceActivated(const QString& devicePath);
    void deviceDeactivated(const QString& devicePath);
    void waitingForDevice(const QString& devicePath);

private:
#ifdef HAVE_FFMPEG
    // FFmpeg context management
    bool initializeFFmpeg();
    void cleanupFFmpeg();
    bool openInputDevice(const QString& devicePath, const QSize& resolution, int framerate);
    void closeInputDevice();
#endif

#ifdef HAVE_LIBJPEG_TURBO
    // Note: decodeJpegFrame is declared in public section above
#endif
    
    // Threading
    class CaptureThread;
#ifdef HAVE_FFMPEG
    std::unique_ptr<CaptureThread> m_captureThread;
    
    // FFmpeg components
    AVFormatContext* m_formatContext;
    AVCodecContext* m_codecContext;
    AVFrame* m_frame;
    AVFrame* m_frameRGB;
    AVPacket* m_packet;
    SwsContext* m_swsContext;
#endif
    
#ifdef HAVE_LIBJPEG_TURBO
    // TurboJPEG components
    tjhandle m_turboJpegHandle;
#endif
    
    // State management
#ifdef HAVE_FFMPEG
    QString m_currentDevice;
    QSize m_currentResolution;
    int m_currentFramerate;
    bool m_captureRunning;
    int m_videoStreamIndex;
#endif
    
    // Hotplug monitoring
    HotplugMonitor* m_hotplugMonitor;
    QString m_expectedDevicePath;
    bool m_waitingForDevice;
    QTimer* m_deviceWaitTimer;
    bool m_suppressErrors;
    
    // Output management
    QGraphicsVideoItem* m_graphicsVideoItem;
    VideoPane* m_videoPane;
    
#ifdef HAVE_FFMPEG
    // Thread safety
    mutable QMutex m_mutex;
    QWaitCondition m_frameCondition;
    
    // Performance monitoring
    QTimer* m_performanceTimer;
    int m_frameCount;
    qint64 m_lastFrameTime;
#endif
};

#endif // FFMPEGBACKENDHANDLER_H
