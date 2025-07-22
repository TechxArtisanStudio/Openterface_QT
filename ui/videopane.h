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
#include "inputhandler.h"

#include <QVideoWidget>
#include <QMouseEvent>
#include <QPixmap>
#include <QPainter>


class VideoPane : public QVideoWidget
{
    Q_OBJECT

public:
    explicit VideoPane(QWidget *parent = nullptr);

    void showHostMouse();
    void hideHostMouse();
    void moveMouseToCenter();
    void startEscTimer();
    void stopEscTimer();

    bool focusNextPrevChild(bool next) override;

    bool isRelativeModeEnabled() const { return relativeModeEnable; }
    void setRelativeModeEnabled(bool enable) { relativeModeEnable = enable; }

public slots:
    void onCameraDeviceSwitching(const QString& fromDevice, const QString& toDevice);
    void onCameraDeviceSwitchComplete(const QString& device);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    int lastX=0;
    int lastY=0;
    bool relativeModeEnable;
    
    InputHandler *m_inputHandler;

    QTimer *escTimer;
    bool holdingEsc=false;
    
    // Frame preservation during camera switching
    QPixmap m_lastFrame;
    bool m_isCameraSwitching;
    
    MouseEventDTO* calculateRelativePosition(QMouseEvent *event);
    MouseEventDTO* calculateAbsolutePosition(QMouseEvent *event);
    MouseEventDTO* calculateMouseEventDto(QMouseEvent *event);
    
    void captureCurrentFrame();
};

#endif
