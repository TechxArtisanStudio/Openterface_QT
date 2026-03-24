#include "resolutionmodel.h"
#include "edidresolutionparser.h"
#include "edidutils.h"

void ResolutionModel::clear()
{
    availableResolutions.clear();
}

void ResolutionModel::addResolution(const QString &description, int width, int height, int refreshRate, quint8 vic,
                                   bool isStandardTiming, bool isEnabled)
{
    for (const auto &existing : qAsConst(availableResolutions)) {
        if (existing.width == width && existing.height == height && existing.refreshRate == refreshRate) {
            return;
        }
    }

    ResolutionInfo info(description, width, height, refreshRate, vic, isStandardTiming);
    info.isEnabled = isEnabled;
    info.userSelected = isEnabled;
    availableResolutions.append(info);
}

void ResolutionModel::loadFromEDID(const QByteArray &edidBlock, const QByteArray &firmwareData)
{
    clear();

    auto standardTimings = edid::EDIDResolutionParser::parseStandardTimings(edidBlock);
    for (const auto &r : standardTimings) {
        addResolution(r.description, r.width, r.height, r.refreshRate, r.vic, r.isStandardTiming, r.isEnabled);
    }

    auto detailedTimings = edid::EDIDResolutionParser::parseDetailedTimingDescriptors(edidBlock);
    for (const auto &r : detailedTimings) {
        addResolution(r.description, r.width, r.height, r.refreshRate, r.vic, r.isStandardTiming, r.isEnabled);
    }

    int baseOffset = edid::EDIDUtils::findEDIDBlock0(firmwareData);
    auto extensionResolutions = edid::EDIDResolutionParser::parseCEA861ExtensionBlocks(firmwareData, baseOffset);
    for (const auto &r : extensionResolutions) {
        addResolution(r.description, r.width, r.height, r.refreshRate, r.vic, r.isStandardTiming, r.isEnabled);
    }
}

void ResolutionModel::populateTable(QTableWidget *table) const
{
    if (!table) return;

    table->clearContents();
    table->setRowCount(availableResolutions.size());

    for (int row = 0; row < availableResolutions.size(); ++row) {
        const ResolutionInfo &resolution = availableResolutions[row];

        auto *checkItem = new QTableWidgetItem();
        checkItem->setCheckState(resolution.userSelected ? Qt::Checked : Qt::Unchecked);
        checkItem->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
        table->setItem(row, 0, checkItem);

        auto *resolutionItem = new QTableWidgetItem(QString("%1x%2").arg(resolution.width).arg(resolution.height));
        resolutionItem->setFlags(Qt::ItemIsEnabled);
        table->setItem(row, 1, resolutionItem);

        auto *refreshItem = new QTableWidgetItem(QString("%1 Hz").arg(resolution.refreshRate));
        refreshItem->setFlags(Qt::ItemIsEnabled);
        table->setItem(row, 2, refreshItem);

        QString sourceText = QObject::tr("Extension");
        if (resolution.vic > 0) {
            sourceText += QString(" (VIC %1)").arg(resolution.vic);
        }

        auto *sourceItem = new QTableWidgetItem(sourceText);
        sourceItem->setFlags(Qt::ItemIsEnabled);
        table->setItem(row, 3, sourceItem);
    }
}

QList<ResolutionInfo> ResolutionModel::selected() const
{
    QList<ResolutionInfo> selected;
    for (const auto &r : availableResolutions) {
        if (r.userSelected) selected.append(r);
    }
    return selected;
}

bool ResolutionModel::hasChanges() const
{
    for (const auto &r : availableResolutions) {
        if (r.userSelected != r.isEnabled) return true;
    }
    return false;
}

void ResolutionModel::setSelectionFromTable(QTableWidget *table)
{
    if (!table) return;
    int rowCount = qMin(table->rowCount(), availableResolutions.size());

    for (int row = 0; row < rowCount; ++row) {
        QTableWidgetItem *checkItem = table->item(row, 0);
        if (checkItem) {
            availableResolutions[row].userSelected = (checkItem->checkState() == Qt::Checked);
        }
    }
}

QSet<quint8> ResolutionModel::enabledVICs() const
{
    QSet<quint8> list;
    for (const auto &r : availableResolutions) {
        if (r.vic > 0 && r.userSelected) list.insert(r.vic);
    }
    return list;
}

QSet<quint8> ResolutionModel::disabledVICs() const
{
    QSet<quint8> list;
    for (const auto &r : availableResolutions) {
        if (r.vic > 0 && !r.userSelected) list.insert(r.vic);
    }
    return list;
}
