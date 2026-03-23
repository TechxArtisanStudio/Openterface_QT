#include "edidresolutionparser.h"
#include <QDebug>

namespace edid {

ResolutionInfo::ResolutionInfo()
    : width(0), height(0), refreshRate(0), vic(0), isStandardTiming(false), isEnabled(false), userSelected(false)
{
}

ResolutionInfo::ResolutionInfo(const QString &desc, int w, int h, int rate, quint8 v, bool isStd)
    : description(desc), width(w), height(h), refreshRate(rate), vic(v), isStandardTiming(isStd), isEnabled(false), userSelected(false)
{
}

QList<ResolutionInfo> EDIDResolutionParser::parseStandardTimings(const QByteArray &edidBlock)
{
    QList<ResolutionInfo> list;
    if (edidBlock.size() != 128) return list;

    for (int offset = 35; offset <= 42; offset += 2) {
        quint8 byte1 = static_cast<quint8>(edidBlock[offset]);
        quint8 byte2 = static_cast<quint8>(edidBlock[offset + 1]);
        if (byte1 == 0x01 && byte2 == 0x01) continue;

        int hres = (byte1 + 31) * 8;
        quint8 aspect = (byte2 >> 6) & 0x03;
        int vres = 0;
        switch (aspect) {
            case 0: vres = hres * 10 / 16; break;
            case 1: vres = hres * 3 / 4; break;
            case 2: vres = hres * 4 / 5; break;
            case 3: vres = hres * 9 / 16; break;
        }

        int refresh = (byte2 & 0x3F) + 60;
        list.append(ResolutionInfo(QString("%1x%2 @ %3Hz").arg(hres).arg(vres).arg(refresh), hres, vres, refresh, 0, true));
    }

    return list;
}

QList<ResolutionInfo> EDIDResolutionParser::parseDetailedTimingDescriptors(const QByteArray &edidBlock)
{
    QList<ResolutionInfo> list;
    if (edidBlock.size() != 128) return list;

    for (int desc = 54; desc <= 108; desc += 18) {
        if (desc + 18 > edidBlock.size()) break;

        quint16 pixelClock = static_cast<quint8>(edidBlock[desc]) | (static_cast<quint8>(edidBlock[desc + 1]) << 8);
        if (pixelClock == 0) continue;

        int hActive = (static_cast<quint8>(edidBlock[desc + 2]) | ((static_cast<quint8>(edidBlock[desc + 4]) & 0xF0) << 4));
        int vActive = (static_cast<quint8>(edidBlock[desc + 5]) | ((static_cast<quint8>(edidBlock[desc + 7]) & 0xF0) << 4));

        list.append(ResolutionInfo(QString("%1x%2").arg(hActive).arg(vActive), hActive, vActive, 0, 0, false));
    }

    return list;
}

QList<ResolutionInfo> EDIDResolutionParser::parseCEA861ExtensionBlocks(const QByteArray &firmwareData, int baseOffset)
{
    QList<ResolutionInfo> list;
    if (baseOffset < 0 || baseOffset + 126 >= firmwareData.size()) return list;

    QByteArray edidBlock = firmwareData.mid(baseOffset, 128);
    quint8 extensionCount = static_cast<quint8>(edidBlock[126]);

    for (int blockIndex = 1; blockIndex <= extensionCount; ++blockIndex) {
        int offset = baseOffset + blockIndex * 128;
        if (offset + 128 > firmwareData.size()) break;

        QByteArray block = firmwareData.mid(offset, 128);
        if (static_cast<quint8>(block[0]) != 0x02) continue;

        quint8 dtdOffset = static_cast<quint8>(block[2]);

        if (dtdOffset > 4 && dtdOffset < 128) {
            int descriptorStart = 4;

            while (descriptorStart < dtdOffset) {
                quint8 tag = static_cast<quint8>(block[descriptorStart]);
                quint8 length = tag & 0x1F;
                quint8 type = (tag >> 5) & 0x07;

                if (type == 0) break;
                if (type == 2) {
                    QByteArray videoData = block.mid(descriptorStart + 1, length);
                    for (int i = 0; i < videoData.size(); ++i) {
                        quint8 vic = static_cast<quint8>(videoData[i]) & 0x7F;
                        list.append(getVICResolutionInfo(vic));
                    }
                }

                descriptorStart += 1 + length;
            }
        }
    }

    return list;
}

ResolutionInfo EDIDResolutionParser::getVICResolutionInfo(quint8 vic)
{
    switch (vic) {
        case 1: return ResolutionInfo("640x480 @ 60Hz", 640, 480, 60, vic);
        case 2: return ResolutionInfo("720x480 @ 60Hz", 720, 480, 60, vic);
        case 3: return ResolutionInfo("720x480 @ 60Hz", 720, 480, 60, vic);
        case 4: return ResolutionInfo("1280x720 @ 60Hz", 1280, 720, 60, vic);
        case 16: return ResolutionInfo("1920x1080 @ 60Hz", 1920, 1080, 60, vic);
        case 93: return ResolutionInfo("3840x2160 @ 24Hz", 3840, 2160, 24, vic);
        case 97: return ResolutionInfo("3840x2160 @ 60Hz", 3840, 2160, 60, vic);
        default: return ResolutionInfo(QString("Unknown VIC %1").arg(vic), 0, 0, 0, vic);
    }
}

QString EDIDResolutionParser::getVICResolution(quint8 vic)
{
    switch (vic) {
        case 1: return "640x480p @ 59.94/60Hz";
        case 2: return "720x480p @ 59.94/60Hz";
        case 4: return "1280x720p @ 59.94/60Hz";
        case 16: return "1920x1080p @ 59.94/60Hz";
        case 93: return "3840x2160p @ 23.98/24Hz";
        case 97: return "3840x2160p @ 59.94/60Hz";
        default: return QString("Unknown VIC %1").arg(vic);
    }
}

} // namespace edid
