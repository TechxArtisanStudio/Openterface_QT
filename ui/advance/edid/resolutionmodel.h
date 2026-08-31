#ifndef RESOLUTIONMODEL_H
#define RESOLUTIONMODEL_H

#include <QList>
#include <QSet>
#include <QTableWidget>
#include <QString>

namespace edid {
class EDIDResolutionParser;
class EDIDUtils;
}

// Structure to hold resolution information
struct ResolutionInfo {
    QString description;
    int width;
    int height;
    int refreshRate;
    quint8 vic;
    bool isStandardTiming;
    bool isEnabled;
    bool userSelected;

    ResolutionInfo() : width(0), height(0), refreshRate(0), vic(0),
        isStandardTiming(false), isEnabled(false), userSelected(false) {}

    ResolutionInfo(const QString& desc, int w, int h, int rate, quint8 v = 0, bool isStd = false)
        : description(desc), width(w), height(h), refreshRate(rate), vic(v),
        isStandardTiming(isStd), isEnabled(false), userSelected(false) {}
};

class ResolutionModel
{
public:
    void clear();
    void addResolution(const QString& description, int width, int height, int refreshRate,
                       quint8 vic = 0, bool isStandardTiming = false, bool isEnabled = false);
    void loadFromEDID(const QByteArray &edidBlock, const QByteArray &firmwareData);
    void populateTable(QTableWidget *table) const;
    QList<ResolutionInfo> selected() const;
    bool hasChanges() const;
    void setSelectionFromTable(QTableWidget *table);
    QSet<quint8> enabledVICs() const;
    QSet<quint8> disabledVICs() const;

    bool isEmpty() const { return availableResolutions.isEmpty(); }

private:
    QList<ResolutionInfo> availableResolutions;
};

#endif // RESOLUTIONMODEL_H
