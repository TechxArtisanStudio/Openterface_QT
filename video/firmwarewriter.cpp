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
        return;
    }

    int percent = qBound(1, (writtenBytes * 100) / static_cast<int>(m_totalSize), 100);
    int last = m_lastPercent.load();

    // Always update progress at least once per percent-step
    if (percent > last) {
        m_lastPercent.store(percent);
        emit progress(percent);
    } else if (percent == last && percent > 0 && percent < 100) {
        // Value may remain same during small chunks; emit occasionally to keep UI responsive
        emit progress(percent);
    }
} 

void FirmwareWriter::process()
{
    
    // Initialize progress state
    m_totalSize = m_firmware.size();
    m_lastPercent.store(0);

    // Perform the actual firmware write directly in this worker thread.
    // Pass our onChunkWritten handler as a callback so VideoHid can report
    // per-chunk progress without needing the removed firmwareWriteChunkComplete signal.
    if (!m_videoHid) {
        emit finished(false);
        return;
    }

    bool success = m_videoHid->writeEeprom(m_address, m_firmware,
                                            [this](int n){ onChunkWritten(n); });

    emit finished(success);
}
