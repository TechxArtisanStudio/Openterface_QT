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

#include "KeyboardManager.h"
#include "target/Keymapping.h"

#include <QtConcurrent/QtConcurrent>

Q_LOGGING_CATEGORY(log_keyboard, "opf.host.keyboard")

KeyboardManager::KeyboardManager(QObject *parent) : QObject(parent){

}

void KeyboardManager::handleKeyboardAction(int keyCode, int modifiers, bool isKeyDown) {
    QByteArray keyData = QByteArray::fromHex("57 AB 00 02 08 00 00 00 00 00 00 00 00");

    unsigned int combinedModifiers = 0;
    int mappedKeyCode = mappedKeyCode = keyMap.value(keyCode, 0);

    if(isModiferKeys(keyCode)){
        // Distingush the left or right modifiers, the modifiers is a native event
        // And the keyMap uses right modifer by default
        if( modifiers == 1537){ // left shift
            mappedKeyCode = 0xe1;
            currentModifiers |= 0x02;
        } else if(modifiers == 1538){// left ctrl
            mappedKeyCode = 0xe0;
            currentModifiers |= 0x01;
        } else if(modifiers == 1540){ //left alt
            mappedKeyCode = 0xe2;
            currentModifiers |= 0x04;
        }
    }else if(isKeypadKeys(keyCode, modifiers)){
        if(keyCode == Qt::Key_7){
            mappedKeyCode = 0x5F;
        } else if(keyCode == Qt::Key_4){
            mappedKeyCode = 0x5C;
        } else if(keyCode == Qt::Key_1){
            mappedKeyCode = 0x59;
        } else if(keyCode == Qt::Key_Slash){
            mappedKeyCode = 0x54;
        } else if(keyCode == Qt::Key_8){
            mappedKeyCode = 0x60;
        } else if(keyCode == Qt::Key_5){
            mappedKeyCode = 0x5D;
        } else if(keyCode == Qt::Key_2){
            mappedKeyCode = 0x5A;
        } else if(keyCode == Qt::Key_0){
            mappedKeyCode = 0x62;
        } else if(keyCode == Qt::Key_Asterisk) {
            mappedKeyCode = 0x55;
        } else if(keyCode == Qt::Key_9){
            mappedKeyCode = 0x61;
        } else if(keyCode == Qt::Key_6){
            mappedKeyCode = 0x5E;
        } else if(keyCode == Qt::Key_3){
            mappedKeyCode = 0x5B;
        } else if(keyCode == Qt::Key_Period){
            mappedKeyCode = 0x63;
        } else if(keyCode == Qt::Key_Minus){
            mappedKeyCode = 0x56;
        } else if(keyCode == Qt::Key_Plus){
            mappedKeyCode = 0x57;
        } else if(keyCode == Qt::Key_Enter){
            mappedKeyCode = 0x58;
        }
    }else {
        if(currentModifiers!=0){
            qCDebug(log_keyboard) << "Send release command :" << keyData.toHex(' ');
            emit SerialPortManager::getInstance().sendCommandAsync(keyData, false);
            // // release previous modifier
            // SerialPortManager::getInstance().sendAsyncCommand(keyData, false);
            currentModifiers = 0;
            return;
        }

        combinedModifiers = handleKeyModifiers(modifiers, isKeyDown);
    }

    qCDebug(log_keyboard) << "isKeyDown:" << isKeyDown << ", KeyCode:"<<keyCode<<", Mapped Keycode:" << mappedKeyCode << ", modifiers: " << combinedModifiers;
    if (mappedKeyCode != 0) {
        keyData[5] = isKeyDown ? combinedModifiers : 0;
        keyData[7] = isKeyDown ? mappedKeyCode : 0;
        emit SerialPortManager::getInstance().sendCommandAsync(keyData, false);
    }
}



int KeyboardManager::handleKeyModifiers(int modifier, bool isKeyDown) {
    // Check if the modifier key is pressed or released
    int combinedModifiers = currentModifiers;

    if(modifier & Qt::ShiftModifier){
        combinedModifiers |= 0x02;
    }

    if(modifier & Qt::ControlModifier){ // Ctrl
        combinedModifiers |= 0x01;
    }

    if(modifier & Qt::AltModifier){ // Alt
        combinedModifiers |= 0x04;
    }

    // Update currentModifiers based on the modifierKeyCode and isKeyDown
    if (isKeyDown) {
        // If the key is down, add the modifier to currentModifiers
        currentModifiers |= combinedModifiers;
    } else {
        // If the key is up, remove the modifier from currentModifiers
        currentModifiers &= ~combinedModifiers;
    }

    qCDebug(log_keyboard) << "Key" << (isKeyDown?"down":"up") << "currentModifiers:" << currentModifiers << ", combinedModifiers:" << combinedModifiers;
    return combinedModifiers;
}

bool KeyboardManager::isModiferKeys(int keycode){
    return SHIFT_KEYS.contains(keycode)
           || CTRL_KEYS.contains(keycode)
           || ALT_KEYS.contains(keycode); //Shift, Ctrl, Alt
}

bool KeyboardManager::isKeypadKeys(int keycode, int modifiers){
    return KEYPAD_KEYS.contains(keycode) && modifiers == Qt::KeypadModifier;
}

void KeyboardManager::handlePastingCharacters(const QString& text, const QMap<uint8_t, int>& charMapping) {
    for (int i = 0; i < text.length(); i++) {
        QChar ch = text.at(i);
        uint8_t charString = ch.unicode();
        int key = charMapping[charString];
        bool needShift = needShiftWhenPaste(ch);
        int modifiers = needShift ? Qt::ShiftModifier : 0;
        qCDebug(log_keyboard)<< "Pasting character: " << ch << " with key: " << ((int)key) << " and modifiers: " << modifiers;
        handleKeyboardAction(key, modifiers, true);
        QThread::msleep(1);
        handleKeyboardAction(key, modifiers, false);
        QThread::msleep(1);
    }
}

void KeyboardManager::pasteTextToTarget(const QString &text) {
    auto charMappingCopy = charMapping; // Make a copy of charMapping to capture in the lambda
    QFuture<void> future = QtConcurrent::run([this, text, charMappingCopy]() {
        handlePastingCharacters(text, charMappingCopy);
    });
}


bool KeyboardManager::needShiftWhenPaste(const QChar character) {
    return character.isUpper() || NEED_SHIFT_KEYS.contains(character);
}
