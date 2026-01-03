// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef OPENTERFACE_GSTREAMER_PIPELINEFACTORY_H
#define OPENTERFACE_GSTREAMER_PIPELINEFACTORY_H

#include <QString>
#include <QSize>

#ifdef HAVE_GSTREAMER
#include <gst/gst.h>
#endif

namespace Openterface {
namespace GStreamer {

class PipelineFactory
{
public:
#ifdef HAVE_GSTREAMER
    // Creates an in-process Gst pipeline using gst_parse_launch.
    // Tries the flexible primary pipeline first then reasonable fallbacks.
    // Returns a GstElement* pipeline or nullptr on failure and sets outErrorMsg.
    static GstElement* createPipeline(const QString &device,
                                      const QSize &resolution,
                                      int framerate,
                                      const QString &videoSink,
                                      QString &outErrorMsg);
#else
    // When GStreamer isn't available, return the primary pipeline string
    // so callers can run it with gst-launch or similar.
    static QString buildPrimaryPipelineString(const QString &device,
                                              const QSize &resolution,
                                              int framerate,
                                              const QString &videoSink);
#endif
};

} // namespace GStreamer
} // namespace Openterface

#endif // OPENTERFACE_GSTREAMER_PIPELINEFACTORY_H
