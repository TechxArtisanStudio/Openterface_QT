#include "firmwareutils.h"
#include <QDebug>

namespace edid {

quint16 FirmwareUtils::calculateFirmwareChecksumWithDiff(const QByteArray &originalFirmware, const QByteArray &originalEDID, const QByteArray &modifiedEDID)
{
    if (originalFirmware.size() < 2) {
        qWarning() << "Firmware too small for checksum calculation";
        return 0;
    }

    if (originalEDID.size() != modifiedEDID.size() || originalEDID.size() != 128) {
        qWarning() << "EDID blocks must be 128 bytes and same size";
        return 0;
    }

    quint8 originalLowByte = static_cast<quint8>(originalFirmware[originalFirmware.size() - 2]);
    quint8 originalHighByte = static_cast<quint8>(originalFirmware[originalFirmware.size() - 1]);
    quint16 originalChecksumBE = (originalLowByte << 8) | originalHighByte;

    qint32 edidDifference = 0;
    for (int i = 0; i < 128; ++i) {
        edidDifference += static_cast<quint8>(modifiedEDID[i]) - static_cast<quint8>(originalEDID[i]);
    }

    qint32 newChecksumInt = static_cast<qint32>(originalChecksumBE) + edidDifference;
    quint16 newChecksum = static_cast<quint16>(newChecksumInt & 0xFFFF);

    qDebug() << "Original checksum (big-endian): 0x" << QString::number(originalChecksumBE, 16).toUpper().rightJustified(4, '0');
    qDebug() << "EDID difference:" << edidDifference;
    qDebug() << "New checksum (16-bit): 0x" << QString::number(newChecksum, 16).toUpper().rightJustified(4, '0');

    return newChecksum;
}

quint16 FirmwareUtils::calculateFirmwareChecksumWithDiff(const QByteArray &originalFirmware, const QByteArray &modifiedFirmware)
{
    if (originalFirmware.size() < 2 || modifiedFirmware.size() < 2) {
        qWarning() << "Firmware too small for checksum calculation";
        return 0;
    }

    if (originalFirmware.size() != modifiedFirmware.size()) {
        qWarning() << "Original and modified firmware must be same size";
        return 0;
    }

    quint8 originalLowByte = static_cast<quint8>(originalFirmware[originalFirmware.size() - 2]);
    quint8 originalHighByte = static_cast<quint8>(originalFirmware[originalFirmware.size() - 1]);
    quint16 originalChecksum = (originalLowByte << 8) | originalHighByte;

    qint32 firmwareDifference = 0;
    int checksumExcludeSize = originalFirmware.size() - 2;
    for (int i = 0; i < checksumExcludeSize; ++i) {
        firmwareDifference += static_cast<quint8>(modifiedFirmware[i]) - static_cast<quint8>(originalFirmware[i]);
    }

    qint32 newChecksumInt = static_cast<qint32>(originalChecksum) + firmwareDifference;
    quint16 newChecksum = static_cast<quint16>(newChecksumInt & 0xFFFF);

    qDebug() << "Original checksum (big-endian): 0x" << QString::number(originalChecksum, 16).toUpper().rightJustified(4, '0');
    qDebug() << "Firmware difference:" << firmwareDifference;
    qDebug() << "New checksum (16-bit): 0x" << QString::number(newChecksum, 16).toUpper().rightJustified(4, '0');

    return newChecksum;
}

} // namespace edid