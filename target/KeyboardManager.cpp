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
#include "../serial/ch9329.h"

#include <QList>
#include <QtConcurrent/QtConcurrent>
#include <QTimer>
#include <cstdint>
#include <array>


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
    165,  // Menu Right
    Qt::Key_AltGr
};

const QList<int> KeyboardManager::KEYPAD_KEYS = {
    Qt::Key_0, Qt::Key_1, Qt::Key_2, Qt::Key_3, Qt::Key_4,
    Qt::Key_5, Qt::Key_6, Qt::Key_7, Qt::Key_8, Qt::Key_9,
    Qt::Key_Return, Qt::Key_Plus, Qt::Key_Minus,
    Qt::Key_Asterisk, Qt::Key_Slash, Qt::Key_Period, 
    Qt::Key_NumLock, Qt::Key_ScrollLock
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

void KeyboardManager::handleKeyboardAction(int keyCode, int modifiers, bool isKeyDown, unsigned int nativeVirtualKey) {
    QByteArray keyData = CMD_SEND_KB_GENERAL_DATA;
    unsigned int combinedModifiers = 0;

    // Debug the incoming key code with modifier names and native VK (when available)
    qCDebug(log_keyboard) << "Processing key:" << QString::number(keyCode) + "(0x" + QString::number(keyCode, 16) + ")"
                         << "with modifiers:" << mapModifierKeysToNames(modifiers)
                         << "isKeyDown:" << isKeyDown << " nativeVK:" << QString::number(nativeVirtualKey, 16);

    if(nativeVirtualKey == 0){
        // Check if it's a function key
        if (keyCode >= Qt::Key_F1 && keyCode <= Qt::Key_F12) {
            qCDebug(log_keyboard) << "Function key detected:" << keyCode;
        }

        // Check if it's a navigation key
        if (keyCode >= Qt::Key_Left && keyCode <= Qt::Key_PageDown) {
            qCDebug(log_keyboard) << "Navigation key detected:" << keyCode;
        }
    }

    // Use current layout's keyMap instead of the static one
    mappedKeyCode = currentLayout.keyMap.value(keyCode, 0);

#ifdef Q_OS_WIN
    // Handle Japanese IME keys using Windows Virtual Key codes
    // Only apply for Japanese keyboard layouts (JIS)
    if (currentLayout.name.contains("Japanese") || currentLayout.name.contains("JIS")) {
        // Windows Virtual Key codes for Japanese IME:
        // VK_NONCONVERT (0x1D / 29) = 無変換 (Muhenkan)
        // VK_CONVERT    (0x1C / 28) = 変換 (Henkan)
        // VK_OEM_ATTN   (0xF0 / 240) and related = 半角/全角 (Zenkaku/Hankaku)
        constexpr unsigned int VK_NONCONVERT = 0x1D;  // 29
        constexpr unsigned int VK_CONVERT = 0x1C;     // 28
        constexpr unsigned int VK_OEM_AUTO = 0xF3;    // 243
        constexpr unsigned int VK_OEM_ENLW = 0xF4;    // 244

        if (nativeVirtualKey == VK_NONCONVERT) {
            mappedKeyCode = currentLayout.keyMap.value(Qt::Key_Muhenkan, 0);
            qCDebug(log_keyboard) << "Muhenkan key detected: VK=" << nativeVirtualKey << "isKeyDown:" << isKeyDown;
        } else if (nativeVirtualKey == VK_CONVERT) {
            mappedKeyCode = currentLayout.keyMap.value(Qt::Key_Henkan, 0);
            qCDebug(log_keyboard) << "Henkan key detected: VK=" << nativeVirtualKey << "isKeyDown:" << isKeyDown;
        } else if (nativeVirtualKey == VK_OEM_AUTO || nativeVirtualKey == VK_OEM_ENLW) {
            // TODO: Implement Zenkaku/Hankaku key mapping when keyMap supports it
            // mappedKeyCode = currentLayout.keyMap.value(Qt::Key_Zenkaku_Hankaku, 0);
            qCDebug(log_keyboard) << "ZenkakuHankaku key detected: VK=" << nativeVirtualKey << "isKeyDown:" << isKeyDown;
        }
    }
#endif

    if (mappedKeyCode == 0) {
        uint32_t unicodeValue = keyCode;
        qDebug() << "Unicode key detected:" << QString::number(unicodeValue, 16);
        if (unicodeValue == 0x1000002) {
            mappedKeyCode = 0x2B;
            qDebug() << "tab key detected:" << QString::number(unicodeValue, 16);
        }else if(unicodeValue == 0x1000005){
            mappedKeyCode = 0x58;
            qDebug() << "enter key detected:" << QString::number(unicodeValue, 16);
        }else if(unicodeValue == 0x1000026){
            mappedKeyCode = 0x47;
            qDebug() << "scroll lock key detected:" << QString::number(unicodeValue, 16);
        }
        else{
            mappedKeyCode = currentLayout.unicodeMap.value(unicodeValue, 0);
            qDebug() << "Trying Unicode mapping for U+" << QString::number(unicodeValue, 16) 
                                << "-> scancode: 0x" << QString::number(mappedKeyCode, 16);

            if (mappedKeyCode != 0) {
                QChar unicodeChar(unicodeValue);
                if (currentLayout.needAltGrKeys.contains(unicodeValue)) {
                    qDebug() << "Character requires AltGr, forcing modifier";
                    modifiers |= Qt::GroupSwitchModifier;
                }
            }
        }
    }
    qCDebug(log_keyboard) << "Mapped to scancode: 0x" + QString::number(mappedKeyCode, 16);
    qCDebug(log_keyboard) << "Current layout name:" << currentLayout.name;
    qCDebug(log_keyboard) << "Layout has" << currentLayout.keyMap.size() << "mappings";

    if(isModiferKeys(keyCode)){
        // Distinguish the left or right modifiers using nativeVirtualKey where available
        if (nativeVirtualKey != 0) {
            switch (nativeVirtualKey) {
                case 0xA0: // VK_LSHIFT
                    mappedKeyCode = 0xE1; // left shift
                    if (isKeyDown) currentModifiers |= 0x02; else currentModifiers &= ~0x02;
                    combinedModifiers = isKeyDown ? 0x02 : 0x00;
                    qCDebug(log_keyboard) << "Detected Left Shift (VK 0xA0)";
                    break;
                case 0xA1: // VK_RSHIFT
                    mappedKeyCode = 0xE5; // right shift
                    if (isKeyDown) currentModifiers |= 0x02; else currentModifiers &= ~0x02;
                    combinedModifiers = isKeyDown ? 0x02 : 0x00;
                    qCDebug(log_keyboard) << "Detected Right Shift (VK 0xA1)";
                    break;
                case 0xA2: // VK_LCONTROL
                    mappedKeyCode = 0xE0; // left ctrl
                    if (isKeyDown) currentModifiers |= 0x01; else currentModifiers &= ~0x01;
                    combinedModifiers = isKeyDown ? 0x01 : 0x00;
                    qCDebug(log_keyboard) << "Detected Left Ctrl (VK 0xA2)";
                    break;
                case 0xA3: // VK_RCONTROL
                    mappedKeyCode = 0xE4; // right ctrl
                    if (isKeyDown) currentModifiers |= 0x01; else currentModifiers &= ~0x01;
                    combinedModifiers = isKeyDown ? 0x01 : 0x00;
                    qCDebug(log_keyboard) << "Detected Right Ctrl (VK 0xA3)";
                    break;
                case 0xA4: // VK_LMENU (Left Alt)
                    mappedKeyCode = 0xE2; // left alt
                    if (isKeyDown) currentModifiers |= 0x04; else currentModifiers &= ~0x04;
                    combinedModifiers = isKeyDown ? 0x04 : 0x00;
                    qCDebug(log_keyboard) << "Detected Left Alt (VK 0xA4)";
                    break;
                case 0xA5: // VK_RMENU (Right Alt)
                    mappedKeyCode = 0xE6; // right alt / AltGr
                    if (isKeyDown) currentModifiers |= 0x40; else currentModifiers &= ~0x40;
                    combinedModifiers = isKeyDown ? 0x40 : 0x00;
                    qCDebug(log_keyboard) << "Detected Right Alt (VK 0xA5)";
                    break;
                default:
                    qCDebug(log_keyboard) << "Unknown native VK, falling back to legacy detection";
            }
        }

        // Fallback for platforms where nativeVirtualKey is unavailable or unknown
        if (mappedKeyCode == 0) {
            if( modifiers == 1537){ // left shift
                mappedKeyCode = 0xe1;
                if (isKeyDown) currentModifiers |= 0x02; else currentModifiers &= ~0x02;
                combinedModifiers = isKeyDown ? 0x02 : 0x00;
            } else if(modifiers == 1538){// left ctrl
                mappedKeyCode = 0xe0;
                if (isKeyDown) currentModifiers |= 0x01; else currentModifiers &= ~0x01;
                combinedModifiers = isKeyDown ? 0x01 : 0x00;
            } else if(modifiers == 1540){ //left alt
                mappedKeyCode = 0xe2;
                if (isKeyDown) currentModifiers |= 0x04; else currentModifiers &= ~0x04;
                combinedModifiers = isKeyDown ? 0x04 : 0x00;
            }else if(modifiers & Qt::GroupSwitchModifier){ // altgr
                mappedKeyCode = 0xE6;
                if (isKeyDown) currentModifiers |= 0x40; else currentModifiers &= ~0x40;
                combinedModifiers = isKeyDown ? 0x40 : 0x00;
            }
        }
    }else if(nativeVirtualKey == 0 && isKeypadKeys(keyCode, modifiers)){
        if (keyCode == Qt::Key_NumLock) {
            mappedKeyCode = 0x53;
        }
        if (keyCode == Qt::Key_ScrollLock) {
            mappedKeyCode = 0x47;
        }
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
        if(currentModifiers!=0 && mappedKeyCode == 0){
            qCDebug(log_keyboard) << "Send release command :" << keyData.toHex(' ');
            emit SerialPortManager::getInstance().sendCommandAsync(keyData, false);
            currentModifiers = 0;
            return;
        }

        combinedModifiers = handleKeyModifiers(modifiers, isKeyDown);
    }


    if (mappedKeyCode != 0) {
        // Update currentMappedKeyCodes: add on press, remove on release
        if (isKeyDown) {
            if (!currentMappedKeyCodes.contains(mappedKeyCode) && currentMappedKeyCodes.size() < 6) {
                currentMappedKeyCodes.insert(mappedKeyCode);
            }
        } else {
            currentMappedKeyCodes.remove(mappedKeyCode);
        }

        // Always use currentModifiers for the modifier byte
        keyData[5] = currentModifiers;

        // Set the key array from currentMappedKeyCodes
        int i = 0;
        for (const auto &key : currentMappedKeyCodes) {
            keyData[7 + i] = key;
            i++;
        }
        // Fill remaining slots with 0
        for (; i < 6; ++i) {
            keyData[7 + i] = 0;
        }

        // Send the command
        emit SerialPortManager::getInstance().sendCommandAsync(keyData, false);

        // If this is a lock key (NumLock, CapsLock, or ScrollLock), request key state update
        if (isLockKey(keyCode)) {
            qCDebug(log_keyboard) << "Lock key detected, requesting key state update";
            // Add a small delay to allow the target device to process the key
            QTimer::singleShot(50, []() {
                emit SerialPortManager::getInstance().sendCommandAsync(CMD_GET_INFO, false);
            });
        }
    }
}

void KeyboardManager::handlePasteChar(int key, int modifiers){
    unsigned int control = 0x00;
    QByteArray keyData = CMD_SEND_KB_GENERAL_DATA;
    unsigned int mappedKey = currentLayout.keyMap.value(key, 0);
    if (mappedKey == 0) {
        uint32_t unicodeValue = key;
        mappedKey = currentLayout.unicodeMap.value(unicodeValue, 0);
    }
    switch (modifiers){
        case Qt::ShiftModifier:
            control = 0x02;
            break;
        case Qt::GroupSwitchModifier:
            control = 0x40;
            break;
        default:
            control = 0x00;
            break;
    }
    keyData[5] = control;
    keyData[7] = mappedKey;
    // QThread::msleep(2);
    emit SerialPortManager::getInstance().sendCommandAsync(keyData, false);
    QThread::msleep(3);
    emit SerialPortManager::getInstance().sendCommandAsync(CMD_SEND_KB_GENERAL_DATA, false);
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

    if (modifier & Qt::GroupSwitchModifier) {
        combinedModifiers &= ~0x04;
        combinedModifiers |= 0x40;  // Alt modifier
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
    if (keycode == Qt::Key_AltGr) {
        return true;
    }
    return SHIFT_KEYS.contains(keycode)
           || CTRL_KEYS.contains(keycode)
           || ALT_KEYS.contains(keycode); //Shift, Ctrl, Alt
}

bool KeyboardManager::isKeypadKeys(int keycode, int modifiers){
    return KEYPAD_KEYS.contains(keycode) && modifiers == Qt::KeypadModifier;
}

bool KeyboardManager::isLockKey(int keycode) {
    return keycode == Qt::Key_NumLock || keycode == Qt::Key_CapsLock || keycode == Qt::Key_ScrollLock;
}

void KeyboardManager::handlePastingCharacters(const QString& text, const QMap<uint8_t, int>& charMapping) {
    qDebug(log_keyboard) << "Handle pasting characters now";
    
    // Store text and mapping for processing
    QString remainingText = text;
    QMap<uint8_t, int> mapping = charMapping;
    
    const int batchSize = 10;
    const int delayBetweenBatches = 5; // Delay between batches in ms
    const int delayBetweenChars = 3;    // Delay between characters in ms
    
    QTimer* pasteTimer = new QTimer(this);
    connect(pasteTimer, &QTimer::timeout, this, [=]() mutable {
        // Process up to batchSize characters
        for (int i = 0; i < batchSize && !remainingText.isEmpty(); ++i) {
            QChar ch = remainingText.at(0);
            uint8_t charString = ch.unicode();
            int key = mapping[charString];

            bool needShift = needShiftWhenPaste(ch);
            bool needAltGr = needAltGrWhenPaste(ch);

            int modifiers = 0;
            if (needShift) modifiers |= Qt::ShiftModifier;
            if (needAltGr) modifiers |= Qt::GroupSwitchModifier;

            handlePasteChar(key, modifiers);
            QThread::msleep(delayBetweenChars);
            emit SerialPortManager::getInstance().sendCommandAsync(CMD_SEND_KB_GENERAL_DATA, false);
            
            remainingText.remove(0, 1);
        }
        
        // Stop timer if no more characters to process
        if (remainingText.isEmpty()) {
            pasteTimer->stop();
            pasteTimer->deleteLater();
        }
    });
    
    // Start timer with delay between batches
    pasteTimer->start(delayBetweenBatches);
}

void KeyboardManager::pasteTextToTarget(const QString &text) {
    qDebug() << "Paste text to target:" << text;
    handlePastingCharacters(text, currentLayout.charMapping);
}

bool KeyboardManager::needShiftWhenPaste(const QChar character) {
    return character.isUpper() || currentLayout.needShiftKeys.contains(character.unicode());
}

bool KeyboardManager::needAltGrWhenPaste(const QChar character) {
    return currentLayout.needAltGrKeys.contains(character.unicode());
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
    qCDebug(log_keyboard) << "Current keyboard layout:" << m_locale.language() << m_locale.territory();
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
