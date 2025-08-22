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

#include "gstreamerbackendhandler.h"
#include "../../ui/videopane.h"
#include "../../ui/globalsetting.h"
#include <QThread>
#include <QApplication>
#include <QDebug>
#include <QWidget>
#include <QFile>
#include <QLoggingCategory>
#include <QGraphicsVideoItem>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QProcess>

// Logging category for GStreamer backend
Q_LOGGING_CATEGORY(log_gstreamer_backend, "opf.backend.gstreamer")

// GStreamer includes (conditional compilation)
#ifdef HAVE_GSTREAMER
#include <gst/gst.h>
#include <gst/video/videooverlay.h>

#ifndef GSTREAMER_DYNAMIC_LINKING
// Static plugin registration declarations for static linking
extern "C" {
    // Core GStreamer plugins needed for video pipeline
    void gst_plugin_coreelements_register(void);      // queue, capsfilter, tee, etc.
    void gst_plugin_typefindfunctions_register(void); // typefind for format detection
    void gst_plugin_videoconvertscale_register(void); // videoconvert, videoscale
    void gst_plugin_video4linux2_register(void);      // v4l2src plugin
    void gst_plugin_jpeg_register(void);              // jpegdec, jpegenc
    void gst_plugin_videofilter_register(void);       // video filter base
    void gst_plugin_videotestsrc_register(void);      // videotestsrc for testing
    void gst_plugin_ximagesink_register(void);        // ximagesink
    void gst_plugin_xvimagesink_register(void);       // xvimagesink
    void gst_plugin_autodetect_register(void);        // autovideosink
}
#endif // GSTREAMER_DYNAMIC_LINKING
#endif

GStreamerBackendHandler::GStreamerBackendHandler(QObject *parent)
    : MultimediaBackendHandler(parent),
      m_pipeline(nullptr),
      m_source(nullptr),
      m_sink(nullptr),
      m_bus(nullptr),
      m_videoWidget(nullptr),
      m_graphicsVideoItem(nullptr),
      m_healthCheckTimer(new QTimer(this)),
      m_gstProcess(nullptr),
      m_pipelineRunning(false),
      m_currentFramerate(30),
      m_currentResolution(1280, 720)  // Initialize with a valid default resolution
{
    m_config = getDefaultConfig();
    
    // Initialize GStreamer if available
    initializeGStreamer();
    
    // Set up health check timer
    connect(m_healthCheckTimer, &QTimer::timeout, this, &GStreamerBackendHandler::checkPipelineHealth);
}

GStreamerBackendHandler::~GStreamerBackendHandler()
{
    cleanupGStreamer();
}

MultimediaBackendType GStreamerBackendHandler::getBackendType() const
{
    return MultimediaBackendType::GStreamer;
}

QString GStreamerBackendHandler::getBackendName() const
{
    return "GStreamer";
}

MultimediaBackendConfig GStreamerBackendHandler::getDefaultConfig() const
{
    MultimediaBackendConfig config;
    config.cameraInitDelay = 25;
    config.deviceSwitchDelay = 25;
    config.videoOutputSetupDelay = 25;
    config.captureSessionDelay = 25;
    config.useConservativeFrameRates = true;
    config.requireVideoOutputReset = true;
    config.useGradualVideoOutputSetup = true;
    config.useStandardFrameRatesOnly = true;
    return config;
}

void GStreamerBackendHandler::prepareCameraCreation(QCamera* oldCamera)
{
    if (oldCamera) {
        qCDebug(log_gstreamer_backend) << "GStreamer: Disconnecting old camera from capture session before creating new one.";
        QThread::msleep(m_config.deviceSwitchDelay);
    }
}

void GStreamerBackendHandler::setupCaptureSession(QMediaCaptureSession* session, QCamera* camera)
{
    // For GStreamer backend, always use direct pipeline and never set up Qt camera
    // This prevents Qt from accessing the V4L2 device and causing conflicts
    qCDebug(log_gstreamer_backend) << "GStreamer: Skipping Qt capture session setup - always use direct pipeline to avoid device conflicts";
    
    // Do not call session->setCamera(camera) for GStreamer backend
    // The direct pipeline will handle video rendering without Qt camera
}

void GStreamerBackendHandler::finalizeVideoOutputConnection(QMediaCaptureSession* session, QObject* videoOutput)
{
    // For GStreamer backend, always use direct rendering and never set up Qt video output
    // This prevents Qt from accessing the V4L2 device and causing conflicts
    qCDebug(log_gstreamer_backend) << "GStreamer: Skipping Qt video output setup - always use direct GStreamer rendering to avoid device conflicts";
    
    // Do not call session->setVideoOutput(videoOutput) for GStreamer backend
    // The direct pipeline will render directly to the video widget
}

void GStreamerBackendHandler::startCamera(QCamera* camera)
{
     qCWarning(log_gstreamer_backend) << "!!!!!!!!!!!!!!!!!!!!!! GStreamer startCamera called";
     qCDebug(log_gstreamer_backend) << "Current device:" << m_currentDevice;
     qCDebug(log_gstreamer_backend) << "Current resolution:" << m_currentResolution;
     qCDebug(log_gstreamer_backend) << "Current framerate:" << m_currentFramerate;
     
    // Check if we should use direct GStreamer pipeline instead of Qt's camera
    bool useDirectPipeline = true; // This could be configurable
    
    if (useDirectPipeline && !m_currentDevice.isEmpty()) {
        qCDebug(log_gstreamer_backend) << "GStreamer: Using direct pipeline - Qt camera will NOT be started";
        
        // Ensure Qt camera is definitely stopped and disconnected
        if (camera) {
            qCDebug(log_gstreamer_backend) << "Ensuring Qt camera is stopped and disconnected";
            camera->stop();
            QThread::msleep(300); // Give extra time for device to be fully released
        }
        
        if (!createGStreamerPipeline(m_currentDevice, m_currentResolution, m_currentFramerate)) {
            qCWarning(log_gstreamer_backend) << "Failed to create GStreamer pipeline, falling back to Qt camera";
            useDirectPipeline = false;
        } else if (!startGStreamerPipeline()) {
            qCWarning(log_gstreamer_backend) << "Failed to start GStreamer pipeline, falling back to Qt camera";
            useDirectPipeline = false;
        } else {
            qCDebug(log_gstreamer_backend) << "GStreamer pipeline started successfully - NOT starting Qt camera";
            return; // SUCCESS: Direct pipeline is running, don't start Qt camera
        }
    } else {
        qCWarning(log_gstreamer_backend) << "GStreamer: No valid device configured, using Qt camera";
        useDirectPipeline = false;
    }
    
    if (!useDirectPipeline) {
        // Fallback to original Qt camera approach
        qCDebug(log_gstreamer_backend) << "GStreamer: Starting Qt camera as fallback";
        camera->start();
        QThread::msleep(50);
    }
}

void GStreamerBackendHandler::stopCamera(QCamera* camera)
{
    qCDebug(log_gstreamer_backend) << "GStreamer: Stopping camera with careful shutdown procedure.";
    
    // Stop direct pipeline if running
    if (m_pipelineRunning) {
        stopGStreamerPipeline();
    }
    
    // Disable GStreamer mode on VideoPane if we have a graphics video item
    if (m_graphicsVideoItem && m_graphicsVideoItem->scene()) {
        QList<QGraphicsView*> views = m_graphicsVideoItem->scene()->views();
        if (!views.isEmpty()) {
            if (auto videoPane = qobject_cast<VideoPane*>(views.first())) {
                qCDebug(log_gstreamer_backend) << "Disabling direct GStreamer mode on VideoPane";
                videoPane->enableDirectGStreamerMode(false);
            }
        }
    }
    
    // Also stop Qt camera
    camera->stop();
    QThread::msleep(100);
}

void GStreamerBackendHandler::prepareVideoOutputConnection(QMediaCaptureSession* session, QObject* videoOutput)
{
    if (m_config.requireVideoOutputReset) {
        qCDebug(log_gstreamer_backend) << "GStreamer: Temporarily disconnecting video output before final connection.";
        session->setVideoOutput(nullptr);
        QThread::msleep(m_config.videoOutputSetupDelay);
    }
    
    // Check if videoOutput is a QWidget
    QWidget* widget = qobject_cast<QWidget*>(videoOutput);
    if (widget) {
        setVideoOutput(widget);
        qCDebug(log_gstreamer_backend) << "GStreamer: Set video output widget for direct rendering";
        return;
    }
    
    // Check if videoOutput is a QGraphicsVideoItem
    QGraphicsVideoItem* graphicsVideoItem = qobject_cast<QGraphicsVideoItem*>(videoOutput);
    if (graphicsVideoItem) {
        setVideoOutput(graphicsVideoItem);
        qCDebug(log_gstreamer_backend) << "GStreamer: Set graphics video item for direct rendering";
        return;
    }
    
    qCDebug(log_gstreamer_backend) << "GStreamer: Video output type not supported for direct rendering, will use Qt's default handling";
}

void GStreamerBackendHandler::configureCameraDevice(QCamera* camera, const QCameraDevice& device)
{
    qCWarning(log_gstreamer_backend) << "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@ configureCameraDevice CALLED!";
    qCDebug(log_gstreamer_backend) << "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@";
    // Extract device path for direct GStreamer usage
    QString deviceId = QString::fromUtf8(device.id());
    
    qCDebug(log_gstreamer_backend) << "Configuring camera device with ID:" << deviceId;
    
    // Convert Qt device ID to V4L2 device path if needed
    if (!deviceId.startsWith("/dev/video")) {
        // Check if deviceId is a simple number (like "0", "1", etc.)
        bool isNumber = false;
        int deviceNumber = deviceId.toInt(&isNumber);
        
        if (isNumber) {
            // Direct numeric ID - convert to /dev/video path
            m_currentDevice = QString("/dev/video%1").arg(deviceNumber);
            qCDebug(log_gstreamer_backend) << "Converted numeric device ID" << deviceId << "to path:" << m_currentDevice;
        } else {
            // Complex device ID (Windows style, USB IDs, etc.) - try to find corresponding V4L2 device
            // For now, default to video0 but this could be enhanced with device enumeration
            qCDebug(log_gstreamer_backend) << "Complex device ID detected:" << deviceId << "- using fallback /dev/video0";
            m_currentDevice = "/dev/video0";
            
            // TODO: Implement more sophisticated device mapping based on:
            // - USB vendor/product IDs
            // - Device description matching
            // - /sys/class/video4linux/ enumeration
        }
    } else {
        // Already a proper V4L2 device path
        m_currentDevice = deviceId;
        qCDebug(log_gstreamer_backend) << "Using direct device path:" << m_currentDevice;
    }
    
    qCDebug(log_gstreamer_backend) << "GStreamer device path configured as:" << m_currentDevice;
    
    // Ensure Qt camera is stopped before we configure it for potential direct GStreamer use
    if (camera && !m_currentDevice.isEmpty()) {
        qCDebug(log_gstreamer_backend) << "Stopping Qt camera during device configuration to prevent early device access";
        camera->stop();
        QThread::msleep(100);
    }
    
    // Call parent implementation for standard Qt camera configuration
    // But note: this might start the camera again, which is why we handle it in setupCaptureSession
    MultimediaBackendHandler::configureCameraDevice(camera, device);
}

QList<int> GStreamerBackendHandler::getSupportedFrameRates(const QCameraFormat& format) const
{
    if (m_config.useStandardFrameRatesOnly) {
        qCDebug(log_gstreamer_backend) << "GStreamer: Providing only standard, safe frame rates.";
        QList<int> rates;
        std::vector<int> safeRates = {5, 10, 15, 20, 24, 25, 30, 50, 60};
        for (int rate : safeRates) {
            if (rate >= format.minFrameRate() && rate <= format.maxFrameRate()) {
                rates.append(rate);
            }
        }
        return rates;
    }
    return MultimediaBackendHandler::getSupportedFrameRates(format);
}

QCameraFormat GStreamerBackendHandler::selectOptimalFormat(const QList<QCameraFormat>& formats,
                                                         const QSize& resolution,
                                                         int desiredFrameRate,
                                                         QVideoFrameFormat::PixelFormat pixelFormat) const
{
    qCDebug(log_gstreamer_backend) << "GStreamer: Selecting optimal format with conservative frame rate matching.";
    for (const auto& format : formats) {
        if (format.resolution() == resolution && format.pixelFormat() == pixelFormat) {
            if (desiredFrameRate >= format.minFrameRate() && desiredFrameRate <= format.maxFrameRate()) {
                if (desiredFrameRate == format.minFrameRate() || desiredFrameRate == format.maxFrameRate()) {
                    return format;
                }
            }
        }
    }
    return MultimediaBackendHandler::selectOptimalFormat(formats, resolution, desiredFrameRate, pixelFormat);
}

void GStreamerBackendHandler::handleCameraError(QCamera::Error error, const QString& errorString)
{
    qCCritical(log_gstreamer_backend) << "GStreamer Camera Error:" << error << "-" << errorString;
    if (errorString.contains("GStreamer")) {
        emit backendWarning("A GStreamer-specific error occurred. Please check GStreamer installation and plugins.");
    }
}

// Direct GStreamer pipeline methods
bool GStreamerBackendHandler::createGStreamerPipeline(const QString& device, const QSize& resolution, int framerate)
{
    qCDebug(log_gstreamer_backend) << "Creating GStreamer pipeline for device:" << device 
                                   << "resolution:" << resolution.width() << "x" << resolution.height() 
                                   << "framerate:" << framerate;
    
    // Clean up any existing pipeline first
    if (m_pipeline) {
        qCDebug(log_gstreamer_backend) << "Cleaning up existing pipeline before creating new one";
        cleanupGStreamer();
    }
    
    // Validate parameters before creating pipeline
    if (device.isEmpty()) {
        qCCritical(log_gstreamer_backend) << "Cannot create pipeline: device path is empty";
        return false;
    }
    
    if (resolution.width() <= 0 || resolution.height() <= 0) {
        qCCritical(log_gstreamer_backend) << "Cannot create pipeline: invalid resolution" << resolution;
        return false;
    }
    
    if (framerate <= 0) {
        qCCritical(log_gstreamer_backend) << "Cannot create pipeline: invalid framerate" << framerate;
        return false;
    }
    
    m_currentDevice = device;
    m_currentResolution = resolution;
    m_currentFramerate = framerate;
    
#ifdef HAVE_GSTREAMER
    // Try creating pipeline with flexible format first
    QString pipelineStr = generatePipelineString(device, resolution, framerate);
    
    GError* error = nullptr;
    m_pipeline = gst_parse_launch(pipelineStr.toUtf8().data(), &error);
    
    if (!m_pipeline || error) {
        QString errorMsg = error ? error->message : "Unknown error creating pipeline";
        qCWarning(log_gstreamer_backend) << "Failed to create primary pipeline:" << errorMsg;
        if (error) g_error_free(error);
        
        // Try fallback with MJPG format and different sink
        qCDebug(log_gstreamer_backend) << "Trying MJPG fallback pipeline...";
        QString mjpgPipeline = QString(
            "v4l2src device=%1 ! "
            "image/jpeg,width=%2,height=%3,framerate=%4/1 ! "
            "jpegdec ! "
            "videoconvert ! "
            "autovideosink"
        ).arg(device)
         .arg(resolution.width())
         .arg(resolution.height())
         .arg(framerate);
        
        error = nullptr;
        m_pipeline = gst_parse_launch(mjpgPipeline.toUtf8().data(), &error);
        
        if (!m_pipeline || error) {
            QString mjpgErrorMsg = error ? error->message : "Unknown error creating MJPG pipeline";
            qCWarning(log_gstreamer_backend) << "Failed to create MJPG pipeline:" << mjpgErrorMsg;
            if (error) g_error_free(error);
            
            // Try even more conservative pipeline with smaller resolution and autosink
            qCDebug(log_gstreamer_backend) << "Trying conservative 1280x720 pipeline...";
            QString conservativePipeline = QString(
                "v4l2src device=%1 ! "
                "image/jpeg,width=1280,height=720,framerate=30/1 ! "
                "jpegdec ! "
                "videoconvert ! "
                "autovideosink"
            ).arg(device);
            
            error = nullptr;
            m_pipeline = gst_parse_launch(conservativePipeline.toUtf8().data(), &error);
            
            if (!m_pipeline || error) {
                QString consErrorMsg = error ? error->message : "Unknown error creating conservative pipeline";
                qCCritical(log_gstreamer_backend) << "Failed to create any GStreamer pipeline:" << consErrorMsg;
                if (error) g_error_free(error);
                return false;
            } else {
                qCWarning(log_gstreamer_backend) << "Using conservative 1280x720@30fps pipeline as fallback";
            }
        } else {
            qCDebug(log_gstreamer_backend) << "MJPG fallback pipeline created successfully";
        }
    } else {
        qCDebug(log_gstreamer_backend) << "Flexible pipeline created successfully";
    }
    
    // Get bus for message handling with proper validation
    m_bus = gst_element_get_bus(m_pipeline);
    if (m_bus) {
        gst_bus_add_signal_watch(m_bus);
        qCDebug(log_gstreamer_backend) << "GStreamer bus initialized successfully";
        // Connect to Qt's signal system would require additional setup
    } else {
        qCWarning(log_gstreamer_backend) << "Failed to get GStreamer bus - error reporting will be limited";
    }
    
    // Final validation - ensure pipeline is actually usable
    if (!m_pipeline) {
        qCCritical(log_gstreamer_backend) << "Pipeline is null after creation attempts";
        return false;
    }
    
    // Test if pipeline can reach NULL state (basic sanity check)
    GstStateChangeReturn testRet = gst_element_set_state(m_pipeline, GST_STATE_NULL);
    if (testRet == GST_STATE_CHANGE_FAILURE) {
        qCCritical(log_gstreamer_backend) << "Pipeline failed basic state change test";
        gst_object_unref(m_pipeline);
        m_pipeline = nullptr;
        return false;
    }
    
    qCDebug(log_gstreamer_backend) << "GStreamer pipeline created and validated successfully";
    return true;
    
#else
    // Fallback: Use QProcess to launch gst-launch-1.0
    qCDebug(log_gstreamer_backend) << "Using QProcess fallback for GStreamer pipeline";
    
    if (!m_gstProcess) {
        m_gstProcess = new QProcess(this);
        connect(m_gstProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this](int exitCode, QProcess::ExitStatus exitStatus) {
                    qCDebug(log_gstreamer_backend) << "GStreamer process finished with code:" << exitCode;
                    m_pipelineRunning = false;
                });
    }
    
    return true;
#endif
}

QString GStreamerBackendHandler::generatePipelineString(const QString& device, const QSize& resolution, int framerate) const
{
    // Validate inputs before generating pipeline
    if (device.isEmpty()) {
        qCWarning(log_gstreamer_backend) << "Device path is empty, cannot generate pipeline";
        return QString();
    }
    
    if (resolution.width() <= 0 || resolution.height() <= 0) {
        qCWarning(log_gstreamer_backend) << "Invalid resolution:" << resolution << "- using fallback 1280x720";
        QSize fallbackResolution(1280, 720);  // Use more conservative fallback
        return generatePipelineString(device, fallbackResolution, framerate);
    }
    
    if (framerate <= 0) {
        qCWarning(log_gstreamer_backend) << "Invalid framerate:" << framerate << "- using fallback 30fps";
        return generatePipelineString(device, resolution, 30);
    }
    
    // Use configurable pipeline template from settings
    QString pipelineTemplate = GlobalSetting::instance().getGStreamerPipelineTemplate();
    
    // Validate the pipeline template before using it
    if (pipelineTemplate.isEmpty() || !pipelineTemplate.contains("%DEVICE%")) {
        qCWarning(log_gstreamer_backend) << "Invalid or empty pipeline template, using default";
        pipelineTemplate = "v4l2src device=%DEVICE% ! "
                          "image/jpeg,width=%WIDTH%,height=%HEIGHT%,framerate=%FRAMERATE%/1 ! "
                          "jpegdec ! "
                          "videoconvert ! "
                          "xvimagesink name=videosink";
    }
    
    // Replace placeholders with actual values
    QString pipelineStr = pipelineTemplate;
    pipelineStr.replace("%DEVICE%", device);
    pipelineStr.replace("%WIDTH%", QString::number(resolution.width()));
    pipelineStr.replace("%HEIGHT%", QString::number(resolution.height()));
    pipelineStr.replace("%FRAMERATE%", QString::number(framerate));
    
    qCDebug(log_gstreamer_backend) << "Generated pipeline from template:" << pipelineStr;
    qCDebug(log_gstreamer_backend) << "Template used:" << pipelineTemplate;
    
    return pipelineStr;
}

bool GStreamerBackendHandler::startGStreamerPipeline()
{
    qCDebug(log_gstreamer_backend) << "Starting GStreamer pipeline";
    
#ifdef HAVE_GSTREAMER
    if (!m_pipeline) {
        qCWarning(log_gstreamer_backend) << "No pipeline to start";
        return false;
    }
    
    // First check if the device is accessible
    if (!checkCameraAvailable(m_currentDevice)) {
        qCCritical(log_gstreamer_backend) << "Camera device not accessible:" << m_currentDevice;
        return false;
    }
    
    qCDebug(log_gstreamer_backend) << "Setting pipeline to READY state first...";
    GstStateChangeReturn ret = gst_element_set_state(m_pipeline, GST_STATE_READY);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        qCCritical(log_gstreamer_backend) << "Failed to set pipeline to READY state";
        // Get more detailed error information only if bus exists
        if (m_bus) {
            GstMessage* msg = gst_bus_pop_filtered(m_bus, GST_MESSAGE_ERROR);
            if (msg) {
                GError* error = nullptr;
                gchar* debug_info = nullptr;
                gst_message_parse_error(msg, &error, &debug_info);
                qCCritical(log_gstreamer_backend) << "GStreamer Error:" << (error ? error->message : "Unknown");
                qCCritical(log_gstreamer_backend) << "Debug info:" << (debug_info ? debug_info : "None");
                if (error) g_error_free(error);
                if (debug_info) g_free(debug_info);
                gst_message_unref(msg);
            }
        } else {
            qCCritical(log_gstreamer_backend) << "Bus not available for error details";
        }
        return false;
    }
    
    // Wait for READY state to complete
    GstState state, pending;
    ret = gst_element_get_state(m_pipeline, &state, &pending, 2000 * GST_MSECOND);
    if (ret == GST_STATE_CHANGE_FAILURE || state != GST_STATE_READY) {
        qCCritical(log_gstreamer_backend) << "Pipeline failed to reach READY state";
        return false;
    }
    
    qCDebug(log_gstreamer_backend) << "Pipeline in READY state, now setting up video overlay...";
    
    // Set up video overlay BEFORE starting playback - this is crucial for hardware acceleration
    WId windowId = 0;
    
    if (m_videoWidget) {
        windowId = m_videoWidget->winId();
        qCDebug(log_gstreamer_backend) << "Using video widget window ID:" << windowId;
    } else if (m_graphicsVideoItem) {
        // For QGraphicsVideoItem, get the window handle from the parent graphics view
        if (m_graphicsVideoItem->scene()) {
            QList<QGraphicsView*> views = m_graphicsVideoItem->scene()->views();
            if (!views.isEmpty()) {
                QGraphicsView* view = views.first();
                
                // Check if this is a VideoPane with GStreamer mode enabled and use overlay widget
                if (auto videoPane = qobject_cast<VideoPane*>(view)) {
                    if (videoPane->isDirectGStreamerModeEnabled() && videoPane->getOverlayWidget()) {
                        windowId = videoPane->getVideoOverlayWindowId();
                        qCDebug(log_gstreamer_backend) << "Using VideoPane overlay widget window ID:" << windowId;
                    } else {
                        windowId = view->winId();
                        qCDebug(log_gstreamer_backend) << "Using VideoPane window ID:" << windowId;
                    }
                } else {
                    windowId = view->winId();
                    qCDebug(log_gstreamer_backend) << "Using graphics view window ID:" << windowId;
                }
            } else {
                qCWarning(log_gstreamer_backend) << "Graphics video item has no associated view";
            }
        } else {
            qCWarning(log_gstreamer_backend) << "Graphics video item has no scene";
        }
    }
    
    if (windowId) {
        // Find the video sink element with better error checking
        // First try to find named sink, then fall back to interface search
        GstElement* videoSink = gst_bin_get_by_name(GST_BIN(m_pipeline), "videosink");
        if (!videoSink) {
            // Fallback: find any element that supports video overlay
            videoSink = gst_bin_get_by_interface(GST_BIN(m_pipeline), GST_TYPE_VIDEO_OVERLAY);
            if (videoSink) {
                qCDebug(log_gstreamer_backend) << "Found video sink by interface (autovideosink fallback)";
            }
        }
        
        if (videoSink) {
            // Check if the element actually supports video overlay interface
            if (GST_IS_VIDEO_OVERLAY(videoSink)) {
                qCDebug(log_gstreamer_backend) << "Setting up video overlay with window ID:" << windowId;
                gst_video_overlay_set_window_handle(GST_VIDEO_OVERLAY(videoSink), windowId);
            } else {
                qCWarning(log_gstreamer_backend) << "Video sink element does not support overlay interface";
            }
            gst_object_unref(videoSink);
        } else {
            qCWarning(log_gstreamer_backend) << "Could not find any video sink element for overlay setup";
        }
    } else {
        qCWarning(log_gstreamer_backend) << "No valid window ID available, overlay setup skipped";
    }
    
    qCDebug(log_gstreamer_backend) << "Now setting pipeline to PLAYING...";
    ret = gst_element_set_state(m_pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        qCCritical(log_gstreamer_backend) << "Failed to start GStreamer pipeline to PLAYING state";
        // Get detailed error information only if bus exists
        if (m_bus) {
            GstMessage* msg = gst_bus_pop_filtered(m_bus, GST_MESSAGE_ERROR);
            if (msg) {
                GError* error = nullptr;
                gchar* debug_info = nullptr;
                gst_message_parse_error(msg, &error, &debug_info);
                qCCritical(log_gstreamer_backend) << "GStreamer Error:" << (error ? error->message : "Unknown");
                qCCritical(log_gstreamer_backend) << "Debug info:" << (debug_info ? debug_info : "None");
                if (error) g_error_free(error);
                if (debug_info) g_free(debug_info);
                gst_message_unref(msg);
            }
        } else {
            qCCritical(log_gstreamer_backend) << "Bus not available for error details";
        }
        return false;
    }
    
    m_pipelineRunning = true;
    m_healthCheckTimer->start(2000); // Check health every 2 seconds (less frequent)
    
#else
    // Fallback: Use QProcess
    if (!m_gstProcess) {
        qCWarning(log_gstreamer_backend) << "No GStreamer process available";
        return false;
    }
    
    QString program = "gst-launch-1.0";
    QString pipelineStr = generatePipelineString(m_currentDevice, m_currentResolution, m_currentFramerate);
    
    // Remove "gst-launch-1.0" prefix if present and split into arguments
    QStringList arguments = pipelineStr.split(' ', Qt::SkipEmptyParts);
    
    qCDebug(log_gstreamer_backend) << "Starting GStreamer process:" << program << arguments.join(' ');
    
    m_gstProcess->start(program, arguments);
    
    if (!m_gstProcess->waitForStarted(3000)) {
        qCCritical(log_gstreamer_backend) << "Failed to start GStreamer process:" << m_gstProcess->errorString();
        return false;
    }
    
    m_pipelineRunning = true;
    m_healthCheckTimer->start(1000);
#endif
    
    qCDebug(log_gstreamer_backend) << "GStreamer pipeline started successfully";
    return true;
}

void GStreamerBackendHandler::stopGStreamerPipeline()
{
    qCDebug(log_gstreamer_backend) << "Stopping GStreamer pipeline";
    
    m_healthCheckTimer->stop();
    m_pipelineRunning = false;
    
#ifdef HAVE_GSTREAMER
    if (m_pipeline) {
        gst_element_set_state(m_pipeline, GST_STATE_NULL);
        qCDebug(log_gstreamer_backend) << "GStreamer pipeline stopped";
    }
#else
    if (m_gstProcess && m_gstProcess->state() == QProcess::Running) {
        m_gstProcess->terminate();
        if (!m_gstProcess->waitForFinished(3000)) {
            m_gstProcess->kill();
            qCDebug(log_gstreamer_backend) << "GStreamer process forcefully killed";
        } else {
            qCDebug(log_gstreamer_backend) << "GStreamer process terminated gracefully";
        }
    }
#endif
}

void GStreamerBackendHandler::setVideoOutput(QWidget* widget)
{
    m_videoWidget = widget;
    m_graphicsVideoItem = nullptr;  // Clear graphics video item if widget is set
    
    if (widget) {
        // Ensure widget is properly configured for video overlay (same as working widgets_main.cpp)
        widget->setAttribute(Qt::WA_NativeWindow, true);
        widget->setAttribute(Qt::WA_PaintOnScreen, true);
        
        qCDebug(log_gstreamer_backend) << "Video widget configured with native window attributes";
        
        // If pipeline is already created, set up the overlay immediately
        if (m_pipeline) {
            embedVideoInWidget(widget);
        }
    }
}

void GStreamerBackendHandler::setVideoOutput(QGraphicsVideoItem* videoItem)
{
    m_graphicsVideoItem = videoItem;
    m_videoWidget = nullptr;  // Clear widget if graphics video item is set
    
    if (videoItem) {
        qCDebug(log_gstreamer_backend) << "Graphics video item configured for direct rendering";
        
        // For QGraphicsVideoItem, we need to ensure the parent view has native window
        if (videoItem->scene()) {
            QList<QGraphicsView*> views = videoItem->scene()->views();
            if (!views.isEmpty()) {
                QGraphicsView* view = views.first();
                view->setAttribute(Qt::WA_NativeWindow, true);
                view->setAttribute(Qt::WA_PaintOnScreen, true);
                qCDebug(log_gstreamer_backend) << "Graphics view configured with native window attributes";
                
                // Check if this is a VideoPane and enable GStreamer mode
                if (auto videoPane = qobject_cast<VideoPane*>(view)) {
                    qCDebug(log_gstreamer_backend) << "Enabling direct GStreamer mode on VideoPane";
                    videoPane->enableDirectGStreamerMode(true);
                }
                
                // If pipeline is already created, set up the overlay immediately
                if (m_pipeline) {
                    embedVideoInGraphicsView(view);
                }
            }
        }
    }
}

bool GStreamerBackendHandler::initializeGStreamer()
{
#ifdef HAVE_GSTREAMER
    GError* error = nullptr;
    if (!gst_init_check(nullptr, nullptr, &error)) {
        QString errorMsg = error ? error->message : "Unknown GStreamer initialization error";
        qCCritical(log_gstreamer_backend) << "Failed to initialize GStreamer:" << errorMsg;
        if (error) g_error_free(error);
        return false;
    }
    
    qCDebug(log_gstreamer_backend) << "GStreamer initialized successfully";
    
    // Register static plugins required for video pipeline (only for static builds)
#ifndef GSTREAMER_DYNAMIC_LINKING
    qCDebug(log_gstreamer_backend) << "Registering static GStreamer plugins...";
    
    try {
        // Register core elements (queue, capsfilter, tee, etc.)
        gst_plugin_coreelements_register();
        qCDebug(log_gstreamer_backend) << "✓ Registered coreelements plugin";
        
        // Register typefind functions for format detection
        gst_plugin_typefindfunctions_register();
        qCDebug(log_gstreamer_backend) << "✓ Registered typefindfunctions plugin";
        
        // Register video conversion and scaling
        gst_plugin_videoconvertscale_register();
        qCDebug(log_gstreamer_backend) << "✓ Registered videoconvertscale plugin";
        
        // Register V4L2 plugin (v4l2src)
        gst_plugin_video4linux2_register();
        qCDebug(log_gstreamer_backend) << "✓ Registered video4linux2 plugin (v4l2src)";
        
        // Register JPEG decoder/encoder
        gst_plugin_jpeg_register();
        qCDebug(log_gstreamer_backend) << "✓ Registered jpeg plugin";
        
        // Register video filter base
        gst_plugin_videofilter_register();
        qCDebug(log_gstreamer_backend) << "✓ Registered videofilter plugin";
        
        // Register video test source (for testing)
        gst_plugin_videotestsrc_register();
        qCDebug(log_gstreamer_backend) << "✓ Registered videotestsrc plugin";
        
        // Register X11 video sinks
        gst_plugin_ximagesink_register();
        qCDebug(log_gstreamer_backend) << "✓ Registered ximagesink plugin";
        
        gst_plugin_xvimagesink_register();
        qCDebug(log_gstreamer_backend) << "✓ Registered xvimagesink plugin";
        
        // Register autodetect elements
        gst_plugin_autodetect_register();
        qCDebug(log_gstreamer_backend) << "✓ Registered autodetect plugin (autovideosink)";
        
        qCDebug(log_gstreamer_backend) << "All static GStreamer plugins registered successfully";
    } catch (...) {
        qCWarning(log_gstreamer_backend) << "Exception occurred during static plugin registration";
    }
#else
    qCDebug(log_gstreamer_backend) << "Using dynamic GStreamer plugins (no static registration needed)";
#endif
    
    // Verify that v4l2src element is now available
    GstElementFactory* factory = gst_element_factory_find("v4l2src");
    if (factory) {
        qCDebug(log_gstreamer_backend) << "✓ v4l2src element is available";
        gst_object_unref(factory);
    } else {
        qCWarning(log_gstreamer_backend) << "✗ v4l2src element not available - check GStreamer plugins installation";
    }
        
    } catch (...) {
        qCCritical(log_gstreamer_backend) << "Exception occurred during plugin registration";
        return false;
    }
    
    return true;
#else
    qCDebug(log_gstreamer_backend) << "GStreamer not available, using QProcess fallback";
    return true;
#endif
}

void GStreamerBackendHandler::cleanupGStreamer()
{
    qCDebug(log_gstreamer_backend) << "Cleaning up GStreamer resources";
    
    stopGStreamerPipeline();
    
#ifdef HAVE_GSTREAMER
    if (m_bus) {
        gst_bus_remove_signal_watch(m_bus);
        gst_object_unref(m_bus);
        m_bus = nullptr;
        qCDebug(log_gstreamer_backend) << "GStreamer bus cleaned up";
    }
    
    if (m_pipeline) {
        // Set to NULL state first to ensure proper cleanup
        gst_element_set_state(m_pipeline, GST_STATE_NULL);
        gst_object_unref(m_pipeline);
        m_pipeline = nullptr;
        qCDebug(log_gstreamer_backend) << "GStreamer pipeline cleaned up";
        
        // Give the device some time to be fully released
        QThread::msleep(100);
    }
#endif
    
    if (m_gstProcess) {
        if (m_gstProcess->state() == QProcess::Running) {
            m_gstProcess->kill();
            m_gstProcess->waitForFinished(1000);
        }
        m_gstProcess->deleteLater();
        m_gstProcess = nullptr;
        qCDebug(log_gstreamer_backend) << "GStreamer process cleaned up";
    }
}

bool GStreamerBackendHandler::embedVideoInWidget(QWidget* widget)
{
#ifdef HAVE_GSTREAMER
    if (!widget || !m_pipeline) {
        qCWarning(log_gstreamer_backend) << "Cannot embed video: widget or pipeline is null";
        return false;
    }
    
    // Find the video sink element by name (same approach as working widgets_main.cpp)
    GstElement* videoSink = gst_bin_get_by_name(GST_BIN(m_pipeline), "videosink");
    if (!videoSink) {
        qCWarning(log_gstreamer_backend) << "No video sink element named 'videosink' found in pipeline";
        // Fallback: try to find by interface
        videoSink = gst_bin_get_by_interface(GST_BIN(m_pipeline), GST_TYPE_VIDEO_OVERLAY);
        if (!videoSink) {
            qCWarning(log_gstreamer_backend) << "No video overlay interface found in pipeline either";
            return false;
        }
    }
    
    // Get window ID and embed video
    WId winId = widget->winId();
    if (winId) {
        qCDebug(log_gstreamer_backend) << "Embedding video in widget with window ID:" << winId;
        gst_video_overlay_set_window_handle(GST_VIDEO_OVERLAY(videoSink), winId);
        gst_object_unref(videoSink);
        qCDebug(log_gstreamer_backend) << "Video embedded in widget successfully";
        return true;
    } else {
        qCWarning(log_gstreamer_backend) << "Widget window ID is null, cannot embed video";
        gst_object_unref(videoSink);
        return false;
    }
#else
    // For QProcess approach, we rely on autovideosink to find the display
    qCDebug(log_gstreamer_backend) << "Using autovideosink for video output";
    return true;
#endif
}

bool GStreamerBackendHandler::embedVideoInGraphicsView(QGraphicsView* view)
{
#ifdef HAVE_GSTREAMER
    if (!view || !m_pipeline) {
        qCWarning(log_gstreamer_backend) << "Cannot embed video: graphics view or pipeline is null";
        return false;
    }
    
    // Find the video sink element by name
    GstElement* videoSink = gst_bin_get_by_name(GST_BIN(m_pipeline), "videosink");
    if (!videoSink) {
        qCWarning(log_gstreamer_backend) << "No video sink element named 'videosink' found in pipeline";
        // Fallback: try to find by interface
        videoSink = gst_bin_get_by_interface(GST_BIN(m_pipeline), GST_TYPE_VIDEO_OVERLAY);
        if (!videoSink) {
            qCWarning(log_gstreamer_backend) << "No video overlay interface found in pipeline either";
            return false;
        }
    }
    
    // Get window ID from graphics view and embed video
    WId winId = view->winId();
    if (winId) {
        qCDebug(log_gstreamer_backend) << "Embedding video in graphics view with window ID:" << winId;
        gst_video_overlay_set_window_handle(GST_VIDEO_OVERLAY(videoSink), winId);
        gst_object_unref(videoSink);
        qCDebug(log_gstreamer_backend) << "Video embedded in graphics view successfully";
        return true;
    } else {
        qCWarning(log_gstreamer_backend) << "Graphics view window ID is null, cannot embed video";
        gst_object_unref(videoSink);
        return false;
    }
#else
    // For QProcess approach, we rely on autovideosink to find the display
    qCDebug(log_gstreamer_backend) << "Using autovideosink for video output";
    return true;
#endif
}

void GStreamerBackendHandler::onPipelineMessage()
{
    // This would be connected to GStreamer bus signals in a full implementation
#ifdef HAVE_GSTREAMER
    // Handle GStreamer messages here
#endif
}

void GStreamerBackendHandler::checkPipelineHealth()
{
    if (!m_pipelineRunning) {
        return;
    }
    
#ifdef HAVE_GSTREAMER
    if (m_pipeline) {
        GstState state;
        GstState pending;
        // Wait up to 500ms for state change to complete
        GstStateChangeReturn ret = gst_element_get_state(m_pipeline, &state, &pending, 500 * GST_MSECOND);
        
        // Only report failure for actual failures, not async state changes
        if (ret == GST_STATE_CHANGE_FAILURE) {
            qCWarning(log_gstreamer_backend) << "GStreamer pipeline health check failed - state change failure";
            
            // Check for error messages on the bus
            GstMessage* msg = gst_bus_pop_filtered(m_bus, GST_MESSAGE_ERROR);
            if (msg) {
                GError* error = nullptr;
                gchar* debug_info = nullptr;
                gst_message_parse_error(msg, &error, &debug_info);
                qCCritical(log_gstreamer_backend) << "Pipeline Error:" << (error ? error->message : "Unknown");
                qCCritical(log_gstreamer_backend) << "Debug info:" << (debug_info ? debug_info : "None");
                if (error) g_error_free(error);
                if (debug_info) g_free(debug_info);
                gst_message_unref(msg);
            }
            
            emit backendWarning("GStreamer pipeline has failed");
            m_pipelineRunning = false;
            m_healthCheckTimer->stop();
        } else if (ret == GST_STATE_CHANGE_SUCCESS && state != GST_STATE_PLAYING) {
            qCDebug(log_gstreamer_backend) << "GStreamer pipeline not in PLAYING state, current state:" << state << "pending:" << pending;
            // Don't emit warning immediately - pipeline might be transitioning
        } else if (ret == GST_STATE_CHANGE_ASYNC) {
            // Pipeline is still transitioning, this is normal - don't report as error
            qCDebug(log_gstreamer_backend) << "GStreamer pipeline state change in progress (ASYNC), current state:" << state;
        } else if (ret == GST_STATE_CHANGE_SUCCESS && state == GST_STATE_PLAYING) {
            qCDebug(log_gstreamer_backend) << "GStreamer pipeline health check: OK (PLAYING)";
        }
    }
#else
    if (m_gstProcess && m_gstProcess->state() != QProcess::Running) {
        qCWarning(log_gstreamer_backend) << "GStreamer process is not running";
        m_pipelineRunning = false;
        emit backendWarning("GStreamer process has stopped unexpectedly");
    }
#endif
}

// Enhanced methods based on working example from widgets_main.cpp
bool GStreamerBackendHandler::checkCameraAvailable(const QString& device)
{
    // Check if camera device file exists
    if (!QFile::exists(device)) {
        qCWarning(log_gstreamer_backend) << "Camera device file does not exist:" << device;
        return false;
    }
    
    // Try a basic device accessibility check first
    QFile deviceFile(device);
    if (!deviceFile.open(QIODevice::ReadOnly)) {
        qCWarning(log_gstreamer_backend) << "Camera device not accessible (permission denied?):" << device;
        qCWarning(log_gstreamer_backend) << "Error:" << deviceFile.errorString();
        return false;
    }
    // deviceFile.close();
    
    qCDebug(log_gstreamer_backend) << "Camera device is accessible:" << device;
    return true;
}

WId GStreamerBackendHandler::getVideoWidgetWindowId() const
{
    if (m_videoWidget) {
        return m_videoWidget->winId();
    }
    
    if (m_graphicsVideoItem && m_graphicsVideoItem->scene()) {
        QList<QGraphicsView*> views = m_graphicsVideoItem->scene()->views();
        if (!views.isEmpty()) {
            return views.first()->winId();
        }
    }
    
    return 0;
}

void GStreamerBackendHandler::setResolutionAndFramerate(const QSize& resolution, int framerate)
{
    qCDebug(log_gstreamer_backend) << "Setting resolution and framerate:" << resolution << "fps:" << framerate;
    
    m_currentResolution = resolution;
    m_currentFramerate = framerate;
    
    // If we have a running pipeline and the parameters changed significantly, we might need to recreate it
    // For now, just store the values for the next pipeline creation
    qCDebug(log_gstreamer_backend) << "Resolution and framerate updated for next pipeline creation";
}
