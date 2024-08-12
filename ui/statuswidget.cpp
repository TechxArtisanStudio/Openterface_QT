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

StatusWidget::StatusWidget(QWidget *parent) : QWidget(parent) {
    resolutionLabel = new QLabel("💻:", this);
    inputResolutionLabel = new QLabel("INPUT(NA),", this);
    captureResolutionLabel = new QLabel("CAPTURE(NA)", this);
    connectedPortLabel = new QLabel("🔌: N/A", this);

    QHBoxLayout *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(1);

    layout->addWidget(connectedPortLabel);
    layout->addWidget(new QLabel("|", this));
    layout->addWidget(resolutionLabel);
    layout->addWidget(inputResolutionLabel);
    layout->addWidget(captureResolutionLabel);

    setLayout(layout);
    setMinimumHeight(30);
    update();
}

void StatusWidget::setInputResolution(const int &width, const int &height, const float &fps) {
    inputResolutionLabel->setText(QString("INPUT(%1X%2@%3),").arg(width).arg(height).arg(fps));
    update(); 
}

void StatusWidget::setCaptureResolution(const int &width, const int &height, const float &fps) {
    captureResolutionLabel->setText(QString("CAPTURE(%1X%2@%3)").arg(width).arg(height).arg(fps));
    update(); 
}

void StatusWidget::setConnectedPort(const QString &port) {
    connectedPortLabel->setText("🔌: " + port);
    update(); 
}
