// SPDX-License-Identifier: GPL-3.0-or-later
#include "videooverlaymanager.h"

#include <QDebug>
#include <QGuiApplication>

#ifdef HAVE_GSTREAMER
#include <gst/gst.h>
#include <gst/video/videooverlay.h>
#endif

#include "../../ui/videopane.h"

Q_LOGGING_CATEGORY(log_gstreamer_backend, "opf.backend.gstreamer")

using namespace Openterface::GStreamer;

bool VideoOverlayManager::embedVideoInWidget(void* pipeline, QWidget* widget)
{
#ifdef HAVE_GSTREAMER
    if (!widget || !pipeline) {
        qCWarning(log_gstreamer_backend) << "Cannot embed video: widget or pipeline is null";
        return false;
    }

    GstElement* videoSink = gst_bin_get_by_name(GST_BIN(pipeline), "videosink");
    if (!videoSink) {
        qCWarning(log_gstreamer_backend) << "No video sink element named 'videosink' found in pipeline";
        videoSink = gst_bin_get_by_interface(GST_BIN(pipeline), GST_TYPE_VIDEO_OVERLAY);
        if (!videoSink) {
            qCWarning(log_gstreamer_backend) << "No video overlay interface found in pipeline either";
            return false;
        }
    }

    WId winId = widget->winId();
    if (winId) {
        qCDebug(log_gstreamer_backend) << "Embedding video in widget with window ID:" << winId;
        gst_video_overlay_set_window_handle(GST_VIDEO_OVERLAY(videoSink), winId);
        gst_object_unref(videoSink);
        qCDebug(log_gstreamer_backend) << "Video embedded in widget successfully";
        return true;
    } else {
        qCWarning(log_gstreamer_backend) << "Widget window ID is null, cannot embed video";
        gst_object_unref(videoSink);
        return false;
    }
#else
    Q_UNUSED(pipeline)
    Q_UNUSED(widget)
    qCDebug(log_gstreamer_backend) << "Using autovideosink for video output (no in-process GStreamer)";
    return true;
#endif
}

bool VideoOverlayManager::embedVideoInGraphicsView(void* pipeline, QGraphicsView* view)
{
#ifdef HAVE_GSTREAMER
    if (!view || !pipeline) {
        qCWarning(log_gstreamer_backend) << "Cannot embed video: graphics view or pipeline is null";
        return false;
    }

    GstElement* videoSink = gst_bin_get_by_name(GST_BIN(pipeline), "videosink");
    if (!videoSink) {
        qCWarning(log_gstreamer_backend) << "No video sink element named 'videosink' found in pipeline";
        videoSink = gst_bin_get_by_interface(GST_BIN(pipeline), GST_TYPE_VIDEO_OVERLAY);
        if (!videoSink) {
            qCWarning(log_gstreamer_backend) << "No video overlay interface found in pipeline either";
            return false;
        }
    }

    WId winId = view->winId();
    if (winId) {
        qCDebug(log_gstreamer_backend) << "Embedding video in graphics view with window ID:" << winId;
        gst_video_overlay_set_window_handle(GST_VIDEO_OVERLAY(videoSink), winId);
        gst_object_unref(videoSink);
        qCDebug(log_gstreamer_backend) << "Video embedded in graphics view successfully";
        return true;
    } else {
        qCWarning(log_gstreamer_backend) << "Graphics view window ID is null, cannot embed video";
        gst_object_unref(videoSink);
        return false;
    }
#else
    Q_UNUSED(pipeline)
    Q_UNUSED(view)
    qCDebug(log_gstreamer_backend) << "Using autovideosink for video output (no in-process GStreamer)";
    return true;
#endif
}

bool VideoOverlayManager::embedVideoInVideoPane(void* pipeline, VideoPane* videoPane)
{
#ifdef HAVE_GSTREAMER
    if (!videoPane || !pipeline) {
        qCWarning(log_gstreamer_backend) << "Cannot embed video: VideoPane or pipeline is null";
        return false;
    }

    GstElement* videoSink = gst_bin_get_by_name(GST_BIN(pipeline), "videosink");
    if (!videoSink) {
        qCWarning(log_gstreamer_backend) << "No video sink element named 'videosink' found in pipeline";
        videoSink = gst_bin_get_by_interface(GST_BIN(pipeline), GST_TYPE_VIDEO_OVERLAY);
        if (!videoSink) {
            qCWarning(log_gstreamer_backend) << "No video overlay interface found in pipeline either";
            return false;
        }
    }

    WId winId = videoPane->getVideoOverlayWindowId();
    if (winId) {
        qCDebug(log_gstreamer_backend) << "Embedding video in VideoPane overlay with window ID:" << winId;
        gst_video_overlay_set_window_handle(GST_VIDEO_OVERLAY(videoSink), winId);
        gst_object_unref(videoSink);
        qCDebug(log_gstreamer_backend) << "Video embedded in VideoPane overlay successfully";
        return true;
    } else {
        qCWarning(log_gstreamer_backend) << "VideoPane overlay window ID is null, cannot embed video";
        gst_object_unref(videoSink);
        return false;
    }
#else
    Q_UNUSED(pipeline)
    Q_UNUSED(videoPane)
    qCDebug(log_gstreamer_backend) << "Using autovideosink for video output (no in-process GStreamer)";
    return true;
#endif
}

bool VideoOverlayManager::setupVideoOverlay(void* videoSinkPtr, WId windowId, QWidget* videoWidget, QGraphicsVideoItem* graphicsVideoItem)
{
#ifdef HAVE_GSTREAMER
    if (!videoSinkPtr || windowId == 0) {
        qCWarning(log_gstreamer_backend) << "Invalid parameters for overlay setup: sink=" << videoSinkPtr << "windowId=" << windowId;
        return false;
    }

    GstElement* videoSink = static_cast<GstElement*>(videoSinkPtr);

    // Check if the sink supports video overlay interface
    if (GST_IS_VIDEO_OVERLAY(videoSink)) {
        qCDebug(log_gstreamer_backend) << "Sink supports video overlay - setting up overlay with window ID:" << windowId;

#ifdef Q_OS_LINUX
        // Add X11 error handling to prevent segmentation fault
        XErrorHandler old_handler = nullptr;
        Display* display = nullptr;
        if (QGuiApplication::platformName().contains("xcb")) {
            x11_overlay_error_occurred_local = false;
            display = XOpenDisplay(nullptr);
            if (display) {
                old_handler = XSetErrorHandler(x11_overlay_error_handler_local);
            }
        }
#endif

        // Add error handling for the overlay setup to prevent crashes
        try {
            gst_video_overlay_set_window_handle(GST_VIDEO_OVERLAY(videoSink), windowId);

            // Configure video sink for proper scaling and aspect ratio
            if (g_object_class_find_property(G_OBJECT_GET_CLASS(videoSink), "force-aspect-ratio")) {
                g_object_set(videoSink, "force-aspect-ratio", TRUE, NULL);
                qCDebug(log_gstreamer_backend) << "Enabled force-aspect-ratio on video sink";
            }

            if (g_object_class_find_property(G_OBJECT_GET_CLASS(videoSink), "pixel-aspect-ratio")) {
                g_object_set(videoSink, "pixel-aspect-ratio", "1/1", NULL);
                qCDebug(log_gstreamer_backend) << "Set pixel-aspect-ratio to 1:1 on video sink";
            }

            // Configure render rectangle based on provided targets
            if (videoWidget) {
                QSize widgetSize = videoWidget->size();
                if (widgetSize.width() > 0 && widgetSize.height() > 0) {
                    gst_video_overlay_set_render_rectangle(GST_VIDEO_OVERLAY(videoSink), 0, 0, widgetSize.width(), widgetSize.height());
                    qCDebug(log_gstreamer_backend) << "Set render rectangle to widget size:" << widgetSize;
                }
            } else if (graphicsVideoItem) {
                QRectF itemRect = graphicsVideoItem->boundingRect();
                if (itemRect.width() > 0 && itemRect.height() > 0) {
                    gst_video_overlay_set_render_rectangle(GST_VIDEO_OVERLAY(videoSink), 0, 0, (gint)itemRect.width(), (gint)itemRect.height());
                    qCDebug(log_gstreamer_backend) << "Set render rectangle to video item size:" << itemRect.size();
                }
            }

#ifdef Q_OS_LINUX
            if (display && old_handler) {
                XSync(display, False); // Force any pending X requests to be processed
                XSetErrorHandler(old_handler);
                XCloseDisplay(display);

                if (x11_overlay_error_occurred_local) {
                    qCWarning(log_gstreamer_backend) << "X11 error occurred during overlay setup - continuing without embedding";
                } else {
                    qCDebug(log_gstreamer_backend) << "Video overlay setup completed successfully";
                }
            } else if (!old_handler) {
                qCDebug(log_gstreamer_backend) << "Video overlay setup completed (no X11 error handling)";
            }
#endif

            qCDebug(log_gstreamer_backend) << "Overlay setup completed";
        } catch (...) {
            qCCritical(log_gstreamer_backend) << "Exception during video overlay setup - continuing without embedding";
#ifdef Q_OS_LINUX
            if (display && old_handler) {
                XSetErrorHandler(old_handler);
                XCloseDisplay(display);
            }
#endif
            return false;
        }

        return true;
    }

    // For autovideosink, try to get the actual sink it selected and set up overlay on that
    const GstElementFactory* factory = gst_element_get_factory(videoSink);
    const gchar* sinkName = factory ? gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(factory)) : "unknown";
    const QByteArray sinkNameBA = QByteArray(sinkName);

    if (sinkNameBA.contains("autovideo")) {
        GstElement* actualSink = nullptr;
        if (GST_IS_BIN(videoSink)) {
            GstIterator* iter = gst_bin_iterate_sinks(GST_BIN(videoSink));
            GValue item = G_VALUE_INIT;
            if (gst_iterator_next(iter, &item) == GST_ITERATOR_OK) {
                actualSink = GST_ELEMENT(g_value_get_object(&item));
                if (actualSink && GST_IS_VIDEO_OVERLAY(actualSink)) {
                    qCDebug(log_gstreamer_backend) << "Found overlay-capable sink inside autovideosink";
                    gst_video_overlay_set_window_handle(GST_VIDEO_OVERLAY(actualSink), windowId);
                    gst_video_overlay_set_render_rectangle(GST_VIDEO_OVERLAY(actualSink), 0, 0, -1, -1);
                    gst_video_overlay_expose(GST_VIDEO_OVERLAY(actualSink));
                    g_value_unset(&item);
                    gst_iterator_free(iter);
                    return true;
                }
                g_value_unset(&item);
            }
            gst_iterator_free(iter);
        }
        qCDebug(log_gstreamer_backend) << "autovideosink selected sink doesn't support overlay - video will display in separate window";
        return false;
    }

    qCWarning(log_gstreamer_backend) << "Sink does not support video overlay:" << sinkName;
    return false;
#else
    Q_UNUSED(videoSinkPtr)
    Q_UNUSED(windowId)
    Q_UNUSED(videoWidget)
    Q_UNUSED(graphicsVideoItem)
    qCDebug(log_gstreamer_backend) << "No in-process GStreamer - overlay unavailable";
    return false;
#endif
}

bool VideoOverlayManager::setupVideoOverlayForPipeline(void* pipeline, WId windowId, QWidget* videoWidget, QGraphicsVideoItem* graphicsVideoItem)
{
#ifdef HAVE_GSTREAMER
    if (!pipeline) return false;

    GstElement* videoSink = gst_bin_get_by_name(GST_BIN(pipeline), "videosink");
    if (!videoSink) {
        videoSink = gst_bin_get_by_interface(GST_BIN(pipeline), GST_TYPE_VIDEO_OVERLAY);
    }

    if (videoSink) {
        bool ok = setupVideoOverlay(videoSink, windowId, videoWidget, graphicsVideoItem);
        gst_object_unref(videoSink);
        return ok;
    }
    qCWarning(log_gstreamer_backend) << "No video sink found in pipeline";
    return false;
#else
    Q_UNUSED(pipeline)
    Q_UNUSED(windowId)
    return false;
#endif
}

bool VideoOverlayManager::completePendingOverlaySetup(void* pipeline,
                                                    QWidget* videoWidget,
                                                    QGraphicsVideoItem* graphicsVideoItem,
                                                    VideoPane* videoPane,
                                                    bool &pendingFlag)
{
    qCDebug(log_gstreamer_backend) << "VideoOverlayManager: Completing pending overlay setup...";

    if (!pendingFlag || !pipeline) {
        qCDebug(log_gstreamer_backend) << "No pending setup or no pipeline";
        return false;
    }

    const QString platform = QGuiApplication::platformName();
    const bool isXcb = platform.contains("xcb", Qt::CaseInsensitive);
    const bool hasXDisplay = !qgetenv("DISPLAY").isEmpty();
    if (!isXcb || !hasXDisplay) {
        qCWarning(log_gstreamer_backend) << "Skipping deferred overlay setup: platform is" << platform << "(DISPLAY set:" << hasXDisplay << ")";
        pendingFlag = false;
        return false;
    }

    WId windowId = 0;

    if (videoPane) {
        windowId = videoPane->getVideoOverlayWindowId();
        qCDebug(log_gstreamer_backend) << "Completing overlay setup with VideoPane window ID:" << windowId;
    } else if (graphicsVideoItem) {
        if (graphicsVideoItem->scene()) {
            QList<QGraphicsView*> views = graphicsVideoItem->scene()->views();
            if (!views.isEmpty()) {
                QGraphicsView* view = views.first();
                if (auto pane = qobject_cast<VideoPane*>(view)) {
                    if (pane->isDirectGStreamerModeEnabled() && pane->getOverlayWidget()) {
                        windowId = pane->getVideoOverlayWindowId();
                        qCDebug(log_gstreamer_backend) << "Completing overlay setup with VideoPane overlay widget window ID:" << windowId;
                    } else {
                        qCDebug(log_gstreamer_backend) << "VideoPane overlay widget still not ready";
                        return false;
                    }
                } else {
                    windowId = view->winId();
                    qCDebug(log_gstreamer_backend) << "Completing overlay setup with graphics view window ID:" << windowId;
                }
            } else {
                qCWarning(log_gstreamer_backend) << "Graphics video item has no associated view";
                return false;
            }
        } else {
            qCWarning(log_gstreamer_backend) << "Graphics video item has no scene";
            return false;
        }
    }

    if (windowId && windowId != 0) {
        GstElement* videoSink = gst_bin_get_by_name(GST_BIN(pipeline), "videosink");
        if (!videoSink) {
            videoSink = gst_bin_get_by_interface(GST_BIN(pipeline), GST_TYPE_VIDEO_OVERLAY);
            if (videoSink) qCDebug(log_gstreamer_backend) << "Deferred path: found sink by overlay interface";
        }

        if (videoSink) {
            const GstElementFactory* factory = gst_element_get_factory(videoSink);
            const gchar* sinkName = factory ? gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(factory)) : "unknown";
            if (!sinkName) sinkName = "unknown";

            const QByteArray sinkNameBA = QByteArray(sinkName);
            if (sinkNameBA.contains("qt6videosink")) {
                QWidget* targetWidget = nullptr;
                if (videoPane && videoPane->getOverlayWidget()) {
                    targetWidget = videoPane->getOverlayWidget();
                } else if (videoWidget) {
                    targetWidget = videoWidget;
                }
                if (targetWidget) {
                    qCDebug(log_gstreamer_backend) << "Deferred: binding qt6videosink to QWidget" << targetWidget;
                    g_object_set(G_OBJECT(videoSink), "widget", (gpointer)targetWidget, nullptr);
                    gst_object_unref(videoSink);
                    pendingFlag = false;
                    qCDebug(log_gstreamer_backend) << "Deferred qt6videosink binding completed";
                    return true;
                } else {
                    qCWarning(log_gstreamer_backend) << "Deferred: no target QWidget available to bind qt6videosink";
                }
            }

            const bool supportsOverlay = GST_IS_VIDEO_OVERLAY(videoSink);
            const bool looksLikeXSink = sinkNameBA.contains("xvimage") || sinkNameBA.contains("ximage");

            if (!supportsOverlay) {
                qCWarning(log_gstreamer_backend) << "Deferred overlay skipped: sink does not support overlay interface (" << sinkName << ")";
                gst_object_unref(videoSink);
                pendingFlag = false;
                return false;
            }

            if (!looksLikeXSink) {
                qCWarning(log_gstreamer_backend) << "Deferred overlay skipped: sink is not an X sink (" << sinkName << ") on platform" << QGuiApplication::platformName();
                gst_object_unref(videoSink);
                pendingFlag = false;
                return false;
            }

            qCDebug(log_gstreamer_backend) << "Setting up deferred video overlay with window ID:" << windowId << "using sink" << sinkName;
            gst_video_overlay_set_window_handle(GST_VIDEO_OVERLAY(videoSink), windowId);
            gst_object_unref(videoSink);
            pendingFlag = false;
            qCDebug(log_gstreamer_backend) << "Deferred overlay setup completed successfully";
            return true;
        } else {
            qCWarning(log_gstreamer_backend) << "Could not find video sink for deferred overlay setup";
        }
    } else {
        qCWarning(log_gstreamer_backend) << "Still no valid window ID available for deferred overlay setup";
    }

    return false;
}
