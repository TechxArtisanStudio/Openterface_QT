#ifndef EDIDPROCESSOR_H
#define EDIDPROCESSOR_H

#include "resolutionmodel.h"
#include <QByteArray>
#include <QString>
#include <QSet>

class EdidProcessor
{
public:
    explicit EdidProcessor(const ResolutionModel &resolutionModel);

    QByteArray processDisplaySettings(const QByteArray &firmwareData,
                                     const QString &newName,
                                     const QString &newSerial) const;

private:
    bool parseEdidBlock(const QByteArray &firmwareData, int &edidOffset, QByteArray &edidBlock) const;
    void applyEdidUpdates(QByteArray &modifiedFirmware, int edidOffset,
                          const QString &newName, const QString &newSerial) const;
    void finalizeEdidBlock(QByteArray &modifiedFirmware,
                            const QByteArray &originalFirmware) const;
    void updateExtensionBlockResolutions(QByteArray &firmwareData, int edidOffset) const;
    bool updateCEA861ExtensionBlockResolutions(QByteArray &block,
                                               const QSet<quint8> &enabledVICs,
                                               const QSet<quint8> &disabledVICs) const;

    const ResolutionModel &m_resolutionModel;
};

#endif // EDIDPROCESSOR_H
