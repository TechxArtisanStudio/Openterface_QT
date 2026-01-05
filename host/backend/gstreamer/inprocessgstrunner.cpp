#include "inprocessgstrunner.h"
#include "gstreamerhelpers.h"
#include <QDebug>
#include <QLoggingCategory>
#include <QtConcurrent>
#include <QFutureWatcher>

Q_LOGGING_CATEGORY(log_gst_runner_inproc, "opf.backend.gstreamer.runner.inprocess")

#ifdef HAVE_GSTREAMER
InProcessGstRunner::InProcessGstRunner(QObject* parent) : QObject(parent)
{
}

InProcessGstRunner::~InProcessGstRunner()
{
}

bool InProcessGstRunner::start(GstElement* pipeline, int timeoutMs, QString* outError)
{
    if (!pipeline) {
        if (outError) *outError = "pipeline is null";
        qCWarning(log_gst_runner_inproc) << "InProcessGstRunner: no pipeline to start";
        return false;
    }

    // Use the centralized helper for state changes
    bool ok = Openterface::GStreamer::GstHelpers::setPipelineStateWithTimeout(pipeline, GST_STATE_READY, 2000, outError);
    if (!ok) {
        Openterface::GStreamer::GstHelpers::parseAndLogGstErrorMessage(gst_element_get_bus(pipeline), "RUNNER->READY");
        return false;
    }

    ok = Openterface::GStreamer::GstHelpers::setPipelineStateWithTimeout(pipeline, GST_STATE_PLAYING, timeoutMs, outError);
    if (!ok) {
        Openterface::GStreamer::GstHelpers::parseAndLogGstErrorMessage(gst_element_get_bus(pipeline), "RUNNER->PLAYING");
        return false;
    }

    qCDebug(log_gst_runner_inproc) << "InProcessGstRunner: pipeline reached PLAYING state";
    return true;
}

bool InProcessGstRunner::play(GstElement* pipeline, int timeoutMs, QString* outError)
{
    if (!pipeline) {
        if (outError) *outError = "pipeline is null";
        qCWarning(log_gst_runner_inproc) << "InProcessGstRunner::play: no pipeline";
        return false;
    }

    bool ok = Openterface::GStreamer::GstHelpers::setPipelineStateWithTimeout(pipeline, GST_STATE_PLAYING, timeoutMs, outError);
    if (!ok) {
        Openterface::GStreamer::GstHelpers::parseAndLogGstErrorMessage(gst_element_get_bus(pipeline), "RUNNER->PLAYING");
        return false;
    }

    qCDebug(log_gst_runner_inproc) << "InProcessGstRunner::play: pipeline reached PLAYING state";
    return true;
}

bool InProcessGstRunner::prepareAsync(GstElement* pipeline, int timeoutMs)
{
    if (!pipeline) {
        qCWarning(log_gst_runner_inproc) << "prepareAsync: pipeline is null";
        return false;
    }

    // Run setPipelineStateWithTimeout in background and emit prepared signal
    using ResultType = QPair<bool, QString>;
    QFuture<ResultType> future = QtConcurrent::run([pipeline, timeoutMs]() -> ResultType {
        QString err;
        bool ok = Openterface::GStreamer::GstHelpers::setPipelineStateWithTimeout(pipeline, GST_STATE_READY, timeoutMs, &err);
        return qMakePair(ok, err);
    });

    auto* watcher = new QFutureWatcher<ResultType>(this);
    connect(watcher, &QFutureWatcher<ResultType>::finished, this, [this, watcher]() {
        QPair<bool, QString> result = watcher->future().result();
        emit prepared(result.first, result.second);
        watcher->deleteLater();
    }, Qt::QueuedConnection);
    watcher->setFuture(future);

    return true;
}

bool InProcessGstRunner::playAsync(GstElement* pipeline, int timeoutMs)
{
    if (!pipeline) {
        qCWarning(log_gst_runner_inproc) << "playAsync: pipeline is null";
        return false;
    }

    using ResultType = QPair<bool, QString>;
    QFuture<ResultType> future = QtConcurrent::run([pipeline, timeoutMs]() -> ResultType {
        QString err;
        bool ok = Openterface::GStreamer::GstHelpers::setPipelineStateWithTimeout(pipeline, GST_STATE_PLAYING, timeoutMs, &err);
        return qMakePair(ok, err);
    });

    auto* watcher = new QFutureWatcher<ResultType>(this);
    connect(watcher, &QFutureWatcher<ResultType>::finished, this, [this, watcher]() {
        QPair<bool, QString> result = watcher->future().result();
        emit started(result.first, result.second);
        watcher->deleteLater();
    }, Qt::QueuedConnection);
    watcher->setFuture(future);

    return true;
}


void InProcessGstRunner::stop(GstElement* pipeline)
{
    if (!pipeline) return;
    QString err;
    bool ok = Openterface::GStreamer::GstHelpers::setPipelineStateWithTimeout(pipeline, GST_STATE_NULL, 2000, &err);
    if (!ok) {
        qCWarning(log_gst_runner_inproc) << "InProcessGstRunner::stop: failed to set pipeline to NULL:" << err;
        // Fallback: try a simple set to NULL without waiting
        gst_element_set_state(pipeline, GST_STATE_NULL);
    }
}
#endif
