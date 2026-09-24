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

#ifndef EDIDIDENTITY_H
#define EDIDIDENTITY_H

#include <QByteArray>
#include <QMetaType>
#include <QString>

namespace edid {

// Identity-relevant fields of the EDID block 0 stored in the capture chip's
// EEPROM image: what a target computer sees as "the monitor", and therefore
// a per-unit identity readable from the host over HID.
struct EdidIdentity
{
    bool valid = false;            // EDID block 0 found and 128 bytes long
    QString manufacturerId;        // 3-letter PNP id (bytes 8-9)
    quint16 productCode = 0;       // bytes 10-11, little endian
    quint32 serialU32 = 0;         // bytes 12-15, little endian
    QString displayName;           // descriptor 0xFC (13 ASCII chars max)
    QString serialString;          // descriptor 0xFF (13 ASCII chars max)
    int edidOffset = -1;           // offset of block 0 inside the image
    int firmwareSize = 0;          // size of the image the block came from
    QByteArray rawImage;           // the full EEPROM image (for backup/verify)

    // Parse an EEPROM image (as returned by VideoHid::readEeprom). Pure.
    static EdidIdentity fromImage(const QByteArray &image);

    // One-line human readable summary, e.g. for the MCP tool result.
    QString summary() const;
};

} // namespace edid

Q_DECLARE_METATYPE(edid::EdidIdentity)

#endif // EDIDIDENTITY_H
