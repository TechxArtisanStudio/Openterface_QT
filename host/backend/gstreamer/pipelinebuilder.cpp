// SPDX-License-Identifier: GPL-3.0-or-later
#include "pipelinebuilder.h"

#include <QDebug>

using namespace Openterface::GStreamer;

QString PipelineBuilder::buildFlexiblePipeline(const QString& device, const QSize& resolution, int framerate, const QString& videoSink)
{
    // Keep the same structure as old generatePipelineString to preserve recording/tee names
    QString sourceElement = "v4l2src device=%DEVICE% do-timestamp=true";
    QString decoderElement = "image/jpeg,width=%WIDTH%,height=%HEIGHT%,framerate=%FRAMERATE%/1 ! jpegdec";

    QString pipelineTemplate = sourceElement + " ! " +
                      decoderElement + " ! " +
                      "videoconvert ! "
                      "videoscale method=lanczos add-borders=true ! "
                      "video/x-raw,pixel-aspect-ratio=1/1 ! "
                      "identity sync=true ! "
                      "tee name=t allow-not-linked=true "
                      "t. ! queue name=display-queue max-size-buffers=2 leaky=downstream ! " + videoSink + " name=videosink sync=true force-aspect-ratio=true "
                      "t. ! valve name=recording-valve drop=true ! queue name=recording-queue ! identity name=recording-ready";

    QString pipelineStr = pipelineTemplate;
    pipelineStr.replace("%DEVICE%", device);
    pipelineStr.replace("%WIDTH%", QString::number(resolution.width()));
    pipelineStr.replace("%HEIGHT%", QString::number(resolution.height()));
    pipelineStr.replace("%FRAMERATE%", QString::number(framerate));

    return pipelineStr;
}

QString PipelineBuilder::buildVideotestMjpegFallback(const QSize& resolution, int framerate, const QString& videoSink)
{
    QString tmpl(
        "videotestsrc pattern=0 is-live=true ! "
        "video/x-raw,width=%WIDTH%,height=%HEIGHT%,framerate=%FRAMERATE%/1 ! "
        "videoconvert ! "
        "tee name=t ! queue name=display-queue max-size-buffers=5 leaky=downstream ! %SINK% name=videosink sync=false "
        "t. ! valve name=recording-valve drop=true ! queue name=recording-queue max-size-buffers=10 leaky=upstream ! identity name=recording-ready");

    tmpl.replace("%WIDTH%", QString::number(resolution.width()));
    tmpl.replace("%HEIGHT%", QString::number(resolution.height()));
    tmpl.replace("%FRAMERATE%", QString::number(framerate));
    tmpl.replace("%SINK%", videoSink);
    return tmpl;
}

QString PipelineBuilder::buildV4l2JpegFallback(const QString& device, const QSize& resolution, int framerate, const QString& videoSink)
{
    QString tmpl(
        "v4l2src device=%DEVICE% ! "
        "image/jpeg,width=%WIDTH%,height=%HEIGHT%,framerate=%FRAMERATE%/1 ! "
        "jpegdec ! "
        "videoconvert ! "
        "tee name=t ! queue name=display-queue max-size-buffers=5 leaky=downstream ! %SINK% name=videosink sync=false "
        "t. ! valve name=recording-valve drop=true ! queue name=recording-queue max-size-buffers=10 leaky=upstream ! identity name=recording-ready");

    tmpl.replace("%DEVICE%", device);
    tmpl.replace("%WIDTH%", QString::number(resolution.width()));
    tmpl.replace("%HEIGHT%", QString::number(resolution.height()));
    tmpl.replace("%FRAMERATE%", QString::number(framerate));
    tmpl.replace("%SINK%", videoSink);
    return tmpl;
}

QString PipelineBuilder::buildV4l2RawFallback(const QString& device, const QSize& resolution, int framerate, const QString& videoSink)
{
    QString tmpl(
        "v4l2src device=%DEVICE% ! "
        "video/x-raw,width=%WIDTH%,height=%HEIGHT%,framerate=%FRAMERATE%/1 ! "
        "videoconvert ! "
        "tee name=t ! queue name=display-queue max-size-buffers=5 leaky=downstream ! %SINK% name=videosink sync=false "
        "t. ! valve name=recording-valve drop=true ! queue name=recording-queue max-size-buffers=10 leaky=upstream ! identity name=recording-ready");

    tmpl.replace("%DEVICE%", device);
    tmpl.replace("%WIDTH%", QString::number(resolution.width()));
    tmpl.replace("%HEIGHT%", QString::number(resolution.height()));
    tmpl.replace("%FRAMERATE%", QString::number(framerate));
    tmpl.replace("%SINK%", videoSink);
    return tmpl;
}

QString PipelineBuilder::buildVideotestFallback(const QSize& resolution, int framerate, const QString& videoSink)
{
    // Identical to buildVideotestMjpegFallback, but keep as separate method for clarity
    return buildVideotestMjpegFallback(resolution, framerate, videoSink);
}

QString PipelineBuilder::buildMinimalPipeline()
{
    return QString(
        "videotestsrc pattern=0 num-buffers=100 ! "
        "video/x-raw,width=320,height=240,framerate=15/1 ! "
        "fakesink name=videosink");
}

QString PipelineBuilder::buildConservativeTestPipeline(const QString& videoSink)
{
    QString tmpl(
        "videotestsrc pattern=0 is-live=true ! "
        "video/x-raw,width=640,height=480,framerate=15/1 ! "
        "videoconvert ! "
        "tee name=t ! queue name=display-queue max-size-buffers=5 leaky=downstream ! %SINK% name=videosink sync=false "
        "t. ! valve name=recording-valve drop=true ! queue name=recording-queue max-size-buffers=10 leaky=upstream ! identity name=recording-ready");

    tmpl.replace("%SINK%", videoSink);
    return tmpl;
}
