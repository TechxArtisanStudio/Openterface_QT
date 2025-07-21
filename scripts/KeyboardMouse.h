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
union Coordinate {
    struct {
        uint8_t x[2];
        uint8_t y[2];
    } abs;
    struct {
        uint8_t x;
        uint8_t y;
    } rel;
};

struct keyPacket {
    // data of keys
    uint8_t control = 0x00;
    uint8_t constant = 0x00;
    std::array<uint8_t, 6> keyGeneral;

    // Reorder the member variables
    uint8_t mouseMode = 0x00;
    uint8_t mouseButton = 0x00;
    uint8_t mouseRollWheel = 0x00;
    uint8_t mouseClickCount = 1;
    union Coordinate mouseCoord;

    bool mouseSendOrNot = false;
    bool keyboardSendOrNot = false;
    bool keyboardMouseSendOrNot = false;
    // packet key data
    keyPacket(const std::array<uint8_t, 6>& gen, uint8_t ctrl = 0x00)
        : control(ctrl), keyGeneral(gen) 
        {keyboardSendOrNot = true; mouseSendOrNot = false; keyboardMouseSendOrNot = false; }

    // packet Mouse data (unified for ABS and REL)
    keyPacket(const std::array<uint8_t, 6>& gen, uint8_t ctrl, uint8_t mouseMode, uint8_t mouseButton, uint8_t mouseRollWheel, const Coordinate& coord)
        : control(ctrl), keyGeneral(gen), mouseMode(mouseMode), mouseButton(mouseButton), mouseRollWheel(mouseRollWheel), mouseCoord(coord) 
        {keyboardSendOrNot = false; mouseSendOrNot = false; keyboardMouseSendOrNot = true;}

    keyPacket(uint8_t mouseMode, uint8_t mouseButton, uint8_t mouseRollWheel, const Coordinate& coord)
        : mouseMode(mouseMode), mouseButton(mouseButton), mouseRollWheel(mouseRollWheel), mouseCoord(coord) 
        {keyboardSendOrNot = false; mouseSendOrNot = true; keyboardMouseSendOrNot = false; }
    
    QByteArray KeytoQByteArray() const {
        QByteArray byteArray;
        byteArray.append(control);
        byteArray.append(constant);
        for (const auto& byte : keyGeneral) {
            byteArray.append(byte);
        }
        return byteArray;
    }

    QByteArray MousetoQByteArray() const {
        QByteArray byteArray;
        // byteArray.append(mouseMode);
        byteArray.append(mouseButton);

        if (mouseMode == 0x02) { // ABS
            for (const auto& byte : mouseCoord.abs.x) {
                byteArray.append(byte & 0xFF);
            }
            for (const auto& byte : mouseCoord.abs.y) {
                byteArray.append(byte & 0xFF);
            }
        } else if (mouseMode == 0x01) { // REL
            byteArray.append(mouseCoord.rel.x & 0xFF);
            byteArray.append(mouseCoord.rel.y & 0xFF);
        }

        byteArray.append(mouseRollWheel);
        
        return byteArray;
    }
};

class KeyboardMouse : public QObject
{
    Q_OBJECT

public:
    explicit KeyboardMouse(QObject *parent = nullptr);

    void addKeyPacket(const keyPacket& packet);
    void dataSend();
    void keyboardSend();
    void mouseSend();
    void keyboardMouseSend();
    void updateNumCapsScrollLockState();
    bool getNumLockState_();
    bool getCapsLockState_();
    bool getScrollLockState_();
    void setMouseSpeed(int speed);
    int getMouseSpeed();


private:
    std::queue<keyPacket> keyData;
    int mouseSpeed;
    int clickInterval = 50;
    int keyInterval = 40;
    uint8_t calculateChecksum(const QByteArray &data);
};

const QMap<QString, uint8_t> controldata = {
    {"^", 0x01}, // Ctrl
    {"+", 0x02}, // Shift
    {"!", 0x04}, // Alt
    {"#", 0x08}  // Win
};



const QMap<QString, uint8_t> keydata = {
    {"a", 0x04}, // a
    {"A", 0x04}, // A
    {"b", 0x05}, // b
    {"B", 0x05}, // B
    {"c", 0x06}, // c
    {"C", 0x06}, // C
    {"d", 0x07}, // d
    {"D", 0x07}, // D
    {"e", 0x08}, // e
    {"E", 0x08}, // E
    {"f", 0x09}, // f
    {"F", 0x09}, // F
    {"g", 0x0A}, // g
    {"G", 0x0A}, // G
    {"h", 0x0B}, // h
    {"H", 0x0B}, // H
    {"i", 0x0C}, // i
    {"I", 0x0C}, // I
    {"j", 0x0D}, // j
    {"J", 0x0D}, // J
    {"k", 0x0E}, // k
    {"K", 0x0E}, // K
    {"l", 0x0F}, // l
    {"L", 0x0F}, // L
    {"m", 0x10}, // m
    {"M", 0x10}, // M
    {"n", 0x11}, // n
    {"N", 0x11}, // N
    {"o", 0x12}, // o
    {"O", 0x12}, // O
    {"p", 0x13}, // p
    {"P", 0x13}, // P
    {"q", 0x14}, // q
    {"Q", 0x14}, // Q
    {"r", 0x15}, // r
    {"R", 0x15}, // R
    {"s", 0x16}, // s
    {"S", 0x16}, // S
    {"t", 0x17}, // t
    {"T", 0x17}, // T
    {"u", 0x18}, // u
    {"U", 0x18}, // U
    {"v", 0x19}, // v
    {"V", 0x19}, // V
    {"w", 0x1A}, // w
    {"W", 0x1A}, // W
    {"x", 0x1B}, // x
    {"X", 0x1B}, // X
    {"y", 0x1C}, // y
    {"Y", 0x1C}, // Y
    {"z", 0x1D}, // z
    {"Z", 0x1D}, // Z
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
    {" ", 0x2C},    //space
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
    {"^", 0xE0}, // Ctrl
    {"+", 0xE5}, // Shift
    {"!", 0xE2}, // Alt
    {"#", 0xE3}  // Win
};


#endif // KEYBOARDMOUSE_H
