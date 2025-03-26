#include "inputhandler.h"
#include "videopane.h"
#include "host/HostManager.h"
#include "../global.h"
#include <QGuiApplication>
#include <QScreen>

InputHandler::InputHandler(VideoPane *videoPane, QObject *parent)
    : QObject(parent), m_videoPane(videoPane)
{
    if (m_videoPane) {
        m_videoPane->installEventFilter(this);
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
    qreal absoluteX = static_cast<qreal>(event->pos().x()) / m_videoPane->width() * 4096;
    qreal absoluteY = static_cast<qreal>(event->pos().y()) / m_videoPane->height() * 4096;
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
    if (event->type() == QEvent::MouseMove) {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
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
    if (watched == m_videoPane && event->type() == QEvent::KeyPress) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent*>(event);
        handleKeyPressEvent(keyEvent);
        return true;
    }
    if (watched == m_videoPane && event->type() == QEvent::KeyRelease) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent*>(event);
        handleKeyReleaseEvent(keyEvent);
        return true;
    }
    if (watched == m_videoPane && event->type() == QEvent::Leave) {
        if (!GlobalVar::instance().isAbsoluteMouseMode() && m_videoPane->isRelativeModeEnabled()) {
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

    //Only handle the event if it's under absolute mouse control or relative mode is enabled
    if(!eventDto->isAbsoluteMode() && !m_videoPane->isRelativeModeEnabled()) return;

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
