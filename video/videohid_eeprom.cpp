#include "videohid.h"
#include "firmwarereader.h"
#include "videohidchip.h"
#include <QThread>
#include <QDebug>
#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(log_host_hid)

bool VideoHid::readChunk(quint16 address, QByteArray &data, int chunkSize) {
    const int REPORT_SIZE = 9;
    QByteArray ctrlData(REPORT_SIZE, 0);
    QByteArray result(REPORT_SIZE, 0);

    ctrlData[1] = CMD_EEPROM_READ;
    ctrlData[2] = static_cast<char>((address >> 8) & 0xFF);
    ctrlData[3] = static_cast<char>(address & 0xFF);

    if (sendFeatureReport((uint8_t*)ctrlData.data(), ctrlData.size())) {
        if (getFeatureReport((uint8_t*)result.data(), result.size())) {
            data.append(result.mid(4, chunkSize));
            read_size += chunkSize;
            return true;
        }
    }
    qWarning() << "Failed to read chunk from address:" << QString("0x%1").arg(address, 4, 16, QChar('0'));
    return false;
}

QByteArray VideoHid::readEeprom(quint16 address, quint32 size,
                                std::function<void(int)> progressCallback)
{
    const int MAX_CHUNK = 1;
    const int MAX_RETRIES = 3; // Number of retries for failed reads
    QByteArray firmwareData;
    read_size = 0;

    // Begin transaction for the entire operation
    if (!beginTransaction()) {
        qCDebug(log_host_hid) << "Failed to begin transaction for EEPROM read";
        return QByteArray();
    }

    bool success = true;
    quint16 currentAddress = address;
    quint32 bytesRemaining = size;

    while (bytesRemaining > 0 && success) {
        if (QThread::currentThread()->isInterruptionRequested()) {
            qCInfo(log_host_hid) << "readEeprom interrupted";
            endTransaction();
            return QByteArray();
        }

        int chunkSize = qMin(MAX_CHUNK, static_cast<int>(bytesRemaining));
        QByteArray chunk;
        bool chunkSuccess = false;
        int retries = MAX_RETRIES;

        // Retry reading the chunk up to MAX_RETRIES times
        while (retries > 0 && !chunkSuccess) {
            chunkSuccess = readChunk(currentAddress, chunk, chunkSize);
            if (!chunkSuccess) {
                retries--;
                qCDebug(log_host_hid) << "Retry" << (MAX_RETRIES - retries) << "of" << MAX_RETRIES
                                      << "for reading chunk at address:" << QString("0x%1").arg(currentAddress, 4, 16, QChar('0'));
                QThread::msleep(15); // Short delay before retrying
            }
        }

        if (chunkSuccess) {
            firmwareData.append(chunk);
            currentAddress += chunkSize;
            bytesRemaining -= chunkSize;
            if (progressCallback) progressCallback((read_size * 100) / size);
            if (read_size % 64 == 0) {
                qCDebug(log_host_hid) << "Read size:" << read_size;
            }
            QThread::msleep(5); // Add 5ms delay between successful reads
        } else {
            qCDebug(log_host_hid) << "Failed to read chunk from EEPROM at address:" << QString("0x%1").arg(currentAddress, 4, 16, QChar('0'))
                                  << "after" << MAX_RETRIES << "retries";
            success = false;
            break;
        }
    }

    // End transaction
    endTransaction();

    if (!success) {
        qCDebug(log_host_hid) << "EEPROM read failed";
        return QByteArray();
    }

    return firmwareData;
}

quint32 VideoHid::readFirmwareSize(){
    QByteArray header = readEeprom(ADDR_EEPROM, 4);
    if (header.size() != 4) {
        qDebug() << "Can not read firemware header form eeprom:" << header.size();
        return 0;
    }

    quint16 sizeBytes = (static_cast<quint8>(header[2]) << 8) + static_cast<quint8>(header[3]);
    quint32 firmwareSize = sizeBytes + 52;
    qDebug() << "Caculate firmware size:" << firmwareSize << " bytes";
    
    return firmwareSize;
}

void VideoHid::loadEepromToFile(const QString &filePath) {
    quint32 firmwareSize = readFirmwareSize();

    QThread* thread = new QThread();
    thread->setObjectName("FirmwareReaderThread");
    FirmwareReader *worker = new FirmwareReader(this, ADDR_EEPROM, firmwareSize, filePath);
    worker->moveToThread(thread);

    connect(thread, &QThread::started, worker, &FirmwareReader::process);
    connect(worker, &FirmwareReader::finished, thread, &QThread::quit);
    connect(worker, &FirmwareReader::finished, worker, &FirmwareReader::deleteLater);
    connect(thread, &QThread::finished, thread, &QThread::deleteLater);


    connect(worker, &FirmwareReader::finished, this, [](bool success) {
        if (success) {
            qCDebug(log_host_hid) << "Firmware read completed successfully";
        } else {
            qCDebug(log_host_hid) << "Firmware read failed - user should try again";
        }
    });
    
    thread->start();
}

bool VideoHid::writeChunk(quint16 address, const QByteArray &data,
                          const std::function<void(int)>& chunkCallback)
{
    const int chunkSize = 1;
    const int REPORT_SIZE = 9;

    int length = data.size();

    quint16 _address = address;
    bool status = false;
    for (int i = 0; i < length; i += chunkSize) {
        QByteArray chunk = data.mid(i, chunkSize);
        int chunk_length = chunk.size();
        QByteArray report(REPORT_SIZE, 0);
        report[1] = CMD_EEPROM_WRITE;
        report[2] = (_address >> 8) & 0xFF;
        report[3] = _address & 0xFF;
        report.replace(4, chunk_length, chunk);
        qCDebug(log_host_hid)  << "Report:" << report.toHex(' ').toUpper();
        
        status = sendFeatureReport((uint8_t*)report.data(), report.size());
        qCDebug(log_host_hid) << "writeChunk: sendFeatureReport" << (status ? "OK" : "FAIL") << "addr=" << QString("0x%1").arg(_address, 4, 16, QChar('0'));

        if (!status) {
            qWarning() << "Failed to write chunk to address:" << QString("0x%1").arg(_address, 4, 16, QChar('0'));
            return false;
        }
        written_size += chunk_length;
        if (chunkCallback) chunkCallback(written_size);
        _address += chunkSize; 
    }
    return true;
}

bool VideoHid::writeEeprom(quint16 address, const QByteArray &data,
                           std::function<void(int)> chunkCallback)
{
    // Snapshot chip type once to avoid hotplug race changing behavior mid-flash.
    const VideoChipType chipTypeAtStart = m_chipType;

    // Signal all background HID reads (polling thread AND any QtConcurrent threads from start())
    // to bail out immediately so they don't compete with firmware write on the USB bus.
    m_flashInProgress.store(true, std::memory_order_release);

    // Pause polling during EEPROM updates to avoid concurrent HID bus contention.
    bool hadPolling = (m_pollingThread != nullptr);
    if (hadPolling) {
        qCDebug(log_host_hid) << "writeEeprom: stopping polling thread to avoid HID bus contention";
        stopPollingOnly();
        QThread::msleep(50); // brief delay for thread stop handshake
    }

    bool success = true;

    if (chipTypeAtStart == VideoChipType::MS2130S) {
        // Override the chip's onChunkWritten for the duration of this write so it calls
        // our callback instead of the removed firmwareWriteChunkComplete signal.
        if (auto* ms2130s = dynamic_cast<Ms2130sChip*>(m_chipImpl.get())) {
            ms2130s->onChunkWritten = [this, chunkCallback](quint32 n) {
                written_size = n;
                if (chunkCallback) chunkCallback(static_cast<int>(n));
            };
        }
        if (auto* ms2130s = dynamic_cast<Ms2130sChip*>(m_chipImpl.get()))
            success = ms2130s->writeFirmware(address, data);
        else
            success = false;
        // Restore no-op default after write
        if (auto* ms2130s = dynamic_cast<Ms2130sChip*>(m_chipImpl.get())) {
            ms2130s->onChunkWritten = [this](quint32 n) { written_size = n; };
        }
    } else {
        const int MAX_CHUNK = 16;
        QByteArray remainingData = data;
        written_size = 0;

        // Begin transaction for the entire operation
        if (!beginTransaction()) {
            qCDebug(log_host_hid)  << "Failed to begin transaction for EEPROM write";
            success = false;
        } else {
            while (!remainingData.isEmpty() && success) {
                QByteArray chunk = remainingData.left(MAX_CHUNK);
                success = writeChunk(address, chunk, chunkCallback);

                if (success) {
                    address += chunk.size();
                    remainingData = remainingData.mid(MAX_CHUNK);
                    if (written_size % 64 == 0) {
                        qCDebug(log_host_hid)  << "Written size:" << written_size;
                    }
                    QThread::msleep(20); // Throttle a little to avoid USB packet bursts
                } else {
                    qCDebug(log_host_hid)  << "Failed to write chunk to EEPROM";
                    break;
                }
            }

            endTransaction();
        }
    }

    // Allow background reads to resume before restarting the polling thread.
    m_flashInProgress.store(false, std::memory_order_release);

    if (hadPolling) {
        // After MS2130S firmware flash the device needs a power cycle to boot the new
        // firmware.  Restarting the polling thread immediately would hammer a device
        // that is still in an undefined state, generating confusing errors.
        if (chipTypeAtStart != VideoChipType::MS2130S) {
            qCDebug(log_host_hid) << "writeEeprom: restarting polling thread after EEPROM update";
            start();
        } else {
            qCInfo(log_host_hid) << "writeEeprom: NOT restarting polling �?MS2130S needs power cycle after flash"
                                 << "(chip snapshot at start:" << static_cast<int>(chipTypeAtStart) << ")";
        }
    }

    return success;
}


