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

#ifndef SENDKEYMAPS_H
#define SENDKEYMAPS_H

#include <cstdint>
#include <QChar>
#include <QMap>
#include <QPair>
#include <QSet>

// Backtick escape map for AHK-style Send command.
// In AHK, a backtick (`) is the escape character. This allows typing literal
// modifier symbols (^, !, +, #) or common control sequences.
//
// Format: escapeChar → { HID usage ID, needsShift }
const QMap<QChar, QPair<uint8_t, bool>> backtickEscapeMap = {
    {'!', {0x1E, true}},   // `! → literal !  (Shift+1)
    {'#', {0x20, true}},   // `# → literal #  (Shift+3)
    {'+', {0x2E, true}},   // `+ → literal +  (Shift+=)
    {'^', {0x23, true}},   // `^ → literal ^  (Shift+6)
    {'`', {0x35, false}},  // `` → literal `  (grave accent)
    {'n', {0x28, false}},  // `n → newline    (Enter)
    {'t', {0x2B, false}},  // `t → tab        (Tab)
    {'r', {0x28, false}},  // `r → carriage return (Enter)
};

// Symbol characters that require a Shift modifier when typed literally.
// Does not include ^, !, +, # — those are AHK modifier prefix characters
// handled separately via controldata.
const QSet<QChar> shiftRequiredChars = {
    '@', '$', '%', '&', '*', '(', ')'
};

#endif // SENDKEYMAPS_H
