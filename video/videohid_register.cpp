#include "videohid.h"
#include "../global.h"
#include <QThread>
#include <QDebug>
#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(log_host_hid)

/*
Get the input resolution from capture card. 
*/
QPair<int, int> VideoHid::getResolution() {
    VideoHidResolutionInfo info = getInputStatus();
    normalizeResolution(info);
    quint32 width = info.width;
    quint32 height = info.height;
    
    qCDebug(log_host_hid) << "getResolution: Read values --> " << width << "x" << height;
    
    return qMakePair(static_cast<int>(width), static_cast<int>(height));
}

float VideoHid::getFps() {
    VideoHidResolutionInfo info = getInputStatus();
    normalizeResolution(info);
    float fps = info.fps;
    
    qCDebug(log_host_hid) << "getFps: Read FPS:" << fps;
    
    return fps;
}

/*
 * Address: 0xDF00 bit0 indicates the hard switch status,
 * true means switchable usb connects to the target,
 * false means switchable usb connects to the host
 */
bool VideoHid::getGpio0() {
    uint16_t gpio_addr = ADDR_GPIO0;
    // Prefer chip implementation when available
    if (m_chipImpl) gpio_addr = m_chipImpl->addrGpio0();

    bool result = readRegisterSafe(gpio_addr, 0, "gpio0") & 0x01;
    return result;
}

float VideoHid::getPixelclk() {
    VideoHidRegisterSet set = getRegisterSetForCurrentChip();
    qCDebug(log_host_hid) << "getPixelclk: Using registers from chip impl" << (m_chipImpl ? m_chipImpl->name() : QString("Unknown"));

    VideoHidResolutionInfo info = getInputStatus();
    normalizeResolution(info);
    qCDebug(log_host_hid) << "getPixelclk: Returning Pixel Clock=" << info.pixclk << "MHz (from registers)";
    return info.pixclk;
}


bool VideoHid::getSpdifout() {
    int bit = 1;
    int mask = 0xFE;  // Mask for potential future use
    uint16_t spdifout_addr = ADDR_SPDIFOUT;

    if (m_chipImpl) spdifout_addr = m_chipImpl->addrSpdifout();

    if (GlobalVar::instance().getCaptureCardFirmwareVersion() < "24081309") {
        qCDebug(log_host_hid)  << "Firmware version is less than 24081309";
        bit = 0x10;
        mask = 0xEF;
    }
    Q_UNUSED(mask)  // Suppress unused variable warning
    
    bool result = readRegisterSafe(spdifout_addr, 0, "spdifout") & bit;
    return result;
}

void VideoHid::switchToHost() {
    qCDebug(log_host_hid)  << "Switch to host";
    setSpdifout(false);
    GlobalVar::instance().setSwitchOnTarget(false);
    // if(eventCallback) eventCallback->onSwitchableUsbToggle(false);
}

void VideoHid::switchToTarget() {
    qCDebug(log_host_hid)  << "Switch to target";
    setSpdifout(true);
    GlobalVar::instance().setSwitchOnTarget(true);
    // if(eventCallback) eventCallback->onSwitchableUsbToggle(true);
}

/*
 * Address: 0xDF01 bitn indicates the soft switch status, the firmware before 24081309, it's bit5, after 24081309, it's bit0
 * true means switchable usb connects to the target,
 * false means switchable usb connects to the host
 */
void VideoHid::setSpdifout(bool enable) {
    int bit = 1;
    int mask = 0xFE;
    quint16 spdifout_addr = ADDR_SPDIFOUT;
    // Prefer chip implementation when available
    if (m_chipImpl) spdifout_addr = m_chipImpl->addrSpdifout();
    
    if (GlobalVar::instance().getCaptureCardFirmwareVersion() < "24081309") {
        qCDebug(log_host_hid)  << "Firmware version is less than 24081309";
        bit = 0x10;
        mask = 0xEF;
    }

    quint8 spdifout = readRegisterSafe(spdifout_addr, 0, "spdifout");
    if (enable) {
        spdifout |= bit;
    } else {
        spdifout &= mask;
    }
    QByteArray data(4, 0); // Create a 4-byte array initialized to zero
    data[0] = spdifout;
    if(writeRegisterSafe(spdifout_addr, data, "setSpdifout")){
        qCDebug(log_host_hid)  << "SPDIFOUT set successfully";
    }else{
        qCDebug(log_host_hid)  << "SPDIFOUT set failed";
    }
}

std::string VideoHid::getFirmwareVersion() {
    bool ok;
    int version_0 = 0, version_1 = 0, version_2 = 0, version_3 = 0;
    bool wasInTransaction = m_inTransaction;
    
    // Only begin transaction if not already in one
    if (!wasInTransaction && !beginTransaction()) {
        qCDebug(log_host_hid)  << "Failed to begin transaction for getFirmwareVersion";
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
        qCDebug(log_host_hid)  << "Exception occurred during firmware version read";
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
        qCDebug(log_host_hid) << "Using" << m_chipImpl->name() << "HDMI status register:" << QString::number(status_addr, 16);
    } else if (m_chipImpl && m_chipImpl->type() == VideoChipType::MS2109S) {
        // MS2109S uses its own HDMI status register
        status_addr = MS2109S_ADDR_HDMI_CONNECTION_STATUS;
        qCDebug(log_host_hid) << "Using" << m_chipImpl->name() << "HDMI status register (MS2109S):" << QString::number(status_addr, 16);
    } else {
        // Default to MS2109 register
        status_addr = ADDR_HDMI_CONNECTION_STATUS;
        qCDebug(log_host_hid) << "Using" << (m_chipImpl ? m_chipImpl->name() : QString("MS2109")) << "HDMI status register:" << QString::number(status_addr, 16);
    }
    
    QPair<QByteArray, bool> result = usbXdataRead4Byte(status_addr);
    
    if (!result.second || result.first.isEmpty()) {
        qCWarning(log_host_hid) << "Failed to read HDMI connection status from address:" << QString::number(status_addr, 16);
        return false;
    }
    
    bool connected = result.first.at(0) & 0x01;
    // qCDebug(log_host_hid) << "HDMI connected:" << connected << ", raw value:" << (int)result.first.at(0);
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
            qCWarning(log_host_hid) << "HID READ FAILED (tag:" << tag << ") from address:" << QString("0x%1").arg(addr, 4, 16, QChar('0'))
                                   << "result.second:" << result.second << "result.first.size():" << result.first.size();
        } else {
            qCWarning(log_host_hid) << "HID READ FAILED from address:" << QString("0x%1").arg(addr, 4, 16, QChar('0'))
                                   << "result.second:" << result.second << "result.first.size():" << result.first.size();
        }
        return defaultValue;
    }
    quint8 value = static_cast<quint8>(result.first.at(0));
    if (!tag.isEmpty()) {
        // qCDebug(log_host_hid) << "HID READ SUCCESS (tag:" << tag << ") from address:" << QString("0x%1").arg(addr, 4, 16, QChar('0'))
        //                       << "value:" << QString("0x%1").arg(value, 2, 16, QChar('0')) << "(" << value << ")";
    }
    return value;
}

bool VideoHid::writeRegisterSafe(quint16 addr, const QByteArray &data, const QString &tag) {
    bool result = usbXdataWrite4Byte(addr, data);
    if (!result) {
        if (!tag.isEmpty()) {
            qCWarning(log_host_hid) << "HID WRITE FAILED (tag:" << tag << ") to address:" << QString("0x%1").arg(addr, 4, 16, QChar('0'))
                                   << "data:" << data.toHex(' ').toUpper();
        } else {
            qCWarning(log_host_hid) << "HID WRITE FAILED to address:" << QString("0x%1").arg(addr, 4, 16, QChar('0'))
                                   << "data:" << data.toHex(' ').toUpper();
        }
    } else {
        if (!tag.isEmpty()) {
            // qCDebug(log_host_hid) << "HID WRITE SUCCESS (tag:" << tag << ") to address:" << QString("0x%1").arg(addr, 4, 16, QChar('0'))
            //                       << "data:" << data.toHex(' ').toUpper();
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
    qCDebug(log_host_hid)  << "isHardSwitchOnTarget" << isHardSwitchOnTarget << "currentSwitchOnTarget" << currentSwitchOnTarget;
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
    if (currentSwitchOnTarget) spdifout |= bit; else spdifout &= mask;
    QByteArray data(4, 0);
    data[0] = spdifout;
    writeRegisterSafe(spdif_addr, data, "handleSpdifToggle");
    isHardSwitchOnTarget = currentSwitchOnTarget;
}

void VideoHid::dispatchSwitchableUsbToggle(bool isToTarget) {
    if (eventCallback) {
        // eventCallback->onSwitchableUsbToggle(isToTarget);
    }
}

