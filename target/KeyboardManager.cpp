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
    }else {
        if(currentModifiers!=0){
            qCDebug(log_keyboard) << "sendCommand:" << keyData.toHex(' ');
            // release previous modifier
            SerialPortManager::getInstance().sendCommand(keyData, false);
            currentModifiers = 0;
        }

        combinedModifiers = handleKeyModifiers(modifiers, isKeyDown);
    }


    qCDebug(log_keyboard) << "isKeyDown:" << isKeyDown << ", KeyCode:"<<keyCode<<", Mapped Keycode:" << mappedKeyCode << ", modifiers: " << combinedModifiers;
    if (mappedKeyCode != 0) {
        keyData[5] = isKeyDown ? combinedModifiers : 0;
        keyData[7] = isKeyDown ? mappedKeyCode : 0;
        qCDebug(log_keyboard) << "sendCommand:" << keyData.toHex(' ');
        SerialPortManager::getInstance().sendCommand(keyData, false);
    }
}

int KeyboardManager::handleKeyModifiers(int modifier, bool isKeyDown) {
    qCDebug(log_keyboard) << "handleKeyModifiers:";
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
