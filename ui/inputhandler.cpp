#include "inputhandler.h"
#include "videopane.h"
#include "host/HostManager.h"
#include "../global.h"
#include <QGuiApplication>
#include <QScreen>
#include <QDateTime>

/*
 * CRITICAL FIX for maximize screen crash:
 * 
 * ROOT CAUSE: Debug logging was calling m_videoPane->isDirectGStreamerModeEnabled() 
 * for EVERY event, including MetaCall events. During window maximize/resize, VideoPane
 * is in an inconsistent state and calling its methods causes a segmentation fault.
 * 
 * FIXES APPLIED:
 * 1. Removed method calls from debug logging - only check if pointer is null, don't call methods
 * 2. Using QPointer instead of raw pointers for automatic null safety
 * 3. Filtering out internal Qt events (MetaCall, Timer, Paint, etc.) - don't process them
 * 4. Added multiple null checks before accessing VideoPane
 * 
 * The crash happened because:
 * - Qt delivers MetaCall event to InputHandler during window state change
 * - Debug log tries to call isDirectGStreamerModeEnabled() on VideoPane
 * - VideoPane is being resized/repainted and is in inconsistent state
 * - Method call on inconsistent object -> SEGFAULT
 */

Q_LOGGING_CATEGORY(log_ui_input, "opf.ui.input")

InputHandler::InputHandler(VideoPane *videoPane, QObject *parent)
    : QObject(parent), m_videoPane(videoPane), m_currentEventTarget(nullptr),
      m_lastMouseMoveTime(0), m_mouseMoveInterval(16), m_droppedMouseEvents(0)
{
    if (m_videoPane) {
        m_videoPane->installEventFilter(this);
        m_currentEventTarget = m_videoPane;
    }
}

MouseEventDTO* InputHandler::calculateMouseEventDto(QMouseEvent *event)
{
    if (!m_videoPane) {
        qCWarning(log_ui_input) << "InputHandler::calculateMouseEventDto - m_videoPane is null!";
        return new MouseEventDTO(0, 0, GlobalVar::instance().isAbsoluteMouseMode());
    }
    
    MouseEventDTO* dto = GlobalVar::instance().isAbsoluteMouseMode() ? calculateAbsolutePosition(event) : calculateRelativePosition(event);
    dto->setMouseButton(m_isDragging ? lastMouseButton : 0);
    return dto;
}

MouseEventDTO* InputHandler::calculateRelativePosition(QMouseEvent *event) {
    qreal relativeX = static_cast<qreal>(event->pos().x() - lastX);
    qreal relativeY = static_cast<qreal>(event->pos().y() - lastY);

    QSize screenSize = getScreenResolution();

    qreal widthRatio = static_cast<qreal>(GlobalVar::instance().getWinWidth()) / screenSize.width();
    qreal heightRatio = static_cast<qreal>(GlobalVar::instance().getWinHeight()) / screenSize.height();

    int relX = static_cast<int>(relativeX * widthRatio);
    int relY = static_cast<int>(relativeY * heightRatio);

    // Use consistent position access - use pos() instead of position()
    lastX = event->pos().x();
    lastY = event->pos().y();
    
    return new MouseEventDTO(relX, relY, false);
}

MouseEventDTO* InputHandler::calculateAbsolutePosition(QMouseEvent *event) {
    // Get the effective video widget (overlay or main VideoPane)
    QWidget* effectiveWidget = getEffectiveVideoWidget();
    
    // SAFETY: Check if we have a valid widget
    if (!effectiveWidget || effectiveWidget->width() == 0 || effectiveWidget->height() == 0) {
        qCWarning(log_ui_input) << "InputHandler::calculateAbsolutePosition - Invalid widget state:"
                                << "widget=" << effectiveWidget
                                << "size=" << (effectiveWidget ? effectiveWidget->size() : QSize(0,0));
        return new MouseEventDTO(0, 0, true);
    }
    
    // Transform mouse position if needed
    QPoint transformedPos = transformMousePosition(event, effectiveWidget);
    
    // Use VideoPane's coordinate transformation for consistency
    QPoint videoPos = transformedPos;
    if (m_videoPane && effectiveWidget == m_videoPane) {
        // Use VideoPane's transformation logic for accurate video area mapping
        videoPos = m_videoPane->getTransformedMousePosition(transformedPos);
        
        // Debug: Log coordinate transformation details
        static int logCounter = 0;
        if (++logCounter % 20 == 1) { // Log every 20th event to reduce spam
            // qCDebug(log_ui_input) << "InputHandler::calculateAbsolutePosition:"
            //                       << "original=" << event->pos()
            //                       << "transformed=" << transformedPos
            //                       << "video-transformed=" << videoPos
            //                       << "widget size=" << effectiveWidget->size();
        }
    }
    
    qreal absoluteX = static_cast<qreal>(videoPos.x()) / effectiveWidget->width() * 4096;
    qreal absoluteY = static_cast<qreal>(videoPos.y()) / effectiveWidget->height() * 4096;
    lastX = static_cast<int>(absoluteX);
    lastY = static_cast<int>(absoluteY);
    return new MouseEventDTO(lastX, lastY, true);
}

int InputHandler::getMouseButton(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        return 1;
    } else if (event->button() == Qt::RightButton) {
        return 2;
    } else if (event->button() == Qt::MiddleButton) {
        return 4;
    } else {
        return 0;
    }
}

QSize InputHandler::getScreenResolution() {
    QScreen *screen = QGuiApplication::primaryScreen();
    if (screen) {
        return screen->size();
    } else {
        return QSize(0, 0);
    }
}

bool InputHandler::eventFilter(QObject *watched, QEvent *event)
{
    // CRITICAL SAFETY: Exit early if event processing is disabled
    if (!m_processingEnabled) {
        return QObject::eventFilter(watched, event);
    }
    
    // CRITICAL SAFETY: Check if VideoPane is valid FIRST before any other checks
    // QPointer will be null if the object is destroyed or in an invalid state
    if (m_videoPane.isNull() && m_currentEventTarget.isNull()) {
        qCWarning(log_ui_input) << "InputHandler::eventFilter - Both videoPane and currentEventTarget are null!";
        return QObject::eventFilter(watched, event);
    }
    
    // CRITICAL SAFETY: Ignore internal Qt events that could cause issues during state changes
    // MetaCall, Timer, ChildAdded, ChildRemoved, etc. should be passed through without processing
    // MetaCall is particularly dangerous as it can access object methods during state transitions
    if (event->type() == QEvent::MetaCall || 
        event->type() == QEvent::Timer ||
        event->type() == QEvent::ChildAdded ||
        event->type() == QEvent::ChildRemoved ||
        event->type() == QEvent::ChildPolished ||
        event->type() == QEvent::DeferredDelete ||
        event->type() == QEvent::Paint ||           // Don't intercept paint events
        event->type() == QEvent::UpdateRequest ||   // Don't intercept update requests
        event->type() == QEvent::LayoutRequest) {   // Don't intercept layout requests
        // Log MetaCall for debugging but don't process it
        if (event->type() == QEvent::MetaCall) {
            static int metacallCount = 0;
            if (++metacallCount % 50 == 1) {
                qCDebug(log_ui_input) << "InputHandler::eventFilter - Passing through MetaCall event (not processing)";
            }
        }
        return QObject::eventFilter(watched, event);
    }
    
    // CRITICAL SAFETY: Check if watched object is valid and matches our expected targets
    if (!watched) {
        qCWarning(log_ui_input) << "InputHandler::eventFilter - watched object is null!";
        return QObject::eventFilter(watched, event);
    }
    
    // SAFETY: Verify the watched object is one we're tracking
    // Use data() to get raw pointer from QPointer for comparison
    QObject* videoPaneObj = m_videoPane.data();
    QObject* targetObj = m_currentEventTarget.data();
    
    bool isValidTarget = (watched == videoPaneObj || watched == targetObj);
    if (!isValidTarget) {
        // Not our target, pass through
        return QObject::eventFilter(watched, event);
    }
    
    // ADDITIONAL SAFETY: Verify VideoPane hasn't become invalid between checks
    if (m_videoPane.isNull()) {
        qCWarning(log_ui_input) << "InputHandler::eventFilter - VideoPane became null during event processing!";
        return QObject::eventFilter(watched, event);
    }
    
    // PERFORMANCE: Fast path for mouse move events - avoid expensive logging and checks
    if (event->type() == QEvent::MouseMove) {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
        handleMouseMoveEvent(mouseEvent);
        return true;
    }
    
    // PERFORMANCE: Reduce excessive debug logging for other mouse events
    // Only log non-mouse events and first few mouse events for debugging
    static int mouseEventCount = 0;
    bool isMouseEvent = (event->type() == QEvent::MouseButtonPress || 
                        event->type() == QEvent::MouseButtonRelease);
    
    if (isMouseEvent) {
        mouseEventCount++;
        // Only log first 10 mouse events to reduce debug spam
        if (mouseEventCount <= 10) {
            qCDebug(log_ui_input) << "InputHandler::eventFilter - Event type:" << event->type() 
                     << "watched object:" << watched 
                     << "VideoPane valid:" << !m_videoPane.isNull()
                     << "(logging limited for performance)";
        }
    } else {
        // Log non-mouse events normally (but less frequently)
        static int nonMouseEventCount = 0;
        if (++nonMouseEventCount % 100 == 1) {
            qCDebug(log_ui_input) << "InputHandler::eventFilter - Event type:" << event->type() 
                     << "watched object:" << watched 
                     << "VideoPane valid:" << !m_videoPane.isNull();
        }
    }
    if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonDblClick) {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
        handleMousePressEvent(mouseEvent);
        return true;
    }
    if (event->type() == QEvent::MouseButtonRelease) {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
        handleMouseReleaseEvent(mouseEvent);
        return true;
    }
    if (event->type() == QEvent::Wheel) {
        QWheelEvent *wheelEvent = static_cast<QWheelEvent*>(event);
        handleWheelEvent(wheelEvent);
        return true;
    }
    if (event->type() == QEvent::Enter) {
        if (GlobalVar::instance().isMouseAutoHideEnabled() && m_videoPane) {
            m_videoPane->setCursor(Qt::BlankCursor);
            qCDebug(log_ui_input) << "Mouse entered VideoPane - hiding cursor";
        }
    }
    if (event->type() == QEvent::Leave) {
        if (GlobalVar::instance().isMouseAutoHideEnabled() && m_videoPane) {
            m_videoPane->setCursor(Qt::ArrowCursor);
            qCDebug(log_ui_input) << "Mouse left VideoPane - showing cursor";
        }
    }
    if ((watched == m_videoPane || watched == m_currentEventTarget) && event->type() == QEvent::KeyPress) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent*>(event);
        if (!keyEvent->isAutoRepeat()){
            handleKeyPressEvent(keyEvent);
            return true;
        }
    }
    if ((watched == m_videoPane || watched == m_currentEventTarget) && event->type() == QEvent::KeyRelease) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent*>(event);
        if (!keyEvent->isAutoRepeat()){
            handleKeyReleaseEvent(keyEvent);
            return true;
        }
    }
    if ((watched == m_videoPane || watched == m_currentEventTarget) && event->type() == QEvent::Leave) {
        if (!GlobalVar::instance().isAbsoluteMouseMode() && m_videoPane && m_videoPane->isRelativeModeEnabled()) {
            m_videoPane->moveMouseToCenter();
            return true;
        }
    }
    return QObject::eventFilter(watched, event);
}

void InputHandler::handleMouseMoveEvent(QMouseEvent *event)
{
    // SAFETY: Check if VideoPane is still valid
    if (!m_videoPane) {
        qCWarning(log_ui_input) << "InputHandler::handleMouseMoveEvent - m_videoPane is null!";
        return;
    }
    
    // PERFORMANCE OPTIMIZATION: Adaptive mouse throttling to reduce CPU usage
    // High-frequency mouse movements can cause excessive CPU load, especially on Pi
    qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
    
    // ADAPTIVE THROTTLING: Adjust interval based on recent event frequency
    static int recentEventCount = 0;
    static qint64 lastIntervalAdjustment = 0;
    recentEventCount++;
    
    // Every 2 seconds, adjust throttling based on event frequency
    if (currentTime - lastIntervalAdjustment > 2000) {
        if (recentEventCount > 200) {
            // Very high frequency - increase throttling (reduce responsiveness to save CPU)
            m_mouseMoveInterval = qMin(50, m_mouseMoveInterval + 5); // Max 20 FPS
        } else if (recentEventCount > 100) {
            // High frequency - moderate throttling
            m_mouseMoveInterval = 25; // 40 FPS
        } else if (recentEventCount > 50) {
            // Normal frequency - standard throttling
            m_mouseMoveInterval = 16; // ~62 FPS
        } else {
            // Low frequency - minimal throttling for better responsiveness
            m_mouseMoveInterval = qMax(8, m_mouseMoveInterval - 2); // Max ~125 FPS
        }
        
        // Log throttling adjustments occasionally
        static int adjustmentCount = 0;
        if (++adjustmentCount % 10 == 1) {
            qCDebug(log_ui_input) << "InputHandler: Adaptive throttling - events in 2s:" << recentEventCount 
                                  << "new interval:" << m_mouseMoveInterval << "ms";
        }
        
        recentEventCount = 0;
        lastIntervalAdjustment = currentTime;
    }
    
    // Skip mouse move if it's too soon since the last one (throttling)
    if (currentTime - m_lastMouseMoveTime < m_mouseMoveInterval) {
        m_droppedMouseEvents++;
        
        // Log dropped events occasionally for monitoring (less frequent than before)
        if (m_droppedMouseEvents % 2000 == 0) {
            qCDebug(log_ui_input) << "InputHandler: Dropped" << m_droppedMouseEvents 
                                  << "mouse move events for performance (current interval:" 
                                  << m_mouseMoveInterval << "ms)";
        }
        return; // Drop this mouse move event
    }
    
    m_lastMouseMoveTime = currentTime;
    
    QScopedPointer<MouseEventDTO> eventDto(calculateMouseEventDto(event));
    eventDto->setMouseButton(isDragging() ? lastMouseButton : 0);

    // qDebug() << "InputHandler::handleMouseMoveEvent - pos:" << event->pos() 
    //          << "absolute mode:" << eventDto->isAbsoluteMode() 
    //          << "relative mode enabled:" << m_videoPane->isRelativeModeEnabled()
    //          << "x:" << eventDto->getX() << "y:" << eventDto->getY();

    //Only handle the event if it's under absolute mouse control or relative mode is enabled
    if(!eventDto->isAbsoluteMode() && !m_videoPane->isRelativeModeEnabled()) {
        qCDebug(log_ui_input) << "InputHandler: Mouse move event rejected - not in correct mode";
        return;
    }

    HostManager::getInstance().handleMouseMove(eventDto.get());
}

void InputHandler::handleMousePressEvent(QMouseEvent* event)
{
    if (!m_videoPane) {
        qCWarning(log_ui_input) << "InputHandler::handleMousePressEvent - m_videoPane is null!";
        return;
    }
    
    QScopedPointer<MouseEventDTO> eventDto(calculateMouseEventDto(event));
    eventDto->setMouseButton(lastMouseButton = getMouseButton(event));
    setDragging(true);

    if(!eventDto->isAbsoluteMode()) m_videoPane->setRelativeModeEnabled(true);

    HostManager::getInstance().handleMousePress(eventDto.get());

    if(eventDto->isAbsoluteMode()){
        m_videoPane->showHostMouse();
    }else{
        m_videoPane->hideHostMouse();
    }
}

void InputHandler::handleMouseReleaseEvent(QMouseEvent* event)
{
    if (!m_videoPane) {
        qCWarning(log_ui_input) << "InputHandler::handleMouseReleaseEvent - m_videoPane is null!";
        return;
    }
    
    QScopedPointer<MouseEventDTO> eventDto(calculateMouseEventDto(event));
    setDragging(false);
    HostManager::getInstance().handleMouseRelease(eventDto.get());
    if(eventDto->isAbsoluteMode()){
        m_videoPane->showHostMouse();
    }else{
        m_videoPane->hideHostMouse();
    }
}

void InputHandler::handleWheelEvent(QWheelEvent *event)
{
    QScopedPointer<MouseEventDTO> eventDto(new MouseEventDTO(lastX, lastY, GlobalVar::instance().isAbsoluteMouseMode()));

    eventDto->setWheelDelta(event->angleDelta().y());

    HostManager::getInstance().handleMouseScroll(eventDto.get());
}

void InputHandler::setDragging(bool dragging)
{
    m_isDragging = dragging;
}

void InputHandler::handleKeyPressEvent(QKeyEvent *event)
{
    HostManager::getInstance().handleKeyPress(event);

    if(!m_holdingEsc && event->key() == Qt::Key_Escape && !GlobalVar::instance().isAbsoluteMouseMode()) {
        if (!m_videoPane) {
            qCWarning(log_ui_input) << "InputHandler::handleKeyPressEvent - m_videoPane is null!";
            return;
        }
        qCDebug(log_ui_input) << "Esc Pressed, timer started";
        m_holdingEsc = true;
        m_videoPane->startEscTimer();
    }
}

void InputHandler::handleKeyReleaseEvent(QKeyEvent *event)
{
    HostManager::getInstance().handleKeyRelease(event);

    if(m_holdingEsc && event->key() == Qt::Key_Escape && !GlobalVar::instance().isAbsoluteMouseMode()) {
        if (!m_videoPane) {
            qCWarning(log_ui_input) << "InputHandler::handleKeyReleaseEvent - m_videoPane is null!";
            return;
        }
        qCDebug(log_ui_input) << "Esc Released, timer stop";
        m_videoPane->stopEscTimer();
        m_holdingEsc = false;
    }
}

// Public methods that delegate to private event handlers
void InputHandler::handleKeyPress(QKeyEvent *event)
{
    handleKeyPressEvent(event);
}

void InputHandler::handleKeyRelease(QKeyEvent *event)
{
    handleKeyReleaseEvent(event);
}

void InputHandler::handleMousePress(QMouseEvent *event)
{
    handleMousePressEvent(event);
}

void InputHandler::handleMouseMove(QMouseEvent *event)
{
    handleMouseMoveEvent(event);
}

void InputHandler::handleMouseRelease(QMouseEvent *event)
{
    handleMouseReleaseEvent(event);
}

// Methods to handle GStreamer overlay widget events
void InputHandler::updateEventFilterTarget()
{
    if (!m_videoPane) return;
    
    QWidget* overlayWidget = m_videoPane->getOverlayWidget();
    
    if (m_videoPane->isDirectGStreamerModeEnabled() && overlayWidget) {
        // Switch to overlay widget if GStreamer mode is enabled
        if (m_currentEventTarget != overlayWidget) {
            removeOverlayEventFilter();
            installOverlayEventFilter(overlayWidget);
            qCDebug(log_ui_input) << "InputHandler: Switched event filter to GStreamer overlay widget";
        }
    } else {
        // Switch back to main VideoPane
        if (m_currentEventTarget != m_videoPane) {
            removeOverlayEventFilter();
            m_videoPane->installEventFilter(this);
            m_currentEventTarget = m_videoPane;
            qCDebug(log_ui_input) << "InputHandler: Switched event filter back to VideoPane";
        }
    }
}

void InputHandler::installOverlayEventFilter(QWidget* overlayWidget)
{
    if (overlayWidget && overlayWidget != m_currentEventTarget) {
        // Remove from previous target
        if (m_currentEventTarget) {
            m_currentEventTarget->removeEventFilter(this);
        }
        
        // Install on overlay widget
        overlayWidget->installEventFilter(this);
        overlayWidget->setMouseTracking(true);
        overlayWidget->setFocusPolicy(Qt::StrongFocus);
        m_currentEventTarget = overlayWidget;
        
        qCDebug(log_ui_input) << "InputHandler: Installed event filter on overlay widget";
    }
}

void InputHandler::removeOverlayEventFilter()
{
    if (m_currentEventTarget) {
        m_currentEventTarget->removeEventFilter(this);
        m_currentEventTarget = nullptr;
    }
}

// Helper methods for coordinate transformation
QPoint InputHandler::transformMousePosition(QMouseEvent *event, QWidget* sourceWidget)
{
    if (!sourceWidget || !m_videoPane) {
        return event->pos();
    }
    
    // Always use VideoPane's transformation logic for consistency
    // regardless of whether we're in GStreamer mode or not
    if (sourceWidget == m_videoPane) {
        // For VideoPane events, use the direct position - VideoPane handles its own transformation
        return event->pos();
    } else if (m_videoPane->isDirectGStreamerModeEnabled() && sourceWidget != m_videoPane) {
        // For overlay widget events in GStreamer mode, convert to VideoPane coordinates
        // The overlay widget should have the same coordinate system as the VideoPane
        // since it's positioned to fill the VideoPane
        return event->pos();
    }
    
    return event->pos();
}

QWidget* InputHandler::getEffectiveVideoWidget() const
{
    // QPointer automatically becomes null if the object is destroyed
    if (m_videoPane.isNull()) {
        qCWarning(log_ui_input) << "InputHandler::getEffectiveVideoWidget - m_videoPane is null or destroyed!";
        return nullptr;
    }
    
    // Return overlay widget if in GStreamer mode, otherwise return VideoPane
    if (m_videoPane->isDirectGStreamerModeEnabled()) {
        QWidget* overlayWidget = m_videoPane->getOverlayWidget();
        if (overlayWidget && overlayWidget->isVisible()) {
            return overlayWidget;
        }
    }
    
    return m_videoPane.data();  // Get raw pointer from QPointer
}
