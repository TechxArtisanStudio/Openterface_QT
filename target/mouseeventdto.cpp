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

#include "target/mouseeventdto.h"

MouseEventDTO::MouseEventDTO(int x, int y, bool isAbsoluteMode, int mouseButton, int wheelDelta)
    : absX(x), absY(y), _isAbsoluteMode(isAbsoluteMode) {
    if (!isAbsoluteMode) {
        // Calculate delta values if not in absolute mode
        deltaX = x; // Assuming x is deltaX in relative mode
        deltaY = y; // Assuming y is deltaY in relative mode
    }
    this->mouseButton = mouseButton;
    this->wheelDelta = wheelDelta;
}

MouseEventDTO::MouseEventDTO(int x, int y, bool isAbsoluteMode, int mouseButton)
    : absX(x), absY(y), _isAbsoluteMode(isAbsoluteMode) {
    MouseEventDTO(x, y, isAbsoluteMode, mouseButton, 0);
    if (!isAbsoluteMode) {
        // Calculate delta values if not in absolute mode
        deltaX = x; // Assuming x is deltaX in relative mode
        deltaY = y; // Assuming y is deltaY in relative mode
    }
}

MouseEventDTO::MouseEventDTO(int x, int y, bool isAbsoluteMode)
    : absX(x), absY(y), _isAbsoluteMode(isAbsoluteMode) {
    MouseEventDTO(x, y, isAbsoluteMode, 0, 0);
    if (!isAbsoluteMode) {
        // Calculate delta values if not in absolute mode
        deltaX = x; // Assuming x is deltaX in relative mode
        deltaY = y; // Assuming y is deltaY in relative mode
    }
}

int MouseEventDTO::getX() const {
    return _isAbsoluteMode ? absX : deltaX;
}

int MouseEventDTO::getY() const {
    return _isAbsoluteMode ? absY : deltaY;
}

bool MouseEventDTO::isAbsoluteMode() {
    return _isAbsoluteMode;
}
