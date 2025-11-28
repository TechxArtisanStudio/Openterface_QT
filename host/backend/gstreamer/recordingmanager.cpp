#include "recordingmanager.h"
#include <QDebug>
#include <QLoggingCategory>
#include <QThread>
#ifdef HAVE_GSTREAMER
#include <gst/app/gstappsink.h>
#endif

Q_LOGGING_CATEGORY(log_gst_recording, "opf.backend.gstreamer.recording")

RecordingManager::RecordingManager(QObject* parent)
    : QObject(parent), m_mainPipeline(nullptr),
      m_recordingPipeline(nullptr), m_recordingTee(nullptr), m_recordingValve(nullptr),
      m_recordingSink(nullptr), m_recordingQueue(nullptr), m_recordingEncoder(nullptr),
      m_recordingVideoConvert(nullptr), m_recordingMuxer(nullptr), m_recordingFileSink(nullptr),
      m_recordingAppSink(nullptr), m_recordingTeeSrcPad(nullptr),
      m_recordingActive(false), m_recordingPaused(false), m_recordingVideoBitrate(0),
      m_recordingStartTime(0), m_recordingPausedTime(0), m_totalPausedDuration(0), m_recordingFrameNumber(0)
{
}

RecordingManager::~RecordingManager()
{
#ifdef HAVE_GSTREAMER
    // Clean up any active recording branch
    removeRecordingBranch();
#endif
}

bool RecordingManager::startRecording(GstElement* mainPipeline, const QString& outputPath, const QString& format, int videoBitrate)
{
    qCDebug(log_gst_recording) << "RecordingManager::startRecording to" << outputPath << "format:" << format << "bitrate:" << videoBitrate;

    if (m_recordingActive) {
        qCWarning(log_gst_recording) << "Recording is already active";
        emit recordingError("Recording is already active");
        return false;
    }

    if (!mainPipeline) {
        qCWarning(log_gst_recording) << "Main pipeline not available - cannot start recording";
        emit recordingError("Main pipeline not running");
        return false;
    }

    // Store main pipeline reference for branch operations
    m_mainPipeline = mainPipeline;

    // Validate output path
    QFileInfo outputFileInfo(outputPath);
    QDir outputDir = outputFileInfo.dir();
    if (!outputDir.exists()) {
        QString error = QString("Output directory does not exist: %1").arg(outputDir.absolutePath());
        qCCritical(log_gst_recording) << error;
        emit recordingError(error);
        return false;
    }

    QFileInfo outDirInfo(outputDir.absolutePath());
    if (!outDirInfo.isWritable()) {
        QString error = QString("Output directory is not writable: %1").arg(outputDir.absolutePath());
        qCCritical(log_gst_recording) << error;
        emit recordingError(error);
        return false;
    }

#ifdef HAVE_GSTREAMER
    // Prefer valve-based recording when possible. If that fails, try separate branch or frame-based fallbacks.
    if (!createRecordingBranch(outputPath, format, videoBitrate)) {
        qCWarning(log_gst_recording) << "Valve-based recording not available, attempting separate branch or frame-based fallback";
        // Try adding a separate branch (encoder+filesink) first
        if (!createSeparateRecordingPipeline(outputPath, format, videoBitrate)) {
            qCWarning(log_gst_recording) << "Separate pipeline approach failed, attempting frame-based appsink+ffmpeg fallback";
            if (!initializeFrameBasedRecording(format)) {
                QString error = QString("Failed to initialize any recording pipeline for format %1").arg(format);
                qCCritical(log_gst_recording) << error;
                emit recordingError(error);
                return false;
            }
        }
    }
#else
    Q_UNUSED(format)
    Q_UNUSED(videoBitrate)
    Q_UNUSED(outputPath)
    QString err = "GStreamer not available - external recording not supported by RecordingManager";
    qCWarning(log_gst_recording) << err;
    emit recordingError(err);
    return false;
#endif

    m_recordingActive = true;
    m_recordingPaused = false;
    m_recordingOutputPath = outputPath;
    m_recordingStartTime = QDateTime::currentMSecsSinceEpoch();
    m_totalPausedDuration = 0;

    qCInfo(log_gst_recording) << "Recording started successfully to:" << outputPath;
    emit recordingStarted(outputPath);
    return true;
}

#ifdef HAVE_GSTREAMER
bool RecordingManager::createSeparateRecordingPipeline(const QString& outputPath, const QString& format, int videoBitrate)
{
    qCDebug(log_gst_recording) << "RecordingManager::createSeparateRecordingPipeline to" << outputPath << "format" << format;

    if (!m_mainPipeline) {
        qCCritical(log_gst_recording) << "Main pipeline is null - cannot create separate recording pipeline";
        return false;
    }

    m_recordingTee = gst_bin_get_by_name(GST_BIN(m_mainPipeline), "t");
    if (!m_recordingTee) {
        qCCritical(log_gst_recording) << "Could not find tee element 't' in main pipeline for separate branch";
        emit recordingError("Pipeline lacks tee element for recording");
        return false;
    }

    // Clean up any existing elements first
    if (m_recordingQueue) {
        gst_element_set_state(m_recordingQueue, GST_STATE_NULL);
        gst_bin_remove(GST_BIN(m_mainPipeline), m_recordingQueue);
        m_recordingQueue = nullptr;
    }
    if (m_recordingEncoder) {
        gst_element_set_state(m_recordingEncoder, GST_STATE_NULL);
        gst_bin_remove(GST_BIN(m_mainPipeline), m_recordingEncoder);
        m_recordingEncoder = nullptr;
    }
    if (m_recordingMuxer) {
        gst_element_set_state(m_recordingMuxer, GST_STATE_NULL);
        gst_bin_remove(GST_BIN(m_mainPipeline), m_recordingMuxer);
        m_recordingMuxer = nullptr;
    }
    if (m_recordingFileSink) {
        gst_element_set_state(m_recordingFileSink, GST_STATE_NULL);
        gst_bin_remove(GST_BIN(m_mainPipeline), m_recordingFileSink);
        m_recordingFileSink = nullptr;
    }

    QString encoderName, muxerName;
    QString finalOutput = outputPath;
    if (format.toLower() == "mp4") {
        encoderName = "x264enc";
        muxerName = "mp4mux";
    } else if (format.toLower() == "avi") {
        encoderName = "jpegenc";
        muxerName = "avimux";
    } else if (format.toLower() == "mkv") {
        encoderName = "x264enc";
        muxerName = "matroskamux";
    } else {
        encoderName = "jpegenc";
        QFileInfo fi(outputPath);
        finalOutput = fi.path() + "/" + fi.completeBaseName() + ".mjpeg";
        muxerName.clear();
    }

    m_recordingQueue = gst_element_factory_make("queue", "recording-queue");
    m_recordingEncoder = gst_element_factory_make(encoderName.toUtf8().constData(), "recording-encoder");
    m_recordingFileSink = gst_element_factory_make("filesink", "recording-filesink");

    if (!m_recordingQueue || !m_recordingEncoder || !m_recordingFileSink) {
        qCCritical(log_gst_recording) << "Failed to create separate recording elements";
        return false;
    }

    if (encoderName == "jpegenc") {
        g_object_set(m_recordingEncoder, "quality", 85, nullptr);
    } else if (encoderName == "x264enc") {
        g_object_set(m_recordingEncoder, "bitrate", videoBitrate, nullptr);
        g_object_set(m_recordingEncoder, "speed-preset", 6, nullptr);
    }

    g_object_set(m_recordingFileSink, "location", finalOutput.toUtf8().constData(), nullptr);

    if (!muxerName.isEmpty()) {
        m_recordingMuxer = gst_element_factory_make(muxerName.toUtf8().constData(), "recording-muxer");
        if (!m_recordingMuxer) {
            qCCritical(log_gst_recording) << "Failed to create muxer" << muxerName;
            return false;
        }
        gst_bin_add_many(GST_BIN(m_mainPipeline), m_recordingQueue, m_recordingEncoder, m_recordingMuxer, m_recordingFileSink, NULL);
    } else {
        gst_bin_add_many(GST_BIN(m_mainPipeline), m_recordingQueue, m_recordingEncoder, m_recordingFileSink, NULL);
    }

    GstPad* teeSrcPad = gst_element_request_pad_simple(m_recordingTee, "src_%u");
    GstPad* queueSinkPad = gst_element_get_static_pad(m_recordingQueue, "sink");
    if (!teeSrcPad || !queueSinkPad) {
        qCCritical(log_gst_recording) << "Failed to obtain pads for separate recording branch";
        return false;
    }

    if (gst_pad_link(teeSrcPad, queueSinkPad) != GST_PAD_LINK_OK) {
        qCCritical(log_gst_recording) << "Failed to link tee to recording queue";
        gst_object_unref(teeSrcPad);
        gst_object_unref(queueSinkPad);
        return false;
    }
    gst_object_unref(teeSrcPad);
    gst_object_unref(queueSinkPad);

    // Link elements
    if (m_recordingMuxer) {
        if (!gst_element_link_many(m_recordingQueue, m_recordingEncoder, m_recordingMuxer, m_recordingFileSink, NULL)) {
            qCCritical(log_gst_recording) << "Failed to link recording elements with muxer";
            return false;
        }
    } else {
        if (!gst_element_link_many(m_recordingQueue, m_recordingEncoder, m_recordingFileSink, NULL)) {
            qCCritical(log_gst_recording) << "Failed to link recording elements without muxer";
            return false;
        }
    }

    // Sync states
    gst_element_sync_state_with_parent(m_recordingQueue);
    gst_element_sync_state_with_parent(m_recordingEncoder);
    if (m_recordingMuxer) gst_element_sync_state_with_parent(m_recordingMuxer);
    gst_element_sync_state_with_parent(m_recordingFileSink);

    m_recordingOutputPath = finalOutput;
    qCInfo(log_gst_recording) << "Separate recording branch added to" << finalOutput;
    return true;
}

bool RecordingManager::initializeFrameBasedRecording(const QString& format)
{
    qCDebug(log_gst_recording) << "RecordingManager::initializeFrameBasedRecording format" << format;

    if (!m_mainPipeline) {
        qCCritical(log_gst_recording) << "Main pipeline not set for frame-based recording";
        return false;
    }

    // Clean up any previous recording process
    if (m_recordingProcess) {
        m_recordingProcess->terminate();
        if (!m_recordingProcess->waitForFinished(3000)) m_recordingProcess->kill();
        delete m_recordingProcess;
        m_recordingProcess = nullptr;
    }

    // Find tee
    GstElement* tee = gst_bin_get_by_name(GST_BIN(m_mainPipeline), "t");
    if (!tee) {
        qCCritical(log_gst_recording) << "Could not find tee element 't' for frame-based recording";
        return false;
    }

    GstElement* queue = gst_element_factory_make("queue", "recording-queue");
    GstElement* videoconvert = gst_element_factory_make("videoconvert", "recording-convert");
    GstElement* appsink = gst_element_factory_make("appsink", "recording-appsink");

    if (!queue || !appsink) {
        qCCritical(log_gst_recording) << "Failed to create queue or appsink for frame-based recording";
        if (queue) gst_object_unref(queue);
        if (videoconvert) gst_object_unref(videoconvert);
        if (appsink) gst_object_unref(appsink);
        gst_object_unref(tee);
        return false;
    }

    // Try to determine resolution and framerate from pipeline - fallback to sensible defaults
    int width = 1280, height = 720, framerate = 30;
    // (Attempt to infer from pads / caps is possible but can be brittle; keep defaults to remain robust)

    GstCaps* caps = gst_caps_new_simple("video/x-raw",
                                        "format", G_TYPE_STRING, "RGB",
                                        "width", G_TYPE_INT, width,
                                        "height", G_TYPE_INT, height,
                                        "framerate", GST_TYPE_FRACTION, framerate, 1,
                                        NULL);

    g_object_set(appsink,
                 "caps", caps,
                 "emit-signals", TRUE,
                 "sync", FALSE,
                 "drop", TRUE,
                 "max-buffers", 5,
                 NULL);

    gst_caps_unref(caps);

    // Keep appsink reference
    m_recordingAppSink = appsink;

    // Connect appsink new-sample -> onNewRecordingSample
    g_signal_connect(appsink, "new-sample", G_CALLBACK(+[](GstAppSink* sink, gpointer user_data) -> GstFlowReturn {
        RecordingManager* mgr = static_cast<RecordingManager*>(user_data);
        return mgr->onNewRecordingSample(sink);
    }), this);

    // Add elements to main pipeline and link
    if (videoconvert) {
        gst_bin_add_many(GST_BIN(m_mainPipeline), queue, videoconvert, appsink, NULL);
    } else {
        gst_bin_add_many(GST_BIN(m_mainPipeline), queue, appsink, NULL);
    }

    GstPad* teeSrcPad = gst_element_request_pad_simple(tee, "src_%u");
    GstPad* queueSinkPad = gst_element_get_static_pad(queue, "sink");
    if (!teeSrcPad || !queueSinkPad) {
        qCCritical(log_gst_recording) << "Failed to get pads for frame-based recording";
        gst_object_unref(tee);
        return false;
    }

    if (gst_pad_link(teeSrcPad, queueSinkPad) != GST_PAD_LINK_OK) {
        qCCritical(log_gst_recording) << "Failed to link tee to recording branch for appsink";
        gst_object_unref(teeSrcPad);
        gst_object_unref(queueSinkPad);
        gst_object_unref(tee);
        return false;
    }

    gst_object_unref(teeSrcPad);
    gst_object_unref(queueSinkPad);
    gst_object_unref(tee);

    if (videoconvert) {
        if (!gst_element_link_many(queue, videoconvert, appsink, NULL)) {
            qCCritical(log_gst_recording) << "Failed to link recording chain (with videoconvert)";
            return false;
        }
    } else {
        if (!gst_element_link(queue, appsink)) {
            qCCritical(log_gst_recording) << "Failed to link recording chain (direct)";
            return false;
        }
    }

    // Sync with parent
    gst_element_sync_state_with_parent(queue);
    if (videoconvert) gst_element_sync_state_with_parent(videoconvert);
    gst_element_sync_state_with_parent(appsink);

    // Now start an FFmpeg process to read raw RGB from appsink and write to file
    m_recordingProcess = new QProcess(this);
    connect(m_recordingProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, &RecordingManager::handleRecordingProcessFinished);
    connect(m_recordingProcess, &QProcess::errorOccurred, this, &RecordingManager::handleRecordingProcessError);

    QStringList args;
    args << "-f" << "rawvideo";
    args << "-pix_fmt" << "rgb24";
    args << "-s" << QString("%1x%2").arg(width).arg(height);
    args << "-r" << QString::number(framerate);
    args << "-i" << "-"; // input from stdin

    if (format.toLower() == "avi") {
        args << "-c:v" << "mjpeg" << "-q:v" << "2";
    } else if (format.toLower() == "mp4") {
        args << "-c:v" << "libx264" << "-preset" << "fast" << "-crf" << "23";
    } else {
        args << "-c:v" << "mjpeg" << "-q:v" << "2";
    }

    args << "-y" << m_recordingOutputPath;

    qCDebug(log_gst_recording) << "Starting FFmpeg with args:" << args.join(' ');
    m_recordingProcess->start("ffmpeg", args);
    if (!m_recordingProcess->waitForStarted(5000)) {
        qCCritical(log_gst_recording) << "Failed to start FFmpeg process:" << m_recordingProcess->errorString();
        delete m_recordingProcess;
        m_recordingProcess = nullptr;
        return false;
    }

    qCInfo(log_gst_recording) << "Initialized appsink-based recording";
    return true;
}

#ifdef HAVE_GSTREAMER
GstFlowReturn RecordingManager::onNewRecordingSample(GstAppSink* sink)
{
    if (!sink) return GST_FLOW_OK;

    GstSample* sample = gst_app_sink_pull_sample(sink);
    if (!sample) return GST_FLOW_OK;

    GstBuffer* buffer = gst_sample_get_buffer(sample);
    if (!buffer) {
        gst_sample_unref(sample);
        return GST_FLOW_OK;
    }

    GstMapInfo map;
    if (gst_buffer_map(buffer, &map, GST_MAP_READ)) {
        if (m_recordingProcess && m_recordingProcess->state() == QProcess::Running) {
            qint64 written = m_recordingProcess->write(reinterpret_cast<const char*>(map.data), static_cast<qint64>(map.size));
            Q_UNUSED(written);
        }
        gst_buffer_unmap(buffer, &map);
    }

    gst_sample_unref(sample);

    // Basic frame counting / throttling if needed
    ++m_recordingFrameNumber;

    return GST_FLOW_OK;
}
#endif

bool RecordingManager::initializeDirectFilesinkRecording(const QString& outputPath, const QString& format)
{
    qCDebug(log_gst_recording) << "RecordingManager::initializeDirectFilesinkRecording to" << outputPath << "format" << format;

    if (!m_mainPipeline) {
        qCCritical(log_gst_recording) << "No pipeline available for direct filesink recording";
        return false;
    }

    GstElement* tee = gst_bin_get_by_name(GST_BIN(m_mainPipeline), "t");
    if (!tee) {
        qCCritical(log_gst_recording) << "Could not find tee element 't' for direct filesink recording";
        return false;
    }

    GstElement* queue = gst_element_factory_make("queue", "recording-queue");
    GstElement* identity = gst_element_factory_make("identity", "recording-identity");
    GstElement* videoconvert = gst_element_factory_make("videoconvert", "recording-convert");
    GstElement* encoder = nullptr;
    GstElement* muxer = nullptr;
    GstElement* filesink = gst_element_factory_make("filesink", "recording-filesink");

    if (!queue || !filesink) {
        qCCritical(log_gst_recording) << "Failed to create basic direct filesink elements";
        if (queue) gst_object_unref(queue);
        if (filesink) gst_object_unref(filesink);
        gst_object_unref(tee);
        return false;
    }

    g_object_set(filesink, "location", outputPath.toUtf8().data(), NULL);

    if (format.toLower() == "avi") {
        encoder = gst_element_factory_make("jpegenc", "recording-encoder");
        muxer = gst_element_factory_make("avimux", "recording-muxer");
        if (!muxer) muxer = gst_element_factory_make("matroskamux", "recording-muxer");
    } else {
        // best-effort - use raw file if encoders not available
    }

    if (!videoconvert) videoconvert = gst_element_factory_make("videoconvertscale", "recording-convert");

    // Add elements based on availability
    if (encoder && muxer && videoconvert) {
        gst_bin_add_many(GST_BIN(m_mainPipeline), identity ? identity : NULL, queue, videoconvert, encoder, muxer, filesink, NULL);
    } else if (encoder && muxer) {
        gst_bin_add_many(GST_BIN(m_mainPipeline), identity ? identity : NULL, queue, encoder, muxer, filesink, NULL);
    } else if (encoder && videoconvert) {
        gst_bin_add_many(GST_BIN(m_mainPipeline), identity ? identity : NULL, queue, videoconvert, encoder, filesink, NULL);
    } else if (encoder) {
        gst_bin_add_many(GST_BIN(m_mainPipeline), identity ? identity : NULL, queue, encoder, filesink, NULL);
    } else if (videoconvert) {
        gst_bin_add_many(GST_BIN(m_mainPipeline), identity ? identity : NULL, queue, videoconvert, filesink, NULL);
    } else {
        gst_bin_add_many(GST_BIN(m_mainPipeline), identity ? identity : NULL, queue, filesink, NULL);
    }

    GstPad* teeSrcPad = gst_element_request_pad_simple(tee, "src_%u");
    GstPad* firstSinkPad = identity ? gst_element_get_static_pad(identity, "sink") : gst_element_get_static_pad(queue, "sink");
    if (!teeSrcPad || !firstSinkPad) {
        qCCritical(log_gst_recording) << "Failed to get pads for direct filesink branch";
        gst_object_unref(tee);
        return false;
    }

    if (gst_pad_link(teeSrcPad, firstSinkPad) != GST_PAD_LINK_OK) {
        qCCritical(log_gst_recording) << "Failed to link tee to direct filesink branch";
        gst_object_unref(teeSrcPad);
        gst_object_unref(firstSinkPad);
        gst_object_unref(tee);
        return false;
    }

    gst_object_unref(teeSrcPad);
    gst_object_unref(firstSinkPad);
    gst_object_unref(tee);

    // Link chain as per availability - simplified
    // This implementation assumes the elements are reasonable and present
    if (encoder && muxer && videoconvert) {
        if (!gst_element_link_many(identity ? identity : queue, queue, videoconvert, encoder, muxer, filesink, NULL)) {
            qCCritical(log_gst_recording) << "Failed to link full direct filesink chain";
            return false;
        }
    }

    // Sync states for all new elements (best-effort)
    if (identity) gst_element_sync_state_with_parent(identity);
    gst_element_sync_state_with_parent(queue);
    if (videoconvert) gst_element_sync_state_with_parent(videoconvert);
    if (encoder) gst_element_sync_state_with_parent(encoder);
    if (muxer) gst_element_sync_state_with_parent(muxer);
    gst_element_sync_state_with_parent(filesink);

    m_recordingOutputPath = outputPath;
    qCInfo(log_gst_recording) << "Direct filesink recording branch created";
    return true;
}

void RecordingManager::handleRecordingProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    Q_UNUSED(exitStatus)
    qCDebug(log_gst_recording) << "RecordingManager: recording process finished with code" << exitCode;
    if (exitCode != 0) {
        emit recordingError(QString("Recording process finished with error code: %1").arg(exitCode));
    }

    if (m_recordingProcess) {
        m_recordingProcess->deleteLater();
        m_recordingProcess = nullptr;
    }

    m_recordingActive = false;
    emit recordingStopped();
}

void RecordingManager::handleRecordingProcessError(QProcess::ProcessError error)
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

    qCCritical(log_gst_recording) << "Recording process error:" << errorString;
    emit recordingError(errorString);

    m_recordingActive = false;
    if (m_recordingProcess) {
        m_recordingProcess->deleteLater();
        m_recordingProcess = nullptr;
    }
}

#endif // HAVE_GSTREAMER

bool RecordingManager::stopRecording()
{
    qCDebug(log_gst_recording) << "RecordingManager::stopRecording";

    if (!m_recordingActive) {
        qCDebug(log_gst_recording) << "No active recording to stop";
        return false;
    }

#ifdef HAVE_GSTREAMER
    removeRecordingBranch();
#else
    // Nothing to do for external process recording in this manager
#endif

    m_recordingActive = false;
    m_recordingPaused = false;
    m_recordingOutputPath.clear();

    qCInfo(log_gst_recording) << "Recording stopped";
    emit recordingStopped();
    return true;
}

void RecordingManager::pauseRecording()
{
    qCDebug(log_gst_recording) << "RecordingManager::pauseRecording";
    if (!m_recordingActive) {
        qCWarning(log_gst_recording) << "No active recording to pause";
        return;
    }

#ifdef HAVE_GSTREAMER
    if (m_recordingValve) {
        g_object_set(m_recordingValve, "drop", TRUE, NULL);
        qCDebug(log_gst_recording) << "Recording valve closed for pause";
    }
#endif

    m_recordingPaused = true;
    m_recordingPausedTime = QDateTime::currentMSecsSinceEpoch();
    emit recordingPaused();
}

void RecordingManager::resumeRecording()
{
    qCDebug(log_gst_recording) << "RecordingManager::resumeRecording";
    if (!m_recordingActive) {
        qCWarning(log_gst_recording) << "No active recording to resume";
        return;
    }

#ifdef HAVE_GSTREAMER
    if (m_recordingValve) {
        g_object_set(m_recordingValve, "drop", FALSE, NULL);
        qCDebug(log_gst_recording) << "Recording valve opened for resume";
    }
#endif

    if (m_recordingPausedTime > 0) {
        m_totalPausedDuration += QDateTime::currentMSecsSinceEpoch() - m_recordingPausedTime;
        m_recordingPausedTime = 0;
    }

    m_recordingPaused = false;
    emit recordingResumed();
}

bool RecordingManager::isRecording() const
{
    return m_recordingActive;
}

QString RecordingManager::getCurrentRecordingPath() const
{
    return m_recordingOutputPath;
}

qint64 RecordingManager::getRecordingDuration() const
{
    if (!m_recordingActive || m_recordingStartTime == 0) return 0;
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    qint64 dur = now - m_recordingStartTime - m_totalPausedDuration;
    if (m_recordingPaused && m_recordingPausedTime > 0) {
        dur -= (now - m_recordingPausedTime);
    }
    return qMax(dur, (qint64)0);
}

void RecordingManager::setRecordingConfig(const QString& codec, const QString& format, int bitrate)
{
    Q_UNUSED(codec)
    Q_UNUSED(format)
    m_recordingVideoBitrate = bitrate;
}

bool RecordingManager::createRecordingBranch(const QString& outputPath, const QString& format, int videoBitrate)
{
#ifdef HAVE_GSTREAMER
    if (!m_mainPipeline) {
        qCCritical(log_gst_recording) << "Main pipeline is null - cannot create recording branch";
        return false;
    }

    m_recordingTee = gst_bin_get_by_name(GST_BIN(m_mainPipeline), "t");
    if (!m_recordingTee) {
        qCCritical(log_gst_recording) << "Could not find tee element 't' in main pipeline";
        emit recordingError("Pipeline lacks tee element for recording");
        return false;
    }

    qCDebug(log_gst_recording) << "Creating recording branch elements";
    m_recordingQueue = gst_element_factory_make("queue", "recording-queue");
    m_recordingFileSink = gst_element_factory_make("filesink", "recording-filesink");

    if (!m_recordingQueue || !m_recordingFileSink) {
        qCCritical(log_gst_recording) << "Failed to create basic recording elements";
        return false;
    }

    // Configure queue
    g_object_set(m_recordingQueue,
                 "max-size-buffers", 5,
                 "max-size-bytes", 0,
                 "max-size-time", (guint64)(200 * GST_MSECOND),
                 "leaky", 2,
                 "flush-on-eos", TRUE,
                 "silent", TRUE,
                 NULL);

    // Only supporting direct JPEG encoding in the static build scenario
    m_recordingEncoder = gst_element_factory_make("jpegenc", "recording-encoder");
    if (!m_recordingEncoder) {
        qCCritical(log_gst_recording) << "Failed to create jpegenc encoder for recording";
        return false;
    }

    QString finalOutputPath = computeAdjustedOutputPath(outputPath, true);
    g_object_set(m_recordingFileSink, "location", finalOutputPath.toLatin1().constData(), NULL);

    // Add elements
    gst_bin_add_many(GST_BIN(m_mainPipeline), m_recordingQueue, m_recordingEncoder, m_recordingFileSink, NULL);

    // Request tee src pad and link
    GstPadTemplate* tee_src_pad_template = gst_element_class_get_pad_template(GST_ELEMENT_GET_CLASS(m_recordingTee), "src_%u");
    m_recordingTeeSrcPad = gst_element_request_pad(m_recordingTee, tee_src_pad_template, NULL, NULL);
    if (!m_recordingTeeSrcPad) {
        qCCritical(log_gst_recording) << "Failed to request tee src pad";
        return false;
    }

    GstPad* queue_sink_pad = gst_element_get_static_pad(m_recordingQueue, "sink");
    if (!queue_sink_pad) {
        qCCritical(log_gst_recording) << "Failed to get sink pad from recording queue";
        return false;
    }

    GstPadLinkReturn link_result = gst_pad_link(m_recordingTeeSrcPad, queue_sink_pad);
    gst_object_unref(queue_sink_pad);
    if (link_result != GST_PAD_LINK_OK) {
        qCCritical(log_gst_recording) << "Failed to link tee to recording queue:" << link_result;
        return false;
    }

    // Link queue -> encoder -> filesink
    if (!gst_element_link_many(m_recordingQueue, m_recordingEncoder, m_recordingFileSink, NULL)) {
        qCCritical(log_gst_recording) << "Failed to link recording elements";
        return false;
    }

    // Transition to READY then sync with parent
    gst_element_set_state(m_recordingQueue, GST_STATE_READY);
    gst_element_set_state(m_recordingEncoder, GST_STATE_READY);
    gst_element_set_state(m_recordingFileSink, GST_STATE_READY);

    QThread::msleep(100);

    // Sync state
    gst_element_sync_state_with_parent(m_recordingQueue);
    gst_element_sync_state_with_parent(m_recordingEncoder);
    gst_element_sync_state_with_parent(m_recordingFileSink);

    // Wait for PAUSED at least (quick check)
    GstState queue_state = GST_STATE_NULL;
    GstState encoder_state = GST_STATE_NULL;
    GstState filesink_state = GST_STATE_NULL;
    GstClockTime timeout = 2000 * GST_MSECOND;
    gst_element_get_state(m_recordingQueue, &queue_state, NULL, timeout);
    gst_element_get_state(m_recordingEncoder, &encoder_state, NULL, timeout);
    gst_element_get_state(m_recordingFileSink, &filesink_state, NULL, timeout);

    if (!(queue_state >= GST_STATE_PAUSED && encoder_state >= GST_STATE_PAUSED && filesink_state >= GST_STATE_PAUSED)) {
        qCWarning(log_gst_recording) << "Recording elements did not reach PAUSED state as expected";
    }

    qCDebug(log_gst_recording) << "Recording branch created and linked successfully";
    return true;
#else
    Q_UNUSED(outputPath)
    Q_UNUSED(format)
    Q_UNUSED(videoBitrate)
    return false;
#endif
}

bool RecordingManager::initializeValveBasedRecording(const QString& format)
{
    Q_UNUSED(format)
    // For compatibility - this manager always prefers createRecordingBranch when valve is present
    return m_mainPipeline ? createRecordingBranch(m_recordingOutputPath, format, m_recordingVideoBitrate) : false;
}

void RecordingManager::removeRecordingBranch()
{
#ifdef HAVE_GSTREAMER
    qCDebug(log_gst_recording) << "Removing recording branch";

    if (!m_mainPipeline) return;

    // Unlink and remove elements if present
    if (m_recordingTeeSrcPad && m_recordingQueue) {
        GstPad* queueSink = gst_element_get_static_pad(m_recordingQueue, "sink");
        if (queueSink) {
            gst_pad_unlink(m_recordingTeeSrcPad, queueSink);
            gst_object_unref(queueSink);
        }
    }

    if (m_recordingTee && m_recordingTeeSrcPad) {
        gst_element_release_request_pad(m_recordingTee, m_recordingTeeSrcPad);
        gst_object_unref(m_recordingTeeSrcPad);
        m_recordingTeeSrcPad = nullptr;
    }

    // Safely set elements to NULL state and remove
    if (m_recordingFileSink) {
        gst_element_set_state(m_recordingFileSink, GST_STATE_NULL);
        gst_bin_remove(GST_BIN(m_mainPipeline), m_recordingFileSink);
        gst_object_unref(m_recordingFileSink);
        m_recordingFileSink = nullptr;
    }

    if (m_recordingEncoder) {
        gst_element_set_state(m_recordingEncoder, GST_STATE_NULL);
        gst_bin_remove(GST_BIN(m_mainPipeline), m_recordingEncoder);
        gst_object_unref(m_recordingEncoder);
        m_recordingEncoder = nullptr;
    }

    if (m_recordingQueue) {
        gst_element_set_state(m_recordingQueue, GST_STATE_NULL);
        gst_bin_remove(GST_BIN(m_mainPipeline), m_recordingQueue);
        gst_object_unref(m_recordingQueue);
        m_recordingQueue = nullptr;
    }

    // Keep tee reference for reuse (if pipeline remains)

    qCDebug(log_gst_recording) << "Recording branch removed";
#endif
}

QString RecordingManager::computeAdjustedOutputPath(const QString& outputPath, bool /*directMJPEG*/)
{
    // For now, keep the same path. Other transformations can be applied here if needed.
    return outputPath;
}
