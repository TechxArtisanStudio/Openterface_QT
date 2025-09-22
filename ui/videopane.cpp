/*
* ========================================================================== *
*                                                                            *
*    This file is part of the Openterface Mini KVM App QT version            *
*                                                                            *
*    Copyright (C) 2024   <info@openterface.com>                             *
*                                                                            *
*    This program is free software: you can redistribute it and/or modify    *
*    it under the terms of the GNU General Public License as published by    *
*    the Free Software Foundation version 3.                                 *
*                                                                            *
*    This program is distributed in the hope that it will be useful, but     *
*    WITHOUT ANY WARRANTY; without even the implied warranty of              *
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU        *
*    General Public License for more details.                                *
*                                                                            *
*    You should have received a copy of the GNU General Public License       *
*    along with this program. If not, see <http://www.gnu.org/licenses/>.    *
*                                                                            *
* ========================================================================== *
*/

#include "videopane.h"
#include "host/HostManager.h"
#include "inputhandler.h"
#include "../global.h"

#include <QtWidgets>
#include <QtMultimedia>
#include <QtMultimediaWidgets>
#include <QDebug>
#include <QTimer>

Q_LOGGING_CATEGORY(log_ui_video, "opf.ui.video")

VideoPane::VideoPane(QWidget *parent) : QGraphicsView(parent), 
    escTimer(new QTimer(this)), 
    m_inputHandler(new InputHandler(this, this)), 
    m_isCameraSwitching(false),
    m_scene(new QGraphicsScene(this)),
    m_videoItem(nullptr),
    m_pixmapItem(nullptr),
    m_aspectRatioMode(Qt::KeepAspectRatio),
    m_scaleFactor(1.0),
    m_maintainAspectRatio(true),
    m_directGStreamerMode(false),
    m_overlayWidget(nullptr),
    m_directFFmpegMode(false)
{
    qDebug(log_ui_video) << "VideoPane init...";
    
    // Set up the graphics scene
    setupScene();
    
    // Create and initialize the video item
    m_videoItem = new QGraphicsVideoItem();
    m_scene->addItem(m_videoItem);
    m_videoItem->setZValue(0); // Below pixmap item
    
    // Configure the graphics view
    setScene(m_scene);
    setDragMode(QGraphicsView::NoDrag);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setRenderHint(QPainter::Antialiasing, true);
    setRenderHint(QPainter::SmoothPixmapTransform, true);
    setOptimizationFlag(QGraphicsView::DontAdjustForAntialiasing, true);
    setViewportUpdateMode(QGraphicsView::SmartViewportUpdate);

    this->setMouseTracking(true);
    this->installEventFilter(m_inputHandler);
    this->setFocusPolicy(Qt::StrongFocus);
    relativeModeEnable = false;
    // Set up the timer
    connect(escTimer, &QTimer::timeout, this, &VideoPane::showHostMouse);
}

VideoPane::~VideoPane()
{
    qDebug() << "VideoPane destructor started";
    
    // 1. FIRST: Remove event filter and stop input handler to prevent event processing
    if (m_inputHandler) {
        removeEventFilter(m_inputHandler);
        m_inputHandler->deleteLater(); // Use deleteLater for safer cleanup
        m_inputHandler = nullptr;
    }
    
    // 2. Stop timers
    if (escTimer) {
        escTimer->stop();
        escTimer->deleteLater();
        escTimer = nullptr;
    }
    
    // 3. Disconnect all signals to prevent callbacks during destruction
    disconnect();
    
    // 4. Clean up graphics items in correct order
    if (m_scene) {
        // Remove items from scene before deleting them
        if (m_videoItem) {
            m_scene->removeItem(m_videoItem);
            m_videoItem->deleteLater(); // Use deleteLater for graphics items
            m_videoItem = nullptr;
        }
        if (m_pixmapItem) {
            m_scene->removeItem(m_pixmapItem);
            m_pixmapItem = nullptr;
        }
        
        // Clear and delete scene
        m_scene->clear();
        m_scene->deleteLater();
        m_scene = nullptr;
    }

    qDebug() << "VideoPane destructor completed";
}

/*
    * This function is called when the focus is on the video pane and the user presses the Tab key.
    * This function is overridden to prevent the focus from moving to the next widget.
*/
bool VideoPane::focusNextPrevChild(bool next) {
    return false;
}

void VideoPane::moveMouseToCenter()
{
    // Temporarily disable the mouse event handling
    this->relativeModeEnable = false;

    // Move the mouse to the center of the window
    QCursor::setPos(this->mapToGlobal(QPoint(this->width() / 2, this->height() / 2)));
    lastX= this->width() / 2;
    lastY= this->height() / 2;

    this->relativeModeEnable = true;
}

void VideoPane::showHostMouse() {
    QCursor arrowCursor(Qt::ArrowCursor);
    this->setCursor(arrowCursor);
    this->relativeModeEnable = false;
}

void VideoPane::hideHostMouse() {
    // Hide the cursor
    QCursor blankCursor(Qt::BlankCursor);
    this->setCursor(blankCursor);
    this->relativeModeEnable = true;
}

void VideoPane::startEscTimer()
{
    escTimer->start(500); // 0.5 seconds
}

void VideoPane::stopEscTimer()
{
    escTimer->stop();
}

void VideoPane::onCameraDeviceSwitching(const QString& fromDevice, const QString& toDevice)
{
    // qCDebug(log_ui_video) << "VideoPane: Camera switching from" << fromDevice << "to" << toDevice;
    
    // Capture the current frame before switching
    captureCurrentFrame();
    
    // Set switching mode to display the last frame
    m_isCameraSwitching = true;
    
    // Force a repaint to show the captured frame
    update();
}

void VideoPane::onCameraDeviceSwitchComplete(const QString& device)
{
    qCDebug(log_ui_video) << "VideoPane: Camera switch complete to" << device;
    
    // Clear switching mode to resume normal video display
    m_isCameraSwitching = false;
    
    // Clear the captured frame
    m_lastFrame = QPixmap();
    
    // Handle visibility based on current mode
    if (m_directFFmpegMode) {
        // In FFmpeg mode, keep pixmap item visible and hide Qt video item
        if (m_videoItem) {
            m_videoItem->setVisible(false);
            qDebug() << "VideoPane: Video item hidden - FFmpeg mode active";
        }
        
        if (m_pixmapItem) {
            m_pixmapItem->setVisible(true);
            qDebug() << "VideoPane: Pixmap item kept visible for FFmpeg frames";
        }
    } else {
        // In normal Qt mode, show video item and hide pixmap item
        if (m_videoItem) {
            m_videoItem->setVisible(true);
            qDebug() << "VideoPane: Video item made visible for new camera feed";
        }
        
        if (m_pixmapItem) {
            m_pixmapItem->setVisible(false);
            qDebug() << "VideoPane: Pixmap item hidden to show live video";
        }
    }
    
    // Force a repaint to resume normal video display
    update();
    
    qDebug() << "VideoPane: Ready to display new camera feed";
}

void VideoPane::captureCurrentFrame()
{
    // Try multiple methods to capture the current frame
    if (this->isVisible() && this->size().isValid()) {
        // Method 1: Grab the graphics view content
        m_lastFrame = this->grab();
        
        // Method 2: If grab() failed or returned null, try to render the scene
        if (m_lastFrame.isNull() || m_lastFrame.size().isEmpty()) {
            m_lastFrame = QPixmap(this->size());
            m_lastFrame.fill(Qt::black); // Fill with black as fallback
            QPainter painter(&m_lastFrame);
            if (m_scene) {
                m_scene->render(&painter, this->rect(), this->rect());
            } else {
                this->render(&painter);
            }
        }
        
        qCDebug(log_ui_video) << "VideoPane: Captured frame" << m_lastFrame.size() << "for preservation during camera switch";
    } else {
        // Create a black fallback frame
        m_lastFrame = QPixmap(this->size().isEmpty() ? QSize(640, 480) : this->size());
        m_lastFrame.fill(Qt::black);
        qCDebug(log_ui_video) << "VideoPane: Created fallback black frame for camera switch";
    }
}

void VideoPane::paintEvent(QPaintEvent *event)
{
    // PERFORMANCE: Reduce redundant visibility checks and updates
    if (m_isCameraSwitching && !m_lastFrame.isNull()) {
        // During camera switching, show preserved frame using pixmap item
        if (!m_pixmapItem) {
            m_pixmapItem = m_scene->addPixmap(m_lastFrame);
            m_pixmapItem->setZValue(1); // Above video item
        } else {
            m_pixmapItem->setPixmap(m_lastFrame);
            m_pixmapItem->setVisible(true);
        }
        
        if (m_videoItem) {
            m_videoItem->setVisible(false);
        }
        
        qCDebug(log_ui_video) << "VideoPane: Displaying preserved frame during camera switch";
    } else if (m_directFFmpegMode) {
        // In FFmpeg mode, ensure pixmap item is visible and video item is hidden
        // PERFORMANCE: Only change visibility if actually needed
        if (m_pixmapItem && !m_pixmapItem->isVisible()) {
            m_pixmapItem->setVisible(true);
        }
        if (m_videoItem && m_videoItem->isVisible()) {
            m_videoItem->setVisible(false);
        }
        // PERFORMANCE: Remove excessive debug logging from paintEvent
        // qCDebug(log_ui_video) << "VideoPane: paintEvent - FFmpeg mode, pixmap visible:" 
        //                       << (m_pixmapItem ? m_pixmapItem->isVisible() : false);
    } else {
        // Normal video display mode
        // PERFORMANCE: Only change visibility if actually needed
        if (m_pixmapItem && m_pixmapItem->isVisible()) {
            m_pixmapItem->setVisible(false);
        }
        if (m_videoItem && !m_videoItem->isVisible()) {
            m_videoItem->setVisible(true);
        }
    }
    
    // Call the base class paintEvent
    QGraphicsView::paintEvent(event);
}

// QVideoWidget compatibility methods
void VideoPane::setAspectRatioMode(Qt::AspectRatioMode mode)
{
    m_aspectRatioMode = mode;
    m_maintainAspectRatio = (mode != Qt::IgnoreAspectRatio);
    updateVideoItemTransform();
}

Qt::AspectRatioMode VideoPane::aspectRatioMode() const
{
    return m_aspectRatioMode;
}

// QGraphicsView enhancement methods
void VideoPane::setVideoItem(QGraphicsVideoItem* videoItem)
{
    if (m_videoItem) {
        m_scene->removeItem(m_videoItem);
    }
    
    m_videoItem = videoItem;
    if (m_videoItem) {
        m_scene->addItem(m_videoItem);
        m_videoItem->setZValue(0); // Below pixmap item
        updateVideoItemTransform();
    }
}

QGraphicsVideoItem* VideoPane::videoItem() const
{
    return m_videoItem;
}

void VideoPane::resetZoom()
{
    m_scaleFactor = 1.0;
    resetTransform();
    updateVideoItemTransform();
}

void VideoPane::zoomIn(double factor)
{
    m_scaleFactor *= factor;
    scale(factor, factor);
}

void VideoPane::zoomOut(double factor)
{
    m_scaleFactor *= factor;
    scale(factor, factor);
}

void VideoPane::fitToWindow()
{
    if (m_videoItem) {
        // Reset any existing transformations
        resetTransform();
        m_scaleFactor = 1.0;
        
        // Update the video item transform to fit the current view
        updateVideoItemTransform();
    }
}

void VideoPane::actualSize()
{
    resetZoom();
    if (m_videoItem) {
        centerVideoItem();
    }
}


void VideoPane::resizeEvent(QResizeEvent *event)
{
    QGraphicsView::resizeEvent(event);
    
    // Update the scene rect to match the viewport size
    if (m_scene) {
        m_scene->setSceneRect(viewport()->rect());
    }
    
    updateVideoItemTransform();
    
    // Update overlay widget size for direct GStreamer mode
    if (m_directGStreamerMode && m_overlayWidget) {
        m_overlayWidget->resize(size());
        qDebug() << "VideoPane: Resized GStreamer overlay widget to:" << size();
    }
}

// Helper methods
void VideoPane::updateVideoItemTransform()
{
    // Handle both Qt video item and FFmpeg pixmap item
    QGraphicsItem* targetItem = nullptr;
    QRectF itemRect;
    
    if (m_directFFmpegMode && m_pixmapItem) {
        targetItem = m_pixmapItem;
        itemRect = m_pixmapItem->boundingRect();
        // qDebug(log_ui_video) << "VideoPane: Updating FFmpeg pixmap transform";
    } else if (m_videoItem) {
        targetItem = m_videoItem;
        itemRect = m_videoItem->boundingRect();
        // qDebug() << "VideoPane: Updating Qt video item transform";
    }
    
    if (!targetItem) return;
    
    QRectF viewRect = viewport()->rect();
    
    if (itemRect.isEmpty() || viewRect.isEmpty()) return;
    // qDebug() << "Updating item transform with itemRect:" << itemRect << "viewRect:" << viewRect;
    
    // Reset transform and position first
    targetItem->setTransform(QTransform());
    targetItem->setPos(0, 0);
    
    // Normalize the item rectangle to start from (0,0) to handle any offset in boundingRect
    QRectF normalizedRect(0, 0, itemRect.width(), itemRect.height());
    QPointF itemOffset = itemRect.topLeft(); // Store the original offset
    
    if (m_maintainAspectRatio) {
        // Calculate scale to fit while maintaining aspect ratio
        double scaleX = viewRect.width() / normalizedRect.width();
        double scaleY = viewRect.height() / normalizedRect.height();
        double scale = qMin(scaleX, scaleY);
        
        // Apply transformation
        QTransform transform;
        transform.scale(scale, scale);
        targetItem->setTransform(transform);
        
        // Center the item after scaling, accounting for the original offset
        QRectF scaledRect = QRectF(0, 0, normalizedRect.width() * scale, normalizedRect.height() * scale);
        double x = (viewRect.width() - scaledRect.width()) / 2.0 - (itemOffset.x() * scale);
        double y = (viewRect.height() - scaledRect.height()) / 2.0 - (itemOffset.y() * scale);
        targetItem->setPos(x, y);
        // qDebug() << "Item transformed with scale:" << scale << "at position:" << QPointF(x, y) << "offset:" << itemOffset;
    } else {
        // Stretch to fill (ignore aspect ratio)
        QTransform transform;
        transform.scale(viewRect.width() / normalizedRect.width(), 
                    viewRect.height() / normalizedRect.height());
        targetItem->setTransform(transform);
        // Account for the original offset when stretching
        targetItem->setPos(-itemOffset.x(), -itemOffset.y());
    }
}

void VideoPane::centerVideoItem()
{
    // Handle both Qt video item and FFmpeg pixmap item
    QGraphicsItem* targetItem = nullptr;
    QRectF itemRect;
    
    if (m_directFFmpegMode && m_pixmapItem) {
        targetItem = m_pixmapItem;
        itemRect = m_pixmapItem->boundingRect();
        qDebug(log_ui_video) << "VideoPane: Centering FFmpeg pixmap item";
    } else if (m_videoItem) {
        targetItem = m_videoItem;
        itemRect = m_videoItem->boundingRect();
        qDebug() << "VideoPane: Centering Qt video item";
    }
    
    if (!targetItem) return;
    
    QRectF viewRect = viewport()->rect();
    
    // Normalize the item rectangle and get the original offset
    QRectF normalizedRect(0, 0, itemRect.width(), itemRect.height());
    QPointF itemOffset = itemRect.topLeft();
    
    // Get the current transform to calculate the scaled size
    QTransform transform = targetItem->transform();
    QRectF scaledRect = transform.mapRect(normalizedRect);
    
    // Center the item accounting for the original offset
    double x = (viewRect.width() - scaledRect.width()) / 2.0 - (itemOffset.x() * transform.m11());
    double y = (viewRect.height() - scaledRect.height()) / 2.0 - (itemOffset.y() * transform.m22());
    
    targetItem->setPos(x, y);
}

void VideoPane::setupScene()
{
    if (!m_scene) {
        m_scene = new QGraphicsScene(this);
    }
    
    m_scene->setBackgroundBrush(QBrush(Qt::black));
    
    // Set initial scene size to match viewport
    m_scene->setSceneRect(viewport()->rect());
}

QPoint VideoPane::getTransformedMousePosition(const QPoint& viewportPos)
{
    // Handle different video display modes appropriately
    QGraphicsItem* targetItem = nullptr;
    QRectF itemRect;
    
    if (m_directFFmpegMode && m_pixmapItem && m_pixmapItem->isVisible()) {
        targetItem = m_pixmapItem;
        itemRect = m_pixmapItem->boundingRect();
    } else if (m_videoItem && m_videoItem->isVisible()) {
        targetItem = m_videoItem;
        itemRect = m_videoItem->boundingRect();
    }
    
    // If no valid target item, return the original position
    if (!targetItem || itemRect.isEmpty()) {
        return viewportPos;
    }
    
    // Convert viewport coordinates to scene coordinates
    QPointF scenePos = mapToScene(viewportPos);
    
    // Map scene coordinates to item coordinates
    QPointF itemPos = targetItem->mapFromScene(scenePos);
    
    // Account for the item's transform to get the actual video content coordinates
    QTransform itemTransform = targetItem->transform();
    QRectF transformedRect = itemTransform.mapRect(QRectF(0, 0, itemRect.width(), itemRect.height()));
    
    // Normalize coordinates to 0-1 range based on the video content area
    double normalizedX = 0.0;
    double normalizedY = 0.0;
    
    if (transformedRect.width() > 0 && transformedRect.height() > 0) {
        // Calculate position relative to the transformed content
        QPointF itemOffset = itemRect.topLeft();
        double relativeX = (itemPos.x() - itemOffset.x()) / itemRect.width();
        double relativeY = (itemPos.y() - itemOffset.y()) / itemRect.height();
        
        // Clamp to 0-1 range to ensure we stay within video bounds
        normalizedX = qBound(0.0, relativeX, 1.0);
        normalizedY = qBound(0.0, relativeY, 1.0);
    }
    
    // Convert normalized coordinates back to viewport coordinates for the logical video area
    QRectF viewRect = viewport()->rect();
    int transformedX = static_cast<int>(normalizedX * viewRect.width());
    int transformedY = static_cast<int>(normalizedY * viewRect.height());
    
    // Debug output only when coordinates are significantly different
    QPoint result(transformedX, transformedY);
    if ((result - viewportPos).manhattanLength() > 5) {
        // qDebug() << "VideoPane: Transformed mouse pos from" << viewportPos 
        //          << "to" << result
        //          << "via scene:" << scenePos << "item:" << itemPos 
        //          << "normalized:" << normalizedX << normalizedY
        //          << "mode: FFmpeg=" << m_directFFmpegMode << "pixmap visible=" << (m_pixmapItem ? m_pixmapItem->isVisible() : false);
    }
    
    return result;
}

void VideoPane::validateMouseCoordinates(const QPoint& original, const QString& eventType)
{
    // This method helps debug coordinate transformation consistency
    QPoint transformed = getTransformedMousePosition(original);
    
    static QPoint lastOriginal, lastTransformed;
    static QString lastEventType;
    
    if (!lastOriginal.isNull() && eventType != lastEventType) {
        int originalDiff = (original - lastOriginal).manhattanLength();
        int transformedDiff = (transformed - lastTransformed).manhattanLength();
        
        // Log if there's a significant difference in coordinate transformation behavior
        if (abs(originalDiff - transformedDiff) > 2) {
            qCDebug(log_ui_video) << "VideoPane coordinate validation:"
                                  << "Event transition:" << lastEventType << "->" << eventType
                                  << "Original diff:" << originalDiff
                                  << "Transformed diff:" << transformedDiff
                                  << "Delta:" << abs(originalDiff - transformedDiff);
        }
    }
    
    lastOriginal = original;
    lastTransformed = transformed;
    lastEventType = eventType;
}

// Event handlers
void VideoPane::wheelEvent(QWheelEvent *event)
{
    qDebug() << "VideoPane::wheelEvent - angleDelta:" << event->angleDelta();
    
    // Call InputHandler's public method to process the event
    if (m_inputHandler) {
        m_inputHandler->handleWheelEvent(event);
    }
    event->accept();
}

void VideoPane::mousePressEvent(QMouseEvent *event)
{
    // qDebug() << "VideoPane::mousePressEvent - pos:" << event->pos();
    
    // Validate coordinate transformation consistency (debug helper)
    validateMouseCoordinates(event->pos(), "Press");
    
    // Transform the mouse position for status bar display only
    QPoint transformedPos = getTransformedMousePosition(event->pos());
    
    // Emit signal for status bar update
    emit mouseMoved(transformedPos, "Press");
    
    // Call InputHandler's public method to process the original event
    // Let InputHandler handle its own coordinate transformations
    if (m_inputHandler) {
        m_inputHandler->handleMousePress(event);
    }
    
    // Let the base class handle the event
    QGraphicsView::mousePressEvent(event);
}

void VideoPane::mouseMoveEvent(QMouseEvent *event)
{
    // qDebug() << "VideoPane::mouseMoveEvent - pos:" << event->pos() << "mouse tracking:" << hasMouseTracking();
    
    // Validate coordinate transformation consistency (debug helper) - but reduce frequency
    static int moveValidationCounter = 0;
    if (++moveValidationCounter % 10 == 1) { // Only validate every 10th move for performance
        validateMouseCoordinates(event->pos(), "Move");
    }
    
    // Transform the mouse position for status bar display only
    QPoint transformedPos = getTransformedMousePosition(event->pos());
    
    // Emit signal for status bar update
    emit mouseMoved(transformedPos, "Move");
    
    // Call InputHandler's public method to process the original event
    // Let InputHandler handle its own coordinate transformations
    if (m_inputHandler) {
        m_inputHandler->handleMouseMove(event);
    }
    
    // Let the base class handle the event
    QGraphicsView::mouseMoveEvent(event);
}

void VideoPane::mouseReleaseEvent(QMouseEvent *event)
{
    // qDebug() << "VideoPane::mouseReleaseEvent - pos:" << event->pos();
    
    // Validate coordinate transformation consistency (debug helper)
    validateMouseCoordinates(event->pos(), "Release");
    
    // Transform the mouse position for status bar display only
    QPoint transformedPos = getTransformedMousePosition(event->pos());
    
    // Emit signal for status bar update
    emit mouseMoved(transformedPos, "Release");
    
    // Call InputHandler's public method to process the original event
    // Let InputHandler handle its own coordinate transformations
    if (m_inputHandler) {
        m_inputHandler->handleMouseRelease(event);
    }
    
    // Let the base class handle the event
    QGraphicsView::mouseReleaseEvent(event);
}

// Direct GStreamer support methods (based on widgets_main.cpp approach)
void VideoPane::enableDirectGStreamerMode(bool enable)
{
    qCDebug(log_ui_video) << "VideoPane: Setting direct GStreamer mode to:" << enable << "current mode:" << m_directGStreamerMode;
    
    if (m_directGStreamerMode == enable) {
        qCDebug(log_ui_video) << "VideoPane: GStreamer mode already in requested state, no change needed";
        return;
    }
    
    m_directGStreamerMode = enable;
    
    if (enable) {
        qCDebug(log_ui_video) << "VideoPane: Enabling GStreamer mode";
        
        // Disable FFmpeg mode if it was enabled to prevent conflicts
        if (m_directFFmpegMode) {
            qCDebug(log_ui_video) << "VideoPane: Disabling FFmpeg mode for GStreamer";
            enableDirectFFmpegMode(false);
        }
        
        // Set up the overlay for GStreamer video
        setupForGStreamerOverlay();
        
        // IMPORTANT: Hide the Qt video item to prevent interference
        if (m_videoItem) {
            m_videoItem->setVisible(false);
            qCDebug(log_ui_video) << "VideoPane: Hidden Qt video item for GStreamer mode";
        }
        
        // Also hide pixmap item if it exists
        if (m_pixmapItem) {
            m_pixmapItem->setVisible(false);
            qCDebug(log_ui_video) << "VideoPane: Hidden pixmap item for GStreamer mode";
        }
        
    } else {
        qCDebug(log_ui_video) << "VideoPane: Disabling GStreamer mode";
        
        // Clean up overlay widget if it exists
        if (m_overlayWidget) {
            qCDebug(log_ui_video) << "VideoPane: Destroying GStreamer overlay widget";
            m_overlayWidget->hide();
            m_overlayWidget->deleteLater();
            m_overlayWidget = nullptr;
        }
        
        // Restore Qt video item when disabling GStreamer mode
        if (m_videoItem) {
            m_videoItem->setVisible(true);
            qCDebug(log_ui_video) << "VideoPane: Restored Qt video item";
        }
    }
    
    // Update the InputHandler's event filter target
    if (m_inputHandler) {
        m_inputHandler->updateEventFilterTarget();
    }
    
    // Force a scene update and repaint to ensure changes are visible
    if (m_scene) {
        m_scene->update();
    }
    update(); // Force view repaint
    
    qCDebug(log_ui_video) << "VideoPane: GStreamer mode" << (enable ? "enabled" : "disabled")
                          << "- overlay widget:" << (m_overlayWidget ? "exists" : "null")
                          << "video item visible:" << (m_videoItem ? m_videoItem->isVisible() : false);
}

WId VideoPane::getVideoOverlayWindowId() const
{
    // Prefer the overlay widget's native window id when in direct GStreamer mode
    if (m_directGStreamerMode && m_overlayWidget && m_overlayWidget->isVisible()) {
        WId wid = m_overlayWidget->winId();
        if (wid != 0) {
            return wid; // Accept any non-zero WId (small XIDs can be valid)
        }
        qCDebug(log_ui_video) << "VideoPane: Overlay widget winId() is 0 (not yet native)";
    }
    // Fall back to the VideoPane's own native window
    if (isVisible()) {
        WId wid = winId();
        if (wid != 0) {
            return wid; // Accept any non-zero WId
        }
        qCDebug(log_ui_video) << "VideoPane: View winId() is 0 (not yet native)";
    }
    qCWarning(log_ui_video) << "VideoPane: No valid window ID available yet";
    return 0; // Indicate no valid window ID yet
}

void VideoPane::setupForGStreamerOverlay()
{
    qCDebug(log_ui_video) << "VideoPane: Setting up for GStreamer video overlay";
    
    // Create overlay widget if it doesn't exist
    if (!m_overlayWidget) {
        m_overlayWidget = new QWidget(this);
        m_overlayWidget->setObjectName("gstreamerOverlayWidget");
        
        // CRITICAL: Transparent background so it doesn't block video display
        m_overlayWidget->setStyleSheet("background-color: transparent;");
        m_overlayWidget->setMinimumSize(640, 480);
        
        // Enable native window for video overlay (from widgets_main.cpp approach)
        m_overlayWidget->setAttribute(Qt::WA_NativeWindow, true);
        m_overlayWidget->setAttribute(Qt::WA_PaintOnScreen, true);
        
        // IMPORTANT: Make it transparent to mouse/keyboard events when not needed
        // This prevents it from interfering with normal interaction
        m_overlayWidget->setAttribute(Qt::WA_TransparentForMouseEvents, false); // Enable for GStreamer input
        
        // Enable mouse tracking and focus for event handling
        m_overlayWidget->setMouseTracking(true);
        m_overlayWidget->setFocusPolicy(Qt::StrongFocus);
        
        // Position the overlay widget to fill the viewport
        m_overlayWidget->resize(size());
        m_overlayWidget->show();
        
        qCDebug(log_ui_video) << "VideoPane: Created GStreamer overlay widget with window ID:" << m_overlayWidget->winId();
        qCDebug(log_ui_video) << "Overlay widget size:" << m_overlayWidget->size() << "position:" << m_overlayWidget->pos();
        
        // Update the InputHandler to use the overlay widget for events
        if (m_inputHandler) {
            m_inputHandler->updateEventFilterTarget();
        }
    } else {
        qCDebug(log_ui_video) << "VideoPane: GStreamer overlay widget already exists, ensuring visibility";
        m_overlayWidget->show();
        m_overlayWidget->raise(); // Ensure it's on top
    }
}

// FFmpeg direct video frame support
void VideoPane::updateVideoFrame(const QPixmap& frame)
{
    // PERFORMANCE: Eliminate per-frame logging completely
    if (!m_directFFmpegMode || frame.isNull()) {
        return;
    }
    
    // RESPONSIVENESS OPTIMIZATION: Reduce frame rate limiting for better mouse response
    // More aggressive frame dropping to prioritize mouse responsiveness
    static qint64 lastFrameTime = 0;
    qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
    
    // Reduce UI update interval for more responsive mouse handling
    // 12ms = ~83 FPS max (was 16ms = ~60 FPS)
    if (currentTime - lastFrameTime < 12) {
        return; // Drop frame silently for performance
    }
    lastFrameTime = currentTime;
    
    // PERFORMANCE: Log only the very first frame
    static bool firstFrameProcessed = false;
    static int frameCounter = 0;
    frameCounter++;
    
    if (!firstFrameProcessed && !frame.isNull()) {
        firstFrameProcessed = true;
        qCDebug(log_ui_video) << "VideoPane: First FFmpeg frame received, size:" << frame.size();
        // Skip all expensive analysis for performance
    }
    
    // PERFORMANCE OPTIMIZATION: Track frame size changes to only update transform when needed
    static QSize lastFrameSize;
    static QSize lastViewportSize;
    QSize currentFrameSize = frame.size();
    QSize currentViewportSize = viewport()->rect().size();
    
    bool frameSizeChanged = (currentFrameSize != lastFrameSize);
    bool viewportSizeChanged = (currentViewportSize != lastViewportSize);
    bool needsTransformUpdate = frameSizeChanged || viewportSizeChanged;
    
    // Create or update pixmap item for displaying the decoded frame
    if (!m_pixmapItem) {
        m_pixmapItem = m_scene->addPixmap(frame);
        m_pixmapItem->setZValue(2); // Above video item and GStreamer overlay
        m_pixmapItem->setVisible(true);
        
        // CRITICAL: Hide Qt video item to prevent interference
        if (m_videoItem) {
            m_videoItem->setVisible(false);
        }
        
        // Always update transform for the first frame
        updateVideoItemTransform();
        centerVideoItem();
        needsTransformUpdate = false; // Already handled
        
    } else {
        // For subsequent frames, just update the pixmap - much faster!
        m_pixmapItem->setPixmap(frame);
        
        // Ensure pixmap item is visible (only if needed)
        if (!m_pixmapItem->isVisible()) {
            m_pixmapItem->setVisible(true);
        }
        
        // Ensure item is properly added to scene if it somehow got removed
        if (m_pixmapItem->scene() != m_scene) {
            m_scene->addItem(m_pixmapItem);
            m_pixmapItem->setZValue(2);
        }
    }
    
    // SMART OPTIMIZATION: Only update transform when frame size or viewport size changes
    if (needsTransformUpdate) {
        updateVideoItemTransform();
        centerVideoItem();
        
        // Update cached sizes
        lastFrameSize = currentFrameSize;
        lastViewportSize = currentViewportSize;
        
        qCDebug(log_ui_video) << "VideoPane: Updated transform due to size change - frame:" 
                              << currentFrameSize << "viewport:" << currentViewportSize;
    }
    
    // PERFORMANCE: Minimize update calls - only update the pixmap item region
    if (m_pixmapItem) {
        // Force immediate scene invalidation for just the pixmap area - more efficient
        m_scene->invalidate(m_pixmapItem->boundingRect());
    }
    
    // CRITICAL: Hide Qt video item to prevent interference
    if (m_videoItem) {
        m_videoItem->setVisible(false);
    }
    
    // Update video item transform to handle scaling and aspect ratio
    updateVideoItemTransform();
    
    // Center the display
    centerVideoItem();
    
    // PERFORMANCE: Minimize updates for better performance
    if (m_scene) {
        m_scene->update(); // Update scene
    }
    this->update(); // Update view
    
    // REMOVED: QCoreApplication::processEvents() - causes excessive CPU usage
    // Let Qt's natural event loop handle updates
}

void VideoPane::enableDirectFFmpegMode(bool enable)
{
    qCDebug(log_ui_video) << "VideoPane: enableDirectFFmpegMode called with:" << enable << "current mode:" << m_directFFmpegMode;
    
    m_directFFmpegMode = enable;
    
    if (enable) {
        qCDebug(log_ui_video) << "VideoPane: Enabling FFmpeg mode";
        
        // Hide the Qt video item when using direct FFmpeg mode
        if (m_videoItem) {
            m_videoItem->setVisible(false);
            qCDebug(log_ui_video) << "VideoPane: Hidden Qt video item for FFmpeg mode";
        }
        
        // Ensure we have a pixmap item ready for frame display
        if (!m_pixmapItem) {
            QPixmap placeholder(640, 480);
            placeholder.fill(Qt::black);
            m_pixmapItem = m_scene->addPixmap(placeholder);
            m_pixmapItem->setZValue(2); // Above video item
            qCDebug(log_ui_video) << "VideoPane: Created pixmap item for FFmpeg frames";
        }
        
        // CRITICAL: Force pixmap item to be visible
        if (m_pixmapItem) {
            m_pixmapItem->setVisible(true);
            qCDebug(log_ui_video) << "VideoPane: Forced pixmap item visibility to true";
        }
        
        // Disable GStreamer mode if it was enabled
        if (m_directGStreamerMode) {
            qCDebug(log_ui_video) << "VideoPane: Disabling GStreamer mode for FFmpeg";
            enableDirectGStreamerMode(false);
        }
        
    } else {
        qCDebug(log_ui_video) << "VideoPane: Disabling FFmpeg mode";
        
        // Restore Qt video item when disabling FFmpeg mode
        if (m_videoItem) {
            m_videoItem->setVisible(true);
            qCDebug(log_ui_video) << "VideoPane: Restored Qt video item";
        }
        
        // Hide the pixmap item
        if (m_pixmapItem) {
            m_pixmapItem->setVisible(false);
            qCDebug(log_ui_video) << "VideoPane: Hidden pixmap item";
        }
    }
    
    // Force a scene update and repaint
    if (m_scene) {
        m_scene->update();
    }
    update(); // Force view repaint
    
    qCDebug(log_ui_video) << "VideoPane: FFmpeg mode" << (enable ? "enabled" : "disabled") 
                          << "- pixmap visible:" << (m_pixmapItem ? m_pixmapItem->isVisible() : false)
                          << "video visible:" << (m_videoItem ? m_videoItem->isVisible() : false);
}

