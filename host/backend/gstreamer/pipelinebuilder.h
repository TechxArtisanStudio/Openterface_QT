// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef OPENTERFACE_GSTREAMER_PIPELINEBUILDER_H
#define OPENTERFACE_GSTREAMER_PIPELINEBUILDER_H

#include <QString>
#include <QSize>

namespace Openterface {
namespace GStreamer {

class PipelineBuilder
{
public:
    // Flexible pipeline - recording-enabled template using v4l2src + jpegdec and scaling
    static QString buildFlexiblePipeline(const QString& device, const QSize& resolution, int framerate, const QString& videoSink);

    // Fallback pipelines (videotestsrc, v4l2 + jpeg, v4l2 raw)
    static QString buildVideotestMjpegFallback(const QSize& resolution, int framerate, const QString& videoSink);
    static QString buildV4l2JpegFallback(const QString& device, const QSize& resolution, int framerate, const QString& videoSink);
    static QString buildV4l2RawFallback(const QString& device, const QSize& resolution, int framerate, const QString& videoSink);
    static QString buildVideotestFallback(const QSize& resolution, int framerate, const QString& videoSink);

    // Minimal test pipeline (last resort)
    static QString buildMinimalPipeline();

    // Final conservative fallback pipeline
    static QString buildConservativeTestPipeline(const QString& videoSink);
};

} // namespace GStreamer
} // namespace Openterface

#endif // OPENTERFACE_GSTREAMER_PIPELINEBUILDER_H
