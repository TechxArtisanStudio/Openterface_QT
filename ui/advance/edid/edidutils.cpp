#include "edidutils.h"
#include <QDebug>

namespace edid {

int EDIDUtils::findEDIDBlock0(const QByteArray &firmwareData)
{
    const QByteArray edidHeader = QByteArray::fromHex("00FFFFFFFFFFFF00");
    for (int i = 0; i <= firmwareData.size() - edidHeader.size(); ++i) {
        if (firmwareData.mid(i, edidHeader.size()) == edidHeader) {
            qDebug() << "EDID Block 0 found at offset:" << i;
            return i;
        }
    }
    qDebug() << "EDID Block 0 not found in firmware";
    return -1;
}

void EDIDUtils::parseEDIDDescriptors(const QByteArray &edidBlock, QString &displayName, QString &serialNumber)
{
    displayName.clear();
    serialNumber.clear();

    if (edidBlock.size() != 128) {
        qWarning() << "Invalid EDID block size:" << edidBlock.size();
        return;
    }

    for (int descriptorOffset = 54; descriptorOffset <= 54 + 3 * 18; descriptorOffset += 18) {
        if (descriptorOffset + 18 > edidBlock.size()) break;

        if (edidBlock[descriptorOffset] == 0x00 &&
            edidBlock[descriptorOffset + 1] == 0x00 &&
            edidBlock[descriptorOffset + 2] == 0x00) {

            quint8 descriptorType = static_cast<quint8>(edidBlock[descriptorOffset + 3]);

            if (descriptorType == 0xFC) {
                QByteArray nameBytes = edidBlock.mid(descriptorOffset + 5, 13);
                for (int i = 0; i < nameBytes.size(); ++i) {
                    char c = nameBytes[i];
                    if (c == 0x0A) break;
                    if (c >= 32 && c <= 126) {
                        displayName += c;
                    }
                }
                displayName = displayName.trimmed();

            } else if (descriptorType == 0xFF) {
                QByteArray serialBytes = edidBlock.mid(descriptorOffset + 5, 13);
                for (int i = 0; i < serialBytes.size(); ++i) {
                    char c = serialBytes[i];
                    if (c == 0x0A) break;
                    if (c >= 32 && c <= 126) {
                        serialNumber += c;
                    }
                }
                serialNumber = serialNumber.trimmed();
            }
        }
    }
}

quint8 EDIDUtils::calculateEDIDChecksum(const QByteArray &edidBlock)
{
    if (edidBlock.size() != 128) {
        qWarning() << "EDID block size is not 128 bytes:" << edidBlock.size();
        return 0;
    }

    quint16 sum = 0;
    for (int i = 0; i < 127; ++i) {
        sum += static_cast<quint8>(edidBlock[i]);
    }

    quint8 checksum = (256 - (sum & 0xFF)) & 0xFF;
    qDebug() << "Calculated EDID checksum:" << QString("0x%1").arg(checksum, 2, 16, QChar('0'));
    return checksum;
}

void EDIDUtils::updateEDIDDisplayName(QByteArray &edidBlock, const QString &newName)
{
    int targetDescriptorOffset = -1;

    for (int descriptorOffset = 54; descriptorOffset <= 54 + 3 * 18; descriptorOffset += 18) {
        if (descriptorOffset + 18 > edidBlock.size()) break;
        if (edidBlock[descriptorOffset] == 0x00 &&
            edidBlock[descriptorOffset + 1] == 0x00 &&
            edidBlock[descriptorOffset + 2] == 0x00 &&
            edidBlock[descriptorOffset + 3] == static_cast<char>(0xFC)) {
            targetDescriptorOffset = descriptorOffset;
            break;
        }
    }

    if (targetDescriptorOffset == -1) {
        targetDescriptorOffset = 108;
        qDebug() << "No existing display name descriptor found, using descriptor at offset 108";
    }

    if (targetDescriptorOffset + 18 > edidBlock.size()) {
        qWarning() << "Target descriptor offset exceeds EDID block size";
        return;
    }

    QByteArray nameBytes = newName.toUtf8();
    if (nameBytes.size() > 13) {
        nameBytes = nameBytes.left(13);
    }

    nameBytes.append(0x0A);

    while (nameBytes.size() < 13) {
        nameBytes.append(' ');
    }

    qDebug() << "Updating display name descriptor at offset:" << targetDescriptorOffset;

    // Show descriptor BEFORE update
    qDebug() << "=== DESCRIPTOR BEFORE UPDATE (offset" << targetDescriptorOffset << ") ===";
    QByteArray beforeDescriptor = edidBlock.mid(targetDescriptorOffset, 18);
    QString beforeHex;
    for (int i = 0; i < beforeDescriptor.size(); ++i) {
        beforeHex += QString("%1 ").arg(static_cast<quint8>(beforeDescriptor[i]), 2, 16, QChar('0')).toUpper();
    }
    qDebug() << "Before:" << beforeHex;

    // Set descriptor header for display name
    edidBlock[targetDescriptorOffset] = 0x00;
    edidBlock[targetDescriptorOffset + 1] = 0x00;
    edidBlock[targetDescriptorOffset + 2] = 0x00;
    edidBlock[targetDescriptorOffset + 3] = 0xFC; // Display name tag
    edidBlock[targetDescriptorOffset + 4] = 0x00;

    // Copy display name
    for (int i = 0; i < 13; ++i) {
        if (i < nameBytes.size()) {
            edidBlock[targetDescriptorOffset + 5 + i] = nameBytes[i];
        } else {
            edidBlock[targetDescriptorOffset + 5 + i] = ' ';
        }
    }

    // Show descriptor AFTER update
    qDebug() << "=== DESCRIPTOR AFTER UPDATE (offset" << targetDescriptorOffset << ") ===";
    QByteArray afterDescriptor = edidBlock.mid(targetDescriptorOffset, 18);
    QString afterHex;
    for (int i = 0; i < afterDescriptor.size(); ++i) {
        afterHex += QString("%1 ").arg(static_cast<quint8>(afterDescriptor[i]), 2, 16, QChar('0')).toUpper();
    }
    qDebug() << "After:" << afterHex;

    qDebug() << "Display name updated to:" << newName;
}

void EDIDUtils::updateEDIDSerialNumber(QByteArray &edidBlock, const QString &newSerial)
{
    int targetDescriptorOffset = -1;

    for (int descriptorOffset = 54; descriptorOffset <= 54 + 3 * 18; descriptorOffset += 18) {
        if (descriptorOffset + 18 > edidBlock.size()) break;

        if (edidBlock[descriptorOffset] == 0x00 &&
            edidBlock[descriptorOffset + 1] == 0x00 &&
            edidBlock[descriptorOffset + 2] == 0x00 &&
            edidBlock[descriptorOffset + 3] == static_cast<char>(0xFF)) {
            targetDescriptorOffset = descriptorOffset;
            break;
        }
    }

    if (targetDescriptorOffset == -1) {
        for (int descriptorOffset = 54; descriptorOffset <= 54 + 3 * 18; descriptorOffset += 18) {
            if (descriptorOffset + 18 > edidBlock.size()) break;

            bool isUnused = true;
            for (int i = 0; i < 18; ++i) {
                if (edidBlock[descriptorOffset + i] != 0x00) {
                    isUnused = false;
                    break;
                }
            }

            if (!isUnused &&
                edidBlock[descriptorOffset] == 0x00 &&
                edidBlock[descriptorOffset + 1] == 0x00 &&
                edidBlock[descriptorOffset + 2] == 0x00 &&
                edidBlock[descriptorOffset + 3] == static_cast<char>(0xFC)) {
                continue;
            }

            if (isUnused) {
                targetDescriptorOffset = descriptorOffset;
                break;
            }
        }
    }

    if (targetDescriptorOffset == -1) {
        targetDescriptorOffset = 72;
        qDebug() << "No existing serial number descriptor found, using descriptor at offset 72";
    }

    if (targetDescriptorOffset + 18 > edidBlock.size()) {
        qWarning() << "Target descriptor offset exceeds EDID block size";
        return;
    }

    QByteArray serialBytes = newSerial.toUtf8();
    if (serialBytes.size() > 13) {
        serialBytes = serialBytes.left(13);
    }

    serialBytes.append(0x0A);

    while (serialBytes.size() < 13) {
        serialBytes.append(' ');
    }

    qDebug() << "Updating serial number descriptor at offset:" << targetDescriptorOffset;

    qDebug() << "=== SERIAL DESCRIPTOR BEFORE UPDATE (offset" << targetDescriptorOffset << ") ===";
    QByteArray beforeDescriptor = edidBlock.mid(targetDescriptorOffset, 18);
    QString beforeHex;
    for (int i = 0; i < beforeDescriptor.size(); ++i) {
        beforeHex += QString("%1 ").arg(static_cast<quint8>(beforeDescriptor[i]), 2, 16, QChar('0')).toUpper();
    }
    qDebug() << "Before:" << beforeHex;

    edidBlock[targetDescriptorOffset] = 0x00;
    edidBlock[targetDescriptorOffset + 1] = 0x00;
    edidBlock[targetDescriptorOffset + 2] = 0x00;
    edidBlock[targetDescriptorOffset + 3] = 0xFF;
    edidBlock[targetDescriptorOffset + 4] = 0x00;

    for (int i = 0; i < 13; ++i) {
        if (i < serialBytes.size()) {
            edidBlock[targetDescriptorOffset + 5 + i] = serialBytes[i];
        } else {
            edidBlock[targetDescriptorOffset + 5 + i] = ' ';
        }
    }

    qDebug() << "=== SERIAL DESCRIPTOR AFTER UPDATE (offset" << targetDescriptorOffset << ") ===";
    QByteArray afterDescriptor = edidBlock.mid(targetDescriptorOffset, 18);
    QString afterHex;
    for (int i = 0; i < afterDescriptor.size(); ++i) {
        afterHex += QString("%1 ").arg(static_cast<quint8>(afterDescriptor[i]), 2, 16, QChar('0')).toUpper();
    }
    qDebug() << "After:" << afterHex;

    qDebug() << "Serial number updated to:" << newSerial;
}

void EDIDUtils::showEDIDDescriptors(const QByteArray &edidBlock)
{
    qDebug() << "EDID Block size:" << edidBlock.size();

    for (int descriptorOffset = 54; descriptorOffset <= 54 + 3 * 18; descriptorOffset += 18) {
        if (descriptorOffset + 18 > edidBlock.size()) break;

        QByteArray descriptor = edidBlock.mid(descriptorOffset, 18);
        QString hexString;
        for (int i = 0; i < descriptor.size(); ++i) {
            hexString += QString("%1 ").arg(static_cast<quint8>(descriptor[i]), 2, 16, QChar('0')).toUpper();
        }

        qDebug() << QString("Descriptor at offset %1:").arg(descriptorOffset);
        qDebug() << "  Hex:" << hexString;

        quint8 descriptorType = static_cast<quint8>(descriptor[3]);
        if (descriptor[0] == 0x00 && descriptor[1] == 0x00 && descriptor[2] == 0x00) {
            switch (descriptorType) {
                case 0xFF:
                    qDebug() << "  Type: Display Serial Number";
                    break;
                case 0xFE:
                    qDebug() << "  Type: Unspecified Text";
                    break;
                case 0xFD:
                    qDebug() << "  Type: Display Range Limits";
                    break;
                case 0xFC:
                    qDebug() << "  Type: Display Product Name";
                    break;
                case 0xFB:
                    qDebug() << "  Type: Color Point Data";
                    break;
                case 0xFA:
                    qDebug() << "  Type: Standard Timing Identifications";
                    break;
                default:
                    if (descriptorType == 0x00) {
                        qDebug() << "  Type: Empty/Unused Descriptor";
                    } else {
                        qDebug() << "  Type: Unknown (" << QString("0x%1").arg(descriptorType, 2, 16, QChar('0')).toUpper() << ")";
                    }
                    break;
            }
        } else {
            qDebug() << "  Type: Detailed Timing Descriptor";
        }
    }
}

void EDIDUtils::showFirmwareHexDump(const QByteArray &firmwareData, int startOffset, int length)
{
    if (length == -1) {
        length = firmwareData.size() - startOffset;
    }

    length = qMin(length, firmwareData.size() - startOffset);

    for (int i = 0; i < length; i += 16) {
        QString line = QString("0x%1: ").arg(startOffset + i, 8, 16, QChar('0')).toUpper();

        for (int j = 0; j < 16 && (i + j) < length; ++j) {
            quint8 byte = static_cast<quint8>(firmwareData[startOffset + i + j]);
            line += QString("%1 ").arg(byte, 2, 16, QChar('0')).toUpper();
        }

        for (int j = (i + 16 > length) ? length - i : 16; j < 16; ++j) {
            line += "   ";
        }

        line += " | ";

        for (int j = 0; j < 16 && (i + j) < length; ++j) {
            char c = firmwareData[startOffset + i + j];
            line += (c >= 32 && c <= 126) ? QString(c) : QString('.');
        }

        qDebug() << line;
    }
}

} // namespace edid
