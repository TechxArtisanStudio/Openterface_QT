/*
 * In-process GStreamer runner helper
 */
#ifndef INPROCESSGSTRUNNER_H
#define INPROCESSGSTRUNNER_H

#include <QObject>

#ifdef HAVE_GSTREAMER
#include <gst/gst.h>
#endif

class InProcessGstRunner : public QObject
{
    Q_OBJECT

public:
    explicit InProcessGstRunner(QObject* parent = nullptr);
    ~InProcessGstRunner();

#ifdef HAVE_GSTREAMER
    // Attempt to start the provided pipeline. Returns true on success.
    bool start(GstElement* pipeline, int timeoutMs = 5000, QString* outError = nullptr);
    // Only transition to PLAYING state (assumes READY already set)
    bool play(GstElement* pipeline, int timeoutMs = 5000, QString* outError = nullptr);
    // Asynchronous helpers (non-blocking): prepare transitions pipeline to READY
    bool prepareAsync(GstElement* pipeline, int timeoutMs = 2000);
    // Asynchronous play transition
    bool playAsync(GstElement* pipeline, int timeoutMs = 5000);
    // Stop the pipeline
    void stop(GstElement* pipeline);
#ifdef HAVE_GSTREAMER
signals:
    void prepared(bool success, const QString& error);
    void started(bool success, const QString& error);
#endif
#endif
};

#endif // INPROCESSGSTRUNNER_H
