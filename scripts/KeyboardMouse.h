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

#ifndef KEYBOARDMOUSE_H
#define KEYBOARDMOUSE_H
#include <cstdint>
#include <array>
#include <queue>
#include <QByteArray>
#include <QMap>
#include <QString>
#include <QDebug>
#include <QObject>
#include "serial/SerialPortManager.h"
#include "AST.h"


// keyboard data packet
struct keyPacket
{
    uint8_t control = 0x00;
    uint8_t constant = 0x00;
    std::array<uint8_t, 6> general;

    keyPacket(const std::array<uint8_t, 6>& gen, uint8_t ctrl = 0x00)
        : control(ctrl), general(gen) {}

    QByteArray toQByteArray() const {
        QByteArray byteArray;
        byteArray.append(control);
        byteArray.append(constant);
        for (const auto& byte : general) {
            byteArray.append(byte);
        }
        return byteArray;
    }
};

class KeyboardMouse : public QObject
{
    Q_OBJECT

public:
    explicit KeyboardMouse(QObject *parent = nullptr);

    void addKeyPacket(const keyPacket& packet);
    void executeCommand();
    void updateNumCapsScrollLockState();
    bool getNumLockState_();
    bool getCapsLockState_();
    bool getScrollLockState_();

private:
    std::queue<keyPacket> keyData;

};

const QMap<QString, uint8_t> controldata = {
    {"^", 0x01}, // Ctrl
    {"+", 0x02}, // Shift
    {"!", 0x04}, // Alt
    {"#", 0x08}  // Win
};



const QMap<QString, uint8_t> keydata = {
    {"a", 0x04}, // a
    {"b", 0x05}, // b
    {"c", 0x06}, // c
    {"d", 0x07}, // d
    {"e", 0x08}, // e
    {"f", 0x09}, // f
    {"g", 0x0A}, // g
    {"h", 0x0B}, // h
    {"i", 0x0C}, // i
    {"j", 0x0D}, // j
    {"k", 0x0E}, // k
    {"l", 0x0F}, // l
    {"m", 0x10}, // m
    {"n", 0x11}, // n
    {"o", 0x12}, // o
    {"p", 0x13}, // p
    {"q", 0x14}, // q
    {"r", 0x15}, // r
    {"s", 0x16}, // s
    {"t", 0x17}, // t
    {"u", 0x18}, // u
    {"v", 0x19}, // v
    {"w", 0x1A}, // w
    {"x", 0x1B}, // x
    {"y", 0x1C}, // y
    {"z", 0x1D}, // z
    {"0", 0x27}, // 0
    {"1", 0x1E}, // 1
    {"2", 0x1F}, // 2
    {"3", 0x20}, // 3
    {"4", 0x21}, // 4
    {"5", 0x22}, // 5
    {"6", 0x23}, // 6
    {"7", 0x24}, // 7
    {"8", 0x25}, // 8
    {"9", 0x26}, // 9
    {"Enter", 0x28}, // Big enter
    {"Enter", 0x58}, // Numpad enter
    {"Escape", 0x29}, // esc
    {"Esc", 0x29}, // esc
    {"Backspace", 0x2A}, // backspace
    {"BS", 0x2A}, // backspace
    {"Tab", 0x2B}, // tab
    {"Space", 0x2C}, // space
    {"Minus", 0x2D}, // -
    {"Equal", 0x2E}, // =
    {"BracketLeft", 0x2F}, // [
    {"BracketRight", 0x30}, // ]
    {"Backslash", 0x31}, // Backslash
    {"Semicolon", 0x33}, // ;
    {"Apostrophe", 0x34}, // '
    {"QuoteLeft", 0x35}, // `
    {"Comma", 0x36}, // ,
    {"Period", 0x37}, // .
    {"Slash", 0x38}, // /
    {"CapsLock", 0x39}, // caps lock
    {"F1", 0x3A}, // f1
    {"F2", 0x3B}, // f2
    {"F3", 0x3C}, // f3
    {"F4", 0x3D}, // f4
    {"F5", 0x3E}, // f5
    {"F6", 0x3F}, // f6
    {"F7", 0x40}, // f7
    {"F8", 0x41}, // f8
    {"F9", 0x42}, // f9
    {"F10", 0x43}, // f10
    {"F11", 0x44}, // f11
    {"F12", 0x45}, // f12
    {"PrintScreen", 0x46}, // print screen
    {"ScrollLock", 0x47}, // scroll lock
    {"Pause", 0x48}, // pause
    {"Insert", 0x49}, // insert
    {"Ins", 0x49}, // insert
    {"Home", 0x4A}, // home
    {"PgUp", 0x4B}, // page up
    {"Delete", 0x4C}, // delete
    {"Del", 0x4C}, // delete
    {"End", 0x4D}, // end
    {"PgDn", 0x4E}, // page down
    {"Right", 0x4F}, // right arrow
    {"Left", 0x50}, // left arrow
    {"Down", 0x51}, // down arrow
    {"Up", 0x52}, // up arrow
    {"NumLock", 0x53}, // num lock
    //{"Slash", 0x54}, // keypad slash
    {"Asterisk", 0x55}, // keypad *
    {"Plus", 0x57}, // keypad +
    {"Shift", 0xE5}, // Right Shift
    {"Control", 0xE4}, // Right Ctrl
    {"Ctrl", 0xE4}, // Right Ctrl
    {"Alt", 0xE6}, // Right Alt
    {"AsciiTilde", 0x35}, // key `
    {"Exclam", 0x1E}, // key 1
    {"At", 0x1F}, // key 2
    {"NumberSign", 0x20}, // key 3
    {"Dollar", 0x21}, // key 4
    {"Percent", 0x22}, // key 5
    {"AsciiCircum", 0x23}, // key 6
    {"Ampersand", 0x24}, // key 7
    {"Asterisk", 0x25}, // key *
    {"ParenLeft", 0x26}, // key 9
    {"ParenRight", 0x27}, // key 0
    {"Underscore", 0x2D}, // key -
    {"Plus", 0x2E}, // key =
    {"BraceLeft", 0x2F}, // key [
    {"BraceRight", 0x30}, // key ]
    {"Colon", 0x33}, // key ;
    {"QuoteDbl", 0x34}, // key '
    {"Bar", 0x31}, // Backslash
    {"Less", 0x36}, // key ,
    {"Greater", 0x37}, // key .
    {"Question", 0x38}, // key /
    {"Win", 0xE3}, // win
    {"^", 0xE4}, // Ctrl
    {"+", 0xE5}, // Shift
    {"!", 0xE6}, // Alt
    {"#", 0xE3}  // Win
};


#endif // KEYBOARDMOUSE_H
