#include "firmwarereader.h"
#include "videohid.h"
#include <QFile>
#include <QDebug>
#include <QThread>

FirmwareReader::FirmwareReader(VideoHid* videoHid, quint16 address, quint32 size, const QString& outputFilePath, QObject* parent)
    : QObject(parent), m_videoHid(videoHid), m_address(address), m_size(size), m_outputFilePath(outputFilePath)
{
}

void FirmwareReader::process()
{

    // Read firmware from EEPROM, passing a progress callback so VideoHid can report
    // per-chunk progress without needing the removed firmwareReadProgress signal.
    QByteArray firmwareData = m_videoHid->readEeprom(m_address, m_size,
                                                      [this](int pct){ emit progress(pct); });

    if (QThread::currentThread()->isInterruptionRequested()) {
        emit finished(false);
        return;
    }

    if (firmwareData.isEmpty()) {
        emit error("Failed to read firmware from EEPROM");
        emit finished(false);
        return;
    }

    // Return firmware via signal without writing to disk
    emit finished(true, firmwareData);
}
