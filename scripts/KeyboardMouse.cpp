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
    QMutexLocker locker(&queueMutex);
    keyData.push(packet);
}


void KeyboardMouse::dataSend(){
    QMutexLocker locker(&queueMutex);
    qDebug() << "[KeyboardMouse::dataSend] Starting to send" << keyData.size() << "key packet(s)";
    
    if (keyData.empty()) {
        qDebug() << "[KeyboardMouse::dataSend] Queue is empty, nothing to send";
        return;
    }
    
    while(!keyData.empty()){
        const auto& packet = keyData.front();
        qDebug() << "[KeyboardMouse::dataSend] Processing packet - Queue size:" << keyData.size()
                 << "| KB:" << packet.keyboardSendOrNot 
                 << "| Mouse:" << packet.mouseSendOrNot
                 << "| Combined:" << packet.keyboardMouseSendOrNot;
        
        // Unlock while sending to prevent deadlock with serial operations
        locker.unlock();
        
        if(packet.keyboardSendOrNot) {
            qDebug() << "[KeyboardMouse::dataSend] -> Sending keyboard keystroke";
            keyboardSend();
        }
        if(packet.mouseSendOrNot) {
            qDebug() << "[KeyboardMouse::dataSend] -> Sending mouse action";
            mouseSend();
        }
        if(packet.keyboardMouseSendOrNot) {
            qDebug() << "[KeyboardMouse::dataSend] -> Sending combined keyboard + mouse action";
            keyboardMouseSend();
        }
        
        // Re-lock and pop
        locker.relock();
        if (!keyData.empty()) {  // Check again before popping
            keyData.pop();
            qDebug() << "[KeyboardMouse::dataSend] Packet sent - Remaining queue size:" << keyData.size();
        } else {
            qDebug() << "[KeyboardMouse::dataSend] WARNING: Queue became empty unexpectedly";
            break;
        }
    }
    qDebug() << "[KeyboardMouse::dataSend] All packets sent successfully";
}

void KeyboardMouse::keyboardSend(){
    QMutexLocker locker(&queueMutex);
    
    if (keyData.empty()) {
        qDebug() << "[KeyboardMouse::keyboardSend] ERROR: Queue is empty!";
        return;
    }
    
    QByteArray data = CMD_SEND_KB_GENERAL_DATA;
    QByteArray release = CMD_SEND_KB_GENERAL_DATA;

    QByteArray tmpKeyData = keyData.front().KeytoQByteArray();
    data.replace(data.size() - 8, 8, tmpKeyData);
    
    locker.unlock();  // Unlock before serial operations
    
    qDebug() << "[KeyboardMouse::keyboardSend] Sending key press data:" << data.toHex();
    emit SerialPortManager::getInstance().sendCommandAsync(data, false);
    QThread::msleep(clickInterval);
    qDebug() << "[KeyboardMouse::keyboardSend] Sending key release data:" << release.toHex();
    emit SerialPortManager::getInstance().sendCommandAsync(release, false);
    QThread::msleep(clickInterval);
}

uint8_t KeyboardMouse::calculateChecksum(const QByteArray &data){
    quint32 sum = 0;
    for (auto byte : data) {
        sum += static_cast<unsigned char>(byte);
    }
    return sum % 256;
}

void KeyboardMouse::mouseSend(){
    QMutexLocker locker(&queueMutex);
    
    if (keyData.empty()) {
        qDebug() << "[KeyboardMouse::mouseSend] ERROR: Queue is empty!";
        return;
    }
    
    QByteArray data;
    QByteArray release;
    uint8_t clickCount = keyData.front().mouseClickCount;
    
    QString modeStr = (keyData.front().mouseMode == 0x02) ? "Absolute" : "Relative";
    qDebug() << "[KeyboardMouse::mouseSend] Mouse action -" << modeStr << "mode |" << clickCount << "click(s)";
    
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
    
    data.append(tmpMouseData);
    release.append(releaseMouseData);
    data.append(static_cast<char>(calculateChecksum(data)));
    release.append(static_cast<char>(calculateChecksum(release)));
    
    qDebug() << "[KeyboardMouse::mouseSend] Press data:" << data.toHex();
    qDebug() << "[KeyboardMouse::mouseSend] Release data:" << release.toHex();
    
    locker.unlock();  // Unlock before serial operations
    
    for (int i = 0; i<clickCount; i++){
        qDebug() << "[KeyboardMouse::mouseSend] Click" << (i+1) << "of" << clickCount;
        emit SerialPortManager::getInstance().sendCommandAsync(data, false);
        QThread::msleep(clickInterval);
        emit SerialPortManager::getInstance().sendCommandAsync(release, false);
        QThread::msleep(clickInterval);
    }
}

void KeyboardMouse::keyboardMouseSend(){
    QMutexLocker locker(&queueMutex);
    
    if (keyData.empty()) {
        qDebug() << "[KeyboardMouse::keyboardMouseSend] ERROR: Queue is empty!";
        return;
    }
    
    qDebug() << "[KeyboardMouse::keyboardMouseSend] Sending combined keyboard + mouse action";
    
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
    releaseMouseData[0] = 0x00;

    mouseData.append(tmpMouseData);
    mouseRelease.append(releaseMouseData);
    mouseData.append(static_cast<char>(calculateChecksum(mouseData)));
    mouseRelease.append(static_cast<char>(calculateChecksum(mouseRelease)));
    
    qDebug() << "[KeyboardMouse::keyboardMouseSend] Keyboard press data:" << keyboardData.toHex();
    qDebug() << "[KeyboardMouse::keyboardMouseSend] Mouse press data:" << mouseData.toHex();

    locker.unlock();  // Unlock before serial operations

    // Send press data for both devices
    emit SerialPortManager::getInstance().sendCommandAsync(keyboardData, false);
    emit SerialPortManager::getInstance().sendCommandAsync(mouseData, false);

    // Send release data for both devices
    qDebug() << "[KeyboardMouse::keyboardMouseSend] Mouse release data:" << mouseRelease.toHex();
    emit SerialPortManager::getInstance().sendCommandAsync(mouseRelease, false);
    QThread::msleep(keyInterval);
    qDebug() << "[KeyboardMouse::keyboardMouseSend] Keyboard release data:" << keyboardRelease.toHex();
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
