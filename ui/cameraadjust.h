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

#ifndef CAMERAAJUST_H
#define CAMERAAJUST_H

#include <QWidget>
#include <QSlider>
#include <QLabel>
#include <QVBoxLayout>
#include <QPalette>
#include <QDebug>
#include "host/usbcontrol.h"

class CameraAdjust : public QWidget
{
    Q_OBJECT

public:
    explicit CameraAdjust(QWidget *parent = nullptr);
    ~CameraAdjust();

    void updatePosition();
    void initializeControls();

public slots:
    void toggleVisibility();
    void updatePosition(int menuBarHeight, int parentWidth);
    void updateColors();

private slots:
    void onContrastChanged(int value);

private:
    void setupUI();
    QSlider *contrastSlider;
    QLabel *contrastLabel;
    USBControl *usbControl;
};

#endif // CAMERAAJUST_H
