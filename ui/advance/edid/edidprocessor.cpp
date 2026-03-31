#include "edidprocessor.h"
#include "edidutils.h"
#include "edidresolutionparser.h"
#include "firmwareutils.h"
#include <QDebug>

EdidProcessor::EdidProcessor(const ResolutionModel &resolutionModel)
    : m_resolutionModel(resolutionModel)
{
}

bool EdidProcessor::parseEdidBlock(const QByteArray &firmwareData, int &edidOffset, QByteArray &edidBlock) const
{
    edidOffset = edid::EDIDUtils::findEDIDBlock0(firmwareData);
    if (edidOffset == -1 || edidOffset + 128 > firmwareData.size()) {
        return false;
    }
    edidBlock = firmwareData.mid(edidOffset, 128);
    return true;
}

void EdidProcessor::applyEdidUpdates(QByteArray &modifiedFirmware, int edidOffset,
                                     const QString &newName, const QString &newSerial) const
{
    QByteArray edidBlock = modifiedFirmware.mid(edidOffset, 128);

    if (!newName.isEmpty()) {
        edid::EDIDUtils::updateEDIDDisplayName(edidBlock, newName);
    }
    if (!newSerial.isEmpty()) {
        edid::EDIDUtils::updateEDIDSerialNumber(edidBlock, newSerial);
    }

    if (m_resolutionModel.hasChanges()) {
        updateExtensionBlockResolutions(modifiedFirmware, edidOffset);
    }

    quint8 edidChecksum = edid::EDIDUtils::calculateEDIDChecksum(edidBlock);
    edidBlock[127] = edidChecksum;
    modifiedFirmware.replace(edidOffset, 128, edidBlock);
}

void EdidProcessor::updateExtensionBlockResolutions(QByteArray &firmwareData, int edidOffset) const
{
    qDebug() << "=== UPDATING EXTENSION BLOCK RESOLUTIONS ===";

    if (edidOffset + 126 >= firmwareData.size()) {
        qWarning() << "EDID block too small to check extension count";
        return;
    }

    quint8 extensionCount = static_cast<quint8>(firmwareData[edidOffset + 126]);
    if (extensionCount == 0) {
        qDebug() << "No extension blocks found - cannot update resolutions";
        return;
    }

    qDebug() << "Found" << extensionCount << "extension block(s) for resolution updates";

    QSet<quint8> enabledVICs = m_resolutionModel.enabledVICs();
    QSet<quint8> disabledVICs = m_resolutionModel.disabledVICs();

    for (const quint8 vic : enabledVICs) {
        qDebug() << "  Enable VIC" << vic;
    }
    for (const quint8 vic : disabledVICs) {
        qDebug() << "  Disable VIC" << vic;
    }

    bool anyBlockModified = false;

    for (int blockIndex = 1; blockIndex <= extensionCount; ++blockIndex) {
        int blockOffset = edidOffset + (blockIndex * 128);
        if (blockOffset + 128 > firmwareData.size()) {
            qWarning() << "Extension Block" << blockIndex << "exceeds firmware size";
            continue;
        }

        QByteArray extensionBlock = firmwareData.mid(blockOffset, 128);
        quint8 extensionTag = static_cast<quint8>(extensionBlock[0]);

        if (extensionTag == 0x02) { // CEA-861
            qDebug() << "Processing CEA-861 extension block" << blockIndex << "at offset" << blockOffset;
            if (updateCEA861ExtensionBlockResolutions(extensionBlock, enabledVICs, disabledVICs)) {
                quint8 blockChecksum = edid::EDIDUtils::calculateEDIDChecksum(extensionBlock);
                qDebug() << "Updated extension block" << blockIndex << "checksum to 0x" << QString::number(blockChecksum, 16).toUpper().rightJustified(2, '0');
                firmwareData.replace(blockOffset, 128, extensionBlock);
                anyBlockModified = true;
                qDebug() << "Extension block" << blockIndex << "updated successfully";
            }
        } else {
            qDebug() << "Skipping non-CEA-861 extension block" << blockIndex << "(tag 0x" << QString::number(extensionTag, 16).toUpper().rightJustified(2, '0') << ")";
        }
    }

    qDebug() << (anyBlockModified ? "Extension blocks have been updated with resolution changes" : "No extension blocks were modified");
    qDebug() << "=== EXTENSION BLOCK RESOLUTION UPDATE COMPLETE ===";
}

bool EdidProcessor::updateCEA861ExtensionBlockResolutions(QByteArray &block,
                                                           const QSet<quint8> &enabledVICs,
                                                           const QSet<quint8> &disabledVICs) const
{
    if (block.size() != 128) {
        qWarning() << "Invalid CEA-861 extension block size:" << block.size();
        return false;
    }

    quint8 dtdOffset = static_cast<quint8>(block[2]);
    if (dtdOffset <= 4 || dtdOffset > 127) {
        qWarning() << "Invalid DTD offset in CEA-861 block:" << dtdOffset;
        return false;
    }

    int offset = 4;
    bool foundVideoDataBlock = false;
    bool blockModified = false;

    while (offset < dtdOffset && offset < block.size()) {
        quint8 header = static_cast<quint8>(block[offset]);
        quint8 tag = (header >> 5) & 0x07;
        quint8 length = header & 0x1F;

        if (tag == 2) {
            qDebug() << "Found Video Data Block at offset" << offset << "with length" << length;
            foundVideoDataBlock = true;

            for (int i = 1; i <= length && offset + i < block.size(); ++i) {
                quint8 currentVIC = static_cast<quint8>(block[offset + i]) & 0x7F;
                if (currentVIC == 0) continue;

                if (disabledVICs.contains(currentVIC)) {
                    qDebug() << "  Disabling VIC" << currentVIC << "-> setting to 0x00";
                    block[offset + i] = 0x00;
                    blockModified = true;
                } else if (enabledVICs.contains(currentVIC)) {
                    qDebug() << "  VIC" << currentVIC << "remains enabled";
                } else {
                    qDebug() << "  VIC" << currentVIC << "not in selection list - leaving unchanged";
                }
            }

            break;
        }

        offset += length + 1;
        if (offset >= dtdOffset) break;
    }

    if (!foundVideoDataBlock) {
        qDebug() << "No Video Data Block found in CEA-861 extension block";
        return false;
    }

    return blockModified;
}

void EdidProcessor::finalizeEdidBlock(QByteArray &modifiedFirmware, const QByteArray &originalFirmware) const
{
    quint16 firmwareChecksum = edid::FirmwareUtils::calculateFirmwareChecksumWithDiff(originalFirmware, modifiedFirmware);
    if (modifiedFirmware.size() >= 2) {
        modifiedFirmware[modifiedFirmware.size() - 2] = static_cast<char>((firmwareChecksum >> 8) & 0xFF);
        modifiedFirmware[modifiedFirmware.size() - 1] = static_cast<char>(firmwareChecksum & 0xFF);
    }
}

QByteArray EdidProcessor::processDisplaySettings(const QByteArray &firmwareData,
                                                 const QString &newName,
                                                 const QString &newSerial) const
{
    qDebug() << "Processing EDID display settings update...";
    if (!newName.isEmpty()) {
        qDebug() << "  Updating display name to:" << newName;
    }
    if (!newSerial.isEmpty()) {
        qDebug() << "  Updating serial number to:" << newSerial;
    }

    bool hasResolutionUpdate = m_resolutionModel.hasChanges();
    if (hasResolutionUpdate) {
        qDebug() << "  Updating resolution settings in extension blocks";
    }

    QByteArray modifiedFirmware = firmwareData;

    qDebug() << "=== COMPLETE FIRMWARE BEFORE UPDATE ===";
    qDebug() << "Firmware size:" << firmwareData.size() << "bytes";
    edid::EDIDUtils::showFirmwareHexDump(firmwareData, 0, qMin(256, firmwareData.size()));

    int edidOffset;
    QByteArray edidBlock;
    if (!parseEdidBlock(modifiedFirmware, edidOffset, edidBlock)) {
        qWarning() << "EDID Block 0 not found or incomplete in firmware";
        return QByteArray();
    }

    QByteArray originalEDIDBlock = edidBlock;

    qDebug() << "=== EDID DESCRIPTORS BEFORE UPDATE ===";
    edid::EDIDUtils::showEDIDDescriptors(edidBlock);

    applyEdidUpdates(modifiedFirmware, edidOffset, newName, newSerial);

    qDebug() << "=== EDID DESCRIPTORS AFTER UPDATE ===";
    edid::EDIDUtils::showEDIDDescriptors(modifiedFirmware.mid(edidOffset, 128));

    finalizeEdidBlock(modifiedFirmware, firmwareData);

    qDebug() << "=== COMPLETE FIRMWARE AFTER UPDATE ===";
    qDebug() << "Modified firmware size:" << modifiedFirmware.size() << "bytes";
    edid::EDIDUtils::showFirmwareHexDump(modifiedFirmware, 0, qMin(256, modifiedFirmware.size()));

    if (modifiedFirmware.size() > 32) {
        qDebug() << "=== FIRMWARE END (last 32 bytes) ===";
        edid::EDIDUtils::showFirmwareHexDump(modifiedFirmware, modifiedFirmware.size() - 32, 32);
    }

    qDebug() << "EDID display settings processing completed successfully";
    return modifiedFirmware;
}
