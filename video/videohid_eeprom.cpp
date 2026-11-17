#include "videohid.h"
#include "firmwarereader.h"
#include "videohidchip.h"
#include "detection/ChipDetector.h"
#include "deviceaccessguard.h"
#include "../device/DeviceManager.h"
#include <QThread>
#include <QDebug>
#include "log/opflogging.h"

Q_DECLARE_LOGGING_CATEGORY(log_hid_firmware)

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
        qCDebug(log_hid_firmware) << "Failed to begin transaction for EEPROM read";
        return QByteArray();
    }

    bool success = true;
    quint16 currentAddress = address;
    quint32 bytesRemaining = size;

    while (bytesRemaining > 0 && success) {
        if (QThread::currentThread()->isInterruptionRequested()) {
            qCInfo(log_hid_firmware) << "readEeprom interrupted";
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
                qCDebug(log_hid_firmware) << "Retry" << (MAX_RETRIES - retries) << "of" << MAX_RETRIES
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
                qCDebug(log_hid_firmware) << "Read size:" << read_size;
            }
            QThread::msleep(5); // Add 5ms delay between successful reads
        } else {
            qCDebug(log_hid_firmware) << "Failed to read chunk from EEPROM at address:" << QString("0x%1").arg(currentAddress, 4, 16, QChar('0'))
                                  << "after" << MAX_RETRIES << "retries";
            success = false;
            break;
        }
    }

    // End transaction
    endTransaction();

    if (!success) {
        qCDebug(log_hid_firmware) << "EEPROM read failed";
        return QByteArray();
    }

    return firmwareData;
}

quint32 VideoHid::readFirmwareSize(){
    QByteArray header = readEeprom(ADDR_EEPROM, 4);
    if (header.size() != 4) {
        return 0;
    }

    quint16 sizeBytes = (static_cast<quint8>(header[2]) << 8) + static_cast<quint8>(header[3]);
    quint32 firmwareSize = sizeBytes + 52;
    
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
            qCDebug(log_hid_firmware) << "Firmware read completed successfully";
        } else {
            qCDebug(log_hid_firmware) << "Firmware read failed - user should try again";
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
        qCDebug(log_hid_firmware)  << "Report:" << report.toHex(' ').toUpper();
        
        status = sendFeatureReport((uint8_t*)report.data(), report.size());
        qCDebug(log_hid_firmware) << "writeChunk: sendFeatureReport" << (status ? "OK" : "FAIL") << "addr=" << QString("0x%1").arg(_address, 4, 16, QChar('0'));

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
    // Use RAII guard to acquire exclusive device access.
    // This sets m_flashInProgress flag and stops polling thread cooperatively.
    DeviceAccessGuard guard(*this);
    if (!guard.isExclusive()) {
        qCWarning(log_hid_firmware) << "writeEeprom: Failed to acquire exclusive device access";
        return false;
    }

    qCDebug(log_hid_firmware) << "writeEeprom: Exclusive access acquired via DeviceAccessGuard";

    // Before firmware write, ensure chip type is correctly detected.
    // This is critical for devices in recovery mode (345F:0001) which should be MS2130S.
    // Re-detect if chip type is unknown or if device path suggests recovery mode.
    qCDebug(log_hid_firmware) << "writeEeprom: Checking if chip detection is needed, m_chipType ="
        << (m_chipType == VideoChipType::UNKNOWN ? "UNKNOWN" :
            m_chipType == VideoChipType::MS2130S ? "MS2130S" :
            m_chipType == VideoChipType::MS2109S ? "MS2109S" : "MS2109");

    if (m_chipType == VideoChipType::UNKNOWN || m_chipType == VideoChipType::MS2109S) {
        // Get device path from transport if not cached
        QString devicePath = m_currentHIDDevicePath;
        qCDebug(log_hid_firmware) << "writeEeprom: m_currentHIDDevicePath =" << devicePath;

        if (devicePath.isEmpty() && m_deviceTransport) {
            devicePath = m_deviceTransport->getHIDDevicePath();
            qCDebug(log_hid_firmware) << "writeEeprom: Got device path from transport:" << devicePath;
            // Sync the path so detectChipType() can use it
            if (!devicePath.isEmpty()) {
                qCDebug(log_hid_firmware) << "writeEeprom: Syncing device path from transport for chip detection:" << devicePath;
                m_currentHIDDevicePath = devicePath;
            }
        }

        if (!devicePath.isEmpty()) {
            // Check if device is in recovery mode (345F:0001)
            if (devicePath.contains("0001", Qt::CaseInsensitive) &&
                devicePath.contains("345F", Qt::CaseInsensitive)) {
                qCDebug(log_hid_firmware) << "writeEeprom: Device appears to be in recovery mode (345F:0001), manually setting MS2130S";
                // Polling is now stopped, so we can safely set chip type without mutex
                if (!m_chipImpl || m_chipType != VideoChipType::MS2130S) {
                    m_chipImpl = ChipDetector::createChip(VideoChipType::MS2130S, this);
                    m_chipType = VideoChipType::MS2130S;
                    qCDebug(log_hid_firmware) << "writeEeprom: Manually set m_chipType to MS2130S and created m_chipImpl";
                }
            } else if (m_chipType == VideoChipType::UNKNOWN) {
                qCDebug(log_hid_firmware) << "writeEeprom: Chip type unknown, re-detecting";
                detectChipType();
            }
        } else {
            qCWarning(log_hid_firmware) << "writeEeprom: No device path available for chip re-detection!";
        }
    }

    // Snapshot chip type once to avoid hotplug race changing behavior mid-flash.
    const VideoChipType chipTypeAtStart = m_chipType;
    qCDebug(log_hid_firmware) << "writeEeprom: chipTypeAtStart ="
        << (chipTypeAtStart == VideoChipType::MS2130S ? "MS2130S" :
            chipTypeAtStart == VideoChipType::MS2109S ? "MS2109S" :
            chipTypeAtStart == VideoChipType::MS2109 ? "MS2109" : "UNKNOWN");

    // Debug logging to identify chip type and implementation
    auto chipTypeToString = [](VideoChipType t) -> const char* {
        switch (t) {
            case VideoChipType::MS2109:  return "MS2109";
            case VideoChipType::MS2109S: return "MS2109S";
            case VideoChipType::MS2130S: return "MS2130S";
            default:                     return "UNKNOWN";
        }
    };
    qCDebug(log_hid_firmware) << "writeEeprom: chipTypeAtStart =" << chipTypeToString(chipTypeAtStart)
                              << "m_chipImpl.get() =" << (void*)m_chipImpl.get()
                              << "address =" << QString("0x%1").arg(address, 4, 16, QChar('0'))
                              << "data size =" << data.size();

    bool success = true;

    if (chipTypeAtStart == VideoChipType::MS2130S) {
        qCDebug(log_hid_firmware) << "writeEeprom: Taking MS2130S SPI flash write path";

        // Override the chip's onChunkWritten for the duration of this write so it calls
        // our callback instead of the removed firmwareWriteChunkComplete signal.
        if (auto* ms2130s = dynamic_cast<Ms2130sChip*>(m_chipImpl.get())) {
            qCDebug(log_hid_firmware) << "writeEeprom: Successfully cast to Ms2130sChip, calling writeFirmware()";
            ms2130s->onChunkWritten = [this, chunkCallback](quint32 n) {
                written_size = n;
                if (chunkCallback) chunkCallback(static_cast<int>(n));
            };
        } else {
            qCWarning(log_hid_firmware) << "writeEeprom: chipType is MS2130S but m_chipImpl is not Ms2130sChip!";
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
        qCDebug(log_hid_firmware) << "writeEeprom: Taking generic EEPROM write path (chipType is not MS2130S)";
        const int MAX_CHUNK = 16;
        QByteArray remainingData = data;
        written_size = 0;

        // Begin transaction for the entire operation
        if (!beginTransaction()) {
            qCDebug(log_hid_firmware)  << "Failed to begin transaction for EEPROM write";
            success = false;
        } else {
            while (!remainingData.isEmpty() && success) {
                QByteArray chunk = remainingData.left(MAX_CHUNK);
                success = writeChunk(address, chunk, chunkCallback);

                if (success) {
                    address += chunk.size();
                    remainingData = remainingData.mid(MAX_CHUNK);
                    if (written_size % 64 == 0) {
                        qCDebug(log_hid_firmware)  << "Written size:" << written_size;
                    }
                    QThread::msleep(20); // Throttle a little to avoid USB packet bursts
                } else {
                    qCDebug(log_hid_firmware)  << "Failed to write chunk to EEPROM";
                    break;
                }
            }

            endTransaction();
        }
    }

    // Guard destructor will automatically:
    // 1. Clear m_flashInProgress flag
    // 2. Clean up polling thread if it was stopped

    // After a firmware write (successful or not), the device may re-enumerate with a
    // different HID interface. Clear the cached device path so the next open() call
    // forces fresh device discovery instead of using a potentially stale path.
    clearDevicePathCache();

    if (!success) {
        // On failure, the device may be in an undefined state (especially MS2130S).
        // Force a fresh device discovery after a brief delay to let Windows re-enumerate.
        qCInfo(log_hid_firmware) << "writeEeprom: firmware write failed, clearing caches and forcing re-discovery";
        DeviceManager::getInstance().discoverDevices();
    }

    // After MS2130S firmware flash the device needs a power cycle to boot the new
    // firmware.  Restarting the polling thread immediately would hammer a device
    // that is still in an undefined state, generating confusing errors.
    if (chipTypeAtStart != VideoChipType::MS2130S) {
        qCDebug(log_hid_firmware) << "writeEeprom: restarting polling thread after EEPROM update";
        start();
    } else {
        qCInfo(log_hid_firmware) << "writeEeprom: NOT restarting polling - MS2130S needs power cycle after flash"
                             << "(chip snapshot at start:" << static_cast<int>(chipTypeAtStart) << ")";
    }

    return success;
}

