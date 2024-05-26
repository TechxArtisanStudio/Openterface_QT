/*
* ========================================================================== *
*                                                                            *
*    This file is part of the Openterface Mini KVM App QT version            *
*                                                                            *
*    Copyright (C) 2024   <info@openterface.com>                             *
*                                                                            *
*    This program is free software: you can redistribute it and/or modify    *
*    it under the terms of the GNU General Public License as published by    *
*    the Free Software Foundation, either version 3 of the License, or       *
*    (at your option) any later version.                                     *
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

#ifndef KEYMAPPING_LINUX_H
#define KEYMAPPING_LINUX_H

const QMap<uint16_t, uint8_t> KeyboardManager::keyMap = {
    {38, 0x04}, // a
    {56, 0x05}, // b
    {54, 0x06}, // c
    {40, 0x07}, // d
    {26, 0x08}, // e
    {41, 0x09}, // f
    {42, 0x0A}, // g
    {43, 0x0B}, // h
    {31, 0x0C}, // i
    {44, 0x0D}, // j
    {45, 0x0E}, // k
    {46, 0x0F}, // l
    {58, 0x10}, // m
    {57, 0x11}, // n
    {32, 0x12}, // o
    {33, 0x13}, // p
    {24, 0x14}, // q
    {27, 0x15}, // r
    {39, 0x16}, // s
    {28, 0x17}, // t
    {30, 0x18}, // u
    {55, 0x19}, // v
    {25, 0x1A}, // w
    {53, 0x1B}, // x
    {29, 0x1C}, // y
    {52, 0x1D}, // z
    {10, 0x1E}, // 1
    {11, 0x1F}, // 2
    {12, 0x20}, // 3
    {13, 0x21}, // 4
    {14, 0x22}, // 5
    {15, 0x23}, // 6
    {16, 0x24}, // 7
    {17, 0x25}, // 8
    {18, 0x26}, // 9
    {19, 0x27}, // 0
    {36, 0x28}, //return
    {9, 0x29}, // esc
    {22, 0x2A}, // backspace
    {23, 0x2B}, // tab
    {65, 0x2C}, // space

    {20, 0x28}, // -
    {21, 0x29}, // =
    {34, 0x2A}, // [
    {35, 0x2B}, // ]
    {51, 0x2C}, // \
    {47, 0x2F}, // ;
    {48, 0x30}, // '
    {49, 0x31}, // `
    {59, 0x33}, // ,
    {60, 0x34}, // .
    {61, 0x35}, // /

    {67, 0x04}, // F1
    {68, 0x05}, // F2
    {69, 0x06}, // F3
    {70, 0x07}, // F4
    {71, 0x08}, // F5
    {72, 0x09}, // F6
    {73, 0x0A}, // F7
    {74, 0x0B}, // F8
    {75, 0x0C}, // F9
    {76, 0x0D}, // F10
    {95, 0x0E}, // F11
    {96, 0x0F}, // F12
    {111, 0x10}, // PrintScreen
    {78, 0x11}, // ScrollLock
    {110, 0x12}, // Pause
    {97, 0x4F}, // Home
    {103, 0x50}, // Up
    {104, 0x51}, // PageUp
    {105, 0x52}, // Left
    {106, 0x53}, // Right
    {107, 0x54}, // End
    {80108, 0x55}, // Down
    {109, 0x56}, // PageDown
    {110, 0x57}, // Insert
};

const QList<uint16_t> KeyboardManager::SHIFT_KEYS = {
    50, // Shift Left
    62 // Shift Right
};

const QList<uint16_t> KeyboardManager::CTRL_KEYS = {
    34, // Ctrl Left
    109 // Ctrl Right
};

const QList<uint16_t> KeyboardManager::ALT_KEYS = {
    64, // Alt Left
    113 // Alt Right
};
#endif // KEYMAPPING_LINUX_H
