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

#ifdef _WIN32
#include <hidclass.h>
extern "C"
{
#include <hidsdi.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <devpkey.h>
}
#elif __linux__
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/hidraw.h>
#endif

Q_LOGGING_CATEGORY(log_host_hid, "opf.core.hid");

VideoHid::VideoHid(QObject *parent) : QObject(parent), m_inTransaction(false) {
    // Initialize current device tracking
    m_currentHIDDevicePath.clear();
    m_currentHIDPortChain.clear();
    
    // Connect to hotplug monitor for automatic device management
    connectToHotplugMonitor();

    // Create platform adapter responsible for platform-specific HID operations
    m_platformAdapter.reset(PlatformHidAdapter::create(this));
    if (m_platformAdapter) {
        qCDebug(log_host_hid) << "PlatformHidAdapter initialized. Initial HID path:" << m_platformAdapter->getHIDDevicePath();
    } else {
        qCDebug(log_host_hid) << "No PlatformHidAdapter available for this platform.";
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

// Detect chipset type based on VID/PID or other identifiers
void VideoHid::detectChipType() {
    // Store previous chip type for comparison
    VideoChipType previousChipType = m_chipType;
    
    // Default to UNKNOWN
    m_chipType = VideoChipType::UNKNOWN;
    
    // Check if we have a valid device path
    if (m_currentHIDDevicePath.isEmpty()) {
        qCDebug(log_host_hid) << "No valid HID device path to detect chip type";
        return;
    }
    
    // Extract VID/PID from device path
    QString devicePath = m_currentHIDDevicePath;
    qCDebug(log_host_hid) << "Detecting chip type from device path:" << devicePath;
    qCDebug(log_host_hid) << "Current port chain:" << m_currentHIDPortChain;

    // We need to check both hidDevicePath and DeviceInfo's VID/PID since different platforms
    // format the device path differently. On some systems the VID/PID might not be in the path.
    
    bool isMS2130S = false;
    bool isMS2109 = false;
    bool isMS2109S = false;
    
    // We'll rely on the device path instead of trying to access protected VID/PIDs
    // On Windows: rely on the path /VID_/PID_ tokens (device path already often contains them)
    #ifdef _WIN32
    // Look for MS2130S identifiers (VID: 345F, PID: 2132)
    if (devicePath.contains("345F", Qt::CaseInsensitive) && 
        devicePath.contains("2132", Qt::CaseInsensitive)) {
        isMS2130S = true;
        qCDebug(log_host_hid) << "Detected MS2130S chipset from path (345F:2132)";
    }
    // Look for MS2109S identifiers (VID: 345F, PID: 2109)
    else if (devicePath.contains("345F", Qt::CaseInsensitive) && 
             devicePath.contains("2109", Qt::CaseInsensitive)) {
        isMS2109S = true;
        qCDebug(log_host_hid) << "Detected MS2109S chipset from path (345F:2109)";
    }
    // Look for MS2109 identifiers (VID: 534D, PID: 2109)
    else if (devicePath.contains("534D", Qt::CaseInsensitive) && 
             devicePath.contains("2109", Qt::CaseInsensitive)) {
        isMS2109 = true;
        qCDebug(log_host_hid) << "Detected MS2109 chipset from path (534D:2109)";
    }
    // Also check for Windows style VID/PID format
    else if (devicePath.contains("vid_345f", Qt::CaseInsensitive) && 
             devicePath.contains("pid_2132", Qt::CaseInsensitive)) {
        isMS2130S = true;
        qCDebug(log_host_hid) << "Detected MS2130S chipset from Windows-style path";
    }
    else if (devicePath.contains("vid_345f", Qt::CaseInsensitive) && 
             devicePath.contains("pid_2109", Qt::CaseInsensitive)) {
        isMS2109S = true;
        qCDebug(log_host_hid) << "Detected MS2109S chipset from Windows-style path";
    }
    else if (devicePath.contains("vid_534d", Qt::CaseInsensitive) && 
             devicePath.contains("pid_2109", Qt::CaseInsensitive)) {
        isMS2109 = true;
        qCDebug(log_host_hid) << "Detected MS2109 chipset from Windows-style path";
    }
    #elif defined(__linux__)
    // On Linux: we may have /dev/hidrawX paths. Use sysfs to find idVendor/idProduct.
    {
        QString devPath(devicePath);
        // Extract hidraw device name if full path provided
        QString hidrawName = devPath;
        if (hidrawName.startsWith("/dev/")) {
            hidrawName = QFileInfo(hidrawName).fileName();
        }

        QString sysDevicePath = QString("/sys/class/hidraw/%1/device").arg(hidrawName);
        QFileInfo fi(sysDevicePath);
        QString resolvedPath = fi.canonicalFilePath();
        if (resolvedPath.isEmpty()) {
            resolvedPath = sysDevicePath; // fallback
        }

        // Walk up the device tree to find idVendor/idProduct files
        QString curPath = resolvedPath;
        int up = 0;
        while (!curPath.isEmpty() && up < 6) {
            QString idVendorPath = curPath + "/idVendor";
            QString idProductPath = curPath + "/idProduct";
            if (QFile::exists(idVendorPath) && QFile::exists(idProductPath)) {
                QString vidStr, pidStr;
                QFile vfile(idVendorPath), pfile(idProductPath);
                if (vfile.open(QIODevice::ReadOnly | QIODevice::Text)) {
                    vidStr = QString::fromUtf8(vfile.readAll()).trimmed();
                    vfile.close();
                }
                if (pfile.open(QIODevice::ReadOnly | QIODevice::Text)) {
                    pidStr = QString::fromUtf8(pfile.readAll()).trimmed();
                    pfile.close();
                }

                // Normalize hex strings (remove 0x and uppercase)
                vidStr = vidStr.remove("0x", Qt::CaseInsensitive).trimmed().toUpper();
                pidStr = pidStr.remove("0x", Qt::CaseInsensitive).trimmed().toUpper();

                if (vidStr == QStringLiteral("345F") && pidStr == QStringLiteral("2132")) {
                    isMS2130S = true;
                    qCDebug(log_host_hid) << "Detected MS2130S chipset from hidraw sysfs (VID:345F PID:2132)";
                    break;
                } else if (vidStr == QStringLiteral("345F") && pidStr == QStringLiteral("2109")) {
                    // V3 devices (345F:2109) use MS2109S register mapping
                    isMS2109S = true;
                    qCDebug(log_host_hid) << "Detected MS2109S chipset from hidraw sysfs (VID:345F PID:2109)";
                    break;
                } else if (vidStr == QStringLiteral("534D") && pidStr == QStringLiteral("2109")) {
                    isMS2109 = true;
                    qCDebug(log_host_hid) << "Detected MS2109 chipset from hidraw sysfs (VID:534D PID:2109)";
                    break;
                } else {
                    qCDebug(log_host_hid) << "HID sysfs vendor/product read:" << vidStr << pidStr << "(no match)";
                }
            }
            // Move to parent directory and continue searching
            QFileInfo cfi(curPath);
            QString parent = cfi.dir().path();
            if (parent == curPath || parent.isEmpty()) break;
            curPath = parent;
            ++up;
        }
        // If nothing found via sysfs, also consider matching the original device path content
        if (!isMS2130S && !isMS2109 && !isMS2109S) {
            if (devicePath.contains("345F", Qt::CaseInsensitive) && devicePath.contains("2132", Qt::CaseInsensitive)) {
                isMS2130S = true;
                qCDebug(log_host_hid) << "Detected MS2130S chipset from path (fallback)";
            } else if (devicePath.contains("345F", Qt::CaseInsensitive) && devicePath.contains("2109", Qt::CaseInsensitive)) {
                isMS2109S = true;
                qCDebug(log_host_hid) << "Detected MS2109S chipset from path (fallback)";
            } else if (devicePath.contains("534D", Qt::CaseInsensitive) && devicePath.contains("2109", Qt::CaseInsensitive)) {
                isMS2109 = true;
                qCDebug(log_host_hid) << "Detected MS2109 chipset from path (fallback)";
            }
        }
    }
    #endif
    
    if (isMS2130S) {
        m_chipType = VideoChipType::MS2130S;
        m_chipImpl = std::make_unique<Ms2130sChip>(this);
    } else if (isMS2109S) {
        m_chipType = VideoChipType::MS2109S;
        m_chipImpl = std::make_unique<Ms2109sChip>(this);
    } else if (isMS2109) {
        m_chipType = VideoChipType::MS2109;
        m_chipImpl = std::make_unique<Ms2109Chip>(this);
    } else {
        qCDebug(log_host_hid) << "Unknown chipset in device path:" << devicePath;
        // If we couldn't detect the type but had a previous valid type, keep using it
        if (previousChipType != VideoChipType::UNKNOWN) {
            m_chipType = previousChipType;
            // Ensure the implementation matches the previous type
            if (m_chipType == VideoChipType::MS2130S) m_chipImpl = std::make_unique<Ms2130sChip>(this);
            else if (m_chipType == VideoChipType::MS2109S) m_chipImpl = std::make_unique<Ms2109sChip>(this);
            else if (m_chipType == VideoChipType::MS2109) m_chipImpl = std::make_unique<Ms2109Chip>(this);

            qCDebug(log_host_hid) << "Falling back to previous chip type";
        }
    }
    
    // Log when chip type changes
    if (previousChipType != m_chipType) {
        qCDebug(log_host_hid) << "Chip type changed from" 
            << (previousChipType == VideoChipType::MS2109 ? "MS2109" :
                previousChipType == VideoChipType::MS2109S ? "MS2109S" :
                previousChipType == VideoChipType::MS2130S ? "MS2130S" : "Unknown") 
            << "to" 
            << (m_chipType == VideoChipType::MS2109 ? "MS2109" :
                m_chipType == VideoChipType::MS2109S ? "MS2109S" :
                m_chipType == VideoChipType::MS2130S ? "MS2130S" : "Unknown");
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

void VideoHid::fetchBinFileToString(QString &url, int timeoutMs){
    QNetworkAccessManager manager; // Create a network access manager
    QNetworkRequest request(url);  // Set up the request with the given URL
    QNetworkReply *reply = manager.get(request); // Send a GET request

    // Use QEventLoop to wait for the request to complete with timeout
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);

    QObject::connect(reply, &QNetworkReply::finished, &loop, [&]() {
        qCDebug(log_host_hid) << "Firmware download finished";
        loop.quit();
    });

    QObject::connect(&timer, &QTimer::timeout, &loop, [&]() {
        qCDebug(log_host_hid) << "Firmware download timed out";
        fireware_result = FirmwareResult::Timeout;
        reply->abort();
        loop.quit();
    });

    timer.start(timeoutMs > 0 ? timeoutMs : 5000); // Use provided timeout or default to 5 seconds
    loop.exec(); // Block until the request finishes or times out

    if (timer.isActive()) {
        timer.stop(); // Stop the timer if not triggered
    }

    // Check if timeout occurred before processing response
    if (fireware_result == FirmwareResult::Timeout) {
        qCDebug(log_host_hid) << "Firmware download timed out";
        reply->deleteLater();
        return;
    }

    std::string result; // Initialize an empty std::string to hold the result
    if (reply->error() == QNetworkReply::NoError) {
        // Read the binary data into a QByteArray
        QByteArray data = reply->readAll();
        // Convert QByteArray to std::string
        result = std::string(data.constData(), data.size());
        networkFirmware.assign(data.begin(), data.end());
        qCDebug(log_host_hid)  << "Successfully read file, size:" << data.size() << "bytes";
    } else {
        qCDebug(log_host_hid)  << "Failed to fetch latest firmware:" << reply->errorString();
        fireware_result = FirmwareResult::CheckFailed; // Set the result to check failed
    }
    int version_0 = result.length() > 12 ? (unsigned char)result[12] : 0;
    int version_1 = result.length() > 13 ? (unsigned char)result[13] : 0;
    int version_2 = result.length() > 14 ? (unsigned char)result[14] : 0;
    int version_3 = result.length() > 15 ? (unsigned char)result[15] : 0;
    m_firmwareVersion = QString("%1%2%3%4").arg(version_0, 2, 10, QChar('0'))
                                            .arg(version_1, 2, 10, QChar('0'))
                                            .arg(version_2, 2, 10, QChar('0'))
                                            .arg(version_3, 2, 10, QChar('0')).toStdString();
    reply->deleteLater(); // Clean up the reply object
}

QString VideoHid::getLatestFirmwareFilenName(QString &url, int timeoutMs) {
    QNetworkAccessManager manager;
    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", "MyFirmwareChecker/1.0");

    QNetworkReply *reply = manager.get(request);
    if (!reply) {
        qCDebug(log_host_hid) << "Failed to create network reply";
        fireware_result = FirmwareResult::CheckFailed;
        return QString();
    } else {
        fireware_result = FirmwareResult::Checking; // Set the initial state to checking
        qCDebug(log_host_hid) << "Network reply created successfully";
    }

    qCDebug(log_host_hid) << "Fetching latest firmware index from" << url;

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);

    QObject::connect(reply, &QNetworkReply::finished, &loop, [&]() {
        qDebug(log_host_hid) << "Network reply finished";
        loop.quit();
    });

    QObject::connect(&timer, &QTimer::timeout, &loop, [&]() {
        qCDebug(log_host_hid) << "Request timed out";
        fireware_result = FirmwareResult::Timeout;
        reply->abort();
        reply->deleteLater();
        loop.quit();
    });

    timer.start(timeoutMs);
    loop.exec();

    if (timer.isActive()) {
        timer.stop(); // Stop the timer if not triggered
    }

    if (fireware_result == FirmwareResult::Timeout) {
        qCDebug(log_host_hid) << "Firmware check timed out";
        return QString(); // Already handled in timeout handler
    }

    if (reply->error() == QNetworkReply::NoError) {
        qCDebug(log_host_hid) << "Successfully fetched latest firmware index";
        QString indexContent = QString::fromUtf8(reply->readAll());
        // Select filename matching our chip (handles CSV multi-line and legacy single-line formats)
        QString fileName = pickFirmwareFileNameFromIndex(indexContent, m_chipType);
        if (fileName.isEmpty()) {
            qCWarning(log_host_hid) << "No firmware filename could be selected from index for chipType:" << (int)m_chipType;
            fireware_result = FirmwareResult::CheckFailed;
            reply->deleteLater();
            return QString();
        }
        qCInfo(log_host_hid) << "Selected firmware file for chipType" << (int)m_chipType << ":" << fileName;
        fireware_result = FirmwareResult::CheckSuccess;
        reply->deleteLater();
        return fileName;
    } else {
        qCDebug(log_host_hid) << "Fail to get firmware index:" << reply->errorString();
        fireware_result = FirmwareResult::CheckFailed;
        reply->deleteLater();
        return QString();
    }
} 

// Parse an index file and pick the appropriate firmware filename for the given chip.
// Supports:
//  - legacy single-line: "filename.bin"
//  - CSV multi-line: "<version>,<filename>,<chipToken>" (one entry per line)
QString VideoHid::pickFirmwareFileNameFromIndex(const QString &indexContent, VideoChipType chip) {
    if (indexContent.trimmed().isEmpty()) return QString();

    // split on '\n' and trim each line (trimmed() will remove '\r') — avoids QRegExp (deprecated/removed in some Qt6 builds)
    QStringList lines = indexContent.split('\n', Qt::SkipEmptyParts);
    struct Candidate { QString version; QString filename; QString chipToken; };
    QVector<Candidate> candidates;

    auto isUpgradeLike = [](const QString &text) -> bool {
        QString t = text.toLower();
        return t.contains("upg") || t.contains("upgrade") || t.contains("boot") ||
               t.contains("loader") || t.contains("hidonly") || t.contains("tool") ||
               t.contains("ram");
    };

    for (QString rawLine : lines) {
        QString line = rawLine.trimmed();
        if (line.isEmpty() || line.startsWith('#')) continue;
        // CSV format: version,filename,chip
        QStringList parts = line.split(',', Qt::KeepEmptyParts);
        for (int i = 0; i < parts.size(); ++i) parts[i] = parts[i].trimmed();
        if (parts.size() == 1) {
            // Legacy single-line containing filename only.
            // Do NOT return early here: route through the same safety filters so
            // upgrader/boot images can be rejected consistently.
            if (!parts[0].isEmpty() && !parts[0].contains(',')) {
                Candidate c;
                c.version = QStringLiteral("0");
                c.filename = parts[0];
                c.chipToken = QString();
                candidates.append(c);
            }
            continue;
        }
        if (parts.size() < 2) continue;
        Candidate c; c.version = parts[0]; c.filename = parts[1]; c.chipToken = parts.size() > 2 ? parts[2].toLower() : QString();
        candidates.append(c);
    }

    auto tokenMatchesChip = [](const QString &token, VideoChipType chipType) -> bool {
        if (token.isEmpty()) return false;
        QString t = token.toLower();
        switch (chipType) {
            case VideoChipType::MS2109:  return t.contains("2109") && !t.contains('s');
            case VideoChipType::MS2109S: return t.contains("2109s") || (t.contains("2109") && t.contains('s'));
            case VideoChipType::MS2130S: return t.contains("2130") || t.contains("2130s") || t.contains("2130_s");
            default: return false;
        }
    };

    QVector<Candidate> matched;
    for (const Candidate &c : candidates) {
        if (chip != VideoChipType::UNKNOWN && tokenMatchesChip(c.chipToken, chip)) matched.append(c);
    }

    if (matched.isEmpty()) {
        // try to infer by filename if chip token didn't match or chip is UNKNOWN
        for (const Candidate &c : candidates) {
            QString fn = c.filename.toLower();
            if (fn.contains("2130")) matched.append(c);
            else if (fn.contains("2109s") || fn.contains("2109_s")) matched.append(c);
            else if (fn.contains("2109")) matched.append(c);
        }
    }

    // Safety filter: prefer non-upgrader images to avoid flashing HID-only firmware.
    QVector<Candidate> nonUpgrade;
    for (const Candidate &c : matched) {
        if (!isUpgradeLike(c.filename) && !isUpgradeLike(c.chipToken)) {
            nonUpgrade.append(c);
        }
    }
    if (!nonUpgrade.isEmpty()) {
        matched = nonUpgrade;
    }

    if (matched.isEmpty()) matched = candidates; // fallback
    if (matched.isEmpty()) return QString();

    auto versionKey = [](const QString &v) -> qint64 {
        bool ok = false; qint64 n = v.toLongLong(&ok); if (ok) return n;
        QString digits; for (QChar ch : v) if (ch.isDigit()) digits.append(ch);
        return digits.isEmpty() ? 0 : digits.toLongLong();
    };

    Candidate best = matched.first();
    qint64 bestVer = versionKey(best.version);
    for (const Candidate &c : matched) {
        qint64 ver = versionKey(c.version);
        if (ver > bestVer) { best = c; bestVer = ver; }
    }
    if (isUpgradeLike(best.filename) || isUpgradeLike(best.chipToken)) {
        qCWarning(log_host_hid) << "Refusing upgrader/bootloader firmware candidate:" << best.filename
                                << "token:" << best.chipToken;
        return QString();
    }
    return best.filename;
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
        // Fallback to legacy behavior
        if (m_chipType == VideoChipType::MS2130S) {
            result = usbXdataRead4ByteMS2130S(u16_address);
        } else if (m_chipType == VideoChipType::MS2109S) {
            result = usbXdataRead4ByteMS2109S(u16_address);
        } else {
            result = usbXdataRead4ByteMS2109(u16_address);
        }
    }

    // Normalize the result to have the data byte at position 0
    if (result.second && !result.first.isEmpty()) {
        quint8 dataByte = result.first.at(0);
        return qMakePair(QByteArray(1, dataByte), true);
    } else {
        return qMakePair(QByteArray(1, 0), false);
    }
}

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

bool VideoHid::usbXdataWrite4Byte(quint16 u16_address, QByteArray data) {
    // Different control data size for different chip types
    QByteArray ctrlData;
    
    // Select appropriate command and format based on chip implementation
    if (m_chipImpl && m_chipImpl->type() == VideoChipType::MS2130S) {
        // MS2130S uses an 11-byte MS2109-compatible feature packet and report ID 1 by default
        ctrlData = QByteArray(11, 0); // Initialize with 11 bytes set to 0
        ctrlData[0] = 0x01; // Report ID 1 is preferred for MS2130S
        ctrlData[1] = MS2130S_CMD_XDATA_WRITE;
        ctrlData[2] = static_cast<char>((u16_address >> 8) & 0xFF);
        ctrlData[3] = static_cast<char>(u16_address & 0xFF);
        ctrlData.replace(4, 4, data);
    } else if (m_chipImpl && m_chipImpl->type() == VideoChipType::MS2109S) {
        // MS2109S uses an 11-byte structure matching its specialized read implementation
        ctrlData = QByteArray(9, 0); 
        ctrlData[0] = 0x00; // Report ID
        ctrlData[1] = MS2109S_CMD_XDATA_WRITE;
        ctrlData[2] = static_cast<char>((u16_address >> 8) & 0xFF);
        ctrlData[3] = static_cast<char>(u16_address & 0xFF);
        ctrlData.replace(4, 4, data);
    } else {
        // MS2109 standard format
        ctrlData = QByteArray(9, 0); // Initialize with 9 bytes set to 0
        ctrlData[1] = CMD_XDATA_WRITE;
        ctrlData[2] = static_cast<char>((u16_address >> 8) & 0xFF);
        ctrlData[3] = static_cast<char>(u16_address & 0xFF);
        ctrlData.replace(4, 4, data);
    }

    // Display debug information about the write operation
    QString hexData = data.toHex(' ').toUpper();
    qCDebug(log_host_hid) << "Writing to address:" << QString("0x%1").arg(u16_address, 4, 16, QChar('0'))
                         << "for chip type:" << (m_chipImpl ? m_chipImpl->name() : QString("Unknown"))
                         << "data:" << hexData
                         << "report buffer:" << ctrlData.toHex(' ').toUpper();

    bool result = this->sendFeatureReport((uint8_t*)ctrlData.data(), ctrlData.size());
    if (!result) {
        qCWarning(log_host_hid) << "Failed to write to address:" << QString("0x%1").arg(u16_address, 4, 16, QChar('0'));
        
        // For MS2130S, if the standard approach fails, try an alternative format
        if (m_chipImpl && m_chipImpl->type() == VideoChipType::MS2130S && !result) {
            qCDebug(log_host_hid) << "Trying alternative format for MS2130S write operation";
            
            // Try an alternative format - some devices need a different structure
            QByteArray altCtrlData = QByteArray(65, 0); // Try with larger buffer
            altCtrlData[0] = 0x00; // Report ID
            altCtrlData[1] = MS2130S_CMD_XDATA_WRITE;
            altCtrlData[2] = static_cast<char>((u16_address >> 8) & 0xFF);
            altCtrlData[3] = static_cast<char>(u16_address & 0xFF);
            altCtrlData.replace(4, 4, data);
            
            qCDebug(log_host_hid) << "Trying alternative write with buffer size:" << altCtrlData.size();
            result = this->sendFeatureReport((uint8_t*)altCtrlData.data(), altCtrlData.size());
            
            if (result) {
                qCDebug(log_host_hid) << "Alternative format succeeded for MS2130S";
            } else {
                qCWarning(log_host_hid) << "Alternative format also failed for MS2130S";
            }
        }
    }
    return result;
}

bool VideoHid::getFeatureReport(uint8_t* buffer, size_t bufferLength) {
    // Prefer adapter if available
    if (m_platformAdapter) {
        return m_platformAdapter->getFeatureReport(buffer, bufferLength);
    }
#ifdef _WIN32
    return this->getFeatureReportWindows(buffer, bufferLength);
#elif __linux__
    return this->getFeatureReportLinux(buffer, bufferLength);
#else
    Q_UNUSED(buffer); Q_UNUSED(bufferLength);
    return false;
#endif
}

bool VideoHid::sendFeatureReport(uint8_t* buffer, size_t bufferLength) {
    // Prefer adapter if available
    if (m_platformAdapter) {
        return m_platformAdapter->sendFeatureReport(buffer, bufferLength);
    }
#ifdef _WIN32
    int retries = 2;
    while (retries-- > 0) {
        if (sendFeatureReportWindows(buffer, bufferLength)) {
            return true;
        }
        qCDebug(log_host_hid)  << "Retrying sendFeatureReportWindows...";
    }
    return false;
#elif __linux__
    int retries = 2;
    while (retries-- > 0) {
        if (sendFeatureReportLinux(buffer, bufferLength)) {
            return true;
        }
        qCDebug(log_host_hid)  << "Retrying sendFeatureReportLinux...";
    }
    return false;
#else
    Q_UNUSED(buffer); Q_UNUSED(bufferLength);
    return false;
#endif
}

void VideoHid::closeHIDDeviceHandle() {
    QMutexLocker locker(&m_deviceHandleMutex);
    
    #ifdef _WIN32
        if (deviceHandle != INVALID_HANDLE_VALUE) {
            qCDebug(log_host_hid) << "Closing HID device handle...";
            CloseHandle(deviceHandle);
            deviceHandle = INVALID_HANDLE_VALUE;
        }
    #elif __linux__
        if (hidFd >= 0) {
            close(hidFd);
            hidFd = -1;
        }
    #endif
}

// Platform wrapper implementations used by PlatformHidAdapter
bool VideoHid::platform_openDevice() {
#ifdef _WIN32
    return openHIDDeviceHandle();
#elif __linux__
    return openHIDDevice();
#else
    return false;
#endif
}

void VideoHid::platform_closeDevice() {
    closeHIDDeviceHandle();
}

bool VideoHid::platform_sendFeatureReport(uint8_t* reportBuffer, size_t bufferSize) {
#ifdef _WIN32
    return sendFeatureReportWindows(reinterpret_cast<BYTE*>(reportBuffer), static_cast<DWORD>(bufferSize));
#elif __linux__
    return sendFeatureReportLinux(reportBuffer, static_cast<int>(bufferSize));
#else
    Q_UNUSED(reportBuffer); Q_UNUSED(bufferSize);
    return false;
#endif
}

bool VideoHid::platform_getFeatureReport(uint8_t* reportBuffer, size_t bufferSize) {
#ifdef _WIN32
    return getFeatureReportWindows(reinterpret_cast<BYTE*>(reportBuffer), static_cast<DWORD>(bufferSize));
#elif __linux__
    return getFeatureReportLinux(reportBuffer, static_cast<int>(bufferSize));
#else
    Q_UNUSED(reportBuffer); Q_UNUSED(bufferSize);
    return false;
#endif
}

QString VideoHid::platform_getHIDDevicePath() {
#ifdef _WIN32
    std::wstring wpath = getHIDDevicePath();
    return QString::fromStdWString(wpath);
#elif __linux__
    return getHIDDevicePath();
#else
    return QString();
#endif
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
            success = openHIDDeviceHandle();
        #elif __linux__
            success = openHIDDevice();
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
                    qCInfo(log_host_hid) << "✓ HID device stop scheduled for unplugged device at port:" << device.portChain;
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
                    qCInfo(log_host_hid) << "✓ HID device auto-switched to new device at port:" << device.portChain;
                    
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

#ifdef _WIN32
std::wstring VideoHid::getHIDDevicePath() {
    QString portChain = GlobalSetting::instance().getOpenterfacePortChain();
    QString hidPath = findMatchingHIDDevice(portChain);
    
    if (!hidPath.isEmpty()) {
        // For MS2130S devices with VID_345F & PID_2132, we need to get the full device path
        // that Windows can use with CreateFileW, not just the device instance path
        if (hidPath.contains("VID_345F", Qt::CaseInsensitive) && 
            hidPath.contains("PID_2132", Qt::CaseInsensitive)) {
            // We need to get the proper path for this device using SetupDi functions
            std::wstring fullPath = getProperDevicePath(hidPath.toStdWString());
            if (!fullPath.empty()) {
                // qCDebug(log_host_hid) << "Using proper device path for MS2130S:" 
                //                      << QString::fromStdWString(fullPath);
                return fullPath;
            }
        }
        return hidPath.toStdWString();
    }
    
    // Fallback to original VID/PID enumeration method
    qCDebug(log_host_hid) << "Falling back to VID/PID enumeration for HID device discovery";
    
    GUID hidGuid;
    HidD_GetHidGuid(&hidGuid); // Get the HID GUID

    // Get a handle to a device information set for all present HID devices.
    HDEVINFO deviceInfoSet = SetupDiGetClassDevs(&hidGuid, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);

    if (deviceInfoSet == INVALID_HANDLE_VALUE) {
        return L"";
    }

    SP_DEVICE_INTERFACE_DATA deviceInterfaceData;
    deviceInterfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

    // Enumerate through all devices in the set
    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(deviceInfoSet, NULL, &hidGuid, i, &deviceInterfaceData); i++) {
        DWORD requiredSize = 0;

        // Get the size required for the device interface detail
        SetupDiGetDeviceInterfaceDetail(deviceInfoSet, &deviceInterfaceData, NULL, 0, &requiredSize, NULL);

        PSP_DEVICE_INTERFACE_DETAIL_DATA deviceInterfaceDetailData = (PSP_DEVICE_INTERFACE_DETAIL_DATA)malloc(requiredSize);
        deviceInterfaceDetailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

        // Now retrieve the device interface detail which includes the device path
        if (SetupDiGetDeviceInterfaceDetail(deviceInfoSet, &deviceInterfaceData, deviceInterfaceDetailData, requiredSize, NULL, NULL)) {
            HANDLE deviceHandle = CreateFile(deviceInterfaceDetailData->DevicePath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);

            if (deviceHandle != INVALID_HANDLE_VALUE) {
                HIDD_ATTRIBUTES attributes;
                attributes.Size = sizeof(attributes);

                if (HidD_GetAttributes(deviceHandle, &attributes)) {
                    if (attributes.VendorID == 0x534D && attributes.ProductID == 0x2109) {
                        std::wstring devicePath = deviceInterfaceDetailData->DevicePath;
                        CloseHandle(deviceHandle);
                        free(deviceInterfaceDetailData);
                        SetupDiDestroyDeviceInfoList(deviceInfoSet);
                        
                        return devicePath; // Found the device
                    }
                }
                CloseHandle(deviceHandle);
            }
        }
        free(deviceInterfaceDetailData);
    }

    SetupDiDestroyDeviceInfoList(deviceInfoSet);
    
    return L""; // Device not found
}

std::wstring VideoHid::getProperDevicePath(const std::wstring& deviceInstancePath) {
    GUID hidGuid;
    HidD_GetHidGuid(&hidGuid);
    
    // Convert wstring to QString for easier comparison and analysis
    QString instancePathStr = QString::fromStdWString(deviceInstancePath);
    qCDebug(log_host_hid) << "Looking for proper device path for:" << instancePathStr;
    
    // Check if path already starts with "\\?\hid" which indicates it's already a proper device path
    if (instancePathStr.startsWith("\\\\?\\hid", Qt::CaseInsensitive) || 
        instancePathStr.startsWith("\\\\.\\hid", Qt::CaseInsensitive)) {
        qCDebug(log_host_hid) << "Path already appears to be a proper device path";
        // Try to extract VID/PID information for logging
        if (instancePathStr.contains("VID_", Qt::CaseInsensitive) && instancePathStr.contains("PID_", Qt::CaseInsensitive)) {
            qCDebug(log_host_hid) << "Using existing device path with VID/PID in it";
            return deviceInstancePath;
        }
    }
    
    // Parse VID and PID from the original path if available
    uint16_t targetVid = 0x345F;  // Default for MS2130S
    uint16_t targetPid = 0x2132;  // Default for MS2130S
    QString mi;
    
    // Extract VID and PID using simple string search since QRegularExpression might not be available
    if (instancePathStr.contains("VID_", Qt::CaseInsensitive) && 
        instancePathStr.contains("PID_", Qt::CaseInsensitive)) {
        
        int vidIndex = instancePathStr.indexOf("VID_", 0, Qt::CaseInsensitive) + 4;
        int pidIndex = instancePathStr.indexOf("PID_", 0, Qt::CaseInsensitive) + 4;
        
        if (vidIndex > 4 && pidIndex > 4) {
            bool vidOk = false, pidOk = false;
            QString vidStr = instancePathStr.mid(vidIndex, 4);
            QString pidStr = instancePathStr.mid(pidIndex, 4);
            
            targetVid = vidStr.toUInt(&vidOk, 16);
            targetPid = pidStr.toUInt(&pidOk, 16);
            
            if (vidOk && pidOk) {
                qCDebug(log_host_hid) << "Extracted VID:" << QString("0x%1").arg(targetVid, 4, 16, QChar('0'))
                                      << "PID:" << QString("0x%1").arg(targetPid, 4, 16, QChar('0'));
            }
        }
    }
    
    // Extract MI (interface) value if present
    if (instancePathStr.contains("MI_", Qt::CaseInsensitive)) {
        int miIndex = instancePathStr.indexOf("MI_", 0, Qt::CaseInsensitive) + 3;
        if (miIndex > 3) {
            mi = instancePathStr.mid(miIndex, 2);
            qCDebug(log_host_hid) << "Extracted MI:" << mi;
        }
    }
    
    // Get a handle to a device information set for all present HID devices
    HDEVINFO deviceInfoSet = SetupDiGetClassDevs(&hidGuid, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    
    if (deviceInfoSet == INVALID_HANDLE_VALUE) {
        qCDebug(log_host_hid) << "Failed to get device info set";
        return L"";
    }
    
    SP_DEVICE_INTERFACE_DATA deviceInterfaceData;
    deviceInterfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);
    
    std::wstring properPath = L"";
    
    // Enumerate through all devices in the set
    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(deviceInfoSet, NULL, &hidGuid, i, &deviceInterfaceData); i++) {
        DWORD requiredSize = 0;
        
        // Get the size required for the device interface detail
        SetupDiGetDeviceInterfaceDetail(deviceInfoSet, &deviceInterfaceData, NULL, 0, &requiredSize, NULL);
        
        if (requiredSize == 0) {
            continue;
        }
        
        PSP_DEVICE_INTERFACE_DETAIL_DATA deviceInterfaceDetailData = 
            (PSP_DEVICE_INTERFACE_DETAIL_DATA)malloc(requiredSize);
        if (!deviceInterfaceDetailData) {
            continue;
        }
        
        deviceInterfaceDetailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);
        
        // Now retrieve the device interface detail which includes the device path
        if (SetupDiGetDeviceInterfaceDetail(deviceInfoSet, &deviceInterfaceData, 
                                          deviceInterfaceDetailData, requiredSize, NULL, NULL)) {
            
            // Convert device path to string for comparison
            QString currentDevicePath = QString::fromWCharArray(deviceInterfaceDetailData->DevicePath);
            
            // Get handle to the device
            HANDLE deviceHandle = CreateFile(deviceInterfaceDetailData->DevicePath, 
                                         GENERIC_READ | GENERIC_WRITE, 
                                         FILE_SHARE_READ | FILE_SHARE_WRITE, 
                                         NULL, OPEN_EXISTING, 0, NULL);
            
            if (deviceHandle != INVALID_HANDLE_VALUE) {
                HIDD_ATTRIBUTES attributes;
                attributes.Size = sizeof(attributes);
                
                if (HidD_GetAttributes(deviceHandle, &attributes)) {
                    // Check if this device matches our target VID and PID
                    if (attributes.VendorID == targetVid && attributes.ProductID == targetPid) {
                        // Check for MI if it was specified in the original path
                        bool miMatch = true;
                        if (!mi.isEmpty()) {
                            miMatch = currentDevicePath.contains("MI_" + mi, Qt::CaseInsensitive);
                        }
                        
                        if (miMatch) {
                            qCDebug(log_host_hid) << "Found matching device with path:" << currentDevicePath;
                            properPath = deviceInterfaceDetailData->DevicePath;
                            CloseHandle(deviceHandle);
                            free(deviceInterfaceDetailData);
                            break;  // Found a match
                        } else {
                            qCDebug(log_host_hid) << "Found device with matching VID/PID but different MI:" << currentDevicePath;
                        }
                    }
                }
                CloseHandle(deviceHandle);
            } else {
                qCDebug(log_host_hid) << "Failed to open device:" << currentDevicePath 
                                     << "Error:" << GetLastError();
            }
        }
        free(deviceInterfaceDetailData);
    }
    
    SetupDiDestroyDeviceInfoList(deviceInfoSet);
    
    if (properPath.empty()) {
        qCWarning(log_host_hid) << "Could not find proper device path for:" << instancePathStr;
    } else {
        qCDebug(log_host_hid) << "Successfully found proper device path:" 
                             << QString::fromStdWString(properPath);
    }
    
    return properPath;
}

// ---------------------------------------------------------------------------
// hidSendFeatureNoTimeout
// ---------------------------------------------------------------------------
// Replacement for HidD_SetFeature that has NO internal 5-second timeout.
//
// HidD_SetFeature is implemented in hid.dll as:
//   DeviceIoControl(handle, IOCTL_HID_SET_FEATURE, ..., lpOverlapped=NULL)
// When handle has FILE_FLAG_OVERLAPPED but lpOverlapped is NULL the call waits
// synchronously with an internal WaitForSingleObject(event, 5000 ms), causing
// ERROR_SEM_TIMEOUT (121) for any USB control transfer that takes longer than
// 5 s (e.g. MS2130S sector erase ~5-8 s, 4096-byte burst data packets).
//
// By calling DeviceIoControl ourselves with an explicit OVERLAPPED and then
// WaitForSingleObject(event, timeoutMs) we control (and can remove) that limit.
//
// hDevice MUST be opened with FILE_FLAG_OVERLAPPED.
// timeoutMs: max wait in ms; pass INFINITE (0xFFFFFFFF) to wait forever.
// Returns TRUE on success. On timeout sets last error to ERROR_SEM_TIMEOUT (121).
static BOOL hidSendFeatureNoTimeout(HANDLE hDevice, void* buf, DWORD bufLen,
                                    DWORD timeoutMs = INFINITE)
{
    // IOCTL_HID_SET_FEATURE = HID_IN_CTL_CODE(100)
    //   = CTL_CODE(FILE_DEVICE_KEYBOARD=0xb, 100, METHOD_IN_DIRECT=1, FILE_ANY_ACCESS=0)
    //   = (0xb<<16)|(0<<14)|(100<<2)|1 = 0x000B0191
    constexpr DWORD kSetFeatureIoctl = 0x000B0191UL;

    OVERLAPPED ol = {};
    ol.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!ol.hEvent) return FALSE;

    DWORD dummy = 0;
    // Submit the IOCTL asynchronously.
    // IOCTL_HID_SET_FEATURE uses METHOD_IN_DIRECT: the HID class driver builds an MDL
    // from the *output* buffer and maps it to the device.  Both lpInBuffer and
    // lpOutBuffer must point to the report data, both with the same size — exactly as
    // HidD_SetFeature and libusb do internally.  Passing NULL/0 for the output buffer
    // causes the device to receive no data (empty MDL), corrupting flash writes.
    BOOL ok = DeviceIoControl(hDevice, kSetFeatureIoctl,
                              buf, bufLen,
                              buf, bufLen,
                              &dummy, &ol);
    if (!ok) {
        DWORD err = GetLastError();
        if (err == ERROR_IO_PENDING) {
            // Wait with our own (possibly infinite) timeout
            DWORD w = WaitForSingleObject(ol.hEvent, timeoutMs);
            if (w == WAIT_OBJECT_0) {
                ok = GetOverlappedResult(hDevice, &ol, &dummy, FALSE);
            } else {
                // Timed out — cancel the in-flight I/O and propagate 121
                CancelIo(hDevice);
                GetOverlappedResult(hDevice, &ol, &dummy, TRUE); // drain cancel
                SetLastError(ERROR_SEM_TIMEOUT);
                ok = FALSE;
            }
        }
        // else: immediate non-pending error — ok already FALSE, last error set
    }
    // Preserve last error across CloseHandle (which may reset it on success)
    DWORD savedError = GetLastError();
    CloseHandle(ol.hEvent);
    if (!ok) SetLastError(savedError);
    return ok;
}

bool VideoHid::sendFeatureReportWindows(BYTE* reportBuffer, DWORD bufferSize) {
    QMutexLocker locker(&m_deviceHandleMutex);
    
    bool openedForOperation = m_inTransaction || openHIDDeviceHandle();
    
    if (!openedForOperation) {
        qCDebug(log_host_hid) << "Failed to open device handle for sending feature report.";
        return false;
    }

    // Add debug info about the report being sent
    if (bufferSize > 0) {
        QString hexData;
        for (DWORD i = 0; i < bufferSize && i < 20; i++) { // Limit to first 20 bytes for readability
            hexData += QString("%1 ").arg(reportBuffer[i], 2, 16, QChar('0')).toUpper();
        }
        // qCDebug(log_host_hid) << "Sending feature report with size:" << bufferSize 
        //                      << "data (hex):" << hexData;
    }
    
    // For MS2130S, make sure reportBuffer[0] is the report ID (usually 0)
    if (m_chipImpl && m_chipImpl->type() == VideoChipType::MS2130S) {
        // In case the MS2130S needs a specific report ID
        // reportBuffer[0] = 0; // This should already be set
        qCDebug(log_host_hid) << "Using report ID" << (int)reportBuffer[0] << "for" << m_chipImpl->name() << "device";
    }

    // Get device capabilities to better understand what's supported
    PHIDP_PREPARSED_DATA preparsedData = nullptr;
    HIDP_CAPS caps;
    memset(&caps, 0, sizeof(HIDP_CAPS)); // Properly initialize all fields to zero
    
    if (HidD_GetPreparsedData(deviceHandle, &preparsedData)) {
        if (HidP_GetCaps(preparsedData, &caps) == HIDP_STATUS_SUCCESS) {
            // qCDebug(log_host_hid) << "Device capabilities - Feature Report Byte Length:" << caps.FeatureReportByteLength
            //                      << "Input Report Byte Length:" << caps.InputReportByteLength
            //                      << "Output Report Byte Length:" << caps.OutputReportByteLength;
            
            // MS2130S reports a very large feature report size, but we know it works with smaller sizes
            if (caps.FeatureReportByteLength > 1000 && m_chipImpl && m_chipImpl->type() == VideoChipType::MS2130S) {
                // qCDebug(log_host_hid) << "Detected very large feature report size for" << m_chipImpl->name() << ", using standard size instead";
                // Don't try to adjust the buffer - we'll use our predefined size
            }
            // For normal cases, warn about size mismatch
            else if (bufferSize != caps.FeatureReportByteLength && caps.FeatureReportByteLength > 0) {
                // qCDebug(log_host_hid) << "Warning: Feature report size mismatch. Using:" << bufferSize
                //                      << "Device expects:" << caps.FeatureReportByteLength;
            }
        }
        HidD_FreePreparsedData(preparsedData);
    }

    // For MS2130S devices with large expected report sizes, use robust handling
    bool result = false;

    if (m_chipImpl && m_chipImpl->type() == VideoChipType::MS2130S) {
        // For burst data packet marker (0x03), send raw without modification.
        if (bufferSize > 0 && reportBuffer[0] == 0x03) {
            // 4096-byte burst data packet.  Use hidSendFeatureNoTimeout so the call
            // waits as long as the device needs to absorb the data — no 5 s ceiling.
            qCDebug(log_host_hid) << "MS2130S burst packet send (raw) bufferSize" << bufferSize;
            BOOL ok = hidSendFeatureNoTimeout(deviceHandle, reportBuffer, bufferSize,
                                             30000 /*30 s max*/);
            if (!ok) {
                DWORD error = GetLastError();
                if (error == 121 /*ERROR_SEM_TIMEOUT*/) {
                    // The USB STATUS stage timed out at the kernel level (hidusb.sys
                    // enforces a 5 s per-URB limit).  The DATA stage completed — the
                    // device received all 4096 bytes — so treat this as success.
                    qCDebug(log_host_hid) << "MS2130S burst packet USB timeout (data transferred) – treating as success";
                } else {
                    qCWarning(log_host_hid) << "MS2130S burst packet failed. Error:" << error;
                    if (!m_inTransaction) closeHIDDeviceHandle();
                    return false;
                }
            } else {
                qCDebug(log_host_hid) << "MS2130S burst packet sent successfully";
            }
            if (!m_inTransaction) closeHIDDeviceHandle();
            return true;
        }

        // For erase-done query (0xFD), do controlled multi-attempt retry (site can be busy).
        if (bufferSize > 1 && reportBuffer[1] == 0xFD) {
            qCDebug(log_host_hid) << "MS2130S erase-done query";

            auto attemptEraseDone = [&](BYTE rid)->bool {
                QByteArray temp(bufferSize, 0);
                memcpy(temp.data(), reportBuffer, bufferSize);
                temp[0] = rid;

                const int maxErasingAttempts = 5;
                for (int attempt = 0; attempt < maxErasingAttempts; ++attempt) {
                    qCDebug(log_host_hid) << "MS2130S erase-done command attempt" << (attempt + 1)
                                          << "reportID" << (int)rid;

                    BOOL localResult = HidD_SetFeature(deviceHandle, reinterpret_cast<BYTE*>(temp.data()), bufferSize);
                    if (localResult) {
                        return true;
                    }

                    DWORD err = GetLastError();
                    qCWarning(log_host_hid) << "MS2130S erase-done attempt" << (attempt + 1)
                                            << "failed, error" << err;

                    if (err == ERROR_DEVICE_NOT_CONNECTED || err == 433 /*ERROR_NO_SUCH_DEVICE*/) {
                        qCWarning(log_host_hid) << "MS2130S device not connected during erase-done, aborting";
                        break;
                    }
                    if (err == 121 || err == 87 || err == 31) {
                        QThread::msleep(100 + attempt * 40);
                        continue;
                    }
                    break;
                }
                return false;
            };

            // Prefer report ID from reportBuffer (likely 1), then fallback to 0.
            BYTE origRid = reportBuffer[0];
            result = attemptEraseDone(origRid);
            if (!result && origRid != 0x00) {
                qCDebug(log_host_hid) << "MS2130S erase-done fallback to reportID 0";
                result = attemptEraseDone(0x00);
            }

            if (!result && origRid != 0x01) {
                qCDebug(log_host_hid) << "MS2130S erase-done fallback to reportID 1";
                result = attemptEraseDone(0x01);
            }

            if (!m_inTransaction) {
                closeHIDDeviceHandle();
            }
            return result;
        }

        // For sector erase (0xFB): the MS2130S holds the USB control transfer open during
        // flash erase.  The Windows HID class driver enforces a ~5 s per-URB timeout on
        // ALL control transfers (overlapped and sync alike), so HidD_SetFeature either
        // returns TRUE just as the erase completes, or returns ERROR_SEM_TIMEOUT (121)
        // if the erase runs slightly long.  The device is still erasing in the latter
        // case; the NEXT 0xFB command will fail with ERROR_GEN_FAILURE (31) unless we
        // wait for the erase to finish first.
        //
        // NOTE: 0xFD (erase-done query) is only valid after full-chip erase (0xFF).
        // After per-sector 0xFB the device holds 0xFD control transfers open for 5 s
        // without responding — using SetFeature(0xFD) as a readiness probe here would
        // waste 5 s per attempt.  Use HidD_GetFeature instead: it returns quickly
        // (< 200 ms) with ERROR_GEN_FAILURE (31) while the sector is erasing, and with
        // any other result once the device is idle.
        if (bufferSize >= 2 && reportBuffer[1] == 0xFB) {
            // Use hidSendFeatureNoTimeout: sector erase takes ~5-8 s hardware time.
            // HidD_SetFeature times out after exactly 5 s (ERROR_SEM_TIMEOUT=121);
            // with our own OVERLAPPED + WaitForSingleObject(30s) we wait the full
            // hardware erase time and receive TRUE — no CancelIo, no GetFeature polling.
            qCDebug(log_host_hid) << "MS2130S sector erase (0xFB) – sending (no-timeout path)";
            BOOL r = hidSendFeatureNoTimeout(deviceHandle, reportBuffer, bufferSize,
                                            30000 /*30 s max*/);
            if (r) {
                qCDebug(log_host_hid) << "MS2130S 0xFB sector erase complete";
            } else {
                DWORD err = GetLastError();
                if (err == 121 /*ERROR_SEM_TIMEOUT*/) {
                    // hidusb.sys enforces a 5 s per-URB kernel timeout.  The device DID
                    // receive the 0xFB command and is erasing; it just didn't ACK the USB
                    // STATUS stage before the host cancelled the transfer.  The device
                    // typically finishes ~200 ms after the timeout.  Poll HidD_GetFeature
                    // every 200 ms until error 31 (ERROR_GEN_FAILURE = device busy) clears.
                    // This ensures the device is fully idle before we send the next 0xFB.
                    qCDebug(log_host_hid) << "MS2130S 0xFB USB timeout – polling until device ready";
                    constexpr int kMaxPollMs    = 5000;
                    constexpr int kPollInterval =  200;
                    BYTE probe[65] = {0x01};
                    int pollWaited = 0;
                    for (; pollWaited < kMaxPollMs; pollWaited += kPollInterval) {
                        QThread::msleep(kPollInterval);
                        BOOL  pr   = HidD_GetFeature(deviceHandle, probe, sizeof(probe));
                        DWORD perr = pr ? 0 : GetLastError();
                        if (perr != ERROR_GEN_FAILURE) {
                            qCDebug(log_host_hid) << "MS2130S sector erase done after"
                                                 << (pollWaited + kPollInterval) << "ms post-timeout";
                            break;
                        }
                    }
                } else {
                    qCWarning(log_host_hid) << "MS2130S sector erase (0xFB) failed, error" << err;
                    if (!m_inTransaction) closeHIDDeviceHandle();
                    return false;
                }
            }
            if (!m_inTransaction) closeHIDDeviceHandle();
            return true;
        }

        // For burst-write init (0xE7), the device enters burst-receive mode WITHOUT sending
        // an ACK. HidD_SetFeature therefore blocks until the USB SET_FEATURE control transfer
        // times out (ERROR_SEM_TIMEOUT = 121, ~5 s). Treat that timeout as a normal/expected
        // outcome: the device did receive the command and is waiting for the data packets.
        // Any other error means the command genuinely failed.
        if (bufferSize >= 2 && reportBuffer[1] == 0xE7) {
            // Use hidSendFeatureNoTimeout so we wait as long as the device needs to
            // enter burst-receive mode — no 5 s ceiling.
            qCDebug(log_host_hid) << "MS2130S burst-write init (0xE7) – sending (no-timeout path)";
            BOOL r = hidSendFeatureNoTimeout(deviceHandle, reportBuffer, bufferSize,
                                            30000 /*30 s max*/);
            if (r) {
                qCDebug(log_host_hid) << "MS2130S 0xE7 burst init ACKed";
            } else {
                DWORD err = GetLastError();
                if (err == 121 /*ERROR_SEM_TIMEOUT*/) {
                    // Timed out after 30 s — very unusual; device may still be ready.
                    qCWarning(log_host_hid) << "MS2130S 0xE7 exceeded 30 s – proceeding";
                } else {
                    qCWarning(log_host_hid) << "MS2130S burst-write init (0xE7) failed, error" << err;
                    if (!m_inTransaction) closeHIDDeviceHandle();
                    return false;
                }
            }
            if (!m_inTransaction) closeHIDDeviceHandle();
            return true;
        }

        // For command packets (0x01 for V2/V3 path), keep report ID unchanged and retry:
        const int maxAttempts = 8;
        DWORD lastError = 0;
        bool reportIdFallbackTried = false;

        // Use local copy so we can try alternate report IDs without modifying the caller buffer.
        std::vector<BYTE> sendBuffer(reportBuffer, reportBuffer + bufferSize);
        BYTE originalReportId = sendBuffer[0];

        for (int attempt = 0; attempt < maxAttempts; ++attempt) {
            qCDebug(log_host_hid) << "MS2130S sending feature report attempt" << (attempt + 1)
                                 << "reportID" << (int)sendBuffer[0]
                                 << "cmd" << QString("0x%1").arg((int)sendBuffer[1], 2, 16, QChar('0')).toUpper()
                                 << "bufferSize" << bufferSize;

            result = HidD_SetFeature(deviceHandle, sendBuffer.data(), bufferSize);
            if (result) {
                if (bufferSize > 9) {
                    qCDebug(log_host_hid) << "MS2130S large packet HidD_SetFeature succeeded (reportID" << (int)sendBuffer[0] << ")";
                }
                if (!m_inTransaction) {
                    closeHIDDeviceHandle();
                }
                return true;
            }

            lastError = GetLastError();
            qCWarning(log_host_hid) << "MS2130S HidD_SetFeature failed (reportID" << (int)sendBuffer[0] << ") attempt" << (attempt + 1)
                                    << "error" << lastError;

            // Fatal: device is not present — no point retrying.
            if (lastError == ERROR_DEVICE_NOT_CONNECTED /*1167*/ ||
                lastError == 433                        /*ERROR_NO_SUCH_DEVICE*/ ||
                lastError == ERROR_FILE_NOT_FOUND       /*2*/) {
                qCWarning(log_host_hid) << "MS2130S device not connected (error" << lastError << "), aborting retries";
                break;
            }

            // If device not functioning, attempt alternate report ID once.
            if ((lastError == 31 || lastError == 121 || lastError == 87) && !reportIdFallbackTried) {
                BYTE altReportId = (originalReportId == 0x01) ? 0x00 : 0x01;
                if (altReportId != sendBuffer[0]) {
                    qCDebug(log_host_hid) << "MS2130S switching reportID fallback from" << (int)sendBuffer[0] << "to" << (int)altReportId;
                    sendBuffer[0] = altReportId;
                    reportIdFallbackTried = true;
                    QThread::msleep(40);
                    continue;
                }
            }

            // If busy, wait a bit and retry.
            if (lastError == 121 || lastError == 87) {
                QThread::msleep(40 + attempt * 20);
                continue;
            }

            QThread::msleep(30);
        }

        // WriteFile fallback for small packets
        if (bufferSize <= 9) {
            qCDebug(log_host_hid) << "Attempting alternative write method (WriteFile) for" << m_chipImpl->name();
            BYTE smallBuf[9] = {0};
            memcpy(smallBuf, reportBuffer, std::min<DWORD>(9, bufferSize));
            DWORD bytesWritten = 0;
            OVERLAPPED ol;
            memset(&ol, 0, sizeof(OVERLAPPED));
            if (WriteFile(deviceHandle, smallBuf, sizeof(smallBuf), &bytesWritten, &ol)) {
                qCDebug(log_host_hid) << "Alternative WriteFile method succeeded, wrote" << bytesWritten << "bytes";
                if (!m_inTransaction) {
                    closeHIDDeviceHandle();
                }
                return true;
            } else {
                lastError = GetLastError();
                qCWarning(log_host_hid) << "Alternative WriteFile method failed. Error:" << lastError;
            }
        }

        qCWarning(log_host_hid) << "MS2130S HidD_SetFeature all attempts exhausted. Last error:" << lastError;
        if (!m_inTransaction) {
            closeHIDDeviceHandle();
        }
        return false;
    }

    // Standard method as a fallback for other devices
    result = HidD_SetFeature(deviceHandle, reportBuffer, bufferSize);
    
    if (!result) {
        DWORD error = GetLastError();
        qCWarning(log_host_hid) << "Failed to send feature report. Windows error:" << error;
        
        // Try with a different approach for MS2130S
        if (m_chipImpl && m_chipImpl->type() == VideoChipType::MS2130S) {
            // Some devices require a different approach - try WriteFile
            qCDebug(log_host_hid) << "Attempting alternative method (WriteFile) for" << m_chipImpl->name();
            
            // Try with a fixed small buffer
            BYTE smallBuffer[9] = {0};
            memcpy(smallBuffer, reportBuffer, std::min<DWORD>(9, bufferSize));
            
            DWORD bytesWritten = 0;
            OVERLAPPED ol;
            memset(&ol, 0, sizeof(OVERLAPPED));
            
            result = WriteFile(deviceHandle, smallBuffer, sizeof(smallBuffer), &bytesWritten, &ol);
            
            if (result) {
                qCDebug(log_host_hid) << "Alternative write method succeeded, wrote" << bytesWritten << "bytes";
            } else {
                error = GetLastError();
                qCWarning(log_host_hid) << "Alternative write method failed. Error:" << error;
            }
        }
        
        if (!m_inTransaction) {
            closeHIDDeviceHandle();
        }
        return result;
    }

    if (!m_inTransaction) {
        closeHIDDeviceHandle();
    }
    return true;
}

bool VideoHid::openHIDDeviceHandle() {
    QMutexLocker locker(&m_deviceHandleMutex);
    
    if (deviceHandle == INVALID_HANDLE_VALUE) {
        qCDebug(log_host_hid)  << "Opening HID device handle...";
        
        // Get the full device path
        std::wstring devicePath = getHIDDevicePath();
        if (devicePath.empty()) {
            qCWarning(log_host_hid) << "Failed to get valid HID device path";
            return false;
        }
        
        QString qDevicePath = QString::fromStdWString(devicePath);
        qCDebug(log_host_hid) << "Opening device with path:" << qDevicePath;
        
        // Format check: Windows requires paths that begin with \\?\ or \\.\ for device paths
        if (!qDevicePath.startsWith("\\\\") && !qDevicePath.startsWith("\\\\.\\") && !qDevicePath.startsWith("\\\\?\\")) {
            qCWarning(log_host_hid) << "Invalid device path format, must start with \\\\.\\, \\\\?\\ or \\\\";
            return false;
        }
        
        // Open with GENERIC_READ only, matching the reference MsHidLink implementation.
        // HidD_SetFeature uses IOCTL_HID_SET_FEATURE which does not require write access.
        // Adding GENERIC_WRITE causes the HID class driver to hold the write channel
        // exclusively, preventing the device from ACKing feature report IOCTLs promptly —
        // that is why 0xFB (erase) and 0xE7 (burst init) would block for 5 s and return
        // ERROR_SEM_TIMEOUT (121) instead of completing immediately.
        deviceHandle = CreateFileW(devicePath.c_str(),
                                 GENERIC_READ,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE,
                                 NULL,
                                 OPEN_EXISTING,
                                 FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
                                 NULL);
        
        if (deviceHandle == INVALID_HANDLE_VALUE) {
            DWORD error = GetLastError();
            qCWarning(log_host_hid) << "Failed to open device handle. Error code:" << error;
            return false;
        }
    }
    // qCDebug(log_host_hid) << "Successfully opened device handle";
    return true;
}

bool VideoHid::getFeatureReportWindows(BYTE* reportBuffer, DWORD bufferSize) {
    QMutexLocker locker(&m_deviceHandleMutex);

    bool openedForOperation = m_inTransaction || openHIDDeviceHandle();

    if (!openedForOperation) {
        qCDebug(log_host_hid) << "Failed to open device handle for getting feature report.";
        return false;
    }

    // Add debug info about the report being requested
    QString hexData;
    for (DWORD i = 0; i < 4 && i < bufferSize; i++) { // First few bytes for debugging
        hexData += QString("%1 ").arg(reportBuffer[i], 2, 16, QChar('0')).toUpper();
    }
    // qCDebug(log_host_hid) << "Getting feature report with size:" << bufferSize 
    //                      << "report ID:" << (int)reportBuffer[0]
    //                      << "first bytes:" << hexData;
    
    // Get device capabilities to better understand what's supported
    PHIDP_PREPARSED_DATA preparsedData = nullptr;
    if (HidD_GetPreparsedData(deviceHandle, &preparsedData)) {
        HIDP_CAPS caps;
        if (HidP_GetCaps(preparsedData, &caps) == HIDP_STATUS_SUCCESS) {
            // qCDebug(log_host_hid) << "Device capabilities - Feature Report Byte Length:" << caps.FeatureReportByteLength
            //                      << "Input Report Byte Length:" << caps.InputReportByteLength
            //                      << "Output Report Byte Length:" << caps.OutputReportByteLength;
        }
        HidD_FreePreparsedData(preparsedData);
    }

    // Send the Get Feature Report request
    // For MS2130S devices, use a specialized approach first
    bool result = false;
    
    if (m_chipImpl && m_chipImpl->type() == VideoChipType::MS2130S) {
        qCDebug(log_host_hid) << "Using" << m_chipImpl->name() << "specific get feature report method";
        
        // Try with a fixed small buffer size that's known to work with MS2130S
        const DWORD MS2130S_BUFFER_SIZE = 64;
        BYTE* ms2130sBuffer = new BYTE[MS2130S_BUFFER_SIZE];
        memset(ms2130sBuffer, 0, MS2130S_BUFFER_SIZE);
        ms2130sBuffer[0] = reportBuffer[0]; // Copy the report ID
        
        // Use HidD_GetFeature (handles FILE_FLAG_OVERLAPPED handles correctly internally).
        result = HidD_GetFeature(deviceHandle, ms2130sBuffer, MS2130S_BUFFER_SIZE);
        DWORD bytesReturned = MS2130S_BUFFER_SIZE;
        
        if (result) {
            // qCDebug(log_host_hid) << "MS2130S-specific IOCTL get method succeeded, got" << bytesReturned << "bytes";
            // Copy the data back to the original buffer
            memcpy(reportBuffer, ms2130sBuffer, std::min<DWORD>(bufferSize, bytesReturned));
            delete[] ms2130sBuffer;
            return true;
        } else {
            DWORD error = GetLastError();
            qCWarning(log_host_hid) << "MS2130S-specific IOCTL get method failed. Error:" << error;
        }
        
        delete[] ms2130sBuffer;
    }
    
    // Standard approach as fallback
    result = HidD_GetFeature(deviceHandle, reportBuffer, bufferSize);

    if (!result) {
        DWORD error = GetLastError();
        qCWarning(log_host_hid) << "Failed to get feature report. Windows error:" << error;

        // Try a report ID fallback for MS2130S
        if (m_chipImpl && m_chipImpl->type() == VideoChipType::MS2130S) {
            qCDebug(log_host_hid) << "MS2130S get feature report fallback: trying alternate report IDs";
            for (BYTE rid : { (BYTE)0x00, (BYTE)0x01 }) {
                QByteArray tmp(reinterpret_cast<char*>(reportBuffer), bufferSize);
                tmp[0] = static_cast<char>(rid);
                BOOL got = HidD_GetFeature(deviceHandle, reinterpret_cast<BYTE*>(tmp.data()), bufferSize);
                if (got) {
                    qCDebug(log_host_hid) << "MS2130S get feature report succeeded with reportID" << (int)rid;
                    memcpy(reportBuffer, tmp.data(), bufferSize);
                    if (!m_inTransaction) {
                        closeHIDDeviceHandle();
                    }
                    return true;
                }
                DWORD e = GetLastError();
                qCWarning(log_host_hid) << "MS2130S get feature report fallback reportID" << (int)rid << "failed. Error:" << e;
            }

            // Try ReadFile approach as last resort for MIS behaviors
            qCDebug(log_host_hid) << "Attempting alternative read method for" << m_chipImpl->name() << "get feature report";
            const DWORD SAFE_BUFFER_SIZE = 64;
            DWORD bytesRead = 0;
            OVERLAPPED ol;
            memset(&ol, 0, sizeof(OVERLAPPED));
            BYTE* readBuffer = new BYTE[SAFE_BUFFER_SIZE];
            memset(readBuffer, 0, SAFE_BUFFER_SIZE);
            readBuffer[0] = reportBuffer[0];
            result = ReadFile(deviceHandle, readBuffer, SAFE_BUFFER_SIZE, &bytesRead, &ol);
            if (result) {
                qCDebug(log_host_hid) << "Alternative read method succeeded, read" << bytesRead << "bytes";
                memcpy(reportBuffer, readBuffer, std::min<DWORD>(bufferSize, bytesRead));
            } else {
                error = GetLastError();
                qCWarning(log_host_hid) << "Alternative read method failed. Error:" << error;
            }
            delete[] readBuffer;
        }
    }

    if (!m_inTransaction) {
        closeHIDDeviceHandle();
    }

    return result;
}

#elif __linux__
QString VideoHid::getHIDDevicePath() {
    QString portChain = GlobalSetting::instance().getOpenterfacePortChain();
    QString hidPath = findMatchingHIDDevice(portChain);
    
    if (!hidPath.isEmpty()) {
        return hidPath;
    }
    
    // Fallback to original device name enumeration method
    qCDebug(log_host_hid) << "Falling back to device name enumeration for HID device discovery";
    
    QDir dir("/sys/class/hidraw");

    QStringList hidrawDevices = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);

    if (hidrawDevices.isEmpty()) {
        qCDebug(log_host_hid)  << "No Openterface device found.";
        
        return QString();
    }

    foreach (const QString &device, hidrawDevices) {\
        QString devicePath = "/sys/class/hidraw/" + device + "/device/uevent";

        QFile file(devicePath);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&file);
            
            while (!in.atEnd()) {
                QString line = in.readLine();
                qCDebug(log_host_hid)  << "Line: " << line;
                if (line.isEmpty()) {
                    break;
                }                    if (line.contains("HID_NAME")) {
                        // Check if this is the device you are looking for
                        if (line.contains("Openterface") || line.contains("MACROSILICON")) {
                            QString foundPath = "/dev/" + device;
                            
                            return foundPath;
                        }
                    }
            }
        } else {
            qCDebug(log_host_hid)  << "Failed to open device path: " << devicePath;
        }   
    }

    return QString();
}

// Open the HID device and cache the file descriptor
bool VideoHid::openHIDDevice() {
    if (hidFd < 0) {
        QString devicePath = getHIDDevicePath();
        hidFd = open(devicePath.toStdString().c_str(), O_RDWR);
        if (hidFd < 0) {
            qCDebug(log_host_hid)  << "Failed to open HID device (" << devicePath << "). Error:" << strerror(errno);
            return false;
        }
    }
    return true;
}

bool VideoHid::sendFeatureReportLinux(uint8_t* reportBuffer, int bufferSize) {
    bool openedForOperation = m_inTransaction || openHIDDevice();
    
    if (!openedForOperation) {
        return false;
    }

    std::vector<uint8_t> buffer(bufferSize);
    std::copy(reportBuffer, reportBuffer + bufferSize, buffer.begin());
    int res = ioctl(hidFd, HIDIOCSFEATURE(buffer.size()), buffer.data());

    if (res < 0) {
        qCDebug(log_host_hid)  << "Failed to send feature report. Error:" << strerror(errno);
        return false;
    }

    if (!m_inTransaction) {
        closeHIDDeviceHandle();
    }

    return true;
}

bool VideoHid::getFeatureReportLinux(uint8_t* reportBuffer, int bufferSize) {
    bool openedForOperation = m_inTransaction || openHIDDevice();
    
    if (!openedForOperation) {
        return false;
    }

    std::vector<uint8_t> buffer(bufferSize);
    int res = ioctl(hidFd, HIDIOCGFEATURE(buffer.size()), buffer.data());

    if (res < 0) {
        qCDebug(log_host_hid)  << "Failed to get feature report. Error:" << strerror(errno);
        return false;
    }

    std::copy(buffer.begin(), buffer.end(), reportBuffer);

    if (!m_inTransaction) {
        closeHIDDeviceHandle();
    }
    return true;
}
#endif

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

    if (m_chipType == VideoChipType::MS2130S) {
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
        if (m_chipType != VideoChipType::MS2130S) {
            qCDebug(log_host_hid) << "writeEeprom: restarting polling thread after EEPROM update";
            start();
        } else {
            qCInfo(log_host_hid) << "writeEeprom: NOT restarting polling – MS2130S needs power cycle after flash";
        }
    }

    return success;
}


bool VideoHid::ms2130sEraseSector(quint32 startAddress) {
    // Use HidD_SetFeature directly, matching the reference mshidlink_flash_erase_sector()
    // exactly.  The device holds the USB STATUS stage until the erase completes, so
    // HidD_SetFeature blocks for the full erase duration (~50-300 ms per sector).
#ifdef _WIN32
    QMutexLocker locker(&m_deviceHandleMutex);
    if (deviceHandle == INVALID_HANDLE_VALUE) {
        qCWarning(log_host_hid) << "MS2130S sector erase: device handle invalid";
        return false;
    }

    BYTE ctrlData[9] = {0};
    ctrlData[0] = (m_ms2130sConnectMode == 1) ? 0x00 : 0x01;
    ctrlData[1] = 0xFB;
    ctrlData[2] = (BYTE)((startAddress >> 16) & 0xFF);
    ctrlData[3] = (BYTE)((startAddress >> 8) & 0xFF);
    ctrlData[4] = (BYTE)(startAddress & 0xFF);

    BOOL ret = HidD_SetFeature(deviceHandle, ctrlData, 9);
    if (!ret) {
        DWORD err = GetLastError();
        qCWarning(log_host_hid) << "MS2130S sector erase failed at"
                                << QString("0x%1").arg(startAddress, 8, 16, QChar('0'))
                                << "error" << err;
        return false;
    }
    qCDebug(log_host_hid) << "MS2130S sector erase complete at"
                          << QString("0x%1").arg(startAddress, 8, 16, QChar('0'));
    return true;
#else
    QByteArray ctrlData(9, 0);
    ctrlData[0] = 0x01;
    ctrlData[1] = 0xFB;
    ctrlData[2] = static_cast<char>((startAddress >> 16) & 0xFF);
    ctrlData[3] = static_cast<char>((startAddress >> 8) & 0xFF);
    ctrlData[4] = static_cast<char>(startAddress & 0xFF);

    if (!sendFeatureReport((uint8_t*)ctrlData.data(), ctrlData.size())) {
        qCWarning(log_host_hid) << "MS2130S sector erase failed at"
                                << QString("0x%1").arg(startAddress, 8, 16, QChar('0'));
        return false;
    }
    qCDebug(log_host_hid) << "MS2130S sector erase complete at"
                          << QString("0x%1").arg(startAddress, 8, 16, QChar('0'));
    return true;
#endif
}

bool VideoHid::ms2130sFlashEraseDone(bool &done) {
    // Mirror MsHidLink::mshidlink_flash_erase_done exactly:
    // Use the SAME 9-byte buffer for both HidD_SetFeature and HidD_GetFeature.
    // Routing through the abstract sendFeatureReport/getFeatureReport wrappers causes
    // getFeatureReportWindows to use a 64-byte intermediate buffer and a different code
    // path that returns the command bytes unchanged (byte[2] stays 0xFD forever).
    QMutexLocker locker(&m_deviceHandleMutex);

    bool openedForOperation = m_inTransaction || openHIDDeviceHandle();
    if (!openedForOperation) {
        qCWarning(log_host_hid) << "MS2130S erase-done: failed to open device handle";
        done = false;
        return true; // keep polling
    }

    BYTE ctrlData[9] = { 0 };
    ctrlData[0] = 0x01;
    ctrlData[1] = 0xFD;
    ctrlData[2] = 0xFD;

    BOOL setOk = HidD_SetFeature(deviceHandle, ctrlData, 9);
    if (!setOk) {
        qCWarning(log_host_hid) << "MS2130S erase-done HidD_SetFeature failed, error" << GetLastError();
        if (!m_inTransaction) closeHIDDeviceHandle();
        done = false;
        return true; // keep polling
    }

    BOOL getOk = HidD_GetFeature(deviceHandle, ctrlData, 9);
    if (!getOk) {
        qCWarning(log_host_hid) << "MS2130S erase-done HidD_GetFeature failed, error" << GetLastError();
        if (!m_inTransaction) closeHIDDeviceHandle();
        done = false;
        return true; // keep polling
    }

    if (!m_inTransaction) closeHIDDeviceHandle();

    quint8 statusByte = static_cast<quint8>(ctrlData[2]);
    done = (statusByte == 0x00);
    qCDebug(log_host_hid) << "MS2130S erase-done status byte[2]:"
                          << QString("0x%1").arg(statusByte, 2, 16, QChar('0')).toUpper()
                          << "done=" << done;
    return true;
}

bool VideoHid::ms2130sFlashBurstWrite(quint32 address, const QByteArray &data) {
    if (data.isEmpty()) return false;

    // CRITICAL: The entire burst sequence (0xE7 init + all 0x03 data packets) must be
    // atomic — no other HID command may be interleaved.  Hold the mutex for the entire
    // operation.
    //
    // Use HidD_SetFeature for both 0xE7 init and 0x03 data packets, exactly matching
    // the reference mshidlink_flash_burst_write() implementation.  HidD_SetFeature
    // passes lpOutBuffer=NULL to DeviceIoControl, which is critical — our earlier
    // hidSendFeatureNoTimeout passed lpOutBuffer=buf (creating an MDL), which may cause
    // the HID/USB driver stack to behave differently with METHOD_IN_DIRECT IOCTLs.
#ifdef _WIN32
    QMutexLocker locker(&m_deviceHandleMutex);
    if (deviceHandle == INVALID_HANDLE_VALUE) {
        qCWarning(log_host_hid) << "MS2130S burst write: device handle invalid";
        return false;
    }

    if (m_ms2130sConnectMode == 1) {
        BYTE ctrlData[9] = {0};
        ctrlData[0] = 0x00;
        ctrlData[1] = 0xF8;
        ctrlData[2] = 1;
        ctrlData[3] = (BYTE)((address >> 16) & 0xFF);
        ctrlData[4] = (BYTE)((address >> 8) & 0xFF);
        ctrlData[5] = (BYTE)(address & 0xFF);
        UINT16 u16_length = static_cast<UINT16>(data.size());
        ctrlData[6] = (BYTE)((u16_length >> 8) & 0xFF);
        ctrlData[7] = (BYTE)(u16_length & 0xFF);

        if (!HidD_SetFeature(deviceHandle, ctrlData, 9)) {
            qCWarning(log_host_hid) << "MS2130S V1 burst write init failed, error" << GetLastError();
            return false;
        }

        for (UINT16 offset = 0; offset < u16_length; offset = static_cast<UINT16>(offset + 6)) {
            BYTE packet[9] = {0};
            packet[0] = 0x00;
            packet[1] = 0xF8;
            packet[2] = 0;
            const UINT16 chunkLen = static_cast<UINT16>(qMin<int>(6, u16_length - offset));
            memcpy(&packet[3], data.constData() + offset, chunkLen);
            if (offset == 0) {
                QString hexDump;
                for (int k = 0; k < 9; ++k) {
                    hexDump += QString("%1 ").arg(packet[k], 2, 16, QChar('0'));
                }
                qCInfo(log_host_hid) << "MS2130S V1 burst write first packet:" << hexDump.trimmed();
            }
            if (!HidD_SetFeature(deviceHandle, packet, 9)) {
                qCWarning(log_host_hid) << "MS2130S V1 burst write data packet failed at offset"
                                        << offset << "error" << GetLastError();
                return false;
            }
            written_size += chunkLen;
            emit firmwareWriteChunkComplete(written_size);
        }
        return true;
    }

    // Step 1: Send 0xE7 burst write init via HidD_SetFeature (matching reference)
    BYTE ctrlData[9] = {0};
    ctrlData[0] = 0x01;
    ctrlData[1] = 0xE7;
    ctrlData[2] = (BYTE)((address >> 24) & 0xFF);
    ctrlData[3] = (BYTE)((address >> 16) & 0xFF);
    ctrlData[4] = (BYTE)((address >> 8) & 0xFF);
    ctrlData[5] = (BYTE)(address & 0xFF);
    UINT16 u16_length = static_cast<UINT16>(data.size());
    ctrlData[6] = (BYTE)((u16_length >> 8) & 0xFF);
    ctrlData[7] = (BYTE)(u16_length & 0xFF);

    BOOL ret = HidD_SetFeature(deviceHandle, ctrlData, 9);
    if (!ret) {
        DWORD err = GetLastError();
        qCWarning(log_host_hid) << "MS2130S burst write 0xE7 init failed, error" << err;
        return false;
    }

    // Step 2: Send data in 4096-byte packets (0x03 + 4095 data) via HidD_SetFeature.
    BYTE buffer[4096] = { 0x03 };
    const UINT16 u16_bufferSize = 4095;

    int cnt = u16_length / u16_bufferSize;
    int left = u16_length % u16_bufferSize;
    int j = 0;

    for (j = 0; j < cnt; j++) {
        memcpy(&buffer[1], data.constData() + static_cast<qsizetype>(u16_bufferSize) * j, u16_bufferSize);
        // Log first packet content to verify data integrity
        if (j == 0) {
            QString hexDump;
            for (int k = 0; k < 17 && k < (int)(u16_bufferSize + 1); ++k) {
                hexDump += QString("%1 ").arg(buffer[k], 2, 16, QChar('0'));
            }
            qCInfo(log_host_hid) << "MS2130S burst write first 0x03 packet (17 bytes):" << hexDump.trimmed();
        }
        ret = HidD_SetFeature(deviceHandle, buffer, u16_bufferSize + 1);
        if (!ret) {
            qCWarning(log_host_hid) << "MS2130S burst write data chunk" << j << "failed, error" << GetLastError();
            return false;
        }
        written_size += u16_bufferSize;
        emit firmwareWriteChunkComplete(written_size);
    }
    if ((left != 0)) {
        memcpy(&buffer[1], data.constData() + static_cast<qsizetype>(u16_bufferSize) * j, left);
        ret = HidD_SetFeature(deviceHandle, buffer, u16_bufferSize + 1);
        if (!ret) {
            qCWarning(log_host_hid) << "MS2130S burst write final chunk failed, error" << GetLastError();
            return false;
        }
        written_size += left;
        emit firmwareWriteChunkComplete(written_size);
    }

    return true;
#else
    // Non-Windows fallback: use sendFeatureReport (original path)
    QByteArray cmd(9, 0);
    cmd[0] = 0x01;
    cmd[1] = 0xE7;
    cmd[2] = static_cast<char>((address >> 24) & 0xFF);
    cmd[3] = static_cast<char>((address >> 16) & 0xFF);
    cmd[4] = static_cast<char>((address >> 8) & 0xFF);
    cmd[5] = static_cast<char>(address & 0xFF);
    quint16 length16 = static_cast<quint16>(data.size());
    cmd[6] = static_cast<char>((length16 >> 8) & 0xFF);
    cmd[7] = static_cast<char>(length16 & 0xFF);
    if (!sendFeatureReport((uint8_t*)cmd.data(), cmd.size())) {
        qCWarning(log_host_hid) << "MS2130S burst write init failed";
        return false;
    }
    const quint32 chunkData = 4095;
    const quint32 packetSize = 4096;
    quint32 written = 0;
    quint32 total = static_cast<quint32>(data.size());
    QByteArray packet(static_cast<int>(packetSize), 0);
    packet[0] = 0x03;
    while (written < total) {
        quint32 block = qMin(chunkData, total - written);
        memcpy(packet.data() + 1, data.constData() + written, block);
        if (block < chunkData)
            memset(packet.data() + 1 + block, 0, chunkData - block);
        if (!sendFeatureReport((uint8_t*)packet.data(), static_cast<size_t>(packetSize))) {
            qCWarning(log_host_hid) << "MS2130S burst write chunk failed at" << written << "bytes";
            return false;
        }
        written += block;
        written_size = written;
        emit firmwareWriteChunkComplete(written_size);
    }
    return true;
#endif
}

bool VideoHid::ms2130sFlashBurstRead(quint32 address, quint32 length, QByteArray &outData) {
    // Mirrors mshidlink_flash_burst_read V2/V3 mode from the reference C++ implementation.
    // CRITICAL: The entire burst sequence (0xE7 init + all 0x03 data reads) must be
    // atomic — same as burst write.  Hold the mutex for the entire operation.
    // Use HidD_SetFeature for the 0xE7 init, matching the reference exactly.
    outData.clear();
    if (length == 0) return true;

#ifdef _WIN32
    QMutexLocker locker(&m_deviceHandleMutex);
    if (deviceHandle == INVALID_HANDLE_VALUE) {
        qCWarning(log_host_hid) << "MS2130S burst read: device handle invalid";
        return false;
    }
    
    // Step 1: Send 0xE7 burst read init via HidD_SetFeature (matching reference)
    BYTE ctrlData[9] = {0};
    ctrlData[0] = 0x01;
    ctrlData[1] = 0xE7;
    ctrlData[2] = (BYTE)((address >> 24) & 0xFF);
    ctrlData[3] = (BYTE)((address >> 16) & 0xFF);
    ctrlData[4] = (BYTE)((address >> 8) & 0xFF);
    ctrlData[5] = (BYTE)(address & 0xFF);
    UINT16 u16_length = static_cast<UINT16>(qMin(length, quint32(0xFFFF)));
    ctrlData[6] = (BYTE)((u16_length >> 8) & 0xFF);
    ctrlData[7] = (BYTE)(u16_length & 0xFF);

    BOOL ret = HidD_SetFeature(deviceHandle, ctrlData, 9);
    if (!ret) {
        DWORD err = GetLastError();
        qCWarning(log_host_hid) << "MS2130S burst read 0xE7 init failed, error" << err;
        return false;
    }

    // Step 2: Read data via HidD_GetFeature, matching reference EXACTLY.
    // CRITICAL: buffer must be initialized OUTSIDE the loop, matching reference implementation!
    BYTE buffer[4096] = { 0x03 };  // buffer[0]=0x03, rest=0, initialized ONCE
    const UINT16 u16_bufferSize = 4095;
    const UINT16 packetSize = 4096;
    outData.reserve(static_cast<int>(length));

    auto getPacketWithFallback = [&](BYTE &headerOut) -> bool {
        const BYTE tryIds[] = {0x03, 0x00, 0x01};
        for (BYTE rid : tryIds) {
            buffer[0] = rid;
            BOOL ok = HidD_GetFeature(deviceHandle, buffer, packetSize);
            if (!ok) {
                continue;
            }
            headerOut = buffer[0];
            if (headerOut == 0x03) {
                if (rid != 0x03) {
                    qCWarning(log_host_hid) << "MS2130S burst read recovered with request report id"
                                            << QString("0x%1").arg(rid, 2, 16, QChar('0'));
                }
                return true;
            }
        }
        return false;
    };

    int cnt = u16_length / u16_bufferSize;
    int left = u16_length % u16_bufferSize;
    int j = 0;

    // Read complete 4095-byte chunks
    for (j = 0; j < cnt; j++) {
        BYTE header = 0x00;
        bool ok = getPacketWithFallback(header);
        if (!ok) {
            DWORD err = GetLastError();
            qCWarning(log_host_hid) << "MS2130S burst read chunk" << j 
                                    << "/" << cnt << "failed, error" << err;
            return false;
        }
        if (header != 0x03) {
            qCWarning(log_host_hid) << "MS2130S burst read chunk" << j
                                    << "unexpected report id/header"
                                    << QString("0x%1").arg(header, 2, 16, QChar('0'));
            return false;
        }

        // Log first packet for debugging
        if (j == 0) {
            QString hexDump;
            for (int k = 0; k < 17; ++k) {
                hexDump += QString("%1 ").arg(buffer[k], 2, 16, QChar('0'));
            }
            qCInfo(log_host_hid) << "MS2130S burst read first packet (17 bytes):" 
                                 << hexDump.trimmed();
        }

        // Copy exactly 4095 bytes of data (skip buffer[0] which is report ID)
        outData.append(reinterpret_cast<const char*>(buffer + 1), u16_bufferSize);
    }

    // Read final partial chunk if any
    if (left != 0) {
        BYTE header = 0x00;
        bool ok = getPacketWithFallback(header);
        if (!ok) {
            DWORD err = GetLastError();
            qCWarning(log_host_hid) << "MS2130S burst read final chunk failed, error" << err;
            return false;
        }
        if (header != 0x03) {
            qCWarning(log_host_hid) << "MS2130S burst read final chunk unexpected report id/header"
                                    << QString("0x%1").arg(header, 2, 16, QChar('0'));
            return false;
        }
        // Copy only the remaining bytes needed
        outData.append(reinterpret_cast<const char*>(buffer + 1), left);
    }

    quint32 totalRead = static_cast<quint32>(outData.size());
    qCDebug(log_host_hid) << "MS2130S burst read complete:" << totalRead << "bytes from"
                          << QString("0x%1").arg(address, 8, 16, QChar('0'));
    return true;
#else
    // Non-Windows fallback: use sendFeatureReport for 0xE7 init
    QByteArray cmd(9, 0);
    cmd[0] = 0x01;
    cmd[1] = 0xE7;
    cmd[2] = static_cast<char>((address >> 24) & 0xFF);
    cmd[3] = static_cast<char>((address >> 16) & 0xFF);
    cmd[4] = static_cast<char>((address >> 8) & 0xFF);
    cmd[5] = static_cast<char>(address & 0xFF);
    quint16 len16 = static_cast<quint16>(qMin(length, quint32(0xFFFF)));
    cmd[6] = static_cast<char>((len16 >> 8) & 0xFF);
    cmd[7] = static_cast<char>(len16 & 0xFF);
    if (!sendFeatureReport((uint8_t*)cmd.data(), cmd.size())) {
        qCWarning(log_host_hid) << "MS2130S burst read init (0xE7) failed";
        return false;
    }
    qCWarning(log_host_hid) << "MS2130S burst read not implemented for non-Windows";
    return false;
#endif
}

int VideoHid::ms2130sDetectConnectMode() {
    // Mirrors _check_connect_mode() from the reference MsHidLink.cpp.
    // The reference probes V1 first and only falls back to V2/V3 if that fails.
    // That ordering is important because V1 uses different report IDs and flash opcodes.

#ifdef _WIN32
    QMutexLocker locker(&m_deviceHandleMutex);
    if (deviceHandle == INVALID_HANDLE_VALUE) {
        qCWarning(log_host_hid) << "MS2130S connect mode detection: device handle invalid";
        return 0;
    }

    // Log device caps for diagnostics
    PHIDP_PREPARSED_DATA preparsedData = nullptr;
    if (HidD_GetPreparsedData(deviceHandle, &preparsedData)) {
        HIDP_CAPS caps;
        if (HidP_GetCaps(preparsedData, &caps) == HIDP_STATUS_SUCCESS) {
            qCInfo(log_host_hid) << "MS2130S HID caps: FeatureReportByteLength="
                                 << caps.FeatureReportByteLength
                                 << "InputReportByteLength=" << caps.InputReportByteLength
                                 << "OutputReportByteLength=" << caps.OutputReportByteLength;
        }
        HidD_FreePreparsedData(preparsedData);
    }

    BYTE setData[9] = {0x00, 0xB5, 0xFF, 0x01, 0, 0, 0, 0, 0};
    BYTE getData[9] = {0};
    if (HidD_SetFeature(deviceHandle, setData, 9)) {
        setData[1] = 0xFC;
        setData[2] = 0x01;
        if (HidD_SetFeature(deviceHandle, setData, 9)) {
            Sleep(100);
            setData[0] = 0x00;
            setData[1] = 0xB5;
            setData[2] = 0xFF;
            setData[3] = 0x01;
            if (HidD_SetFeature(deviceHandle, setData, 9)) {
                getData[0] = 0x00;
                if (HidD_GetFeature(deviceHandle, getData, 9)) {
                    QString hexRecv;
                    for (int i = 0; i < 9; ++i) {
                        hexRecv += QString("%1 ").arg(getData[i], 2, 16, QChar('0'));
                    }
                    qCInfo(log_host_hid) << "MS2130S connect mode V1 probe recv =" << hexRecv.trimmed();
                    if (getData[4] == 0x13) {
                        qCInfo(log_host_hid) << "MS2130S connect mode: V1";
                        return 1;
                    }
                }
            }
        }
    } else {
        qCDebug(log_host_hid) << "MS2130S V1 probe SetFeature failed, falling back to V2/V3, error" << GetLastError();
    }

    memset(setData, 0, sizeof(setData));
    memset(getData, 0, sizeof(getData));
    setData[0] = 0x01;
    setData[1] = 0xB5;
    setData[2] = 0xFF;
    setData[3] = 0x01;
    if (!HidD_SetFeature(deviceHandle, setData, 9)) {
        qCWarning(log_host_hid) << "MS2130S connect mode detection: V2/V3 SetFeature failed, error" << GetLastError();
        return 0;
    }

    getData[0] = 0x01;
    if (!HidD_GetFeature(deviceHandle, getData, 9)) {
        qCWarning(log_host_hid) << "MS2130S connect mode detection: V2/V3 GetFeature failed, error" << GetLastError();
        return 0;
    }

    QString hexSent, hexRecv;
    for (int i = 0; i < 9; ++i) {
        hexSent += QString("%1 ").arg(setData[i], 2, 16, QChar('0'));
        hexRecv += QString("%1 ").arg(getData[i], 2, 16, QChar('0'));
    }
    qCInfo(log_host_hid) << "MS2130S connect mode V2/V3 probe sent =" << hexSent.trimmed();
    qCInfo(log_host_hid) << "MS2130S connect mode V2/V3 probe recv =" << hexRecv.trimmed();

    for (int i = 0; i < 4; ++i) {
        if (setData[i] != getData[i]) {
            qCInfo(log_host_hid) << "MS2130S connect mode: V2 (byte" << i << "differs)";
            return 2;
        }
    }
    qCInfo(log_host_hid) << "MS2130S connect mode: V3 (bytes 0-3 match)";
    return 3;
#else
    // Non-Windows: use wrapper functions
    QByteArray setData(9, 0);
    setData[0] = 0x01;
    setData[1] = static_cast<char>(0xB5);
    setData[2] = static_cast<char>(0xFF);
    setData[3] = 0x01;
    if (!sendFeatureReport(reinterpret_cast<uint8_t*>(setData.data()), setData.size())) {
        qCWarning(log_host_hid) << "MS2130S connect mode detection: SetFeature failed";
        return 0;
    }
    QByteArray getData(9, 0);
    getData[0] = 0x01;
    if (!getFeatureReport(reinterpret_cast<uint8_t*>(getData.data()), getData.size())) {
        qCWarning(log_host_hid) << "MS2130S connect mode detection: GetFeature failed";
        return 0;
    }
    QString hexSent, hexRecv;
    for (int i = 0; i < 9; ++i) {
        hexSent += QString("%1 ").arg(static_cast<quint8>(setData[i]), 2, 16, QChar('0'));
        hexRecv += QString("%1 ").arg(static_cast<quint8>(getData[i]), 2, 16, QChar('0'));
    }
    qCInfo(log_host_hid) << "MS2130S connect mode detection: sent =" << hexSent.trimmed();
    qCInfo(log_host_hid) << "MS2130S connect mode detection: recv =" << hexRecv.trimmed();
    for (int i = 0; i < 4; ++i) {
        if (setData[i] != getData[i]) {
            qCInfo(log_host_hid) << "MS2130S connect mode: V2 (byte" << i << "differs)";
            return 2;
        }
    }
    qCInfo(log_host_hid) << "MS2130S connect mode: V3 (bytes 0-3 match)";
    return 3;
#endif
}

bool VideoHid::ms2130sInitializeGPIO() {
    // Mirrors mshidlink_open_device() GPIO init from the reference MsHidLink.cpp.
    // Configures GPIO registers so the SPI flash subsystem is accessible.
    //
    // CRITICAL: On Windows, use DIRECT HidD_SetFeature/HidD_GetFeature calls — no
    // wrappers.  The sendFeatureReportWindows/getFeatureReportWindows wrappers have
    // complex retry logic, report ID fallback, and intermediate buffers that may
    // cause GPIO register writes to silently fail — leading to burst read/write
    // operating on MCU XDATA instead of SPI flash.
    qCDebug(log_host_hid) << "MS2130S initializing GPIO for flash operations...";

    // Detect the actual connect mode (uses its own mutex locking)
    if (m_ms2130sConnectMode == 0) {
        m_ms2130sConnectMode = ms2130sDetectConnectMode();
        if (m_ms2130sConnectMode == 0) {
            qCWarning(log_host_hid) << "MS2130S connect mode detection failed";
            return false;
        }
    }
    const int mode = m_ms2130sConnectMode;
    const quint8 reportId = (mode == 1) ? 0x00 : 0x01;
    const int read8_offset  = (mode == 2) ? 2 : 3;
    const int read16_offset = (mode == 2) ? 3 : 4;

#ifdef _WIN32
    QMutexLocker locker(&m_deviceHandleMutex);
    if (deviceHandle == INVALID_HANDLE_VALUE) {
        qCWarning(log_host_hid) << "MS2130S GPIO init: device handle invalid";
        return false;
    }

    // Direct register access — matching reference mshidlink_open_device() exactly.
    // All operations use HidD_SetFeature/HidD_GetFeature directly, no wrappers.
    auto directWrite8 = [&](BYTE addr, BYTE value) -> bool {
        BYTE buf[9] = {reportId, 0xC6, addr, value, 0, 0, 0, 0, 0};
        BOOL ok = HidD_SetFeature(deviceHandle, buf, 9);
        if (!ok) {
            qCWarning(log_host_hid) << "MS2130S GPIO direct write8 failed: addr="
                                    << QString("0x%1").arg(addr, 2, 16, QChar('0'))
                                    << "value=" << QString("0x%1").arg(value, 2, 16, QChar('0'))
                                    << "error=" << GetLastError();
        }
        return ok;
    };

    auto directRead8 = [&](BYTE addr, BYTE &value) -> bool {
        BYTE setBuf[9] = {reportId, 0xC5, addr, 0, 0, 0, 0, 0, 0};
        if (!HidD_SetFeature(deviceHandle, setBuf, 9)) {
            qCWarning(log_host_hid) << "MS2130S GPIO direct read8 SetFeature failed: addr="
                                    << QString("0x%1").arg(addr, 2, 16, QChar('0'))
                                    << "error=" << GetLastError();
            return false;
        }
        BYTE getBuf[9] = {};
        getBuf[0] = reportId;
        if (!HidD_GetFeature(deviceHandle, getBuf, 9)) {
            qCWarning(log_host_hid) << "MS2130S GPIO direct read8 GetFeature failed: addr="
                                    << QString("0x%1").arg(addr, 2, 16, QChar('0'))
                                    << "error=" << GetLastError();
            return false;
        }
        value = getBuf[read8_offset];
        return true;
    };

    auto directWrite16 = [&](UINT16 addr, BYTE value) -> bool {
        BYTE buf[9] = {reportId, 0xB6, (BYTE)((addr>>8)&0xFF), (BYTE)(addr&0xFF), value, 0, 0, 0, 0};
        BOOL ok = HidD_SetFeature(deviceHandle, buf, 9);
        if (!ok) {
            qCWarning(log_host_hid) << "MS2130S GPIO direct write16 failed: addr="
                                    << QString("0x%1").arg(addr, 4, 16, QChar('0'))
                                    << "error=" << GetLastError();
        }
        return ok;
    };

    auto directRead16 = [&](UINT16 addr, BYTE &value) -> bool {
        BYTE setBuf[9] = {reportId, 0xB5, (BYTE)((addr>>8)&0xFF), (BYTE)(addr&0xFF), 0, 0, 0, 0, 0};
        if (!HidD_SetFeature(deviceHandle, setBuf, 9)) {
            qCWarning(log_host_hid) << "MS2130S GPIO direct read16 SetFeature failed: addr="
                                    << QString("0x%1").arg(addr, 4, 16, QChar('0'))
                                    << "error=" << GetLastError();
            return false;
        }
        BYTE getBuf[9] = {};
        getBuf[0] = reportId;
        if (!HidD_GetFeature(deviceHandle, getBuf, 9)) {
            qCWarning(log_host_hid) << "MS2130S GPIO direct read16 GetFeature failed: addr="
                                    << QString("0x%1").arg(addr, 4, 16, QChar('0'))
                                    << "error=" << GetLastError();
            return false;
        }
        value = getBuf[read16_offset];
        return true;
    };

    // Step 1: 0xB0 — clear bit 2 (SPI CS control)
    BYTE b0Val = 0;
    if (!directRead8(0xB0, b0Val)) { qCWarning(log_host_hid) << "MS2130S GPIO init: failed to read 0xB0"; return false; }
    m_gpio_saved_b0 = b0Val;
    BYTE b0New = b0Val & ~0x04;
    if (!directWrite8(0xB0, b0New)) { qCWarning(log_host_hid) << "MS2130S GPIO init: failed to write 0xB0"; return false; }
    BYTE b0Verify = 0;
    directRead8(0xB0, b0Verify);
    qCInfo(log_host_hid) << "MS2130S GPIO: 0xB0 saved=" << QString("0x%1").arg(b0Val, 2, 16, QChar('0'))
                         << "wrote=" << QString("0x%1").arg(b0New, 2, 16, QChar('0'))
                         << "readback=" << QString("0x%1").arg(b0Verify, 2, 16, QChar('0'))
                         << (b0Verify == b0New ? "OK" : "MISMATCH!");

    // Step 2: 0xA0 — set bit 2 (SPI CS direction)
    BYTE a0Val = 0;
    if (!directRead8(0xA0, a0Val)) { qCWarning(log_host_hid) << "MS2130S GPIO init: failed to read 0xA0"; return false; }
    m_gpio_saved_a0 = a0Val;
    BYTE a0New = a0Val | 0x04;
    if (!directWrite8(0xA0, a0New)) { qCWarning(log_host_hid) << "MS2130S GPIO init: failed to write 0xA0"; return false; }
    BYTE a0Verify = 0;
    directRead8(0xA0, a0Verify);
    qCInfo(log_host_hid) << "MS2130S GPIO: 0xA0 saved=" << QString("0x%1").arg(a0Val, 2, 16, QChar('0'))
                         << "wrote=" << QString("0x%1").arg(a0New, 2, 16, QChar('0'))
                         << "readback=" << QString("0x%1").arg(a0Verify, 2, 16, QChar('0'))
                         << (a0Verify == a0New ? "OK" : "MISMATCH!");

    // Step 3: 0xC7 — SPI clock config = 0xD1
    // Note: Like MsHidLink, we write SPI config registers without strict verification.
    // Some registers may return hardware status (not config shadow) on read.
    directRead8(0xC7, m_gpio_saved_c7);
    if (!directWrite8(0xC7, 0xD1)) { qCWarning(log_host_hid) << "MS2130S GPIO init: failed to write 0xC7"; return false; }
    BYTE c7Verify = 0;
    directRead8(0xC7, c7Verify);
    qCInfo(log_host_hid) << "MS2130S GPIO: 0xC7 saved=" << QString("0x%1").arg(m_gpio_saved_c7, 2, 16, QChar('0'))
                         << "wrote=0xd1 readback=" << QString("0x%1").arg(c7Verify, 2, 16, QChar('0'))
                         << (c7Verify == 0xD1 ? "OK" : "INFO-only");

    // Step 4: 0xC8 — SPI mode config = 0xC0
    // CRITICAL INSIGHT: MsHidLink.cpp does NOT verify this register write!
    // Testing shows 0xC8 reads back as 0x05 even after writing 0xC0, BUT flash ops work correctly.
    // HYPOTHESIS: 0xC8 read returns hardware status, not the config shadow register value.
    // The write IS taking effect (enabling SPI mode), but the readback shows different info.
    directRead8(0xC8, m_gpio_saved_c8);
    if (!directWrite8(0xC8, 0xC0)) { 
        qCWarning(log_host_hid) << "MS2130S GPIO init: failed to write 0xC8"; 
        return false; 
    }
    BYTE c8Verify = 0;
    directRead8(0xC8, c8Verify);
    
    // Don't abort on mismatch - log as info/warning but continue
    if (c8Verify != 0xC0) {
        qCWarning(log_host_hid) << "MS2130S GPIO 0xC8: wrote 0xC0 but readback=" << QString("0x%1").arg(c8Verify, 2, 16, QChar('0'))
                                << "- continuing (MsHidLink doesn't verify this register)";
    }
    
    qCInfo(log_host_hid) << "MS2130S GPIO: 0xC8 saved=" << QString("0x%1").arg(m_gpio_saved_c8, 2, 16, QChar('0'))
                         << "wrote=0xc0 readback=" << QString("0x%1").arg(c8Verify, 2, 16, QChar('0'))
                         << (c8Verify == 0xC0 ? "OK" : "READBACK-DIFFERS (expected for this register)");

    // Step 5: 0xCA — SPI config = 0x00
    directRead8(0xCA, m_gpio_saved_ca);
    if (!directWrite8(0xCA, 0x00)) { qCWarning(log_host_hid) << "MS2130S GPIO init: failed to write 0xCA"; return false; }
    BYTE caVerify = 0;
    directRead8(0xCA, caVerify);
    qCInfo(log_host_hid) << "MS2130S GPIO: 0xCA saved=" << QString("0x%1").arg(m_gpio_saved_ca, 2, 16, QChar('0'))
                         << "wrote=0x00 readback=" << QString("0x%1").arg(caVerify, 2, 16, QChar('0'))
                         << (caVerify == 0x00 ? "OK" : "INFO-only");

    // Step 6: 0xF01F — set bit 4, clear bit 7
    BYTE f01fVal = 0;
    if (!directRead16(0xF01F, f01fVal)) { qCWarning(log_host_hid) << "MS2130S GPIO init: failed to read 0xF01F"; return false; }
    m_gpio_saved_f01f = f01fVal;
    BYTE f01fNew = (f01fVal | 0x10) & ~0x80;
    if (!directWrite16(0xF01F, f01fNew)) { qCWarning(log_host_hid) << "MS2130S GPIO init: failed to write 0xF01F"; return false; }
    BYTE f01fVerify = 0;
    directRead16(0xF01F, f01fVerify);
    qCInfo(log_host_hid) << "MS2130S GPIO: 0xF01F saved=" << QString("0x%1").arg(f01fVal, 2, 16, QChar('0'))
                         << "wrote=" << QString("0x%1").arg(f01fNew, 2, 16, QChar('0'))
                         << "readback=" << QString("0x%1").arg(f01fVerify, 2, 16, QChar('0'))
                         << (f01fVerify == f01fNew ? "OK" : "INFO-only");

    m_gpioSaved = true;
    qCInfo(log_host_hid) << "MS2130S GPIO initialization completed successfully";
    return true;

#else
    // Non-Windows: use wrapper functions (unchanged)
    auto read8 = [this, read8_offset](quint8 addr, quint8 &value) -> bool {
        QByteArray cmd(9, 0);
        cmd[0] = 0x01;
        cmd[1] = static_cast<char>(0xC5);
        cmd[2] = static_cast<char>(addr);
        if (!sendFeatureReport(reinterpret_cast<uint8_t*>(cmd.data()), cmd.size()))
            return false;
        QByteArray resp(9, 0);
        resp[0] = 0x01;
        if (!getFeatureReport(reinterpret_cast<uint8_t*>(resp.data()), resp.size()))
            return false;
        value = static_cast<quint8>(resp[read8_offset]);
        return true;
    };
    auto write8 = [this](quint8 addr, quint8 value) -> bool {
        QByteArray cmd(9, 0);
        cmd[0] = 0x01;
        cmd[1] = static_cast<char>(0xC6);
        cmd[2] = static_cast<char>(addr);
        cmd[3] = static_cast<char>(value);
        return sendFeatureReport(reinterpret_cast<uint8_t*>(cmd.data()), cmd.size());
    };
    auto read16 = [this, read16_offset](quint16 addr, quint8 &value) -> bool {
        QByteArray cmd(9, 0);
        cmd[0] = 0x01;
        cmd[1] = static_cast<char>(0xB5);
        cmd[2] = static_cast<char>((addr >> 8) & 0xFF);
        cmd[3] = static_cast<char>(addr & 0xFF);
        if (!sendFeatureReport(reinterpret_cast<uint8_t*>(cmd.data()), cmd.size()))
            return false;
        QByteArray resp(9, 0);
        resp[0] = 0x01;
        if (!getFeatureReport(reinterpret_cast<uint8_t*>(resp.data()), resp.size()))
            return false;
        value = static_cast<quint8>(resp[read16_offset]);
        return true;
    };
    auto write16 = [this](quint16 addr, quint8 value) -> bool {
        QByteArray cmd(9, 0);
        cmd[0] = 0x01;
        cmd[1] = static_cast<char>(0xB6);
        cmd[2] = static_cast<char>((addr >> 8) & 0xFF);
        cmd[3] = static_cast<char>(addr & 0xFF);
        cmd[4] = static_cast<char>(value);
        return sendFeatureReport(reinterpret_cast<uint8_t*>(cmd.data()), cmd.size());
    };

    // Step 1: 0xB0
    if (!read8(0xB0, m_gpio_saved_b0)) { return false; }
    if (!write8(0xB0, m_gpio_saved_b0 & static_cast<quint8>(~0x04))) { return false; }
    // Step 2: 0xA0
    if (!read8(0xA0, m_gpio_saved_a0)) { return false; }
    if (!write8(0xA0, m_gpio_saved_a0 | 0x04)) { return false; }
    // Step 3-5: SPI config
    read8(0xC7, m_gpio_saved_c7);
    if (!write8(0xC7, 0xD1)) { return false; }
    read8(0xC8, m_gpio_saved_c8);
    if (!write8(0xC8, 0xC0)) { return false; }
    read8(0xCA, m_gpio_saved_ca);
    if (!write8(0xCA, 0x00)) { return false; }
    // Step 6: 0xF01F
    if (!read16(0xF01F, m_gpio_saved_f01f)) { return false; }
    quint8 f01fMod = (m_gpio_saved_f01f | 0x10) & static_cast<quint8>(~0x80);
    if (!write16(0xF01F, f01fMod)) { return false; }

    m_gpioSaved = true;
    qCInfo(log_host_hid) << "MS2130S GPIO initialization completed successfully";
    return true;
#endif
}

void VideoHid::ms2130sRestoreGPIO() {
    // Restore GPIO registers to their original values saved by ms2130sInitializeGPIO().
    // On Windows, closing the device handle does NOT trigger a USB reset, so we must
    // explicitly restore these registers to take the chip out of SPI-flash-access mode.
    if (!m_gpioSaved) {
        qCDebug(log_host_hid) << "MS2130S GPIO restore: nothing saved, skipping";
        return;
    }
    qCDebug(log_host_hid) << "MS2130S restoring GPIO registers to pre-flash values...";

    const quint8 reportId = (m_ms2130sConnectMode == 1) ? 0x00 : 0x01;

    auto write8 = [this, reportId](quint8 addr, quint8 value) -> bool {
#ifdef _WIN32
        QMutexLocker locker(&m_deviceHandleMutex);
        if (deviceHandle == INVALID_HANDLE_VALUE) {
            return false;
        }
        BYTE cmd[9] = {reportId, 0xC6, static_cast<BYTE>(addr), static_cast<BYTE>(value), 0, 0, 0, 0, 0};
        return hidSendFeatureNoTimeout(deviceHandle, cmd, 9, 1500) == TRUE;
#else
        QByteArray cmd(9, 0);
        cmd[0] = 0x01;
        cmd[1] = static_cast<char>(0xC6);
        cmd[2] = static_cast<char>(addr);
        cmd[3] = static_cast<char>(value);
        return sendFeatureReport(reinterpret_cast<uint8_t*>(cmd.data()), cmd.size());
#endif
    };
    auto write16 = [this, reportId](quint16 addr, quint8 value) -> bool {
#ifdef _WIN32
        QMutexLocker locker(&m_deviceHandleMutex);
        if (deviceHandle == INVALID_HANDLE_VALUE) {
            return false;
        }
        BYTE cmd[9] = {
            reportId,
            0xB6,
            static_cast<BYTE>((addr >> 8) & 0xFF),
            static_cast<BYTE>(addr & 0xFF),
            static_cast<BYTE>(value),
            0,
            0,
            0,
            0
        };
        return hidSendFeatureNoTimeout(deviceHandle, cmd, 9, 1500) == TRUE;
#else
        QByteArray cmd(9, 0);
        cmd[0] = 0x01;
        cmd[1] = static_cast<char>(0xB6);
        cmd[2] = static_cast<char>((addr >> 8) & 0xFF);
        cmd[3] = static_cast<char>(addr & 0xFF);
        cmd[4] = static_cast<char>(value);
        return sendFeatureReport(reinterpret_cast<uint8_t*>(cmd.data()), cmd.size());
#endif
    };

    bool restoreOk = true;

    // Restore in reverse order
    if (!write16(0xF01F, m_gpio_saved_f01f)) {
        qCWarning(log_host_hid) << "MS2130S GPIO restore failed at 0xF01F";
        restoreOk = false;
    }
    if (!write8(0xCA, m_gpio_saved_ca)) {
        qCWarning(log_host_hid) << "MS2130S GPIO restore failed at 0xCA";
        restoreOk = false;
    }
    if (!write8(0xC8, m_gpio_saved_c8)) {
        qCWarning(log_host_hid) << "MS2130S GPIO restore failed at 0xC8";
        restoreOk = false;
    }
    if (!write8(0xC7, m_gpio_saved_c7)) {
        qCWarning(log_host_hid) << "MS2130S GPIO restore failed at 0xC7";
        restoreOk = false;
    }
    if (!write8(0xA0, m_gpio_saved_a0)) {
        qCWarning(log_host_hid) << "MS2130S GPIO restore failed at 0xA0";
        restoreOk = false;
    }
    if (!write8(0xB0, m_gpio_saved_b0)) {
        qCWarning(log_host_hid) << "MS2130S GPIO restore failed at 0xB0";
        restoreOk = false;
    }

    m_gpioSaved = false;
    if (restoreOk) {
        qCInfo(log_host_hid) << "MS2130S GPIO registers restored";
    } else {
        qCWarning(log_host_hid) << "MS2130S GPIO restore completed with errors";
    }
}

bool VideoHid::ms2130sWriteFirmware(quint16 address, const QByteArray &data) {
    if (!beginTransaction()) {
        qCWarning(log_host_hid) << "MS2130S could not begin transaction for firmware write";
        return false;
    }

    // 🔥 SOLUTION 2: DO NOT close/reopen device handle - keep the existing handle
    // The MsHidLink reference opens the device ONCE in mshidlink_open_device() and
    // reuses that same handle for all GPIO init + flash operations without closing.
    // Closing and reopening may cause the device driver to lose GPIO configuration state.
    // TESTING: Try keeping the existing handle that was already opened by beginTransaction().
    qCDebug(log_host_hid) << "MS2130S keeping existing device handle for flash ops (not closing/reopening)...";
    // closeHIDDeviceHandle();  // ← DISABLED for testing
    // if (!openHIDDeviceHandle()) {  // ← DISABLED for testing
    //     qCWarning(log_host_hid) << "MS2130S failed to reopen device handle for flash ops";
    //     endTransaction();
    //     return false;
    // }
    // qCDebug(log_host_hid) << "MS2130S fresh device handle opened successfully";

    bool ok = true;
    bool gpioInitialized = false;

    // Phase 0: Initialize GPIO for flash operations (matches Swift initializeGPIO).
    // This configures the chip so the SPI flash subsystem is accessible.
    if (!ms2130sInitializeGPIO()) {
        qCWarning(log_host_hid) << "MS2130S GPIO initialization failed – aborting firmware write";
        ok = false;
    } else {
        gpioInitialized = true;
    }

    // Phase 1: Erase all flash sectors covered by the firmware image.
    // The reference uses plain HidD_SetFeature for erase — it blocks until the USB
    // control transfer STATUS stage ACKs (device holds it until erase finishes).
    if (ok) {
        int numSectors = (data.size() + 4095) / 4096;
        qCDebug(log_host_hid) << "MS2130S erasing" << numSectors
                              << "sector(s) starting at"
                              << QString("0x%1").arg(address, 4, 16, QChar('0'));
        QElapsedTimer eraseTimer;
        for (int i = 0; i < numSectors; ++i) {
            quint32 sectorAddr = static_cast<quint32>(address) + static_cast<quint32>(i) * 4096;
            eraseTimer.start();
            if (!ms2130sEraseSector(sectorAddr)) {
                qCWarning(log_host_hid) << "MS2130S sector erase failed at"
                                        << QString("0x%1").arg(sectorAddr, 8, 16, QChar('0'));
                ok = false;
                break;
            }
            qint64 eraseMs = eraseTimer.elapsed();
            qCDebug(log_host_hid) << "MS2130S erased sector" << (i + 1) << "/" << numSectors
                                  << "at" << QString("0x%1").arg(sectorAddr, 8, 16, QChar('0'))
                                  << "in" << eraseMs << "ms";
        }

        // Extra sector 15 erase when firmware is small (matches reference)
        if (ok && numSectors <= 15) {
            quint32 sector15Addr = static_cast<quint32>(address) + 15u * 4096;
            qCDebug(log_host_hid) << "MS2130S erasing extra sector 15 at"
                                  << QString("0x%1").arg(sector15Addr, 8, 16, QChar('0'));
            ms2130sEraseSector(sector15Addr); // best-effort, don't fail on this
        }

        if (ok) {
            qCInfo(log_host_hid) << "MS2130S all" << numSectors << "sectors erased successfully";
        }
    }

    // The reference MSFlashUpgradeToolDlg goes straight from erase to burst write
    // with NO delays and NO intermediate reads.  Do NOT issue a burst read here:
    // entering burst-read mode right before burst-write corrupts the write sequence.

    // Wait 1 second after erase to allow flash chip to complete internal erase
    // operations. Swift reference implementation uses Thread.sleep(forTimeInterval: 1.0).
    if (ok) {
        qCDebug(log_host_hid) << "MS2130S waiting 1 second after erase for flash to stabilize...";
        QThread::msleep(1000);

        qCInfo(log_host_hid) << "MS2130S starting burst write";
    }

    // Phase 2: Burst write in 60 KB chunks.
    // The reference MSFlashUpgradeToolDlg (V2/V3 mode) writes 60*1024 bytes per burst.
    // (The 0x1000-byte bursts in the reference code are only for V1 upgrade mode.)
    quint32 written = 0;
    const quint32 totalSize = static_cast<quint32>(data.size());
    const quint32 maxBurstSize = 60u * 1024u; // 60 KB, matching reference C++ tool

    while (written < totalSize && ok) {
        quint32 chunkAddr = address + written;
        quint32 toWrite = qMin(maxBurstSize, totalSize - written);
        bool chunkOk = ms2130sFlashBurstWrite(chunkAddr, data.mid(written, static_cast<int>(toWrite)));
        if (!chunkOk) {
            qCWarning(log_host_hid) << "MS2130S burst write failed at"
                                    << QString("0x%1").arg(chunkAddr, 8, 16, QChar('0'))
                                    << "len=" << toWrite;
            ok = false;
        } else {
            written += toWrite;
            qCDebug(log_host_hid) << "MS2130S burst write chunk OK: wrote" << toWrite
                                  << "bytes at" << QString("0x%1").arg(chunkAddr, 8, 16, QChar('0'))
                                  << "(" << written << "/" << totalSize << ")";
        }
    }

    if (ok) {
        qCInfo(log_host_hid) << "MS2130S firmware write COMPLETED SUCCESSFULLY:"
                             << totalSize << "bytes written";
    } else {
        qCWarning(log_host_hid) << "MS2130S firmware write FAILED after writing"
                                << written << "/" << totalSize << "bytes";
    }

    if (ok) {
        // Log first 16 bytes of firmware data for diagnostics
        {
            QString hexDump;
            for (int i = 0; i < 16 && i < data.size(); ++i) {
                hexDump += QString("%1 ").arg(static_cast<quint8>(data.at(i)), 2, 16, QChar('0'));
            }
            qCInfo(log_host_hid) << "MS2130S firmware data first 16 bytes:" << hexDump.trimmed();
        }

        // Keep verification off by default: on some devices the immediate burst-read path
        // can stall the HID pipe (error 121) and then poison follow-up GPIO restore commands.
        const bool enableReadBackVerify = false;
        if (enableReadBackVerify) {
            bool verifyOk = true;
            bool verifyTransportOk = true;

            qCInfo(log_host_hid) << "MS2130S waiting 2000 ms for flash to settle after write...";
            QThread::msleep(2000);

            qCInfo(log_host_hid) << "MS2130S starting read-back verification (same device handle)...";
            quint32 verified = 0;
            while (verified < totalSize && verifyOk) {
                quint32 toRead = qMin(maxBurstSize, totalSize - verified);
                QByteArray readBack;
                if (!ms2130sFlashBurstRead(address + verified, toRead, readBack)) {
                    qCWarning(log_host_hid) << "MS2130S verify: burst read failed at"
                                            << QString("0x%1").arg(address + verified, 8, 16, QChar('0'));
                    verifyTransportOk = false;
                    break;
                }
                if (verified == 0) {
                    QString hexDump;
                    for (int i = 0; i < 16 && i < readBack.size(); ++i) {
                        hexDump += QString("%1 ").arg(static_cast<quint8>(readBack.at(i)), 2, 16, QChar('0'));
                    }
                    qCInfo(log_host_hid) << "MS2130S read-back first 16 bytes:" << hexDump.trimmed();
                }
                for (quint32 k = 0; k < toRead; ++k) {
                    if (readBack.at(static_cast<int>(k)) != data.at(static_cast<int>(verified + k))) {
                        qCWarning(log_host_hid) << "MS2130S verify MISMATCH at offset"
                                                << QString("0x%1").arg(verified + k, 8, 16, QChar('0'))
                                                << "expected" << QString("0x%1").arg(static_cast<quint8>(data.at(static_cast<int>(verified + k))), 2, 16, QChar('0'))
                                                << "got" << QString("0x%1").arg(static_cast<quint8>(readBack.at(static_cast<int>(k))), 2, 16, QChar('0'));
                        verifyOk = false;
                        break;
                    }
                }
                verified += toRead;
            }
            if (verifyOk && verifyTransportOk) {
                qCInfo(log_host_hid) << "MS2130S read-back verification PASSED:" << totalSize << "bytes OK";
            } else if (!verifyTransportOk) {
                qCWarning(log_host_hid) << "MS2130S read-back verification SKIPPED due to transport/protocol incompatibility";
            } else {
                qCWarning(log_host_hid) << "MS2130S read-back verification FAILED – flash data is corrupt!";
                ok = false;
            }
        } else {
            qCWarning(log_host_hid) << "MS2130S read-back verification disabled to keep HID control channel stable";
        }
    }

    // Wait for flash internal programming to settle before closing the HID handle.
    // Keep this conservative; we avoid any extra register writes after burst-write.
    qCDebug(log_host_hid) << "MS2130S waiting 2000 ms for flash programming to settle...";
    QThread::msleep(2000);

    // IMPORTANT: Do not restore GPIO registers here.
    // The reference MsHidLink flash flow does not perform a post-write GPIO restore;
    // it closes the tool and asks for a full power cycle. Extra register writes right
    // after burst-write can interfere with devices that are still finalizing flash.
    Q_UNUSED(gpioInitialized);
    qCInfo(log_host_hid) << "MS2130S skipping GPIO restore after flash write (aligned with reference flow)";

    // Phase 5: Close connection and let the user power-cycle.
    // The reference MSFlashUpgradeToolDlg does NOT perform soft-reset after the
    // actual firmware flash.  It only uses the two-stage soft-reset (ROM → RAM)
    // when flashing the V1 upgrade firmware, which is a different scenario.
    // For the actual firmware write the reference says:
    //   "关闭此工具后再给设备重新上电可加载新固件功能"
    //   (Close this tool and power cycle the device to load the new firmware.)
    // Performing soft-reset here was leaving the MCU in an inconsistent state,
    // causing the device to become unrecognizable after a power cycle.
    if (ok) {
        qCInfo(log_host_hid) << "MS2130S firmware update complete – user must power-cycle the device to load new firmware";
    } else {
        qCWarning(log_host_hid) << "MS2130S firmware update failed – device may need recovery reflash";
    }

    endTransaction();

    return ok;
}

void VideoHid::loadFirmwareToEeprom() {
    qCDebug(log_host_hid) << "loadFirmwareToEeprom() called";
    
    // Create firmware data
    if (networkFirmware.empty()){
        qCDebug(log_host_hid) << "No firmware data available to write - networkFirmware is empty";
        emit firmwareWriteComplete(false);
        return;
    }
    
    qCDebug(log_host_hid) << "networkFirmware size:" << networkFirmware.size() << "bytes";

    QByteArray firmware(reinterpret_cast<const char*>(networkFirmware.data()), networkFirmware.size());
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
            qCInfo(log_host_hid) << "[Firmware Update] SUCCESS – firmware has been written to the device";
            emit firmwareWriteComplete(true);
        } else {
            qCWarning(log_host_hid) << "[Firmware Update] FAILED – please reconnect the device and try again";
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
    // Check if TLS/SSL is available (important for statically built Qt applications)
    if (!QSslSocket::supportsSsl()) {
        qCWarning(log_host_hid) << "TLS/SSL not available - skipping firmware check";
        fireware_result = FirmwareResult::CheckFailed;
        return FirmwareResult::CheckFailed;
    }

    qCDebug(log_host_hid) << "Checking for latest firmware...";
    QString firemwareFileName = getLatestFirmwareFilenName(firmwareURL);
    qCDebug(log_host_hid) << "Latest firmware file name:" << firemwareFileName;
    if (fireware_result == FirmwareResult::Timeout) {
        return FirmwareResult::Timeout;
    }
    qCDebug(log_host_hid) << "After timeout checking: " << firmwareURL;
    // Build binary URL by replacing the last path segment with the chosen filename (robust vs. index name)
    QString newURL = firmwareURL;
    int lastSlash = newURL.lastIndexOf('/');
    if (lastSlash >= 0) newURL = newURL.left(lastSlash + 1) + firemwareFileName;
    fetchBinFileToString(newURL, 5000); // Add 5-second timeout
    m_currentfirmwareVersion = getFirmwareVersion();
    qCDebug(log_host_hid)  << "Firmware version:" << QString::fromStdString(m_currentfirmwareVersion);
    qCDebug(log_host_hid)  << "Latest firmware version:" << QString::fromStdString(m_firmwareVersion);
    if (getFirmwareVersion() == m_firmwareVersion) {
        fireware_result = FirmwareResult::Latest;
        return FirmwareResult::Latest;
    }
    if (safe_stoi(getFirmwareVersion()) <= safe_stoi(m_firmwareVersion)) {
        fireware_result = FirmwareResult::Upgradable;
        return FirmwareResult::Upgradable;
    }
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