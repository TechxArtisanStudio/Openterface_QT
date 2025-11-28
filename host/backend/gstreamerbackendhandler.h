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

#ifndef GSTREAMERBACKENDHANDLER_H
#define GSTREAMERBACKENDHANDLER_H

#include "../multimediabackend.h"
#include <QProcess>
#include <QWidget>
#include <QTimer>
#include <gst/gst.h>
#include <gst/app/gstappsink.h>

// Forward declarations for Qt types
class QGraphicsVideoItem;
class QGraphicsView;
class VideoPane;

// Forward declarations for GStreamer types - now properly defined via includes above
// typedef struct _GstElement GstElement;
// typedef struct _GstBus GstBus;
// typedef struct _GstMessage GstMessage;
// typedef struct _GstPad GstPad;
// typedef struct _GstAppSink GstAppSink;
// typedef enum _GstFlowReturn GstFlowReturn;

/**
 * @brief GStreamer backend handler implementation with direct pipeline support
 */
class GStreamerBackendHandler : public MultimediaBackendHandler
{
    Q_OBJECT

public:
    explicit GStreamerBackendHandler(QObject *parent = nullptr);
    ~GStreamerBackendHandler();

    MultimediaBackendType getBackendType() const override;
    QString getBackendName() const override;
    MultimediaBackendConfig getDefaultConfig() const override;

    void prepareCameraCreation() override;
    void configureCameraDevice() override;
    void setupCaptureSession(QMediaCaptureSession* session) override;
    void prepareVideoOutputConnection(QMediaCaptureSession* session, QObject* videoOutput) override;
    void finalizeVideoOutputConnection(QMediaCaptureSession* session, QObject* videoOutput) override;
    void startCamera() override;
    void stopCamera() override;

    // Hotplug support: track device port chain in private members
    QList<int> getSupportedFrameRates(const QCameraFormat& format) const override;
    QCameraFormat selectOptimalFormat(const QList<QCameraFormat>& formats, 
                                    const QSize& resolution, 
                                    int desiredFrameRate,
                                    QVideoFrameFormat::PixelFormat pixelFormat) const override;

    void handleCameraError(int errorCode, const QString& errorString) override;

    // Direct GStreamer pipeline methods (enhanced with working example approach)
    bool createGStreamerPipeline(const QString& device, const QSize& resolution, int framerate);
    bool startGStreamerPipeline();
    void stopGStreamerPipeline();
    void setVideoOutput(QWidget* widget);
    void setVideoOutput(QGraphicsVideoItem* videoItem);  // Support for QGraphicsVideoItem
    void setVideoOutput(VideoPane* videoPane);  // Support for VideoPane overlay
    
    // Enhanced methods based on working example
    bool checkCameraAvailable(const QString& device = "/dev/video0");
    WId getVideoWidgetWindowId() const;
    
    // Resolution and framerate configuration
    void setResolutionAndFramerate(const QSize& resolution, int framerate);
    
    // Video scaling and render rectangle configuration
    void updateVideoRenderRectangle(const QSize& widgetSize);
    void updateVideoRenderRectangle(int x, int y, int width, int height);
    
    // Pipeline string generation
    QString generatePipelineString(const QString& device, const QSize& resolution, int framerate, const QString& videoSink) const;

    // Recording configuration
    struct RecordingConfig {
        QString outputPath;
        QString format = "mp4";          // mp4, avi, mov, mkv
        QString videoCodec = "x264enc";  // x264enc, x265enc, vp8enc, vp9enc
        int videoBitrate = 2000000;      // 2 Mbps default
        int videoQuality = 23;           // Quality setting
        bool useHardwareAcceleration = false;
    };

    // Video recording methods
    bool startRecording(const QString& outputPath, const QString& format = "mp4", int videoBitrate = 2000000) override;
    bool stopRecording() override;
    void pauseRecording() override;
    void resumeRecording() override;
    bool isRecording() const override;
    bool isPaused() const;
    QString getCurrentRecordingPath() const override;
    qint64 getRecordingDuration() const override;
    
    // Advanced recording methods
    bool isPipelineReady() const;
    bool supportsAdvancedRecording() const;
    bool startRecordingAdvanced(const QString& outputPath, const RecordingConfig& config);
    bool forceStopRecording();
    QString getLastError() const;
    
    // Recording statistics
    bool supportsRecordingStats() const;
    qint64 getRecordingFileSize() const;
    
    void setRecordingConfig(const RecordingConfig& config);
    RecordingConfig getRecordingConfig() const;

    // Hotplug support: set current device and port chain for tracking
    void setCurrentDevicePortChain(const QString& portChain);
    void setCurrentDevice(const QString& devicePath);

    // Start a direct GStreamer pipeline using the current device/resolution/framerate
    // Returns true on successful create + start, false otherwise
    // NOTE: moved to private section

private slots:
    void onPipelineMessage();
    void checkPipelineHealth();
    // External runner event handlers
    void onExternalRunnerStarted();
    void onExternalRunnerFailed(const QString& error);
    void onExternalRunnerFinished(int exitCode, QProcess::ExitStatus status);
    
    // Recording lifecycle events are handled by RecordingManager

private:
    // GStreamer pipeline components
    GstElement* m_pipeline;
    GstElement* m_source;
    GstElement* m_sink;
    GstBus* m_bus;
    
    // Recording pipeline components (kept for compatibility during refactor)
    GstElement* m_recordingPipeline;
    GstElement* m_recordingTee;
    GstElement* m_recordingValve;    // Controls recording flow
    GstElement* m_recordingSink;
    GstElement* m_recordingQueue;
    GstElement* m_recordingEncoder;
    GstElement* m_recordingVideoConvert;
    GstElement* m_recordingMuxer;
    GstElement* m_recordingFileSink;
    GstElement* m_recordingAppSink;  // For frame capture
    GstPad* m_recordingTeeSrcPad;
    // Recording manager (encapsulates recording branch logic)
    class RecordingManager* m_recordingManager;
    
    // Qt integration
    QWidget* m_videoWidget;
    QGraphicsVideoItem* m_graphicsVideoItem;  // Support for QGraphicsVideoItem
    VideoPane* m_videoPane;  // Support for VideoPane overlay
    QTimer* m_healthCheckTimer;
    QProcess* m_gstProcess;  // Fallback for process-based approach
    
    // Pipeline state
    bool m_pipelineRunning;
    QString m_selectedSink; // textual name of the selected video sink element
    QString m_currentDevice;
    QString m_currentDevicePortChain; // Track port chain for hotplug support
    QSize m_currentResolution;
    int m_currentFramerate;
    
    // Overlay setup state
    bool m_overlaySetupPending;
    
    // Recording state (kept for compatibility during refactor)
    bool m_recordingActive;
    bool m_recordingPaused;
    QString m_recordingOutputPath;
    RecordingConfig m_recordingConfig;
    qint64 m_recordingStartTime;
    qint64 m_recordingPausedTime;
    qint64 m_totalPausedDuration;
    int m_recordingFrameNumber;
    
    // Error tracking
    QString m_lastError;
    
    // Frame-based recording handled by RecordingManager

    // Runner objects (in-process and external)
    class InProcessGstRunner* m_inProcessRunner;
    class ExternalGstRunner* m_externalRunner;
    
    // Helper methods
    bool initializeGStreamer();
    void cleanupGStreamer();
    bool startDirectPipeline();
    bool embedVideoInWidget(QWidget* widget);
    bool embedVideoInGraphicsView(QGraphicsView* view);
    bool embedVideoInVideoPane(VideoPane* videoPane);
    void handleGStreamerMessage(GstMessage* message);
    void completePendingOverlaySetup();
    
    // Enhanced overlay methods
    bool setupVideoOverlay(GstElement* videoSink, WId windowId);
    void setupVideoOverlayForCurrentPipeline();
    void refreshVideoOverlay();
    
    // Window validation for overlay setup
    bool isValidWindowId(WId windowId) const;
    
    // Recording helper moved to RecordingManager
    // Delegating recording helper methods (thin wrappers -> RecordingManager)
    void removeRecordingBranch();
    QString generateRecordingElements(const QString& outputPath, const QString& format, int videoBitrate) const;
    bool createSeparateRecordingPipeline(const QString& outputPath, const QString& format, int videoBitrate);
    bool initializeDirectFilesinkRecording();
    // Recording samples are handled by RecordingManager when GStreamer is available
};

#endif // GSTREAMERBACKENDHANDLER_H
