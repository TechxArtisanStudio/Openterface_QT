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

#ifndef KEYBOARDMANAGER_H
#define KEYBOARDMANAGER_H

#include "../serial/SerialPortManager.h"
#include "ui/statusevents.h"
#include "KeyboardLayouts.h"

#include <QObject>
#include <QLoggingCategory>
#include <QSet>
#include <QMap>
#include <QLocale>
#include <QApplication>
#include <QInputMethod>
#include <QKeyEvent>

Q_DECLARE_LOGGING_CATEGORY(log_host_keyboard)

class KeyboardManager: public QObject
{
    Q_OBJECT

public:
    explicit KeyboardManager(QObject *parent = nullptr);

    void handleKeyboardAction(int keyCode, int modifiers, bool isKeyDown);
    void handlePasteChar(int key, int modifiers);

    /*
     * Check if the key is a modifier key, eg: shift, ctrl, alt
     */
    bool isModiferKeys(int keycode);

    /*
     * Check if the key is a keypad key
     */
    bool isKeypadKeys(int keycode, int modifiers);

    void pasteTextToTarget(const QString &text);

    /*
     * Send F1 to F12 functional keys
     */
    void sendFunctionKey(int functionKeyCode);

    /*
     * Send ctrl + alt + del composite keys
     */
    void sendCtrlAltDel();

    void sendKey(int keyCode, int modifiers, bool isKeyDown);

    void setKeyboardLayout(const QString& layoutName);

private:
    QSet<unsigned int> currentMappedKeyCodes;

    void handlePastingCharacters(const QString& text, const QMap<uint8_t, int>& charMapping);
    bool needShiftWhenPaste(const QChar character);
    bool needAltGrWhenPaste(const QChar character);
    
    int handleKeyModifiers(int modifierKeyCode, bool isKeyDown);
    int currentModifiers = 0;

    // Add this new method
    void sendKeyToTarget(uint8_t keyCode, bool isPressed);

    // Add these new constants
    static const uint8_t CTRL_KEY = 0xE0;
    static const uint8_t ALT_KEY = 0xE2;
    static const uint8_t DEL_KEY = 0x4C;
    
    QLocale m_locale;
    void getKeyboardLayout();
    unsigned int mappedKeyCode;

    KeyboardLayoutConfig currentLayout;

    // Define static members
    static const QList<int> SHIFT_KEYS;
    static const QList<int> CTRL_KEYS;
    static const QList<int> ALT_KEYS;
    static const QList<int> KEYPAD_KEYS;
    static const QMap<int, uint8_t> functionKeyMap;

    // Add these constants for key mappings
    static const QMap<int, uint8_t> defaultKeyMap;
    static const QMap<uint8_t, int> defaultCharMapping;
    static const QList<char> defaultNeedShiftKeys;
    QString mapModifierKeysToNames(int modifiers);

private slots:
};

#endif // KEYBOARDMANAGER_H
