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
#include "KeyboardLayouts.h"

#include <QList>
#include <QtConcurrent/QtConcurrent>
#include <QTimer>


Q_LOGGING_CATEGORY(log_keyboard, "opf.host.keyboard")

// Define static members
const QList<int> KeyboardManager::SHIFT_KEYS = {
    Qt::Key_Shift,
    160, // Shift Left
    161  // Shift Right
};

const QList<int> KeyboardManager::CTRL_KEYS = {
    Qt::Key_Control,
    162, // Ctrl Left
    163  // Ctrl Right
};

const QList<int> KeyboardManager::ALT_KEYS = {
    Qt::Key_Alt,
    164, // Menu Left
    165  // Menu Right
};

const QList<int> KeyboardManager::KEYPAD_KEYS = {
    Qt::Key_0, Qt::Key_1, Qt::Key_2, Qt::Key_3, Qt::Key_4,
    Qt::Key_5, Qt::Key_6, Qt::Key_7, Qt::Key_8, Qt::Key_9,
    Qt::Key_Return, Qt::Key_Plus, Qt::Key_Minus,
    Qt::Key_Asterisk, Qt::Key_Slash, Qt::Key_Period
};

const QMap<int, uint8_t> KeyboardManager::functionKeyMap = {
    {Qt::Key_F1, 0x3A},
    {Qt::Key_F2, 0x3B},
    {Qt::Key_F3, 0x3C},
    {Qt::Key_F4, 0x3D},
    {Qt::Key_F5, 0x3E},
    {Qt::Key_F6, 0x3F},
    {Qt::Key_F7, 0x40},
    {Qt::Key_F8, 0x41},
    {Qt::Key_F9, 0x42},
    {Qt::Key_F10, 0x43},
    {Qt::Key_F11, 0x44},
    {Qt::Key_F12, 0x45}
};

KeyboardManager::KeyboardManager(QObject *parent) : QObject(parent), 
                                            currentMappedKeyCodes()
{
    // Set US QWERTY as default layout
    setKeyboardLayout("US QWERTY");
    getKeyboardLayout();
}

QString KeyboardManager::mapModifierKeysToNames(int modifiers) {
    QStringList modifierNames;
    if (modifiers & Qt::ShiftModifier) {
        modifierNames << "Shift";
    }
    if (modifiers & Qt::ControlModifier) {
        modifierNames << "Ctrl";
    }
    if (modifiers & Qt::AltModifier) {
        modifierNames << "Alt";
    }
    if (modifiers & Qt::MetaModifier) {
        modifierNames << "Meta";
    }
    return modifierNames.join(" + ");
}

void KeyboardManager::handleKeyboardAction(int keyCode, int modifiers, bool isKeyDown) {
    QByteArray keyData = CMD_SEND_KB_GENERAL_DATA;
    unsigned int combinedModifiers = 0;

    // Debug the incoming key code with modifier names
    qCDebug(log_keyboard) << "Processing key:" << QString::number(keyCode) + "(0x" + QString::number(keyCode, 16) + ")"
                         << "with modifiers:" << mapModifierKeysToNames(modifiers)
                         << "isKeyDown:" << isKeyDown;

    // Check if it's a function key
    if (keyCode >= Qt::Key_F1 && keyCode <= Qt::Key_F12) {
        qCDebug(log_keyboard) << "Function key detected:" << keyCode;
    }

    // Check if it's a navigation key
    if (keyCode >= Qt::Key_Left && keyCode <= Qt::Key_PageDown) {
        qCDebug(log_keyboard) << "Navigation key detected:" << keyCode;
    }

    // Use current layout's keyMap instead of the static one
    mappedKeyCode = currentLayout.keyMap.value(keyCode, 0);
    qCDebug(log_keyboard) << "Mapped to scancode: 0x" + QString::number(mappedKeyCode, 16);
    qCDebug(log_keyboard) << "Current layout name:" << currentLayout.name;
    qCDebug(log_keyboard) << "Layout has" << currentLayout.keyMap.size() << "mappings";

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
            currentModifiers = 0;
            return;
        }

        combinedModifiers = handleKeyModifiers(modifiers, isKeyDown);
    }


    if(currentMappedKeyCodes.contains(mappedKeyCode)){
        if(!isKeyDown){
            currentMappedKeyCodes.remove(mappedKeyCode);
        }
    }else{
        if(isKeyDown && currentMappedKeyCodes.size() < 6){
            currentMappedKeyCodes.insert(mappedKeyCode);
        }
    }
    
    qCDebug(log_keyboard) << "isKeyDown:" << isKeyDown << ", KeyCode:"<< QString::number(keyCode, 16) <<", Mapped Keycode:" << QString::number(mappedKeyCode, 16) << ", modifiers: " << QString::number(combinedModifiers, 16);
    if (mappedKeyCode != 0) {
        keyData[5] = isKeyDown ? combinedModifiers : 0;
        int i = 0;
        for (const auto &keyCode : currentMappedKeyCodes) {
            keyData[7 + i] = keyCode;
            i++;
        }
        qCDebug(log_keyboard) << "currentMappedKeyCodes size:" << currentMappedKeyCodes.size();
        if(currentMappedKeyCodes.size() == 1 && !isKeyDown){
            for(int j = 0; j < 6; j++){
                keyData[7 + j] = 0;
            }
            currentMappedKeyCodes.clear();
        }
        qDebug() << "Send command :" << keyData.toHex(' ');
        
        emit SerialPortManager::getInstance().sendCommandAsync(keyData, false);
        currentMappedKeyCodes.clear(); //clear the mapped keycodes after send command

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
    qDebug(log_keyboard) << "Handle pasting characters now";
    for (int i = 0; i < text.length(); i++) {
        QChar ch = text.at(i);
        uint8_t charString = ch.unicode();
        int key = charMapping[charString];
        bool needShift = needShiftWhenPaste(ch);
        int modifiers = needShift ? Qt::ShiftModifier : 0;
        qCDebug(log_keyboard)<< "Pasting character: " << ch << " with key: 0x" << QString::number(key, 16) << " and modifiers: " << modifiers;
        handleKeyboardAction(key, modifiers, true);
        // QThread::msleep(1);
        handleKeyboardAction(key, modifiers, false);
        // QThread::msleep(1);
    }
}

void KeyboardManager::pasteTextToTarget(const QString &text) {
    handlePastingCharacters(text, currentLayout.charMapping);
}

bool KeyboardManager::needShiftWhenPaste(const QChar character) {
    return character.isUpper() || currentLayout.needShiftKeys.contains(character.toLatin1());
}

void KeyboardManager::sendFunctionKey(int functionKeyCode) {
    uint8_t keyCode = functionKeyMap.value(functionKeyCode, 0);
    if (keyCode != 0) {
        sendKeyToTarget(keyCode, true);  // Key press
        QThread::msleep(1);  // Small delay between press and release
        sendKeyToTarget(keyCode, false); // Key release
    } else {
        qCWarning(log_keyboard) << "Unknown function key code:" << functionKeyCode;
    }
}

void KeyboardManager::sendKeyToTarget(uint8_t keyCode, bool isPressed) {
    QByteArray keyData = CMD_SEND_KB_GENERAL_DATA;
    keyData[5] = isPressed ? currentModifiers : 0;
    keyData[7] = isPressed ? keyCode : 0;

    qCDebug(log_keyboard) << "Sending function key:" << (isPressed ? "press" : "release") << "keyCode:" << keyCode;
    emit SerialPortManager::getInstance().sendCommandAsync(keyData, false);
}

void KeyboardManager::sendCtrlAltDel() {
    QByteArray keyData = CMD_SEND_KB_GENERAL_DATA;

    // Press Ctrl+Alt
    keyData[5] = 0x05;  // 0x01 (Ctrl) | 0x04 (Alt)
    keyData[7] = CTRL_KEY;
    keyData[8] = ALT_KEY;
    emit SerialPortManager::getInstance().sendCommandAsync(keyData, false);
    QThread::msleep(1);

    // Press Del
    keyData[7] = CTRL_KEY;
    keyData[8] = ALT_KEY;
    keyData[9] = DEL_KEY;
    emit SerialPortManager::getInstance().sendCommandAsync(keyData, false);
    QThread::msleep(1);

    // Release all keys
    keyData[5] = 0x00;
    keyData[7] = 0x00;
    keyData[8] = 0x00;
    keyData[9] = 0x00;
    emit SerialPortManager::getInstance().sendCommandAsync(keyData, false);

    qCDebug(log_keyboard) << "Sent Ctrl+Alt+Del compose key";
}

void KeyboardManager::sendKey(int keyCode, int modifiers, bool isKeyDown) {
    handleKeyboardAction(keyCode, modifiers, isKeyDown);
}

void KeyboardManager::getKeyboardLayout() {
    QInputMethod *inputMethod = QGuiApplication::inputMethod();
    m_locale = inputMethod->locale();
    qCDebug(log_keyboard) << "Current keyboard layout:" << m_locale.language() << m_locale.country();
}

void KeyboardManager::setKeyboardLayout(const QString& layoutName) {
    qCDebug(log_keyboard) << "Setting keyboard layout to:" << layoutName;
    
    if (layoutName.isEmpty()) {
        qCWarning(log_keyboard) << "Empty layout name provided, using US QWERTY as default";
        currentLayout = KeyboardLayoutManager::getInstance().getLayout("US QWERTY");
    } else {
        currentLayout = KeyboardLayoutManager::getInstance().getLayout(layoutName);
    }
    
    if (currentLayout.name.isEmpty()) {
        qCWarning(log_keyboard) << "Failed to load layout:" << layoutName << ", using US QWERTY as default";
        currentLayout = KeyboardLayoutManager::getInstance().getLayout("US QWERTY");
    }
    
    // Debug the loaded layout
    qCDebug(log_keyboard) << "Loaded layout with" << currentLayout.keyMap.size() << "key mappings";
    qCDebug(log_keyboard) << "Layout name:" << currentLayout.name;
    qCDebug(log_keyboard) << "Available mappings:";
    for (auto it = currentLayout.keyMap.begin(); it != currentLayout.keyMap.end(); ++it) {
        qCDebug(log_keyboard) << "  Qt key:" << it.key() 
                             << "(0x" << QString::number(it.key(), 16) << ")"
                             << "-> Scancode: 0x" << QString::number(it.value(), 16);
    }
}

