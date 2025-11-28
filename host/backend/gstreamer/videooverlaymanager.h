// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef OPENTERFACE_GSTREAMER_VIDEOOVERLAYMANAGER_H
#define OPENTERFACE_GSTREAMER_VIDEOOVERLAYMANAGER_H

#include <QWidget>
#include <QGraphicsVideoItem>
#include <QGraphicsView>

namespace Openterface {
namespace GStreamer {

class VideoPane; // forward declare to avoid header cycle

class VideoOverlayManager
{
public:
    // Embed video into a native widget using the pipeline's videosink
    static bool embedVideoInWidget(void* pipeline, QWidget* widget);

    // Embed into a graphics view (QGraphicsView)
    static bool embedVideoInGraphicsView(void* pipeline, QGraphicsView* view);

    // Embed into a VideoPane overlay (VideoPane provides getVideoOverlayWindowId())
    static bool embedVideoInVideoPane(void* pipeline, VideoPane* videoPane);

    // Setup overlay for a specific videoSink element and windowId
    // Accept optional targets to set render rectangle / widget bindings
    static bool setupVideoOverlay(void* videoSink, WId windowId, QWidget* videoWidget = nullptr, QGraphicsVideoItem* graphicsVideoItem = nullptr);

    // Setup overlay for current pipeline given a windowId
    // Accept optional targets so the manager can configure render rectangle or widget binding
    static bool setupVideoOverlayForPipeline(void* pipeline, WId windowId, QWidget* videoWidget = nullptr, QGraphicsVideoItem* graphicsVideoItem = nullptr);

    // Attempt to complete pending overlay setup using the available targets
    // Returns true when overlay completed (sets pending=false), false otherwise
    static bool completePendingOverlaySetup(void* pipeline,
                                            QWidget* videoWidget,
                                            QGraphicsVideoItem* graphicsVideoItem,
                                            VideoPane* videoPane,
                                            bool &pendingFlag);
};

} // namespace GStreamer
} // namespace Openterface

#endif // OPENTERFACE_GSTREAMER_VIDEOOVERLAYMANAGER_H
