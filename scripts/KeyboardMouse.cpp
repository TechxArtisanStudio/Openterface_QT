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
*    This program incorporates portions of the AutoHotkey source code,       *
*    which is licensed under the GNU General Public License version 2 or     *
*    later. The original AutoHotkey source code can be found at:             *
*    https://github.com/Lexikos/AutoHotkey_L                                 *
*                                                                            *
*                                                                            *
*    The AutoHotkey source code is copyrighted by the AutoHotkey             *
*    development team and contributors.                                      *
*                                                                            *
* ========================================================================== *
*/

#include "KeyboardMouse.h"
#include <queue>
#include <QDebug>


KeyboardMouse::KeyboardMouse(QObject *parent) : QObject(parent)
{
    // Constructor implementation
}

void KeyboardMouse::addKeyPacket(const keyPacket& packet) {
    keyData.push(packet);
}

void KeyboardMouse::keyboardSend(){
    QByteArray data = CMD_SEND_KB_GENERAL_DATA;
    QByteArray release = CMD_SEND_KB_GENERAL_DATA;
    while(!keyData.empty()){
        QByteArray tmpKeyData = keyData.front().KeytoQByteArray();
        qDebug() << "Data: " << tmpKeyData;
        data.replace(data.size() - 8, 8, tmpKeyData);   // replace the last 8 byte data
        emit SerialPortManager::getInstance().sendCommandAsync(data, false);
        keyData.pop();
        emit SerialPortManager::getInstance().sendCommandAsync(release, false);
    }
}

void KeyboardMouse::mouseSend(){
    QByteArray data;

    while(!keyData.empty()){
        if (keyData.front().mouseMode == 0x02) data.append(MOUSE_ABS_ACTION_PREFIX);
        else data.append(MOUSE_REL_ACTION_PREFIX);

        QByteArray tmpMouseData = keyData.front().MousetoQByteArray();
        qDebug() << "Mouse Data: " << tmpMouseData;
        data.append(tmpMouseData);

        emit SerialPortManager::getInstance().sendCommandAsync(data, false);
        keyData.pop();
        // emit SerialPortManager::getInstance().sendCommandAsync(release, false);
    }
}

void KeyboardMouse::keyboardMouseSend(){
    QByteArray data;
    QByteArray release = CMD_SEND_KB_GENERAL_DATA;
    while(!keyData.empty()){
        // distinguish relative & ABS move of the mouse
        if (keyData.front().mouseMode == 0x02) data.append(MOUSE_ABS_ACTION_PREFIX);
        if (keyData.front().mouseMode == 0x01) data.append(MOUSE_REL_ACTION_PREFIX);

        if(keyData.front().mouseMode == 0x00){
            data = CMD_SEND_KB_GENERAL_DATA;
            QByteArray tmpKeyData = keyData.front().KeytoQByteArray();
            data.replace(data.size() - 8, 8, tmpKeyData);   // replace the last 8 byte data
            emit SerialPortManager::getInstance().sendCommandAsync(data, false);
            keyData.pop();
        }else{
            QByteArray tmpMouseData = keyData.front().MousetoQByteArray();
            data.append(tmpMouseData);
            emit SerialPortManager::getInstance().sendCommandAsync(data, false);
            keyData.pop();
            data[6] = 0x00;
            emit SerialPortManager::getInstance().sendCommandAsync(data, false);
            emit SerialPortManager::getInstance().sendCommandAsync(release, false);     // release all the keyborad data
        }
    }
}

void KeyboardMouse::setMouseSpeed(int speed){
    mouseSpeed = speed;
}

int KeyboardMouse::getMouseSpeed(){
    return mouseSpeed;
}

void KeyboardMouse::updateNumCapsScrollLockState(){
    emit SerialPortManager::getInstance().sendCommandAsync(CMD_GET_INFO, false);
}

bool KeyboardMouse::getNumLockState_(){
    return SerialPortManager::getInstance().getNumLockState();
}

bool KeyboardMouse::getCapsLockState_(){
    return SerialPortManager::getInstance().getCapsLockState();
}

bool KeyboardMouse::getScrollLockState_(){
    return SerialPortManager::getInstance().getScrollLockState();
}