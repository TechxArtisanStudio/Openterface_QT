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

#include "statuswidget.h"
#include <QSvgRenderer>
#include <QPainter>

StatusWidget::StatusWidget(QWidget *parent) : QWidget(parent), m_captureWidth(0), m_captureHeight(0) {
    keyboardIndicatorsLabel = new QLabel("", this);
    statusLabel = new QLabel("", this);
    resolutionLabel = new QLabel("💻:", this);
    inputResolutionLabel = new QLabel("INPUT(NA),", this);
    captureResolutionLabel = new QLabel("CAPTURE(NA)", this);
    connectedPortLabel = new QLabel("🔌: N/A", this);

    QHBoxLayout *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(1);

    layout->addWidget(statusLabel);
    layout->addWidget(new QLabel("| ", this));
    layout->addWidget(keyboardIndicatorsLabel);
    layout->addWidget(new QLabel("|", this));
    layout->addWidget(connectedPortLabel);
    layout->addWidget(new QLabel("|", this));
    layout->addWidget(resolutionLabel);
    layout->addWidget(inputResolutionLabel);
    layout->addWidget(captureResolutionLabel);

    setLayout(layout);
    setMinimumHeight(30);
    update();
}

void StatusWidget::setInputResolution(const int &width, const int &height, const float &fps, const float &pixelClk) {
    if(width == 0 || height == 0 || fps == 0) {
        inputResolutionLabel->setText("INPUT(NA),");
        inputResolutionLabel->setToolTip("Input video is not available");
        update();
        return;
    }
    inputResolutionLabel->setText(QString("INPUT(%1X%2@%3),").arg(width).arg(height).arg(fps));
    QString tooltip = QString("Input Resolution: %1 x %2@%3\nPixel Clock: %4Mhz").arg(width).arg(height).arg(fps).arg(pixelClk);
    inputResolutionLabel->setToolTip(tooltip);
    update(); 
}

void StatusWidget::setCaptureResolution(const int &width, const int &height, const float &fps) {
    m_captureWidth = width;
    m_captureHeight = height;
    captureResolutionLabel->setText(QString("CAPTURE(%1X%2@%3)").arg(width).arg(height).arg(fps));
    update(); 
}

void StatusWidget::setConnectedPort(const QString &port, const int &baudrate) {
    if(baudrate > 0){
        connectedPortLabel->setText(QString("🔌: %1@%2").arg(port).arg(baudrate));
    }else{
        connectedPortLabel->setText(QString("🔌: N/A"));
    }
    update(); 
}

void StatusWidget::setStatusUpdate(const QString &status){
    statusLabel->setText(status);
    update();
}

void StatusWidget::setTargetUsbConnected(const bool isConnected){
    QString keyboardSvgPath = ":/images/keyboard.svg";
    QString mouseSvgPath = ":/images/mouse-default.svg";
    QColor fillColor = isConnected ? QColor(0, 255, 0, 128) : QColor(255, 0, 0, 200);
    
    QPixmap combinedPixmap(36, 18);
    combinedPixmap.fill(Qt::transparent);
    QPainter painter(&combinedPixmap);
    
    // Render keyboard
    QSvgRenderer keyboardRenderer(keyboardSvgPath);
    keyboardRenderer.render(&painter, QRectF(0, 0, 18, 18));
    
    // Render mouse
    QSvgRenderer mouseRenderer(mouseSvgPath);
    QRectF mouseRect(18, 1.8, 14.4, 14.4);  // 20% smaller, centered vertically
    mouseRenderer.render(&painter, mouseRect);
    
    // Apply color overlay
    painter.setCompositionMode(QPainter::CompositionMode_SourceAtop);
    painter.fillRect(combinedPixmap.rect(), fillColor);
    painter.end();
    
    keyboardIndicatorsLabel->setPixmap(combinedPixmap);
    update();
}

void StatusWidget::setBaudrate(int baudrate)
{
    // Update the UI element that displays the baudrate
    connectedPortLabel->setText(QString("🔌: %1@%2").arg(connectedPortLabel->text().split('@').first()).arg(baudrate));
    update();
}

// Implement the new methods:
int StatusWidget::getCaptureWidth() const
{
    return m_captureWidth;
}

int StatusWidget::getCaptureHeight() const
{
    return m_captureHeight;
}
