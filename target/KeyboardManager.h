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

#include <QObject>
#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(log_host_keyboard)

class KeyboardManager: public QObject
{
    Q_OBJECT

public:
    explicit KeyboardManager(SerialPortManager& spm, QObject *parent = nullptr);

    void handleKeyboardAction(int keyCode, int modifiers, bool isKeyDown);

    bool isModiferKeys(int keycode);

private:
    SerialPortManager& serialPortManager;
    static const QMap<int, uint8_t> keyMap;
    static const QList<int> SHIFT_KEYS;
    static const QList<int> CTRL_KEYS;
    static const QList<int> ALT_KEYS;
    
    int handleKeyModifiers(int modifierKeyCode, bool isKeyDown);
    int currentModifiers = 0;
};

#endif // KEYBOARDMANAGER_H
