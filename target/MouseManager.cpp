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

Q_LOGGING_CATEGORY(log_core_mouse, "opf.host.mouse")

MouseManager::MouseManager(QObject *parent) : QObject(parent), mouseMoverThread(nullptr) {
    qCDebug(log_core_mouse) << "MouseManager created";
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

    QByteArray data;
    uint8_t mappedWheelMovement = mapScrollWheel(wheelMovement);
    if(mappedWheelMovement>0){    qCDebug(log_core_mouse) << "mappedWheelMovement:" << mappedWheelMovement; }
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
    qCDebug(log_core_mouse) << "handleRelativeMouseAction";
    QByteArray data;
    uint8_t mappedWheelMovement = mapScrollWheel(wheelMovement);
    if(mappedWheelMovement>0){    qCDebug(log_core_mouse) << "mappedWheelMovement:" << mappedWheelMovement; }
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
        return uint8_t(delta / 100);
    }else{
        return 0xFF - uint8_t(-1*delta / 100)+1;
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
