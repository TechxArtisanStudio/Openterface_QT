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
#include <thread>
#include <chrono>
#include <cmath>

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
    m_originalVideoSize(QSize(GlobalVar::instance().getWinWidth(), GlobalVar::instance().getWinHeight())),
    m_maintainAspectRatio(true),
    m_directGStreamerMode(false),
    m_overlayWidget(nullptr),
    m_directFFmpegMode(false),
    m_lastViewportSize(QSize()),
    m_frameIsViewportSized(false)
{
    qDebug(log_ui_video) << "VideoPane init...";
    
    // Set up the graphics scene
    setupScene();
    
    // Create and initialize the video item
    m_videoItem = new QGraphicsVideoItem();
    m_scene->addItem(m_videoItem);
    m_videoItem->setZValue(0); // Below pixmap item
    
    // Initialize coordinate offset correction values
    m_zoomOffsetCorrectionX = 5;
    m_zoomOffsetCorrectionY = 5;
    
    // Configure the graphics view
    setScene(m_scene);
    setDragMode(QGraphicsView::NoDrag);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    
    // ============ OPTIMIZED RENDERING FOR VIDEO STREAMING ============
    setRenderHint(QPainter::Antialiasing, true);
    setRenderHint(QPainter::SmoothPixmapTransform, true); 
    setRenderHint(QPainter::TextAntialiasing, true);  // Critical for text clarity
    
    
    // DO NOT use DontAdjustForAntialiasing - it degrades quality for performance
    // setOptimizationFlag(QGraphicsView::DontAdjustForAntialiasing, true);  // REMOVED
    
    // CRITICAL FIX: Use MinimalViewportUpdate for better video streaming performance
    // FullViewportUpdate can cause update batching that leads to freezing
    setViewportUpdateMode(QGraphicsView::MinimalViewportUpdate);
    
    // CRITICAL FIX: Disable cache for video streaming to prevent stale frames
    // CacheBackground can hold old frames and prevent updates
    setCacheMode(QGraphicsView::CacheNone);

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
    
    // 1. FIRST: Clean up overlay widget before anything else (to prevent event filter crashes)
    if (m_overlayWidget) {
        qCDebug(log_ui_video) << "VideoPane: Cleaning up overlay widget in destructor";
        m_overlayWidget->hide();
        // Do NOT call deleteLater here - it's already been called from stopCamera()
        // Just clear the pointer to prevent dangling reference
        m_overlayWidget = nullptr;
    }
    
    // 2. Remove event filter and stop input handler to prevent event processing
    if (m_inputHandler) {
        removeEventFilter(m_inputHandler);
        m_inputHandler->deleteLater(); // Use deleteLater for safer cleanup
        m_inputHandler = nullptr;
    }
    
    // 3. Stop timers
    if (escTimer) {
        escTimer->stop();
        escTimer->deleteLater();
        escTimer = nullptr;
    }
    
    // 4. Specific signal disconnection to prevent callbacks during destruction
    // Note: Do NOT use wildcard disconnect() as it causes crashes during app shutdown
    // when trying to disconnect from destroyed signals on partially-destroyed objects
    
    // 5. Clean up graphics items in correct order
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
        updateScrollBarsAndSceneRect();
    }
}

QGraphicsVideoItem* VideoPane::videoItem() const
{
    return m_videoItem;
}

void VideoPane::resetZoom()
{
    m_scaleFactor = 1.0;
    resetTransform(); // Reset view transform
    updateVideoItemTransform();
    updateScrollBarsAndSceneRect();
    
    // Log zoom reset
    qCDebug(log_ui_video) << "Zoom reset: current zoom=" << m_scaleFactor
                         << "transform=" << transform();
}

void VideoPane::centerOn(const QPointF &pos)
{
    // Center the view on a specific scene point
    QGraphicsView::centerOn(pos);
    
    // Log centering for debugging
    static int centerCounter = 0;
    if (++centerCounter % 10 == 1) {
        qCDebug(log_ui_video) << "Centering view on:" << pos
                             << "scroll bars:" << QPoint(horizontalScrollBar()->value(), verticalScrollBar()->value());
    }
}

void VideoPane::zoomIn(double factor)
{
    // Store the center point of the viewport before zooming
    QPointF centerPoint = mapToScene(viewport()->rect().center());
    
    // Apply zoom
    m_scaleFactor *= factor;
    scale(factor, factor);
    updateVideoItemTransform();
    updateScrollBarsAndSceneRect();
    
    // Center back on the same scene point to maintain focus during zoom
    centerOn(centerPoint);
    
    // Log zoom information
    qCDebug(log_ui_video) << "Zoom in: factor=" << factor << "current zoom=" << m_scaleFactor
                        << "view transform=" << transform() 
                        << "viewport size=" << viewport()->size()
                        << "centered on=" << centerPoint;
}

void VideoPane::zoomOut(double factor)
{
    // Store the center point of the viewport before zooming
    QPointF centerPoint = mapToScene(viewport()->rect().center());
    
    // Apply zoom
    m_scaleFactor *= factor;
    scale(factor, factor);
    updateVideoItemTransform();
    updateScrollBarsAndSceneRect();
    
    // Center back on the same scene point to maintain focus during zoom
    centerOn(centerPoint);
    
    // Log zoom information
    qCDebug(log_ui_video) << "Zoom out: factor=" << factor << "current zoom=" << m_scaleFactor
                        << "view transform=" << transform()
                        << "viewport size=" << viewport()->size()
                        << "centered on=" << centerPoint;
}

void VideoPane::fitToWindow()
{
    if (m_videoItem) {
        // Reset any existing transformations
        resetTransform();
        m_scaleFactor = 1.0;
        
        // Update the video item transform to fit the current view
        updateVideoItemTransform();
        updateScrollBarsAndSceneRect();
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
    
    // Track viewport size changes and emit signal
    QSize newViewportSize = viewport()->size();
    if (m_lastViewportSize != newViewportSize) {
        m_lastViewportSize = newViewportSize;
        emit viewportSizeChanged(newViewportSize);
    }
    
    // If in FFmpeg direct mode and we have a pixmap, re-evaluate frame sizing so we can pre-scale to exact viewport
    if (m_directFFmpegMode && m_pixmapItem && !m_pixmapItem->pixmap().isNull()) {
        // Re-run the update path with the currently displayed pixmap to keep 1:1 mapping on resize
        updateVideoFrame(m_pixmapItem->pixmap());
    }

    // Update scene rect and scroll bars on resize
    updateScrollBarsAndSceneRect();
    
    updateVideoItemTransform();
    
    // Update overlay widget size for direct GStreamer mode
    if (m_directGStreamerMode && m_overlayWidget) {
        m_overlayWidget->resize(size());
        qDebug() << "VideoPane: Resized GStreamer overlay widget to:" << size();
        
        // Emit signal for GStreamer backend to update render rectangle
        emit videoPaneResized(size());
    }
}

// Helper methods
void VideoPane::updateVideoItemTransform()
{
    qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
    QGraphicsItem* targetItem = nullptr;
    QRectF itemRect;
    
    if (m_directFFmpegMode && m_pixmapItem) {
        targetItem = m_pixmapItem;
        itemRect = m_pixmapItem->boundingRect();
        // qDebug(log_ui_video) << "VideoPane: Updating FFmpeg pixmap transform";
    } else if (m_directGStreamerMode) {
        // For GStreamer overlay mode, the Qt video item is not used; instead, ensure the overlay
        // widget matches the viewport geometry and skip detailed QGraphics transforms.
        if (m_overlayWidget) {
            QRectF viewRect = viewport()->rect();
            if (viewRect.width() > 0 && viewRect.height() > 0) {
                m_overlayWidget->setGeometry(viewRect.toRect());
            }
        }
        qCDebug(log_ui_video) << "VideoPane: Updated GStreamer overlay widget geometry to:" << m_overlayWidget->geometry();
        // Nothing further to transform; overlay handles the rendered video
        return;
    } else if (m_videoItem) {
        // Default: use the Qt video item when not in FFmpeg mode and not in GStreamer overlay mode
        targetItem = m_videoItem;
        itemRect = m_videoItem->boundingRect();
        qCDebug(log_ui_video) << "VideoPane: Updating Qt video item transform";
    }

    // If we don't have a valid target item, nothing to transform
    if (!targetItem) {
        return;
    }

    QRectF viewRect = viewport()->rect();
    if (itemRect.isEmpty() || viewRect.isEmpty()) return;

    // Normalize the item rectangle to start from (0,0) and get the original offset
    QRectF normalizedRect(0, 0, itemRect.width(), itemRect.height());
    QPointF itemOffset = itemRect.topLeft();

    // Check if frame is viewport-sized (pre-scaled at decode time) and use 1:1 scaling
    if (m_directFFmpegMode && m_frameIsViewportSized) {
        // Frame is already sized to viewport - use identity transform for 1:1 display
        qCDebug(log_ui_video) << "Using 1:1 scaling for viewport-sized frame:" << normalizedRect.size() << "viewport:" << viewRect.size();
        
        QTransform transform;
        // Identity transform (no scaling)
        targetItem->setTransform(transform);
        
        // Center the item directly without additional scaling
        double x = (viewRect.width() - normalizedRect.width()) / 2.0 - itemOffset.x();
        double y = (viewRect.height() - normalizedRect.height()) / 2.0 - itemOffset.y();
        targetItem->setPos(x, y);
        
        return; // Skip standard scaling logic
    }

    if (m_scaleFactor > 1.0) {
        // When zoomed in, use the view transform to scale the item, but apply a base transform
        double scaleX = viewRect.width() / normalizedRect.width();
        double scaleY = viewRect.height() / normalizedRect.height();
        double scale = qMin(scaleX, scaleY);

        QTransform transform;
        transform.scale(scale, scale);
        targetItem->setTransform(transform);

        QRectF scaledRect = QRectF(0, 0, normalizedRect.width() * scale, normalizedRect.height() * scale);
        double x = (viewRect.width() - scaledRect.width()) / 2.0 - (itemOffset.x() * scale);
        double y = (viewRect.height() - scaledRect.height()) / 2.0 - (itemOffset.y() * scale);
        targetItem->setPos(x, y);
    } else if (m_maintainAspectRatio) {
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
    } else {
        // Stretch to fill (ignore aspect ratio)
        QTransform transform;
        transform.scale(viewRect.width() / normalizedRect.width(), viewRect.height() / normalizedRect.height());
        targetItem->setTransform(transform);
        // Account for the original offset when stretching
        targetItem->setPos(-itemOffset.x(), -itemOffset.y());
    }
    // qCDebug(log_ui_video) <<  QDateTime::currentMSecsSinceEpoch() - currentTime << "ms taken to update video item transform.";
}

void VideoPane::centerVideoItem()
{
    // Handle both Qt video item and FFmpeg pixmap item
    QGraphicsItem* targetItem = nullptr;
    QRectF itemRect;
    
    if (m_directFFmpegMode && m_pixmapItem) {
        targetItem = m_pixmapItem;
        itemRect = m_pixmapItem->boundingRect();
        //qDebug(log_ui_video) << "VideoPane: Centering FFmpeg pixmap item";
    } else if (m_videoItem) {
        targetItem = m_videoItem;
        itemRect = m_videoItem->boundingRect();
        //qDebug() << "VideoPane: Centering Qt video item";
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

void VideoPane::updateScrollBarsAndSceneRect()
{
    // Get the actual video content size
    QRectF contentRect;
    if (m_directFFmpegMode && m_pixmapItem) {
        contentRect = m_pixmapItem->boundingRect();
    } else if (m_videoItem) {
        contentRect = m_videoItem->boundingRect();
    }
    
    if (contentRect.isEmpty()) {
        // Fallback to viewport size if no content
        contentRect = viewport()->rect();
    }
    
    if (m_scaleFactor > 1.0) {
        // Enable scroll bars when zoomed in
        setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        
        // When zoomed in, we need a scene rect that accounts for the zoom level
        // to ensure proper scrolling boundaries
        if (m_scene) {
            QRectF viewportRect = viewport()->rect();
            
            // Calculate the effective scene size based on the zoom factor
            // This ensures scroll bars have the correct range
            QRectF zoomedSceneRect = QRectF(
                viewportRect.x(),
                viewportRect.y(),
                viewportRect.width(), 
                viewportRect.height()
            );
            
            // Set the scene rect to match the viewport
            m_scene->setSceneRect(zoomedSceneRect);
            
            // Log the scene rect update
            qCDebug(log_ui_video) << "Updated scene rect for zoom:" << zoomedSceneRect
                                 << "zoom factor:" << m_scaleFactor
                                 << "viewport:" << viewportRect;
        }
    } else {
        // Disable scroll bars when at normal zoom or below
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        
        // Reset scene rect to viewport size
        if (m_scene) {
            m_scene->setSceneRect(viewport()->rect());
        }
    }
}

QPointF VideoPane::getTransformedMousePosition(const QPoint& viewportPos)
{
    // qCDebug(log_ui_video) << "      [getTransformed] Input viewportPos:" << viewportPos;
    
    // Handle different video display modes appropriately
    QGraphicsItem* targetItem = nullptr;
    QRectF itemRect;
    
    if (m_directFFmpegMode && m_pixmapItem && m_pixmapItem->isVisible()) {
        targetItem = m_pixmapItem;
        itemRect = m_pixmapItem->boundingRect();
        // qCDebug(log_ui_video) << "      [getTransformed] Using FFmpeg pixmap item";
    } else if (m_videoItem && m_videoItem->isVisible()) {
        targetItem = m_videoItem;
        itemRect = m_videoItem->boundingRect();
        // qCDebug(log_ui_video) << "      [getTransformed] Using video item";
    } else if (m_directGStreamerMode) {
        // Special handling for GStreamer mode
        QRectF viewRect = viewport()->rect();
        qDebug() << "      [getTransformed] viewRect:" << viewRect;

        // Guard against invalid view or original video size
        if (viewRect.width() <= 0 || viewRect.height() <= 0) {
            qCWarning(log_ui_video) << "Invalid viewport size for GStreamer mapping:" << viewRect.size();
            return QPointF(viewportPos);
        }

        int vwInt = m_originalVideoSize.width();
        int vhInt = m_originalVideoSize.height();
        if (vwInt <= 0 || vhInt <= 0) {
            qCWarning(log_ui_video) << "Invalid original video size for GStreamer mapping:" << m_originalVideoSize;
            // fallback to viewport coordinates
            return QPointF(viewportPos);
        }

        double vw = static_cast<double>(vwInt);
        double vh = static_cast<double>(vhInt);
        double viewW = viewRect.width();
        double viewH = viewRect.height();
        double videoAspect = vw / vh;
        double viewAspect = viewW / viewH;
        double scale;
        if (videoAspect > viewAspect) {
            scale = viewW / vw;
        } else {
            scale = viewH / vh;
        }
        double scaledWidth = vw * scale;
        double scaledHeight = vh * scale;
        double x = (viewW - scaledWidth) / 2;
        double y = (viewH - scaledHeight) / 2;
        QRectF videoRect(x, y, scaledWidth, scaledHeight);
        qDebug() << "      [videoRect] " << x << y << scaledWidth << scaledHeight;
        // Calculate itemPos manually
        QPointF itemPos = viewportPos - videoRect.topLeft();
        double itemWidth = videoRect.width();
        double itemHeight = videoRect.height();
        qDebug() << "      [getTransformed] itemPos, itemWidth, itemHeight:" << itemPos << itemWidth << itemHeight;
        if (itemWidth <= 0 || itemHeight <= 0) {
            return viewportPos;
        }
        qDebug() << "      [getTransformed] itemWidth/itemHeight:" << itemWidth << itemHeight;
        double relativeX = itemPos.x() / itemWidth;
        double relativeY = itemPos.y() / itemHeight;
        double normalizedX = qBound(0.0, relativeX, 1.0);
        double normalizedY = qBound(0.0, relativeY, 1.0);
        double transformedXDouble = normalizedX * viewRect.width();
        double transformedYDouble = normalizedY * viewRect.height();
        int transformedX = qRound(transformedXDouble);
        int transformedY = qRound(transformedYDouble);
        QPointF finalResult(transformedXDouble, transformedYDouble);
        qDebug() << "      [getTransformed] Before zoom correction:" << finalResult;
        if (m_scaleFactor > 1.0) {
            transformedX += m_zoomOffsetCorrectionX;
            transformedY += m_zoomOffsetCorrectionY;
            finalResult = QPointF(transformedX, transformedY);
        }
        return finalResult;
    }
    
    // If no valid target item, return the original position
    if (!targetItem || itemRect.isEmpty()) {
        // qCDebug(log_ui_video) << "      [getTransformed] No valid item, returning original pos";
        return QPointF(viewportPos);
    }
    
    QRectF viewRect = viewport()->rect();
    QTransform viewTransform = transform();
    QTransform itemTransform = targetItem->transform();
    
    // Step 1: Convert viewport coordinates to scene coordinates (this accounts for scrolling)
    QPointF scenePos = mapToScene(viewportPos);
    // qCDebug(log_ui_video) << "      [getTransformed] After mapToScene:" << scenePos;
    
    // Step 2: Map scene coordinates to item coordinates
    QPointF itemPos = targetItem->mapFromScene(scenePos);
    // qCDebug(log_ui_video) << "      [getTransformed] After mapFromScene (itemPos):" << itemPos;
    
    // Step 3: Calculate relative position within the item's original coordinates
    QPointF itemOffset = itemRect.topLeft();
    double itemWidth = itemRect.width();
    double itemHeight = itemRect.height();
    
    // qCDebug(log_ui_video) << "      [getTransformed] itemRect:" << itemRect;
    // qCDebug(log_ui_video) << "      [getTransformed] itemOffset:" << itemOffset << "size:" << QSizeF(itemWidth, itemHeight);
    
    // Check if dimensions are valid to prevent division by zero
    if (itemWidth <= 0 || itemHeight <= 0) {
        qCWarning(log_ui_video) << "Invalid item dimensions: width=" << itemWidth << "height=" << itemHeight;
        return viewportPos;
    }
    
    // Calculate relative position within the item
    double relativeX = (itemPos.x() - itemOffset.x()) / itemWidth;
    double relativeY = (itemPos.y() - itemOffset.y()) / itemHeight;
    
    // qCDebug(log_ui_video) << "      [getTransformed] relativeX/Y:" << relativeX << relativeY;
    
    // Step 4: Clamp to 0-1 range to ensure we stay within video bounds
    double normalizedX = qBound(0.0, relativeX, 1.0);
    double normalizedY = qBound(0.0, relativeY, 1.0);
    
    // qCDebug(log_ui_video) << "      [getTransformed] normalizedX/Y:" << normalizedX << normalizedY;
    
    // Step 5: Convert normalized coordinates back to viewport coordinates for the logical video area
    // (This is the actual size expected by the target device)
    // Use qRound() for proper rounding to minimize error
    double transformedXDouble = normalizedX * viewRect.width();
    double transformedYDouble = normalizedY * viewRect.height();
    
    // qCDebug(log_ui_video) << "      [getTransformed] Before rounding:" << transformedXDouble << transformedYDouble;
    
    int transformedX = qRound(transformedXDouble);
    int transformedY = qRound(transformedYDouble);
    
    // qCDebug(log_ui_video) << "      [getTransformed] After qRound:" << transformedX << transformedY;
    
    // For zoomed mode, we need to apply additional logic to handle the scrolled view
    QPointF finalResult;
    if (m_scaleFactor > 1.0) {
        // When zoomed, we take the normalizedX/Y coordinates (relative position within the video)
        // and map them to the target device's coordinate system
        
        // Apply configurable correction factors to account for the observed offset
        // The positive correction compensates for the observed negative offset
        transformedX += m_zoomOffsetCorrectionX;
        transformedY += m_zoomOffsetCorrectionY;
        
        finalResult = QPointF(transformedX, transformedY);
        // qCDebug(log_ui_video) << "      [getTransformed] Zoomed mode - with correction:" << finalResult;
    } else {
        // When not zoomed, use the straightforward transformation
        finalResult = QPointF(transformedXDouble, transformedYDouble);
    }
    
    // qCDebug(log_ui_video) << "      [getTransformed] Final result:" << finalResult;
    
    return finalResult;
}

void VideoPane::validateMouseCoordinates(const QPoint& original, const QString& eventType)
{
    // This method helps debug coordinate transformation consistency
    QPointF transformedF = getTransformedMousePosition(original);
    QPoint transformed = QPoint(qRound(transformedF.x()), qRound(transformedF.y()));
    
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
    // qDebug() << "VideoPane::wheelEvent - angleDelta:" << event->angleDelta();
    
    // Call InputHandler's public method to process the event
    if (m_inputHandler) {
        m_inputHandler->handleWheelEvent(event);
    }
    event->accept();
}

void VideoPane::mousePressEvent(QMouseEvent *event)
{
    // Validate coordinate transformation consistency (debug helper)
    validateMouseCoordinates(event->pos(), "Press");
    
    // Transform the mouse position ONCE and cache it
    QPointF transformedPosF = getTransformedMousePosition(event->pos());
    QPoint transformedPos = QPoint(qRound(transformedPosF.x()), qRound(transformedPosF.y()));
    
    // Emit signal for status bar update
    emit mouseMoved(transformedPos, "Press");
    
    // Call InputHandler - it will skip if eventFilter already processed it
    if (m_inputHandler) {
        m_inputHandler->handleMousePress(event);
    }
    
    // Call base class handler
    QGraphicsView::mousePressEvent(event);
}

void VideoPane::mouseMoveEvent(QMouseEvent *event)
{
    // Optional debug: track raw mouse positions for troubleshooting
    static int debugCounter = 0;
    if (m_scaleFactor > 1.0 && ++debugCounter % 100 == 1) {
        QPoint viewportPos = event->pos();
        QPointF scenePos = mapToScene(viewportPos);
        qCDebug(log_ui_video) << "Raw mouse move: viewport=" << viewportPos 
                             << "scene=" << scenePos
                             << "zoom=" << m_scaleFactor
                             << "scroll=" << QPoint(horizontalScrollBar()->value(), verticalScrollBar()->value());
    }
    
    // Track mouse move event intervals
    static qint64 lastMoveTime = 0;
    static qint64 totalMoveInterval = 0;
    static int moveEventCount = 0;
    static qint64 lastPrintTime = 0;
    
    qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
    
    if (lastMoveTime > 0) {
        qint64 interval = currentTime - lastMoveTime;
        totalMoveInterval += interval;
        moveEventCount++;
        
        // Print average interval every second
        if (currentTime - lastPrintTime >= 1000) {
            double avgInterval = static_cast<double>(totalMoveInterval) / moveEventCount;
            qCDebug(log_ui_video) << "Mouse Move Event Statistics:"
                                 << "Event count (1s):" << moveEventCount
                                 << "Average interval:" << QString::number(avgInterval, 'f', 2) << "ms";
            
            // Reset counters
            totalMoveInterval = 0;
            moveEventCount = 0;
            lastPrintTime = currentTime;
        }
    }
    lastMoveTime = currentTime;
    
    // Validate coordinate transformation consistency (debug helper) - but reduce frequency
    static int moveValidationCounter = 0;
    if (++moveValidationCounter % 10 == 1) { // Only validate every 10th move for performance
        validateMouseCoordinates(event->pos(), "Move");
    }
    
    // Transform the mouse position for status bar display only
    QPointF transformedPosF = getTransformedMousePosition(event->pos());
    QPoint transformedPos = QPoint(qRound(transformedPosF.x()), qRound(transformedPosF.y()));
    
    // Emit signal for status bar update
    emit mouseMoved(transformedPos, "Move");
    
    // Call InputHandler - it will skip if eventFilter already processed it
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
    QPointF transformedPosF = getTransformedMousePosition(event->pos());
    QPoint transformedPos = QPoint(qRound(transformedPosF.x()), qRound(transformedPosF.y()));
    
    // Emit signal for status bar update
    emit mouseMoved(transformedPos, "Release");
    
    // Call InputHandler - it will skip if eventFilter already processed it
    if (m_inputHandler) {
        m_inputHandler->handleMouseRelease(event);
    }
    
    // Call base class handler
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
    // Use working v0.4.0 approach: simple and direct
    if (m_directGStreamerMode && m_overlayWidget) {
        return m_overlayWidget->winId();
    }
    // Fall back to the main widget's window ID
    return winId();
}

void VideoPane::setupForGStreamerOverlay()
{
    qCDebug(log_ui_video) << "VideoPane: Setting up for GStreamer video overlay";
    
    // Create overlay widget if it doesn't exist
    if (!m_overlayWidget) {
        m_overlayWidget = new QWidget(this);
        m_overlayWidget->setObjectName("gstreamerOverlayWidget");
        
        // CRITICAL: Black background for GStreamer overlay to work properly (from working v0.4.0)
        m_overlayWidget->setStyleSheet("background-color: black; border: 2px solid white;");
        m_overlayWidget->setMinimumSize(640, 480);
        
        // Enable native window for video overlay (from widgets_main.cpp approach)
        m_overlayWidget->setAttribute(Qt::WA_NativeWindow, true);
        m_overlayWidget->setAttribute(Qt::WA_PaintOnScreen, true);
        
        // IMPORTANT: Make sure the widget can receive video overlay (from working v0.4.0)
        // Removing transparent mouse events to ensure proper overlay functionality
        // m_overlayWidget->setAttribute(Qt::WA_TransparentForMouseEvents, false); // Enable for GStreamer input
        
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

        // Use a separate detached thread to wait 0.5s then request fitToWindow()
        // The actual call is queued to the GUI thread using QMetaObject::invokeMethod
        // to ensure all GUI operations run on the main thread.
        // std::thread([this]() {
        //     std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        //     QMetaObject::invokeMethod(this, [this]() { this->fitToWindow(); }, Qt::QueuedConnection);
        // }).detach();
    } else {
        qCDebug(log_ui_video) << "VideoPane: GStreamer overlay widget already exists, ensuring visibility";
        m_overlayWidget->show();
        m_overlayWidget->raise(); // Ensure it's on top
        // // Schedule fitToWindow() after a short delay using a background thread
        // std::thread([this]() {
        //     std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        //     QMetaObject::invokeMethod(this, [this]() { this->fitToWindow(); }, Qt::QueuedConnection);
        // }).detach();
    }
}

// FFmpeg direct video frame support - optimized version receiving QImage
void VideoPane::updateVideoFrameFromImage(const QImage& image)
{
    if (image.isNull()) {
        return;
    }

    // Use window/widget DPR to ensure consistent logical/physical pixel mapping
    qreal widgetDpr = 1.0;
    if (window()) widgetDpr = window()->devicePixelRatioF();
    else widgetDpr = this->devicePixelRatioF();
    
    // Convert QImage to QPixmap (fromImage already does a deep copy, no need for extra copy)
    QPixmap frame = QPixmap::fromImage(image);
    frame.setDevicePixelRatio(widgetDpr);

    qCDebug(log_ui_video) << "updateVideoFrameFromImage: image.size()=" << image.size()
                         << " image.dpr=" << image.devicePixelRatioF()
                         << " widgetDpr=" << widgetDpr
                         << " resulting pixmap.logicalSize=" << QSizeF(frame.width()/widgetDpr, frame.height()/widgetDpr);

    updateVideoFrame(frame);
    
    // CRITICAL FIX: Force immediate viewport update to prevent freezing
    viewport()->update();
}

// Update QGraphicsVideoItem from QImage (GUI thread conversion)
void VideoPane::updateGraphicsVideoItemFromImage(QGraphicsVideoItem* videoItem, const QImage& image)
{
    if (!videoItem || image.isNull()) {
        return;
    }

    if (!videoItem->scene()) {
        qCWarning(log_ui_video) << "QGraphicsVideoItem has no scene";
        return;
    }

    // Convert QImage to QPixmap on GUI thread
    QPixmap frame = QPixmap::fromImage(image);
    
    // Find or create pixmap item for display
    QGraphicsPixmapItem* pixmapItem = nullptr;
    QList<QGraphicsItem*> items = videoItem->scene()->items();
    for (auto item : items) {
        if (auto pItem = qgraphicsitem_cast<QGraphicsPixmapItem*>(item)) {
            pixmapItem = pItem;
            break;
        }
    }

    if (!pixmapItem) {
        pixmapItem = videoItem->scene()->addPixmap(frame);
        pixmapItem->setZValue(1); // Above video item
    } else {
        pixmapItem->setPixmap(frame);
    }
}

void VideoPane::updateVideoFrame(const QPixmap& frame)
{
    if (!m_directFFmpegMode || frame.isNull()) {
        return;
    }

    // Ensure widget DPR is used consistently
    qreal widgetDpr = 1.0;
    if (window()) widgetDpr = window()->devicePixelRatioF();
    else widgetDpr = this->devicePixelRatioF();

    // Force pixmap DPR to widget DPR (incoming pixmap may carry different DPR)
    QPixmap local = frame;
    local.setDevicePixelRatio(widgetDpr);

    // Logical frame size in device-independent pixels
    QSizeF logicalFrameSizeF(local.width() / widgetDpr, local.height() / widgetDpr);
    QSize logicalFrameSize(qRound(logicalFrameSizeF.width()), qRound(logicalFrameSizeF.height()));

    qCDebug(log_ui_video) << "updateVideoFrame: pixmap.physical=" << local.size()
                         << " widgetDpr=" << widgetDpr
                         << " logicalFrameSize=" << logicalFrameSizeF
                         << " viewport=" << viewport()->rect().size();

    // Store original video logical size
    m_originalVideoSize = logicalFrameSize;

    // Compare using logical pixels (allow small tolerance to account for rounding/DPR differences)
    QSize viewportLogical = viewport()->rect().size();
    const int tolerance = 2; // allow small diffs (rounding, DPR, scrollbars, etc.)
    m_frameIsViewportSized = (qAbs(qRound(logicalFrameSizeF.width()) - viewportLogical.width()) <= tolerance &&
                              qAbs(qRound(logicalFrameSizeF.height()) - viewportLogical.height()) <= tolerance);

    // FAST PATH: 1:1 mapping of logical pixels (no runtime resampling)
    if (m_frameIsViewportSized) {
        // If the sizes are close but not exact, pre-scale the pixmap to exact viewport physical size
        QSize targetPhysicalSize(qRound(viewportLogical.width() * widgetDpr), qRound(viewportLogical.height() * widgetDpr));
        if (local.size() != targetPhysicalSize) {
            qCDebug(log_ui_video) << "Treating near-match as viewport-sized; pre-scaling pixmap to exact viewport (physical):" << targetPhysicalSize;
            QPixmap scaled = local.scaled(targetPhysicalSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            scaled.setDevicePixelRatio(widgetDpr);
            local = scaled;
        }

        if (!m_pixmapItem) {
            m_pixmapItem = m_scene->addPixmap(local);
            m_pixmapItem->setZValue(2);
            m_pixmapItem->setTransformationMode(Qt::SmoothTransformation);
            m_pixmapItem->setCacheMode(QGraphicsItem::NoCache);
        } else {
            m_pixmapItem->setPixmap(local);
        }

        m_pixmapItem->setTransform(QTransform());
        m_pixmapItem->setPos(0, 0);
        m_pixmapItem->setVisible(true);

        if (m_videoItem) m_videoItem->setVisible(false);

        // Ensure scene rect equals viewport logical rect (use integers)
        m_scene->setSceneRect(QRectF(0, 0, viewportLogical.width(), viewportLogical.height()));
        
        // CRITICAL FIX: Force immediate updates to prevent freezing
        QRectF updateRect = m_pixmapItem->boundingRect();
        m_pixmapItem->update(updateRect);
        m_scene->invalidate(updateRect, QGraphicsScene::ForegroundLayer);
        m_scene->update(updateRect);
        viewport()->update();
        
        return;
    }

    // FALLBACK: Non-viewport-sized frame - let transform pipeline handle it,
    // but keep pixmap DPR consistent and disable item caching to avoid stale scaled cache.
    if (!m_pixmapItem) {
        m_pixmapItem = m_scene->addPixmap(local);
        m_pixmapItem->setZValue(2);
        m_pixmapItem->setVisible(true);
        m_pixmapItem->setTransformationMode(Qt::SmoothTransformation);
        m_pixmapItem->setCacheMode(QGraphicsItem::NoCache);
    } else {
        m_pixmapItem->setPixmap(local);
    }
    if (m_videoItem) m_videoItem->setVisible(false);

    updateVideoItemTransform();
    centerVideoItem();
    updateScrollBarsAndSceneRect();
    
    // CRITICAL FIX: Force updates in fallback path to prevent freezing
    QRectF updateRect = m_pixmapItem->boundingRect();
    m_pixmapItem->update(updateRect);
    m_scene->invalidate(updateRect, QGraphicsScene::ForegroundLayer);
    m_scene->update(updateRect);
    viewport()->update();
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

void VideoPane::clearVideoFrame()
{
    qWarning() << "VideoPane: Clearing current video frame";
    
    if (m_pixmapItem) {
        // Create a black pixmap of the same size as the current one or default size
        QSize size = m_pixmapItem->pixmap().size();
        if (size.isEmpty()) {
            size = QSize(640, 480); // Default size
        }
        QPixmap blackPixmap(size);
        blackPixmap.fill(Qt::black);
        m_pixmapItem->setPixmap(blackPixmap);
        
        // Force update
        if (m_scene) {
            m_scene->invalidate(m_pixmapItem->boundingRect());
            m_scene->update();
        }
        update();
        
        qCDebug(log_ui_video) << "VideoPane: Cleared pixmap item with black frame";
    }
}

void VideoPane::onCameraActiveChanged(bool active)
{
    qWarning() << "VideoPane: Camera active changed:" << active;
    
    if (!active) {
        // Camera deactivated, clear the current frame
        qWarning() << "VideoPane: Clearing video frame due to camera deactivation";
        clearVideoFrame();
        qWarning() << "VideoPane: Video frame cleared";
    }else{
        std::thread([this]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            QMetaObject::invokeMethod(this, [this]() { 
                qCInfo(log_ui_video) << "VideoPane: Calling fitToWindow after camera activation";
                this->fitToWindow(); 
            }, Qt::QueuedConnection);
        }).detach();
    }
}

