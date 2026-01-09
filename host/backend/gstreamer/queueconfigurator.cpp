// SPDX-License-Identifier: GPL-3.0-or-later
#include "queueconfigurator.h"

#include <QDebug>
#include "logging.h"

#ifdef HAVE_GSTREAMER
#include <gst/gst.h>
#endif

Q_LOGGING_CATEGORY(log_gstreamer_queueconfigurator, "opf.backend.queueconfigurator")

using namespace Openterface::GStreamer;

void QueueConfigurator::configureDisplayQueue(void* pipeline)
{
#ifdef HAVE_GSTREAMER
    GstElement* bin = static_cast<GstElement*>(pipeline);
    if (!bin) return;

    GstElement* displayQueue = gst_bin_get_by_name(GST_BIN(bin), "display-queue");
    if (displayQueue) {
        // Aggressive buffering for display with low latency
        g_object_set(displayQueue,
                     "max-size-buffers", 5,
                     "max-size-time", G_GUINT64_CONSTANT(100000000), // 100ms
                     "leaky", 2, // GST_QUEUE_LEAK_DOWNSTREAM
                     NULL);
        qCDebug(log_gstreamer_queueconfigurator) << "✓ Configured display queue with higher priority for qtsink";
        gst_object_unref(displayQueue);
    } else {
        qCDebug(log_gstreamer_queueconfigurator) << "Display queue element not found (no named display-queue)";
    }
#else
    Q_UNUSED(pipeline);
#endif
}

void QueueConfigurator::configureRecordingQueue(void* pipeline)
{
#ifdef HAVE_GSTREAMER
    GstElement* bin = static_cast<GstElement*>(pipeline);
    if (!bin) return;

    GstElement* recordingQueue = gst_bin_get_by_name(GST_BIN(bin), "recording-queue");
    if (recordingQueue) {
        // Conservative buffering for recording with lower priority
        g_object_set(recordingQueue,
                     "max-size-buffers", 10,
                     "max-size-time", G_GUINT64_CONSTANT(500000000), // 500ms
                     "leaky", 1, // GST_QUEUE_LEAK_UPSTREAM
                     NULL);
        qCDebug(log_gstreamer_queueconfigurator) << "✓ Configured recording queue with lower priority relative to display";
        gst_object_unref(recordingQueue);
    }
#else
    Q_UNUSED(pipeline);
#endif
}

void QueueConfigurator::configureQueues(void* pipeline)
{
    configureDisplayQueue(pipeline);
    configureRecordingQueue(pipeline);
}
