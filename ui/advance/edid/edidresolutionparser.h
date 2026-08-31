#ifndef EDIDRESOLUTIONPARSER_H
#define EDIDRESOLUTIONPARSER_H

#include <QList>
#include <QByteArray>
#include <QString>

namespace edid {

struct ResolutionInfo {
    QString description;
    int width;
    int height;
    int refreshRate;
    quint8 vic;
    bool isStandardTiming;
    bool isEnabled;
    bool userSelected;

    ResolutionInfo();
    ResolutionInfo(const QString &desc, int w, int h, int rate, quint8 v = 0, bool isStd = false);
};

class EDIDResolutionParser
{
public:
    static QList<ResolutionInfo> parseStandardTimings(const QByteArray &edidBlock);
    static QList<ResolutionInfo> parseDetailedTimingDescriptors(const QByteArray &edidBlock);
    static QList<ResolutionInfo> parseCEA861ExtensionBlocks(const QByteArray &firmwareData, int baseOffset);
    static ResolutionInfo getVICResolutionInfo(quint8 vic);
    static QString getVICResolution(quint8 vic);
};

} // namespace edid

#endif // EDIDRESOLUTIONPARSER_H
