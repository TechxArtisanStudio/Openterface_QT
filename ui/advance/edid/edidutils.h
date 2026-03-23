#ifndef EDIDUTILS_H
#define EDIDUTILS_H

#include <QByteArray>
#include <QString>

namespace edid {

class EDIDUtils
{
public:
    static int findEDIDBlock0(const QByteArray &firmwareData);
    static void parseEDIDDescriptors(const QByteArray &edidBlock, QString &displayName, QString &serialNumber);
    static quint8 calculateEDIDChecksum(const QByteArray &edidBlock);
    static void updateEDIDDisplayName(QByteArray &edidBlock, const QString &newName);
    static void updateEDIDSerialNumber(QByteArray &edidBlock, const QString &newSerial);
    static void showEDIDDescriptors(const QByteArray &edidBlock);
    static void showFirmwareHexDump(const QByteArray &firmwareData, int startOffset = 0, int length = -1);
};

} // namespace edid

#endif // EDIDUTILS_H
