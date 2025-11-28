// SPDX-License-Identifier: GPL-3.0-or-later
#include "pipelinefactory.h"
#include "pipelinebuilder.h"

#include <QDebug>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(log_pipeline_factory, "opf.backend.gstreamer.pipelinefactory")

using namespace Openterface::GStreamer;

#ifdef HAVE_GSTREAMER
GstElement* PipelineFactory::createPipeline(const QString &device,
                                            const QSize &resolution,
                                            int framerate,
                                            const QString &videoSink,
                                            QString &outErrorMsg)
{
    // Primary
    QString primary = PipelineBuilder::buildFlexiblePipeline(device, resolution, framerate, videoSink);
    qCDebug(log_pipeline_factory) << "PipelineFactory: trying primary pipeline" << primary;

    GError* error = nullptr;
    GstElement* pipeline = gst_parse_launch(primary.toUtf8().data(), &error);
    if (pipeline && !error) {
        qCDebug(log_pipeline_factory) << "PipelineFactory: primary pipeline created";
        return pipeline;
    }

    if (error) {
        outErrorMsg = error->message;
        qCWarning(log_pipeline_factory) << "Primary pipeline parse failed:" << outErrorMsg;
        g_error_free(error);
        error = nullptr;
    } else {
        outErrorMsg = "Unknown error creating primary pipeline";
    }

    // Probe available elements
    GstElementFactory* v4l2Factory = gst_element_factory_find("v4l2src");
    GstElementFactory* jpegFactory = gst_element_factory_find("jpegdec");
    GstElementFactory* videotestFactory = gst_element_factory_find("videotestsrc");

    QString fallbackPipeline;
    if (!v4l2Factory && videotestFactory && jpegFactory) {
        qCDebug(log_pipeline_factory) << "Choosing videotest + MJPEG fallback";
        fallbackPipeline = PipelineBuilder::buildVideotestMjpegFallback(resolution, framerate, videoSink);
    } else if (v4l2Factory && jpegFactory) {
        qCDebug(log_pipeline_factory) << "Choosing v4l2 + jpeg fallback";
        fallbackPipeline = PipelineBuilder::buildV4l2JpegFallback(device, resolution, framerate, videoSink);
    } else if (v4l2Factory) {
        qCDebug(log_pipeline_factory) << "Choosing v4l2 raw fallback";
        fallbackPipeline = PipelineBuilder::buildV4l2RawFallback(device, resolution, framerate, videoSink);
    } else if (videotestFactory) {
        qCDebug(log_pipeline_factory) << "Choosing videotest fallback";
        fallbackPipeline = PipelineBuilder::buildVideotestFallback(resolution, framerate, videoSink);
    } else {
        qCWarning(log_pipeline_factory) << "No suitable factories available - using minimal pipeline";
        fallbackPipeline = PipelineBuilder::buildMinimalPipeline();
    }

    if (v4l2Factory) gst_object_unref(v4l2Factory);
    if (jpegFactory) gst_object_unref(jpegFactory);
    if (videotestFactory) gst_object_unref(videotestFactory);

    qCDebug(log_pipeline_factory) << "PipelineFactory: trying fallback pipeline" << fallbackPipeline;
    pipeline = gst_parse_launch(fallbackPipeline.toUtf8().data(), &error);
    if (pipeline && !error) {
        qCDebug(log_pipeline_factory) << "PipelineFactory: fallback pipeline created";
        return pipeline;
    }

    if (error) {
        outErrorMsg = error->message;
        qCWarning(log_pipeline_factory) << "Fallback pipeline failed to create:" << outErrorMsg;
        g_error_free(error);
        error = nullptr;
    }

    // Final conservative pipeline
    QString conservative = PipelineBuilder::buildConservativeTestPipeline(videoSink);
    qCDebug(log_pipeline_factory) << "PipelineFactory: trying conservative pipeline" << conservative;
    pipeline = gst_parse_launch(conservative.toUtf8().data(), &error);
    if (pipeline && !error) {
        qCWarning(log_pipeline_factory) << "PipelineFactory: conservative pipeline created";
        return pipeline;
    }

    if (error) {
        outErrorMsg = error->message;
        qCCritical(log_pipeline_factory) << "Conservative pipeline failed to create:" << outErrorMsg;
        g_error_free(error);
    } else {
        outErrorMsg = "Unknown error creating conservative pipeline";
        qCCritical(log_pipeline_factory) << outErrorMsg;
    }

    return nullptr;
}
#else
QString PipelineFactory::buildPrimaryPipelineString(const QString &device,
                                                   const QSize &resolution,
                                                   int framerate,
                                                   const QString &videoSink)
{
    return PipelineBuilder::buildFlexiblePipeline(device, resolution, framerate, videoSink);
}
#endif
