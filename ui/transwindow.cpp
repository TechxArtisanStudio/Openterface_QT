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

#include "transwindow.h"
#include <QApplication>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsRectItem>
#include <QRegion>
#include <QEvent>

TransWindow::TransWindow(QWidget *parent) :
    QDialog(parent),
    escTimer(new QTimer(this))
{
    this->setMouseTracking(true);
    this->setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);

    // Set up the timer
    connect(escTimer, &QTimer::timeout, this, &TransWindow::close);
}

TransWindow::~TransWindow()
{
}

void TransWindow::updateGeometry(QRect *geometry)
{


    this->setWindowOpacity(0.5);
    QRegion region(this->geometry());
    qDebug() << "geometry:  " << region << " mask: " << geometry;
    // Subtract the entire window area from the region
    region = region.subtracted(*geometry);

    // Set the mask
    this->setMask(region);
}

void TransWindow::mouseMoveEvent(QMouseEvent *event)
{
    // Handle mouse move event here
    // For example, you can print the mouse position:
    qDebug() << "Transparent Window mouse moved to position:" << event->pos();
}

void TransWindow::keyPressEvent(QKeyEvent *event)
{

    if(!holdingEsc && event->key() == Qt::Key_Escape) {
        qDebug() << "Esc Pressed, timer started";
        holdingEsc = true;
        escTimer->start(500); // 0.5 seconds
    }
}

void TransWindow::keyReleaseEvent(QKeyEvent *event)
{
    if(holdingEsc && event->key() == Qt::Key_Escape) {
        qDebug() << "Esc Released, timer stop";
        escTimer->stop();
        holdingEsc = false;
    }
}


void TransWindow::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::ActivationChange) {
        if (this->isActiveWindow()) {
            qDebug() << "Window activated";
        } else {
            qDebug() << "Window deactivated";
            this->activateWindow();
        }
    }
}
