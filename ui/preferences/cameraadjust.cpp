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

#include "cameraadjust.h"
#include <QApplication>
#include <QPalette>

CameraAdjust::CameraAdjust(QWidget *parent)
    : QWidget(parent)
{
    setupUI();
    usbControl = new USBControl(this);
    initializeControls();
    
    // Ensure the widget stays on top
    setWindowFlags(Qt::Widget);
    // Make sure it's visible in the parent widget
    raise();

    // Connect to palette change event
    connect(qApp, &QApplication::paletteChanged, this, &CameraAdjust::updateColors);
}

CameraAdjust::~CameraAdjust()
{
}

void CameraAdjust::setupUI()
{
    // Set fixed width for the control panel
    setFixedWidth(180);
    
    // Create layout
    QVBoxLayout *controlsLayout = new QVBoxLayout(this);
    
    // Create and setup the contrast label
    contrastLabel = new QLabel("Contrast:", this);
    controlsLayout->addWidget(contrastLabel);
    
    // Create and setup the contrast slider
    contrastSlider = new QSlider(Qt::Horizontal, this);
    contrastSlider->setMinimum(0);
    contrastSlider->setMaximum(255);
    contrastSlider->setValue(128); // Default value
    contrastSlider->setFixedWidth(150);
    
    controlsLayout->addWidget(contrastSlider);
    
    // Set the layout alignment and reduce margins
    controlsLayout->setAlignment(Qt::AlignTop);
    controlsLayout->setContentsMargins(5, 5, 5, 5);
    
    // Connect the slider's value changed signal
    connect(contrastSlider, &QSlider::valueChanged, this, &CameraAdjust::onContrastChanged);
    
    // Set a fixed size for the widget
    setFixedSize(180, 50);
    
    // Set initial colors
    updateColors();
    
    // Hide initially
    hide();
}

void CameraAdjust::updateColors()
{
    QPalette systemPalette = QApplication::palette();
    QPalette widgetPalette = palette();
    
    // Get system colors
    QColor textColor = systemPalette.color(QPalette::WindowText);
    QColor backgroundColor = systemPalette.color(QPalette::Window);
    QColor highlightColor = systemPalette.color(QPalette::Highlight);
    
    // Set background with some transparency
    backgroundColor.setAlpha(230);
    setAutoFillBackground(true);
    widgetPalette.setColor(QPalette::Window, backgroundColor);
    setPalette(widgetPalette);
    
    // Update label color
    contrastLabel->setStyleSheet(QString("QLabel { color: %1; }").arg(textColor.name()));
    
    // Update slider style
    QString sliderStyle = QString(
        "QSlider::groove:horizontal {"
        "    background: %1;"
        "    height: 4px;"
        "    margin: 0px;"
        "}"
        "QSlider::handle:horizontal {"
        "    background: %2;"
        "    width: 16px;"
        "    margin: -6px 0;"
        "    border-radius: 8px;"
        "}"
        "QSlider::add-page:horizontal {"
        "    background: %3;"
        "}"
        "QSlider::sub-page:horizontal {"
        "    background: %4;"
        "}")
        .arg(systemPalette.color(QPalette::Mid).name())
        .arg(highlightColor.name())
        .arg(systemPalette.color(QPalette::Mid).name())
        .arg(highlightColor.name());
    
    contrastSlider->setStyleSheet(sliderStyle);
}

void CameraAdjust::initializeControls()
{
    if (usbControl && usbControl->initializeUSB()) {
        qDebug() << "USB initialized";
        if (usbControl->findAndOpenUVCDevice()) {
            qDebug() << "USB device found and opened";
            int currentContrast = usbControl->getContrast();
            if (currentContrast >= 0) {
                contrastSlider->setValue(currentContrast);
            }
        }
    }
}

void CameraAdjust::toggleVisibility()
{
    // clicked to show or hide
    if (!isVisible()) {
        hide();
    } else {
        show();
        updatePosition(parentWidget()->property("menuBarHeight").toInt(), parentWidget()->width());
        raise(); // Ensure it's on top
    }
}

void CameraAdjust::updatePosition(int menuBarHeight, int parentWidth)
{
    // Position the widget in the top right corner, but offset from the contrast button
    setGeometry(
        parentWidth - width() - 60,  // Move further left to avoid covering the button
        menuBarHeight + 35,          // Move down below the button
        width(),
        height()
    );
}

void CameraAdjust::onContrastChanged(int value)
{
    if (usbControl) {
        if (usbControl->setContrast(value)) {
            qDebug() << "Contrast set to:" << value;
        } else {
            qDebug() << "Failed to set contrast";
        }
    }
}
