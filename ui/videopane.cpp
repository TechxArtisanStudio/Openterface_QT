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

VideoPane::VideoPane(QWidget *parent) : QGraphicsView(parent), 
    escTimer(new QTimer(this)), 
    m_inputHandler(new InputHandler(this, this)), 
    m_isCameraSwitching(false),
    m_scene(new QGraphicsScene(this)),
    m_videoItem(nullptr),
    m_pixmapItem(nullptr),
    m_aspectRatioMode(Qt::KeepAspectRatio),
    m_scaleFactor(1.0),
    m_maintainAspectRatio(true)
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
    qCDebug(log_ui_video) << "VideoPane: Camera switching from" << fromDevice << "to" << toDevice;
    
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
    
    // Ensure video item is visible and pixmap item is hidden
    if (m_videoItem) {
        m_videoItem->setVisible(true);
        qDebug() << "VideoPane: Video item made visible for new camera feed";
    }
    
    if (m_pixmapItem) {
        m_pixmapItem->setVisible(false);
        qDebug() << "VideoPane: Pixmap item hidden to show live video";
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
    } else {
        // Normal video display
        if (m_pixmapItem) {
            m_pixmapItem->setVisible(false);
        }
        if (m_videoItem) {
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
}

// Helper methods
void VideoPane::updateVideoItemTransform()
{
    if (!m_videoItem) return;
    
    QRectF itemRect = m_videoItem->boundingRect();
    QRectF viewRect = viewport()->rect();
    
    if (itemRect.isEmpty() || viewRect.isEmpty()) return;
    qDebug() << "Updating video item transform with itemRect:" << itemRect << "viewRect:" << viewRect;
    
    // Reset transform and position first
    m_videoItem->setTransform(QTransform());
    m_videoItem->setPos(0, 0);
    
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
        m_videoItem->setTransform(transform);
        
        // Center the item after scaling, accounting for the original offset
        QRectF scaledRect = QRectF(0, 0, normalizedRect.width() * scale, normalizedRect.height() * scale);
        double x = (viewRect.width() - scaledRect.width()) / 2.0 - (itemOffset.x() * scale);
        double y = (viewRect.height() - scaledRect.height()) / 2.0 - (itemOffset.y() * scale);
        m_videoItem->setPos(x, y);
        qDebug() << "Video item transformed with scale:" << scale << "at position:" << QPointF(x, y) << "offset:" << itemOffset;
    } else {
        // Stretch to fill (ignore aspect ratio)
        QTransform transform;
        transform.scale(viewRect.width() / normalizedRect.width(), 
                       viewRect.height() / normalizedRect.height());
        m_videoItem->setTransform(transform);
        // Account for the original offset when stretching
        m_videoItem->setPos(-itemOffset.x(), -itemOffset.y());
    }
}

void VideoPane::centerVideoItem()
{
    if (!m_videoItem) return;
    
    QRectF itemRect = m_videoItem->boundingRect();
    QRectF viewRect = viewport()->rect();
    
    // Normalize the item rectangle and get the original offset
    QRectF normalizedRect(0, 0, itemRect.width(), itemRect.height());
    QPointF itemOffset = itemRect.topLeft();
    
    // Get the current transform to calculate the scaled size
    QTransform transform = m_videoItem->transform();
    QRectF scaledRect = transform.mapRect(normalizedRect);
    
    // Center the item accounting for the original offset
    double x = (viewRect.width() - scaledRect.width()) / 2.0 - (itemOffset.x() * transform.m11());
    double y = (viewRect.height() - scaledRect.height()) / 2.0 - (itemOffset.y() * transform.m22());
    
    m_videoItem->setPos(x, y);
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
    if (!m_videoItem) {
        return viewportPos;
    }
    
    // Convert viewport coordinates to scene coordinates
    QPointF scenePos = mapToScene(viewportPos);
    
    // Map scene coordinates to video item coordinates
    QPointF itemPos = m_videoItem->mapFromScene(scenePos);
    
    // Get the video item's bounding rect
    QRectF itemRect = m_videoItem->boundingRect();
    
    // Normalize coordinates to 0-1 range based on the video content area
    double normalizedX = 0.0;
    double normalizedY = 0.0;
    
    if (itemRect.width() > 0 && itemRect.height() > 0) {
        // Account for any offset in the bounding rect
        double relativeX = (itemPos.x() - itemRect.x()) / itemRect.width();
        double relativeY = (itemPos.y() - itemRect.y()) / itemRect.height();
        
        // Clamp to 0-1 range
        normalizedX = qBound(0.0, relativeX, 1.0);
        normalizedY = qBound(0.0, relativeY, 1.0);
    }
    
    // Convert normalized coordinates back to viewport coordinates for the logical video area
    QRectF viewRect = viewport()->rect();
    int transformedX = static_cast<int>(normalizedX * viewRect.width());
    int transformedY = static_cast<int>(normalizedY * viewRect.height());
    
    qDebug() << "VideoPane: Transformed mouse pos from" << viewportPos 
             << "to" << QPoint(transformedX, transformedY)
             << "via scene:" << scenePos << "item:" << itemPos 
             << "normalized:" << normalizedX << normalizedY;
    
    return QPoint(transformedX, transformedY);
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
    qDebug() << "VideoPane::mousePressEvent - pos:" << event->pos();
    
    // Transform the mouse position to account for zoom and pan
    QPoint transformedPos = getTransformedMousePosition(event->pos());
    QMouseEvent transformedEvent(event->type(), transformedPos, event->globalPos(), 
                                event->button(), event->buttons(), event->modifiers());
    
    // Emit signal for status bar update
    emit mouseMoved(transformedPos, "Press");
    
    // Call InputHandler's public method to process the event
    if (m_inputHandler) {
        m_inputHandler->handleMousePress(&transformedEvent);
    }
    
    // Let the base class handle the event
    QGraphicsView::mousePressEvent(event);
}

void VideoPane::mouseMoveEvent(QMouseEvent *event)
{
    qDebug() << "VideoPane::mouseMoveEvent - pos:" << event->pos() << "mouse tracking:" << hasMouseTracking();
    
    // Transform the mouse position to account for zoom and pan
    QPoint transformedPos = getTransformedMousePosition(event->pos());
    QMouseEvent transformedEvent(event->type(), transformedPos, event->globalPos(), 
                                event->button(), event->buttons(), event->modifiers());
    
    // Emit signal for status bar update
    emit mouseMoved(transformedPos, "Move");
    
    // Call InputHandler's public method to process the event
    if (m_inputHandler) {
        m_inputHandler->handleMouseMove(&transformedEvent);
    }
    
    // Let the base class handle the event
    QGraphicsView::mouseMoveEvent(event);
}

void VideoPane::mouseReleaseEvent(QMouseEvent *event)
{
    qDebug() << "VideoPane::mouseReleaseEvent - pos:" << event->pos();
    
    // Transform the mouse position to account for zoom and pan
    QPoint transformedPos = getTransformedMousePosition(event->pos());
    QMouseEvent transformedEvent(event->type(), transformedPos, event->globalPos(), 
                                event->button(), event->buttons(), event->modifiers());
    
    // Emit signal for status bar update
    emit mouseMoved(transformedPos, "Release");
    
    // Call InputHandler's public method to process the event
    if (m_inputHandler) {
        m_inputHandler->handleMouseRelease(&transformedEvent);
    }
    
    // Let the base class handle the event
    QGraphicsView::mouseReleaseEvent(event);
}
