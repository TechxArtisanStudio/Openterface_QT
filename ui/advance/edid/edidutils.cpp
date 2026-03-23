#include "edidutils.h"
#include "edidresolutionparser.h"
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

void EDIDUtils::logSupportedResolutions(const QByteArray &edidBlock)
{
    if (edidBlock.size() != 128) {
        qWarning() << "Invalid EDID block size:" << edidBlock.size();
        return;
    }

    qDebug() << "=== SUPPORTED RESOLUTIONS FROM EDID ===";
    quint8 extensionCount = static_cast<quint8>(edidBlock[126]);
    qDebug() << "EDID Extension blocks count:" << extensionCount;

    if (extensionCount > 0) {
        qDebug() << "There are" << extensionCount << "extension blocks available.";
    } else {
        qDebug() << "No extension blocks in EDID block 0";
    }

    qDebug() << "Standard Timings (bytes 35-42): [Skipping detailed analysis - focusing on extension blocks]";
    qDebug() << "Detailed Timing Descriptors (Block 0): [May contain some legacy timings]";

    for (int descriptorOffset = 54; descriptorOffset <= 54 + 3 * 18; descriptorOffset += 18) {
        if (descriptorOffset + 18 > edidBlock.size()) break;

        quint16 pixelClock = static_cast<quint16>(edidBlock[descriptorOffset]) |
                             (static_cast<quint16>(edidBlock[descriptorOffset + 1]) << 8);

        if (pixelClock > 0) {
            quint16 hActive = static_cast<quint16>(edidBlock[descriptorOffset + 2]) |
                              ((static_cast<quint16>(edidBlock[descriptorOffset + 4]) & 0xF0) << 4);

            quint16 vActive = static_cast<quint16>(edidBlock[descriptorOffset + 5]) |
                              ((static_cast<quint16>(edidBlock[descriptorOffset + 7]) & 0xF0) << 4);

            double pixelClockMHz = pixelClock / 100.0;
            qDebug() << "  " << hActive << "x" << vActive << "@ pixel clock" << pixelClockMHz << "MHz";
        }
    }

    if (extensionCount > 0) {
        qDebug() << "";
        qDebug() << "=> FOCUS: Extension blocks contain the actual supported resolutions.";
        qDebug() << "=> Resolution table will show VIC codes and detailed timings from extension blocks.";
        qDebug() << "=> Standard timings above are often legacy and may not reflect true capabilities.";
    } else {
        qDebug() << "";
        qDebug() << "=> WARNING: No extension blocks found. This may be a basic/legacy display.";
        qDebug() << "=> Modern displays typically use extension blocks for resolution information.";
    }

    qDebug() << "=== END SUPPORTED RESOLUTIONS ===";
}

void EDIDUtils::parseEDIDExtensionBlocks(const QByteArray &firmwareData, int baseBlockOffset)
{
    if (baseBlockOffset < 0 || baseBlockOffset + 128 > firmwareData.size()) {
        qWarning() << "Invalid base block offset for extension parsing:" << baseBlockOffset;
        return;
    }

    quint8 extensionCount = static_cast<quint8>(firmwareData[baseBlockOffset + 126]);
    if (extensionCount == 0) {
        qDebug() << "No EDID extension blocks found";
        return;
    }

    qDebug() << "=== PARSING EDID EXTENSION BLOCKS ===";
    qDebug() << "Extension count:" << extensionCount;

    for (int blockIndex = 1; blockIndex <= extensionCount; ++blockIndex) {
        int blockOffset = baseBlockOffset + (blockIndex * 128);
        if (blockOffset + 128 > firmwareData.size()) {
            qWarning() << "Extension Block" << blockIndex << "not found in firmware (offset" << blockOffset << ")";
            continue;
        }

        QByteArray extensionBlock = firmwareData.mid(blockOffset, 128);
        quint8 extensionTag = static_cast<quint8>(extensionBlock[0]);

        qDebug() << "";
        qDebug() << "=== EXTENSION BLOCK" << blockIndex << "===";
        qDebug() << "Block offset:" << blockOffset;
        qDebug() << "Extension tag: 0x" << QString::number(extensionTag, 16).toUpper().rightJustified(2, '0');

        switch (extensionTag) {
            case 0x02:
                qDebug() << "Type: CEA-861 Extension Block";
                parseCEA861ExtensionBlock(extensionBlock, blockIndex);
                break;
            case 0x10:
                qDebug() << "Type: Video Timing Extension Block";
                parseVideoTimingExtensionBlock(extensionBlock, blockIndex);
                break;
            case 0x20:
                qDebug() << "Type: EDID 2.0 Extension Block";
                break;
            case 0x30:
                qDebug() << "Type: Color Information Extension Block";
                break;
            case 0x40:
                qDebug() << "Type: DVI Feature Extension Block";
                break;
            case 0x50:
                qDebug() << "Type: Touch Screen Extension Block";
                break;
            case 0x60:
                qDebug() << "Type: Block Map Extension Block";
                break;
            case 0x70:
                qDebug() << "Type: Display Device Data Extension Block";
                break;
            case 0xF0:
                qDebug() << "Type: Block Map Extension Block (alternate)";
                break;
            default:
                qDebug() << "Type: Unknown/Proprietary Extension Block";
                break;
        }

        qDebug() << "First 32 bytes:";
        QString hexDump;
        for (int i = 0; i < qMin(32, extensionBlock.size()); ++i) {
            hexDump += QString("%1 ").arg(static_cast<quint8>(extensionBlock[i]), 2, 16, QChar('0')).toUpper();
        }
        qDebug() << hexDump;
    }

    qDebug() << "=== END EXTENSION BLOCKS ===";
}

void EDIDUtils::parseCEA861ExtensionBlock(const QByteArray &block, int blockNumber)
{
    if (block.size() != 128) {
        qWarning() << "Invalid CEA-861 extension block size:" << block.size();
        return;
    }

    quint8 revision = static_cast<quint8>(block[1]);
    quint8 dtdOffset = static_cast<quint8>(block[2]);
    quint8 flags = static_cast<quint8>(block[3]);

    qDebug() << "CEA-861 Revision:" << revision;
    qDebug() << "DTD offset:" << dtdOffset;
    qDebug() << "Flags: 0x" << QString::number(flags, 16).toUpper().rightJustified(2, '0');

    bool hasUnderscan = (flags & 0x80) != 0;
    bool hasBasicAudio = (flags & 0x40) != 0;
    bool hasYCC444 = (flags & 0x20) != 0;
    bool hasYCC422 = (flags & 0x10) != 0;

    qDebug() << "Capabilities:";
    qDebug() << "  Underscan support:" << (hasUnderscan ? "Yes" : "No");
    qDebug() << "  Basic audio support:" << (hasBasicAudio ? "Yes" : "No");
    qDebug() << "  YCC 4:4:4 support:" << (hasYCC444 ? "Yes" : "No");
    qDebug() << "  YCC 4:2:2 support:" << (hasYCC422 ? "Yes" : "No");

    if (dtdOffset > 4 && dtdOffset < 128) {
        int descriptorStart = 4;
        while (descriptorStart < dtdOffset) {
            quint8 tag = static_cast<quint8>(block[descriptorStart]);
            quint8 length = tag & 0x1F;
            quint8 type = (tag >> 5) & 0x07;

            if (type == 0) break;
            if (type == 2) {
                QByteArray videoData = block.mid(descriptorStart + 1, length);
                parseVideoDataBlock(videoData);
            }

            descriptorStart += 1 + length;
        }
    }
}

void EDIDUtils::parseVideoTimingExtensionBlock(const QByteArray &block, int blockNumber)
{
    qDebug() << "Video Timing Extension Block parsing not fully implemented";
    qDebug() << "This block contains additional timing information";
}

void EDIDUtils::parseVideoDataBlock(const QByteArray &vdbData)
{
    qDebug() << "    Video Data Block contains" << vdbData.size() << "Short Video Descriptors:";

    for (int i = 0; i < vdbData.size(); ++i) {
        quint8 svd = static_cast<quint8>(vdbData[i]);
        quint8 vic = svd & 0x7F;
        bool isNative = (svd & 0x80) != 0;

        QString resolutionInfo = edid::EDIDResolutionParser::getVICResolution(vic);
        qDebug() << QString("      VIC %1: %2%3")
                    .arg(vic)
                    .arg(resolutionInfo)
                    .arg(isNative ? " (Native)" : "");
    }
}

} // namespace edid
