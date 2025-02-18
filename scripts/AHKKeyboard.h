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



#ifndef AHKKEYBOARD_H
#define AHKKEYBOARD_H

#include <QMap>

// ... existing code ...
const QMap<QString, Qt::Key> AHKmapping = {
    {"a", Qt::Key_A}, // a
    {"b", Qt::Key_B}, // b
    {"c", Qt::Key_C}, // c
    {"d", Qt::Key_D}, // d
    {"e", Qt::Key_E}, // e
    {"f", Qt::Key_F}, // f
    {"g", Qt::Key_G}, // g
    {"h", Qt::Key_H}, // h
    {"i", Qt::Key_I}, // i
    {"j", Qt::Key_J}, // j
    {"k", Qt::Key_K}, // k
    {"l", Qt::Key_L}, // l
    {"m", Qt::Key_M}, // m
    {"n", Qt::Key_N}, // n
    {"o", Qt::Key_O}, // o
    {"p", Qt::Key_P}, // p
    {"q", Qt::Key_Q}, // q
    {"r", Qt::Key_R}, // r
    {"s", Qt::Key_S}, // s
    {"t", Qt::Key_T}, // t
    {"u", Qt::Key_U}, // u
    {"v", Qt::Key_V}, // v
    {"w", Qt::Key_W}, // w
    {"x", Qt::Key_X}, // x
    {"y", Qt::Key_Y}, // y
    {"z", Qt::Key_Z}, // z
    {"0", Qt::Key_0}, // 0
    {"1", Qt::Key_1}, // 1
    {"2", Qt::Key_2}, // 2
    {"3", Qt::Key_3}, // 3
    {"4", Qt::Key_4}, // 4
    {"5", Qt::Key_5}, // 5
    {"6", Qt::Key_6}, // 6
    {"7", Qt::Key_7}, // 7
    {"8", Qt::Key_8}, // 8
    {"9", Qt::Key_9}, // 9
    {"NumberpadEnter", Qt::Key_Return}, // NumberpadEnter
    {"NumpadSub", Qt::Key_Minus}, // NumpadSub
    {"F1", Qt::Key_F1}, // F1
    {"F2", Qt::Key_F2}, // F2
    {"F3", Qt::Key_F3}, // F3
    {"F4", Qt::Key_F4}, // F4
    {"F5", Qt::Key_F5}, // F5
    {"F6", Qt::Key_F6}, // F6
    {"F7", Qt::Key_F7}, // F7
    {"F8", Qt::Key_F8}, // F8
    {"F9", Qt::Key_F9}, // F9
    {"F10", Qt::Key_F10}, // F10
    {"F11", Qt::Key_F11}, // F11
    {"F12", Qt::Key_F12}, // F12
    {"!", Qt::Key_Exclam}, // !
    {"#", Qt::Key_NumberSign}, // #
    {"+", Qt::Key_Plus}, // +
    {"^", Qt::Key_AsciiCircum}, // ^
    {"{", Qt::Key_BraceLeft}, // {
    {"}", Qt::Key_BraceRight}, // }
    {"Enter", Qt::Key_Return}, // Enter
    {"Esc", Qt::Key_Escape}, // esc
    {"Escape", Qt::Key_Escape}, // Escape
    {"Space", Qt::Key_Space}, // space
    {"Tab", Qt::Key_Tab}, // Tab
    {"Backspace", Qt::Key_Backspace}, // Backspace
    {"BS", Qt::Key_Backspace}, // Backspace
    {"Del", Qt::Key_Delete}, // Del
    {"Delete", Qt::Key_Delete}, // Delete
    {"Insert", Qt::Key_Insert}, // Insert
    {"Ins", Qt::Key_Insert}, // Ins
    {"Up", Qt::Key_Up}, // Up
    {"Down", Qt::Key_Down}, // Down
    {"Left", Qt::Key_Left}, // Left
    {"Right", Qt::Key_Right}, // Right
    {"Home", Qt::Key_Home}, // Home
    {"End", Qt::Key_End}, // End
    {"PgUp", Qt::Key_PageUp}, // PgUp
    {"PgDn", Qt::Key_PageDown}, // PgDn
    {"CapsLock", Qt::Key_CapsLock}, // CapsLock
    {"ScrollLock", Qt::Key_ScrollLock}, // ScrollLock
    {"NumLock", Qt::Key_NumLock}, // NumLock
    {"Control", Qt::Key_Control}, // Control right
    {"Ctrl", Qt::Key_Control}, // Control right
    {"LControl", Qt::Key_Control}, // Control left
    {"LCtrl", Qt::Key_Control}, // Control left
    {"Alt", Qt::Key_Alt}, // Alt
    {"RAlt", Qt::Key_Alt}, // RAlt
    {"Shift", Qt::Key_Shift}, // Shift
    {"LWin", Qt::Key_Meta}, // LWin
    {"RWin", Qt::Key_Meta}, // RWin 
    {"AppsKey", Qt::Key_Menu} // APPWin
};
// ... existing code ...

/*
    byte 0: Ctrl
    byte 1: Shift
    byte 2: Alt
    byte 3: Win
*/

const QMap<QString, Qt::Key> specialKeys = {
    {"^", Qt::Key_Control}, // Ctrl
    {"+", Qt::Key_Shift}, // Shift
    {"!", Qt::Key_Alt}, // Alt
    {"#", Qt::Key_Meta}  // Win
};


#endif // AHKKEYBOARD_H
