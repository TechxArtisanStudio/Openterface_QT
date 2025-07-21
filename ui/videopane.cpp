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

#include <QPainter>
#include <QMouseEvent>
#include <QDebug>
#include <QGuiApplication>
#include <QScreen>

VideoPane::VideoPane(QWidget *parent) : QVideoWidget(parent), escTimer(new QTimer(this)), m_inputHandler(new InputHandler(this, this))
{
    qDebug() << "VideoPane init...";
    QWidget* childWidget = qobject_cast<QWidget*>(this->children()[0]);
    if(childWidget) {
        qDebug() << "Child widget:" << childWidget << "type:" << childWidget->metaObject()->className();
        childWidget->setMouseTracking(true);
        // childWidget->setAttribute(Qt::WA_TransparentForMouseEvents);
        childWidget->setStyleSheet("background-color: rgba(0, 0, 0, 50);"); // 50 is the alpha value (0-255), adjust as needed
        childWidget->setAttribute(Qt::WA_TranslucentBackground);
    }
    this->setAspectRatioMode(Qt::IgnoreAspectRatio);
    this->setMouseTracking(true);
    this->installEventFilter(m_inputHandler);
    this->setFocusPolicy(Qt::StrongFocus);
    this->relativeModeEnable = false;

    // Set up the timer
    connect(escTimer, &QTimer::timeout, this, &VideoPane::showHostMouse);
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


