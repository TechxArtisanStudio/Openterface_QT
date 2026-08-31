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

    static const struct AspectRatio { int num; int den; } aspectRates[4] = {
        {10, 16}, // 16:10
        {3, 4},   // 4:3
        {4, 5},   // 5:4
        {9, 16}   // 16:9
    };

    for (int offset = 35; offset <= 42; offset += 2) {
        quint8 byte1 = static_cast<quint8>(edidBlock[offset]);
        quint8 byte2 = static_cast<quint8>(edidBlock[offset + 1]);
        if (byte1 == 0x01 && byte2 == 0x01) continue;

        int hres = (byte1 + 31) * 8;
        quint8 aspect = (byte2 >> 6) & 0x03;

        if (aspect >= 4) continue;
        int vres = hres * aspectRates[aspect].num / aspectRates[aspect].den;

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

namespace {

const struct VICEntry { quint8 vic; int width; int height; int refresh; const char *desc; const char *pretty; } VIC_TABLE[] = {
    {1,  640,  480,  60, "640x480 @ 60Hz",   "640x480p @ 59.94/60Hz"},
    {2,  720,  480,  60, "720x480 @ 60Hz",   "720x480p @ 59.94/60Hz"},
    {3,  720,  480,  60, "720x480 @ 60Hz",   "720x480p @ 59.94/60Hz"},
    {4, 1280,  720,  60, "1280x720 @ 60Hz",  "1280x720p @ 59.94/60Hz"},
    {16,1920, 1080, 60, "1920x1080 @ 60Hz", "1920x1080p @ 59.94/60Hz"},
    {93,3840,2160, 24, "3840x2160 @ 24Hz", "3840x2160p @ 23.98/24Hz"},
    {97,3840,2160, 60, "3840x2160 @ 60Hz", "3840x2160p @ 59.94/60Hz"},
};

const int VIC_TABLE_SIZE = sizeof(VIC_TABLE) / sizeof(VIC_TABLE[0]);

} // namespace

ResolutionInfo EDIDResolutionParser::getVICResolutionInfo(quint8 vic)
{
    for (int i = 0; i < VIC_TABLE_SIZE; ++i) {
        if (VIC_TABLE[i].vic == vic) {
            ResolutionInfo info(VIC_TABLE[i].desc, VIC_TABLE[i].width, VIC_TABLE[i].height, VIC_TABLE[i].refresh, vic);
            return info;
        }
    }
    return ResolutionInfo(QString("Unknown VIC %1").arg(vic), 0, 0, 0, vic);
}

QString EDIDResolutionParser::getVICResolution(quint8 vic)
{
    for (int i = 0; i < VIC_TABLE_SIZE; ++i) {
        if (VIC_TABLE[i].vic == vic) {
            return QString::fromUtf8(VIC_TABLE[i].pretty);
        }
    }
    return QString("Unknown VIC %1").arg(vic);
}

} // namespace edid
