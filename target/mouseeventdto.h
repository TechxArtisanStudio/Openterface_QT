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

#ifndef MOUSEEVENTDTO_H
#define MOUSEEVENTDTO_H

class MouseEventDTO {
public:
    MouseEventDTO(int x, int y, bool isAbsoluteMode, int mouseButton, int wheelDelta);
    MouseEventDTO(int x, int y, bool isAbsoluteMode, int mouseButton);
    MouseEventDTO(int x, int y, bool isAbsoluteMode);

    int getX() const;
    int getY() const;
    bool isAbsoluteMode();
    int getMouseButton() const { return mouseButton; }
    void setMouseButton(int button) { mouseButton = button; }
    int getWheelDelta() const { return wheelDelta; }
    void setWheelDelta(int delta) { wheelDelta = delta; }

private:
    int absX;
    int absY;
    int deltaX;
    int deltaY;
    bool _isAbsoluteMode;

    int mouseButton;
    int wheelDelta;
};

#endif // MOUSEEVENTDTO_H
