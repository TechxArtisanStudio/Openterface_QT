/*
* ========================================================================== *
*                                                                            *
*    This file is part of the Openterface Mini KVM App QT version            *
*                                                                            *
*    Copyright (C) 2024   <info@openterface.com>                             *
*                                                                            *
*    This program is free software: you can redistribute it and/or modify    *
*    it under the terms of the GNU General Public License as published by    *
*    the Free Software Foundation, either version 3 of the License, or       *
*    (at your option) any later version.                                     *
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
#include <QPainter>
#include <QMouseEvent>
#include <QDebug>

VideoPane::VideoPane(QWidget *parent) : QVideoWidget(parent)
{
    QWidget* childWidget = qobject_cast<QWidget*>(this->children()[0]);
    if(childWidget) {
        childWidget->setMouseTracking(true);
    }
    this->setMouseTracking(true);
    this->installEventFilter(this);
    this->setFocusPolicy(Qt::StrongFocus);
}

void VideoPane::mouseMoveEvent(QMouseEvent *event)
{
    QPoint absPos = calculateRelativePosition(event);
    HostManager::getInstance().handleMouseMove(absPos.x(), absPos.y(), isDragging ? lastMouseButton : 0);
}

void VideoPane::mousePressEvent(QMouseEvent* event)
{
    QPoint absPos = calculateRelativePosition(event);
    lastMouseButton = getMouseButton(event);
    isDragging = true;
    lastX=absPos.x();
    lastY=absPos.y();
    HostManager::getInstance().handleMousePress(lastX, lastY, lastMouseButton);
}

void VideoPane::mouseReleaseEvent(QMouseEvent* event)
{
    QPoint absPos = calculateRelativePosition(event);
    isDragging = false;
    lastX=absPos.x();
    lastY=absPos.y();
    HostManager::getInstance().handleMouseRelease(lastX, lastY);
}

void VideoPane::wheelEvent(QWheelEvent *event)
{
    int delta = event->angleDelta().y();
    HostManager::getInstance().handleMouseScroll(lastX, lastY, delta);
}

void VideoPane::keyPressEvent(QKeyEvent *event)
{
    HostManager::getInstance().handleKeyPress(event);
}

void VideoPane::keyReleaseEvent(QKeyEvent *event)
{
    HostManager::getInstance().handleKeyRelease(event);
}

QPoint VideoPane::calculateRelativePosition(QMouseEvent *event) {
    qreal relativeX = static_cast<qreal>(event->pos().x()) / this->width() * 4096;
    qreal relativeY = static_cast<qreal>(event->pos().y()) / this->height() * 4096;
    return QPoint(static_cast<int>(relativeX), static_cast<int>(relativeY));
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
