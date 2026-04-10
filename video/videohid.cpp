#include "videohid.h"

#include <QDebug>
#include <QDir>
#include <QTimer>
#include <QElapsedTimer>
#include <atomic>
#include <QThread>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QEventLoop>
#include <QPointer>
#include <QtConcurrent>
#include "firmware/FirmwareNetworkClient.h"

#include "ms2109.h"
#include "ms2109s.h"
#include "ms2130s.h"
#include "firmwarewriter.h"
#include "firmwarereader.h"
#include "../global.h"
#include "../device/DeviceManager.h"
#include "../device/HotplugMonitor.h"
#include "../ui/globalsetting.h"
#include "../device/platform/AbstractPlatformDeviceManager.h"
#include "platformhidadapter.h"
#include "videohidchip.h"
#include "detection/ChipDetector.h"
#include "transport/WindowsHIDTransport.h"
#include "transport/LinuxHIDTransport.h"

Q_LOGGING_CATEGORY(log_host_hid, "opf.core.hid");

VideoHid::VideoHid(QObject *parent) : QObject(parent), m_inTransaction(false) {
    // Initialize current device tracking
    m_currentHIDDevicePath.clear();
    m_currentHIDPortChain.clear();
    
    // Connect to hotplug monitor for automatic device management
    connectToHotplugMonitor();

    // Create the platform-specific transport (owns device handle and all HID I/O).
#ifdef _WIN32
    m_deviceTransport = std::make_unique<WindowsHIDTransport>(this);
#elif __linux__
    m_deviceTransport = std::make_unique<LinuxHIDTransport>(this);
#endif

    // Create legacy platform adapter (delegates to platform_* which now delegate
    // to m_deviceTransport).  Kept for backward-compat with existing call sites.
    m_platformAdapter.reset(PlatformHidAdapter::create(this));
    if (m_platformAdapter) {
        qCDebug(log_host_hid) << "PlatformHidAdapter initialized. Initial HID path:"
                              << m_platformAdapter->getHIDDevicePath();
    }
}

VideoHid::~VideoHid() {
    // Fast exit if already cleaned up to prevent redundant work
    if (!m_pollingThread && !m_inTransaction) {
        return;
    }
    
    // Ensure we disconnect from hotplug monitor prior to stopping to avoid callbacks into a destroyed object
    disconnectFromHotplugMonitor();
    // Ensure we stop polling and close any open device before destruction
    stop();
}

// Nested PollingThread type definition for VideoHid
class VideoHid::PollingThread : public QThread {
public:
    PollingThread(VideoHid* owner, int intervalMs = 1000) : m_owner(owner), m_intervalMs(intervalMs), m_running(true) {}
    void run() override {
        qCDebug(log_host_hid) << "PollingThread started with interval (ms):" << m_intervalMs;
        while (m_running) {
            if (m_owner) {
                try {
                    m_owner->pollDeviceStatus();
                } catch (...) {
                    qCWarning(log_host_hid) << "Exception in PollingThread while calling pollDeviceStatus";
                }
            }
            // Use interruptible sleep for faster shutdown response
            int remainingSleep = m_intervalMs;
            const int sleepChunk = 100; // 100ms chunks
            while (m_running && remainingSleep > 0) {
                int actualSleep = qMin(sleepChunk, remainingSleep);
                QThread::msleep(actualSleep);
                remainingSleep -= actualSleep;
            }
        }
        qCDebug(log_host_hid) << "PollingThread stopping";
    }
    void stop() { m_running = false; }
private:
    VideoHid* m_owner{nullptr};
    int m_intervalMs{1000};
    std::atomic_bool m_running{true};
};

// Detect chipset type based on VID/PID from the HID device path.
void VideoHid::detectChipType() {
    const VideoChipType previousChipType = m_chipType;

    qCDebug(log_host_hid) << "Detecting chip type from device path:" << m_currentHIDDevicePath;
    qCDebug(log_host_hid) << "Current port chain:" << m_currentHIDPortChain;

    m_chipType = ChipDetector::detect(m_currentHIDDevicePath, m_currentHIDPortChain);

    // If detection fails, preserve the last known good chip type.
    if (m_chipType == VideoChipType::UNKNOWN && previousChipType != VideoChipType::UNKNOWN) {
        qCDebug(log_host_hid) << "ChipDetector returned UNKNOWN �?keeping previous chip type";
        m_chipType = previousChipType;
    }

    m_chipImpl = ChipDetector::createChip(m_chipType, this);

    // Wire the progress callback for Ms2130sChip firmware writes
    if (auto* ms2130s = dynamic_cast<Ms2130sChip*>(m_chipImpl.get())) {
        ms2130s->onChunkWritten = [this](quint32 n) {
            written_size = n;
            emit firmwareWriteChunkComplete(static_cast<int>(n));
        };
    }

    if (!m_chipImpl)
        qCDebug(log_host_hid) << "Unknown chipset �?no chip implementation created for:" << m_currentHIDDevicePath;

    if (previousChipType != m_chipType) {
        auto chipName = [](VideoChipType t) -> const char* {
            switch (t) {
            case VideoChipType::MS2109:  return "MS2109";
            case VideoChipType::MS2109S: return "MS2109S";
            case VideoChipType::MS2130S: return "MS2130S";
            default:                     return "Unknown";
            }
        };
        qCDebug(log_host_hid) << "Chip type changed from" << chipName(previousChipType)
                              << "to" << chipName(m_chipType);
    }
}

// Update the start method to keep HID device continuously open
void VideoHid::start() {
    qCDebug(log_host_hid) << "Starting VideoHid monitoring...";

    // Prevent multiple starts
    if (m_pollingThread) {
        qCDebug(log_host_hid) << "VideoHid already started, ignoring duplicate start call";
        return;
    }
    
    // Initialize current device tracking from global settings
    QString currentPortChain = GlobalSetting::instance().getOpenterfacePortChain();
    if (!currentPortChain.isEmpty()) {
        m_currentHIDPortChain = currentPortChain;
        QString hidPath = findMatchingHIDDevice(currentPortChain);
        
        // Set the current HID device path and detect chip type
        if (!hidPath.isEmpty()) {
            m_currentHIDDevicePath = hidPath;
            detectChipType();
        }
        if (!hidPath.isEmpty()) {
            m_currentHIDDevicePath = hidPath;
            qCDebug(log_host_hid) << "Initialized HID device with port chain:" << currentPortChain 
                                 << "device path:" << hidPath;
        }
    }
    
    if (!m_currentHIDDevicePath.isEmpty()) {
        // Do not block UI thread with firmware read/drivers in startup
        QtConcurrent::run([this]() {
            std::string captureCardFirmwareVersion = getFirmwareVersion();
            qCDebug(log_host_hid) << "Firmware VERSION async:" << QString::fromStdString(captureCardFirmwareVersion);
            GlobalVar::instance().setCaptureCardFirmwareVersion(captureCardFirmwareVersion);
            bool switchValue = getSpdifout();
            this->isHardSwitchOnTarget = switchValue;
            qCDebug(log_host_hid) << "SPDIFOUT async:" << switchValue;
            if (eventCallback) {
                setSpdifout(switchValue);
            }
        });
    } else {
        qCDebug(log_host_hid) << "No HID device path available at start; skipping immediate firmware/SPDIF load";
    }

    // Open HID device once and keep it open for continuous monitoring
    if (!beginTransaction()) {
        qCWarning(log_host_hid) << "Failed to open HID device for continuous monitoring";
        return;
    }

    // Add longer delay to allow device to fully stabilize after opening
    qCDebug(log_host_hid) << "Waiting for device stabilization before starting timer...";
    QThread::msleep(500);

    // Log the detected chip type
    qCDebug(log_host_hid) << "Starting polling thread with chip type:" << (m_chipImpl ? m_chipImpl->name() : QString("Unknown"));
    // Start the polling thread to get the HDMI connection status every m_pollIntervalMs
    m_pollingThread = new PollingThread(this, m_pollIntervalMs);
    m_pollingThread->start();
}

void VideoHid::stop() {
    qCDebug(log_host_hid)  << "Stopping VideoHid polling thread.";
    if (m_pollingThread) {
        m_pollingThread->stop();
        m_pollingThread->quit();
        if (!m_pollingThread->wait(2000)) {
            qCWarning(log_host_hid) << "Polling thread did not stop within 2 seconds, forcing termination";
            m_pollingThread->terminate();
            m_pollingThread->wait(1000);
        }
        delete m_pollingThread;
        m_pollingThread = nullptr;
    }
    
    // Close the HID device when stopping
    endTransaction();
    // Reset chip implementation and type to force re-detection on next start
    m_chipImpl.reset();
    m_chipType = VideoChipType::UNKNOWN;
    qCDebug(log_host_hid)  << "VideoHid stopped successfully and chip state cleared.";
}

void VideoHid::stopPollingOnly() {
    qCDebug(log_host_hid)  << "Stopping VideoHid polling thread but keeping HID connection for firmware update.";
    if (m_pollingThread) {
        qCDebug(log_host_hid) << "Signaling polling thread to stop...";
        m_pollingThread->stop();
        
        qCDebug(log_host_hid) << "Quitting polling thread...";
        m_pollingThread->quit();
        
        qCDebug(log_host_hid) << "Waiting for polling thread to finish...";
        if (!m_pollingThread->wait(3000)) {
            qCWarning(log_host_hid) << "Polling thread did not stop within 3 seconds, forcing termination";
            m_pollingThread->terminate();
            m_pollingThread->wait(1000);
        }
        
        qCDebug(log_host_hid) << "Deleting polling thread...";
        delete m_pollingThread;
        m_pollingThread = nullptr;
        qCDebug(log_host_hid) << "Polling thread deleted successfully";
    } else {
        qCDebug(log_host_hid) << "No polling thread to stop";
    }
    // Keep HID connection open for firmware update
    // Do NOT call endTransaction() or reset chip state
    qCDebug(log_host_hid)  << "VideoHid polling stopped, HID connection maintained for firmware update.";
}

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

// Poll device status previously implemented inline in the timer lambda
void VideoHid::pollDeviceStatus() {
    // Device is already open - no need for beginTransaction/endTransaction
    try {
        bool currentSwitchOnTarget = getGpio0();
        bool hdmiConnected = isHdmiConnected();
        // qCDebug(log_host_hid) << "chip type" << (m_chipImpl ? m_chipImpl->name() : QString("Unknown"));
        if (eventCallback) {
            VideoHidResolutionInfo info = getInputStatus();
            normalizeResolution(info);

            float aspectRatio = info.height ? static_cast<float>(info.width) / info.height : 0;
            GlobalVar::instance().setInputAspectRatio(aspectRatio);

            if (info.hdmiConnected) {
                if (GlobalVar::instance().getInputWidth() != static_cast<int>(info.width) || GlobalVar::instance().getInputHeight() != static_cast<int>(info.height)) {
                    qCDebug(log_host_hid).nospace() << "Resolution changed from "
                                         << GlobalVar::instance().getInputWidth() << "x" << GlobalVar::instance().getInputHeight()
                                         << " to " << info.width << "x" << info.height;
                    emit inputResolutionChanged(GlobalVar::instance().getInputWidth(), GlobalVar::instance().getInputHeight(), info.width, info.height);
                }
                qCDebug(log_host_hid) << "Emitting resolution update - Width:" << info.width << "Height:" << info.height << "FPS:" << info.fps << "PixelClock:" << info.pixclk << "MHz";
                emit resolutionChangeUpdate(static_cast<int>(info.width), static_cast<int>(info.height), info.fps, info.pixclk);
            } else {
                // qCDebug(log_host_hid) << "No HDMI connection detected, emitting zero resolution";
                emit resolutionChangeUpdate(0, 0, 0, 0);
            }

            if (m_chipImpl ? m_chipImpl->type() == VideoChipType::MS2109 : false){
                emit gpio0StatusChanged(currentSwitchOnTarget);
            }
            
        }
    }
    catch (...) {
        qCDebug(log_host_hid)  << "Exception occurred during timer processing";
    }
}

void VideoHid::setEventCallback(StatusEventCallback* callback) {
    eventCallback = callback;
}

// Slot: perform a write to EEPROM in the VideoHid thread context and return success
bool VideoHid::performWriteEeprom(quint16 address, const QByteArray &data)
{
    qCDebug(log_host_hid) << "performWriteEeprom called in thread:" << QThread::currentThread();
    return writeEeprom(address, data);
}

// Slot scheduled to run in VideoHid's thread when a device is unplugged
void VideoHid::handleScheduledDisconnect(const QString &oldPath)
{
    qCDebug(log_host_hid) << "handleScheduledDisconnect running in thread:" << QThread::currentThread();
    stop();
    emit hidDeviceDisconnected(oldPath);
}

// Slot scheduled to run in VideoHid's thread when a new device is plugged in
void VideoHid::handleScheduledConnect()
{
    qCDebug(log_host_hid) << "handleScheduledConnect running in thread:" << QThread::currentThread();
    // Add longer delay to allow device to fully stabilize after plugging in
    QThread::msleep(500);
    // Ensure chip detection and start happen on this object's thread
    detectChipType();
    qCInfo(log_host_hid) << "Verified chip type on new device: " << (m_chipImpl ? m_chipImpl->name() : QString("Unknown"));
    start();
    emit hidDeviceConnected(m_currentHIDDevicePath);
}

void VideoHid::clearDevicePathCache() {
    qCDebug(log_host_hid) << "Clearing HID device path cache";
    m_cachedDevicePath.clear();
    m_lastPathQuery = std::chrono::steady_clock::now() - std::chrono::seconds(11); // Force refresh on next call
}

void VideoHid::refreshHIDDevice() {
    qCDebug(log_host_hid) << "Refreshing HID device connection";

    // Clear cached device path to force re-discovery
    clearDevicePathCache();

    // If we're currently in a transaction, restart it with the new device
    bool wasInTransaction = m_inTransaction;
    if (wasInTransaction) {
        endTransaction();
        if (!beginTransaction()) {
            qCWarning(log_host_hid) << "Failed to restart HID transaction after refresh";
        }
    }
}

QPair<QByteArray, bool> VideoHid::usbXdataRead4Byte(quint16 u16_address) {
    // Bail immediately while firmware is being flashed to avoid USB bus contention.
    if (m_flashInProgress.load(std::memory_order_acquire)) {
        return qMakePair(QByteArray(1, 0), false);
    }
    // Different approaches for different chip types
    qCDebug(log_host_hid).nospace().noquote() << QString("usbXdataRead4Byte called for address: 0x%1 chip type: %2")
        .arg(QString::number(u16_address, 16).rightJustified(4, '0').toUpper())
        .arg(m_chipImpl ? m_chipImpl->name() : QString("Unknown"));

    QPair<QByteArray, bool> result;
    if (m_chipImpl) {
        result = m_chipImpl->read4Byte(u16_address);
    } else {
        return qMakePair(QByteArray(1, 0), false);
    }

    // Normalize the result to have the data byte at position 0
    if (result.second && !result.first.isEmpty()) {
        quint8 dataByte = result.first.at(0);
        return qMakePair(QByteArray(1, dataByte), true);
    } else {
        return qMakePair(QByteArray(1, 0), false);
    }
}

// Legacy per-chip read helpers (usbXdataRead4ByteMS2130S/MS2109/MS2109S) removed.
// Logic now lives in Ms2xxxChip::read4Byte().

#if 0  // BEGIN REMOVED LEGACY METHODS
QPair<QByteArray, bool> VideoHid::usbXdataRead4ByteMS2130S(quint16 u16_address) {
    // For MS2130S chip - use multiple approaches with strict error handling
    bool wasInTransaction = m_inTransaction;

    if (!wasInTransaction) {
        if (!beginTransaction()) {
            qCWarning(log_host_hid) << "Failed to begin transaction for MS2130S read";
            return qMakePair(QByteArray(4, 0), false);
        }
    }

    // Define several strategies with different buffer sizes and report IDs
    QByteArray readResult(1, 0);
    bool success = false;
    QString valueHex;

    qCDebug(log_host_hid) << "MS2130S reading from address:" << QString("0x%1").arg(u16_address, 4, 16, QChar('0'));

    // Strategy 1: Standard buffer (11 bytes) with report ID 1 (preferred for MS2130S)
    if (!success) {
        QByteArray ctrlData(11, 0);
        QByteArray result(11, 0);

        ctrlData[0] = 0x01;  // Report ID 1
        ctrlData[1] = MS2130S_CMD_XDATA_READ;
        ctrlData[2] = static_cast<char>((u16_address >> 8) & 0xFF);
        ctrlData[3] = static_cast<char>(u16_address & 0xFF);

        #ifdef _WIN32
        bool openedForOperation = m_inTransaction || openHIDDeviceHandle();
        if (openedForOperation) {
            BYTE buffer[11] = {0};
            memcpy(buffer, ctrlData.data(), 11);

            if (sendFeatureReportWindows(buffer, 11)) {
                memset(buffer, 0, sizeof(buffer));
                buffer[0] = 0x01;  // Report ID preserved

                if (getFeatureReportWindows(buffer, 11)) {
                    readResult[0] = buffer[4];
                    valueHex = QString("0x%1").arg((quint8)readResult[0], 2, 16, QChar('0'));
                    success = true;
                }
            }

            if (!m_inTransaction) {
                closeHIDDeviceHandle();
            }
        }
        #else
        if (this->sendFeatureReport((uint8_t*)ctrlData.data(), ctrlData.size())) {
            if (this->getFeatureReport((uint8_t*)result.data(), result.size())) {
                readResult[0] = result[4];
                valueHex = QString("0x%1").arg((quint8)readResult[0], 2, 16, QChar('0'));
                qCDebug(log_host_hid) << "MS2130S read success (ID 1) from address:"
                                     << QString("0x%1").arg(u16_address, 4, 16, QChar('0'))
                                     << "value:" << valueHex;
                success = true;
            }
        }
        #endif
    }

    // Strategy 2: Try with report ID 0 if strategy 1 failed
    if (!success) {
        QByteArray ctrlData(11, 0);
        QByteArray result(11, 0);

        ctrlData[0] = 0x00;  // Report ID 0
        ctrlData[1] = MS2130S_CMD_XDATA_READ;
        ctrlData[2] = static_cast<char>((u16_address >> 8) & 0xFF);
        ctrlData[3] = static_cast<char>(u16_address & 0xFF);
        
        #ifdef _WIN32
        bool openedForOperation = m_inTransaction || openHIDDeviceHandle();
        if (openedForOperation) {
            BYTE buffer[11] = {0};
            memcpy(buffer, ctrlData.data(), 11);
            
            if (sendFeatureReportWindows(buffer, 11)) {
                // Clear the receive buffer
                memset(buffer, 0, sizeof(buffer));
                buffer[0] = 0x00;  // Report ID 0
                
                if (getFeatureReportWindows(buffer, 11)) {
                    readResult[0] = buffer[4];
                    valueHex = QString("0x%1").arg((quint8)readResult[0], 2, 16, QChar('0'));
                    success = true;
                }
            }

            if (!success) {
                // If 0 failed, try with 1 again in case the device is in inconsistent report mode
                qCDebug(log_host_hid) << "MS2130S read through report ID 0 failed, trying report ID 1";
                memset(buffer, 0, sizeof(buffer));
                ctrlData[0] = 0x01;
                memcpy(buffer, ctrlData.data(), 11);

                if (sendFeatureReportWindows(buffer, 11)) {
                    memset(buffer, 0, sizeof(buffer));
                    buffer[0] = 0x01;
                    if (getFeatureReportWindows(buffer, 11)) {
                        readResult[0] = buffer[4];
                        valueHex = QString("0x%1").arg((quint8)readResult[0], 2, 16, QChar('0'));
                        success = true;
                    }
                }
            }
            
            if (!m_inTransaction) {
                closeHIDDeviceHandle();
            }
        }
        #else
        if (this->sendFeatureReport((uint8_t*)ctrlData.data(), ctrlData.size())) {
            if (this->getFeatureReport((uint8_t*)result.data(), result.size())) {
                readResult[0] = result[4];
                valueHex = QString("0x%1").arg((quint8)readResult[0], 2, 16, QChar('0'));
                qCDebug(log_host_hid) << "MS2130S read success (report ID 0) from address:" 
                                     << QString("0x%1").arg(u16_address, 4, 16, QChar('0')) 
                                     << "value:" << valueHex;
                success = true;
            }
        }

        if (!success) {
            // fallback to report ID 1
            ctrlData[0] = 0x01;
            if (this->sendFeatureReport((uint8_t*)ctrlData.data(), ctrlData.size())) {
                if (this->getFeatureReport((uint8_t*)result.data(), result.size())) {
                    readResult[0] = result[4];
                    valueHex = QString("0x%1").arg((quint8)readResult[0], 2, 16, QChar('0'));
                    qCDebug(log_host_hid) << "MS2130S read success (report ID 1 fallback) from address:" 
                                         << QString("0x%1").arg(u16_address, 4, 16, QChar('0')) 
                                         << "value:" << valueHex;
                    success = true;
                }
            }
        }
        #endif
    }
    
    // Strategy 3: Try with a standard 65-byte buffer as last resort
    if (!success) {
        QByteArray ctrlData(65, 0);
        QByteArray result(65, 0);
        
        ctrlData[0] = 0x00;  // Report ID
        ctrlData[1] = MS2130S_CMD_XDATA_READ;
        ctrlData[2] = static_cast<char>((u16_address >> 8) & 0xFF);
        ctrlData[3] = static_cast<char>(u16_address & 0xFF);
        
        if (this->sendFeatureReport((uint8_t*)ctrlData.data(), ctrlData.size())) {
            if (this->getFeatureReport((uint8_t*)result.data(), result.size())) {
                readResult[0] = result[4];
                valueHex = QString("0x%1").arg((quint8)readResult[0], 2, 16, QChar('0'));
                qCDebug(log_host_hid) << "MS2130S read success (large buffer) from address:" 
                                     << QString("0x%1").arg(u16_address, 4, 16, QChar('0')) 
                                     << "value:" << valueHex;
                success = true;
            }
        }
    }
    
    // End the transaction if we started it
    if (!wasInTransaction) {
        endTransaction();
    }
    
    if (!success) {
        qCWarning(log_host_hid) << "MS2130S all read attempts failed for address:" << QString("0x%1").arg(u16_address, 4, 16, QChar('0'));
        return qMakePair(QByteArray(4, 0), false);
    }
    
    // Create a 4-byte result for compatibility with existing code expecting that format
    QByteArray finalResult(4, 0);
    finalResult[0] = readResult[0];
    
    return qMakePair(finalResult, true);
}

QPair<QByteArray, bool> VideoHid::usbXdataRead4ByteMS2109(quint16 u16_address) {
    QByteArray ctrlData(9, 0); // Initialize with 9 bytes set to 0
    QByteArray result(9, 0);

    ctrlData[1] = CMD_XDATA_READ;
    ctrlData[2] = static_cast<char>((u16_address >> 8) & 0xFF);
    ctrlData[3] = static_cast<char>(u16_address & 0xFF);
    
    qCDebug(log_host_hid).nospace() << "MS2109 reading from address:" << QString("0x%1").arg(u16_address, 4, 16, QChar('0'));
    
    // 0: Some devices use report ID 0 to indicate that no specific report ID is used.
    if (this->sendFeatureReport((uint8_t*)ctrlData.data(), ctrlData.size())) {
        if (this->getFeatureReport((uint8_t*)result.data(), result.size())) {
            QByteArray readResult = result.mid(4, 1);
            qCDebug(log_host_hid).nospace() << "MS2109 read success from address:" << QString("0x%1").arg(u16_address, 4, 16, QChar('0')) 
                                 << "value:" << QString("0x%1").arg((quint8)readResult.at(0), 2, 16, QChar('0'));
            return qMakePair(readResult, true);
        }
    } else {
        // 1: Some devices use report ID 1 to indicate that no specific report ID is used.
        ctrlData[0] = 0x01;
        if (this->sendFeatureReport((uint8_t*)ctrlData.data(), ctrlData.size())) {
            bool getSuccess = this->getFeatureReport((uint8_t*)result.data(), result.size());

            if (getSuccess && !result.isEmpty()) {
                QByteArray readResult = result.mid(3, 4);
                qCDebug(log_host_hid) << "MS2109 read success (alt method) from address:" << QString("0x%1").arg(u16_address, 4, 16, QChar('0')) 
                                     << "value:" << readResult.toHex();
                return qMakePair(readResult, true);
            } else {
                qCWarning(log_host_hid) << "MS2109 failed to get feature report for address:" << QString("0x%1").arg(u16_address, 4, 16, QChar('0'));
            }
        } else {
            qCWarning(log_host_hid) << "Failed to send feature report (alt method) for address:" << QString("0x%1").arg(u16_address, 4, 16, QChar('0'));
        }
    }
    return qMakePair(QByteArray(4, 0), false); // Return 4 bytes set to 0 and false
}

// Similar read implementation for MS2109S chipset (matching behavior of MS2109)
QPair<QByteArray, bool> VideoHid::usbXdataRead4ByteMS2109S(quint16 u16_address) {
    bool wasInTransaction = m_inTransaction;
    if (!wasInTransaction) {
        if (!beginTransaction()) {
            qCWarning(log_host_hid) << "Failed to begin transaction for MS2109S read";
            return qMakePair(QByteArray(4, 0), false);
        }
    }

    // Define several strategies with different buffer sizes and report IDs
    QByteArray readResult(1, 0);
    bool success = false;
    QString valueHex;

    // qCDebug(log_host_hid) << "MS2109S reading from address:" << QString("0x%1").arg(u16_address, 4, 16, QChar('0'));

    // Strategy 1: Standard buffer (11 bytes) with report ID 0
    if (!success) {
        QByteArray ctrlData(11, 0);
        QByteArray result(11, 0);

        ctrlData[0] = 0x00;  // Report ID
        ctrlData[1] = MS2109S_CMD_XDATA_READ;
        ctrlData[2] = static_cast<char>((u16_address >> 8) & 0xFF);
        ctrlData[3] = static_cast<char>(u16_address & 0xFF);

        #ifdef _WIN32
        bool openedForOperation = m_inTransaction || openHIDDeviceHandle();
        if (openedForOperation) {
            BYTE buffer[11] = {0};
            memcpy(buffer, ctrlData.data(), 11);

            if (sendFeatureReportWindows(buffer, 11)) {
                // Clear the receive buffer
                memset(buffer, 0, sizeof(buffer));
                buffer[0] = 0x00;  // Report ID must be preserved

                if (getFeatureReportWindows(buffer, 11)) {
                    readResult[0] = buffer[4];
                    valueHex = QString("0x%1").arg((quint8)readResult[0], 2, 16, QChar('0'));
                    success = true;
                }
            }

            if (!m_inTransaction) {
                closeHIDDeviceHandle();
            }
        }
        #else
        // Standard approach for other platforms
        if (this->sendFeatureReport((uint8_t*)ctrlData.data(), ctrlData.size())) {
            if (this->getFeatureReport((uint8_t*)result.data(), result.size())) {
                readResult[0] = result[4];
                valueHex = QString("0x%1").arg((quint8)readResult[0], 2, 16, QChar('0'));
                qCDebug(log_host_hid) << "MS2109S read success (standard buffer) from address:" 
                                     << QString("0x%1").arg(u16_address, 4, 16, QChar('0')) 
                                     << "value:" << valueHex;
                success = true;
            }
        }
        #endif
    }

    // Strategy 2: Try with report ID 1 if strategy 1 failed
    if (!success) {
        QByteArray ctrlData(11, 0);
        QByteArray result(11, 0);

        ctrlData[0] = 0x01;  // Report ID 1
        ctrlData[1] = MS2109S_CMD_XDATA_READ;
        ctrlData[2] = static_cast<char>((u16_address >> 8) & 0xFF);
        ctrlData[3] = static_cast<char>(u16_address & 0xFF);

        #ifdef _WIN32
        bool openedForOperation = m_inTransaction || openHIDDeviceHandle();
        if (openedForOperation) {
            BYTE buffer[11] = {0};
            memcpy(buffer, ctrlData.data(), 11);

            if (sendFeatureReportWindows(buffer, 11)) {
                // Clear the receive buffer
                memset(buffer, 0, sizeof(buffer));
                buffer[0] = 0x01;  // Report ID must be preserved

                if (getFeatureReportWindows(buffer, 11)) {
                    readResult[0] = buffer[4];
                    valueHex = QString("0x%1").arg((quint8)readResult[0], 2, 16, QChar('0'));
                    success = true;
                }
            }

            if (!m_inTransaction) {
                closeHIDDeviceHandle();
            }
        }
        #else
        if (this->sendFeatureReport((uint8_t*)ctrlData.data(), ctrlData.size())) {
            if (this->getFeatureReport((uint8_t*)result.data(), result.size())) {
                readResult[0] = result[4];
                valueHex = QString("0x%1").arg((quint8)readResult[0], 2, 16, QChar('0'));
                qCDebug(log_host_hid) << "MS2109S read success (report ID 1) from address:" 
                                     << QString("0x%1").arg(u16_address, 4, 16, QChar('0')) 
                                     << "value:" << valueHex;
                success = true;
            }
        }
        #endif
    }

    // Strategy 3: Try with a standard 65-byte buffer as last resort
    if (!success) {
        QByteArray ctrlData(65, 0);
        QByteArray result(65, 0);

        ctrlData[0] = 0x00;  // Report ID
        ctrlData[1] = MS2109S_CMD_XDATA_READ;
        ctrlData[2] = static_cast<char>((u16_address >> 8) & 0xFF);
        ctrlData[3] = static_cast<char>(u16_address & 0xFF);

        if (this->sendFeatureReport((uint8_t*)ctrlData.data(), ctrlData.size())) {
            if (this->getFeatureReport((uint8_t*)result.data(), result.size())) {
                readResult[0] = result[4];
                valueHex = QString("0x%1").arg((quint8)readResult[0], 2, 16, QChar('0'));
                qCDebug(log_host_hid) << "MS2109S read success (large buffer) from address:" 
                                     << QString("0x%1").arg(u16_address, 4, 16, QChar('0')) 
                                     << "value:" << valueHex;
                success = true;
            }
        }
    }

    // End the transaction if we started it
    if (!wasInTransaction) {
        endTransaction();
    }

    if (!success) {
        qCWarning(log_host_hid) << "MS2109S all read attempts failed for address:" << QString("0x%1").arg(u16_address, 4, 16, QChar('0'));
        return qMakePair(QByteArray(4, 0), false);
    }

    // Create a 4-byte result for compatibility with existing code expecting that format
    QByteArray finalResult(4, 0);
    finalResult[0] = readResult[0];

    return qMakePair(finalResult, true);
}
#endif  // END REMOVED LEGACY METHODS

bool VideoHid::usbXdataWrite4Byte(quint16 u16_address, QByteArray data) {
    if (!m_chipImpl) return false;
    qCDebug(log_host_hid) << "Writing to address:" << QString("0x%1").arg(u16_address, 4, 16, QChar('0'))
        << "chip:" << m_chipImpl->name() << "data:" << data.toHex(' ').toUpper();
    return m_chipImpl->write4Byte(u16_address, data);
}

bool VideoHid::getFeatureReport(uint8_t* buffer, size_t bufferLength) {
    if (m_deviceTransport) return m_deviceTransport->getFeatureReport(buffer, bufferLength);
    return false;
}

bool VideoHid::sendFeatureReport(uint8_t* buffer, size_t bufferLength) {
    if (m_deviceTransport) return m_deviceTransport->sendFeatureReport(buffer, bufferLength);
    return false;
}

// IHIDTransport::open / close
bool VideoHid::open() {
    if (m_inTransaction) return true;
    return beginTransaction();
}

void VideoHid::close() {
    if (m_inTransaction) endTransaction();
}

// IHIDTransport::sendDirect / getDirect �?delegate to transport (single-shot, no retry)
bool VideoHid::sendDirect(uint8_t* buf, size_t len) {
    if (m_deviceTransport) return m_deviceTransport->sendDirect(buf, len);
    return false;
}

bool VideoHid::getDirect(uint8_t* buf, size_t len) {
    if (m_deviceTransport) return m_deviceTransport->getDirect(buf, len);
    return false;
}

// IHIDTransport::reopenSync �?delegate to transport (re-opens synchronous handle for flash)
bool VideoHid::reopenSync() {
    if (m_deviceTransport) return m_deviceTransport->reopenSync();
    return false;
}

void VideoHid::closeHIDDeviceHandle() {
    if (m_deviceTransport) m_deviceTransport->close();
}

// Platform wrapper implementations used by PlatformHidAdapter
bool VideoHid::platform_openDevice() {
    if (m_deviceTransport) return m_deviceTransport->open();
    return false;
}

void VideoHid::platform_closeDevice() {
    if (m_deviceTransport) m_deviceTransport->close();
}

bool VideoHid::platform_sendFeatureReport(uint8_t* reportBuffer, size_t bufferSize) {
    if (m_deviceTransport) return m_deviceTransport->sendFeatureReport(reportBuffer, bufferSize);
    return false;
}

bool VideoHid::platform_getFeatureReport(uint8_t* reportBuffer, size_t bufferSize) {
    if (m_deviceTransport) return m_deviceTransport->getFeatureReport(reportBuffer, bufferSize);
    return false;
}

QString VideoHid::platform_getHIDDevicePath() {
    if (m_deviceTransport) return m_deviceTransport->getHIDDevicePath();
    return {};
}

bool VideoHid::beginTransaction() {
    if (m_inTransaction) {
        qCDebug(log_host_hid)  << "Transaction already in progress";
        return true;  // Already in a transaction
    }
    
    // We'll retry a few times in case of temporary device issues
    const int MAX_RETRIES = 3;
    bool success = false;
    
    for (int attempt = 0; attempt < MAX_RETRIES && !success; attempt++) {
        if (attempt > 0) {
            qCDebug(log_host_hid) << "Retry attempt" << attempt << "to open HID device";
            // Wait a short time between retries
            QThread::msleep(100); 
        }
        
        #ifdef _WIN32
            success = m_deviceTransport ? m_deviceTransport->open() : false;
        #elif __linux__
            success = m_deviceTransport ? m_deviceTransport->open() : false;
        #else
            success = false;
        #endif
    }

    if (success) {
        m_inTransaction = true;
        
        // Detect chip type if it's unknown
        if (m_chipType == VideoChipType::UNKNOWN) {
            detectChipType();
            qCDebug(log_host_hid) << "Detected chip type:" << (m_chipImpl ? m_chipImpl->name() : QString("Unknown"));
            
            // Add stabilization delay for both chip types to ensure device is ready
            if (m_chipImpl && m_chipImpl->type() == VideoChipType::MS2130S) {
                qCDebug(log_host_hid) << "Performing MS2130S-specific initialization";
                QThread::msleep(100);
            } else if (m_chipImpl && m_chipImpl->type() == VideoChipType::MS2109) {
                qCDebug(log_host_hid) << "Performing MS2109-specific initialization";
                QThread::msleep(100);
            }
        }
        
        qCDebug(log_host_hid)  << "HID transaction started";
        return true;
    } else {
        qCWarning(log_host_hid)  << "Failed to start HID transaction after" << MAX_RETRIES << "attempts";
        return false;
    }
}

void VideoHid::endTransaction() {
    if (m_inTransaction) {
        // For MS2130S, perform any cleanup or specific finalization
        if (m_chipImpl && m_chipImpl->type() == VideoChipType::MS2130S) {
            qCDebug(log_host_hid) << "Performing MS2130S-specific cleanup before closing transaction";
            // Add a short delay to ensure any pending operations complete
            QThread::msleep(10);
        }
        
        closeHIDDeviceHandle();
        m_inTransaction = false;
        qCDebug(log_host_hid) << "HID transaction ended";
    }
}

bool VideoHid::isInTransaction() const {
    return m_inTransaction;
}

void VideoHid::connectToHotplugMonitor()
{
    qCDebug(log_host_hid) << "Connecting VideoHid to hotplug monitor";
    
    // Get the hotplug monitor from DeviceManager
    DeviceManager& deviceManager = DeviceManager::getInstance();
    HotplugMonitor* hotplugMonitor = deviceManager.getHotplugMonitor();
    
    if (!hotplugMonitor) {
        qCWarning(log_host_hid) << "Failed to get hotplug monitor from device manager";
        return;
    }
    
    // Connect to device unplugging signal
    connect(hotplugMonitor, &HotplugMonitor::deviceUnplugged,
            this, [this](const DeviceInfo& device) {
                qCDebug(log_host_hid) << "VideoHid: Attempting HID device deactivation for unplugged device port:" << device.portChain;
                
                // Only deactivate HID device if the device has a HID component
                if (!device.hasHidDevice()) {
                    qCDebug(log_host_hid) << "Device at port" << device.portChain << "has no HID component, skipping HID deactivation";
                    return;
                }
                
                // Check if the unplugged device matches the current HID device
                if (m_currentHIDPortChain == device.portChain) {
                    qCInfo(log_host_hid) << "Stopping HID device for unplugged device at port:" << device.portChain;
                    QString oldPath = m_currentHIDDevicePath;
                    // Defer stop() to avoid blocking the hotplug thread - use QPointer and invokeMethod to avoid acting on destroyed object
                    QPointer<VideoHid> safeThis(this);
                    QTimer::singleShot(0, [safeThis, oldPath]() {
                        if (!safeThis) return;
                        QMetaObject::invokeMethod(safeThis, "handleScheduledDisconnect", Qt::QueuedConnection, Q_ARG(QString, oldPath));
                    });
                    qCInfo(log_host_hid) << "�?HID device stop scheduled for unplugged device at port:" << device.portChain;
                } else {
                    qCDebug(log_host_hid) << "HID device deactivation skipped - port chain mismatch. Current:" << m_currentHIDPortChain << "Unplugged:" << device.portChain;
                }
            });
            
    // Connect to new device plugged in signal
    connect(hotplugMonitor, &HotplugMonitor::newDevicePluggedIn,
            this, [this](const DeviceInfo& device) {
                qCDebug(log_host_hid) << "VideoHid: Attempting HID device auto-switch for new device port:" << device.portChain;
                
                // Only attempt auto-switch if the device has a HID component
                if (!device.hasHidDevice()) {
                    qCDebug(log_host_hid) << "Device at port" << device.portChain << "has no HID component, skipping HID auto-switch";
                    return;
                }
                
                // Check if there's currently an active HID device
                if (isInTransaction()) {
                    qCDebug(log_host_hid) << "HID device already active, skipping auto-switch to port:" << device.portChain;
                    return;
                }
                
                qCDebug(log_host_hid) << "No active HID device found, attempting to switch to new device";
                
                // Switch to the new HID device
                bool switchSuccess = switchToHIDDeviceByPortChain(device.portChain);
                if (switchSuccess) {
                    qCInfo(log_host_hid) << "�?HID device auto-switched to new device at port:" << device.portChain;
                    
                    // Defer the start and stabilization to avoid blocking the hotplug thread - schedule safely
                    QPointer<VideoHid> safeThis(this);
                    QTimer::singleShot(0, [safeThis]() {
                        if (!safeThis) return;
                        QMetaObject::invokeMethod(safeThis, "handleScheduledConnect", Qt::QueuedConnection);
                    });
                } else {
                    qCDebug(log_host_hid) << "HID device auto-switch failed for port:" << device.portChain;
                }
            });
            
    qCDebug(log_host_hid) << "VideoHid successfully connected to hotplug monitor";
}

void VideoHid::disconnectFromHotplugMonitor()
{
    qCDebug(log_host_hid) << "Disconnecting VideoHid from hotplug monitor";
    
    // Get the hotplug monitor from DeviceManager
    DeviceManager& deviceManager = DeviceManager::getInstance();
    HotplugMonitor* hotplugMonitor = deviceManager.getHotplugMonitor();
    
    if (hotplugMonitor) {
        disconnect(hotplugMonitor, nullptr, this, nullptr);
        qCDebug(log_host_hid) << "VideoHid disconnected from hotplug monitor";
    }
}

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
            emit firmwareReadChunkComplete(read_size);
            return true;
        }
    }
    qWarning() << "Failed to read chunk from address:" << QString("0x%1").arg(address, 4, 16, QChar('0'));
    return false;
}

QByteArray VideoHid::readEeprom(quint16 address, quint32 size) {
    const int MAX_CHUNK = 1;
    const int MAX_RETRIES = 3; // Number of retries for failed reads
    QByteArray firmwareData;
    read_size = 0;

    // Begin transaction for the entire operation
    if (!beginTransaction()) {
        qCDebug(log_host_hid) << "Failed to begin transaction for EEPROM read";
        emit firmwareReadError("Failed to begin transaction for EEPROM read");
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
            emit firmwareReadProgress((read_size * 100) / size);
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
        emit firmwareReadError("Failed to read firmware from EEPROM");
        return QByteArray();
    }

    return firmwareData;
}

quint32 VideoHid::readFirmwareSize(){
    QByteArray header = readEeprom(ADDR_EEPROM, 4);
    if (header.size() != 4) {
        qDebug() << "Can not read firemware header form eeprom:" << header.size();
        emit firmwareReadError("Can not read firemware header form eeprom");
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


    connect(worker, &FirmwareReader::finished, this, [this](bool success) {
        if (success) {
            qCDebug(log_host_hid) << "Firmware read completed successfully";
            emit firmwareReadComplete(true);
        } else {
            qCDebug(log_host_hid) << "Firmware read failed - user should try again";
            emit firmwareReadComplete(false);

        }
    });
    
    thread->start();
}

QString VideoHid::findMatchingHIDDevice(const QString& portChain) const
{
    // Check if we have a cached path that's still valid
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - m_lastPathQuery).count();
    
#ifdef _WIN32
    if (!m_cachedDevicePath.empty() && elapsed < 10) {
        // Return cached path if it's less than 10 seconds old
        QString cachedPath = QString::fromStdWString(m_cachedDevicePath);
        qCDebug(log_host_hid) << "Using cached HID device path:" << cachedPath;
        return cachedPath;
    }
#elif __linux__
    if (!m_cachedDevicePath.isEmpty() && elapsed < 10) {
        // Return cached path if it's less than 10 seconds old
        qCDebug(log_host_hid) << "Using cached HID device path:" << m_cachedDevicePath;
        return m_cachedDevicePath;
    }
#endif

    // Update the last query time (need to cast away const since this is conceptually a const operation)
    const_cast<VideoHid*>(this)->m_lastPathQuery = now;

    if (portChain.isEmpty()) {
        qCDebug(log_host_hid) << "Empty port chain provided";
        return QString();
    }

    qCDebug(log_host_hid) << "Finding HID device matching port chain:" << portChain;

    // Use DeviceManager to look up device information by port chain
    DeviceManager& deviceManager = DeviceManager::getInstance();
    
    // Retry logic for HID device detection - HID devices may take time to enumerate after hotplug
    const int maxRetries = 3;
    const int retryDelayMs = 200; // 200ms delay between retries
    
    for (int retry = 0; retry < maxRetries; ++retry) {
        if (retry > 0) {
            qCDebug(log_host_hid) << "HID device not found, retrying in" << retryDelayMs << "ms (attempt" << (retry + 1) << "/" << maxRetries << ")";
            QThread::msleep(retryDelayMs);
            
            // Force fresh device discovery on retry
            deviceManager.discoverDevices();
        }
        
        QList<DeviceInfo> devices = deviceManager.getDevicesByPortChain(portChain);
        
        if (devices.isEmpty()) {
            qCWarning(log_host_hid) << "No devices found for port chain:" << portChain;
            continue; // Try again
        }

        qCDebug(log_host_hid) << "Found" << devices.size() << "device(s) for port chain:" << portChain;

        // Look for a device that has HID information
        DeviceInfo selectedDevice;
        for (const DeviceInfo& device : devices) {
            if (!device.hidDevicePath.isEmpty()) {
                selectedDevice = device;
                qCDebug(log_host_hid) << "Found device with HID info:" 
                         << "hidDevicePath:" << device.hidDevicePath;
                break;
            }
        }

        if (selectedDevice.isValid() && !selectedDevice.hidDevicePath.isEmpty()) {
            qCDebug(log_host_hid) << "Selected HID device path:" << selectedDevice.hidDevicePath;
            
            // Cache the found device path
#ifdef _WIN32
            const_cast<VideoHid*>(this)->m_cachedDevicePath = selectedDevice.hidDevicePath.toStdWString();
#elif __linux__
            const_cast<VideoHid*>(this)->m_cachedDevicePath = selectedDevice.hidDevicePath;
#endif
            
            return selectedDevice.hidDevicePath;
        }
        
        // If we get here, no HID device was found on this attempt
        if (retry < maxRetries - 1) {
            qCWarning(log_host_hid) << "No device with HID information found for port chain:" << portChain 
                                   << "(attempt" << (retry + 1) << "/" << maxRetries << ")";
        }
    }
    
    // All retries failed
    qCWarning(log_host_hid) << "No device with HID information found for port chain:" << portChain 
                           << "after" << maxRetries << "attempts";
    return QString();
}

QString VideoHid::getCurrentHIDDevicePath() const
{
    return m_currentHIDDevicePath;
}

QString VideoHid::getCurrentHIDPortChain() const
{
    return m_currentHIDPortChain;
}

bool VideoHid::switchToHIDDeviceByPortChain(const QString& portChain)
{
    if (portChain.isEmpty()) {
        qCWarning(log_host_hid) << "Cannot switch to HID device with empty port chain";
        return false;
    }

    qCDebug(log_host_hid) << "Attempting to switch to HID device by port chain:" << portChain;

    try {
        QString targetHIDPath = findMatchingHIDDevice(portChain);
        
        if (targetHIDPath.isEmpty()) {
            qCWarning(log_host_hid) << "No matching HID device found for port chain:" << portChain;
            return false;
        }

        qCDebug(log_host_hid) << "Found matching HID device path:" << targetHIDPath << "for port chain:" << portChain;

        // Check if we're already using this device - avoid unnecessary switching
        if (!m_currentHIDDevicePath.isEmpty() && m_currentHIDDevicePath == targetHIDPath) {
            qCDebug(log_host_hid) << "Already using HID device:" << targetHIDPath << "- skipping switch";
            return true;
        }

        QString previousDevicePath = m_currentHIDDevicePath;
        QString previousPortChain = m_currentHIDPortChain;
        
        qCDebug(log_host_hid) << "Switching HID device from" << previousDevicePath 
                             << "to" << targetHIDPath;

        // Close current HID device if open
        bool wasInTransaction = m_inTransaction;
        if (wasInTransaction) {
            qCDebug(log_host_hid) << "Closing current HID device before switch";
            endTransaction();
        }

        // Update current device tracking
        m_currentHIDDevicePath = targetHIDPath;
        m_currentHIDPortChain = portChain;
        
        // Clear cached device path to force re-discovery with new device
        clearDevicePathCache();
        
#ifdef _WIN32
        m_cachedDevicePath = targetHIDPath.toStdWString();
#elif __linux__
        m_cachedDevicePath = targetHIDPath;
#endif

        // Re-open HID device with new path if it was previously open
        bool switchSuccess = true;
        if (wasInTransaction) {
            qCDebug(log_host_hid) << "Re-opening HID device with new path";
            switchSuccess = beginTransaction();
            if (!switchSuccess) {
                qCWarning(log_host_hid) << "Failed to re-open HID device after switch";
                // Revert to previous device info on failure
                m_currentHIDDevicePath = previousDevicePath;
                m_currentHIDPortChain = previousPortChain;
            }
        }

        if (switchSuccess) {
            // Update global settings to remember the new device
            GlobalSetting::instance().setOpenterfacePortChain(portChain);
            
            // Emit signals for HID device switching
            emit hidDeviceChanged(previousDevicePath, targetHIDPath);
            emit hidDeviceSwitched(previousPortChain, portChain);
            emit hidDeviceConnected(targetHIDPath);
            
            if (!previousDevicePath.isEmpty()) {
                emit hidDeviceDisconnected(previousDevicePath);
            }
            
            qCDebug(log_host_hid) << "HID device switch successful to:" << targetHIDPath;
            
            // After successful switch, detect the chip type
            detectChipType();
            qCDebug(log_host_hid) << "Detected chip type:" << (m_chipImpl ? m_chipImpl->name() : QString("Unknown"));
        }
        
        return switchSuccess;

    } catch (const std::exception& e) {
        qCritical() << "Exception in switchToHIDDeviceByPortChain:" << e.what();
        return false;
    } catch (...) {
        qCritical() << "Unknown exception in switchToHIDDeviceByPortChain";
        return false;
    }
}

bool VideoHid::writeChunk(quint16 address, const QByteArray &data) {
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
        qCDebug(log_host_hid) << "writeChunk: emitted firmwareWriteChunkComplete, written_size=" << written_size << " addr=" << QString("0x%1").arg(_address, 4, 16, QChar('0')).toUpper();
        emit firmwareWriteChunkComplete(written_size);
        _address += chunkSize; 
    }
    return true;
}

bool VideoHid::writeEeprom(quint16 address, const QByteArray &data) {
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
        success = ms2130sWriteFirmware(address, data);
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
                success = writeChunk(address, chunk);

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


bool VideoHid::ms2130sEraseSector(quint32 startAddress) {
    auto* chip = dynamic_cast<Ms2130sChip*>(m_chipImpl.get());
    return chip ? chip->eraseSector(startAddress) : false;
}

bool VideoHid::ms2130sFlashEraseDone(bool &done) {
    auto* chip = dynamic_cast<Ms2130sChip*>(m_chipImpl.get());
    return chip ? chip->flashEraseDone(done) : false;
}

bool VideoHid::ms2130sFlashBurstWrite(quint32 address, const QByteArray &data) {
    auto* chip = dynamic_cast<Ms2130sChip*>(m_chipImpl.get());
    return chip ? chip->flashBurstWrite(address, data) : false;
}

bool VideoHid::ms2130sFlashBurstRead(quint32 address, quint32 length, QByteArray &outData) {
    auto* chip = dynamic_cast<Ms2130sChip*>(m_chipImpl.get());
    return chip ? chip->flashBurstRead(address, length, outData) : false;
}

int VideoHid::ms2130sDetectConnectMode() {
    auto* chip = dynamic_cast<Ms2130sChip*>(m_chipImpl.get());
    return chip ? chip->detectConnectMode() : 0;
}

bool VideoHid::ms2130sInitializeGPIO() {
    auto* chip = dynamic_cast<Ms2130sChip*>(m_chipImpl.get());
    return chip ? chip->initializeGPIO() : false;
}

void VideoHid::ms2130sRestoreGPIO() {
    auto* chip = dynamic_cast<Ms2130sChip*>(m_chipImpl.get());
    if (chip) chip->restoreGPIO();
}

bool VideoHid::ms2130sWriteFirmware(quint16 address, const QByteArray &data) {
    auto* chip = dynamic_cast<Ms2130sChip*>(m_chipImpl.get());
    return chip ? chip->writeFirmware(address, data) : false;
}

void VideoHid::loadFirmwareToEeprom() {
    qCDebug(log_host_hid) << "loadFirmwareToEeprom() called";
    
    // Create firmware data
    if (m_netClient.getNetworkFirmware().empty()) {
        qCDebug(log_host_hid) << "No firmware data available to write - networkFirmware is empty";
        emit firmwareWriteComplete(false);
        return;
    }

    const auto& rawFirmware = m_netClient.getNetworkFirmware();
    qCDebug(log_host_hid) << "networkFirmware size:" << rawFirmware.size() << "bytes";

    QByteArray firmware(reinterpret_cast<const char*>(rawFirmware.data()), rawFirmware.size());
    qCDebug(log_host_hid) << "Created QByteArray firmware with size:" << firmware.size() << "bytes";
    
    // Create a worker thread to handle firmware writing
    QThread* thread = new QThread();
    thread->setObjectName("FirmwareWriterThread");
    FirmwareWriter* worker = new FirmwareWriter(this, ADDR_EEPROM, firmware);
    worker->moveToThread(thread);
    
    qCDebug(log_host_hid) << "Created FirmwareWriter worker thread";
    
    // Connect signals/slots
    connect(thread, &QThread::started, worker, &FirmwareWriter::process);
    connect(worker, &FirmwareWriter::finished, thread, &QThread::quit);
    connect(worker, &FirmwareWriter::finished, worker, &FirmwareWriter::deleteLater);
    connect(thread, &QThread::finished, thread, &QThread::deleteLater);
    
    // Connect progress and status signals if needed
    connect(worker, &FirmwareWriter::progress, this, [this](int percent) {
        // Update last-known percent so UI can poll it if signals are missed
        m_lastFirmwarePercent.store(percent);
        qCDebug(log_host_hid) << "Firmware write progress: " << percent << "%";
        emit firmwareWriteProgress(percent);
    });
    
    connect(worker, &FirmwareWriter::finished, this, [this](bool success) {
        if (success) {
            qCInfo(log_host_hid) << "[Firmware Update] SUCCESS �?firmware has been written to the device";
            emit firmwareWriteComplete(true);
        } else {
            qCWarning(log_host_hid) << "[Firmware Update] FAILED �?please reconnect the device and try again";
            emit firmwareWriteComplete(false);
            emit firmwareWriteError("Firmware update failed. Please try again.");
        }
    });
    
    qCDebug(log_host_hid) << "Signals connected, starting FirmwareWriter thread...";
    
    // Start the thread
    thread->start();
    
    qCDebug(log_host_hid) << "FirmwareWriter thread started successfully";
}

FirmwareResult VideoHid::isLatestFirmware() {
    if (!QSslSocket::supportsSsl()) {
        qCWarning(log_host_hid) << "TLS/SSL not available - skipping firmware check";
        fireware_result = FirmwareResult::CheckFailed;
        return FirmwareResult::CheckFailed;
    }

    qCDebug(log_host_hid) << "Checking for latest firmware...";
    m_currentfirmwareVersion = getFirmwareVersion();
    fireware_result = m_netClient.check(m_currentfirmwareVersion, m_chipType,
                                        QString::fromLatin1(firmwareURL));
    qCDebug(log_host_hid) << "Firmware check result:" << (int)fireware_result
                          << "current:" << QString::fromStdString(m_currentfirmwareVersion)
                          << "latest:" << QString::fromStdString(m_netClient.getLatestVersion());
    return fireware_result;
}

int safe_stoi(std::string str, int defaultValue) {
    try {
        return std::stoi(str);
    } catch (const std::invalid_argument&) {
        qCDebug(log_host_hid) << "Invalid argument for stoi, returning default value:" << defaultValue;
        return defaultValue;
    } catch (const std::out_of_range&) {
        qCDebug(log_host_hid) << "Out of range for stoi, returning default value:" << defaultValue;
        qCDebug(log_host_hid) << "String was:" << QString::fromStdString(str);
        return defaultValue;
    }
}
