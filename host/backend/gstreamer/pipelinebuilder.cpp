// SPDX-License-Identifier: GPL-3.0-or-later
#include "pipelinebuilder.h"

#include <QDebug>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(log_gstreamer_pipelinebuilder, "opf.backend.gstreamer.pipelinebuilder")

using namespace Openterface::GStreamer;

QString PipelineBuilder::buildFlexiblePipeline(const QString& device, const QSize& resolution, int framerate, const QString& videoSink, const QSize& widgetSize)
{
    // Keep the same structure as old generatePipelineString to preserve recording/tee names
    QString sourceElement = "v4l2src device=%DEVICE% do-timestamp=true";
    QString decoderElement = "image/jpeg,width=%WIDTH%,height=%HEIGHT%,framerate=%FRAMERATE%/1 ! jpegdec";

    // Calculate the output size for videoscale: output EXACTLY the display size with
    // black borders (letterbox/pillarbox) to fill the screen. This is critical on
    // small screens (e.g. 640x480 Pi touchscreens) where the camera resolution
    // (e.g. 1280x720) exceeds the display.
    // We use add-borders=true to fill the full display size with black bars.
    QString scaleCaps = "video/x-raw,pixel-aspect-ratio=1/1";
    if (widgetSize.width() > 0 && widgetSize.height() > 0 && resolution.width() > 0 && resolution.height() > 0) {
        // Output exactly the display size; videoscale with add-borders=true will
        // letterbox/pillarbox to preserve aspect ratio
        scaleCaps = QString("video/x-raw,width=%1,height=%2,pixel-aspect-ratio=1/1").arg(widgetSize.width()).arg(widgetSize.height());
    }

    QString pipelineTemplate = sourceElement + " ! " +
                      decoderElement + " ! " +
                      "videoconvert ! "
                      "videoscale method=lanczos ! "
                      "%SCALE_CAPS% ! " +
                      "identity sync=true ! "
                      "tee name=t allow-not-linked=true "
                      "t. ! queue name=display-queue max-size-buffers=2 leaky=downstream ! " + videoSink + " name=videosink sync=true "
                      "t. ! valve name=recording-valve drop=true ! queue name=recording-queue ! identity name=recording-ready";

    QString pipelineStr = pipelineTemplate;
    pipelineStr.replace("%DEVICE%", device);
    pipelineStr.replace("%WIDTH%", QString::number(resolution.width()));
    pipelineStr.replace("%HEIGHT%", QString::number(resolution.height()));
    pipelineStr.replace("%FRAMERATE%", QString::number(framerate));
    pipelineStr.replace("%SCALE_CAPS%", scaleCaps);

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
