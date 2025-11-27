// SPDX-License-Identifier: GPL-3.0-or-later
#include "sinkselector.h"

#include <QByteArray>
#include <QDebug>
#include <QLoggingCategory>
#include <QProcessEnvironment>

#ifdef HAVE_GSTREAMER
#include <gst/gst.h>
#endif

Q_LOGGING_CATEGORY(log_gst_sink_selector, "opf.backend.gstreamer.sinkselector")

using namespace Openterface::GStreamer;

QString SinkSelector::selectSink(const QString &platform)
{
    // Read environment override first
    const QByteArray envOverride = qgetenv("OPENTERFACE_GST_SINK");
    if (!envOverride.isEmpty()) {
        const QString override = QString::fromLatin1(envOverride);
        qCDebug(log_gst_sink_selector) << "OPENTERFACE_GST_SINK override set to:" << override;

#ifdef HAVE_GSTREAMER
        // Validate that the element exists in this GStreamer runtime
        GstElementFactory* f = gst_element_factory_find(override.toUtf8().constData());
        if (f) {
            gst_object_unref(f);
            qCDebug(log_gst_sink_selector) << "Override sink validated in GStreamer build:" << override;
            return override;
        }
        qCWarning(log_gst_sink_selector) << "OPENTERFACE_GST_SINK refers to an element not present in this GStreamer build:" << override;
#else
        qCWarning(log_gst_sink_selector) << "OPENTERFACE_GST_SINK is set but GStreamer support is unavailable, ignoring:" << override;
#endif
    }

    // Probe for preferred sinks in order of preference
    const char* preferred[] = {"xvimagesink", "ximagesink", "autovideosink", "qtsink", nullptr};

#ifdef HAVE_GSTREAMER
    for (const char** trySink = preferred; *trySink; ++trySink) {
        GstElementFactory* factory = gst_element_factory_find(*trySink);
        if (factory) {
            const QString found = QString::fromUtf8(*trySink);
            qCDebug(log_gst_sink_selector) << "Selected available sink:" << found;
            gst_object_unref(factory);
            return found;
        }
    }
#else
    Q_UNUSED(preferred);
#endif

    // Last-resort fallback
    qCWarning(log_gst_sink_selector) << "No preferred sinks found or GStreamer not available - defaulting to autovideosink";
    return QStringLiteral("autovideosink");
}
