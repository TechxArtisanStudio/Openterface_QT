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
#include "../global.h"

#include <QPainter>
#include <QMouseEvent>
#include <QDebug>
#include <QGuiApplication>
#include <QScreen>

VideoPane::VideoPane(QWidget *parent) : QVideoWidget(parent), escTimer(new QTimer(this))
{
    QWidget* childWidget = qobject_cast<QWidget*>(this->children()[0]);
    if(childWidget) {
        childWidget->setMouseTracking(true);
    }
    this->setMouseTracking(true);
    this->installEventFilter(this);
    this->setFocusPolicy(Qt::StrongFocus);
    this->relativeModeEnable = false;

    // Set up the timer
    connect(escTimer, &QTimer::timeout, this, &VideoPane::showHostMouse);
}

MouseEventDTO* VideoPane::calculateMouseEventDto(QMouseEvent *event)
{
    MouseEventDTO* dto = GlobalVar::instance().isAbsoluteMouseMode() ? calculateAbsolutePosition(event) : calculateRelativePosition(event);

    return dto;
}

void VideoPane::mouseMoveEvent(QMouseEvent *event)
{
    MouseEventDTO* eventDto = calculateMouseEventDto(event);
    eventDto->setMouseButton(isDragging ? lastMouseButton : 0);

    //Only handle the event if it's under absolute mouse control or relative mode is enabled
    if(!eventDto->isAbsoluteMode() && !this->relativeModeEnable) return;

    HostManager::getInstance().handleMouseMove(eventDto);
}

void VideoPane::mousePressEvent(QMouseEvent* event)
{
    MouseEventDTO* eventDto = calculateMouseEventDto(event);
    eventDto->setMouseButton(lastMouseButton = getMouseButton(event));
    isDragging = true;

    if(!eventDto->isAbsoluteMode()) relativeModeEnable = true;

    HostManager::getInstance().handleMousePress(eventDto);

    if(eventDto->isAbsoluteMode()){
        showHostMouse();
    }else{
        hideHostMouse();
    }
}

void VideoPane::mouseReleaseEvent(QMouseEvent* event)
{
    MouseEventDTO* eventDto = calculateMouseEventDto(event);
    isDragging = false;
    HostManager::getInstance().handleMouseRelease(eventDto);
}

void VideoPane::wheelEvent(QWheelEvent *event)
{
    MouseEventDTO* eventDto = new MouseEventDTO(lastX, lastY, GlobalVar::instance().isAbsoluteMouseMode());

    eventDto->setWheelDelta(event->angleDelta().y());

    HostManager::getInstance().handleMouseScroll(eventDto);
}

/*
    * This function is called when the focus is on the video pane and the user presses the Tab key.
    * This function is overridden to prevent the focus from moving to the next widget.
*/
bool VideoPane::focusNextPrevChild(bool next) {
    return false;
}

void VideoPane::keyPressEvent(QKeyEvent *event)
{
    HostManager::getInstance().handleKeyPress(event);

    if(!holdingEsc && event->key() == Qt::Key_Escape && !GlobalVar::instance().isAbsoluteMouseMode()) {
        qDebug() << "Esc Pressed, timer started";
        holdingEsc = true;
        escTimer->start(500); // 0.5 seconds
    }

}

void VideoPane::keyReleaseEvent(QKeyEvent *event)
{
    HostManager::getInstance().handleKeyRelease(event);

    if(holdingEsc && event->key() == Qt::Key_Escape && !GlobalVar::instance().isAbsoluteMouseMode()) {
        qDebug() << "Esc Released, timer stop";
        escTimer->stop();
        holdingEsc = false;
    }
}

QSize getScreenResolution() {
    QScreen *screen = QGuiApplication::primaryScreen();
    if (screen) {
        return screen->size();
    } else {
        return QSize(0, 0);
    }
}

MouseEventDTO* VideoPane::calculateRelativePosition(QMouseEvent *event) {
    qreal relativeX = static_cast<qreal>(event->pos().x() - lastX);
    qreal relativeY = static_cast<qreal>(event->pos().y() - lastY);

    QSize screenSize = getScreenResolution();

    qreal widthRatio = static_cast<qreal>(GlobalVar::instance().getWinWidth()) / screenSize.width() ;
    qreal heightRatio = static_cast<qreal>(GlobalVar::instance().getWinHeight()) / screenSize.height();
    
    int relX = static_cast<int>(relativeX * widthRatio);
    int relY = static_cast<int>(relativeY * heightRatio);
    
    lastX=event->position().x();
    lastY=event->position().y();
    
    return new MouseEventDTO(relX, relY, false);
}

MouseEventDTO* VideoPane::calculateAbsolutePosition(QMouseEvent *event) {
    int effectiveWidth = this->width();
    int effectiveHeight = this->height();
    
    // Calculate the video area dimensions
    qreal aspectRatio = static_cast<qreal>(GlobalVar::instance().getCaptureWidth()) / GlobalVar::instance().getCaptureHeight();
    
    if (GlobalVar::instance().isToolbarVisible()) {
        int toolbarHeight = GlobalVar::instance().getToolbarHeight();
        effectiveHeight -= toolbarHeight;
    }
    
    int videoWidth = effectiveWidth;
    int videoHeight = static_cast<int>(videoWidth / aspectRatio);
    
    if (videoHeight > effectiveHeight) {
        videoHeight = effectiveHeight;
        videoWidth = static_cast<int>(videoHeight * aspectRatio);
    }
    
    // Calculate the video area position
    int videoX = (effectiveWidth - videoWidth) / 2;
    int videoY = (effectiveHeight - videoHeight) / 2;
    
    // Adjust mouse position relative to the video area
    qreal relativeX = static_cast<qreal>(event->pos().x() - videoX);
    qreal relativeY = static_cast<qreal>(event->pos().y() - videoY);
    
    qreal absoluteX = (relativeX / videoWidth) * 4096;
    qreal absoluteY = (relativeY / videoHeight) * 4096;
    
    lastX = static_cast<int>(absoluteX);
    lastY = static_cast<int>(absoluteY);
    
    return new MouseEventDTO(lastX, lastY, true);
}

int VideoPane::getMouseButton(QMouseEvent *event) {
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

bool VideoPane::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == this && event->type() == QEvent::Leave && !GlobalVar::instance().isAbsoluteMouseMode() && this->relativeModeEnable) {
        moveMouseToCenter();
    }
    return QWidget::eventFilter(watched, event);
}

void VideoPane::moveMouseToCenter()
{
    // Temporarily disable the mouse event handling
    this->relativeModeEnable = false;

    int effectiveWidth = this->width();
    int effectiveHeight = this->height();
    
    if (GlobalVar::instance().isToolbarVisible()) {
        effectiveHeight -= GlobalVar::instance().getToolbarHeight();
    }

    // Move the mouse to the center of the video area
    QPoint centerPoint(effectiveWidth / 2, effectiveHeight / 2);
    QCursor::setPos(this->mapToGlobal(centerPoint));
    lastX = centerPoint.x();
    lastY = centerPoint.y();

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


