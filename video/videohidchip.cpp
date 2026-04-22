// videohidchip.cpp — self-contained chip protocol implementations.
// Each chip class handles its own register read/write protocol without
// calling back into VideoHid.  All I/O goes through IHIDTransport.

#include "videohidchip.h"

#include <QDebug>
#include <QLoggingCategory>
#include <QThread>
#include <QElapsedTimer>

Q_LOGGING_CATEGORY(log_chip, "opf.core.chip")

// ══════════════════════════════════════════════════════════════
//  Ms2109Chip
// ══════════════════════════════════════════════════════════════

QPair<QByteArray, bool> Ms2109Chip::read4Byte(quint16 address)
{
    if (!m_transport) return qMakePair(QByteArray(4, 0), false);

    QByteArray ctrlData(9, 0);
    QByteArray result(9, 0);

    ctrlData[1] = CMD_XDATA_READ;
    ctrlData[2] = static_cast<char>((address >> 8) & 0xFF);
    ctrlData[3] = static_cast<char>(address & 0xFF);

    qCDebug(log_chip).nospace() << "MS2109 reading from address: 0x"
        << QString::number(address, 16).rightJustified(4, '0').toUpper();

    if (m_transport->sendFeatureReport(reinterpret_cast<uint8_t*>(ctrlData.data()), ctrlData.size())) {
        if (m_transport->getFeatureReport(reinterpret_cast<uint8_t*>(result.data()), result.size())) {
            QByteArray readResult = result.mid(4, 1);
            qCDebug(log_chip).nospace() << "MS2109 read OK addr=0x"
                << QString::number(address, 16).rightJustified(4, '0').toUpper()
                << " val=0x" << QString::number(static_cast<quint8>(readResult.at(0)), 16).rightJustified(2, '0').toUpper();
            return qMakePair(readResult, true);
        }
    } else {
        // Retry with report ID 1
        ctrlData[0] = 0x01;
        if (m_transport->sendFeatureReport(reinterpret_cast<uint8_t*>(ctrlData.data()), ctrlData.size())) {
            if (m_transport->getFeatureReport(reinterpret_cast<uint8_t*>(result.data()), result.size()) && !result.isEmpty()) {
                QByteArray readResult = result.mid(3, 4);
                qCDebug(log_chip) << "MS2109 read OK (alt) addr=0x" << QString::number(address, 16);
                return qMakePair(readResult, true);
            }
        }
    }
    qCWarning(log_chip) << "MS2109 read FAILED addr=0x" << QString::number(address, 16);
    return qMakePair(QByteArray(4, 0), false);
}

bool Ms2109Chip::write4Byte(quint16 address, const QByteArray& data)
{
    if (!m_transport) return false;
    QByteArray ctrlData(9, 0);
    ctrlData[1] = CMD_XDATA_WRITE;
    ctrlData[2] = static_cast<char>((address >> 8) & 0xFF);
    ctrlData[3] = static_cast<char>(address & 0xFF);
    ctrlData.replace(4, qMin(4, data.size()), data.left(4));
    return m_transport->sendFeatureReport(reinterpret_cast<uint8_t*>(ctrlData.data()), ctrlData.size());
}

// ══════════════════════════════════════════════════════════════
//  Ms2109sChip
// ══════════════════════════════════════════════════════════════

QPair<QByteArray, bool> Ms2109sChip::read4Byte(quint16 address)
{
    if (!m_transport) return qMakePair(QByteArray(4, 0), false);

    bool wasOpen = m_transport->isOpen();
    if (!wasOpen) {
        if (!m_transport->open()) {
            qCWarning(log_chip) << "MS2109S read: failed to open transport";
            return qMakePair(QByteArray(4, 0), false);
        }
    }

    QByteArray readResult(1, 0);
    bool success = false;

    // Strategy 1: 11-byte buffer, report ID 0
    if (!success) {
        QByteArray ctrlData(11, 0);
        QByteArray result(11, 0);
        ctrlData[0] = 0x00;
        ctrlData[1] = MS2109S_CMD_XDATA_READ;
        ctrlData[2] = static_cast<char>((address >> 8) & 0xFF);
        ctrlData[3] = static_cast<char>(address & 0xFF);

#ifdef _WIN32
        uint8_t buffer[11] = {};
        memcpy(buffer, ctrlData.data(), 11);
        if (m_transport->sendDirect(buffer, 11)) {
            memset(buffer, 0, sizeof(buffer));
            buffer[0] = 0x00;
            if (m_transport->getDirect(buffer, 11)) {
                readResult[0] = buffer[4];
                success = true;
            }
        }
#else
        if (m_transport->sendFeatureReport(reinterpret_cast<uint8_t*>(ctrlData.data()), ctrlData.size())) {
            if (m_transport->getFeatureReport(reinterpret_cast<uint8_t*>(result.data()), result.size())) {
                readResult[0] = result[4];
                success = true;
            }
        }
#endif
    }

    // Strategy 2: 11-byte buffer, report ID 1
    if (!success) {
        QByteArray ctrlData(11, 0);
        QByteArray result(11, 0);
        ctrlData[0] = 0x01;
        ctrlData[1] = MS2109S_CMD_XDATA_READ;
        ctrlData[2] = static_cast<char>((address >> 8) & 0xFF);
        ctrlData[3] = static_cast<char>(address & 0xFF);

#ifdef _WIN32
        uint8_t buffer[11] = {};
        memcpy(buffer, ctrlData.data(), 11);
        if (m_transport->sendDirect(buffer, 11)) {
            memset(buffer, 0, sizeof(buffer));
            buffer[0] = 0x01;
            if (m_transport->getDirect(buffer, 11)) {
                readResult[0] = buffer[4];
                success = true;
            }
        }
#else
        if (m_transport->sendFeatureReport(reinterpret_cast<uint8_t*>(ctrlData.data()), ctrlData.size())) {
            if (m_transport->getFeatureReport(reinterpret_cast<uint8_t*>(result.data()), result.size())) {
                readResult[0] = result[4];
                success = true;
            }
        }
#endif
    }

    // Strategy 3: 65-byte fallback
    if (!success) {
        QByteArray ctrlData(65, 0);
        QByteArray result(65, 0);
        ctrlData[0] = 0x00;
        ctrlData[1] = MS2109S_CMD_XDATA_READ;
        ctrlData[2] = static_cast<char>((address >> 8) & 0xFF);
        ctrlData[3] = static_cast<char>(address & 0xFF);
        if (m_transport->sendFeatureReport(reinterpret_cast<uint8_t*>(ctrlData.data()), ctrlData.size())) {
            if (m_transport->getFeatureReport(reinterpret_cast<uint8_t*>(result.data()), result.size())) {
                readResult[0] = result[4];
                success = true;
            }
        }
    }

    if (!wasOpen) m_transport->close();

    if (!success) {
        qCWarning(log_chip) << "MS2109S all read attempts failed addr=0x" << QString::number(address, 16);
        return qMakePair(QByteArray(4, 0), false);
    }
    QByteArray finalResult(4, 0);
    finalResult[0] = readResult[0];
    return qMakePair(finalResult, true);
}

bool Ms2109sChip::write4Byte(quint16 address, const QByteArray& data)
{
    if (!m_transport) return false;
    QByteArray ctrlData(9, 0);
    ctrlData[0] = 0x00;
    ctrlData[1] = MS2109S_CMD_XDATA_WRITE;
    ctrlData[2] = static_cast<char>((address >> 8) & 0xFF);
    ctrlData[3] = static_cast<char>(address & 0xFF);
    ctrlData.replace(4, qMin(4, data.size()), data.left(4));
    return m_transport->sendFeatureReport(reinterpret_cast<uint8_t*>(ctrlData.data()), ctrlData.size());
}

// ══════════════════════════════════════════════════════════════
//  Ms2130sChip — read / write
// ══════════════════════════════════════════════════════════════

QPair<QByteArray, bool> Ms2130sChip::read4Byte(quint16 address)
{
    if (!m_transport) return qMakePair(QByteArray(4, 0), false);

    // Flash guard: bail while SPI flash is being written to avoid bus contention
    if (flashInProgress.load(std::memory_order_acquire))
        return qMakePair(QByteArray(1, 0), false);

    bool wasOpen = m_transport->isOpen();
    if (!wasOpen) {
        if (!m_transport->open()) {
            qCWarning(log_chip) << "MS2130S read: failed to open transport";
            return qMakePair(QByteArray(4, 0), false);
        }
    }

    QByteArray readResult(1, 0);
    bool success = false;

    qCDebug(log_chip) << "MS2130S reading addr=0x" << QString::number(address, 16).rightJustified(4, '0').toUpper();

    // Strategy 1: 11-byte buffer, report ID 1
    if (!success) {
        QByteArray ctrlData(11, 0);
        ctrlData[0] = 0x01;
        ctrlData[1] = MS2130S_CMD_XDATA_READ;
        ctrlData[2] = static_cast<char>((address >> 8) & 0xFF);
        ctrlData[3] = static_cast<char>(address & 0xFF);

#ifdef _WIN32
        uint8_t buffer[11] = {};
        memcpy(buffer, ctrlData.data(), 11);
        if (m_transport->sendDirect(buffer, 11)) {
            memset(buffer, 0, sizeof(buffer));
            buffer[0] = 0x01;
            if (m_transport->getDirect(buffer, 11)) {
                readResult[0] = buffer[4];
                success = true;
            }
        }
#else
        QByteArray result(11, 0);
        if (m_transport->sendFeatureReport(reinterpret_cast<uint8_t*>(ctrlData.data()), ctrlData.size())) {
            if (m_transport->getFeatureReport(reinterpret_cast<uint8_t*>(result.data()), result.size())) {
                readResult[0] = result[4];
                success = true;
            }
        }
#endif
    }

    // Strategy 2: 11-byte buffer, report ID 0
    if (!success) {
        QByteArray ctrlData(11, 0);
        ctrlData[0] = 0x00;
        ctrlData[1] = MS2130S_CMD_XDATA_READ;
        ctrlData[2] = static_cast<char>((address >> 8) & 0xFF);
        ctrlData[3] = static_cast<char>(address & 0xFF);

#ifdef _WIN32
        uint8_t buffer[11] = {};
        memcpy(buffer, ctrlData.data(), 11);
        if (m_transport->sendDirect(buffer, 11)) {
            memset(buffer, 0, sizeof(buffer));
            buffer[0] = 0x00;
            if (m_transport->getDirect(buffer, 11)) {
                readResult[0] = buffer[4];
                success = true;
            }
        }
        if (!success) {
            // last resort: flip to ID 1
            ctrlData[0] = 0x01;
            memcpy(buffer, ctrlData.data(), 11);
            if (m_transport->sendDirect(buffer, 11)) {
                memset(buffer, 0, sizeof(buffer));
                buffer[0] = 0x01;
                if (m_transport->getDirect(buffer, 11)) {
                    readResult[0] = buffer[4];
                    success = true;
                }
            }
        }
#else
        QByteArray result(11, 0);
        if (m_transport->sendFeatureReport(reinterpret_cast<uint8_t*>(ctrlData.data()), ctrlData.size())) {
            if (m_transport->getFeatureReport(reinterpret_cast<uint8_t*>(result.data()), result.size())) {
                readResult[0] = result[4];
                success = true;
            }
        }
        if (!success) {
            ctrlData[0] = 0x01;
            QByteArray result2(11, 0);
            if (m_transport->sendFeatureReport(reinterpret_cast<uint8_t*>(ctrlData.data()), ctrlData.size())) {
                if (m_transport->getFeatureReport(reinterpret_cast<uint8_t*>(result2.data()), result2.size())) {
                    readResult[0] = result2[4];
                    success = true;
                }
            }
        }
#endif
    }

    // Strategy 3: 65-byte fallback
    if (!success) {
        QByteArray ctrlData(65, 0);
        QByteArray result(65, 0);
        ctrlData[0] = 0x00;
        ctrlData[1] = MS2130S_CMD_XDATA_READ;
        ctrlData[2] = static_cast<char>((address >> 8) & 0xFF);
        ctrlData[3] = static_cast<char>(address & 0xFF);
        if (m_transport->sendFeatureReport(reinterpret_cast<uint8_t*>(ctrlData.data()), ctrlData.size())) {
            if (m_transport->getFeatureReport(reinterpret_cast<uint8_t*>(result.data()), result.size())) {
                readResult[0] = result[4];
                success = true;
            }
        }
    }

    if (!wasOpen) m_transport->close();

    if (!success) {
        qCWarning(log_chip) << "MS2130S all read attempts failed addr=0x" << QString::number(address, 16);
        return qMakePair(QByteArray(4, 0), false);
    }
    QByteArray finalResult(4, 0);
    finalResult[0] = readResult[0];
    return qMakePair(finalResult, true);
}

bool Ms2130sChip::write4Byte(quint16 address, const QByteArray& data)
{
    if (!m_transport) return false;

    // MS2130S: 11-byte packet, report ID 1 preferred
    QByteArray ctrlData(11, 0);
    ctrlData[0] = 0x01;
    ctrlData[1] = MS2130S_CMD_XDATA_WRITE;
    ctrlData[2] = static_cast<char>((address >> 8) & 0xFF);
    ctrlData[3] = static_cast<char>(address & 0xFF);
    ctrlData.replace(4, qMin(4, data.size()), data.left(4));

    bool result = m_transport->sendFeatureReport(reinterpret_cast<uint8_t*>(ctrlData.data()), ctrlData.size());
    if (!result) {
        // Try larger buffer
        QByteArray altCtrl(65, 0);
        altCtrl[0] = 0x00;
        altCtrl[1] = MS2130S_CMD_XDATA_WRITE;
        altCtrl[2] = static_cast<char>((address >> 8) & 0xFF);
        altCtrl[3] = static_cast<char>(address & 0xFF);
        altCtrl.replace(4, qMin(4, data.size()), data.left(4));
        result = m_transport->sendFeatureReport(reinterpret_cast<uint8_t*>(altCtrl.data()), altCtrl.size());
    }
    return result;
}

// ══════════════════════════════════════════════════════════════
//  Ms2130sChip — SPI flash driver
//  All I/O goes through m_transport (IHIDTransport).
// ══════════════════════════════════════════════════════════════

int Ms2130sChip::detectConnectMode()
{
    if (!m_transport) return 0;

    // sendDirect/getDirect are single-shot HID calls with no retry wrappers.
    // On Windows, WindowsHIDTransport::sendDirect/getDirect call HidD_SetFeature/HidD_GetFeature.
    // On Linux, they call sendFeatureReport/getFeatureReport (same implementation).
    uint8_t setData[9] = {0x00, 0xB5, 0xFF, 0x01, 0, 0, 0, 0, 0};
    uint8_t getData[9] = {0};

    if (m_transport->sendDirect(setData, 9)) {
        setData[1] = 0xFC; setData[2] = 0x01;
        if (m_transport->sendDirect(setData, 9)) {
            QThread::msleep(100);
            setData[0] = 0x00; setData[1] = 0xB5; setData[2] = 0xFF; setData[3] = 0x01;
            if (m_transport->sendDirect(setData, 9)) {
                getData[0] = 0x00;
                if (m_transport->getDirect(getData, 9)) {
                    QString hexRecv;
                    for (int i = 0; i < 9; ++i)
                        hexRecv += QString("%1 ").arg(getData[i], 2, 16, QChar('0'));
                    qCInfo(log_chip) << "MS2130S connect mode V1 probe recv =" << hexRecv.trimmed();
                    if (getData[4] == 0x13) {
                        qCInfo(log_chip) << "MS2130S connect mode: V1";
                        return 1;
                    }
                }
            }
        }
    } else {
        qCDebug(log_chip) << "MS2130S V1 probe sendDirect failed, falling back to V2/V3";
    }

    memset(setData, 0, sizeof(setData));
    memset(getData, 0, sizeof(getData));
    setData[0] = 0x01; setData[1] = 0xB5; setData[2] = 0xFF; setData[3] = 0x01;
    if (!m_transport->sendDirect(setData, 9)) {
        qCWarning(log_chip) << "MS2130S connect mode: V2/V3 sendDirect failed";
        return 0;
    }
    getData[0] = 0x01;
    if (!m_transport->getDirect(getData, 9)) {
        qCWarning(log_chip) << "MS2130S connect mode: V2/V3 getDirect failed";
        return 0;
    }
    QString hexSent, hexRecv;
    for (int i = 0; i < 9; ++i) {
        hexSent += QString("%1 ").arg(setData[i], 2, 16, QChar('0'));
        hexRecv += QString("%1 ").arg(getData[i], 2, 16, QChar('0'));
    }
    qCInfo(log_chip) << "MS2130S connect mode V2/V3 probe sent =" << hexSent.trimmed();
    qCInfo(log_chip) << "MS2130S connect mode V2/V3 probe recv =" << hexRecv.trimmed();
    for (int i = 0; i < 4; ++i) {
        if (setData[i] != getData[i]) {
            qCInfo(log_chip) << "MS2130S connect mode: V2 (byte" << i << "differs)";
            return 2;
        }
    }
    qCInfo(log_chip) << "MS2130S connect mode: V3 (bytes 0-3 match)";
    return 3;
}

bool Ms2130sChip::initializeGPIO()
{
    if (!m_transport) return false;

    qCDebug(log_chip) << "MS2130S initializing GPIO for flash operations...";

    if (connectMode == 0) {
        connectMode = detectConnectMode();
        if (connectMode == 0) {
            qCWarning(log_chip) << "MS2130S connect mode detection failed";
            return false;
        }
    }
    const int mode = connectMode;
    const quint8 reportId     = (mode == 1) ? 0x00 : 0x01;
    const int    read8_offset  = (mode == 2) ? 2    : 3;
    const int    read16_offset = (mode == 2) ? 3    : 4;

    auto write8 = [&](quint8 addr, quint8 value) -> bool {
        uint8_t buf[9] = {reportId, 0xC6, addr, value, 0, 0, 0, 0, 0};
        bool ok = m_transport->sendDirect(buf, 9);
        if (!ok) qCWarning(log_chip) << "MS2130S GPIO write8 failed: addr="
            << QString("0x%1").arg(addr, 2, 16, QChar('0'));
        return ok;
    };
    auto read8 = [&](quint8 addr, quint8 &value) -> bool {
        uint8_t s[9] = {reportId, 0xC5, addr, 0, 0, 0, 0, 0, 0};
        if (!m_transport->sendDirect(s, 9)) {
            qCWarning(log_chip) << "MS2130S GPIO read8 send failed: addr="
                << QString("0x%1").arg(addr, 2, 16, QChar('0'));
            return false;
        }
        uint8_t g[9] = {}; g[0] = reportId;
        if (!m_transport->getDirect(g, 9)) {
            qCWarning(log_chip) << "MS2130S GPIO read8 get failed: addr="
                << QString("0x%1").arg(addr, 2, 16, QChar('0'));
            return false;
        }
        value = g[read8_offset];
        return true;
    };
    auto write16 = [&](quint16 addr, quint8 value) -> bool {
        uint8_t buf[9] = {reportId, 0xB6, static_cast<uint8_t>((addr>>8)&0xFF),
                          static_cast<uint8_t>(addr&0xFF), value, 0, 0, 0, 0};
        bool ok = m_transport->sendDirect(buf, 9);
        if (!ok) qCWarning(log_chip) << "MS2130S GPIO write16 failed: addr="
            << QString("0x%1").arg(addr, 4, 16, QChar('0'));
        return ok;
    };
    auto read16 = [&](quint16 addr, quint8 &value) -> bool {
        uint8_t s[9] = {reportId, 0xB5, static_cast<uint8_t>((addr>>8)&0xFF),
                        static_cast<uint8_t>(addr&0xFF), 0, 0, 0, 0, 0};
        if (!m_transport->sendDirect(s, 9)) {
            qCWarning(log_chip) << "MS2130S GPIO read16 send failed: addr="
                << QString("0x%1").arg(addr, 4, 16, QChar('0'));
            return false;
        }
        uint8_t g[9] = {}; g[0] = reportId;
        if (!m_transport->getDirect(g, 9)) {
            qCWarning(log_chip) << "MS2130S GPIO read16 get failed: addr="
                << QString("0x%1").arg(addr, 4, 16, QChar('0'));
            return false;
        }
        value = g[read16_offset];
        return true;
    };

    if (!read8(0xB0, gpio_saved_b0))  { qCWarning(log_chip) << "MS2130S GPIO init: failed read 0xB0"; return false; }
    if (!write8(0xB0, gpio_saved_b0 & static_cast<quint8>(~0x04))) { qCWarning(log_chip) << "MS2130S GPIO init: failed write 0xB0"; return false; }
    if (!read8(0xA0, gpio_saved_a0))  { qCWarning(log_chip) << "MS2130S GPIO init: failed read 0xA0"; return false; }
    if (!write8(0xA0, gpio_saved_a0 | 0x04)) { qCWarning(log_chip) << "MS2130S GPIO init: failed write 0xA0"; return false; }
    read8(0xC7, gpio_saved_c7);
    if (!write8(0xC7, 0xD1)) { qCWarning(log_chip) << "MS2130S GPIO init: failed write 0xC7"; return false; }
    read8(0xC8, gpio_saved_c8);
    if (!write8(0xC8, 0xC0)) { qCWarning(log_chip) << "MS2130S GPIO init: failed write 0xC8"; return false; }
    read8(0xCA, gpio_saved_ca);
    if (!write8(0xCA, 0x00)) { qCWarning(log_chip) << "MS2130S GPIO init: failed write 0xCA"; return false; }
    if (!read16(0xF01F, gpio_saved_f01f)) { qCWarning(log_chip) << "MS2130S GPIO init: failed read 0xF01F"; return false; }
    quint8 f01fMod = (gpio_saved_f01f | 0x10) & static_cast<quint8>(~0x80);
    if (!write16(0xF01F, f01fMod)) { qCWarning(log_chip) << "MS2130S GPIO init: failed write 0xF01F"; return false; }

    gpioSaved = true;
    qCInfo(log_chip) << "MS2130S GPIO initialization completed successfully";
    return true;
}

void Ms2130sChip::restoreGPIO()
{
    if (!gpioSaved) {
        qCDebug(log_chip) << "MS2130S GPIO restore: nothing saved, skipping";
        return;
    }
    if (!m_transport) return;

    qCInfo(log_chip) << "MS2130S restoring GPIO registers to pre-flash values...";
    const int    mode     = connectMode;
    const quint8 reportId = (mode == 1) ? 0x00 : 0x01;

    auto write8 = [&](quint8 addr, quint8 val) {
        uint8_t b[9] = {reportId, 0xC6, addr, val, 0, 0, 0, 0, 0};
        m_transport->sendDirect(b, 9);
    };
    auto write16 = [&](quint16 addr, quint8 val) {
        uint8_t b[9] = {reportId, 0xB6, static_cast<uint8_t>((addr>>8)&0xFF),
                        static_cast<uint8_t>(addr&0xFF), val, 0, 0, 0, 0};
        m_transport->sendDirect(b, 9);
    };

    write16(0xF01F, gpio_saved_f01f);
    write8(0xCA, gpio_saved_ca);
    write8(0xC8, gpio_saved_c8);
    write8(0xC7, gpio_saved_c7);
    write8(0xA0, gpio_saved_a0);
    write8(0xB0, gpio_saved_b0);

    qCInfo(log_chip) << "MS2130S GPIO registers restored";
    gpioSaved = false;
}

bool Ms2130sChip::eraseSector(quint32 startAddress)
{
    if (!m_transport) return false;

    uint8_t ctrlData[9] = {0};
    ctrlData[0] = (connectMode == 1) ? 0x00 : 0x01;
    ctrlData[1] = 0xFB;
    ctrlData[2] = static_cast<uint8_t>((startAddress >> 16) & 0xFF);
    ctrlData[3] = static_cast<uint8_t>((startAddress >> 8) & 0xFF);
    ctrlData[4] = static_cast<uint8_t>(startAddress & 0xFF);

    if (!m_transport->sendDirect(ctrlData, 9)) {
        qCWarning(log_chip) << "MS2130S sector erase failed at"
            << QString("0x%1").arg(startAddress, 8, 16, QChar('0'));
        return false;
    }
    qCDebug(log_chip) << "MS2130S sector erase complete at"
        << QString("0x%1").arg(startAddress, 8, 16, QChar('0'));
    return true;
}

bool Ms2130sChip::flashEraseDone(bool &done)
{
    if (!m_transport) { done = false; return false; }

    bool openedForOp = m_transport->isOpen() || m_transport->open();
    if (!openedForOp) {
        qCWarning(log_chip) << "MS2130S erase-done: failed to open device handle";
        done = false;
        return true;
    }
    uint8_t ctrlData[9] = {0};
    ctrlData[0] = 0x01; ctrlData[1] = 0xFD; ctrlData[2] = 0xFD;
    if (!m_transport->sendDirect(ctrlData, 9)) {
        qCWarning(log_chip) << "MS2130S erase-done sendDirect failed";
        if (!m_transport->isOpen()) m_transport->close();
        done = false;
        return true;
    }
    uint8_t getData[9] = {0x01};
    if (!m_transport->getDirect(getData, 9)) {
        qCWarning(log_chip) << "MS2130S erase-done getDirect failed";
        if (!m_transport->isOpen()) m_transport->close();
        done = false;
        return true;
    }
    if (!m_transport->isOpen()) m_transport->close();
    quint8 statusByte = static_cast<quint8>(getData[2]);
    done = (statusByte == 0x00);
    qCDebug(log_chip) << "MS2130S erase-done status byte[2]:"
        << QString("0x%1").arg(statusByte, 2, 16, QChar('0')).toUpper() << "done=" << done;
    return true;
}

bool Ms2130sChip::flashGetSize(quint32 &flashSize)
{
    flashSize = 0;
    if (!m_transport) return false;

    // Command 0xF4: not only reads flash capacity but also triggers the chip's
    // internal state machine to switch from normal-communication mode into
    // Flash-programming mode.  Without this call, subsequent write commands
    // are silently ignored by the chip ("hidden handshake" from mshidlink).
    uint8_t ctrlData[9] = {0x01, 0xF4, 0, 0, 0, 0, 0, 0, 0};
    if (!m_transport->sendDirect(ctrlData, 9)) {
        qCWarning(log_chip) << "MS2130S flashGetSize: SetFeature 0xF4 failed";
        return false;
    }

    uint8_t getData[9] = {0x01, 0, 0, 0, 0, 0, 0, 0, 0};
    if (!m_transport->getDirect(getData, 9)) {
        qCWarning(log_chip) << "MS2130S flashGetSize: GetFeature 0xF4 failed";
        // The send succeeded, so the state machine switch likely happened;
        // treat as a soft failure – return false but the chip may still be ready.
        return false;
    }

    // Flash size is encoded in bytes [4..7] as a big-endian uint32 (in bytes).
    flashSize = (static_cast<quint32>(getData[4]) << 24)
              | (static_cast<quint32>(getData[5]) << 16)
              | (static_cast<quint32>(getData[6]) << 8)
              |  static_cast<quint32>(getData[7]);

    qCInfo(log_chip) << "MS2130S flashGetSize: flash size =" << flashSize
        << "bytes (" << (flashSize / 1024) << "KB); chip now in Flash-programming mode";
    return true;
}

bool Ms2130sChip::flashBurstWrite(quint32 address, const QByteArray &data)
{
    if (data.isEmpty() || !m_transport) return false;

    quint32 written_size = 0;

    if (connectMode == 1) {
        // V1: 0xF8 init + 6-byte data packets
        uint8_t ctrlData[9] = {0};
        ctrlData[0] = 0x00; ctrlData[1] = 0xF8; ctrlData[2] = 1;
        ctrlData[3] = static_cast<uint8_t>((address >> 16) & 0xFF);
        ctrlData[4] = static_cast<uint8_t>((address >> 8)  & 0xFF);
        ctrlData[5] = static_cast<uint8_t>(address & 0xFF);
        quint16 u16_length = static_cast<quint16>(data.size());
        ctrlData[6] = static_cast<uint8_t>((u16_length >> 8) & 0xFF);
        ctrlData[7] = static_cast<uint8_t>(u16_length & 0xFF);
        if (!m_transport->sendDirect(ctrlData, 9)) {
            qCWarning(log_chip) << "MS2130S V1 burst write init failed";
            return false;
        }
        for (quint16 offset = 0; offset < u16_length; offset = static_cast<quint16>(offset + 6)) {
            uint8_t packet[9] = {0};
            packet[0] = 0x00; packet[1] = 0xF8; packet[2] = 0;
            const quint16 chunkLen = static_cast<quint16>(qMin<int>(6, u16_length - offset));
            memcpy(&packet[3], data.constData() + offset, chunkLen);
            if (!m_transport->sendDirect(packet, 9)) {
                qCWarning(log_chip) << "MS2130S V1 burst write data packet failed at offset" << offset;
                return false;
            }
            written_size += chunkLen;
            if (onChunkWritten) onChunkWritten(written_size);
        }
        return true;
    }

    // V2/V3: 0xE7 init + 4096-byte 0x03 data packets
    uint8_t ctrlData[9] = {0};
    ctrlData[0] = 0x01; ctrlData[1] = 0xE7;
    ctrlData[2] = static_cast<uint8_t>((address >> 24) & 0xFF);
    ctrlData[3] = static_cast<uint8_t>((address >> 16) & 0xFF);
    ctrlData[4] = static_cast<uint8_t>((address >> 8)  & 0xFF);
    ctrlData[5] = static_cast<uint8_t>(address & 0xFF);
    quint16 u16_length = static_cast<quint16>(data.size());
    ctrlData[6] = static_cast<uint8_t>((u16_length >> 8) & 0xFF);
    ctrlData[7] = static_cast<uint8_t>(u16_length & 0xFF);
    {
        QString hexDump;
        for (int k = 0; k < 9; ++k) hexDump += QString("%1 ").arg(ctrlData[k], 2, 16, QChar('0'));
        qCInfo(log_chip) << "MS2130S burst write 0xE7 init packet:" << hexDump.trimmed()
            << "addr=" << QString("0x%1").arg(address, 6, 16, QChar('0')) << "len=" << u16_length;
    }
    // sendDirect tolerates ERROR_SEM_TIMEOUT (error 121) internally on Windows
    if (!m_transport->sendDirect(ctrlData, 9)) {
        qCWarning(log_chip) << "MS2130S burst write 0xE7 init failed";
        return false;
    }

    const quint16 u16_bufferSize = 4095;
    QByteArray buffer(4096, 0);
    buffer[0] = static_cast<char>(0x03);

    int cnt = u16_length / u16_bufferSize, left = u16_length % u16_bufferSize, j = 0;
    for (j = 0; j < cnt; ++j) {
        memcpy(buffer.data() + 1, data.constData() + static_cast<qsizetype>(u16_bufferSize) * j, u16_bufferSize);
        if (j == 0) {
            QString hexDump;
            for (int k = 0; k < 17; ++k) hexDump += QString("%1 ").arg(static_cast<uint8_t>(buffer[k]), 2, 16, QChar('0'));
            qCInfo(log_chip) << "MS2130S burst write first 0x03 packet (17 bytes):" << hexDump.trimmed();
        }
        if (!m_transport->sendDirect(reinterpret_cast<uint8_t*>(buffer.data()), u16_bufferSize + 1)) {
            qCWarning(log_chip) << "MS2130S burst write data chunk" << j << "failed";
            return false;
        }
        written_size += u16_bufferSize;
        if (onChunkWritten) onChunkWritten(written_size);
    }
    if (left != 0) {
        memcpy(buffer.data() + 1, data.constData() + static_cast<qsizetype>(u16_bufferSize) * j, left);
        if (!m_transport->sendDirect(reinterpret_cast<uint8_t*>(buffer.data()), u16_bufferSize + 1)) {
            qCWarning(log_chip) << "MS2130S burst write final chunk failed";
            return false;
        }
        written_size += left;
        if (onChunkWritten) onChunkWritten(written_size);
    }
    return true;
}

bool Ms2130sChip::flashBurstRead(quint32 address, quint32 length, QByteArray &outData)
{
    outData.clear();
    if (length == 0 || !m_transport) return (length == 0);

    uint8_t ctrlData[9] = {0};
    ctrlData[0] = 0x01; ctrlData[1] = 0xE7;
    ctrlData[2] = static_cast<uint8_t>((address >> 24) & 0xFF);
    ctrlData[3] = static_cast<uint8_t>((address >> 16) & 0xFF);
    ctrlData[4] = static_cast<uint8_t>((address >> 8)  & 0xFF);
    ctrlData[5] = static_cast<uint8_t>(address & 0xFF);
    quint16 u16_length = static_cast<quint16>(qMin(length, quint32(0xFFFF)));
    ctrlData[6] = static_cast<uint8_t>((u16_length >> 8) & 0xFF);
    ctrlData[7] = static_cast<uint8_t>(u16_length & 0xFF);
    if (!m_transport->sendDirect(ctrlData, 9)) {
        qCWarning(log_chip) << "MS2130S burst read 0xE7 init failed";
        return false;
    }

    const quint16 u16_bufferSize = 4095;
    QByteArray buffer(4096, 0);
    buffer[0] = static_cast<char>(0x03);
    outData.reserve(static_cast<int>(length));

    // Reference (MsHidLink.cpp mshidlink_flash_burst_read V2/V3 path):
    //   byte buffer[4096] = { 0x03 };
    //   ret = HidD_GetFeature(handle, buffer, 4095 + 1);
    //   if (ret) memcpy(out, &buffer[1], 4095);
    //
    // The reference NEVER checks buffer[0] after a successful call.
    // Windows HID driver may write the device's echoed report ID into buf[0]
    // (which may not be 0x03), so checking buffer[0] == 0x03 is wrong and
    // was the cause of all burst-read failures.  Data is always at buffer[1..].
    auto getPacket = [&]() -> bool {
        buffer[0] = static_cast<char>(0x03);  // report ID for burst-read data
        return m_transport->getDirect(reinterpret_cast<uint8_t*>(buffer.data()), 4096);
    };

    int cnt = u16_length / u16_bufferSize, left = u16_length % u16_bufferSize;
    for (int j = 0; j < cnt; ++j) {
        if (!getPacket()) {
            qCWarning(log_chip) << "MS2130S burst read chunk" << j << "/" << cnt << "failed";
            return false;
        }
        if (j == 0) {
            QString hexDump;
            for (int k = 0; k < 17; ++k) hexDump += QString("%1 ").arg(static_cast<uint8_t>(buffer[k]), 2, 16, QChar('0'));
            qCInfo(log_chip) << "MS2130S burst read first packet (17 bytes):" << hexDump.trimmed()
                             << " (buf[0]=" << QString("0x%1").arg(static_cast<uint8_t>(buffer[0]), 2, 16, QChar('0')) << ")";
        }
        outData.append(buffer.constData() + 1, u16_bufferSize);
    }
    if (left != 0) {
        if (!getPacket()) {
            qCWarning(log_chip) << "MS2130S burst read final chunk failed";
            return false;
        }
        {
            QString hexDump;
            int dumpLen = qMin(17, left + 1);
            for (int k = 0; k < dumpLen; ++k) hexDump += QString("%1 ").arg(static_cast<uint8_t>(buffer[k]), 2, 16, QChar('0'));
            qCInfo(log_chip) << "MS2130S burst read final chunk (first bytes):" << hexDump.trimmed()
                             << " (buf[0]=" << QString("0x%1").arg(static_cast<uint8_t>(buffer[0]), 2, 16, QChar('0')) << ")";
        }
        outData.append(buffer.constData() + 1, left);
    }
    qCDebug(log_chip) << "MS2130S burst read complete:" << outData.size() << "bytes from"
        << QString("0x%1").arg(address, 8, 16, QChar('0'));
    return true;
}

bool Ms2130sChip::writeFirmware(quint16 address, const QByteArray &data)
{
    if (!m_transport) return false;

    if (!m_transport->open()) {
        qCWarning(log_chip) << "MS2130S could not begin transaction for firmware write";
        return false;
    }

    // Re-detect connect mode at start of every flash session.
    connectMode = 0;

    // Use the existing overlapped handle (GENERIC_READ + FILE_FLAG_OVERLAPPED).
    // The reference MsHidLink.cpp opens ONE handle with GENERIC_READ + FILE_FLAG_OVERLAPPED
    // and uses it for ALL operations (GPIO init, erase, burst write, burst read).
    // Previously we called reopenSync() which opened GENERIC_READ|WRITE with no
    // FILE_FLAG_OVERLAPPED; this caused HidD_GetFeature to fail for 4096-byte burst
    // read packets (asymmetric validation: SetFeature passes through unknown report IDs,
    // GetFeature validates them against the HID descriptor).
    qCInfo(log_chip) << "MS2130S flash flow: overlapped handle (matches reference MsHidLink), GPIO restore disabled, soft-reset if verify passes";

    bool ok = true;
    bool gpioInitialized = false;

    if (!initializeGPIO()) {
        qCWarning(log_chip) << "MS2130S GPIO initialization failed – aborting firmware write";
        ok = false;
    } else {
        gpioInitialized = true;
        qCInfo(log_chip) << "MS2130S detected connect mode for flash:" << connectMode;
    }

    if (ok && connectMode == 1) {
        qCWarning(log_chip) << "MS2130S V1 mode is not supported by current updater flow. Aborting.";
        ok = false;
    }

    // ── Hidden handshake (equivalent to mshidlink_flash_get_size) ──────────
    // Send command 0xF4 to switch the chip from normal-communication mode into
    // Flash-programming mode.  Without this step the chip silently ignores all
    // subsequent erase and write commands.
    if (ok) {
        quint32 detectedFlashSize = 0;
        if (!flashGetSize(detectedFlashSize)) {
            qCWarning(log_chip) << "MS2130S flashGetSize (0xF4 handshake) failed – write may not succeed";
            // Not fatal: proceed but warn.  Some firmware revisions do not echo
            // a valid size yet still enter programming mode after the command.
        } else {
            qCInfo(log_chip) << "MS2130S entered Flash-programming mode; detected flash size:"
                << detectedFlashSize << "bytes";
        }
    }

    if (ok) {
        int numSectors = (data.size() + 4095) / 4096;
        qCDebug(log_chip) << "MS2130S erasing" << numSectors << "sector(s)";
        QElapsedTimer eraseTimer;
        for (int i = 0; i < numSectors; ++i) {
            quint32 sectorAddr = static_cast<quint32>(address) + static_cast<quint32>(i) * 4096;
            eraseTimer.start();
            if (!eraseSector(sectorAddr)) {
                qCWarning(log_chip) << "MS2130S sector erase failed at" << QString("0x%1").arg(sectorAddr, 8, 16, QChar('0'));
                ok = false;
                break;
            }
            qCDebug(log_chip) << "MS2130S erased sector" << (i+1) << "/" << numSectors
                << "in" << eraseTimer.elapsed() << "ms";
        }
        if (ok && numSectors <= 15) {
            quint32 sector15Addr = static_cast<quint32>(address) + 15u * 4096;
            eraseSector(sector15Addr);
        }
    }

    quint32 written = 0;
    const quint32 totalSize  = static_cast<quint32>(data.size());
    const quint32 maxBurst   = (connectMode == 1) ? 0x1000u : (60u * 1024u);
    qCInfo(log_chip) << "MS2130S write mode" << connectMode << "max burst:" << maxBurst;

    while (written < totalSize && ok) {
        quint32 chunkAddr = address + written;
        quint32 toWrite   = qMin(maxBurst, totalSize - written);
        if (!flashBurstWrite(chunkAddr, data.mid(written, static_cast<int>(toWrite)))) {
            qCWarning(log_chip) << "MS2130S burst write failed at" << QString("0x%1").arg(chunkAddr, 8, 16, QChar('0'));
            ok = false;
        } else {
            written += toWrite;
        }
    }

    if (ok) {
        qCInfo(log_chip) << "MS2130S firmware write COMPLETED SUCCESSFULLY:" << totalSize << "bytes";
    } else {
        qCWarning(log_chip) << "MS2130S firmware write FAILED after" << written << "/" << totalSize << "bytes";
    }

    // ── Flash readback verification ────────────────────────────────────────────
    // Read back the first 4095 bytes to confirm the data landed in SPI flash.
    // IMPORTANT: must use a multiple of 4095 (one full HID packet worth) so the
    // 0xE7 init length matches what the device will actually DMA into the packet.
    // Using a small value like 64 causes the device to return garbage because
    // the GetFeature call requests 4096 bytes but the device was only told to
    // prepare 64; the extra bytes come from whatever is in the device's buffer.
    bool verifyPassed = false;
    if (ok) {
        // Wait for SPI flash to commit the last burst write before reading back.
        // The chip DMA's the USB buffer to SPI flash asynchronously; calling 0xE7 read
        // init immediately returns stale buffer data. 100 ms is enough for the last sector.
        QThread::msleep(100);
        QByteArray readback;
        constexpr quint32 kVerifyLen = 4095;  // one full HID packet (matches reference 0x1000 chunk read)
        bool rbOk = flashBurstRead(static_cast<quint32>(address), kVerifyLen, readback);
        auto toHex = [](const QByteArray &b, int n) -> QString {
            QString s;
            for (int i = 0; i < qMin(n, (int)b.size()); ++i)
                s += QString("%1 ").arg(static_cast<quint8>(b[i]), 2, 16, QChar('0'));
            return s.trimmed();
        };
        if (rbOk && readback.size() >= 16) {
            QByteArray expected = data.left(static_cast<int>(readback.size()));
            bool match = (readback == expected.left(readback.size()));
            qCInfo(log_chip)  << "MS2130S flash readback  (first 16 B):" << toHex(readback,  16);
            qCInfo(log_chip)  << "MS2130S firmware expected (first 16 B):" << toHex(expected, 16);
            if (match) {
                qCInfo(log_chip) << "MS2130S flash verify PASS – write data confirmed in SPI flash";
                verifyPassed = true;
            } else {
                qCWarning(log_chip) << "MS2130S flash verify FAIL – readback DOES NOT MATCH firmware!"
                                       " Possible causes: firmware file wrong format, write protocol bug.";
            }
        } else {
            qCWarning(log_chip) << "MS2130S flash readback: 0xE7 read returned no data"
                                   " (getDirect failed – see transport error log for Windows error code)";
        }
    }

    // GPIO restore intentionally disabled (volatile registers, power cycle resets them).
    (void)gpioInitialized;

    // ── Post-flash reset ──────────────────────────────────────────────────────
    //
    // Reference (MSFlashUpgradeToolDlg.cpp): two-stage soft reset for ALL modes
    // when verify succeeds:
    //   Stage 1: write FF11=0x00 (Run from ROM) + FFFF=0x01 (soft reset) → wait reconnect
    //   Stage 2: write FF11=0x01 (Run from RAM) + FFFF=0x01 (soft reset) → wait reconnect
    //
    // If verify failed, the reference shows an error and does NOT reset.
    // We mirror that: reset only when verifyPassed, otherwise tell user to power cycle.
    if (ok && verifyPassed) {
        // Determine report ID and read16 response offset for this connect mode.
        const quint8 reportId      = (connectMode == 1) ? 0x00 : 0x01;
        const int    flagReadOffset = (connectMode == 1) ? 3    : 4;   // V1=recvBuf[3], V3=recvBuf[4]

        auto write16reg = [&](quint16 addr, quint8 val) -> bool {
            uint8_t buf[9] = {reportId, 0xB6,
                              static_cast<uint8_t>((addr >> 8) & 0xFF),
                              static_cast<uint8_t>(addr & 0xFF),
                              val, 0, 0, 0, 0};
            bool r = m_transport->sendDirect(buf, 9);
            if (!r) qCWarning(log_chip) << "MS2130S write16reg FAILED addr="
                << QString("0x%1").arg(addr, 4, 16, QChar('0')) << "val=" << val;
            return r;
        };

        auto clearResetFlag = [&]() {
            uint8_t sendBuf[9] = {reportId, 0xB5, 0xFF, 0xFE, 0, 0, 0, 0, 0};
            uint8_t recvBuf[9] = {reportId, 0, 0, 0, 0, 0, 0, 0, 0};
            bool s = m_transport->sendDirect(sendBuf, 9);
            bool g = m_transport->getDirect(recvBuf, 9);
            if (!s || !g)
                qCWarning(log_chip) << "MS2130S clearResetFlag FAILED send=" << s << "get=" << g;
            else
                qCInfo(log_chip) << "MS2130S clearResetFlag OK  0xFFFE="
                    << QString("0x%1").arg(recvBuf[flagReadOffset], 2, 16, QChar('0'));
        };

        // Stage 1: Run from ROM (enter bootloader ROM)
        qCInfo(log_chip) << "MS2130S stage-1 soft reset (Run from ROM, mode=" << connectMode << ")...";
        clearResetFlag();
        write16reg(0xFF12, 0x00);  // Disable MCU write PRAM from ROM
        write16reg(0xFF11, 0x00);  // MCU run from ROM
        write16reg(0xFF13, 0x00);  // ys clk source select: 0=osc
        write16reg(0xFFFF, 0x01);  // soft reset
        qCInfo(log_chip) << "MS2130S stage-1 reset sent; waiting for reconnect...";
        m_transport->close();

        bool reconnected = false;
        for (int attempt = 0; attempt < 100; ++attempt) {
            QThread::msleep(100);
            if (m_transport->open()) { reconnected = true; break; }
        }
        if (reconnected) {
            // Stage 2: Run from RAM (bootloader copies app firmware from flash to RAM)
            qCInfo(log_chip) << "MS2130S stage-2 soft reset (Run from RAM)...";
            clearResetFlag();
            write16reg(0xFF12, 0x00);  // Disable MCU write PRAM from ROM
            write16reg(0xFF11, 0x01);  // MCU run from RAM
            write16reg(0xFF13, 0x00);  // ys clk source select: 0=osc
            write16reg(0xFFFF, 0x01);  // soft reset
            qCInfo(log_chip) << "MS2130S stage-2 reset sent. New firmware should now be active.";
        } else {
            qCWarning(log_chip) << "MS2130S stage-1 reconnect timed out. Power cycle the device.";
        }
    } else if (ok) {
        // Write succeeded but verify failed – we can't confirm the flash contents.
        // Tell user to power cycle; the chip's ROM bootloader will attempt to boot
        // from whatever is in flash.
        qCInfo(log_chip)
            << "MS2130S firmware write complete (verify skipped or failed)."
            << " *** Unplug and re-plug the USB cable to load the new firmware. ***";
    }

    // Close the transport so Windows HID driver does not error when the device
    // disappears off the USB bus after stage-2 reset.
    m_transport->close();

    if (ok) {
        qCInfo(log_chip) << "MS2130S firmware update complete – device is re-enumerating";
    }
    return ok;
}
