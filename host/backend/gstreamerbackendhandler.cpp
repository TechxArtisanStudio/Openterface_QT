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
#include <QDir>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QGraphicsVideoItem>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QProcess>
#include <QPainter>
#include <QGuiApplication>

// Platform-specific includes for window validation
#ifdef Q_OS_LINUX
#include <X11/Xlib.h>
#include <X11/Xutil.h>

// X11 error handler for video overlay setup
static bool x11_overlay_error_occurred = false;
static int x11_overlay_error_handler(Display* display, XErrorEvent* error) {
    x11_overlay_error_occurred = true;
    // Don't print error message as we handle it gracefully
    return 0;
}
#endif

// Logging category for GStreamer backend
Q_LOGGING_CATEGORY(log_gstreamer_backend, "opf.backend.gstreamer")

// GStreamer includes (conditional compilation)
#ifdef HAVE_GSTREAMER
#include <gst/gst.h>
#include <gst/video/videooverlay.h>
#include <gst/app/gstappsink.h>

// Static plugin registration declarations for static linking
// Updated to match plugins actually available in static-qt-complete Docker image
#ifdef GSTREAMER_STATIC_LINKING
extern "C" {
    // Core GStreamer plugins - confirmed available in static-qt-complete Docker image
    void gst_plugin_coreelements_register(void);      // queue, capsfilter, tee, etc.
    void gst_plugin_typefindfunctions_register(void); // typefind for format detection
    void gst_plugin_videoconvertscale_register(void); // videoconvert, videoscale
    void gst_plugin_videotestsrc_register(void);      // videotestsrc for testing
    void gst_plugin_ximagesink_register(void);        // ximagesink
    void gst_plugin_xvimagesink_register(void);       // xvimagesink - available in Docker
    void gst_plugin_playback_register(void);          // playbin, decodebin
    void gst_plugin_jpeg_register(void);              // jpegdec, jpegenc - NOW AVAILABLE
    void gst_plugin_qml6_register(void);              // qtsink, qml6glsink - Qt6 video sinks - NOW AVAILABLE
    
    // Additional plugins confirmed as .a files but register functions may not exist
    // void gst_plugin_jpegformat_register(void);     // JPEG format handling - NO REGISTER FUNCTION
    // void gst_plugin_fbdevsink_register(void);      // framebuffer device sink - NO REGISTER FUNCTION
    // void gst_plugin_v4l2codecs_register(void);     // V4L2 codec elements - NO REGISTER FUNCTION
    // void gst_plugin_autoconvert_register(void);    // autoconvert elements - NO REGISTER FUNCTION
    
    // Missing plugins - available in static-qt-complete Docker image
    void gst_plugin_video4linux2_register(void);   // v4l2src plugin - NOW AVAILABLE
    // void gst_plugin_videofilter_register(void);    // video filter base - NOT AVAILABLE
    void gst_plugin_autodetect_register(void);     // autovideosink - NOW AVAILABLE
    // void gst_plugin_avi_register(void);            // avimux, avidemux - NOT AVAILABLE
    // void gst_plugin_matroska_register(void);       // matroskamux, matroska demux - NOT AVAILABLE
}
#endif
#endif // HAVE_GSTREAMER

#include "gstreamer/sinkselector.h"
#include "gstreamer/pipelinebuilder.h"
#include "gstreamer/queueconfigurator.h"
#include "gstreamer/videooverlaymanager.h"
#include "gstreamer/pipelinefactory.h"

GStreamerBackendHandler::GStreamerBackendHandler(QObject *parent)
    : MultimediaBackendHandler(parent),
      m_pipeline(nullptr),
      m_source(nullptr),
      m_sink(nullptr),
      m_bus(nullptr),
      m_recordingPipeline(nullptr),
      m_recordingTee(nullptr),
      m_recordingValve(nullptr),
      m_recordingSink(nullptr),
      m_recordingQueue(nullptr),
      m_recordingEncoder(nullptr),
      m_recordingVideoConvert(nullptr),
      m_recordingMuxer(nullptr),
      m_recordingFileSink(nullptr),
      m_recordingAppSink(nullptr),
      m_recordingTeeSrcPad(nullptr),
      m_videoWidget(nullptr),
      m_graphicsVideoItem(nullptr),
      m_videoPane(nullptr),
      m_healthCheckTimer(new QTimer(this)),
      m_gstProcess(nullptr),
      m_pipelineRunning(false),
      m_currentFramerate(30),
      m_currentResolution(1280, 720),  // Initialize with a valid default resolution
      m_overlaySetupPending(false),
      m_recordingActive(false),
      m_recordingPaused(false),
      m_recordingStartTime(0),
      m_recordingPausedTime(0),
      m_totalPausedDuration(0),
      m_recordingFrameNumber(0),
      m_recordingProcess(nullptr),
      m_frameCaptureTimer(nullptr)
{
    m_config = getDefaultConfig();
    
    // Initialize recording config
    m_recordingConfig = RecordingConfig();
    
    // Initialize GStreamer if available
    initializeGStreamer();
    
    // Set up health check timer
    connect(m_healthCheckTimer, &QTimer::timeout, this, &GStreamerBackendHandler::checkPipelineHealth);
}

GStreamerBackendHandler::~GStreamerBackendHandler()
{
    // Stop recording first if active
    if (m_recordingActive) {
        stopRecording();
    }
    
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

void GStreamerBackendHandler::prepareCameraCreation()
{
    qCDebug(log_gstreamer_backend) << "GStreamer: Preparing camera creation";
    QThread::msleep(m_config.deviceSwitchDelay);
}

void GStreamerBackendHandler::setupCaptureSession(QMediaCaptureSession* session)
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

void GStreamerBackendHandler::startCamera()
{
     qCWarning(log_gstreamer_backend) << "GStreamer startCamera called";
     qCDebug(log_gstreamer_backend) << "Current device:" << m_currentDevice;
     qCDebug(log_gstreamer_backend) << "Current resolution:" << m_currentResolution;
     qCDebug(log_gstreamer_backend) << "Current framerate:" << m_currentFramerate;
     
    // Check if we should use direct GStreamer pipeline
    bool useDirectPipeline = true; // This could be configurable
    
    if (useDirectPipeline && !m_currentDevice.isEmpty()) {
        qCDebug(log_gstreamer_backend) << "GStreamer: Using direct pipeline";
        // Use extracted helper to create and start the direct pipeline
        if (startDirectPipeline()) {
            qCDebug(log_gstreamer_backend) << "GStreamer pipeline started successfully";
            return; // SUCCESS: Direct pipeline is running
        }
        // Failure will fall through and we may try alternative paths later
        useDirectPipeline = false;
    } else {
        qCWarning(log_gstreamer_backend) << "GStreamer: No valid device configured";
    }
}

bool GStreamerBackendHandler::startDirectPipeline()
{
    qCDebug(log_gstreamer_backend) << "GStreamer: Attempting to start direct pipeline for device:" << m_currentDevice
                                   << "resolution:" << m_currentResolution << "framerate:" << m_currentFramerate;

    if (m_currentDevice.isEmpty()) {
        qCWarning(log_gstreamer_backend) << "Cannot start direct pipeline: no device configured";
        return false;
    }

    if (!createGStreamerPipeline(m_currentDevice, m_currentResolution, m_currentFramerate)) {
        qCWarning(log_gstreamer_backend) << "Failed to create GStreamer pipeline";
        return false;
    }

    if (!startGStreamerPipeline()) {
        qCWarning(log_gstreamer_backend) << "Failed to start GStreamer pipeline";
        return false;
    }

    qCDebug(log_gstreamer_backend) << "Direct GStreamer pipeline started successfully for device:" << m_currentDevice;
    return true;
}

void GStreamerBackendHandler::stopCamera()
{
    qCDebug(log_gstreamer_backend) << "GStreamer: Stopping camera";
    
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

void GStreamerBackendHandler::configureCameraDevice()
{
    qCDebug(log_gstreamer_backend) << "Configuring camera device";
    
    // Call parent implementation for standard Qt camera configuration
    MultimediaBackendHandler::configureCameraDevice();
}

void GStreamerBackendHandler::setCurrentDevicePortChain(const QString& portChain)
{
    m_currentDevicePortChain = portChain;
    qCDebug(log_gstreamer_backend) << "Set current device port chain to:" << m_currentDevicePortChain;
}

void GStreamerBackendHandler::setCurrentDevice(const QString& devicePath)
{
    m_currentDevice = devicePath;
    qCDebug(log_gstreamer_backend) << "Set current device to:" << m_currentDevice;
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

void GStreamerBackendHandler::handleCameraError(int errorCode, const QString& errorString)
{
    qCCritical(log_gstreamer_backend) << "GStreamer Camera Error:" << errorCode << "-" << errorString;
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
    
    // Determine the appropriate video sink for current environment
    const QString platform = QGuiApplication::platformName();
    const bool isXcb = platform.contains("xcb", Qt::CaseInsensitive);
    const bool isWayland = platform.contains("wayland", Qt::CaseInsensitive);
    const bool hasXDisplay = !qgetenv("DISPLAY").isEmpty();
    const bool hasWaylandDisplay = !qgetenv("WAYLAND_DISPLAY").isEmpty();
    
    // Choose a video sink. Use OPENTERFACE_GST_SINK if provided; otherwise auto-detect.
    QString videoSink = Openterface::GStreamer::SinkSelector::selectSink(platform);
    
    qCDebug(log_gstreamer_backend) << "Selected video sink:" << videoSink << "(platform:" << platform
                                   << ", X DISPLAY:" << hasXDisplay << ", WAYLAND_DISPLAY:" << hasWaylandDisplay << ")";
    
    // Centralize pipeline creation and fallbacks in PipelineFactory (HAVE_GSTREAMER)
#ifdef HAVE_GSTREAMER
    QString err;
    m_pipeline = Openterface::GStreamer::PipelineFactory::createPipeline(device, resolution, framerate, videoSink, err);
    if (!m_pipeline) {
        qCCritical(log_gstreamer_backend) << "Failed to create any GStreamer pipeline:" << err;
        return false;
    }
    qCDebug(log_gstreamer_backend) << "PipelineFactory created pipeline successfully";
#else
    // No in-process GStreamer: just generate the pipeline string for external launch
    QString pipelineStr = generatePipelineString(device, resolution, framerate, videoSink);
    qCDebug(log_gstreamer_backend) << "Generated pipeline string (external gst-launch expected):" << pipelineStr;
#endif
    
    // Get bus for message handling with proper validation
    m_bus = gst_element_get_bus(m_pipeline);
    if (m_bus) {
        gst_bus_add_signal_watch(m_bus);
        qCDebug(log_gstreamer_backend) << "GStreamer bus initialized successfully";
        // Connect to Qt's signal system would require additional setup
    } else {
        qCWarning(log_gstreamer_backend) << "Failed to get GStreamer bus - error reporting will be limited";
    }
    
    // Configure queues (display & recording) using helper
    Openterface::GStreamer::QueueConfigurator::configureQueues(m_pipeline);
    
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
}

QString GStreamerBackendHandler::generatePipelineString(const QString& device, const QSize& resolution, int framerate, const QString& videoSink) const
{
    // Validate inputs before generating pipeline
    if (device.isEmpty()) {
        qCWarning(log_gstreamer_backend) << "Device path is empty, cannot generate pipeline";
        return QString();
    }
    
    if (resolution.width() <= 0 || resolution.height() <= 0) {
        qCWarning(log_gstreamer_backend) << "Invalid resolution:" << resolution << "- using fallback 1280x720";
        QSize fallbackResolution(1280, 720);  // Use more conservative fallback
        return generatePipelineString(device, fallbackResolution, framerate, videoSink);
    }
    
    if (framerate <= 0) {
        qCWarning(log_gstreamer_backend) << "Invalid framerate:" << framerate << "- using fallback 30fps";
        return generatePipelineString(device, resolution, 30, videoSink);
    }
    
    // Delegate to PipelineBuilder which centralizes pipeline templates
    return Openterface::GStreamer::PipelineBuilder::buildFlexiblePipeline(device, resolution, framerate, videoSink);
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

    // Only attempt overlay embedding on XCB (X11) platform when using an X sink
    const QString platform = QGuiApplication::platformName();
    const bool isXcb = platform.contains("xcb", Qt::CaseInsensitive);
    
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
                        // Ensure VideoPane is in GStreamer mode and overlay widget is ready
                        if (!videoPane->isDirectGStreamerModeEnabled()) {
                            qCDebug(log_gstreamer_backend) << "Enabling GStreamer mode on VideoPane for overlay setup";
                            videoPane->enableDirectGStreamerMode(true);
                        }
                        
                        // Use simplified approach from working v0.4.0: get window ID directly
                        windowId = videoPane->getVideoOverlayWindowId();
                        qCDebug(log_gstreamer_backend) << "Using VideoPane overlay widget window ID:" << windowId;
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
    }else {
        qCWarning(log_gstreamer_backend) << "No video output widget available for overlay setup";
    }
    
    if (windowId) {
        // Validate the window ID before attempting overlay setup
        if (!isValidWindowId(windowId)) {
            qCWarning(log_gstreamer_backend) << "Window ID" << windowId << "is invalid, skipping overlay setup";
            qCDebug(log_gstreamer_backend) << "Continuing with regular video output without embedding";
            windowId = 0; // Clear invalid window ID
        }
    }
    
    if (windowId) {
        // For ximagesink, we can set up overlay on both X11 and Wayland (via XWayland)
        // Find the video sink element - use working v0.4.0 approach
        GstElement* videoSink = gst_bin_get_by_name(GST_BIN(m_pipeline), "videosink");
        if (!videoSink) {
            // Fallback: find any element that supports video overlay
            videoSink = gst_bin_get_by_interface(GST_BIN(m_pipeline), GST_TYPE_VIDEO_OVERLAY);
        }
        
        if (videoSink) {
            // Delegate overlay setup to VideoOverlayManager which configures windows, properties and render rect
            bool ok = Openterface::GStreamer::VideoOverlayManager::setupVideoOverlayForPipeline(m_pipeline, windowId, m_videoWidget, m_graphicsVideoItem);
            if (!ok) {
                qCWarning(log_gstreamer_backend) << "Video overlay setup failed - continuing without embedding";
            } else {
                m_overlaySetupPending = false;
            }
            gst_object_unref(videoSink);
        } else {
            qCWarning(log_gstreamer_backend) << "Could not find any video sink element for overlay setup";
        }
    } else {
        qCWarning(log_gstreamer_backend) << "No valid window ID available, overlay setup skipped";
        // Mark that we need to set up overlay later when VideoPane is ready
        m_overlaySetupPending = true;
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
    
    // Wait for PLAYING state to be reached and confirm
    ret = gst_element_get_state(m_pipeline, &state, &pending, 5000 * GST_MSECOND);
    if (ret == GST_STATE_CHANGE_FAILURE || state != GST_STATE_PLAYING) {
        qCCritical(log_gstreamer_backend) << "Pipeline failed to reach PLAYING state. Current state:" << state << "Pending:" << pending;
        // Check for additional error messages
        if (m_bus) {
            GstMessage* msg = gst_bus_pop_filtered(m_bus, GST_MESSAGE_ERROR);
            if (msg) {
                GError* error = nullptr;
                gchar* debug_info = nullptr;
                gst_message_parse_error(msg, &error, &debug_info);
                qCCritical(log_gstreamer_backend) << "State change error:" << (error ? error->message : "Unknown");
                qCCritical(log_gstreamer_backend) << "Debug info:" << (debug_info ? debug_info : "None");
                if (error) g_error_free(error);
                if (debug_info) g_free(debug_info);
                gst_message_unref(msg);
            }
        }
        return false;
    }
    
    qCDebug(log_gstreamer_backend) << "Pipeline successfully reached PLAYING state";
    
    m_pipelineRunning = true;
    m_healthCheckTimer->start(2000); // Check health every 2 seconds (less frequent)
    
#else
    // Fallback: Use QProcess
    if (!m_gstProcess) {
        qCWarning(log_gstreamer_backend) << "No GStreamer process available";
        return false;
    }
    
    QString program = "gst-launch-1.0";
    QString pipelineStr = generatePipelineString(m_currentDevice, m_currentResolution, m_currentFramerate, "ximagesink");
    
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
        qCDebug(log_gstreamer_backend) << "Configuring video widget for GStreamer overlay";
        
        // Essential widget attributes for video overlay
        widget->setAttribute(Qt::WA_NativeWindow, true);
        widget->setAttribute(Qt::WA_PaintOnScreen, true);
        widget->setAttribute(Qt::WA_NoSystemBackground, true);
        widget->setAttribute(Qt::WA_OpaquePaintEvent, true);
        
        // Set black background to avoid flicker
        widget->setStyleSheet("background-color: black;");
        
        // Ensure widget is visible (required for winId generation)
        if (!widget->isVisible()) {
            qCDebug(log_gstreamer_backend) << "Making video widget visible for window ID generation";
            widget->show();
        }
        
        // Force window creation if needed
        WId winId = widget->winId();
        if (winId == 0) {
            qCDebug(log_gstreamer_backend) << "Forcing window creation for video widget";
            widget->createWinId();
            winId = widget->winId();
        }
        
        qCDebug(log_gstreamer_backend) << "Video widget configured with window ID:" << winId;
        
        // If pipeline is already created, set up the overlay immediately
        if (m_pipeline && winId != 0) {
            setupVideoOverlayForCurrentPipeline();
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
                    
                    // If overlay setup was pending, complete it now that VideoPane is ready
                    if (m_overlaySetupPending) {
                        qCDebug(log_gstreamer_backend) << "Completing pending overlay setup for VideoPane";
                        completePendingOverlaySetup();
                    }
                }
                
                // If pipeline is already created, set up the overlay immediately
                if (m_pipeline) {
                    embedVideoInGraphicsView(view);
                }
            }
        }
    }
}

void GStreamerBackendHandler::setVideoOutput(VideoPane* videoPane)
{
    m_videoWidget = nullptr;
    m_graphicsVideoItem = nullptr;
    m_videoPane = videoPane;  // Store VideoPane reference
    
    if (videoPane) {
        qCDebug(log_gstreamer_backend) << "VideoPane configured for GStreamer overlay";
        
        // Connect to video pane resize signal to update render rectangle
        connect(videoPane, &VideoPane::videoPaneResized, this, 
                static_cast<void (GStreamerBackendHandler::*)(const QSize&)>(&GStreamerBackendHandler::updateVideoRenderRectangle));
        qCDebug(log_gstreamer_backend) << "Connected VideoPane resize signal for automatic render rectangle updates";
        
        // Enable direct GStreamer mode on the VideoPane
        videoPane->enableDirectGStreamerMode(true);
        
        // If overlay setup was pending, complete it now that VideoPane is ready
        if (m_overlaySetupPending) {
            qCDebug(log_gstreamer_backend) << "Completing pending overlay setup for VideoPane";
            completePendingOverlaySetup();
        } else {
            // If pipeline is already created, set up the overlay immediately
            if (m_pipeline) {
                embedVideoInVideoPane(videoPane);
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
    
    // Set GStreamer log levels to minimum to suppress all non-essential messages
    gst_debug_set_default_threshold(GST_LEVEL_NONE);
    gst_debug_set_threshold_for_name("alsa", GST_LEVEL_NONE);
    gst_debug_set_threshold_for_name("pulse", GST_LEVEL_NONE);
    gst_debug_set_threshold_for_name("v4l2", GST_LEVEL_NONE);
    gst_debug_set_threshold_for_name("value", GST_LEVEL_NONE);  // Suppress gst_value_set_int_range_step warnings
    
    qCDebug(log_gstreamer_backend) << "GStreamer initialized successfully";
    
#ifdef GSTREAMER_STATIC_LINKING
    // Register static plugins required for video pipeline - only available plugins
    qCDebug(log_gstreamer_backend) << "Registering available static GStreamer plugins...";
    
    try {
        // Register core elements (queue, capsfilter, tee, etc.) - ESSENTIAL
        gst_plugin_coreelements_register();
        qCDebug(log_gstreamer_backend) << "✓ Registered coreelements plugin";
        
        // Register typefind functions for format detection - ESSENTIAL
        gst_plugin_typefindfunctions_register();
        qCDebug(log_gstreamer_backend) << "✓ Registered typefindfunctions plugin";
        
        // Register video conversion and scaling - ESSENTIAL
        gst_plugin_videoconvertscale_register();
        qCDebug(log_gstreamer_backend) << "✓ Registered videoconvertscale plugin";
        
        // Register video test source (for testing) - ESSENTIAL
        gst_plugin_videotestsrc_register();
        qCDebug(log_gstreamer_backend) << "✓ Registered videotestsrc plugin";
        
        // Register X11 video sink - ESSENTIAL for Linux
        gst_plugin_ximagesink_register();
        qCDebug(log_gstreamer_backend) << "✓ Registered ximagesink plugin";
        
        // Register XV video sink - CRITICAL: Must be available for tag 0.4.0 compatibility
        gst_plugin_xvimagesink_register();
        qCDebug(log_gstreamer_backend) << "✓ Registered xvimagesink plugin";
        
        // Verify that both video sinks are now available after registration
        GstElementFactory* testXviFactory = gst_element_factory_find("xvimagesink");
        GstElementFactory* testXimageFactory = gst_element_factory_find("ximagesink");
        qCDebug(log_gstreamer_backend) << "Post-registration verification - xvimagesink:" << (testXviFactory ? "available" : "MISSING");
        qCDebug(log_gstreamer_backend) << "Post-registration verification - ximagesink:" << (testXimageFactory ? "available" : "MISSING");
        if (testXviFactory) gst_object_unref(testXviFactory);
        if (testXimageFactory) gst_object_unref(testXimageFactory);
        
        // Register playback elements (decodebin, playbin) - ESSENTIAL
        gst_plugin_playback_register();
        qCDebug(log_gstreamer_backend) << "✓ Registered playback plugin (decodebin, playbin)";
        
        // Register video4linux2 plugin - CRITICAL: Provides v4l2src for camera input
        gst_plugin_video4linux2_register();
        qCDebug(log_gstreamer_backend) << "✓ Registered video4linux2 plugin (v4l2src)";
        
        // Register JPEG format handling - provides jpegdec and jpegenc
        gst_plugin_jpeg_register();
        qCDebug(log_gstreamer_backend) << "✓ Registered jpeg plugin (jpegdec, jpegenc)";
        
        // Register QML6 plugin - provides Qt6 video sinks (qtsink, qml6glsink)
        gst_plugin_qml6_register();
        qCDebug(log_gstreamer_backend) << "✓ Registered qml6 plugin (qtsink, qml6glsink)";
        
        // Register autodetect plugin - provides autovideosink and autoaudiosink
        gst_plugin_autodetect_register();
        qCDebug(log_gstreamer_backend) << "✓ Registered autodetect plugin (autovideosink, autoaudiosink)";
        
        // Register framebuffer device sink - for headless/framebuffer systems
        // gst_plugin_fbdevsink_register(); // Function doesn't exist - plugin loads automatically  
        // qCDebug(log_gstreamer_backend) << "✓ Registered fbdevsink plugin";
        
        // Register V4L2 codecs - for hardware acceleration when available
        // gst_plugin_v4l2codecs_register(); // Function doesn't exist - plugin loads automatically
        // qCDebug(log_gstreamer_backend) << "✓ Registered v4l2codecs plugin";
        
        // Additional plugins - only register if function exists
        // Note: These plugins exist as .a files but may not have register functions
        // gst_plugin_autoconvert_register();
        // qCDebug(log_gstreamer_backend) << "✓ Registered autoconvert plugin";
        
        qCDebug(log_gstreamer_backend) << "All available static GStreamer plugins registered successfully";
        
        // Check what essential elements are available after registration
        QStringList essentialElements = {"queue", "capsfilter", "tee", "videoconvert", "videoscale", 
                                       "videotestsrc", "ximagesink", "xvimagesink", "autovideosink", "qtsink"};
        
        // Also check for elements that auto-load from .a files (without explicit registration)
        QStringList autoLoadElements = {"jpegdec", "jpegenc", "fbdevsink"};
        
        QStringList availableElements, missingElements;
        for (const QString& elementName : essentialElements + autoLoadElements) {
            GstElementFactory* factory = gst_element_factory_find(elementName.toUtf8().data());
            if (factory) {
                availableElements << elementName;
                gst_object_unref(factory);
            } else {
                missingElements << elementName;
            }
        }
        
        qCDebug(log_gstreamer_backend) << "Available elements:" << availableElements;
        if (!missingElements.isEmpty()) {
            qCDebug(log_gstreamer_backend) << "Missing elements (will try auto-loading):" << missingElements;
        }
        
        // Check for v4l2src specifically (now available after registering video4linux2 plugin)
        GstElementFactory* v4l2Factory = gst_element_factory_find("v4l2src");
        if (v4l2Factory) {
            qCDebug(log_gstreamer_backend) << "✓ v4l2src element is available (from static video4linux2 plugin)";
            gst_object_unref(v4l2Factory);
        } else {
            qCWarning(log_gstreamer_backend) << "⚠ v4l2src element not available - registration may have failed, using videotestsrc fallback";
        }
        
    } catch (...) {
        qCCritical(log_gstreamer_backend) << "Exception occurred during plugin registration";
        return false;
    }

#endif
    
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
    return Openterface::GStreamer::VideoOverlayManager::embedVideoInWidget(m_pipeline, widget);
}

bool GStreamerBackendHandler::embedVideoInGraphicsView(QGraphicsView* view)
{
    return Openterface::GStreamer::VideoOverlayManager::embedVideoInGraphicsView(m_pipeline, view);
}

bool GStreamerBackendHandler::embedVideoInVideoPane(VideoPane* videoPane)
{
    return Openterface::GStreamer::VideoOverlayManager::embedVideoInVideoPane(m_pipeline, videoPane);
}

void GStreamerBackendHandler::completePendingOverlaySetup()
{
    Openterface::GStreamer::VideoOverlayManager::completePendingOverlaySetup(m_pipeline,
                                                                              m_videoWidget,
                                                                              m_graphicsVideoItem,
                                                                              m_videoPane,
                                                                              m_overlaySetupPending);
}

bool GStreamerBackendHandler::setupVideoOverlay(GstElement* videoSink, WId windowId)
{
    return Openterface::GStreamer::VideoOverlayManager::setupVideoOverlay(static_cast<void*>(videoSink), windowId);
    return false;
}

void GStreamerBackendHandler::setupVideoOverlayForCurrentPipeline()
{
    if (!m_pipeline) {
        qCDebug(log_gstreamer_backend) << "No pipeline available for overlay setup";
        return;
    }

    WId windowId = getVideoWidgetWindowId();
    if (windowId != 0) {
        bool ok = Openterface::GStreamer::VideoOverlayManager::setupVideoOverlayForPipeline(m_pipeline, windowId);
        if (ok) {
            m_overlaySetupPending = false;
            qCDebug(log_gstreamer_backend) << "Overlay setup completed for current pipeline";
        } else {
            qCWarning(log_gstreamer_backend) << "Failed to setup overlay for current pipeline";
        }
    } else {
        qCWarning(log_gstreamer_backend) << "No valid window ID available for overlay setup";
        m_overlaySetupPending = true; // Retry later
    }
}

void GStreamerBackendHandler::refreshVideoOverlay()
{
    qCDebug(log_gstreamer_backend) << "Refreshing video overlay";
    setupVideoOverlayForCurrentPipeline();
}

bool GStreamerBackendHandler::isValidWindowId(WId windowId) const
{
    if (windowId == 0) {
        qCWarning(log_gstreamer_backend) << "Window ID is 0 (invalid)";
        return false;
    }

#ifdef Q_OS_LINUX
    // Validate X11 window on Linux platforms
    Display* display = nullptr;
    
    // Try to get the X11 display from Qt
    if (QGuiApplication::platformName().contains("xcb")) {
        // For XCB platform, try to get X11 display
        // Note: QX11Info might not be available in all Qt builds
        try {
            display = XOpenDisplay(nullptr);
            if (!display) {
                qCWarning(log_gstreamer_backend) << "Could not open X11 display for window validation";
                return false;
            }
            
            // Check if the window exists
            XWindowAttributes attrs;
            int result = XGetWindowAttributes(display, static_cast<Window>(windowId), &attrs);
            XCloseDisplay(display);
            
            if (result == 0) {
                qCWarning(log_gstreamer_backend) << "Window ID" << windowId << "is not a valid X11 window";
                return false;
            }
            
            qCDebug(log_gstreamer_backend) << "Window ID" << windowId << "validated successfully (X11)";
            return true;
            
        } catch (...) {
            qCWarning(log_gstreamer_backend) << "Exception during X11 window validation for window ID" << windowId;
            if (display) XCloseDisplay(display);
            return false;
        }
    } else {
        // For Wayland or other platforms, we can't validate X11 windows
        // Accept the window ID as valid since Qt provides it
        qCDebug(log_gstreamer_backend) << "Window ID" << windowId << "accepted on platform:" << QGuiApplication::platformName();
        return true;
    }
#else
    // On non-Linux platforms, assume the window ID is valid
    qCDebug(log_gstreamer_backend) << "Window ID validation skipped on non-Linux platform";
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
        // Ensure widget is visible and has native window
        if (!m_videoWidget->isVisible()) {
            qCDebug(log_gstreamer_backend) << "Video widget not visible, making it visible";
            m_videoWidget->show();
        }
        
        // Force native window creation if needed
        if (!m_videoWidget->testAttribute(Qt::WA_NativeWindow)) {
            qCDebug(log_gstreamer_backend) << "Setting native window attribute for video widget";
            m_videoWidget->setAttribute(Qt::WA_NativeWindow, true);
            m_videoWidget->setAttribute(Qt::WA_PaintOnScreen, true);
        }
        
        WId windowId = m_videoWidget->winId();
        
        // Validate window ID and force creation if needed
        if (windowId == 0) {
            qCWarning(log_gstreamer_backend) << "Widget window ID is 0 - forcing window creation";
            m_videoWidget->createWinId();
            windowId = m_videoWidget->winId();
        }
        
        qCDebug(log_gstreamer_backend) << "Video widget window ID:" << windowId;
        return windowId;
    }
    
    if (m_graphicsVideoItem && m_graphicsVideoItem->scene()) {
        QList<QGraphicsView*> views = m_graphicsVideoItem->scene()->views();
        if (!views.isEmpty()) {
            QGraphicsView* view = views.first();
            
            // Ensure view is visible and has native window
            if (!view->isVisible()) {
                qCDebug(log_gstreamer_backend) << "Graphics view not visible, making it visible";
                view->show();
            }
            
            if (!view->testAttribute(Qt::WA_NativeWindow)) {
                qCDebug(log_gstreamer_backend) << "Setting native window attribute for graphics view";
                view->setAttribute(Qt::WA_NativeWindow, true);
                view->setAttribute(Qt::WA_PaintOnScreen, true);
            }
            
            WId windowId = view->winId();
            if (windowId == 0) {
                qCWarning(log_gstreamer_backend) << "Graphics view window ID is 0 - forcing window creation";
                view->createWinId();
                windowId = view->winId();
            }
            
            qCDebug(log_gstreamer_backend) << "Graphics view window ID:" << windowId;
            return windowId;
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

void GStreamerBackendHandler::updateVideoRenderRectangle(const QSize& widgetSize)
{
    updateVideoRenderRectangle(0, 0, widgetSize.width(), widgetSize.height());
}

void GStreamerBackendHandler::updateVideoRenderRectangle(int x, int y, int width, int height)
{
    if (!m_pipeline || !m_pipelineRunning) {
        qCDebug(log_gstreamer_backend) << "Pipeline not running, cannot update render rectangle";
        return;
    }
    
    // Find the video sink element in the pipeline
    GstElement* videoSink = gst_bin_get_by_name(GST_BIN(m_pipeline), "videosink");
    if (!videoSink) {
        // Try to find any element that supports video overlay
        videoSink = gst_bin_get_by_interface(GST_BIN(m_pipeline), GST_TYPE_VIDEO_OVERLAY);
    }
    
    if (videoSink && GST_IS_VIDEO_OVERLAY(videoSink)) {
        gst_video_overlay_set_render_rectangle(GST_VIDEO_OVERLAY(videoSink), x, y, width, height);
        qCDebug(log_gstreamer_backend) << "Updated render rectangle to:" << x << y << width << height;
        gst_object_unref(videoSink);
    } else {
        qCWarning(log_gstreamer_backend) << "Cannot update render rectangle: video sink not found or doesn't support overlay";
    }
}

// ============================================================================
// Video Recording Implementation
// ============================================================================

bool GStreamerBackendHandler::startRecording(const QString& outputPath, const QString& format, int videoBitrate)
{
    qCDebug(log_gstreamer_backend) << "GStreamer: Starting recording to" << outputPath << "format:" << format << "bitrate:" << videoBitrate;
    
    if (m_recordingActive) {
        QString error = "Recording is already active";
        qCWarning(log_gstreamer_backend) << error;
        emit recordingError(error);
        return false;
    }
    
    if (!m_pipeline || !m_pipelineRunning) {
        QString error = "Main pipeline not running - cannot start recording";
        qCWarning(log_gstreamer_backend) << error;
        qCWarning(log_gstreamer_backend) << "Pipeline state: exists=" << (m_pipeline != nullptr) << "running=" << m_pipelineRunning;
        emit recordingError(error);
        return false;
    }
    
    // Validate output path
    QFileInfo outputFileInfo(outputPath);
    QDir outputDir = outputFileInfo.dir();
    if (!outputDir.exists()) {
        QString error = QString("Output directory does not exist: %1").arg(outputDir.absolutePath());
        qCCritical(log_gstreamer_backend) << error;
        emit recordingError(error);
        return false;
    }
    
    QFileInfo dirInfo(outputDir.absolutePath());
    if (!dirInfo.isWritable()) {
        QString error = QString("Output directory is not writable: %1").arg(outputDir.absolutePath());
        qCCritical(log_gstreamer_backend) << error;
        emit recordingError(error);
        return false;
    }
    
    // Check if file already exists and warn user
    if (QFile::exists(outputPath)) {
        qCWarning(log_gstreamer_backend) << "Output file already exists and will be overwritten:" << outputPath;
    }
    
    // Update recording config
    m_recordingConfig.outputPath = outputPath;
    m_recordingConfig.format = format;
    m_recordingConfig.videoBitrate = videoBitrate;
    
    // Set default codec if not specified - check what's actually available
    if (m_recordingConfig.videoCodec.isEmpty()) {
        // Check for available encoders in the static build
#ifdef HAVE_GSTREAMER
        GstElementFactory* jpegFactory = gst_element_factory_find("jpegenc");
        if (jpegFactory) {
            m_recordingConfig.videoCodec = "mjpeg"; // Use mjpeg which maps to jpegenc
            gst_object_unref(jpegFactory);
        } else {
            // No encoder available in static build - recording not possible
            QString error = "No video encoders available in static GStreamer build - recording not supported";
            qCWarning(log_gstreamer_backend) << error;
            emit recordingError(error);
            return false;
        }
#else
        m_recordingConfig.videoCodec = "mjpeg"; // Assume system GStreamer has jpegenc
#endif
    }

#ifdef HAVE_GSTREAMER
    if (!initializeValveBasedRecording()) {
        QString error = QString("Failed to initialize valve-based recording for format %1").arg(format);
        qCCritical(log_gstreamer_backend) << error;
        emit recordingError(error);
        return false;
    }
#else
    QString error = "GStreamer support not available - recording not possible";
    qCWarning(log_gstreamer_backend) << error;
    emit recordingError(error);
    return false;
#endif
    
    m_recordingActive = true;
    m_recordingPaused = false;
    m_recordingOutputPath = outputPath;
    m_recordingStartTime = QDateTime::currentMSecsSinceEpoch();
    m_totalPausedDuration = 0;
    
    qCInfo(log_gstreamer_backend) << "Recording started successfully to:" << outputPath;
    emit recordingStarted(outputPath);
    
    return true;
}

bool GStreamerBackendHandler::stopRecording()
{
    qCDebug(log_gstreamer_backend) << "GStreamer: Stopping valve-based recording";
    
    if (!m_recordingActive) {
        qCDebug(log_gstreamer_backend) << "No active recording to stop";
        return false;
    }

#ifdef HAVE_GSTREAMER
    // For valve-based recording, close the valve and send EOS to finalize the file
    if (m_recordingValve && m_recordingFileSink) {
        qCDebug(log_gstreamer_backend) << "Closing recording valve and sending EOS";
        
        // First close the valve to stop new data
        g_object_set(m_recordingValve, "drop", TRUE, NULL);
        
        // Send EOS to the recording queue to properly close the file
        if (m_recordingQueue) {
            GstPad *queueSrcPad = gst_element_get_static_pad(m_recordingQueue, "src");
            if (queueSrcPad) {
                qCDebug(log_gstreamer_backend) << "Sending EOS to recording pipeline";
                gst_pad_send_event(queueSrcPad, gst_event_new_eos());
                gst_object_unref(queueSrcPad);
            }
        }
        
        // Wait a bit for EOS to propagate and file to close properly
        QThread::msleep(300);
        
        // Remove the recording elements from the pipeline
        qCDebug(log_gstreamer_backend) << "Removing recording elements from pipeline";
        
        // Set elements to NULL state before removing
        if (m_recordingFileSink) {
            gst_element_set_state(m_recordingFileSink, GST_STATE_NULL);
            gst_element_get_state(m_recordingFileSink, NULL, NULL, GST_CLOCK_TIME_NONE);
        }
        if (m_recordingMuxer) {
            gst_element_set_state(m_recordingMuxer, GST_STATE_NULL);
            gst_element_get_state(m_recordingMuxer, NULL, NULL, GST_CLOCK_TIME_NONE);
        }
        if (m_recordingEncoder) {
            gst_element_set_state(m_recordingEncoder, GST_STATE_NULL);
            gst_element_get_state(m_recordingEncoder, NULL, NULL, GST_CLOCK_TIME_NONE);
        }
        if (m_recordingVideoConvert) {
            gst_element_set_state(m_recordingVideoConvert, GST_STATE_NULL);
            gst_element_get_state(m_recordingVideoConvert, NULL, NULL, GST_CLOCK_TIME_NONE);
        }
        
        // Remove elements from pipeline
        if (m_recordingFileSink) {
            gst_bin_remove(GST_BIN(m_pipeline), m_recordingFileSink);
        }
        if (m_recordingMuxer) {
            gst_bin_remove(GST_BIN(m_pipeline), m_recordingMuxer);
        }
        if (m_recordingEncoder) {
            gst_bin_remove(GST_BIN(m_pipeline), m_recordingEncoder);
        }
        if (m_recordingVideoConvert) {
            gst_bin_remove(GST_BIN(m_pipeline), m_recordingVideoConvert);
        }
        
        // Clear references (valve and queue remain in pipeline for next recording)
        m_recordingVideoConvert = nullptr;
        m_recordingEncoder = nullptr;
        m_recordingMuxer = nullptr;
        m_recordingFileSink = nullptr;
        // Keep m_recordingValve and m_recordingQueue for reuse
        
        qCDebug(log_gstreamer_backend) << "Recording elements removed successfully";
    }
#endif
    
    m_recordingActive = false;
    m_recordingPaused = false;
    m_recordingOutputPath.clear();
    
    qCDebug(log_gstreamer_backend) << "Valve-based recording stopped successfully";
    emit recordingStopped();
    return true;
}

void GStreamerBackendHandler::pauseRecording()
{
    qCDebug(log_gstreamer_backend) << "GStreamer: Pausing valve-based recording";
    
    if (!m_recordingActive) {
        qCWarning(log_gstreamer_backend) << "No active recording to pause";
        return;
    }
    
    if (m_recordingPaused) {
        qCDebug(log_gstreamer_backend) << "Recording already paused";
        return;
    }
    
#ifdef HAVE_GSTREAMER
    // For valve-based recording, pause by temporarily closing the valve
    if (m_recordingValve) {
        g_object_set(m_recordingValve, "drop", TRUE, NULL);
        qCDebug(log_gstreamer_backend) << "Recording valve closed for pause";
    }
#endif
    
    m_recordingPaused = true;
    m_recordingPausedTime = QDateTime::currentMSecsSinceEpoch();
    
    qCDebug(log_gstreamer_backend) << "Valve-based recording paused";
    emit recordingPaused();
}

void GStreamerBackendHandler::resumeRecording()
{
    qCDebug(log_gstreamer_backend) << "GStreamer: Resuming valve-based recording";
    
    if (!m_recordingActive) {
        qCWarning(log_gstreamer_backend) << "No active recording to resume";
        return;
    }
    
    if (!m_recordingPaused) {
        qCDebug(log_gstreamer_backend) << "Recording not paused";
        return;
    }
    
#ifdef HAVE_GSTREAMER
    // Resume by opening the valve again
    if (m_recordingValve) {
        g_object_set(m_recordingValve, "drop", FALSE, NULL);
        qCDebug(log_gstreamer_backend) << "Recording valve opened for resume";
    }
#endif
    
    if (m_recordingPausedTime > 0) {
        m_totalPausedDuration += QDateTime::currentMSecsSinceEpoch() - m_recordingPausedTime;
        m_recordingPausedTime = 0;
    }
    
    m_recordingPaused = false;
    
    qCDebug(log_gstreamer_backend) << "Valve-based recording resumed";
    emit recordingResumed();
}

bool GStreamerBackendHandler::isRecording() const
{
    return m_recordingActive;
}

QString GStreamerBackendHandler::getCurrentRecordingPath() const
{
    return m_recordingOutputPath;
}

qint64 GStreamerBackendHandler::getRecordingDuration() const
{
    if (!m_recordingActive || m_recordingStartTime == 0) {
        return 0;
    }
    
    qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
    qint64 totalDuration = currentTime - m_recordingStartTime - m_totalPausedDuration;
    
    if (m_recordingPaused && m_recordingPausedTime > 0) {
        totalDuration -= (currentTime - m_recordingPausedTime);
    }
    
    return qMax(totalDuration, 0LL);
}

void GStreamerBackendHandler::setRecordingConfig(const RecordingConfig& config)
{
    m_recordingConfig = config;
}

GStreamerBackendHandler::RecordingConfig GStreamerBackendHandler::getRecordingConfig() const
{
    return m_recordingConfig;
}

#ifdef HAVE_GSTREAMER
bool GStreamerBackendHandler::createRecordingBranch(const QString& outputPath, const QString& format, int videoBitrate)
{
    qCDebug(log_gstreamer_backend) << "Starting frame-based recording to" << outputPath << "format:" << format;
    
    // Initialize recording using frame capture approach (similar to FFmpeg backend)
    m_recordingConfig.outputPath = outputPath;
    m_recordingConfig.format = format;
    m_recordingConfig.videoBitrate = videoBitrate;
    
    // Start the recording process
    if (!initializeFrameBasedRecording()) {
        qCCritical(log_gstreamer_backend) << "Failed to initialize frame-based recording";
        return false;
    }
    
    m_recordingActive = true;
    m_recordingPaused = false;
    m_recordingStartTime = QDateTime::currentMSecsSinceEpoch();
    m_recordingFrameNumber = 0;
    
    qCInfo(log_gstreamer_backend) << "Frame-based recording started successfully";
    emit recordingStarted(outputPath);
    return true;

    // Debug: Print the entire pipeline information
    qCDebug(log_gstreamer_backend) << "=== PIPELINE DEBUG INFORMATION ===";
    
    // List all elements in the pipeline for debugging
    GstIterator *iter = gst_bin_iterate_elements(GST_BIN(m_pipeline));
    GValue value = G_VALUE_INIT;
    gboolean done = FALSE;
    qCDebug(log_gstreamer_backend) << "Current Pipeline Elements:";
    while (!done) {
        switch (gst_iterator_next(iter, &value)) {
            case GST_ITERATOR_OK: {
                GstElement *element = GST_ELEMENT(g_value_get_object(&value));
                gchar *name = gst_element_get_name(element);
                gchar *factory_name = gst_element_get_factory(element) ? 
                    (gchar*)GST_OBJECT_NAME(gst_element_get_factory(element)) : (gchar*)"unknown";
                qCDebug(log_gstreamer_backend) << "  Element:" << name << "Type:" << factory_name;
                g_free(name);
                g_value_reset(&value);
                break;
            }
            case GST_ITERATOR_RESYNC:
                gst_iterator_resync(iter);
                break;
            case GST_ITERATOR_ERROR:
            case GST_ITERATOR_DONE:
                done = TRUE;
                break;
        }
    }
    g_value_unset(&value);
    gst_iterator_free(iter);
    qCDebug(log_gstreamer_backend) << "=== END PIPELINE DEBUG ===";

    // Find the tee element in the main pipeline
    m_recordingTee = gst_bin_get_by_name(GST_BIN(m_pipeline), "t");
    if (!m_recordingTee) {
        qCCritical(log_gstreamer_backend) << "CRITICAL ERROR: Could not find tee element 't' in main pipeline";
        QString error = "Recording failed: Pipeline lacks required tee element for video branching";
        emit recordingError(error);
        return false;
    }
    
    qCDebug(log_gstreamer_backend) << "Found tee element, creating recording elements...";
    
    // Create recording elements
    m_recordingQueue = gst_element_factory_make("queue", "recording-queue");
    m_recordingFileSink = gst_element_factory_make("filesink", "recording-filesink");
    
    if (!m_recordingQueue || !m_recordingFileSink) {
        qCCritical(log_gstreamer_backend) << "Failed to create basic recording elements";
        qCCritical(log_gstreamer_backend) << "  queue:" << (m_recordingQueue ? "OK" : "FAILED");
        qCCritical(log_gstreamer_backend) << "  filesink:" << (m_recordingFileSink ? "OK" : "FAILED");
        return false;
    }
    
    // Configure queue for LIVE recording with proper synchronization
    // CRITICAL: Configure queue for recording without frame drops and proper timing
    g_object_set(m_recordingQueue,
                 "max-size-buffers", 5,          // Allow small buffer for smooth recording
                 "max-size-bytes", 0,            // No byte limit
                 "max-size-time", (guint64)(200 * GST_MSECOND), // 200ms max buffering
                 "leaky", 2,                     // Drop old buffers (downstream)
                 "flush-on-eos", TRUE,           // Flush queue on end-of-stream
                 "silent", TRUE,                 // Reduce debug noise
                 NULL);
    
    qCDebug(log_gstreamer_backend) << "Recording queue configured for live recording with proper timing";
    
    // Try different encoder/muxer combinations based on format
    QStringList encoderOptions;
    QStringList muxerOptions;
    
    // Debug: Check what muxers are actually available
    qCDebug(log_gstreamer_backend) << "Checking available muxers:";
    QStringList testMuxers = {"qtmux", "mp4mux", "avimux", "matroskamux", "identity"};
    for (const QString& muxer : testMuxers) {
        GstElementFactory *factory = gst_element_factory_find(muxer.toLatin1().data());
        qCDebug(log_gstreamer_backend) << "  Muxer" << muxer << ":" << (factory ? "AVAILABLE" : "NOT FOUND");
        if (factory) gst_object_unref(factory);
    }
    
    // Since only jpegenc is available, create a direct MJPEG recording setup
    // For missing muxers, we'll use direct MJPEG encoding to file
    encoderOptions << "jpegenc";
    muxerOptions.clear(); // Clear muxers - we'll do direct encoding
    
    // For direct MJPEG recording, we don't need a muxer
    qCDebug(log_gstreamer_backend) << "Setting up direct MJPEG recording (no muxer needed)";
    
    // Try encoder/muxer combinations until we find one that works
    bool elementsCreated = false;
    QString usedEncoder = "unknown";
    QString usedMuxer = "unknown";
    
    // IMPROVED: More comprehensive availability checking with detailed logging
    qCDebug(log_gstreamer_backend) << "=== ENCODER/MUXER AVAILABILITY CHECK ===";
    
    QStringList availableEncoders, availableMuxers;
    
    // Check encoder availability
    for (const QString& encoderName : encoderOptions) {
        GstElementFactory *factory = gst_element_factory_find(encoderName.toLatin1().data());
        if (factory) {
            availableEncoders << encoderName;
            qCDebug(log_gstreamer_backend) << "✓ Encoder" << encoderName << "is AVAILABLE";
            gst_object_unref(factory);
        } else {
            qCDebug(log_gstreamer_backend) << "✗ Encoder" << encoderName << "is NOT AVAILABLE";
        }
    }
    
    // Check muxer availability
    for (const QString& muxerName : muxerOptions) {
        GstElementFactory *factory = gst_element_factory_find(muxerName.toLatin1().data());
        if (factory) {
            availableMuxers << muxerName;
            qCDebug(log_gstreamer_backend) << "✓ Muxer" << muxerName << "is AVAILABLE";
            gst_object_unref(factory);
        } else {
            qCDebug(log_gstreamer_backend) << "✗ Muxer" << muxerName << "is NOT AVAILABLE";
        }
    }
    
    qCDebug(log_gstreamer_backend) << "Available encoders:" << availableEncoders;
    qCDebug(log_gstreamer_backend) << "Available muxers:" << availableMuxers;
    qCDebug(log_gstreamer_backend) << "=== END AVAILABILITY CHECK ===";
    
    // For direct MJPEG recording without muxer
    if (availableEncoders.contains("jpegenc") && availableMuxers.isEmpty()) {
        qCDebug(log_gstreamer_backend) << "Creating direct MJPEG recording setup (no muxer)";
        
        // Create encoder only
        m_recordingEncoder = gst_element_factory_make("jpegenc", "recording-encoder");
        if (m_recordingEncoder) {
            m_recordingMuxer = nullptr; // No muxer needed for MJPEG
            usedEncoder = "jpegenc";
            usedMuxer = "none (direct)";
            elementsCreated = true;
            qCDebug(log_gstreamer_backend) << "✓ Successfully created direct MJPEG encoder";
        } else {
            qCCritical(log_gstreamer_backend) << "Failed to create jpegenc encoder";
        }
    } else {
        // Try creating elements with available combinations
        for (const QString& encoderName : availableEncoders) {
            for (const QString& muxerName : availableMuxers) {
                qCDebug(log_gstreamer_backend) << "Trying combination:" << encoderName << "+" << muxerName;
                
                // Create encoder
                GstElement* encoder = gst_element_factory_make(encoderName.toLatin1().data(), "recording-encoder");
                if (!encoder) {
                    qCWarning(log_gstreamer_backend) << "Failed to create encoder:" << encoderName;
                    continue;
                }
                
                // Create muxer
                GstElement* muxer = gst_element_factory_make(muxerName.toLatin1().data(), "recording-muxer");
                if (!muxer) {
                    qCWarning(log_gstreamer_backend) << "Failed to create muxer:" << muxerName;
                    gst_object_unref(encoder);
                    continue;
                }
                
                // Success! Use this combination
                m_recordingEncoder = encoder;
                m_recordingMuxer = muxer;
                usedEncoder = encoderName;
                usedMuxer = muxerName;
                elementsCreated = true;
                
                qCDebug(log_gstreamer_backend) << "✓ Successfully created encoder/muxer combination:" 
                                               << encoderName << "+" << muxerName;
                break;
            }
            
            if (elementsCreated) break;
        }
    
    // If standard combinations failed, try encoder-only approach (direct MJPEG)
    if (!elementsCreated && availableEncoders.contains("jpegenc")) {
        qCDebug(log_gstreamer_backend) << "Standard combinations failed, trying direct MJPEG encoding without muxer";
        
        GstElement* encoder = gst_element_factory_make("jpegenc", "recording-encoder");
        if (encoder) {
            m_recordingEncoder = encoder;
            m_recordingMuxer = nullptr; // No muxer for direct MJPEG
            usedEncoder = "jpegenc";
            usedMuxer = "none (direct MJPEG)";
            elementsCreated = true;
            
            qCDebug(log_gstreamer_backend) << "✓ Created direct MJPEG encoder without muxer";
        }
    }
    
    if (!elementsCreated) {
        qCCritical(log_gstreamer_backend) << "FAILED to create any working encoder/muxer combination for format:" << format;
        qCCritical(log_gstreamer_backend) << "Available encoders were:" << availableEncoders;
        qCCritical(log_gstreamer_backend) << "Available muxers were:" << availableMuxers;
        qCCritical(log_gstreamer_backend) << "This indicates a serious GStreamer plugin installation issue";
        
        // Emit detailed error message for user
        QString errorDetail = QString("No suitable encoder/muxer found for format %1. Available: encoders=%2, muxers=%3")
                                .arg(format)
                                .arg(availableEncoders.join(","))
                                .arg(availableMuxers.join(","));
        emit recordingError(errorDetail);
        return false;
    }
    
    // Debug each variable separately to find which one is causing the crash
    qCDebug(log_gstreamer_backend) << "Checking variables before logging...";
    qCDebug(log_gstreamer_backend) << "usedEncoder valid:" << (!usedEncoder.isNull());
    qCDebug(log_gstreamer_backend) << "usedMuxer valid:" << (!usedMuxer.isNull());
    qCDebug(log_gstreamer_backend) << "format valid:" << (!format.isNull());
    
    qCDebug(log_gstreamer_backend) << "usedEncoder value:" << usedEncoder;
    qCDebug(log_gstreamer_backend) << "usedMuxer value:" << usedMuxer;
    qCDebug(log_gstreamer_backend) << "format value:" << format;
    
    qCInfo(log_gstreamer_backend) << "Using recording setup:" << usedEncoder << "+" << usedMuxer << "for format:" << format;
    
    // Validate that we have valid strings before proceeding
    if (usedEncoder.isEmpty() || usedEncoder == "unknown") {
        qCCritical(log_gstreamer_backend) << "ERROR: usedEncoder is not properly set";
        return false;
    }
    
    // For direct MJPEG recording, change the file extension to .mjpeg for clarity
    qCDebug(log_gstreamer_backend) << "Processing output path:" << outputPath;
    QString finalOutputPath = outputPath;
    if (!m_recordingMuxer && usedEncoder == "jpegenc") {
        qCDebug(log_gstreamer_backend) << "Converting to MJPEG extension...";
        QFileInfo outputInfo(outputPath);
        QString baseName = outputInfo.completeBaseName();
        QString dirPath = outputInfo.path();
        qCDebug(log_gstreamer_backend) << "Base name:" << baseName << "Dir path:" << dirPath;
        finalOutputPath = dirPath + "/" + baseName + ".mjpeg";
        qCInfo(log_gstreamer_backend) << "Direct MJPEG recording - changing output to:" << finalOutputPath;
    }
    
    // Configure elements
    qCDebug(log_gstreamer_backend) << "Configuring filesink with path:" << finalOutputPath;
    QByteArray outputPathBytes = finalOutputPath.toLatin1();
    qCDebug(log_gstreamer_backend) << "Path bytes length:" << outputPathBytes.length();
    g_object_set(m_recordingFileSink, "location", outputPathBytes.constData(), NULL);
    qCDebug(log_gstreamer_backend) << "Filesink configured successfully";
    
    // Configure encoder based on type
    if (usedEncoder == "x264enc") {
        g_object_set(m_recordingEncoder, 
                     "bitrate", videoBitrate / 1000,  // x264enc expects kbps
                     "speed-preset", 1,               // ultrafast preset
                     NULL);
    } else if (usedEncoder == "jpegenc") {
        g_object_set(m_recordingEncoder, 
                     "quality", 85,                   // JPEG quality 0-100
                     NULL);
    }
    
    // Validate all elements before adding to pipeline
    qCDebug(log_gstreamer_backend) << "Validating recording elements before adding to pipeline...";
    qCDebug(log_gstreamer_backend) << "  m_recordingQueue:" << (m_recordingQueue ? "OK" : "NULL");
    qCDebug(log_gstreamer_backend) << "  m_recordingEncoder:" << (m_recordingEncoder ? "OK" : "NULL");
    qCDebug(log_gstreamer_backend) << "  m_recordingMuxer:" << (m_recordingMuxer ? "OK" : "NULL");
    qCDebug(log_gstreamer_backend) << "  m_recordingFileSink:" << (m_recordingFileSink ? "OK" : "NULL");
    qCDebug(log_gstreamer_backend) << "  m_pipeline:" << (m_pipeline ? "OK" : "NULL");
    
    if (!m_recordingQueue || !m_recordingEncoder || !m_recordingFileSink || !m_pipeline) {
        qCCritical(log_gstreamer_backend) << "CRITICAL: One or more recording elements are NULL - cannot proceed";
        return false;
    }
    
    // Add all elements to the main pipeline
    qCDebug(log_gstreamer_backend) << "Adding recording elements to pipeline...";
    if (m_recordingMuxer) {
        // Normal case: with muxer
        qCDebug(log_gstreamer_backend) << "Adding elements WITH muxer";
        gst_bin_add_many(GST_BIN(m_pipeline), 
                         m_recordingQueue, 
                         m_recordingEncoder, 
                         m_recordingMuxer, 
                         m_recordingFileSink, 
                         NULL);
    } else {
        // Fallback case: direct encoding without muxer
        qCDebug(log_gstreamer_backend) << "Adding elements WITHOUT muxer (direct MJPEG)";
        gst_bin_add_many(GST_BIN(m_pipeline), 
                         m_recordingQueue, 
                         m_recordingEncoder, 
                         m_recordingFileSink, 
                         NULL);
    }
    qCDebug(log_gstreamer_backend) << "Elements added to pipeline successfully";
    
    qCDebug(log_gstreamer_backend) << "Elements added to pipeline successfully";
    
    // Request a source pad from the tee
    qCDebug(log_gstreamer_backend) << "Requesting source pad from tee element...";
    if (!m_recordingTee) {
        qCCritical(log_gstreamer_backend) << "CRITICAL: m_recordingTee is NULL when trying to request pad";
        return false;
    }
    
    GstPadTemplate* tee_src_pad_template = gst_element_class_get_pad_template(GST_ELEMENT_GET_CLASS(m_recordingTee), "src_%u");
    if (!tee_src_pad_template) {
        qCCritical(log_gstreamer_backend) << "Failed to get tee source pad template";
        return false;
    }
    
    m_recordingTeeSrcPad = gst_element_request_pad(m_recordingTee, tee_src_pad_template, NULL, NULL);
    if (!m_recordingTeeSrcPad) {
        qCCritical(log_gstreamer_backend) << "Failed to request source pad from tee";
        return false;
    }
    qCDebug(log_gstreamer_backend) << "Successfully requested tee source pad";
    
    // Get sink pad from recording queue
    GstPad* queue_sink_pad = gst_element_get_static_pad(m_recordingQueue, "sink");
    if (!queue_sink_pad) {
        qCCritical(log_gstreamer_backend) << "Failed to get sink pad from recording queue";
        return false;
    }
    
    // Link tee source pad to queue sink pad
    GstPadLinkReturn link_result = gst_pad_link(m_recordingTeeSrcPad, queue_sink_pad);
    if (link_result != GST_PAD_LINK_OK) {
        qCCritical(log_gstreamer_backend) << "Failed to link tee to recording queue:" << link_result;
        gst_object_unref(queue_sink_pad);
        return false;
    }
    gst_object_unref(queue_sink_pad);
    
    // Link the rest of the recording chain
    if (m_recordingMuxer) {
        // Normal case: queue -> encoder -> muxer -> filesink
        if (!gst_element_link_many(m_recordingQueue, m_recordingEncoder, m_recordingMuxer, m_recordingFileSink, NULL)) {
            qCCritical(log_gstreamer_backend) << "Failed to link recording elements with muxer";
            return false;
        }
    } else {
        // Fallback case: queue -> encoder -> filesink (direct MJPEG)
        if (!gst_element_link_many(m_recordingQueue, m_recordingEncoder, m_recordingFileSink, NULL)) {
            qCCritical(log_gstreamer_backend) << "Failed to link recording elements without muxer";
            return false;
        }
    }
    
    // Sync element states with the main pipeline - CRITICAL for live recording
    // IMPORTANT: Elements must be set to READY state first, then synced to parent
    qCDebug(log_gstreamer_backend) << "Setting recording elements to READY state...";
    
    GstStateChangeReturn ret1 = gst_element_set_state(m_recordingQueue, GST_STATE_READY);
    GstStateChangeReturn ret2 = gst_element_set_state(m_recordingEncoder, GST_STATE_READY);
    GstStateChangeReturn ret3 = (m_recordingMuxer) ? gst_element_set_state(m_recordingMuxer, GST_STATE_READY) : GST_STATE_CHANGE_SUCCESS;
    GstStateChangeReturn ret4 = gst_element_set_state(m_recordingFileSink, GST_STATE_READY);
    
    if (ret1 == GST_STATE_CHANGE_FAILURE || ret2 == GST_STATE_CHANGE_FAILURE || 
        ret3 == GST_STATE_CHANGE_FAILURE || ret4 == GST_STATE_CHANGE_FAILURE) {
        qCCritical(log_gstreamer_backend) << "Failed to set recording elements to READY state";
        return false;
    }
    
    // Wait a bit for READY state to be reached (critical for proper initialization)
    QThread::msleep(100);
    
    // Now sync with parent pipeline (should transition to PLAYING state)
    qCDebug(log_gstreamer_backend) << "Syncing recording elements with parent pipeline state...";
    
    bool sync1 = gst_element_sync_state_with_parent(m_recordingQueue);
    bool sync2 = gst_element_sync_state_with_parent(m_recordingEncoder);
    bool sync3 = (m_recordingMuxer) ? gst_element_sync_state_with_parent(m_recordingMuxer) : true;
    bool sync4 = gst_element_sync_state_with_parent(m_recordingFileSink);
    
    if (!sync1 || !sync2 || !sync3 || !sync4) {
        qCWarning(log_gstreamer_backend) << "Warning: Some recording elements failed to sync with parent pipeline state";
        qCWarning(log_gstreamer_backend) << "Sync results - Queue:" << sync1 << "Encoder:" << sync2 
                                         << "Muxer:" << sync3 << "FileSink:" << sync4;
    }
    
    // CRITICAL: Wait for elements to actually reach PLAYING state before considering ready
    qCDebug(log_gstreamer_backend) << "Waiting for recording elements to reach PLAYING state...";
    
    GstState queue_state = GST_STATE_NULL, encoder_state = GST_STATE_NULL, filesink_state = GST_STATE_NULL;
    GstState muxer_state = GST_STATE_NULL;
    
    // Allow up to 2 seconds for state changes to complete
    GstClockTime timeout = 2000 * GST_MSECOND;
    
    gst_element_get_state(m_recordingQueue, &queue_state, NULL, timeout);
    gst_element_get_state(m_recordingEncoder, &encoder_state, NULL, timeout);
    if (m_recordingMuxer) {
        gst_element_get_state(m_recordingMuxer, &muxer_state, NULL, timeout);
    }
    gst_element_get_state(m_recordingFileSink, &filesink_state, NULL, timeout);
    
    qCDebug(log_gstreamer_backend) << "Recording element final states - Queue:" << queue_state 
                                   << "Encoder:" << encoder_state << "Muxer:" << muxer_state 
                                   << "FileSink:" << filesink_state;
    
    // Check if all elements reached at least PAUSED state (PLAYING may take time for file operations)
    bool allReady = (queue_state >= GST_STATE_PAUSED) && (encoder_state >= GST_STATE_PAUSED) && 
                    (filesink_state >= GST_STATE_PAUSED);
    if (m_recordingMuxer) {
        allReady = allReady && (muxer_state >= GST_STATE_PAUSED);
    }
    
    if (!allReady) {
        qCCritical(log_gstreamer_backend) << "CRITICAL: Recording elements failed to reach required state";
        qCCritical(log_gstreamer_backend) << "This will result in 0-byte recording files";
        return false;
    } else {
        qCDebug(log_gstreamer_backend) << "All recording elements successfully initialized and ready for data flow";
    }
    
    // Force a small amount of data through the pipeline to "prime" it
    // This ensures the recording branch is actively processing data
    qCDebug(log_gstreamer_backend) << "Priming recording pipeline to ensure data flow...";
    
    // Send a buffer probe to verify data is flowing through the tee AND ensure fresh timestamps
    if (m_recordingTeeSrcPad) {
        // Add a probe to monitor data flow and ensure timestamps are current
        gst_pad_add_probe(m_recordingTeeSrcPad, GST_PAD_PROBE_TYPE_BUFFER,
            [](GstPad *pad, GstPadProbeInfo *info, gpointer user_data) -> GstPadProbeReturn {
                static bool first_buffer = true;
                static GstClockTime last_timestamp = GST_CLOCK_TIME_NONE;
                
                GstBuffer *buffer = GST_PAD_PROBE_INFO_BUFFER(info);
                if (buffer) {
                    GstClockTime timestamp = GST_BUFFER_PTS(buffer);
                    
                    if (first_buffer) {
                        qCDebug(log_gstreamer_backend) << "GOOD: First buffer detected in recording branch";
                        qCDebug(log_gstreamer_backend) << "First buffer timestamp:" << timestamp;
                        first_buffer = false;
                        last_timestamp = timestamp;
                    } else {
                        // Check if we're getting fresh frames (not the same timestamp repeatedly)
                        if (timestamp != last_timestamp) {
                            qCDebug(log_gstreamer_backend) << "Recording branch receiving fresh frames - timestamp changed from" 
                                                           << last_timestamp << "to" << timestamp;
                        } else {
                            qCWarning(log_gstreamer_backend) << "WARNING: Recording branch receiving duplicate timestamp!" 
                                                            << timestamp << "- may indicate frame repetition issue";
                        }
                        last_timestamp = timestamp;
                    }
                }
                return GST_PAD_PROBE_OK; // Allow buffer to continue
            }, nullptr, nullptr);
    }
    
    qCDebug(log_gstreamer_backend) << "Recording branch created and linked successfully";
    return true;
}
}

void GStreamerBackendHandler::removeRecordingBranch()
{
    qCDebug(log_gstreamer_backend) << "Removing recording branch";
    
    // Handle separate recording pipeline case
    if (m_recordingPipeline) {
        qCDebug(log_gstreamer_backend) << "Cleaning up separate recording pipeline";
        
        // Send EOS to properly close the file (non-blocking)
        gst_element_send_event(m_recordingPipeline, gst_event_new_eos());
        
        // Set state to NULL immediately (don't wait for EOS processing)
        gst_element_set_state(m_recordingPipeline, GST_STATE_NULL);
        
        // Wait for state change to complete (with timeout)
        GstStateChangeReturn ret = gst_element_get_state(m_recordingPipeline, NULL, NULL, GST_SECOND);
        if (ret == GST_STATE_CHANGE_FAILURE) {
            qCWarning(log_gstreamer_backend) << "Failed to stop recording pipeline gracefully";
        }
        
        gst_object_unref(m_recordingPipeline);
        m_recordingPipeline = nullptr;
        
        qCDebug(log_gstreamer_backend) << "Separate recording pipeline cleaned up";
        return;
    }
    
    // Handle tee-based recording branch case
    if (!m_pipeline) {
        return;
    }
    
    // Send EOS to recording branch (non-blocking)
    if (m_recordingTeeSrcPad) {
        gst_pad_send_event(m_recordingTeeSrcPad, gst_event_new_eos());
    }
    
    // Set elements to NULL state before unlinking (don't block on state changes)
    if (m_recordingQueue) {
        gst_element_set_state(m_recordingQueue, GST_STATE_NULL);
    }
    if (m_recordingEncoder) {
        gst_element_set_state(m_recordingEncoder, GST_STATE_NULL);
    }
    if (m_recordingMuxer) {
        gst_element_set_state(m_recordingMuxer, GST_STATE_NULL);
    }
    if (m_recordingFileSink) {
        gst_element_set_state(m_recordingFileSink, GST_STATE_NULL);
    }
    
    // Unlink and remove elements
    if (m_recordingTeeSrcPad && m_recordingQueue) {
        GstPad* queue_sink_pad = gst_element_get_static_pad(m_recordingQueue, "sink");
        if (queue_sink_pad) {
            gst_pad_unlink(m_recordingTeeSrcPad, queue_sink_pad);
            gst_object_unref(queue_sink_pad);
        }
    }
    
    // Release the tee pad
    if (m_recordingTee && m_recordingTeeSrcPad) {
        gst_element_release_request_pad(m_recordingTee, m_recordingTeeSrcPad);
        gst_object_unref(m_recordingTeeSrcPad);
        m_recordingTeeSrcPad = nullptr;
    }
    
    // Remove elements from pipeline
    if (m_recordingQueue) {
        gst_bin_remove(GST_BIN(m_pipeline), m_recordingQueue);
        m_recordingQueue = nullptr;
    }
    if (m_recordingEncoder) {
        gst_bin_remove(GST_BIN(m_pipeline), m_recordingEncoder);
        m_recordingEncoder = nullptr;
    }
    if (m_recordingMuxer) {
        gst_bin_remove(GST_BIN(m_pipeline), m_recordingMuxer);
        m_recordingMuxer = nullptr;
    }
    if (m_recordingFileSink) {
        gst_bin_remove(GST_BIN(m_pipeline), m_recordingFileSink);
        m_recordingFileSink = nullptr;
    }
    
    // Clear tee reference
    if (m_recordingTee) {
        gst_object_unref(m_recordingTee);
        m_recordingTee = nullptr;
    }
    
    qCDebug(log_gstreamer_backend) << "Recording branch removed successfully";
}

QString GStreamerBackendHandler::generateRecordingElements(const QString& outputPath, const QString& format, int videoBitrate) const
{
    // This method is kept for reference but not used in the new tee-based approach
    QString encoder, muxer;
    
    if (format.toLower() == "mp4") {
        encoder = "x264enc";
        muxer = "mp4mux";
    } else if (format.toLower() == "avi") {
        encoder = "jpegenc";
        muxer = "avimux";
    } else if (format.toLower() == "mkv") {
        encoder = "x264enc";
        muxer = "matroskamux";
    } else {
        encoder = "jpegenc";
        muxer = "avimux";
    }
    
    return QString("queue ! %1 ! %2 ! filesink location=%3").arg(encoder, muxer, outputPath);
}

bool GStreamerBackendHandler::initializeValveBasedRecording()
{
    qCDebug(log_gstreamer_backend) << "Initializing valve-based recording";
    qCDebug(log_gstreamer_backend) << "Using resolution:" << m_currentResolution << "framerate:" << m_currentFramerate;
    
#ifdef HAVE_GSTREAMER
    if (!m_pipeline) {
        qCCritical(log_gstreamer_backend) << "No GStreamer pipeline available for recording";
        return false;
    }
    
    // Debug: List all elements in the pipeline to see what's actually there
    qCDebug(log_gstreamer_backend) << "=== PIPELINE ELEMENTS DEBUG ===";
    GstIterator *iter = gst_bin_iterate_elements(GST_BIN(m_pipeline));
    GValue value = G_VALUE_INIT;
    gboolean done = FALSE;
    QStringList foundElements;
    while (!done) {
        switch (gst_iterator_next(iter, &value)) {
            case GST_ITERATOR_OK: {
                GstElement *element = GST_ELEMENT(g_value_get_object(&value));
                gchar *name = gst_element_get_name(element);
                foundElements << QString(name);
                g_free(name);
                g_value_reset(&value);
                break;
            }
            case GST_ITERATOR_RESYNC:
                gst_iterator_resync(iter);
                break;
            case GST_ITERATOR_ERROR:
            case GST_ITERATOR_DONE:
                done = TRUE;
                break;
        }
    }
    g_value_unset(&value);
    gst_iterator_free(iter);
    qCDebug(log_gstreamer_backend) << "Found elements in pipeline:" << foundElements;
    qCDebug(log_gstreamer_backend) << "=== END PIPELINE DEBUG ===";
    
    // Debug: Check if valve plugin is available
    GstElementFactory* valveFactory = gst_element_factory_find("valve");
    if (!valveFactory) {
        qCWarning(log_gstreamer_backend) << "Valve element not available in GStreamer installation - falling back to direct recording";
        return initializeDirectFilesinkRecording();
    }
    gst_object_unref(valveFactory);
    
    // Find the recording valve in the existing pipeline
    m_recordingValve = gst_bin_get_by_name(GST_BIN(m_pipeline), "recording-valve");
    if (!m_recordingValve) {
        qCWarning(log_gstreamer_backend) << "Recording valve not found in pipeline - pipeline may not have valve structure";
        qCDebug(log_gstreamer_backend) << "Falling back to direct recording approach";
        return initializeDirectFilesinkRecording();
    }
    
    // Find the recording queue and identity element
    m_recordingQueue = gst_bin_get_by_name(GST_BIN(m_pipeline), "recording-queue");
    GstElement* recordingReady = gst_bin_get_by_name(GST_BIN(m_pipeline), "recording-ready");
    
    if (!m_recordingQueue || !recordingReady) {
        qCWarning(log_gstreamer_backend) << "Recording queue or ready element not found in pipeline";
        qCDebug(log_gstreamer_backend) << "Queue found:" << (m_recordingQueue ? "yes" : "no");
        qCDebug(log_gstreamer_backend) << "Ready element found:" << (recordingReady ? "yes" : "no");
        qCDebug(log_gstreamer_backend) << "Falling back to direct recording approach";
        if (m_recordingQueue) gst_object_unref(m_recordingQueue);
        if (recordingReady) gst_object_unref(recordingReady);
        gst_object_unref(m_recordingValve);
        return initializeDirectFilesinkRecording();
    }
    
    // Create recording encoder chain: videoconvert -> encoder -> muxer -> filesink
    GstElement* videoconvert = gst_element_factory_make("videoconvert", "recording-convert");
    GstElement* encoder = nullptr;
    GstElement* muxer = nullptr;
    GstElement* filesink = gst_element_factory_make("filesink", "recording-filesink");
    
    if (!videoconvert || !filesink) {
        qCCritical(log_gstreamer_backend) << "Failed to create basic recording elements";
        if (videoconvert) gst_object_unref(videoconvert);
        if (filesink) gst_object_unref(filesink);
        gst_object_unref(m_recordingQueue);
        gst_object_unref(recordingReady);
        gst_object_unref(m_recordingValve);
        return false;
    }
    
    // Set output file location
    g_object_set(filesink, "location", m_recordingConfig.outputPath.toUtf8().data(), NULL);
    
    // Create encoder and muxer based on format
    if (m_recordingConfig.format.toLower() == "avi") {
        encoder = gst_element_factory_make("jpegenc", "recording-encoder");
        muxer = gst_element_factory_make("avimux", "recording-muxer");
        if (!muxer) {
            qCDebug(log_gstreamer_backend) << "avimux not available, trying matroskamux";
            muxer = gst_element_factory_make("matroskamux", "recording-muxer");
        }
    } else if (m_recordingConfig.format.toLower() == "mp4") {
        encoder = gst_element_factory_make("x264enc", "recording-encoder");
        muxer = gst_element_factory_make("mp4mux", "recording-muxer");
        if (!muxer) {
            qCDebug(log_gstreamer_backend) << "mp4mux not available, trying qtmux";
            muxer = gst_element_factory_make("qtmux", "recording-muxer");
        }
    } else {
        // Default to MJPEG/AVI
        encoder = gst_element_factory_make("jpegenc", "recording-encoder");
        muxer = gst_element_factory_make("avimux", "recording-muxer");
        if (!muxer) {
            muxer = gst_element_factory_make("matroskamux", "recording-muxer");
        }
    }
    
    if (!encoder) {
        qCWarning(log_gstreamer_backend) << "Preferred encoder not available, trying jpegenc as fallback";
        encoder = gst_element_factory_make("jpegenc", "recording-encoder");
    }
    
    if (!encoder || !muxer) {
        qCCritical(log_gstreamer_backend) << "Failed to create encoder or muxer";
        if (videoconvert) gst_object_unref(videoconvert);
        if (encoder) gst_object_unref(encoder);
        if (muxer) gst_object_unref(muxer);
        if (filesink) gst_object_unref(filesink);
        gst_object_unref(m_recordingQueue);
        gst_object_unref(recordingReady);
        gst_object_unref(m_recordingValve);
        return false;
    }
    
    // Configure encoder settings
    if (g_object_class_find_property(G_OBJECT_GET_CLASS(encoder), "quality")) {
        g_object_set(encoder, "quality", 85, NULL);  // High quality for JPEG
    }
    if (g_object_class_find_property(G_OBJECT_GET_CLASS(encoder), "bitrate")) {
        g_object_set(encoder, "bitrate", m_recordingConfig.videoBitrate, NULL);
    }
    
    // Store references
    m_recordingVideoConvert = videoconvert;
    m_recordingEncoder = encoder;
    m_recordingMuxer = muxer;
    m_recordingFileSink = filesink;
    
    // Add new elements to pipeline
    gst_bin_add_many(GST_BIN(m_pipeline), videoconvert, encoder, muxer, filesink, NULL);
    
    // Link the recording chain: recording-ready -> videoconvert -> encoder -> muxer -> filesink
    if (!gst_element_link_many(recordingReady, videoconvert, encoder, muxer, filesink, NULL)) {
        qCCritical(log_gstreamer_backend) << "Failed to link recording chain";
        gst_object_unref(recordingReady);
        return false;
    }
    
    // Sync all new elements with pipeline state
    gst_element_sync_state_with_parent(videoconvert);
    gst_element_sync_state_with_parent(encoder);
    gst_element_sync_state_with_parent(muxer);
    gst_element_sync_state_with_parent(filesink);
    
    // CRITICAL: Open the valve to allow data flow
    g_object_set(m_recordingValve, "drop", FALSE, NULL);
    qCDebug(log_gstreamer_backend) << "Recording valve opened for recording";
    
    qCDebug(log_gstreamer_backend) << "Valve-based recording initialized successfully";
    qCDebug(log_gstreamer_backend) << "Recording chain: valve -> queue -> convert -> encode -> mux -> filesink";
    
    gst_object_unref(recordingReady);
    return true;
    
#else
    qCCritical(log_gstreamer_backend) << "GStreamer support not compiled in";
    return false;
#endif
}

bool GStreamerBackendHandler::initializeFrameBasedRecording()
{
    qCDebug(log_gstreamer_backend) << "Initializing GStreamer recording (filesink-based approach)";
    qCDebug(log_gstreamer_backend) << "Using resolution:" << m_currentResolution << "framerate:" << m_currentFramerate;
    
    // Check for required GStreamer plugins
    qCDebug(log_gstreamer_backend) << "Checking for required GStreamer plugins...";
    GstRegistry* registry = gst_registry_get();
    
    GstPlugin* corePlugin = gst_registry_find_plugin(registry, "coreelements");
    GstPlugin* videoPlugin = gst_registry_find_plugin(registry, "videoconvert");
    GstPlugin* appPlugin = gst_registry_find_plugin(registry, "app");
    
    qCDebug(log_gstreamer_backend) << "coreelements plugin:" << (corePlugin ? "found" : "NOT FOUND");
    qCDebug(log_gstreamer_backend) << "videoconvert plugin:" << (videoPlugin ? "found" : "NOT FOUND");
    qCDebug(log_gstreamer_backend) << "app plugin:" << (appPlugin ? "found" : "NOT FOUND");
    
    if (corePlugin) gst_object_unref(corePlugin);
    if (videoPlugin) gst_object_unref(videoPlugin);
    if (appPlugin) gst_object_unref(appPlugin);
    
    // Since appsink is not available, use direct filesink approach
    if (!appPlugin) {
        qCDebug(log_gstreamer_backend) << "appsink not available, using direct GStreamer filesink recording";
        return initializeDirectFilesinkRecording();
    }
    
#ifdef HAVE_GSTREAMER
    if (!m_pipeline) {
        qCCritical(log_gstreamer_backend) << "No GStreamer pipeline available for recording";
        return false;
    }
    
    // Validate current resolution and framerate
    if (m_currentResolution.width() <= 0 || m_currentResolution.height() <= 0) {
        qCCritical(log_gstreamer_backend) << "Invalid current resolution:" << m_currentResolution;
        return false;
    }
    
    if (m_currentFramerate <= 0) {
        qCCritical(log_gstreamer_backend) << "Invalid current framerate:" << m_currentFramerate;
        return false;
    }
    
    // Clean up any existing recording process
    if (m_recordingProcess) {
        m_recordingProcess->terminate();
        if (!m_recordingProcess->waitForFinished(3000)) {
            m_recordingProcess->kill();
        }
        delete m_recordingProcess;
        m_recordingProcess = nullptr;
    }
    
    // Add appsink to the tee for frame capture
    GstElement* tee = gst_bin_get_by_name(GST_BIN(m_pipeline), "t");
    if (!tee) {
        qCCritical(log_gstreamer_backend) << "Could not find tee element 't' for recording";
        qCCritical(log_gstreamer_backend) << "This means the pipeline template doesn't include a tee element";
        qCCritical(log_gstreamer_backend) << "Check that the pipeline template includes 'tee name=t'";
        return false;
    }
    
    qCDebug(log_gstreamer_backend) << "Found tee element, creating recording branch elements...";
    
    // Create recording branch with appsink
    GstElement* queue = gst_element_factory_make("queue", "recording-queue");
    if (!queue) {
        qCCritical(log_gstreamer_backend) << "Failed to create 'queue' element - check GStreamer coreelements plugin";
    }
    
    GstElement* videoconvert = gst_element_factory_make("videoconvert", "recording-convert");
    if (!videoconvert) {
        qCCritical(log_gstreamer_backend) << "Failed to create 'videoconvert' element - check GStreamer videoconvert plugin";
        // Try alternative
        videoconvert = gst_element_factory_make("videoconvertscale", "recording-convert");
        if (videoconvert) {
            qCDebug(log_gstreamer_backend) << "Using videoconvertscale as fallback for videoconvert";
        }
    }
    
    GstElement* appsink = gst_element_factory_make("appsink", "recording-appsink");
    if (!appsink) {
        qCCritical(log_gstreamer_backend) << "Failed to create 'appsink' element - check GStreamer app plugin";
    }
    
    if (!queue || !videoconvert || !appsink) {
        qCCritical(log_gstreamer_backend) << "Failed to create recording pipeline elements";
        qCCritical(log_gstreamer_backend) << "queue:" << (queue ? "OK" : "FAILED");
        qCCritical(log_gstreamer_backend) << "videoconvert:" << (videoconvert ? "OK" : "FAILED");
        qCCritical(log_gstreamer_backend) << "appsink:" << (appsink ? "OK" : "FAILED");
        
        // Let's try an even simpler approach - use fakesink to test basic pipeline creation
        if (!queue || !appsink) {
            qCCritical(log_gstreamer_backend) << "Basic elements (queue/appsink) not available - GStreamer installation issue";
        } else if (!videoconvert) {
            qCCritical(log_gstreamer_backend) << "Trying without videoconvert (direct format)...";
            // Try without videoconvert - direct format matching
            gst_object_unref(videoconvert);
            videoconvert = nullptr;
        }
        
        if (queue) gst_object_unref(queue);
        if (videoconvert) gst_object_unref(videoconvert);
        if (appsink) gst_object_unref(appsink);
        gst_object_unref(tee);
        return false;
    }
    
    // Configure appsink for RGB frame capture using current resolution and framerate
    GstCaps* caps = gst_caps_new_simple("video/x-raw",
                                        "format", G_TYPE_STRING, "RGB",
                                        "width", G_TYPE_INT, m_currentResolution.width(),
                                        "height", G_TYPE_INT, m_currentResolution.height(),
                                        "framerate", GST_TYPE_FRACTION, m_currentFramerate, 1,
                                        NULL);
    
    g_object_set(appsink,
                 "caps", caps,
                 "emit-signals", TRUE,
                 "sync", FALSE,
                 "drop", TRUE,
                 "max-buffers", 5,
                 NULL);
    
    gst_caps_unref(caps);
    
    // Store appsink reference for cleanup
    m_recordingAppSink = appsink;
    
    // Connect appsink new-sample callback
    g_signal_connect(appsink, "new-sample", G_CALLBACK(+[](GstAppSink* sink, gpointer user_data) -> GstFlowReturn {
        GStreamerBackendHandler* handler = static_cast<GStreamerBackendHandler*>(user_data);
        return handler->onNewRecordingSample(sink);
    }), this);
    
    // Add elements to pipeline
    if (videoconvert) {
        gst_bin_add_many(GST_BIN(m_pipeline), queue, videoconvert, appsink, NULL);
    } else {
        qCDebug(log_gstreamer_backend) << "Adding elements without videoconvert";
        gst_bin_add_many(GST_BIN(m_pipeline), queue, appsink, NULL);
    }
    
    // Link tee to recording branch
    GstPad* teeSrcPad = gst_element_request_pad_simple(tee, "src_%u");
    GstPad* queueSinkPad = gst_element_get_static_pad(queue, "sink");
    
    if (!teeSrcPad || !queueSinkPad) {
        qCCritical(log_gstreamer_backend) << "Failed to get pads for recording branch";
        gst_object_unref(tee);
        return false;
    }
    
    if (gst_pad_link(teeSrcPad, queueSinkPad) != GST_PAD_LINK_OK) {
        qCCritical(log_gstreamer_backend) << "Failed to link tee to recording branch";
        gst_object_unref(teeSrcPad);
        gst_object_unref(queueSinkPad);
        gst_object_unref(tee);
        return false;
    }
    
    gst_object_unref(teeSrcPad);
    gst_object_unref(queueSinkPad);
    gst_object_unref(tee);
    
    // Link recording elements
    if (videoconvert) {
        if (!gst_element_link_many(queue, videoconvert, appsink, NULL)) {
            qCCritical(log_gstreamer_backend) << "Failed to link recording pipeline elements (with videoconvert)";
            return false;
        }
    } else {
        if (!gst_element_link(queue, appsink)) {
            qCCritical(log_gstreamer_backend) << "Failed to link recording pipeline elements (direct)";
            return false;
        }
    }
    
    // Sync elements with pipeline state
    gst_element_sync_state_with_parent(queue);
    if (videoconvert) {
        gst_element_sync_state_with_parent(videoconvert);
    }
    gst_element_sync_state_with_parent(appsink);
    
    // Create FFmpeg process for encoding
    m_recordingProcess = new QProcess(this);
    
    // Set up process signals
    connect(m_recordingProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &GStreamerBackendHandler::onRecordingProcessFinished);
    connect(m_recordingProcess, &QProcess::errorOccurred,
            this, &GStreamerBackendHandler::onRecordingProcessError);
    
    // Prepare FFmpeg command for frame-based recording from stdin
    QStringList arguments;
    arguments << "-f" << "rawvideo";
    arguments << "-pix_fmt" << "rgb24";
    arguments << "-s" << QString("%1x%2").arg(m_currentResolution.width()).arg(m_currentResolution.height());
    arguments << "-r" << QString::number(m_currentFramerate); // Use actual frame rate
    arguments << "-i" << "-"; // Read from stdin
    
    // Configure output based on format
    if (m_recordingConfig.format.toLower() == "avi") {
        arguments << "-c:v" << "mjpeg";
        arguments << "-q:v" << "2"; // High quality
    } else if (m_recordingConfig.format.toLower() == "mp4") {
        arguments << "-c:v" << "libx264";
        arguments << "-preset" << "fast";
        arguments << "-crf" << "23";
    } else {
        // Default to AVI with MJPEG
        arguments << "-c:v" << "mjpeg";
        arguments << "-q:v" << "2";
    }
    
    arguments << "-y"; // Overwrite output file
    arguments << m_recordingConfig.outputPath;
    
    qCDebug(log_gstreamer_backend) << "FFmpeg command:" << "ffmpeg" << arguments.join(" ");
    
    // Start FFmpeg process
    m_recordingProcess->start("ffmpeg", arguments);
    if (!m_recordingProcess->waitForStarted(5000)) {
        qCCritical(log_gstreamer_backend) << "Failed to start FFmpeg process:" << m_recordingProcess->errorString();
        delete m_recordingProcess;
        m_recordingProcess = nullptr;
        return false;
    }
    
    qCDebug(log_gstreamer_backend) << "GStreamer appsink recording initialized successfully";
    return true;
    
#else
    qCCritical(log_gstreamer_backend) << "GStreamer support not compiled in";
    return false;
#endif
}

bool GStreamerBackendHandler::createSeparateRecordingPipeline(const QString& outputPath, const QString& format, int videoBitrate)
{
    qCDebug(log_gstreamer_backend) << "Adding recording branch to existing pipeline using tee";
    
    if (!m_pipeline || !m_recordingTee) {
        qCCritical(log_gstreamer_backend) << "Main pipeline or tee not available for recording";
        return false;
    }
    
    // Clean up any existing recording elements
    if (m_recordingQueue) {
        gst_element_set_state(m_recordingQueue, GST_STATE_NULL);
        gst_bin_remove(GST_BIN(m_pipeline), m_recordingQueue);
        m_recordingQueue = nullptr;
    }
    if (m_recordingEncoder) {
        gst_element_set_state(m_recordingEncoder, GST_STATE_NULL);
        gst_bin_remove(GST_BIN(m_pipeline), m_recordingEncoder);
        m_recordingEncoder = nullptr;
    }
    if (m_recordingMuxer) {
        gst_element_set_state(m_recordingMuxer, GST_STATE_NULL);
        gst_bin_remove(GST_BIN(m_pipeline), m_recordingMuxer);
        m_recordingMuxer = nullptr;
    }
    if (m_recordingSink) {
        gst_element_set_state(m_recordingSink, GST_STATE_NULL);
        gst_bin_remove(GST_BIN(m_pipeline), m_recordingSink);
        m_recordingSink = nullptr;
    }
    
    // Determine encoder and muxer based on format
    QString encoder, muxer;
    QString finalOutputPath = outputPath;
    
    if (format.toLower() == "mp4") {
        encoder = "x264enc";
        muxer = "mp4mux";
    } else if (format.toLower() == "avi") {
        encoder = "jpegenc";
        muxer = "avimux";
    } else if (format.toLower() == "mkv") {
        encoder = "x264enc";
        muxer = "matroskamux";
    } else {
        // Default to MJPEG for unsupported formats
        encoder = "jpegenc";
        QFileInfo outputInfo(outputPath);
        QString baseName = outputInfo.completeBaseName();
        QString dirPath = outputInfo.path();
        finalOutputPath = dirPath + "/" + baseName + ".mjpeg";
        muxer = "";  // No muxer for raw MJPEG
    }
    
    qCInfo(log_gstreamer_backend) << "Recording to:" << finalOutputPath << "with encoder:" << encoder << "muxer:" << muxer;
    
    // Create recording elements
    m_recordingQueue = gst_element_factory_make("queue", "recording-queue");
    m_recordingEncoder = gst_element_factory_make(encoder.toLatin1().data(), "recording-encoder");
    m_recordingSink = gst_element_factory_make("filesink", "recording-sink");
    
    if (!m_recordingQueue || !m_recordingEncoder || !m_recordingSink) {
        qCCritical(log_gstreamer_backend) << "Failed to create recording elements";
        return false;
    }
    
    // Set encoder properties
    if (encoder == "jpegenc") {
        g_object_set(m_recordingEncoder, "quality", 85, nullptr);
    } else if (encoder == "x264enc") {
        g_object_set(m_recordingEncoder, "bitrate", videoBitrate, nullptr);
        g_object_set(m_recordingEncoder, "speed-preset", 6, nullptr);  // Medium preset
    }
    
    // Set sink location
    g_object_set(m_recordingSink, "location", finalOutputPath.toLatin1().data(), nullptr);
    
    // Add elements to pipeline
    if (!muxer.isEmpty()) {
        m_recordingMuxer = gst_element_factory_make(muxer.toLatin1().data(), "recording-muxer");
        if (!m_recordingMuxer) {
            qCCritical(log_gstreamer_backend) << "Failed to create muxer:" << muxer;
            return false;
        }
        gst_bin_add_many(GST_BIN(m_pipeline), m_recordingQueue, m_recordingEncoder, m_recordingMuxer, m_recordingSink, nullptr);
    } else {
        gst_bin_add_many(GST_BIN(m_pipeline), m_recordingQueue, m_recordingEncoder, m_recordingSink, nullptr);
    }
    
    // Link tee to recording branch
    GstPad *teeSrcPad = gst_element_request_pad_simple(m_recordingTee, "src_%u");
    GstPad *queueSinkPad = gst_element_get_static_pad(m_recordingQueue, "sink");
    
    if (!teeSrcPad || !queueSinkPad) {
        qCCritical(log_gstreamer_backend) << "Failed to get pads for tee connection";
        return false;
    }
    
    if (gst_pad_link(teeSrcPad, queueSinkPad) != GST_PAD_LINK_OK) {
        qCCritical(log_gstreamer_backend) << "Failed to link tee to recording queue";
        gst_object_unref(teeSrcPad);
        gst_object_unref(queueSinkPad);
        return false;
    }
    
    gst_object_unref(teeSrcPad);
    gst_object_unref(queueSinkPad);
    
    // Link recording elements
    if (!muxer.isEmpty()) {
        if (!gst_element_link_many(m_recordingQueue, m_recordingEncoder, m_recordingMuxer, m_recordingSink, nullptr)) {
            qCCritical(log_gstreamer_backend) << "Failed to link recording elements with muxer";
            return false;
        }
    } else {
        if (!gst_element_link_many(m_recordingQueue, m_recordingEncoder, m_recordingSink, nullptr)) {
            qCCritical(log_gstreamer_backend) << "Failed to link recording elements without muxer";
            return false;
        }
    }
    
    // Sync elements with pipeline state
    if (!muxer.isEmpty()) {
        gst_element_sync_state_with_parent(m_recordingQueue);
        gst_element_sync_state_with_parent(m_recordingEncoder);
        gst_element_sync_state_with_parent(m_recordingMuxer);
        gst_element_sync_state_with_parent(m_recordingSink);
    } else {
        gst_element_sync_state_with_parent(m_recordingQueue);
        gst_element_sync_state_with_parent(m_recordingEncoder);
        gst_element_sync_state_with_parent(m_recordingSink);
    }
    
    qCInfo(log_gstreamer_backend) << "Recording branch added successfully to:" << finalOutputPath;
    return true;
}

void GStreamerBackendHandler::onRecordingProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    qCDebug(log_gstreamer_backend) << "Recording process finished with exit code:" << exitCode << "status:" << exitStatus;
    
    if (exitStatus == QProcess::CrashExit) {
        qCWarning(log_gstreamer_backend) << "Recording process crashed";
        emit recordingError("Recording process crashed");
    } else if (exitCode != 0) {
        qCWarning(log_gstreamer_backend) << "Recording process finished with error code:" << exitCode;
        emit recordingError(QString("Recording finished with error code: %1").arg(exitCode));
    } else {
        qCInfo(log_gstreamer_backend) << "Recording completed successfully";
    }
    
    // Clean up
    if (m_recordingProcess) {
        m_recordingProcess->deleteLater();
        m_recordingProcess = nullptr;
    }
    
    m_recordingActive = false;
    emit recordingStopped();
}

void GStreamerBackendHandler::onRecordingProcessError(QProcess::ProcessError error)
{
    QString errorString;
    switch (error) {
        case QProcess::FailedToStart:
            errorString = "Failed to start recording process";
            break;
        case QProcess::Crashed:
            errorString = "Recording process crashed";
            break;
        case QProcess::Timedout:
            errorString = "Recording process timed out";
            break;
        case QProcess::WriteError:
            errorString = "Write error in recording process";
            break;
        case QProcess::ReadError:
            errorString = "Read error in recording process";
            break;
        default:
            errorString = "Unknown recording process error";
            break;
    }
    
    qCCritical(log_gstreamer_backend) << "Recording process error:" << errorString;
    emit recordingError(errorString);
    
    m_recordingActive = false;
    if (m_recordingProcess) {
        m_recordingProcess->deleteLater();
        m_recordingProcess = nullptr;
    }
    
    // Stop frame capture timer
    if (m_frameCaptureTimer) {
        m_frameCaptureTimer->stop();
    }
}

void GStreamerBackendHandler::captureAndWriteFrame()
{
    // This function is no longer used - frames are captured via GStreamer appsink
    // in onNewRecordingSample callback
}

bool GStreamerBackendHandler::initializeDirectFilesinkRecording()
{
    qCDebug(log_gstreamer_backend) << "Initializing direct GStreamer filesink recording";
    
#ifdef HAVE_GSTREAMER
    if (!m_pipeline) {
        qCCritical(log_gstreamer_backend) << "No GStreamer pipeline available for recording";
        return false;
    }
    
    // Find the tee element in the main pipeline
    GstElement* tee = gst_bin_get_by_name(GST_BIN(m_pipeline), "t");
    if (!tee) {
        qCCritical(log_gstreamer_backend) << "Could not find tee element 't' for recording";
        return false;
    }
    
    qCDebug(log_gstreamer_backend) << "Found tee element, creating direct recording branch...";
    
    // Create recording branch elements using available elements
    GstElement* queue = gst_element_factory_make("queue", "recording-queue");
    if (!queue) {
        qCCritical(log_gstreamer_backend) << "Failed to create 'queue' element";
        gst_object_unref(tee);
        return false;
    }
    
    // CRITICAL FIX: Add identity element for proper segment and timestamp handling
    GstElement* identity = gst_element_factory_make("identity", "recording-identity");
    if (!identity) {
        qCWarning(log_gstreamer_backend) << "Failed to create 'identity' element - recording may have timing issues";
    } else {
        // Configure identity for proper segment handling
        g_object_set(identity,
                     "sync", TRUE,           // Enable synchronization
                     "single-segment", TRUE, // Use single segment for recording
                     NULL);
        qCDebug(log_gstreamer_backend) << "Identity element configured for proper segment handling";
    }
    
    // Try to find an appropriate encoder and muxer
    GstElement* encoder = nullptr;
    GstElement* muxer = nullptr;
    GstElement* videoconvert = nullptr;
    GstElement* filesink = gst_element_factory_make("filesink", "recording-filesink");
    
    if (!filesink) {
        qCCritical(log_gstreamer_backend) << "Failed to create 'filesink' element";
        gst_object_unref(queue);
        gst_object_unref(tee);
        return false;
    }
    
    // Set the output file path
    g_object_set(filesink, "location", m_recordingConfig.outputPath.toUtf8().data(), NULL);
    
    // Create encoding pipeline based on format
    if (m_recordingConfig.format.toLower() == "avi") {
        // For AVI, try MJPEG encoding
        encoder = gst_element_factory_make("jpegenc", "recording-encoder");
        if (!encoder) {
            qCDebug(log_gstreamer_backend) << "jpegenc not available, trying x264enc";
            encoder = gst_element_factory_make("x264enc", "recording-encoder");
        }
        
        muxer = gst_element_factory_make("avimux", "recording-muxer");
        if (!muxer) {
            qCDebug(log_gstreamer_backend) << "avimux not available, trying matroskamux";
            muxer = gst_element_factory_make("matroskamux", "recording-muxer");
        }
    } else {
        // Default to raw format if encoders not available
        qCDebug(log_gstreamer_backend) << "Using raw video format (no encoder/muxer)";
    }
    
    // Add conversion elements for better compatibility - only if available
    videoconvert = gst_element_factory_make("videoconvert", "recording-convert");
    if (!videoconvert) {
        qCDebug(log_gstreamer_backend) << "videoconvert plugin not available, trying videoconvertscale";
        videoconvert = gst_element_factory_make("videoconvertscale", "recording-convert");
    }
    
    if (!videoconvert) {
        qCWarning(log_gstreamer_backend) << "No video conversion elements available - recording without conversion";
    }
    
    // Add basic elements to pipeline based on what's available
    if (encoder && muxer && videoconvert) {
        if (identity) {
            gst_bin_add_many(GST_BIN(m_pipeline), identity, queue, videoconvert, encoder, muxer, filesink, NULL);
        } else {
            gst_bin_add_many(GST_BIN(m_pipeline), queue, videoconvert, encoder, muxer, filesink, NULL);
        }
        qCDebug(log_gstreamer_backend) << "Added encoded recording branch: [identity ->] queue -> videoconvert -> encoder -> muxer -> filesink";
    } else if (encoder && muxer) {
        if (identity) {
            gst_bin_add_many(GST_BIN(m_pipeline), identity, queue, encoder, muxer, filesink, NULL);
        } else {
            gst_bin_add_many(GST_BIN(m_pipeline), queue, encoder, muxer, filesink, NULL);
        }
        qCDebug(log_gstreamer_backend) << "Added encoded recording branch: [identity ->] queue -> encoder -> muxer -> filesink";
    } else if (encoder && videoconvert) {
        if (identity) {
            gst_bin_add_many(GST_BIN(m_pipeline), identity, queue, videoconvert, encoder, filesink, NULL);
        } else {
            gst_bin_add_many(GST_BIN(m_pipeline), queue, videoconvert, encoder, filesink, NULL);
        }
        qCDebug(log_gstreamer_backend) << "Added simple encoded recording branch: [identity ->] queue -> videoconvert -> encoder -> filesink";
    } else if (encoder) {
        if (identity) {
            gst_bin_add_many(GST_BIN(m_pipeline), identity, queue, encoder, filesink, NULL);
        } else {
            gst_bin_add_many(GST_BIN(m_pipeline), queue, encoder, filesink, NULL);
        }
        qCDebug(log_gstreamer_backend) << "Added simple encoded recording branch: [identity ->] queue -> encoder -> filesink";
    } else if (videoconvert) {
        if (identity) {
            gst_bin_add_many(GST_BIN(m_pipeline), identity, queue, videoconvert, filesink, NULL);
        } else {
            gst_bin_add_many(GST_BIN(m_pipeline), queue, videoconvert, filesink, NULL);
        }
        qCDebug(log_gstreamer_backend) << "Added raw recording branch: [identity ->] queue -> videoconvert -> filesink";
    } else {
        if (identity) {
            gst_bin_add_many(GST_BIN(m_pipeline), identity, queue, filesink, NULL);
        } else {
            gst_bin_add_many(GST_BIN(m_pipeline), queue, filesink, NULL);
        }
        qCDebug(log_gstreamer_backend) << "Added basic raw recording branch: [identity ->] queue -> filesink";
    }
    
    // Link tee to recording branch (through identity if available)
    GstPad* teeSrcPad = gst_element_request_pad_simple(tee, "src_%u");
    GstPad* firstElementSinkPad;
    
    if (identity) {
        firstElementSinkPad = gst_element_get_static_pad(identity, "sink");
    } else {
        firstElementSinkPad = gst_element_get_static_pad(queue, "sink");
    }
    
    if (!teeSrcPad || !firstElementSinkPad) {
        qCCritical(log_gstreamer_backend) << "Failed to get pads for recording branch";
        gst_object_unref(tee);
        return false;
    }
    
    if (gst_pad_link(teeSrcPad, firstElementSinkPad) != GST_PAD_LINK_OK) {
        qCCritical(log_gstreamer_backend) << "Failed to link tee to recording branch";
        gst_object_unref(teeSrcPad);
        gst_object_unref(firstElementSinkPad);
        gst_object_unref(tee);
        return false;
    }
    
    gst_object_unref(teeSrcPad);
    gst_object_unref(firstElementSinkPad);
    gst_object_unref(tee);
    
    // Link recording elements based on what's available
    if (encoder && muxer && videoconvert) {
        if (identity) {
            if (!gst_element_link_many(identity, queue, videoconvert, encoder, muxer, filesink, NULL)) {
                qCCritical(log_gstreamer_backend) << "Failed to link encoded recording pipeline with identity and videoconvert";
                return false;
            }
        } else {
            if (!gst_element_link_many(queue, videoconvert, encoder, muxer, filesink, NULL)) {
                qCCritical(log_gstreamer_backend) << "Failed to link encoded recording pipeline with videoconvert";
                return false;
            }
        }
    } else if (encoder && muxer) {
        if (identity) {
            if (!gst_element_link_many(identity, queue, encoder, muxer, filesink, NULL)) {
                qCCritical(log_gstreamer_backend) << "Failed to link encoded recording pipeline with identity";
                return false;
            }
        } else {
            if (!gst_element_link_many(queue, encoder, muxer, filesink, NULL)) {
                qCCritical(log_gstreamer_backend) << "Failed to link encoded recording pipeline without videoconvert";
                return false;
            }
        }
    } else if (encoder && videoconvert) {
        if (identity) {
            if (!gst_element_link_many(identity, queue, videoconvert, encoder, filesink, NULL)) {
                qCCritical(log_gstreamer_backend) << "Failed to link simple encoded recording pipeline with identity and videoconvert";
                return false;
            }
        } else {
            if (!gst_element_link_many(queue, videoconvert, encoder, filesink, NULL)) {
                qCCritical(log_gstreamer_backend) << "Failed to link simple encoded recording pipeline with videoconvert";
                return false;
            }
        }
    } else if (encoder) {
        if (identity) {
            if (!gst_element_link_many(identity, queue, encoder, filesink, NULL)) {
                qCCritical(log_gstreamer_backend) << "Failed to link simple encoded recording pipeline with identity";
                return false;
            }
        } else {
            if (!gst_element_link_many(queue, encoder, filesink, NULL)) {
                qCCritical(log_gstreamer_backend) << "Failed to link simple encoded recording pipeline without videoconvert";
                return false;
            }
        }
    } else if (videoconvert) {
        if (identity) {
            if (!gst_element_link_many(identity, queue, videoconvert, filesink, NULL)) {
                qCCritical(log_gstreamer_backend) << "Failed to link raw recording pipeline with identity and videoconvert";
                return false;
            }
        } else {
            if (!gst_element_link_many(queue, videoconvert, filesink, NULL)) {
                qCCritical(log_gstreamer_backend) << "Failed to link raw recording pipeline with videoconvert";
                return false;
            }
        }
    } else {
        if (identity) {
            if (!gst_element_link_many(identity, queue, filesink, NULL)) {
                qCCritical(log_gstreamer_backend) << "Failed to link basic raw recording pipeline with identity";
                return false;
            }
        } else {
            if (!gst_element_link(queue, filesink)) {
                qCCritical(log_gstreamer_backend) << "Failed to link basic raw recording pipeline";
                return false;
            }
        }
    }
    
    // Store elements for cleanup
    m_recordingQueue = queue;
    m_recordingVideoConvert = videoconvert;
    m_recordingEncoder = encoder;
    m_recordingMuxer = muxer;
    m_recordingFileSink = filesink;
    
    // Store identity element for cleanup (using a class member that needs to be added)
    // For now, we'll clean it up locally in the stop function
    
    // Sync elements with pipeline state
    if (identity) gst_element_sync_state_with_parent(identity);
    gst_element_sync_state_with_parent(queue);
    if (videoconvert) gst_element_sync_state_with_parent(videoconvert);
    if (encoder) gst_element_sync_state_with_parent(encoder);
    if (muxer) gst_element_sync_state_with_parent(muxer);
    gst_element_sync_state_with_parent(filesink);
    
    // CRITICAL: Force pipeline to PLAYING state to ensure frames flow through tee
    GstState currentState;
    GstState pendingState;
    GstStateChangeReturn stateRet = gst_element_get_state(m_pipeline, &currentState, &pendingState, 0);
    
    qCDebug(log_gstreamer_backend) << "Pipeline state before recording:" << 
        "current=" << gst_element_state_get_name(currentState) << 
        "pending=" << gst_element_state_get_name(pendingState) << 
        "state_ret=" << stateRet;
    
    if (currentState != GST_STATE_PLAYING) {
        qCDebug(log_gstreamer_backend) << "Setting main pipeline to PLAYING state for recording";
        GstStateChangeReturn playRet = gst_element_set_state(m_pipeline, GST_STATE_PLAYING);
        if (playRet == GST_STATE_CHANGE_FAILURE) {
            qCCritical(log_gstreamer_backend) << "Failed to set pipeline to PLAYING state";
            return false;
        }
        
        // Wait for state change to complete
        playRet = gst_element_get_state(m_pipeline, nullptr, nullptr, GST_CLOCK_TIME_NONE);
        if (playRet == GST_STATE_CHANGE_FAILURE) {
            qCCritical(log_gstreamer_backend) << "Pipeline failed to reach PLAYING state";
            return false;
        }
        qCDebug(log_gstreamer_backend) << "Pipeline successfully set to PLAYING state";
    }
    
    // Wait a moment for frames to start flowing
    QThread::msleep(100);
    
    qCDebug(log_gstreamer_backend) << "Direct GStreamer filesink recording initialized successfully";
    return true;
    
#else
    qCCritical(log_gstreamer_backend) << "GStreamer support not compiled in";
    return false;
#endif
}

#ifdef HAVE_GSTREAMER
GstFlowReturn GStreamerBackendHandler::onNewRecordingSample(GstAppSink* sink)
{
    if (!m_recordingActive || !m_recordingProcess || m_recordingProcess->state() != QProcess::Running) {
        return GST_FLOW_OK;
    }
    
    // Pull sample from appsink
    GstSample* sample = gst_app_sink_pull_sample(sink);
    if (!sample) {
        return GST_FLOW_OK;
    }
    
    GstBuffer* buffer = gst_sample_get_buffer(sample);
    if (!buffer) {
        gst_sample_unref(sample);
        return GST_FLOW_OK;
    }
    
    // Map buffer for reading
    GstMapInfo map;
    if (!gst_buffer_map(buffer, &map, GST_MAP_READ)) {
        gst_sample_unref(sample);
        return GST_FLOW_OK;
    }
    
    // Write raw RGB data to FFmpeg stdin
    qint64 bytesWritten = m_recordingProcess->write(reinterpret_cast<const char*>(map.data), map.size);
    
    // Unmap and cleanup
    gst_buffer_unmap(buffer, &map);
    gst_sample_unref(sample);
    
    if (bytesWritten != static_cast<qint64>(map.size)) {
        qCWarning(log_gstreamer_backend) << "Frame write incomplete:" << bytesWritten << "of" << map.size << "bytes written";
    } else {
        m_recordingFrameNumber++;
        
        // Debug logging for first few frames
        static int debugFrameCount = 0;
        if (++debugFrameCount <= 5 || debugFrameCount % 100 == 0) {
            qCDebug(log_gstreamer_backend) << "Captured and wrote frame" << m_recordingFrameNumber 
                                           << "size:" << map.size << "bytes";
        }
    }
    
    return GST_FLOW_OK;
}
#endif

// Advanced recording methods
bool GStreamerBackendHandler::isPipelineReady() const
{
#ifdef HAVE_GSTREAMER
    return m_pipelineRunning && m_pipeline != nullptr;
#else
    return false;
#endif
}

bool GStreamerBackendHandler::supportsAdvancedRecording() const
{
    return true; // GStreamer backend supports advanced recording
}

bool GStreamerBackendHandler::startRecordingAdvanced(const QString& outputPath, const RecordingConfig& config)
{
    setRecordingConfig(config);
    return startRecording(outputPath, config.format, config.videoBitrate);
}

bool GStreamerBackendHandler::forceStopRecording()
{
#ifdef HAVE_GSTREAMER
    qCDebug(log_gstreamer_backend) << "Force stopping recording";
    m_recordingActive = false;
    m_recordingPaused = false;
    m_recordingOutputPath.clear();
    
    // Clean up recording elements if they exist
    if (m_recordingValve) {
        g_object_set(m_recordingValve, "drop", TRUE, NULL);
    }
    
    emit recordingStopped();
    return true;
#else
    return false;
#endif
}

QString GStreamerBackendHandler::getLastError() const
{
    return m_lastError;
}

bool GStreamerBackendHandler::isPaused() const
{
    return m_recordingPaused;
}

bool GStreamerBackendHandler::supportsRecordingStats() const
{
    return true; // GStreamer backend supports recording statistics
}

qint64 GStreamerBackendHandler::getRecordingFileSize() const
{
#ifdef HAVE_GSTREAMER
    if (m_recordingOutputPath.isEmpty() || !m_recordingActive) {
        return 0;
    }
    
    QFileInfo fileInfo(m_recordingOutputPath);
    return fileInfo.size();
#else
    return 0;
#endif
}

#endif // HAVE_GSTREAMER