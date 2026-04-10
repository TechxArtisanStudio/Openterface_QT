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
    qDebug() << "Starting firmware read process in thread:" << QThread::currentThreadId();

    // Read firmware from EEPROM, passing a progress callback so VideoHid can report
    // per-chunk progress without needing the removed firmwareReadProgress signal.
    QByteArray firmwareData = m_videoHid->readEeprom(m_address, m_size,
                                                      [this](int pct){ emit progress(pct); });

    if (QThread::currentThread()->isInterruptionRequested()) {
        qDebug() << "FirmwareReader::process interrupted after read";
        emit finished(false);
        return;
    }

    if (firmwareData.isEmpty()) {
        qDebug() << "Failed to read firmware from EEPROM";
        emit error("Failed to read firmware from EEPROM");
        emit finished(false);
        return;
    }

    // Return firmware via signal without writing to disk
    qDebug() << "Firmware successfully read into memory (avoided disk write)";
    emit finished(true, firmwareData);
}
