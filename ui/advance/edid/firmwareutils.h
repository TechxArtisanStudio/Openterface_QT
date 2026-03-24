#ifndef FIRMWAREUTILS_H
#define FIRMWAREUTILS_H

#include <QByteArray>

namespace edid {

class FirmwareUtils
{
public:
    static quint16 calculateFirmwareChecksumWithDiff(const QByteArray &originalFirmware, const QByteArray &originalEDID, const QByteArray &modifiedEDID);
    static quint16 calculateFirmwareChecksumWithDiff(const QByteArray &originalFirmware, const QByteArray &modifiedFirmware);
    static bool backupFirmware(const QByteArray &firmwareData, const QString &path);
};

} // namespace edid

#endif // FIRMWAREUTILS_H