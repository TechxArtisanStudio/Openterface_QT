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

    if (edidOffset + 126 >= firmwareData.size()) {
        qWarning() << "EDID block too small to check extension count";
        return;
    }

    quint8 extensionCount = static_cast<quint8>(firmwareData[edidOffset + 126]);
    if (extensionCount == 0) {
        return;
    }

    QSet<quint8> enabledVICs = m_resolutionModel.enabledVICs();
    QSet<quint8> disabledVICs = m_resolutionModel.disabledVICs();

    for (const quint8 vic : enabledVICs) {
    }
    for (const quint8 vic : disabledVICs) {
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
            if (updateCEA861ExtensionBlockResolutions(extensionBlock, enabledVICs, disabledVICs)) {
                quint8 blockChecksum = edid::EDIDUtils::calculateEDIDChecksum(extensionBlock);
                firmwareData.replace(blockOffset, 128, extensionBlock);
                anyBlockModified = true;
            }
        } else {
        }
    }

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
            foundVideoDataBlock = true;

            for (int i = 1; i <= length && offset + i < block.size(); ++i) {
                quint8 currentVIC = static_cast<quint8>(block[offset + i]) & 0x7F;
                if (currentVIC == 0) continue;

                if (disabledVICs.contains(currentVIC)) {
                    block[offset + i] = 0x00;
                    blockModified = true;
                } else if (enabledVICs.contains(currentVIC)) {
                } else {
                }
            }

            break;
        }

        offset += length + 1;
        if (offset >= dtdOffset) break;
    }

    if (!foundVideoDataBlock) {
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
    if (!newName.isEmpty()) {
    }
    if (!newSerial.isEmpty()) {
    }

    bool hasResolutionUpdate = m_resolutionModel.hasChanges();
    if (hasResolutionUpdate) {
    }

    QByteArray modifiedFirmware = firmwareData;

    edid::EDIDUtils::showFirmwareHexDump(firmwareData, 0, qMin(256, firmwareData.size()));

    int edidOffset;
    QByteArray edidBlock;
    if (!parseEdidBlock(modifiedFirmware, edidOffset, edidBlock)) {
        qWarning() << "EDID Block 0 not found or incomplete in firmware";
        return QByteArray();
    }

    QByteArray originalEDIDBlock = edidBlock;

    edid::EDIDUtils::showEDIDDescriptors(edidBlock);

    applyEdidUpdates(modifiedFirmware, edidOffset, newName, newSerial);

    edid::EDIDUtils::showEDIDDescriptors(modifiedFirmware.mid(edidOffset, 128));

    finalizeEdidBlock(modifiedFirmware, firmwareData);

    edid::EDIDUtils::showFirmwareHexDump(modifiedFirmware, 0, qMin(256, modifiedFirmware.size()));

    if (modifiedFirmware.size() > 32) {
        edid::EDIDUtils::showFirmwareHexDump(modifiedFirmware, modifiedFirmware.size() - 32, 32);
    }

    return modifiedFirmware;
}
