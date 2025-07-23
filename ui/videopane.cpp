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
#include <QTimer>

VideoPane::VideoPane(QWidget *parent) : QVideoWidget(parent), escTimer(new QTimer(this)), m_inputHandler(new InputHandler(this, this)), m_isCameraSwitching(false)
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

void VideoPane::onCameraDeviceSwitching(const QString& fromDevice, const QString& toDevice)
{
    qDebug() << "VideoPane: Camera switching from" << fromDevice << "to" << toDevice;
    
    // Capture the current frame before switching
    captureCurrentFrame();
    
    // Set switching mode to display the last frame
    m_isCameraSwitching = true;
    
    // Force a repaint to show the captured frame
    update();
}

void VideoPane::onCameraDeviceSwitchComplete(const QString& device)
{
    qDebug() << "VideoPane: Camera switch complete to" << device;
    
    // Clear switching mode to resume normal video display
    m_isCameraSwitching = false;
    
    // Clear the captured frame
    m_lastFrame = QPixmap();
    
    // Force a repaint to resume normal video display
    update();
}

void VideoPane::captureCurrentFrame()
{
    // Try multiple methods to capture the current frame
    if (this->isVisible() && this->size().isValid()) {
        // Method 1: Grab the widget content
        m_lastFrame = this->grab();
        
        // Method 2: If grab() failed or returned null, try to get pixmap from render
        if (m_lastFrame.isNull() || m_lastFrame.size().isEmpty()) {
            m_lastFrame = QPixmap(this->size());
            m_lastFrame.fill(Qt::black); // Fill with black as fallback
            QPainter painter(&m_lastFrame);
            this->render(&painter);
        }
        
        qDebug() << "VideoPane: Captured frame" << m_lastFrame.size() << "for preservation during camera switch";
    } else {
        // Create a black fallback frame
        m_lastFrame = QPixmap(this->size().isEmpty() ? QSize(640, 480) : this->size());
        m_lastFrame.fill(Qt::black);
        qDebug() << "VideoPane: Created fallback black frame for camera switch";
    }
}

void VideoPane::paintEvent(QPaintEvent *event)
{
    if (m_isCameraSwitching && !m_lastFrame.isNull()) {
        // During camera switching, paint the last captured frame
        QPainter painter(this);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        
        // Scale the frame to fit the widget while maintaining aspect ratio
        QSize widgetSize = this->size();
        QSize frameSize = m_lastFrame.size();
        
        if (!frameSize.isEmpty() && !widgetSize.isEmpty()) {
            QRect targetRect = QRect(QPoint(0, 0), frameSize.scaled(widgetSize, Qt::KeepAspectRatio));
            targetRect.moveCenter(this->rect().center());
            painter.drawPixmap(targetRect, m_lastFrame);
        } else {
            // Fallback: draw frame as-is
            painter.drawPixmap(this->rect(), m_lastFrame);
        }
        
        qDebug() << "VideoPane: Displaying preserved frame during camera switch";
    } else {
        // Normal video display
        QVideoWidget::paintEvent(event);
    }
}


