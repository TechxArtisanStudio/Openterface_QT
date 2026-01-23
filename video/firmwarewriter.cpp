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
    int percent = (writtenBytes * 100) / m_totalSize;
    int last = m_lastPercent.load();
    qDebug() << "FirmwareWriter::onChunkWritten writtenBytes=" << writtenBytes << " total=" << m_totalSize << " percent=" << percent << " last=" << last;
    if (percent > last) {
        // update atomically
        m_lastPercent.store(percent);
        emit progress(percent);
        qDebug() << "FirmwareWriter::progress emitted percent=" << percent;
    }
} 

void FirmwareWriter::process()
{
    qDebug() << "Starting firmware write process in thread:" << QThread::currentThreadId();
    
    // Initialize progress state
    m_totalSize = m_firmware.size();
    m_lastPercent.store(0);

    // Connect to the VideoHid written_size signal to track progress.
    // Use DirectConnection so the slot runs immediately in the emitter's thread and can calculate percent safely
    connect(m_videoHid, &VideoHid::firmwareWriteChunkComplete, this, &FirmwareWriter::onChunkWritten, Qt::DirectConnection);

    // Perform the actual firmware write safely by invoking the VideoHid's slot in its own thread
    bool success = false;
    QPointer<QObject> safeVideo(static_cast<QObject*>(m_videoHid));
    if (!safeVideo) {
        qDebug() << "FirmwareWriter: VideoHid no longer exists, aborting write.";
        // Ensure progress handler disconnected
        disconnect(m_videoHid, &VideoHid::firmwareWriteChunkComplete, this, &FirmwareWriter::onChunkWritten);
        emit finished(false);
        return;
    }

    // Use a blocking queued invocation to perform the write in VideoHid's thread
    QMetaObject::invokeMethod(safeVideo.data(), "performWriteEeprom",
                              Qt::BlockingQueuedConnection,
                              Q_RETURN_ARG(bool, success),
                              Q_ARG(quint16, m_address),
                              Q_ARG(QByteArray, m_firmware));

    // Disconnect progress handler now that write is finished
    disconnect(m_videoHid, &VideoHid::firmwareWriteChunkComplete, this, &FirmwareWriter::onChunkWritten);

    // Signal completion
    emit finished(success);
}
