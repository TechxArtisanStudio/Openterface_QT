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

#include "helppane.h"
#include <QPainter>
#include <QColor>

HelpPane::HelpPane(QWidget *parent) : QWidget(parent)
{

}

void HelpPane::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    QColor color("#040707");
    painter.fillRect(this->rect(), color);
    QPixmap pixmap(":/images/content_dark_eng.png"); // Replace with the path to your image in the resource file

    int paddingWidth = this->width() * 0.05;
    int paddingHeight = this->height() * 0.05;

    // Adjust the rectangle
    QRect paddedRect = this->rect().adjusted(paddingWidth, paddingHeight, -paddingWidth, -paddingHeight);

    painter.drawPixmap(paddedRect, pixmap);
}

