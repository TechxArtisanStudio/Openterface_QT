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
    // Increment frame count logic remains unchanged
#include "../../ui/globalsetting.h"
#include "../../device/DeviceManager.h"
#include "../../device/HotplugMonitor.h"
#include "../../device/DeviceInfo.h"
#include <QThread>
#include <QApplication>
#include <QDebug>
#include <QWidget>
#include <QEvent>
#include <QResizeEvent>
#include <QGraphicsView>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QGraphicsVideoItem>
#ifdef Q_OS_LINUX
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#endif

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

// Small helper that maps common QEvent types to readable names used in debug logging
static const char* qEventTypeName(QEvent::Type t)
{
    switch (t) {
    case QEvent::Show: return "Show";
    case QEvent::Hide: return "Hide";
    case QEvent::WinIdChange: return "WinIdChange";
    case QEvent::ShowToParent: return "ShowToParent";
    case QEvent::Resize: return "Resize";
    case QEvent::Destroy: return "Destroy";
    default: return "Other";
    }
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

#ifdef HAVE_GSTREAMER
#include <gst/video/videooverlay.h>
#include <gst/gstpad.h>
#endif

#ifdef HAVE_GSTREAMER
// Pad probe used to count frames for realtime FPS logging
GstPadProbeReturn GStreamerBackendHandler::gstreamer_frame_probe_cb(GstPad* pad, GstPadProbeInfo* info, gpointer user_data)
{
    Q_UNUSED(pad)
    if (!user_data) return GST_PAD_PROBE_OK;
    if (info->type & GST_PAD_PROBE_TYPE_BUFFER) {
        GStreamerBackendHandler* self = static_cast<GStreamerBackendHandler*>(user_data);
        if (self) {
            self->incrementFrameCount();
        }
    }
    return GST_PAD_PROBE_OK;
}
#endif

GStreamerBackendHandler::GStreamerBackendHandler(QObject *parent)
    : MultimediaBackendHandler(parent),
      m_pipeline(nullptr), m_source(nullptr), m_sink(nullptr), m_bus(nullptr),
      m_hotplugMonitor(nullptr),
      m_recordingPipeline(nullptr), m_recordingTee(nullptr), m_recordingValve(nullptr),
      m_recordingSink(nullptr), m_recordingQueue(nullptr), m_recordingEncoder(nullptr),
      m_recordingVideoConvert(nullptr), m_recordingMuxer(nullptr), m_recordingFileSink(nullptr),
      m_recordingAppSink(nullptr), m_recordingTeeSrcPad(nullptr),
      m_recordingManager(nullptr),
      m_videoWidget(nullptr), m_graphicsVideoItem(nullptr), m_videoPane(nullptr),
    m_healthCheckTimer(nullptr), m_gstProcess(nullptr), m_pipelineRunning(false), m_selectedSink(),
      m_overlaySetupPending(false), m_recordingActive(false), m_recordingPaused(false),
      m_recordingStartTime(0), m_recordingPausedTime(0), m_totalPausedDuration(0), m_recordingFrameNumber(0),
      m_inProcessRunner(nullptr), m_externalRunner(nullptr)
{
    qCDebug(log_gstreamer_backend) << "GStreamerBackendHandler initializing";

    // Load default configuration
    m_config = getDefaultConfig();

    // create health check timer
    m_healthCheckTimer = new QTimer(this);
    m_healthCheckTimer->setInterval(1000);
    connect(m_healthCheckTimer, &QTimer::timeout, this, &GStreamerBackendHandler::checkPipelineHealth);

    // runners
    m_inProcessRunner = new InProcessGstRunner(this);
    m_externalRunner = new ExternalGstRunner(this);
    connect(m_externalRunner, &ExternalGstRunner::started, this, &GStreamerBackendHandler::onExternalRunnerStarted);
    connect(m_externalRunner, &ExternalGstRunner::failed, this, &GStreamerBackendHandler::onExternalRunnerFailed);
    connect(m_externalRunner, &ExternalGstRunner::finished, this, &GStreamerBackendHandler::onExternalRunnerFinished);

    // recording manager
    m_recordingManager = new RecordingManager(this);
    connect(m_recordingManager, &RecordingManager::recordingStarted, this, &GStreamerBackendHandler::recordingStarted);
    connect(m_recordingManager, &RecordingManager::recordingStopped, this, &GStreamerBackendHandler::recordingStopped);
    connect(m_recordingManager, &RecordingManager::recordingError, this, &GStreamerBackendHandler::recordingError);
    
    // Connect to hotplug monitor to handle device unplugging
    connectToHotplugMonitor();
}

// incrementFrameCount is implemented under HAVE_GSTREAMER guard later in the file

GStreamerBackendHandler::~GStreamerBackendHandler()
{
    qCDebug(log_gstreamer_backend) << "GStreamerBackendHandler destructor";

    // CRITICAL: Set destruction flag FIRST to signal event filter to exit early
    // This prevents access to potentially destroyed members (m_videoPane, m_videoWidget, etc.)
    m_isDestructing = true;

    // SECOND: Block all signals to prevent any signal/slot activity during destruction
    blockSignals(true);

    // NOTE: Do NOT try to remove event filters - Qt's destruction sequence will handle this.
    // If we try to call removeEventFilter() on objects being destroyed, it will crash.
    // The event filter will check m_isDestructing and return early for any events during shutdown.
    // Simply clear our tracking set.
    m_watchedObjects.clear();

    // Stop camera / pipelines cleanly
    stopCamera();

    // Clean up any GStreamer objects
    cleanupGStreamer();

    if (m_healthCheckTimer) {
        m_healthCheckTimer->stop();
        delete m_healthCheckTimer;
        m_healthCheckTimer = nullptr;
    }

    if (m_externalRunner) {
        m_externalRunner->stop();
        delete m_externalRunner;
        m_externalRunner = nullptr;
    }

    if (m_inProcessRunner) {
        delete m_inProcessRunner;
        m_inProcessRunner = nullptr;
    }

    if (m_recordingManager) {
        delete m_recordingManager;
        m_recordingManager = nullptr;
    }
}

MultimediaBackendType GStreamerBackendHandler::getBackendType() const
{
    return MultimediaBackendType::GStreamer;
}

QString GStreamerBackendHandler::getBackendName() const
{
    return QStringLiteral("GStreamer");
}

MultimediaBackendConfig GStreamerBackendHandler::getDefaultConfig() const
{
    MultimediaBackendConfig cfg;
    cfg.cameraInitDelay = 200;
    cfg.deviceSwitchDelay = 300;
    cfg.videoOutputSetupDelay = 200;
    cfg.captureSessionDelay = 50;
    cfg.useConservativeFrameRates = true;
    cfg.useStandardFrameRatesOnly = true;
    return cfg;
}

void GStreamerBackendHandler::prepareCameraCreation()
{
    qCDebug(log_gstreamer_backend) << "GStreamer: prepareCameraCreation";
    // Placeholder for any GStreamer-specific camera init steps
}

void GStreamerBackendHandler::configureCameraDevice()
{
    qCDebug(log_gstreamer_backend) << "GStreamer: configureCameraDevice";
    // Configure device parameters if needed
}

void GStreamerBackendHandler::setupCaptureSession(QMediaCaptureSession* session)
{
    qCDebug(log_gstreamer_backend) << "GStreamer: setupCaptureSession";
    Q_UNUSED(session);
}

void GStreamerBackendHandler::prepareVideoOutputConnection(QMediaCaptureSession* session, QObject* videoOutput)
{
    qCDebug(log_gstreamer_backend) << "GStreamer: prepareVideoOutputConnection";
    Q_UNUSED(session);
    Q_UNUSED(videoOutput);
}

void GStreamerBackendHandler::finalizeVideoOutputConnection(QMediaCaptureSession* session, QObject* videoOutput)
{
    qCDebug(log_gstreamer_backend) << "GStreamer: finalizeVideoOutputConnection";
    Q_UNUSED(session);

    // Accept different video output types
    if (!videoOutput) return;

    if (auto widget = qobject_cast<QWidget*>(videoOutput)) {
        setVideoOutput(widget);
        return;
    }

    if (auto graphicsItem = qobject_cast<QGraphicsVideoItem*>(videoOutput)) {
        setVideoOutput(graphicsItem);
        return;
    }

    if (auto vp = qobject_cast<VideoPane*>(videoOutput)) {
        setVideoOutput(vp);
        return;
    }
}

void GStreamerBackendHandler::stopCamera()
{
    qCDebug(log_gstreamer_backend) << "GStreamer: stopCamera called";

#ifdef HAVE_GSTREAMER
    if (m_pipeline) {
        stopGStreamerPipeline();
        gst_element_set_state(m_pipeline, GST_STATE_NULL);
    }
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
    
    m_currentDevicePath = device;
    m_currentResolution = resolution;
    m_currentFramerate = framerate;
    
    // Determine the appropriate video sink for current environment
    const QString platform = QGuiApplication::platformName();
    const bool isXcb = platform.contains("xcb", Qt::CaseInsensitive);
    const bool isWayland = platform.contains("wayland", Qt::CaseInsensitive);
    const bool hasXDisplay = !qgetenv("DISPLAY").isEmpty();
    const bool hasWaylandDisplay = !qgetenv("WAYLAND_DISPLAY").isEmpty();
    
    // Get candidate sinks (env override first then preferred list). We'll try each sink until pipeline creation succeeds.
    QStringList candidateSinks = Openterface::GStreamer::SinkSelector::candidateSinks(platform);
    qCDebug(log_gstreamer_backend) << "Candidate sinks to try:" << candidateSinks << "(platform:" << platform
                                   << ", X DISPLAY:" << hasXDisplay << ", WAYLAND_DISPLAY:" << hasWaylandDisplay << ")";

    // Centralize pipeline creation and fallbacks in PipelineFactory (HAVE_GSTREAMER)
#ifdef HAVE_GSTREAMER
    QString err;
    for (const QString &trySink : candidateSinks) {
        qCDebug(log_gstreamer_backend) << "Trying to create pipeline with sink:" << trySink;
        GstElement* pipeline = Openterface::GStreamer::PipelineFactory::createPipeline(device, resolution, framerate, trySink, err);
        if (!pipeline) {
            qCWarning(log_gstreamer_backend) << "Pipeline creation failed for sink" << trySink << ":" << err;
            continue; // try the next sink
        }
        qCDebug(log_gstreamer_backend) << "PipelineFactory created pipeline successfully with sink:" << trySink;

        // Basic sanity check: ensure pipeline can reach NULL state (some sinks/elements may fail early)
        GstStateChangeReturn sanityRet = gst_element_set_state(pipeline, GST_STATE_NULL);
        if (sanityRet == GST_STATE_CHANGE_FAILURE) {
            qCWarning(log_gstreamer_backend) << "Sanity check (set NULL) failed for sink" << trySink << "- trying next candidate";
            gst_object_unref(pipeline);
            err = QStringLiteral("Pipeline failed basic state change (NULL)");
            continue; // try next sink
        }

        // Assign pipeline and selected sink
        m_pipeline = pipeline;
        m_selectedSink = trySink;
        err.clear();
        break; // stop trying further sinks
    }

    if (!m_pipeline) {
        qCCritical(log_gstreamer_backend) << "Failed to create any GStreamer pipeline from candidate sinks. Last error:" << err;
        return false;
    }
#else
    // No in-process GStreamer: just generate the pipeline string for external launch
    // For external gst-launch builds, we will try the candidate sinks and pick the first pipeline string that is non-empty.
    QString pipelineStr;
    for (const QString &trySink : candidateSinks) {
        pipelineStr = generatePipelineString(device, resolution, framerate, trySink);
        if (!pipelineStr.isEmpty()) {
            m_selectedSink = trySink;
            break;
        }
    }
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
    qCDebug(log_gstreamer_backend) << "Current device:" << m_currentDevicePath;
    qCDebug(log_gstreamer_backend) << "Current resolution:" << m_currentResolution;
    qCDebug(log_gstreamer_backend) << "Current framerate:" << m_currentFramerate;

    // Prefer direct pipeline when we have a configured device
    if (!m_currentDevicePath.isEmpty()) {
#else
    if (m_gstProcess && m_gstProcess->state() == QProcess::Running) {
        m_gstProcess->terminate();
        if (!m_gstProcess->waitForFinished(2000)) m_gstProcess->kill();
    }
#endif
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
    qCDebug(log_gstreamer_backend) << "GStreamer: attempting direct pipeline for device" << m_currentDevicePath;

    if (m_currentDevicePath.isEmpty()) {
        qCWarning(log_gstreamer_backend) << "No device configured for direct pipeline";
        return false;
    }

    if (!createGStreamerPipeline(m_currentDevicePath, m_currentResolution, m_currentFramerate)) {
        qCWarning(log_gstreamer_backend) << "createGStreamerPipeline failed";
        return false;
    }

    return startGStreamerPipeline();
}

bool GStreamerBackendHandler::startGStreamerPipeline()
{
    qCDebug(log_gstreamer_backend) << "Starting GStreamer pipeline";
#ifdef HAVE_GSTREAMER
    // Build a list of candidate sinks and attempt to start the pipeline using each
    const QString platform = QGuiApplication::platformName();
    QStringList candidates = Openterface::GStreamer::SinkSelector::candidateSinks(platform);

    // If we already have a selected sink, make sure it is tried first
    if (!m_selectedSink.isEmpty()) {
        // move the current sink to front if present
        if (candidates.contains(m_selectedSink)) {
            candidates.removeAll(m_selectedSink);
            candidates.prepend(m_selectedSink);
        } else {
            candidates.prepend(m_selectedSink);
        }
    }

    QString lastErr;
    for (const QString &trySink : candidates) {
        qCDebug(log_gstreamer_backend) << "Attempting to start pipeline using sink:" << trySink;

        // If we don't have a pipeline or pipeline sink doesn't match trySink, (re)create it
        if (!m_pipeline || m_selectedSink != trySink) {
            // Cleanup any existing pipeline first
            cleanupGStreamer();

            QString createErr;
            m_pipeline = Openterface::GStreamer::PipelineFactory::createPipeline(m_currentDevicePath, m_currentResolution, m_currentFramerate, trySink, createErr);
            if (!m_pipeline) {
                qCWarning(log_gstreamer_backend) << "Failed to create pipeline with sink" << trySink << ":" << createErr;
                lastErr = createErr;
                continue; // try next sink
            }
            m_selectedSink = trySink;
            // get bus for error diagnostics
            m_bus = gst_element_get_bus(m_pipeline);
        }

        // Try start in-process first
        if (m_inProcessRunner) {
            QString err;
            bool ok = m_inProcessRunner->start(m_pipeline, 5000, &err);
            if (ok) {
                m_pipelineRunning = true;
                qCDebug(log_gstreamer_backend) << "Pipeline started successfully with sink:" << trySink;
                // Try to bind overlay now that pipeline is running
                qCDebug(log_gstreamer_backend) << "Attempting overlay setup after in-process pipeline start (sink:" << trySink << ")";
                setupVideoOverlayForCurrentPipeline();
                if (m_overlaySetupPending) completePendingOverlaySetup();
                // Attach frame probe to count buffers and show realtime FPS
                m_frameCount.store(0, std::memory_order_relaxed);
                attachFrameProbe();
                if (m_healthCheckTimer && !m_healthCheckTimer->isActive()) m_healthCheckTimer->start(1000);
                return true;
            }

            qCWarning(log_gstreamer_backend) << "In-process runner failed with sink" << trySink << ":" << err;
            Openterface::GStreamer::GstHelpers::parseAndLogGstErrorMessage(m_bus, "START_PIPELINE");
            lastErr = err;
            // Try next sink
            cleanupGStreamer();
            continue;
        }

        // Fallback to gst_element_set_state
        GstStateChangeReturn ret = gst_element_set_state(m_pipeline, GST_STATE_PLAYING);
        if (ret == GST_STATE_CHANGE_FAILURE) {
            qCWarning(log_gstreamer_backend) << "gst_element_set_state PLAYING failed for sink" << trySink;
            Openterface::GStreamer::GstHelpers::parseAndLogGstErrorMessage(m_bus, "START_PIPELINE");
            lastErr = QStringLiteral("gst_element_set_state failed");
            // try next sink
            cleanupGStreamer();
            continue;
        }

    m_pipelineRunning = true;
    qCDebug(log_gstreamer_backend) << "Pipeline set to PLAYING with sink:" << trySink;
    // Attempt overlay binding now that the pipeline is playing
    qCDebug(log_gstreamer_backend) << "Attempting overlay setup after gst_element_set_state (sink:" << trySink << ")";
    setupVideoOverlayForCurrentPipeline();
    // Attach frame probe and start health check timer
    m_frameCount.store(0, std::memory_order_relaxed);
    attachFrameProbe();
    if (m_healthCheckTimer && !m_healthCheckTimer->isActive()) m_healthCheckTimer->start(1000);
    return true;
    }

    qCCritical(log_gstreamer_backend) << "Failed to start any pipeline using candidate sinks. Last error:" << lastErr;
    return false;
#else
    // Fallback: external runner (gst-launch) if available. Try candidate sinks in order.
    QString program = "gst-launch-1.0";

    bool started = false;
    for (const QString &trySink : Openterface::GStreamer::SinkSelector::candidateSinks(QGuiApplication::platformName())) {
        QString candidatePipeline = generatePipelineString(m_currentDevicePath, m_currentResolution, m_currentFramerate, trySink);
        if (candidatePipeline.isEmpty()) continue;

        qCDebug(log_gstreamer_backend) << "Trying external runner with sink:" << trySink << "pipeline:" << candidatePipeline;

        if (!m_externalRunner) {
            qCWarning(log_gstreamer_backend) << "No external runner available";
            break;
        }

        bool ok = false;
        if (m_gstProcess) ok = m_externalRunner->start(m_gstProcess, candidatePipeline, program);
        else ok = m_externalRunner->start(candidatePipeline, program);

        if (ok) {
            m_selectedSink = trySink;
            started = true;
            break;
        }
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
            // Use helper to set NULL and wait for state transition to avoid unref'ing playing elements
            QString err;
            if (!Openterface::GStreamer::GstHelpers::setPipelineStateWithTimeout(m_pipeline, GST_STATE_NULL, 2000, &err)) {
                qCWarning(log_gstreamer_backend) << "stopGStreamerPipeline: failed to set pipeline to NULL:" << err;
                // Try a direct set as a last resort
                gst_element_set_state(m_pipeline, GST_STATE_NULL);
            }
        }
        // Also clear cached overlay sink before unref'ing pipeline - do this unconditionally
        if (m_currentOverlaySink) {
            if (GST_IS_VIDEO_OVERLAY(m_currentOverlaySink))
                gst_video_overlay_set_window_handle(GST_VIDEO_OVERLAY(m_currentOverlaySink), 0);
            if (GST_IS_OBJECT(m_currentOverlaySink)) gst_object_unref(m_currentOverlaySink);
            m_currentOverlaySink = nullptr;
            qCDebug(log_gstreamer_backend) << "Cleared cached overlay sink";
        }
        // Detach any frame probe we may have installed
        detachFrameProbe();
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
    // For external runner we can't attach pad probes (no in-process pipeline). Reset buffers counter for logging.
    m_frameCount.store(0, std::memory_order_relaxed);
    // Try to set up overlay when an external runner starts
    qCDebug(log_gstreamer_backend) << "Attempting overlay setup after external GStreamer runner started (sink:" << m_selectedSink << ")";
    setupVideoOverlayForCurrentPipeline();
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

void GStreamerBackendHandler::onDeviceUnplugged(const DeviceInfo& device)
{
    qCInfo(log_gstreamer_backend) << "GStreamerBackendHandler: Device unplugged event received";
    qCInfo(log_gstreamer_backend) << "  Port Chain:" << device.portChain;
    qCInfo(log_gstreamer_backend) << "  Current device port chain:" << m_currentDevicePortChain;
    qCInfo(log_gstreamer_backend) << "  Current device path:" << m_currentDevicePath;
    qCInfo(log_gstreamer_backend) << "  Pipeline running:" << m_pipelineRunning;
    
    // Match by port chain like the serial port manager and FFmpeg backend do
    // This ensures we only stop the camera if the unplugged device is our current camera
    if (!m_currentDevicePortChain.isEmpty() && 
        m_currentDevicePortChain == device.portChain) {
        qCInfo(log_gstreamer_backend) << "  → Our current camera device was unplugged, stopping GStreamer pipeline";
        
        // Stop the pipeline immediately to avoid segfault from accessing destroyed hardware
        if (m_pipelineRunning) {
            QTimer::singleShot(0, this, [this]() {
                qCDebug(log_gstreamer_backend) << "Stopping GStreamer pipeline due to device unplug";
                stopCamera();
                m_currentDevicePortChain.clear();
                m_currentDevicePath.clear();
                emit backendWarning("Camera device was unplugged");
            });
        }
    } else {
        qCDebug(log_gstreamer_backend) << "  → Unplugged device is not our current camera, ignoring";
    }
}

void GStreamerBackendHandler::onDevicePluggedIn(const DeviceInfo& device)
{
    qCDebug(log_gstreamer_backend) << "GStreamerBackendHandler: New device plugged in event received";
    qCDebug(log_gstreamer_backend) << "  Port Chain:" << device.portChain;
    qCDebug(log_gstreamer_backend) << "  Has Camera:" << device.hasCameraDevice();
    // Note: We don't auto-restart the camera here - let the UI handle reconnection
}

void GStreamerBackendHandler::setVideoOutput(QWidget* widget)
{
    // Uninstall event filter from any previous widget
    uninstallVideoWidgetEventFilter();

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

    // Install event filter to track lifecycle events (show/winId/resize)
    installVideoWidgetEventFilter();

    // If pipeline exists, attempt overlay setup now
    if (m_pipeline) {
    setupVideoOverlayForCurrentPipeline();
    if (m_overlaySetupPending) completePendingOverlaySetup();
    }
}

void GStreamerBackendHandler::setVideoOutput(QGraphicsVideoItem* videoItem)
{
    // Uninstall event filter from previous graphics view
    if (m_graphicsVideoItem && m_graphicsVideoItem->scene()) {
        QList<QGraphicsView*> prevViews = m_graphicsVideoItem->scene()->views();
        if (!prevViews.isEmpty()) uninstallGraphicsViewEventFilter(prevViews.first());
    }

    m_graphicsVideoItem = videoItem;
    m_videoWidget = nullptr;
    m_videoPane = nullptr;

    if (!videoItem) return;

    qCDebug(log_gstreamer_backend) << "Configuring QGraphicsVideoItem as video output";

    // Install event filter on the first host view (if any)
    if (videoItem && videoItem->scene()) {
        QList<QGraphicsView*> views = videoItem->scene()->views();
        if (!views.isEmpty()) installGraphicsViewEventFilter(views.first());
    }

    // Trigger overlay setup if needed
    if (m_pipeline) setupVideoOverlayForCurrentPipeline();
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

void GStreamerBackendHandler::setVideoOutput(VideoPane* videoPane)
{
    // Uninstall event filter from previous VideoPane overlay
    if (m_videoPane && m_videoPane->getOverlayWidget()) {
        QWidget* prevOv = m_videoPane->getOverlayWidget();
        prevOv->removeEventFilter(this);
        m_watchedObjects.remove(prevOv);
        if (QWidget* top = prevOv->window()) {
            if (top != prevOv) {
                top->removeEventFilter(this);
                m_watchedObjects.remove(top);
            }
        }
        qCDebug(log_gstreamer_backend) << "Removed event filter from previous VideoPane overlay widget (" << prevOv << ")";
    }

    m_videoPane = videoPane;
    m_videoWidget = nullptr;
    m_graphicsVideoItem = nullptr;

    if (!videoPane) return;

    qCDebug(log_gstreamer_backend) << "Configuring VideoPane as video output";
    // If the VideoPane exposes an overlay widget, install event filter
    if (QWidget* ov = videoPane->getOverlayWidget()) {
        ov->installEventFilter(this);
        m_watchedObjects.insert(ov);
        if (QWidget* top = ov->window()) {
            if (top != ov) {
                top->installEventFilter(this);
                m_watchedObjects.insert(top);
            }
        }
        if (!ov->isVisible()) ov->show();
        if (ov->winId() == 0) ov->createWinId();
        qCDebug(log_gstreamer_backend) << "Installed event filter on VideoPane overlay widget (" << ov << ") and top-level";
    }

    if (m_pipeline) setupVideoOverlayForCurrentPipeline();
}

void GStreamerBackendHandler::completePendingOverlaySetup()
{
    qCDebug(log_gstreamer_backend) << "Completing pending overlay setup (pendingFlag=" << m_overlaySetupPending << ")";
    bool ok = Openterface::GStreamer::VideoOverlayManager::completePendingOverlaySetup(m_pipeline,
                                                                              m_videoWidget,
                                                                              m_graphicsVideoItem,
                                                                              m_videoPane,
                                                                              m_overlaySetupPending);
    qCDebug(log_gstreamer_backend) << "completePendingOverlaySetup result:" << ok << "pendingFlag now=" << m_overlaySetupPending;
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
        // Choose the target widget to pass into the overlay setup - prefer VideoPane overlay widget when available
        QWidget* targetWidget = nullptr;
        // Be defensive: check if m_videoPane still exists and is valid before accessing it
        if (m_videoPane) {
            QWidget* ov = m_videoPane->getOverlayWidget();
            if (ov) targetWidget = ov;
        }
        if (!targetWidget && m_videoWidget) {
            targetWidget = m_videoWidget;
        }

        qCDebug(log_gstreamer_backend) << "Attempting overlay setup for pipeline with windowId:" << windowId << "targetWidget:" << targetWidget << "graphicsItem:" << m_graphicsVideoItem;
        bool ok = Openterface::GStreamer::VideoOverlayManager::setupVideoOverlayForPipeline(m_pipeline, windowId, targetWidget, m_graphicsVideoItem);
        if (ok) {
            m_overlaySetupPending = false;
            qCDebug(log_gstreamer_backend) << "Overlay setup completed for current pipeline";
            // Cache the overlay sink for future render rectangle updates
            GstElement* overlay = findOverlaySinkInPipeline();
            if (overlay) {
                // Replace previous cached overlay sink if any
                if (m_currentOverlaySink && m_currentOverlaySink != overlay) {
                    if (GST_IS_VIDEO_OVERLAY(m_currentOverlaySink)) {
                        gst_video_overlay_set_window_handle(GST_VIDEO_OVERLAY(m_currentOverlaySink), 0);
                    }
                    if (GST_IS_OBJECT(m_currentOverlaySink)) gst_object_unref(m_currentOverlaySink);
                }
                m_currentOverlaySink = overlay;
                const GstElementFactory* f = gst_element_get_factory(overlay);
                const gchar* name = f ? gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(f)) : "unknown";
                qCDebug(log_gstreamer_backend) << "Cached overlay sink for pipeline:" << (name ? name : "unknown");
            }
        } else {
            m_overlaySetupPending = true;
            qCWarning(log_gstreamer_backend) << "Failed to setup overlay for current pipeline - marking overlay as pending for retry";
            // Add sink type diagnostics for failed overlay
#ifdef HAVE_GSTREAMER
            GstElement* videoSink = gst_bin_get_by_name(GST_BIN(m_pipeline), "videosink");
            if (!videoSink) videoSink = gst_bin_get_by_interface(GST_BIN(m_pipeline), GST_TYPE_VIDEO_OVERLAY);
            if (videoSink) {
                const GstElementFactory* factory = gst_element_get_factory(videoSink);
                const gchar* sinkName = factory ? gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(factory)) : "unknown";
                qCDebug(log_gstreamer_backend) << "Overlay failed for sink:" << (sinkName ? sinkName : "unknown");
                gst_object_unref(videoSink);
            }
#endif
        }
    } else {
        qCWarning(log_gstreamer_backend) << "No valid window ID available for overlay setup";
        m_overlaySetupPending = true; // Retry later
    }
}

GstElement* GStreamerBackendHandler::findOverlaySinkInPipeline() const
{
    if (!m_pipeline) return nullptr;

    GstElement* videoSink = gst_bin_get_by_name(GST_BIN(m_pipeline), "videosink");
    GstElement* overlaySink = nullptr;
    if (videoSink) {
        if (GST_IS_VIDEO_OVERLAY(videoSink)) {
            overlaySink = videoSink;
        } else if (GST_IS_BIN(videoSink)) {
            GstIterator* iter = gst_bin_iterate_sinks(GST_BIN(videoSink));
            GValue item = G_VALUE_INIT;
            while (gst_iterator_next(iter, &item) == GST_ITERATOR_OK) {
                GstElement* childSink = GST_ELEMENT(g_value_get_object(&item));
                if (childSink && GST_IS_VIDEO_OVERLAY(childSink)) {
                    overlaySink = childSink;
                    g_value_unset(&item);
                    break;
                }
                g_value_unset(&item);
            }
            gst_iterator_free(iter);
        }
    }

    if (!overlaySink) {
        overlaySink = gst_bin_get_by_interface(GST_BIN(m_pipeline), GST_TYPE_VIDEO_OVERLAY);
    }

    if (!overlaySink) {
        // Return a reference to the element
        overlaySink = gst_bin_get_by_interface(GST_BIN(m_pipeline), GST_TYPE_VIDEO_OVERLAY);
        if (videoSink && overlaySink != videoSink) gst_object_unref(videoSink);
        return overlaySink;
    }

    if (videoSink) gst_object_unref(videoSink);
    return nullptr;
}

void GStreamerBackendHandler::refreshVideoOverlay()
{
    qCDebug(log_gstreamer_backend) << "Refreshing video overlay";
    setupVideoOverlayForCurrentPipeline();
    if (m_overlaySetupPending) completePendingOverlaySetup();
}

// Event filter helpers and lifecycle handling
void GStreamerBackendHandler::installVideoWidgetEventFilter()
{
    if (m_videoWidget) {
        // Install on the widget itself
        m_videoWidget->removeEventFilter(this);
        m_videoWidget->installEventFilter(this);
        m_watchedObjects.insert(m_videoWidget);
        // Also install on the top-level window, so we catch WinId changes associated with
        // the native window of the top-level (when the video widget is a child)
    if (QWidget* top = m_videoWidget->window()) {
            if (top != m_videoWidget) {
                top->removeEventFilter(this);
                top->installEventFilter(this);
                m_watchedObjects.insert(top);
                qCDebug(log_gstreamer_backend) << "Installed event filter on video widget top-level (" << top << ")";
            }
            qCDebug(log_gstreamer_backend) << "Installed event filter on video widget (" << m_videoWidget << ") class:" << m_videoWidget->metaObject()->className() << "winId:" << m_videoWidget->winId();
        }
    }
}

void GStreamerBackendHandler::uninstallVideoWidgetEventFilter()
{
    if (m_videoWidget) {
        // Remove from the widget itself
        m_videoWidget->removeEventFilter(this);
        m_watchedObjects.remove(m_videoWidget);
        // Also remove from the top-level window if present
        if (QWidget* top = m_videoWidget->window()) {
            if (top != m_videoWidget) {
                top->removeEventFilter(this);
                m_watchedObjects.remove(top);
            }
            qCDebug(log_gstreamer_backend) << "Removed event filter from video widget top-level (" << top << ")";
        }
        qCDebug(log_gstreamer_backend) << "Removed event filter from video widget (" << m_videoWidget << ")";
    }
}

void GStreamerBackendHandler::installGraphicsViewEventFilter(QGraphicsView* view)
{
    if (view) {
        view->removeEventFilter(this);
        view->installEventFilter(this);
        m_watchedObjects.insert(view);
        // Also install on top-level window to catch winId related events
        if (QWidget* top = view->window()) {
            if (top != view) {
                top->removeEventFilter(this);
                top->installEventFilter(this);
                m_watchedObjects.insert(top);
            }
        }
        qCDebug(log_gstreamer_backend) << "Installed event filter on graphics view (" << view << ") and top-level";
    }
}

void GStreamerBackendHandler::uninstallGraphicsViewEventFilter(QGraphicsView* view)
{
    if (view) {
        view->removeEventFilter(this);
        m_watchedObjects.remove(view);
        if (QWidget* top = view->window()) {
            if (top != view) {
                top->removeEventFilter(this);
                m_watchedObjects.remove(top);
            }
        }
        qCDebug(log_gstreamer_backend) << "Removed event filter from graphics view (" << view << ")";
    }
}

bool GStreamerBackendHandler::eventFilter(QObject *watched, QEvent *event)
{
    // CRITICAL: If handler is destructing, exit early to avoid accessing destroyed members
    // This prevents crashes when Qt calls event filters during shutdown sequence
    if (m_isDestructing) {
        return QObject::eventFilter(watched, event);
    }

    // Video widget events
    if (m_videoWidget && (watched == m_videoWidget || watched == m_videoWidget->window())) {
        switch (event->type()) {
            case QEvent::Show:
            case QEvent::WinIdChange:
            case QEvent::ShowToParent:
            {
                QWidget* target = m_videoWidget;
                WId wid = target ? target->winId() : 0;
                qCDebug(log_gstreamer_backend) << "Overlay trigger (video widget): target=" << target << "watched=" << watched << "event=" << qEventTypeName(event->type()) << "winId=" << wid;
                setupVideoOverlayForCurrentPipeline();
                if (m_overlaySetupPending) completePendingOverlaySetup();
                break;
            }
            case QEvent::Resize:
            {
                QResizeEvent* re = static_cast<QResizeEvent*>(event);
                if (re) {
                    qCDebug(log_gstreamer_backend) << "Video widget resize event: new size=" << re->size();
                    updateVideoRenderRectangle(re->size());
                }
                break;
            }
            case QEvent::Destroy:
                qCDebug(log_gstreamer_backend) << "Video widget destroyed - removing event filters: target=" << m_videoWidget << "watched=" << watched;
                uninstallVideoWidgetEventFilter();
                break;
            default:
                break;
        }
    }

    // Graphics view events
    // If event is on the graphics view or its top-level window
    QGraphicsView* viewPtr = nullptr;
    if (m_graphicsVideoItem && m_graphicsVideoItem->scene()) {
        QList<QGraphicsView*> views = m_graphicsVideoItem->scene()->views();
        if (!views.isEmpty()) viewPtr = views.first();
    }
    if ((viewPtr && (watched == viewPtr || watched == viewPtr->window())) || qobject_cast<QGraphicsView*>(watched)) {
        QGraphicsView* view = qobject_cast<QGraphicsView*>(watched);
        if (!view) view = viewPtr;
        switch (event->type()) {
            case QEvent::Show:
            case QEvent::WinIdChange:
                qCDebug(log_gstreamer_backend) << "Overlay trigger (graphics view): targetView=" << view << "watched=" << watched << "event=" << qEventTypeName(event->type()) << "winId=" << (view ? view->winId() : 0);
                setupVideoOverlayForCurrentPipeline();
                if (m_overlaySetupPending) completePendingOverlaySetup();
                break;
            case QEvent::Resize:
            {
                QResizeEvent* re = static_cast<QResizeEvent*>(event);
                if (re) {
                    qCDebug(log_gstreamer_backend) << "Graphics view resize event: new size=" << re->size();
                    updateVideoRenderRectangle(re->size());
                }
                break;
            }
            case QEvent::Destroy:
                qCDebug(log_gstreamer_backend) << "Graphics view destroyed - removing event filters, view=" << view << "watched=" << watched;
                uninstallGraphicsViewEventFilter(view);
                break;
            default:
                break;
        }
    }

    // VideoPane overlay widget events
    // Be defensive: m_videoPane or its overlay widget might be destroyed
    if (!m_videoPane) {
        // VideoPane already destroyed, nothing to do
        return QObject::eventFilter(watched, event);
    }
    
    QWidget* ovWidget = m_videoPane->getOverlayWidget();
    if (ovWidget && (watched == ovWidget || watched == ovWidget->window())) {
        switch (event->type()) {
            case QEvent::Show:
            case QEvent::WinIdChange:
            {
                WId wid = ovWidget ? ovWidget->winId() : 0;
                qCDebug(log_gstreamer_backend) << "Overlay trigger (VideoPane overlay widget): targetOverlay=" << ovWidget << "watched=" << watched << "event=" << qEventTypeName(event->type()) << "winId=" << wid;
                setupVideoOverlayForCurrentPipeline();
                break;
            }
            case QEvent::Resize:
            {
                QResizeEvent* re = static_cast<QResizeEvent*>(event);
                if (re && ovWidget) {
                    qCDebug(log_gstreamer_backend) << "VideoPane overlay resize event: new size=" << re->size();
                    updateVideoRenderRectangle(re->size());
                }
                break;
            }
            case QEvent::Destroy:
            {
                qCDebug(log_gstreamer_backend) << "VideoPane overlay widget destroyed - removing event filters, overlay=" << ovWidget << "watched=" << watched;
                // Do NOT try to access m_videoPane again after widget is destroyed
                // Just remove the filter from the watched object directly
                if (watched && watched == ovWidget) {
                    watched->removeEventFilter(this);
                } else if (watched && ovWidget) {
                    // Remove from window if watched is the window
                    if (watched == ovWidget->window()) {
                        watched->removeEventFilter(this);
                    }
                }
                break;
            }
            default:
                break;
        }
    }

    return QObject::eventFilter(watched, event);
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
            
            if (result == 0) {
                qCWarning(log_gstreamer_backend) << "Window ID" << windowId << "is not a valid X11 window";
                if (display) XCloseDisplay(display);
                return false;
            }

            qCDebug(log_gstreamer_backend) << "Window ID" << windowId << "validated successfully (X11)";
            if (display) XCloseDisplay(display);
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
            // Log realtime FPS measured via pad probe (frames counted since last health check tick)
            quint64 framesSinceLast = m_frameCount.exchange(0, std::memory_order_relaxed);
            qCDebug(log_gstreamer_backend) << "Realtime GStreamer FPS (last interval):" << framesSinceLast;
            emit fpsChanged(static_cast<double>(framesSinceLast));
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
    // Prefer VideoPane overlay widget if available
    if (m_videoPane) {
        if (QWidget* ov = m_videoPane->getOverlayWidget()) {
            if (!ov->isVisible()) {
                qCDebug(log_gstreamer_backend) << "VideoPane overlay widget not visible, making it visible";
                ov->show();
            }
            if (!ov->testAttribute(Qt::WA_NativeWindow)) {
                qCDebug(log_gstreamer_backend) << "Setting native window attribute for VideoPane overlay";
                ov->setAttribute(Qt::WA_NativeWindow, true);
                ov->setAttribute(Qt::WA_PaintOnScreen, true);
            }
            WId ovId = ov->winId();
            if (ovId == 0) {
                qCDebug(log_gstreamer_backend) << "VideoPane overlay window ID is 0 - forcing window creation";
                ov->createWinId();
                ovId = ov->winId();
            }
            qCDebug(log_gstreamer_backend) << "VideoPane overlay window ID:" << ovId;
            return ovId;
        }
    }

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

void GStreamerBackendHandler::connectToHotplugMonitor()
{
    qCDebug(log_gstreamer_backend) << "GStreamerBackendHandler: Connecting to hotplug monitor";
    
#ifdef HAVE_GSTREAMER
    // Get HotplugMonitor from DeviceManager
    DeviceManager& deviceManager = DeviceManager::getInstance();
    if (HotplugMonitor* monitor = deviceManager.getHotplugMonitor()) {
        m_hotplugMonitor = monitor;
        
        // Connect to device unplugging signal
        connect(m_hotplugMonitor, &HotplugMonitor::deviceUnplugged,
                this, &GStreamerBackendHandler::onDeviceUnplugged,
                Qt::AutoConnection);
        
        // Connect to new device plugged in signal
        connect(m_hotplugMonitor, &HotplugMonitor::newDevicePluggedIn,
                this, &GStreamerBackendHandler::onDevicePluggedIn,
                Qt::AutoConnection);
        
        qCDebug(log_gstreamer_backend) << "GStreamerBackendHandler successfully connected to hotplug monitor";
    } else {
        qCWarning(log_gstreamer_backend) << "Failed to get hotplug monitor from device manager";
    }
#else
    qCDebug(log_gstreamer_backend) << "GStreamer not available - hotplug monitoring unavailable";
#endif
}

void GStreamerBackendHandler::setCurrentDevicePortChain(const QString& portChain)
{
    m_currentDevicePortChain = portChain;
    qCDebug(log_gstreamer_backend) << "GStreamer: current device port chain set to" << portChain;
}

void GStreamerBackendHandler::setCurrentDevice(const QString& devicePath)
{
    m_currentDevicePath = devicePath;
    qCDebug(log_gstreamer_backend) << "GStreamer: current device set to" << devicePath;
}

bool GStreamerBackendHandler::initializeGStreamer()
{
#ifdef HAVE_GSTREAMER
    if (!gst_is_initialized()) {
        int argc = 0;
        char **argv = nullptr;
        gst_init(&argc, &argv);
        qCDebug(log_gstreamer_backend) << "GStreamer initialized in-process";
    }
    return true;
#else
    return false;
#endif
}

void GStreamerBackendHandler::cleanupGStreamer()
{
    qCDebug(log_gstreamer_backend) << "cleanupGStreamer invoked";
#ifdef HAVE_GSTREAMER
    if (m_pipeline) {
        // Detach any frame probe attached to this pipeline
        detachFrameProbe();
        // Clear any overlay sink cached
        if (m_currentOverlaySink) {
            if (GST_IS_VIDEO_OVERLAY(m_currentOverlaySink))
                gst_video_overlay_set_window_handle(GST_VIDEO_OVERLAY(m_currentOverlaySink), 0);
            if (GST_IS_OBJECT(m_currentOverlaySink)) gst_object_unref(m_currentOverlaySink);
            m_currentOverlaySink = nullptr;
            qCDebug(log_gstreamer_backend) << "Cleared cached overlay sink";
        }
        gst_element_set_state(m_pipeline, GST_STATE_NULL);
        // Wait for pipeline to reach NULL to avoid unreffing elements while still PLAYING
        GstState state, pending;
        gst_element_get_state(m_pipeline, &state, &pending, 2000 * GST_MSECOND);
        if (state != GST_STATE_NULL) {
            qCWarning(log_gstreamer_backend) << "cleanupGStreamer: pipeline did not reach NULL state in time";
        }
        if (m_bus) {
            gst_bus_remove_signal_watch(m_bus);
            gst_object_unref(m_bus);
            m_bus = nullptr;
        }
        gst_object_unref(m_pipeline);
        m_pipeline = nullptr;
    }
#endif
    // Ensure external process stopped
    if (m_gstProcess) {
        if (m_gstProcess->state() == QProcess::Running) {
            m_gstProcess->terminate();
            if (!m_gstProcess->waitForFinished(2000)) m_gstProcess->kill();
        }
        delete m_gstProcess;
        m_gstProcess = nullptr;
    }
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
    // Calculate scaling based on viewport size vs original video resolution
    if (widgetSize.width() <= 0 || widgetSize.height() <= 0) {
        qCDebug(log_gstreamer_backend) << "Invalid widget size, using render rectangle at 0,0," << widgetSize.width() << widgetSize.height();
        updateVideoRenderRectangle(0, 0, widgetSize.width(), widgetSize.height());
        return;
    }

    // Scale video to fill the viewport - calculate dimensions to maintain aspect ratio
    double videoAspect = (double)m_currentResolution.width() / m_currentResolution.height();
    double viewportAspect = (double)widgetSize.width() / widgetSize.height();
    
    int scaledWidth = widgetSize.width();
    int scaledHeight = widgetSize.height();
    
    // If video is wider than viewport, scale down height; otherwise scale down width
    if (videoAspect > viewportAspect) {
        // Video is wider - scale to fit width, center vertically
        scaledHeight = (int)(widgetSize.width() / videoAspect);
    } else {
        // Video is taller - scale to fit height, center horizontally
        scaledWidth = (int)(widgetSize.height() * videoAspect);
    }
    
    // Center the scaled video in the viewport
    int offsetX = (widgetSize.width() - scaledWidth) / 2;
    int offsetY = (widgetSize.height() - scaledHeight) / 2;
    
    qCDebug(log_gstreamer_backend) << "Calculated viewport-based scaling:"
                                   << "viewport:" << widgetSize
                                   << "videoRes:" << m_currentResolution
                                   << "scaledSize:" << QSize(scaledWidth, scaledHeight)
                                   << "offset:" << offsetX << offsetY;
    
    updateVideoRenderRectangle(offsetX, offsetY, scaledWidth, scaledHeight);
}

void GStreamerBackendHandler::updateVideoRenderRectangle(int x, int y, int width, int height)
{
    if (!m_pipeline || !m_pipelineRunning) {
        qCDebug(log_gstreamer_backend) << "Pipeline not running, cannot update render rectangle";
        return;
    }
    
    // Find the video sink element in the pipeline
    GstElement* videoSink = gst_bin_get_by_name(GST_BIN(m_pipeline), "videosink");
    GstElement* overlaySink = nullptr;

    if (videoSink) {
        // If the element itself supports overlay, use it directly
        if (GST_IS_VIDEO_OVERLAY(videoSink)) {
            overlaySink = videoSink;
        } else if (GST_IS_BIN(videoSink)) {
            // Some sinks like autovideosink are a bin; find overlay-capable child
            GstIterator* iter = gst_bin_iterate_sinks(GST_BIN(videoSink));
            GValue item = G_VALUE_INIT;
            while (gst_iterator_next(iter, &item) == GST_ITERATOR_OK) {
                GstElement* childSink = GST_ELEMENT(g_value_get_object(&item));
                if (childSink && GST_IS_VIDEO_OVERLAY(childSink)) {
                    overlaySink = childSink;
                    g_value_unset(&item);
                    break;
                }
                g_value_unset(&item);
            }
            gst_iterator_free(iter);
        }
    }

    if (!overlaySink) {
        // Fallback: find any overlay-capable element in pipeline
        overlaySink = gst_bin_get_by_interface(GST_BIN(m_pipeline), GST_TYPE_VIDEO_OVERLAY);
        if (!overlaySink) {
            // Try iterating all elements to find any which implements the overlay interface
            GstIterator* iter = gst_bin_iterate_elements(GST_BIN(m_pipeline));
            GValue val = G_VALUE_INIT;
            while (gst_iterator_next(iter, &val) == GST_ITERATOR_OK) {
                GstElement* el = GST_ELEMENT(g_value_get_object(&val));
                if (el && GST_IS_VIDEO_OVERLAY(el)) {
                    overlaySink = el;
                    g_value_unset(&val);
                    break;
                }
                g_value_unset(&val);
            }
            gst_iterator_free(iter);
        }
    }

    if (m_currentOverlaySink) {
        // Prefer cached overlay sink if present
        overlaySink = m_currentOverlaySink;
    }

    if (m_currentOverlaySink && GST_IS_VIDEO_OVERLAY(m_currentOverlaySink)) {
        overlaySink = m_currentOverlaySink;
    }

    if (!overlaySink) {
        overlaySink = gst_bin_get_by_interface(GST_BIN(m_pipeline), GST_TYPE_VIDEO_OVERLAY);
    }

    if (overlaySink && GST_IS_VIDEO_OVERLAY(overlaySink)) {
        // Get the device pixel ratio (DPI scaling) from the screen
        // This handles OS-level display scaling (e.g., 150% scaling on Linux)
        qreal dpiScale = 1.0;
        if (QScreen* screen = QGuiApplication::primaryScreen()) {
            dpiScale = screen->devicePixelRatio();
            qCDebug(log_gstreamer_backend) << "DPI scale factor:" << dpiScale;
        }
        
        // Scale render rectangle to account for OS display scaling
        int scaledX = (int)(x * dpiScale);
        int scaledY = (int)(y * dpiScale);
        int scaledWidth = (int)(width * dpiScale);
        int scaledHeight = (int)(height * dpiScale);
        
        gst_video_overlay_set_render_rectangle(GST_VIDEO_OVERLAY(overlaySink), scaledX, scaledY, scaledWidth, scaledHeight);
        // Force sink to re-render if supported, improving responsiveness to rectangle updates
        gst_video_overlay_expose(GST_VIDEO_OVERLAY(overlaySink));
        qCDebug(log_gstreamer_backend) << "Updated render rectangle to:" << scaledX << scaledY << scaledWidth << scaledHeight
                                       << "(before scaling:" << x << y << width << height << ", DPI scale:" << dpiScale << ")";
        // If overlaySink is different from videoSink (child), unref both appropriately
        if (overlaySink != videoSink && overlaySink != m_currentOverlaySink) gst_object_unref(overlaySink);
        if (videoSink) gst_object_unref(videoSink);
    } else {
        qCWarning(log_gstreamer_backend) << "Cannot update render rectangle: video sink not found or doesn't support overlay";
        if (videoSink) gst_object_unref(videoSink);
    }
}

// Frame probe management (only when building with GStreamer)
#ifdef HAVE_GSTREAMER
void GStreamerBackendHandler::attachFrameProbe()
{
    if (!m_pipeline) return;

    // Ensure we don't attach twice
    if (m_frameProbePad && m_frameProbeId) return;

    GstPad* sinkPad = nullptr;
    GstElement* q = gst_bin_get_by_name(GST_BIN(m_pipeline), "display-queue");
    if (q) {
        sinkPad = gst_element_get_static_pad(q, "src");
        gst_object_unref(q);
    }

    GstElement* videoSink = nullptr;
    if (!sinkPad) {
        videoSink = gst_bin_get_by_name(GST_BIN(m_pipeline), "videosink");
        if (!videoSink) {
            // Try to find any element that supports video overlay as a fallback
            videoSink = gst_bin_get_by_interface(GST_BIN(m_pipeline), GST_TYPE_VIDEO_OVERLAY);
        }

        if (!videoSink) {
            qCWarning(log_gstreamer_backend) << "attachFrameProbe: videosink element not found in pipeline";
            return;
        }

        sinkPad = gst_element_get_static_pad(videoSink, "sink");
    }

    if (!sinkPad) {
        qCWarning(log_gstreamer_backend) << "attachFrameProbe: sink pad not found on videosink/display-queue";
        if (videoSink) gst_object_unref(videoSink);
        return;
    }

    // Keep a reference to the pad while probe is installed to avoid dangling pointers
    gst_object_ref(sinkPad);
    m_frameProbePad = sinkPad;
    m_frameProbeId = gst_pad_add_probe(sinkPad, GST_PAD_PROBE_TYPE_BUFFER, (GstPadProbeCallback)GStreamerBackendHandler::gstreamer_frame_probe_cb, this, nullptr);
    if (m_frameProbeId == 0) {
        qCWarning(log_gstreamer_backend) << "attachFrameProbe: failed to add pad probe";
        gst_object_unref(m_frameProbePad);
        m_frameProbePad = nullptr;
    } else {
        qCDebug(log_gstreamer_backend) << "attachFrameProbe: Pad probe added for realtime FPS counting";
    }

    if (videoSink) gst_object_unref(videoSink);
}

void GStreamerBackendHandler::detachFrameProbe()
{
    if (!m_frameProbePad) return;
    if (m_frameProbeId) {
        gst_pad_remove_probe(m_frameProbePad, m_frameProbeId);
        m_frameProbeId = 0;
    }
    gst_object_unref(m_frameProbePad);
    m_frameProbePad = nullptr;
    qCDebug(log_gstreamer_backend) << "detachFrameProbe: pad probe removed";
}
#endif

// No-op fallback if HAVE_GSTREAMER is not defined
#ifdef HAVE_GSTREAMER
void GStreamerBackendHandler::incrementFrameCount()
{
    m_frameCount.fetch_add(1, std::memory_order_relaxed);
}
#else
void GStreamerBackendHandler::incrementFrameCount() { Q_UNUSED(this); }
#endif

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