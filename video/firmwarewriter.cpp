#include "firmwarewriter.h"
#include "videohid.h"

#include <QThread>
#include <QDebug>
#include <QPointer>

FirmwareWriter::FirmwareWriter(VideoHid* videoHid, quint16 address, const QByteArray& firmware, QObject* parent)
    : QObject(parent), m_videoHid(videoHid), m_address(address), m_firmware(firmware)
{
}

void FirmwareWriter::onChunkWritten(int writtenBytes)
{
    if (m_totalSize <= 0) {
        qDebug() << "FirmwareWriter::onChunkWritten called but total size is zero";
        return;
    }

    int percent = qBound(1, (writtenBytes * 100) / static_cast<int>(m_totalSize), 100);
    int last = m_lastPercent.load();

    qDebug() << "FirmwareWriter::onChunkWritten writtenBytes=" << writtenBytes
             << " total=" << m_totalSize << " percent=" << percent << " last=" << last;

    // Always update progress at least once per percent-step
    if (percent > last) {
        m_lastPercent.store(percent);
        emit progress(percent);
        qDebug() << "FirmwareWriter::progress emitted percent=" << percent;
    } else if (percent == last && percent > 0 && percent < 100) {
        // Value may remain same during small chunks; emit occasionally to keep UI responsive
        emit progress(percent);
    }
} 

void FirmwareWriter::process()
{
    qDebug() << "Starting firmware write process in thread:" << QThread::currentThreadId();
    
    // Initialize progress state
    m_totalSize = m_firmware.size();
    m_lastPercent.store(0);

    // Connect to the VideoHid written_size signal to track progress.
    // Use DirectConnection because the write loop is synchronous in this thread, and we need immediate progress updates.
    connect(m_videoHid, &VideoHid::firmwareWriteChunkComplete, this, &FirmwareWriter::onChunkWritten, Qt::DirectConnection);

    // Perform the actual firmware write directly in this worker thread.
    bool success = false;
    if (!m_videoHid) {
        qDebug() << "FirmwareWriter: VideoHid is null, aborting write.";
        disconnect(m_videoHid, &VideoHid::firmwareWriteChunkComplete, this, &FirmwareWriter::onChunkWritten);
        emit finished(false);
        return;
    }

    // perform write directly on worker thread; VideoHid write/EEPROM operations should be thread-safe when polling is paused.
    success = m_videoHid->writeEeprom(m_address, m_firmware);

    disconnect(m_videoHid, &VideoHid::firmwareWriteChunkComplete, this, &FirmwareWriter::onChunkWritten);

    emit finished(success);
}
