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

    if (firmwareData.isEmpty()) {
        qDebug() << "Failed to read firmware from EEPROM";
        emit error("Failed to read firmware from EEPROM");
        emit finished(false);
        return;
    }

    // Save firmware to file
    QFile file(m_outputFilePath);
    if (!file.open(QIODevice::WriteOnly)) {
        qDebug() << "Failed to open file for writing:" << m_outputFilePath;
        emit error(QString("Failed to open file for writing: %1").arg(m_outputFilePath));
        emit finished(false);
        return;
    }

    qint64 bytesWritten = file.write(firmwareData);
    file.close();

    if (bytesWritten != firmwareData.size()) {
        qDebug() << "Failed to write all firmware data to file:" << m_outputFilePath;
        emit error(QString("Failed to write all firmware data to file: %1").arg(m_outputFilePath));
        emit finished(false);
        return;
    }

    qDebug() << "Firmware successfully read and saved to:" << m_outputFilePath;
    emit finished(true);
}