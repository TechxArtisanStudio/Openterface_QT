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

    // Connect to VideoHid's progress signal to forward progress updates
    connect(m_videoHid, &VideoHid::firmwareReadProgress, this, &FirmwareReader::progress);

    // Read firmware from EEPROM
    QByteArray firmwareData = m_videoHid->readEeprom(m_address, m_size);

    // Disconnect the progress signal to prevent further emissions
    disconnect(m_videoHid, &VideoHid::firmwareReadProgress, this, &FirmwareReader::progress);

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
