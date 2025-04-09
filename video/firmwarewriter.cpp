#include "firmwarewriter.h"
#include "videohid.h"

#include <QThread>
#include <QDebug>

FirmwareWriter::FirmwareWriter(VideoHid* videoHid, quint16 address, const QByteArray& firmware, QObject* parent)
    : QObject(parent), m_videoHid(videoHid), m_address(address), m_firmware(firmware)
{
}

void FirmwareWriter::process()
{
    qDebug() << "Starting firmware write process in thread:" << QThread::currentThreadId();
    
    int totalSize = m_firmware.size();
    int lastPercent = 0;
    
    // Connect to the VideoHid written_size signal to track progress
    connect(m_videoHid, &VideoHid::firmwareWriteChunkComplete, this, [this, totalSize, &lastPercent](int writtenBytes) {
        int percent = (writtenBytes * 100) / totalSize;
        if (percent > lastPercent) {
            lastPercent = percent;
            emit progress(percent);
        }
    });
    
    // Perform the actual firmware write
    bool success = m_videoHid->writeEeprom(m_address, m_firmware);
    
    // Signal completion
    emit finished(success);
}
