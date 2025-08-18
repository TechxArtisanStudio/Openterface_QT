#include "inputhandler.h"
#include "videopane.h"
#include "host/HostManager.h"
#include "../global.h"
#include <QGuiApplication>
#include <QScreen>

InputHandler::InputHandler(VideoPane *videoPane, QObject *parent)
    : QObject(parent), m_videoPane(videoPane), m_currentEventTarget(nullptr)
{
    if (m_videoPane) {
        m_videoPane->installEventFilter(this);
        m_currentEventTarget = m_videoPane;
    }
}

MouseEventDTO* InputHandler::calculateMouseEventDto(QMouseEvent *event)
{
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

    lastX = event->position().x();
    lastY = event->position().y();
    
    return new MouseEventDTO(relX, relY, false);
}

MouseEventDTO* InputHandler::calculateAbsolutePosition(QMouseEvent *event) {
    // Get the effective video widget (overlay or main VideoPane)
    QWidget* effectiveWidget = getEffectiveVideoWidget();
    
    // Transform mouse position if needed
    QPoint transformedPos = transformMousePosition(event, effectiveWidget);
    
    qreal absoluteX = static_cast<qreal>(transformedPos.x()) / effectiveWidget->width() * 4096;
    qreal absoluteY = static_cast<qreal>(transformedPos.y()) / effectiveWidget->height() * 4096;
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
    // Debug: Log all events to see what we're receiving
    if (watched == m_videoPane || watched == m_currentEventTarget) {
        qDebug() << "InputHandler::eventFilter - Event type:" << event->type() 
                 << "watched object:" << watched 
                 << "current target:" << m_currentEventTarget
                 << "GStreamer mode:" << (m_videoPane ? m_videoPane->isDirectGStreamerModeEnabled() : false);
    }
    
    if (event->type() == QEvent::MouseMove) {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
        qDebug() << "InputHandler::eventFilter - MouseMove detected, calling handleMouseMoveEvent";
        handleMouseMoveEvent(mouseEvent);
        return true;
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
            qDebug() << "Mouse entered VideoPane - hiding cursor";
        }
    }
    if (event->type() == QEvent::Leave) {
        if (GlobalVar::instance().isMouseAutoHideEnabled() && m_videoPane) {
            m_videoPane->setCursor(Qt::ArrowCursor);
            qDebug() << "Mouse left VideoPane - showing cursor";
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
    QScopedPointer<MouseEventDTO> eventDto(calculateMouseEventDto(event));
    eventDto->setMouseButton(isDragging() ? lastMouseButton : 0);

    qDebug() << "InputHandler::handleMouseMoveEvent - pos:" << event->pos() 
             << "absolute mode:" << eventDto->isAbsoluteMode() 
             << "relative mode enabled:" << m_videoPane->isRelativeModeEnabled()
             << "x:" << eventDto->getX() << "y:" << eventDto->getY();

    //Only handle the event if it's under absolute mouse control or relative mode is enabled
    if(!eventDto->isAbsoluteMode() && !m_videoPane->isRelativeModeEnabled()) {
        qDebug() << "InputHandler: Mouse move event rejected - not in correct mode";
        return;
    }

    HostManager::getInstance().handleMouseMove(eventDto.get());
}

void InputHandler::handleMousePressEvent(QMouseEvent* event)
{
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
        qDebug() << "Esc Pressed, timer started";
        m_holdingEsc = true;
        m_videoPane->startEscTimer();
    }
}

void InputHandler::handleKeyReleaseEvent(QKeyEvent *event)
{
    HostManager::getInstance().handleKeyRelease(event);

    if(m_holdingEsc && event->key() == Qt::Key_Escape && !GlobalVar::instance().isAbsoluteMouseMode()) {
        qDebug() << "Esc Released, timer stop";
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
            qDebug() << "InputHandler: Switched event filter to GStreamer overlay widget";
        }
    } else {
        // Switch back to main VideoPane
        if (m_currentEventTarget != m_videoPane) {
            removeOverlayEventFilter();
            m_videoPane->installEventFilter(this);
            m_currentEventTarget = m_videoPane;
            qDebug() << "InputHandler: Switched event filter back to VideoPane";
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
        
        qDebug() << "InputHandler: Installed event filter on overlay widget";
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
    
    // If the event is from the overlay widget and we need coordinates relative to VideoPane
    if (sourceWidget != m_videoPane && m_videoPane->isDirectGStreamerModeEnabled()) {
        // The overlay widget should have the same coordinate system as the VideoPane
        // since it's positioned to fill the VideoPane, but we still need to transform
        // the coordinates to account for video scaling, zoom, and pan
        return m_videoPane->getTransformedMousePosition(event->pos());
    }
    
    // For events from the VideoPane itself, use the VideoPane's transformation
    return m_videoPane->getTransformedMousePosition(event->pos());
}

QWidget* InputHandler::getEffectiveVideoWidget() const
{
    if (!m_videoPane) {
        return nullptr;
    }
    
    // Return overlay widget if in GStreamer mode, otherwise return VideoPane
    if (m_videoPane->isDirectGStreamerModeEnabled()) {
        QWidget* overlayWidget = m_videoPane->getOverlayWidget();
        if (overlayWidget) {
            return overlayWidget;
        }
    }
    
    return m_videoPane;
}
