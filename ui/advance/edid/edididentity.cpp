#include "edididentity.h"
#include "edidutils.h"

namespace edid {

EdidIdentity EdidIdentity::fromImage(const QByteArray &image)
{
    EdidIdentity id;
    id.firmwareSize = image.size();
    id.rawImage = image;

    int offset = EDIDUtils::findEDIDBlock0(image);
    if (offset < 0 || offset + 128 > image.size()) {
        return id;
    }
    const QByteArray block = image.mid(offset, 128);
    id.edidOffset = offset;
    id.valid = true;

    // Manufacturer id: 2 bytes big endian, three 5-bit letters ('A' = 1).
    const quint8 b8 = static_cast<quint8>(block[8]);
    const quint8 b9 = static_cast<quint8>(block[9]);
    const int l1 = (b8 >> 2) & 0x1F;
    const int l2 = ((b8 & 0x03) << 3) | ((b9 >> 5) & 0x07);
    const int l3 = b9 & 0x1F;
    auto letter = [](int v) -> QChar {
        return (v >= 1 && v <= 26) ? QChar('A' + v - 1) : QChar('?');
    };
    id.manufacturerId = QString() + letter(l1) + letter(l2) + letter(l3);

    id.productCode = static_cast<quint16>(static_cast<quint8>(block[10]))
                   | (static_cast<quint16>(static_cast<quint8>(block[11])) << 8);
    id.serialU32 = static_cast<quint32>(static_cast<quint8>(block[12]))
                 | (static_cast<quint32>(static_cast<quint8>(block[13])) << 8)
                 | (static_cast<quint32>(static_cast<quint8>(block[14])) << 16)
                 | (static_cast<quint32>(static_cast<quint8>(block[15])) << 24);

    EDIDUtils::parseEDIDDescriptors(block, id.displayName, id.serialString);
    return id;
}

QString EdidIdentity::summary() const
{
    if (!valid) {
        return QStringLiteral("EDID block 0 not found in %1-byte image").arg(firmwareSize);
    }
    return QStringLiteral("display_name=\"%1\" serial_string=\"%2\" manufacturer=%3 product=0x%4 serial_u32=%5 (edid @%6 in %7-byte image)")
        .arg(displayName, serialString, manufacturerId)
        .arg(productCode, 4, 16, QChar('0'))
        .arg(serialU32)
        .arg(edidOffset)
        .arg(firmwareSize);
}

QString decorateLabel(const QString &label, const QString &name)
{
    if (name.isEmpty()) {
        return label;
    }
    return QStringLiteral("%1 - %2").arg(name, label);
}

QString windowTitle(const QString &version, const QString &name)
{
    QString title = QStringLiteral("Openterface - %1").arg(version);
    if (!name.isEmpty()) {
        title += QStringLiteral(" - %1").arg(name);
    }
    return title;
}

} // namespace edid
