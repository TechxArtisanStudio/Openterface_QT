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

#include "MouseManager.h"
#include "serial/SerialPortManager.h"
#include "log/opflogging.h"
#include <QThread>

OPF_LOGGING_CATEGORY(log_mouse_abs, "opf.host.mouse.absolute")
OPF_LOGGING_CATEGORY(log_mouse_rel, "opf.host.mouse.relative")
OPF_LOGGING_CATEGORY(log_mouse_scroll, "opf.host.mouse.scroll")

MouseManager::MouseManager(QObject *parent) : QObject(parent), mouseMoverThread(nullptr) {
    qCDebug(log_mouse_abs) << "MouseManager created";
}

MouseManager::~MouseManager() {
    stopAutoMoveMouse();
}

void MouseManager::setEventCallback(StatusEventCallback* callback) {
    statusEventCallback = callback;
}

void MouseManager::handleAbsoluteMouseAction(int x, int y, int mouse_event, int wheelMovement) {
    // stop auto move if it is running
    stopAutoMoveMouse();

    // Remember last known coordinates for scroll-wheel reuse
    lastX = x;
    lastY = y;

    // Update current mouse button state
    // If wheelMovement is provided and mouse_event is 0, preserve current button state
    if (wheelMovement != 0 && mouse_event == 0) {
        mouse_event = currentMouseButton;
    } else {
        currentMouseButton = mouse_event;
    }

    QByteArray data;
    uint8_t mappedWheelMovement = mapScrollWheel(wheelMovement);
    if(mappedWheelMovement>0){    qCDebug(log_mouse_abs) << "mappedWheelMovement:" << mappedWheelMovement; }
    data.append(MOUSE_ABS_ACTION_PREFIX);
    data.append(static_cast<char>(mouse_event));
    data.append(static_cast<char>(x & 0xFF));
    data.append(static_cast<char>((x >> 8) & 0xFF));
    data.append(static_cast<char>(y & 0xFF));
    data.append(static_cast<char>((y >> 8) & 0xFF));
    data.append(static_cast<char>(mappedWheelMovement & 0xFF));

    // send the data to serial
    SerialPortManager::getInstance().sendCommandAsync(data, false);

    QString mouseEventStr;
    if(mouse_event == Qt::LeftButton){
        mouseEventStr = "L";
    }else if(mouse_event == Qt::RightButton){
        mouseEventStr = "R";
    }else if(mouse_event == Qt::MiddleButton){
        mouseEventStr = "M";
    } else{
        mouseEventStr = "";
    }

    if (statusEventCallback) statusEventCallback->onLastMouseLocation(QPoint(x, y), mouseEventStr);
}

void MouseManager::handleRelativeMouseAction(int dx, int dy, int mouse_event, int wheelMovement) {
    qCDebug(log_mouse_rel) << "handleRelativeMouseAction";
    
    // Update current mouse button state
    // If wheelMovement is provided and mouse_event is 0, preserve current button state
    if (wheelMovement != 0 && mouse_event == 0) {
        mouse_event = currentMouseButton;
    } else {
        currentMouseButton = mouse_event;
    }
    
    QByteArray data;
    uint8_t mappedWheelMovement = mapScrollWheel(wheelMovement);
    if(mappedWheelMovement>0){    qCDebug(log_mouse_rel) << "mappedWheelMovement:" << mappedWheelMovement; }
    data.append(MOUSE_REL_ACTION_PREFIX);
    data.append(static_cast<char>(mouse_event));
    data.append(static_cast<char>(dx & 0xFF));
    data.append(static_cast<char>(dy & 0xFF));
    data.append(static_cast<char>(mappedWheelMovement & 0xFF));

    // send the data to serial
    SerialPortManager::getInstance().sendCommandAsync(data, false);

    QString mouseEventStr;
    if(mouse_event == Qt::LeftButton){
        mouseEventStr = "L";
    }else if(mouse_event == Qt::RightButton){
        mouseEventStr = "R";
    }else if(mouse_event == Qt::MiddleButton){
        mouseEventStr = "M";
    } else{
        mouseEventStr = "";
    }

    if (statusEventCallback) statusEventCallback->onLastMouseLocation(QPoint(dx, dy), mouseEventStr);
}

uint8_t MouseManager::mapScrollWheel(int delta){
    if(delta == 0){
        return 0;
    }else if(delta > 0){
        return uint8_t(delta / 50);
    }else{
        return 0xFF - uint8_t(-1*delta / 50)+1;
    }
}

void MouseManager::scrollWheel(int direction, int lines) {
    // direction: positive = scroll up, negative = scroll down
    // lines: number of scroll lines (default 1)
    if (lines < 1) lines = 1;
    qCDebug(log_mouse_scroll) << "Scroll wheel - direction:" << direction << "lines:" << lines;

    // Reuse last known coordinates; fall back to (0, 0) if never moved
    int x = lastX;
    int y = lastY;

    // Send one scroll packet per line with a small delay between them
    for (int i = 0; i < lines; i++) {
        int delta = direction * 100;
        handleAbsoluteMouseAction(x, y, 0, delta);
        if (i < lines - 1) {
            QThread::msleep(20);
        }
    }
}

void MouseManager::startAutoMoveMouse() {
    if (!mouseMoverThread) {
        mouseMoverThread = new MouseMoverThread();
        connect(mouseMoverThread, &MouseMoverThread::finished, mouseMoverThread, &MouseMoverThread::deleteLater);
        mouseMoverThread->start();
    }
}

void MouseManager::stopAutoMoveMouse() {
    if (mouseMoverThread) {
        mouseMoverThread->stop();
        mouseMoverThread = nullptr;
    }
}
