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


void KeyboardMouse::dataSend(){
    while(!keyData.empty()){
        
        qDebug() << "Sending data for key packet: " 
            << keyData.front().keyboardSendOrNot
            << keyData.front().mouseSendOrNot
            << keyData.front().keyboardMouseSendOrNot;
        if(keyData.front().keyboardSendOrNot) {
            qDebug() << "Sending keyboard data.";
            keyboardSend();
        }
        if(keyData.front().mouseSendOrNot) {
            qDebug() << "Sending mouse data.";
            mouseSend();
        }
        if(keyData.front().keyboardMouseSendOrNot) {
            qDebug() << "Sending keyboard and mouse data.";
            keyboardMouseSend();
        }
        keyData.pop();
        qDebug() << "After" << keyData.size();
    }
}

void KeyboardMouse::keyboardSend(){
    QByteArray data = CMD_SEND_KB_GENERAL_DATA;
    QByteArray release = CMD_SEND_KB_GENERAL_DATA;
    // while(!keyData.empty()){
    QByteArray tmpKeyData = keyData.front().KeytoQByteArray();
    qDebug() << "Data: " << tmpKeyData.toHex();
    data.replace(data.size() - 8, 8, tmpKeyData);   // replace the last 8 byte data
    qDebug() << "After checksum data: " << data.toHex();
    emit SerialPortManager::getInstance().sendCommandAsync(data, false);
    QThread::msleep(clickInterval);
    emit SerialPortManager::getInstance().sendCommandAsync(release, false);
    QThread::msleep(clickInterval);
    // keyData.pop();
    // }
}

uint8_t KeyboardMouse::calculateChecksum(const QByteArray &data){
    quint32 sum = 0;
    for (auto byte : data) {
        sum += static_cast<unsigned char>(byte);
    }
    return sum % 256;
}

void KeyboardMouse::mouseSend(){
    QByteArray data;
    QByteArray release;
    uint8_t clickCount = keyData.front().mouseClickCount;
    // while(!keyData.empty()){
    if (keyData.front().mouseMode == 0x02) {
        data.append(MOUSE_ABS_ACTION_PREFIX);
        release.append(MOUSE_ABS_ACTION_PREFIX);
    }else{
        data.append(MOUSE_REL_ACTION_PREFIX);
        release.append(MOUSE_REL_ACTION_PREFIX);
    } 

    QByteArray tmpMouseData = keyData.front().MousetoQByteArray();
    QByteArray releaseMouseData = tmpMouseData;
    releaseMouseData[0] = 0x00;
    qDebug() << "Release data: " << releaseMouseData.toHex();
    qDebug() << "Mouse Data: " << tmpMouseData.toHex();
    
    data.append(tmpMouseData);
    release.append(releaseMouseData);
    data.append(static_cast<char>(calculateChecksum(data)));
    release.append(static_cast<char>(calculateChecksum(release)));
    qDebug() << "merged data: " << data.toHex();
    qDebug() << "merged release: " << release.toHex();
    for (int i = 0; i<clickCount; i++){
        emit SerialPortManager::getInstance().sendCommandAsync(data, false);
        QThread::msleep(clickInterval);
        emit SerialPortManager::getInstance().sendCommandAsync(release, false);
        QThread::msleep(clickInterval);
    }
    
    // keyData.pop();
        // emit SerialPortManager::getInstance().sendCommandAsync(release, false);
    // }
}

void KeyboardMouse::keyboardMouseSend(){
    QByteArray mouseData;
    QByteArray mouseRelease;
    QByteArray keyboardData = CMD_SEND_KB_GENERAL_DATA;
    QByteArray keyboardRelease = CMD_SEND_KB_GENERAL_DATA;

    // Prepare keyboard data
    QByteArray tmpKeyData = keyData.front().KeytoQByteArray();
    keyboardData.replace(keyboardData.size() - 8, 8, tmpKeyData);

    // Prepare mouse data
    if (keyData.front().mouseMode == 0x02) {
        mouseData.append(MOUSE_ABS_ACTION_PREFIX);
        mouseRelease.append(MOUSE_ABS_ACTION_PREFIX);
    } else {
        mouseData.append(MOUSE_REL_ACTION_PREFIX);
        mouseRelease.append(MOUSE_REL_ACTION_PREFIX);
    }

    QByteArray tmpMouseData = keyData.front().MousetoQByteArray();
    QByteArray releaseMouseData = tmpMouseData;
    releaseMouseData[0] = 0x00;  // Set release state for mouse

    mouseData.append(tmpMouseData);
    mouseRelease.append(releaseMouseData);
    mouseData.append(static_cast<char>(calculateChecksum(mouseData)));
    mouseRelease.append(static_cast<char>(calculateChecksum(mouseRelease)));
    qDebug() << "keyboard data: " << keyboardData.toHex();
    qDebug() << "mouse data: " << mouseData.toHex();

    // Send press data for both devices
    emit SerialPortManager::getInstance().sendCommandAsync(keyboardData, false);
    emit SerialPortManager::getInstance().sendCommandAsync(mouseData, false);

    // Send release data for both devices
    emit SerialPortManager::getInstance().sendCommandAsync(mouseRelease, false);
    QThread::msleep(keyInterval);
    emit SerialPortManager::getInstance().sendCommandAsync(keyboardRelease, false);
    

    
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
