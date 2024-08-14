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

#ifndef VIDEOPANE_H
#define VIDEOPANE_H

#include "target/mouseeventdto.h"

#include <QVideoWidget>
#include <QMouseEvent>


class VideoPane : public QVideoWidget
{
    Q_OBJECT

public:
    explicit VideoPane(QWidget *parent = nullptr);

    void showHostMouse();
    void hideHostMouse();

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void moveMouseToCenter();

private:
    int getMouseButton(QMouseEvent *event);
    int lastMouseButton = 0;
    bool isDragging = false;
    int lastX=0;
    int lastY=0;
    bool relativeModeEnable;
    
    QTimer *escTimer;
    bool holdingEsc=false;
    
    MouseEventDTO* calculateRelativePosition(QMouseEvent *event);
    MouseEventDTO* calculateAbsolutePosition(QMouseEvent *event);
    MouseEventDTO* calculateMouseEventDto(QMouseEvent *event);
};

#endif
