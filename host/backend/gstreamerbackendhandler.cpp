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

#include "gstreamer/sinkselector.h"
#include "gstreamer/pipelinebuilder.h"
#include "gstreamer/queueconfigurator.h"
#include "gstreamer/videooverlaymanager.h"
#include "gstreamer/pipelinefactory.h"
#include "gstreamer/gstreamerhelpers.h"

// Runner and recording helpers
#include "gstreamer/inprocessgstrunner.h"
#include "gstreamer/externalgstrunner.h"
#include "gstreamer/recordingmanager.h"

// logging category for this translation unit
Q_LOGGING_CATEGORY(log_gstreamer_backend, "opf.backend.gstreamer")

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
    if (device.isEmpty()) {
        qCWarning(log_gstreamer_backend) << "Device path is empty, cannot generate pipeline";
        return QString();
    }

    if (resolution.width() <= 0 || resolution.height() <= 0) {
        qCWarning(log_gstreamer_backend) << "Invalid resolution:" << resolution << "- using fallback 1280x720";
        QSize fallbackResolution(1280, 720);
        return generatePipelineString(device, fallbackResolution, framerate, videoSink);
    }

    if (framerate <= 0) {
        qCWarning(log_gstreamer_backend) << "Invalid framerate:" << framerate << "- using fallback 30fps";
        return generatePipelineString(device, resolution, 30, videoSink);
    }

    // Delegate to PipelineBuilder which centralizes pipeline templates and tuning
    return Openterface::GStreamer::PipelineBuilder::buildFlexiblePipeline(device, resolution, framerate, videoSink);
}

void GStreamerBackendHandler::startCamera()
{
    qCDebug(log_gstreamer_backend) << "GStreamer startCamera called";
    qCDebug(log_gstreamer_backend) << "Current device:" << m_currentDevice;
    qCDebug(log_gstreamer_backend) << "Current resolution:" << m_currentResolution;
    qCDebug(log_gstreamer_backend) << "Current framerate:" << m_currentFramerate;

    // Prefer direct pipeline when we have a configured device
    if (!m_currentDevice.isEmpty()) {
        if (startDirectPipeline()) {
            qCDebug(log_gstreamer_backend) << "Direct GStreamer pipeline started";
            return;
        }
        qCWarning(log_gstreamer_backend) << "Direct GStreamer pipeline failed, attempting fallback";
    }

    // Fallback to starting via external runner
    if (!startGStreamerPipeline()) {
        qCWarning(log_gstreamer_backend) << "Failed to start any GStreamer pipeline";
    }
}

bool GStreamerBackendHandler::startDirectPipeline()
{
    qCDebug(log_gstreamer_backend) << "GStreamer: attempting direct pipeline for device" << m_currentDevice;

    if (m_currentDevice.isEmpty()) {
        qCWarning(log_gstreamer_backend) << "No device configured for direct pipeline";
        return false;
    }

    if (!createGStreamerPipeline(m_currentDevice, m_currentResolution, m_currentFramerate)) {
        qCWarning(log_gstreamer_backend) << "createGStreamerPipeline failed";
        return false;
    }

    return startGStreamerPipeline();
}

bool GStreamerBackendHandler::startGStreamerPipeline()
{
    qCDebug(log_gstreamer_backend) << "Starting GStreamer pipeline";
#ifdef HAVE_GSTREAMER
    if (!m_pipeline) {
        qCWarning(log_gstreamer_backend) << "No in-process pipeline available";
        return false;
    }

    // Prefer In-process runner if available
    if (m_inProcessRunner) {
        QString err;
        bool ok = m_inProcessRunner->start(m_pipeline, 5000, &err);
        if (!ok) {
            qCCritical(log_gstreamer_backend) << "Failed to start pipeline in-process:" << err;
            Openterface::GStreamer::GstHelpers::parseAndLogGstErrorMessage(m_bus, "START_PIPELINE");
            return false;
        }
        m_pipelineRunning = true;
        return true;
    }

    // Fallback to direct gst_element_set_state
    GstStateChangeReturn ret = gst_element_set_state(m_pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        qCCritical(log_gstreamer_backend) << "gst_element_set_state PLAYING failed";
        Openterface::GStreamer::GstHelpers::parseAndLogGstErrorMessage(m_bus, "START_PIPELINE");
        return false;
    }

    m_pipelineRunning = true;
    return true;
#else
    // Fallback: external runner (gst-launch) if available
    QString program = "gst-launch-1.0";
    QString pipelineStr = generatePipelineString(m_currentDevice, m_currentResolution, m_currentFramerate, "ximagesink");
    if (pipelineStr.isEmpty()) {
        qCWarning(log_gstreamer_backend) << "Empty pipeline string - cannot start external runner";
        return false;
    }

    bool started = false;
    if (m_externalRunner) {
        if (m_gstProcess) started = m_externalRunner->start(m_gstProcess, pipelineStr, program);
        else started = m_externalRunner->start(pipelineStr, program);
    } else {
        qCWarning(log_gstreamer_backend) << "No external runner available";
    }

    if (!started) return false;

    m_pipelineRunning = true;
    return true;
#endif
}

// startExternalGstProcess removed: ExternalGstRunner is used instead

void GStreamerBackendHandler::stopGStreamerPipeline()
{
    qCDebug(log_gstreamer_backend) << "Stopping GStreamer pipeline";
    
    m_healthCheckTimer->stop();
    m_pipelineRunning = false;
    
#ifdef HAVE_GSTREAMER
    if (m_pipeline) {
        if (m_inProcessRunner) {
            m_inProcessRunner->stop(m_pipeline);
        } else {
            gst_element_set_state(m_pipeline, GST_STATE_NULL);
        }
        qCDebug(log_gstreamer_backend) << "GStreamer pipeline stopped";
    }
#else
    // If handler uses its own m_gstProcess keep the behaviour; otherwise use external runner
    if (m_gstProcess && m_gstProcess->state() == QProcess::Running) {
        m_gstProcess->terminate();
        if (!m_gstProcess->waitForFinished(3000)) {
            m_gstProcess->kill();
            qCDebug(log_gstreamer_backend) << "GStreamer process forcefully killed";
        } else {
            qCDebug(log_gstreamer_backend) << "GStreamer process terminated gracefully";
        }
    } else if (m_externalRunner && m_externalRunner->isRunning()) {
        m_externalRunner->stop();
    }
#endif
}

void GStreamerBackendHandler::onExternalRunnerStarted()
{
    qCDebug(log_gstreamer_backend) << "External GStreamer process started";
    m_pipelineRunning = true;
    m_healthCheckTimer->start(1000);
}

void GStreamerBackendHandler::onExternalRunnerFailed(const QString& error)
{
    qCCritical(log_gstreamer_backend) << "External GStreamer runner failed:" << error;
    emit backendWarning(QString("External GStreamer failed: %1").arg(error));
    m_pipelineRunning = false;
}

void GStreamerBackendHandler::onExternalRunnerFinished(int exitCode, QProcess::ExitStatus status)
{
    Q_UNUSED(status)
    qCWarning(log_gstreamer_backend) << "External GStreamer process finished with code:" << exitCode;
    m_pipelineRunning = false;
    emit backendWarning("External GStreamer process stopped unexpectedly");
}

void GStreamerBackendHandler::setVideoOutput(QWidget* widget)
{
    m_videoWidget = widget;
    m_graphicsVideoItem = nullptr;  // Clear graphics video item if widget is set

    if (!widget) return;

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
        widget->show();
        qCDebug(log_gstreamer_backend) << "Made video widget visible for overlay setup";
    }

    // Force native window creation if needed
    if (widget->winId() == 0) {
        widget->createWinId();
        qCDebug(log_gstreamer_backend) << "Forced native window creation for video widget";
    }

    // If pipeline exists, attempt overlay setup now
    if (m_pipeline) {
        setupVideoOverlayForCurrentPipeline();
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
            
            // Check for error messages on the bus (centralized helper)
            Openterface::GStreamer::GstHelpers::parseAndLogGstErrorMessage(m_bus, "HEALTH_CHECK");
            
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
    if (!m_recordingManager) {
        qCWarning(log_gstreamer_backend) << "No RecordingManager available";
        emit recordingError("Recording subsystem not available");
        return false;
    }

    return m_recordingManager->startRecording(m_pipeline, outputPath, format, videoBitrate);
}

bool GStreamerBackendHandler::stopRecording()
{
    if (!m_recordingManager) {
        qCWarning(log_gstreamer_backend) << "No RecordingManager available";
        return false;
    }

    return m_recordingManager->stopRecording();
}

void GStreamerBackendHandler::pauseRecording()
{
    if (!m_recordingManager) {
        qCWarning(log_gstreamer_backend) << "No RecordingManager available";
        return;
    }

    m_recordingManager->pauseRecording();
}

void GStreamerBackendHandler::resumeRecording()
{
    if (!m_recordingManager) {
        qCWarning(log_gstreamer_backend) << "No RecordingManager available";
        return;
    }

    m_recordingManager->resumeRecording();
}

bool GStreamerBackendHandler::isRecording() const
{
    if (m_recordingManager) return m_recordingManager->isRecording();
    return false;
}

QString GStreamerBackendHandler::getCurrentRecordingPath() const
{
    if (m_recordingManager) return m_recordingManager->getCurrentRecordingPath();
    return QString();
}

qint64 GStreamerBackendHandler::getRecordingDuration() const
{
    if (m_recordingManager) return m_recordingManager->getRecordingDuration();
    return 0;
}

void GStreamerBackendHandler::setRecordingConfig(const RecordingConfig& config)
{
    // Pass minimal fields to the recording manager; RecordingConfig is maintained here for API compatibility
    if (m_recordingManager) {
        m_recordingManager->setRecordingConfig(config.videoCodec, config.format, config.videoBitrate);
    }
}

GStreamerBackendHandler::RecordingConfig GStreamerBackendHandler::getRecordingConfig() const
{
    // Return handler-local config if present — for now just return a default to preserve API
    RecordingConfig cfg;
    // We don't store a full copy in manager; return defaults. Future: sync config with manager.
    return cfg;
}

    // createRecordingBranch implementation moved into RecordingManager; no-op placeholder remains

void GStreamerBackendHandler::removeRecordingBranch()
{
    if (!m_recordingManager) {
        qCWarning(log_gstreamer_backend) << "No RecordingManager available";
        return;
    }

    m_recordingManager->stopRecording();
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

// Recording implementation moved into RecordingManager; handler delegates to manager methods

// The detailed recording implementations were moved into RecordingManager.
// GStreamerBackendHandler now delegates recording management to m_recordingManager.

bool GStreamerBackendHandler::createSeparateRecordingPipeline(const QString& outputPath, const QString& format, int videoBitrate)
{
    qCDebug(log_gstreamer_backend) << "Delegating createSeparateRecordingPipeline to RecordingManager";
    if (!m_recordingManager) {
        qCWarning(log_gstreamer_backend) << "No RecordingManager available";
        return false;
    }

    return m_recordingManager->createSeparateRecordingPipeline(outputPath, format, videoBitrate);
    
}

// Recording process lifecycle is handled by RecordingManager now

bool GStreamerBackendHandler::initializeDirectFilesinkRecording()
{
    if (!m_recordingManager) {
        qCWarning(log_gstreamer_backend) << "No RecordingManager available for direct filesink recording";
        return false;
    }
    return m_recordingManager->initializeDirectFilesinkRecording(m_recordingConfig.outputPath, m_recordingConfig.format);
}

// Frame sample handling is now managed by RecordingManager (appsink callback moved there)

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

// end of file