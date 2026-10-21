#include "videohid.h"
#include "../global.h"
#include <QThread>
#include <QDebug>
#include "log/opflogging.h"

Q_DECLARE_LOGGING_CATEGORY(log_hid_detect)
Q_DECLARE_LOGGING_CATEGORY(log_hid_poll)
Q_DECLARE_LOGGING_CATEGORY(log_hid_firmware)
Q_DECLARE_LOGGING_CATEGORY(log_hid_device)

/*
Get the input resolution from capture card.
*/
QPair<int, int> VideoHid::getResolution() {
    VideoHidResolutionInfo info = getInputStatus();
    normalizeResolution(info);
    quint32 width = info.width;
    quint32 height = info.height;

    qCDebug(log_hid_poll) << "getResolution: Read values --> " << width << "x" << height;

    return qMakePair(static_cast<int>(width), static_cast<int>(height));
}

float VideoHid::getFps() {
    VideoHidResolutionInfo info = getInputStatus();
    normalizeResolution(info);
    float fps = info.fps;

    qCDebug(log_hid_poll) << "getFps: Read FPS:" << fps;

    return fps;
}

/*
 * Address: 0xDF00 bit0 indicates the hard switch status,
 * true means switchable usb connects to the target,
 * false means switchable usb connects to the host
 */
bool VideoHid::getGpio0() {
    // Read the actual GPIO0 register to detect physical switch position
    // bit0: 1 = switchable USB connects to target, 0 = connects to host
    quint16 gpio0_addr = m_chipImpl ? m_chipImpl->addrGpio0() : ADDR_GPIO0;
    quint8 gpio0 = readRegisterSafe(gpio0_addr, 0, "gpio0");
    bool isOnTarget = (gpio0 & 0x01) != 0;
    qCDebug(log_hid_poll) << "getGpio0: register=0x" << QString::number(gpio0, 16)
                          << "isOnTarget=" << isOnTarget;
    return isOnTarget;
}

float VideoHid::getPixelclk() {
    qCDebug(log_hid_poll) << "getPixelclk: Using registers from chip impl" << (m_chipImpl ? m_chipImpl->name() : QString("Unknown"));

    VideoHidResolutionInfo info = getInputStatus();
    normalizeResolution(info);
    qCDebug(log_hid_poll) << "getPixelclk: Returning Pixel Clock=" << info.pixclk << "MHz (from registers)";
    return info.pixclk;
}

bool VideoHid::getSpdifout() {
    int bit = 1;
    int mask = 0xFE;  // Mask for potential future use
    uint16_t spdifout_addr = ADDR_SPDIFOUT;

    if (m_chipImpl) spdifout_addr = m_chipImpl->addrSpdifout();

    if (GlobalVar::instance().getCaptureCardFirmwareVersion() < "24081309") {
        qCDebug(log_hid_firmware)  << "Firmware version is less than 24081309";
        bit = 0x10;
        mask = 0xEF;
    }
    Q_UNUSED(mask)  // Suppress unused variable warning

    bool result = readRegisterSafe(spdifout_addr, 0, "spdifout") & bit;
    return result;
}

void VideoHid::switchToHost() {
    // CH9329: USB switch is controlled via MS2109 SPDIFOUT register (HID)
    // CH32V208: Uses serial commands instead
    qCInfo(log_hid_device) << "Switch to host via HID (SPDIFOUT)...";

    // Check if HID device is ready
    if (!m_chipImpl) {
        qCWarning(log_hid_device) << "switchToHost FAILED: m_chipImpl is null - HID device not initialized";
        qCWarning(log_hid_device) << "  m_currentHIDDevicePath:" << m_currentHIDDevicePath;
        qCWarning(log_hid_device) << "  m_currentHIDPortChain:" << m_currentHIDPortChain;
        GlobalVar::instance().setSwitchOnTarget(false);
        return;
    }

    setSpdifout(false);  // false = host
    GlobalVar::instance().setSwitchOnTarget(false);
}

void VideoHid::switchToTarget() {
    // CH9329: USB switch is controlled via MS2109 SPDIFOUT register (HID)
    // CH32V208: Uses serial commands instead
    qCInfo(log_hid_device) << "Switch to target via HID (SPDIFOUT)...";

    // Check if HID device is ready
    if (!m_chipImpl) {
        qCWarning(log_hid_device) << "switchToTarget FAILED: m_chipImpl is null - HID device not initialized";
        qCWarning(log_hid_device) << "  m_currentHIDDevicePath:" << m_currentHIDDevicePath;
        qCWarning(log_hid_device) << "  m_currentHIDPortChain:" << m_currentHIDPortChain;
        GlobalVar::instance().setSwitchOnTarget(true);
        return;
    }

    setSpdifout(true);   // true = target
    GlobalVar::instance().setSwitchOnTarget(true);
}

/*
 * Address: 0xDF01 bitn indicates the soft switch status, the firmware before 24081309, it's bit5, after 24081309, it's bit0
 * true means switchable usb connects to the target,
 * false means switchable usb connects to the host
 */
void VideoHid::setSpdifout(bool enable) {
    // Verify HID device is ready
    if (!m_chipImpl) {
        qCWarning(log_hid_device) << "setSpdifout FAILED: m_chipImpl is null - cannot access HID registers";
        return;
    }

    int bit = 1;
    int mask = 0xFE;
    quint16 spdifout_addr = ADDR_SPDIFOUT;
    // Prefer chip implementation when available
    if (m_chipImpl) spdifout_addr = m_chipImpl->addrSpdifout();

    QString fwVersion = QString::fromStdString(GlobalVar::instance().getCaptureCardFirmwareVersion());

    // If firmware version is empty, read it now (needed for correct bit selection)
    if (fwVersion.isEmpty() || fwVersion == "00000000") {
        qCInfo(log_hid_device) << "setSpdifout: firmware version is empty, reading from device...";
        fwVersion = QString::fromStdString(getFirmwareVersion());
        if (!fwVersion.isEmpty() && fwVersion != "00000000") {
            GlobalVar::instance().setCaptureCardFirmwareVersion(fwVersion.toStdString());
            qCInfo(log_hid_device) << "setSpdifout: firmware version cached:" << fwVersion;
        } else {
            qCWarning(log_hid_device) << "setSpdifout: failed to read firmware version, using default bit0";
        }
    }

    qCInfo(log_hid_device) << "setSpdifout: enable=" << enable
                           << "addr=0x" << QString::number(spdifout_addr, 16)
                           << "firmware=" << fwVersion;

    if (fwVersion.isEmpty() || fwVersion == "00000000" || fwVersion < "24081309") {
        qCInfo(log_hid_firmware)  << "Firmware version is less than 24081309 (or unknown), using bit5";
        bit = 0x10;
        mask = 0xEF;
    } else {
        qCInfo(log_hid_firmware) << "Firmware version >= 24081309, using bit0";
    }

    qCInfo(log_hid_device) << "setSpdifout: bit=0x" << QString::number(bit, 16)
                           << "mask=0x" << QString::number(mask, 16);

    quint8 spdifout = readRegisterSafe(spdifout_addr, 0, "spdifout");
    qCInfo(log_hid_device) << "setSpdifout: current register value=0x" << QString::number(spdifout, 16);

    // Clear BOTH bit0 and bit5 first to avoid conflicts, then set the correct bit
    // This ensures clean state regardless of previous bit configuration
    spdifout &= 0xEE;  // Clear bit0 (0x01) and bit5 (0x10)

    if (enable) {
        spdifout |= bit;
    }
    qCInfo(log_hid_device) << "setSpdifout: new value=0x" << QString::number(spdifout, 16);

    QByteArray data(4, 0); // Create a 4-byte array initialized to zero
    data[0] = spdifout;
    if(writeRegisterSafe(spdifout_addr, data, "setSpdifout")){
        qCInfo(log_hid_device)  << "SPDIFOUT set successfully";
    }else{
        qCWarning(log_hid_device)  << "SPDIFOUT set FAILED - HID write failed";
    }

    // Verify the write
    quint8 verify = readRegisterSafe(spdifout_addr, 0, "spdifout_verify");
    qCInfo(log_hid_device) << "setSpdifout: verify read=0x" << QString::number(verify, 16)
                           << "(expected=0x" << QString::number(spdifout, 16) << ")";
}

std::string VideoHid::getFirmwareVersion() {
    bool ok;
    int version_0 = 0, version_1 = 0, version_2 = 0, version_3 = 0;
    bool wasInTransaction = m_inTransaction;
    
    // Only begin transaction if not already in one
    if (!wasInTransaction && !beginTransaction()) {
        qCDebug(log_hid_firmware)  << "Failed to begin transaction for getFirmwareVersion";
        return "00000000";
    }
    
    try {
        // Define register addresses based on chip implementation
        quint16 ver0_addr = m_chipImpl ? m_chipImpl->addrFirmwareVersion0() : ADDR_FIRMWARE_VERSION_0;
        quint16 ver1_addr = m_chipImpl ? m_chipImpl->addrFirmwareVersion1() : ADDR_FIRMWARE_VERSION_1;
        quint16 ver2_addr = m_chipImpl ? m_chipImpl->addrFirmwareVersion2() : ADDR_FIRMWARE_VERSION_2;
        quint16 ver3_addr = m_chipImpl ? m_chipImpl->addrFirmwareVersion3() : ADDR_FIRMWARE_VERSION_3;
        
        // Read all version bytes
        version_0 = usbXdataRead4Byte(ver0_addr).first.toHex().toInt(&ok, 16);
        version_1 = usbXdataRead4Byte(ver1_addr).first.toHex().toInt(&ok, 16);
        version_2 = usbXdataRead4Byte(ver2_addr).first.toHex().toInt(&ok, 16);
        version_3 = usbXdataRead4Byte(ver3_addr).first.toHex().toInt(&ok, 16);
    }
    catch (...) {
        qCDebug(log_hid_firmware)  << "Exception occurred during firmware version read";
    }
    
    // Only end the transaction if we started it
    if (!wasInTransaction) {
        endTransaction();
    }
    
    return QString("%1%2%3%4").arg(version_0, 2, 10, QChar('0'))
                              .arg(version_1, 2, 10, QChar('0'))
                              .arg(version_2, 2, 10, QChar('0'))
                              .arg(version_3, 2, 10, QChar('0')).toStdString();
}

// pickFirmwareFileNameFromIndex �?static helper, delegates to FirmwareNetworkClient (Phase 4).
QString VideoHid::pickFirmwareFileNameFromIndex(const QString& indexContent, VideoChipType chip)
{
    return FirmwareNetworkClient::pickFirmwareFileNameFromIndex(indexContent, chip);
}

/*
 * Address: 0xFA8C bit0 indicates the HDMI connection status
 */
bool VideoHid::isHdmiConnected() {
    uint16_t status_addr;

    // Use the appropriate register based on chip implementation
    if (m_chipImpl && m_chipImpl->type() == VideoChipType::MS2130S) {
        status_addr = MS2130S_ADDR_HDMI_CONNECTION_STATUS;
        qCDebug(log_hid_poll) << "Using" << m_chipImpl->name() << "HDMI status register:" << QString::number(status_addr, 16);
    } else if (m_chipImpl && m_chipImpl->type() == VideoChipType::MS2109S) {
        // MS2109S uses its own HDMI status register
        status_addr = MS2109S_ADDR_HDMI_CONNECTION_STATUS;
        qCDebug(log_hid_poll) << "Using" << m_chipImpl->name() << "HDMI status register (MS2109S):" << QString::number(status_addr, 16);
    } else {
        // Default to MS2109 register
        status_addr = ADDR_HDMI_CONNECTION_STATUS;
        qCDebug(log_hid_poll) << "Using" << (m_chipImpl ? m_chipImpl->name() : QString("MS2109")) << "HDMI status register:" << QString::number(status_addr, 16);
    }

    QPair<QByteArray, bool> result = usbXdataRead4Byte(status_addr);

    if (!result.second || result.first.isEmpty()) {
        qCWarning(log_hid_poll) << "Failed to read HDMI connection status from address:" << QString::number(status_addr, 16);
        return false;
    }

    bool connected = result.first.at(0) & 0x01;
    // qCDebug(log_hid_poll) << "HDMI connected:" << connected << ", raw value:" << (int)result.first.at(0);
    return connected;
}

// Get register set for current chip
VideoHidRegisterSet VideoHid::getRegisterSetForCurrentChip() const {
    if (m_chipImpl) return m_chipImpl->getRegisterSet();
    // Fallback to MS2109 default registers
    VideoHidRegisterSet rs;
    rs.width_h = ADDR_INPUT_WIDTH_H;
    rs.width_l = ADDR_INPUT_WIDTH_L;
    rs.height_h = ADDR_INPUT_HEIGHT_H;
    rs.height_l = ADDR_INPUT_HEIGHT_L;
    rs.fps_h = ADDR_INPUT_FPS_H;
    rs.fps_l = ADDR_INPUT_FPS_L;
    rs.clk_h = ADDR_INPUT_PIXELCLK_H;
    rs.clk_l = ADDR_INPUT_PIXELCLK_L;
    return rs;
}

// Safe single-byte register reader for reuse
quint8 VideoHid::readRegisterSafe(quint16 addr, quint8 defaultValue, const QString& tag) {
    auto result = usbXdataRead4Byte(addr);
    if (!result.second || result.first.isEmpty()) {
        if (!tag.isEmpty()) {
            qCWarning(log_hid_poll) << "HID READ FAILED (tag:" << tag << ") from address:" << QString("0x%1").arg(addr, 4, 16, QChar('0'))
                                   << "result.second:" << result.second << "result.first.size():" << result.first.size();
        } else {
            qCWarning(log_hid_poll) << "HID READ FAILED from address:" << QString("0x%1").arg(addr, 4, 16, QChar('0'))
                                   << "result.second:" << result.second << "result.first.size():" << result.first.size();
        }
        return defaultValue;
    }
    quint8 value = static_cast<quint8>(result.first.at(0));
    return value;
}

bool VideoHid::writeRegisterSafe(quint16 addr, const QByteArray &data, const QString &tag) {
    bool result = usbXdataWrite4Byte(addr, data);
    if (!result) {
        if (!tag.isEmpty()) {
            qCWarning(log_hid_device) << "HID WRITE FAILED (tag:" << tag << ") to address:" << QString("0x%1").arg(addr, 4, 16, QChar('0'))
                                   << "data:" << data.toHex(' ').toUpper();
        } else {
            qCWarning(log_hid_device) << "HID WRITE FAILED to address:" << QString("0x%1").arg(addr, 4, 16, QChar('0'))
                                   << "data:" << data.toHex(' ').toUpper();
        }
    } else {
        if (!tag.isEmpty()) {
            qCDebug(log_hid_device) << "HID write success (tag:" << tag << ") to address:" << QString("0x%1").arg(addr, 4, 16, QChar('0'))
                                   << "data:" << data.toHex(' ').toUpper();
        }
    }
    return result;
}

// Read the full input status
VideoHidResolutionInfo VideoHid::getInputStatus() {
    VideoHidResolutionInfo info;
    info.hdmiConnected = isHdmiConnected();
    if (!info.hdmiConnected) return info;

    VideoHidRegisterSet set = getRegisterSetForCurrentChip();
    quint8 wh = readRegisterSafe(set.width_h, 0, "width_h");
    quint8 wl = readRegisterSafe(set.width_l, 0, "width_l");
    quint8 hh = readRegisterSafe(set.height_h, 0, "height_h");
    quint8 hl = readRegisterSafe(set.height_l, 0, "height_l");
    quint16 width = (static_cast<quint16>(wh) << 8) + static_cast<quint16>(wl);
    quint16 height = (static_cast<quint16>(hh) << 8) + static_cast<quint16>(hl);

    quint8 fh = readRegisterSafe(set.fps_h, 0, "fps_h");
    quint8 fl = readRegisterSafe(set.fps_l, 0, "fps_l");
    quint16 fps_raw = (static_cast<quint16>(fh) << 8) + static_cast<quint16>(fl);
    float fps = static_cast<float>(fps_raw) / 100.0f;

    quint8 ch = readRegisterSafe(set.clk_h, 0, "clk_h");
    quint8 cl = readRegisterSafe(set.clk_l, 0, "clk_l");
    quint16 clk_raw = (static_cast<quint16>(ch) << 8) + static_cast<quint16>(cl);
    float pixclk = static_cast<float>(clk_raw) / 100.0f;

    info.width = width;
    info.height = height;
    info.fps = fps;
    info.pixclk = pixclk;
    return info;
}

// Normalize resolution based on chip-specific quirks
void VideoHid::normalizeResolution(VideoHidResolutionInfo &info) {
    if (!info.hdmiConnected) return;
    if (m_chipImpl ? m_chipImpl->type() == VideoChipType::MS2109 : false) {
        if (info.pixclk > 189.0f) {
            if (info.width != 4096) info.width *= 2;
            if (info.height != 2160) info.height *= 2;
        }
    } else if (m_chipImpl ? m_chipImpl->type() == VideoChipType::MS2130S : false) {
        if (info.width == 3840 && info.height == 1080) {
            info.height = 2160;
        }
    }
}

// SPDIF toggle handling moved here from timer lambda
void VideoHid::handleSpdifToggle(bool currentSwitchOnTarget) {
    qCDebug(log_hid_device)  << "isHardSwitchOnTarget" << isHardSwitchOnTarget << "currentSwitchOnTarget" << currentSwitchOnTarget;
    if (eventCallback) {
        // Dispatch callback to the VideoHid object's thread (likely main thread) to ensure callbacks run on the UI/main thread
        QMetaObject::invokeMethod(this, "dispatchSwitchableUsbToggle", Qt::QueuedConnection,
                                  Q_ARG(bool, currentSwitchOnTarget));
    }

    int bit = 1;
    int mask = 0xFE;
    if (GlobalVar::instance().getCaptureCardFirmwareVersion() < "24081309") {
        bit = 0x10;
        mask = 0xEF;
    }

    quint16 spdif_addr = m_chipImpl ? m_chipImpl->addrSpdifout() : ADDR_SPDIFOUT;
    quint8 spdifout = readRegisterSafe(spdif_addr, 0, "spdifout");
    qCInfo(log_hid_device) << "handleSpdifToggle: current register value=0x" << QString::number(spdifout, 16)
                           << "target=" << (currentSwitchOnTarget ? "target" : "host");

    // Clear BOTH bit0 and bit5 first to avoid conflicts, then set the correct bit
    spdifout &= 0xEE;  // Clear bit0 (0x01) and bit5 (0x10)

    if (currentSwitchOnTarget) {
        spdifout |= bit;
    }
    qCInfo(log_hid_device) << "handleSpdifToggle: new value=0x" << QString::number(spdifout, 16)
                           << "bit=0x" << QString::number(bit, 16);

    QByteArray data(4, 0);
    data[0] = spdifout;
    writeRegisterSafe(spdif_addr, data, "handleSpdifToggle");
    isHardSwitchOnTarget = currentSwitchOnTarget;
}

void VideoHid::dispatchSwitchableUsbToggle(bool /*isToTarget*/) {
    if (eventCallback) {
        // eventCallback->onSwitchableUsbToggle(isToTarget);
    }
}

