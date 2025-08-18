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

// Forward declarations for FFmpeg types
struct AVFormatContext;
struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct SwsContext;

// Forward declarations for libjpeg-turbo
struct jpeg_decompress_struct;
struct jpeg_error_mgr;

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

    // Direct FFmpeg video capture methods
    bool startDirectCapture(const QString& devicePath, const QSize& resolution, int framerate);
    void stopDirectCapture();
    bool isDirectCaptureRunning() const { return m_captureRunning; }
    
    // Video output management
    void setVideoOutput(QGraphicsVideoItem* videoItem);
    void setVideoOutput(VideoPane* videoPane);
    
signals:
    void frameReady(const QPixmap& frame);
    void captureError(const QString& error);

private slots:
    void processFrame();

private:
    // FFmpeg context management
    bool initializeFFmpeg();
    void cleanupFFmpeg();
    bool openInputDevice(const QString& devicePath, const QSize& resolution, int framerate);
    void closeInputDevice();
    
    // Frame processing
    bool readFrame();
    QPixmap decodeFrame(AVPacket* packet);
    QPixmap decodeJpegFrame(const uint8_t* data, int size);
    QPixmap convertFrameToPixmap(AVFrame* frame);
    
    // Threading
    class CaptureThread;
    std::unique_ptr<CaptureThread> m_captureThread;
    
    // FFmpeg components
    AVFormatContext* m_formatContext;
    AVCodecContext* m_codecContext;
    AVFrame* m_frame;
    AVFrame* m_frameRGB;
    AVPacket* m_packet;
    SwsContext* m_swsContext;
    
    // libjpeg-turbo components
    jpeg_decompress_struct* m_jpegDecompressor;
    jpeg_error_mgr* m_jpegErrorManager;
    
    // State management
    QString m_currentDevice;
    QSize m_currentResolution;
    int m_currentFramerate;
    bool m_captureRunning;
    int m_videoStreamIndex;
    
    // Output management
    QGraphicsVideoItem* m_graphicsVideoItem;
    VideoPane* m_videoPane;
    
    // Thread safety
    mutable QMutex m_mutex;
    QWaitCondition m_frameCondition;
    
    // Performance monitoring
    QTimer* m_performanceTimer;
    int m_frameCount;
    qint64 m_lastFrameTime;
};

#endif // FFMPEGBACKENDHANDLER_H
