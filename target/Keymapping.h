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

#ifndef KEYMAPPING_WIN_H
#define KEYMAPPING_WIN_H

const QMap<int, uint8_t> KeyboardManager::keyMap = {
    {Qt::Key_A, 0x04}, // a
    {Qt::Key_B, 0x05}, // b
    {Qt::Key_C, 0x06}, // c
    {Qt::Key_D, 0x07}, // d
    {Qt::Key_E, 0x08}, // e
    {Qt::Key_F, 0x09}, // f
    {Qt::Key_G, 0x0A}, // g
    {Qt::Key_H, 0x0B}, // h
    {Qt::Key_I, 0x0C}, // i
    {Qt::Key_J, 0x0D}, // j
    {Qt::Key_K, 0x0E}, // k
    {Qt::Key_L, 0x0F}, // l
    {Qt::Key_M, 0x10}, // m
    {Qt::Key_N, 0x11}, // n
    {Qt::Key_O, 0x12}, // o
    {Qt::Key_P, 0x13}, // p
    {Qt::Key_Q, 0x14}, // q
    {Qt::Key_R, 0x15}, // r
    {Qt::Key_S, 0x16}, // s
    {Qt::Key_T, 0x17}, // t
    {Qt::Key_U, 0x18}, // u
    {Qt::Key_V, 0x19}, // v
    {Qt::Key_W, 0x1A}, // w
    {Qt::Key_X, 0x1B}, // x
    {Qt::Key_Y, 0x1C}, // y
    {Qt::Key_Z, 0x1D}, // z
    {Qt::Key_0, 0x27}, // 0
    {Qt::Key_1, 0x1E}, // 1
    {Qt::Key_2, 0x1F}, // 2
    {Qt::Key_3, 0x20}, // 3
    {Qt::Key_4, 0x21}, // 4
    {Qt::Key_5, 0x22}, // 5
    {Qt::Key_6, 0x23}, // 6
    {Qt::Key_7, 0x24}, // 7
    {Qt::Key_8, 0x25}, // 8
    {Qt::Key_9, 0x26}, // 9
    {Qt::Key_Return, 0x28}, // Big enter
    {Qt::Key_Enter, 0x58}, // Numpad enter
    {Qt::Key_Escape, 0x29}, // esc
    {Qt::Key_Backspace, 0x2A}, // backspace
    {Qt::Key_Tab, 0x2B}, // tab
    {Qt::Key_Space, 0x2C}, // space
    {Qt::Key_Minus, 0x2D}, // -
    {Qt::Key_Equal, 0x2E}, // =
    {Qt::Key_BracketLeft, 0x2F}, // [
    {Qt::Key_BracketRight, 0x30}, // ]
    {Qt::Key_Backslash, 0x31}, // Backslash
    {Qt::Key_Semicolon, 0x33}, // ;
    // {Qt::Key_Apostrophe, 0x34}, // '
    {Qt::Key_QuoteLeft, 0x35}, // `
    {Qt::Key_Comma, 0x36}, // ,
    {Qt::Key_Period, 0x37}, // .
    {Qt::Key_Slash, 0x38}, // /
    {Qt::Key_CapsLock, 0x39}, // caps lock
    {Qt::Key_F1, 0x3A}, // f1
    {Qt::Key_F2, 0x3B}, // f2
    {Qt::Key_F3, 0x3C}, // f3
    {Qt::Key_F4, 0x3D}, // f4
    {Qt::Key_F5, 0x3E}, // f5
    {Qt::Key_F6, 0x3F}, // f6
    {Qt::Key_F7, 0x40}, // f7
    {Qt::Key_F8, 0x41}, // f8
    {Qt::Key_F9, 0x42}, // f9
    {Qt::Key_F10, 0x43}, // f10
    {Qt::Key_F11, 0x44}, // f11
    {Qt::Key_F12, 0x45}, // f12
    {Qt::Key_Print, 0x46}, // print screen
    {Qt::Key_ScrollLock, 0x47}, // scroll lock
    {Qt::Key_Pause, 0x48}, // pause
    {Qt::Key_Insert, 0x49}, // insert
    {Qt::Key_Home, 0x4A}, // home
    {Qt::Key_PageUp, 0x4B}, // page up
    {Qt::Key_Delete, 0x4C}, // delete
    {Qt::Key_End, 0x4D}, // end
    {Qt::Key_PageDown, 0x4E}, // page down
    {Qt::Key_Right, 0x4F}, // right arrow
    {Qt::Key_Left, 0x50}, // left arrow
    {Qt::Key_Down, 0x51}, // down arrow
    {Qt::Key_Up, 0x52}, // up arrow
    {Qt::Key_NumLock, 0x53}, // num lock
    //{Qt::Key_Slash, 0x54}, // keypad slash
    {Qt::Key_Asterisk, 0x55}, // keypad *
    {Qt::Key_Plus, 0x57}, // keypad +
    {Qt::Key_Shift, 0xE5}, // Right Shift
    {Qt::Key_Control, 0xE4}, // Right Ctrl
    {Qt::Key_Alt, 0xE6}, // Right Alt
    {Qt::Key_AsciiTilde , 0x35}, // key `
    {Qt::Key_Exclam, 0x1E}, // key 1
    {Qt::Key_At, 0x1F}, // key 2
    {Qt::Key_NumberSign, 0x20}, // key 3
    {Qt::Key_Dollar, 0x21}, // key 4
    {Qt::Key_Percent, 0x22}, // key 5
    {Qt::Key_AsciiCircum, 0x23}, // key 6
    {Qt::Key_Ampersand, 0x24}, // key 7
    {Qt::Key_Asterisk, 0x2A}, // key *
    {Qt::Key_ParenLeft, 0x26}, // key 9
    {Qt::Key_ParenRight, 0x27}, // key 0
    {Qt::Key_Underscore, 0x2D}, // key -
    {Qt::Key_Plus, 0x2E}, // key =
    {Qt::Key_BraceLeft, 0x2F}, // key [
    {Qt::Key_BraceRight, 0x30}, // key ]
    {Qt::Key_Colon, 0x33}, // key ;
    {Qt::Key_QuoteDbl, 0x34}, // key '
    {Qt::Key_Bar, 0x31}, // Backslash
    {Qt::Key_Less, 0x36}, // key ,
    {Qt::Key_Greater, 0x37}, // key .
    {Qt::Key_Question, 0x38} // key /
};

const QList<int> KeyboardManager::SHIFT_KEYS = {
    Qt::Key_Shift, // Shift
    160, // Shift Left
    161 // Shift Right
};

const QList<int> KeyboardManager::CTRL_KEYS = {
    Qt::Key_Control, // Ctrl
    162, // Ctrl Left
    163 // Ctrl Right
};

const QList<int> KeyboardManager::ALT_KEYS = {
    Qt::Key_Alt, // Alt
    164, // Menu Left
    165 // Menu Right
};
#endif // WINKEYMAPPING_H
