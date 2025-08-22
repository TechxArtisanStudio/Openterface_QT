#include "videohid.h"

#include <QDebug>
#include <QDir>
#include <QTimer>
#include <QThread>
#include <QLoggingCategory>

#include "ms2109.h"
#include "firmwarewriter.h"
#include "firmwarereader.h"
#include "../global.h"
#include "../device/DeviceManager.h"
#include "../device/HotplugMonitor.h"
#include "../ui/globalsetting.h"

#ifdef _WIN32
#include <hidclass.h>
extern "C"
{
#include <hidsdi.h>
#include <setupapi.h>
}
#elif __linux__
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/hidraw.h>
#endif

Q_LOGGING_CATEGORY(log_host_hid, "opf.device.hid");

VideoHid::VideoHid(QObject *parent) : QObject(parent), m_inTransaction(false) {
    // Initialize current device tracking
    m_currentHIDDevicePath.clear();
    m_currentHIDPortChain.clear();
    
    // Connect to hotplug monitor for automatic device management
    connectToHotplugMonitor();
}

// Update the start method to keep HID device continuously open
void VideoHid::start() {
    // Initialize current device tracking from global settings
    QString currentPortChain = GlobalSetting::instance().getOpenterfacePortChain();
    if (!currentPortChain.isEmpty()) {
        m_currentHIDPortChain = currentPortChain;
        QString hidPath = findMatchingHIDDevice(currentPortChain);
        if (!hidPath.isEmpty()) {
            m_currentHIDDevicePath = hidPath;
            qCDebug(log_host_hid) << "Initialized HID device with port chain:" << currentPortChain 
                                 << "device path:" << hidPath;
        }
    }
    
    std::string captureCardFirmwareVersion = getFirmwareVersion();
    qCDebug(log_host_hid) << "MS2109 firmware VERSION:" << QString::fromStdString(captureCardFirmwareVersion);    //firmware VERSION
    
    GlobalVar::instance().setCaptureCardFirmwareVersion(captureCardFirmwareVersion);
    isHardSwitchOnTarget = getSpdifout();
    qCDebug(log_host_hid)  << "SPDIFOUT:" << isHardSwitchOnTarget;    //SPDIFOUT
    if(eventCallback){
        eventCallback->onSwitchableUsbToggle(isHardSwitchOnTarget);
        setSpdifout(isHardSwitchOnTarget); //Follow the hard switch by default
    }

    // Open HID device once and keep it open for continuous monitoring
    if (!beginTransaction()) {
        qCWarning(log_host_hid) << "Failed to open HID device for continuous monitoring";
        return;
    }

    //start a timer to get the HDMI connection status every 1 second
    timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [=](){
        // Device is already open - no need for beginTransaction/endTransaction
        try {
            bool currentSwitchOnTarget = getGpio0();
            bool hdmiConnected = isHdmiConnected();

            if (eventCallback) {
                if (hdmiConnected) {
                    // Get resolution-related information
                    quint8 width_h = static_cast<quint8>(usbXdataRead4Byte(ADDR_INPUT_WIDTH_H).first.at(0));
                    quint8 width_l = static_cast<quint8>(usbXdataRead4Byte(ADDR_INPUT_WIDTH_L).first.at(0));
                    quint16 width = (width_h << 8) + width_l;
                    
                    quint8 height_h = static_cast<quint8>(usbXdataRead4Byte(ADDR_INPUT_HEIGHT_H).first.at(0));
                    quint8 height_l = static_cast<quint8>(usbXdataRead4Byte(ADDR_INPUT_HEIGHT_L).first.at(0));
                    quint16 height = (height_h << 8) + height_l;
                    
                    quint8 fps_h = static_cast<quint8>(usbXdataRead4Byte(ADDR_INPUT_FPS_H).first.at(0));
                    quint8 fps_l = static_cast<quint8>(usbXdataRead4Byte(ADDR_INPUT_FPS_L).first.at(0));
                    float fps = static_cast<float>((fps_h << 8) + fps_l) / 100;
                    
                    quint8 clk_h = static_cast<quint8>(usbXdataRead4Byte(ADDR_INPUT_PIXELCLK_H).first.at(0));
                    quint8 clk_l = static_cast<quint8>(usbXdataRead4Byte(ADDR_INPUT_PIXELCLK_L).first.at(0));
                    float pixclk = ((clk_h << 8) + clk_l) / 100.0;
                    
                    float aspectRatio = height ? static_cast<float>(width) / height : 0;
                    GlobalVar::instance().setInputAspectRatio(aspectRatio);

                    if (pixclk > 185) {
                        width *= 2;
                        height *= 2;
                    }

                    if (GlobalVar::instance().getInputWidth() != width || GlobalVar::instance().getInputHeight() != height) {
                        emit inputResolutionChanged(GlobalVar::instance().getInputWidth(), GlobalVar::instance().getInputHeight(), width, height);
                    }
                    
                    emit resolutionChangeUpdate(width, height, fps, pixclk);
                } else {
                    emit resolutionChangeUpdate(0, 0, 0, 0);
                }

                // Handle hardware switch status changes
                if (isHardSwitchOnTarget != currentSwitchOnTarget) {
                    qCDebug(log_host_hid)  << "isHardSwitchOnTarget" << isHardSwitchOnTarget << "currentSwitchOnTarget" << currentSwitchOnTarget;
                    eventCallback->onSwitchableUsbToggle(currentSwitchOnTarget);
                    
                    // Set SPDIF out
                    int bit = 1;
                    int mask = 0xFE;
                    if (GlobalVar::instance().getCaptureCardFirmwareVersion() < "24081309") {
                        bit = 0x10;
                        mask = 0xEF;
                    }

                    quint8 spdifout = static_cast<quint8>(usbXdataRead4Byte(ADDR_SPDIFOUT).first.at(0));
                    if (currentSwitchOnTarget) {
                        spdifout |= bit;
                    } else {
                        spdifout &= mask;
                    }
                    QByteArray data(4, 0);
                    data[0] = spdifout;
                    usbXdataWrite4Byte(ADDR_SPDIFOUT, data);
                    
                    isHardSwitchOnTarget = currentSwitchOnTarget;
                }
            }
        }
        catch (...) {
            qCDebug(log_host_hid)  << "Exception occurred during timer processing";
        }
    });
    timer->start(1000);
}

void VideoHid::stop() {
    qCDebug(log_host_hid)  << "Stopping VideoHid timer.";
    if (timer) {
        timer->stop();
        disconnect(timer, &QTimer::timeout, this, nullptr);
        delete timer;
        timer = nullptr;
    }
    
    // Close the HID device when stopping
    endTransaction();
}

/*
Get the input resolution from capture card. 
*/
QPair<int, int> VideoHid::getResolution() {
    quint8 width_h = static_cast<quint8>(usbXdataRead4Byte(ADDR_INPUT_WIDTH_H).first.at(0));
    quint8 width_l = static_cast<quint8>(usbXdataRead4Byte(ADDR_INPUT_WIDTH_L).first.at(0));
    quint16 width = (width_h << 8) + width_l;
    quint8 height_h = static_cast<quint8>(usbXdataRead4Byte(ADDR_INPUT_HEIGHT_H).first.at(0));
    quint8 height_l = static_cast<quint8>(usbXdataRead4Byte(ADDR_INPUT_HEIGHT_L).first.at(0));
    quint16 height = (height_h << 8) + height_l;
    
    return qMakePair(width, height);
}

float VideoHid::getFps() {
    quint8 fps_h = static_cast<quint8>(usbXdataRead4Byte(ADDR_INPUT_FPS_H).first.at(0));
    quint8 fps_l = static_cast<quint8>(usbXdataRead4Byte(ADDR_INPUT_FPS_L).first.at(0));
    quint16 fps = (fps_h << 8) + fps_l;
    
    return static_cast<float>(fps) / 100;
}

/*
 * Address: 0xDF00 bit0 indicates the hard switch status,
 * true means switchable usb connects to the target,
 * false means switchable usb connects to the host
 */
bool VideoHid::getGpio0() {
    bool result = usbXdataRead4Byte(ADDR_GPIO0).first.at(0) & 0x01;
    return result;
}

float VideoHid::getPixelclk() {
    quint8 clk_h = static_cast<quint8>(usbXdataRead4Byte(ADDR_INPUT_PIXELCLK_H).first.at(0));
    quint8 clk_l = static_cast<quint8>(usbXdataRead4Byte(ADDR_INPUT_PIXELCLK_L).first.at(0));
    float result = ((clk_h << 8) + clk_l)/100.0;
    
    return result;
}


bool VideoHid::getSpdifout() {
    int bit = 1;
    int mask = 0xFE;  // Mask for potential future use
    if (GlobalVar::instance().getCaptureCardFirmwareVersion() < "24081309") {
        qCDebug(log_host_hid)  << "Firmware version is less than 24081309";
        bit = 0x10;
        mask = 0xEF;
    }
    Q_UNUSED(mask)  // Suppress unused variable warning
    
    bool result = usbXdataRead4Byte(ADDR_SPDIFOUT).first.at(0) & bit;
    return result;
}

void VideoHid::switchToHost() {
    qCDebug(log_host_hid)  << "Switch to host";
    setSpdifout(false);
    GlobalVar::instance().setSwitchOnTarget(false);
    if(eventCallback) eventCallback->onSwitchableUsbToggle(false);
}

void VideoHid::switchToTarget() {
    qCDebug(log_host_hid)  << "Switch to target";
    setSpdifout(true);
    GlobalVar::instance().setSwitchOnTarget(true);
    if(eventCallback) eventCallback->onSwitchableUsbToggle(true);
}

/*
 * Address: 0xDF01 bitn indicates the soft switch status, the firmware before 24081309, it's bit5, after 24081309, it's bit0
 * true means switchable usb connects to the target,
 * false means switchable usb connects to the host
 */
void VideoHid::setSpdifout(bool enable) {
    int bit = 1;
    int mask = 0xFE;
    if (GlobalVar::instance().getCaptureCardFirmwareVersion() < "24081309") {
        qCDebug(log_host_hid)  << "Firmware version is less than 24081309";
        bit = 0x10;
        mask = 0xEF;
    }

    quint8 spdifout = static_cast<quint8>(usbXdataRead4Byte(ADDR_SPDIFOUT).first.at(0));
    if (enable) {
        spdifout |= bit;
    } else {
        spdifout &= mask;
    }
    QByteArray data(4, 0); // Create a 4-byte array initialized to zero
    data[0] = spdifout;
    if(usbXdataWrite4Byte(ADDR_SPDIFOUT, data)){
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
        // Read all version bytes
        version_0 = usbXdataRead4Byte(ADDR_FIRMWARE_VERSION_0).first.toHex().toInt(&ok, 16);
        version_1 = usbXdataRead4Byte(ADDR_FIRMWARE_VERSION_1).first.toHex().toInt(&ok, 16);
        version_2 = usbXdataRead4Byte(ADDR_FIRMWARE_VERSION_2).first.toHex().toInt(&ok, 16);
        version_3 = usbXdataRead4Byte(ADDR_FIRMWARE_VERSION_3).first.toHex().toInt(&ok, 16);
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
    }else {
        fireware_result = FirmwareResult::Checking; // Set the initial state to checking
        qCDebug(log_host_hid) << "Network reply created successfully";
    }

    qCDebug(log_host_hid) << "Fetching latest firmware file name from" << url;

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
        qCDebug(log_host_hid) << "Successfully fetched latest firmware";
        QString result = QString::fromUtf8(reply->readAll());
        fireware_result = FirmwareResult::CheckSuccess;
        reply->deleteLater();
        return result;
    } else {
        qCDebug(log_host_hid) << "Fail to get file name:" << reply->errorString();
        fireware_result = FirmwareResult::CheckFailed;
        reply->deleteLater();
        return QString();
    }
}

/*
 * Address: 0xFA8C bit0 indicates the HDMI connection status
 */
bool VideoHid::isHdmiConnected() {
    return usbXdataRead4Byte(ADDR_HDMI_CONNECTION_STATUS).first.at(0) & 0x01;
}

void VideoHid::setEventCallback(StatusEventCallback* callback) {
    eventCallback = callback;
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
    QByteArray ctrlData(9, 0); // Initialize with 9 bytes set to 0
    QByteArray result(9, 0);

    ctrlData[1] = CMD_XDATA_READ;
    ctrlData[2] = static_cast<char>((u16_address >> 8) & 0xFF);
    ctrlData[3] = static_cast<char>(u16_address & 0xFF);
    // 0: Some devices use report ID 0 to indicate that no specific report ID is used.
    if (this->sendFeatureReport((uint8_t*)ctrlData.data(), ctrlData.size())) {
        if (this->getFeatureReport((uint8_t*)result.data(), result.size())) {
            return qMakePair(result.mid(4, 1), true);
        }
    } else {
        // 1: Some devices use report ID 1 to indicate that no specific report ID is used.
        ctrlData[0] = 0x01;
        if (this->sendFeatureReport((uint8_t*)ctrlData.data(), ctrlData.size())) {
            this->getFeatureReport((uint8_t*)result.data(), result.size());

            if (!result.isEmpty()) {
                return qMakePair(result.mid(3, 4), true);
            }
        }
    }
    return qMakePair(QByteArray(4, 0), false); // Return 4 bytes set to 0 and false
}

bool VideoHid::usbXdataWrite4Byte(quint16 u16_address, QByteArray data) {
    QByteArray ctrlData(9, 0); // Initialize with 9 bytes set to 0

    ctrlData[1] = CMD_XDATA_WRITE;
    ctrlData[2] = static_cast<char>((u16_address >> 8) & 0xFF);
    ctrlData[3] = static_cast<char>(u16_address & 0xFF);
    ctrlData.replace(4, 4, data);

    qCDebug(log_host_hid)  << "usbXdataWrite4Byte: " << ctrlData.toHex();

    return this->sendFeatureReport((uint8_t*)ctrlData.data(), ctrlData.size());
}

bool VideoHid::getFeatureReport(uint8_t* buffer, size_t bufferLength) {
#ifdef _WIN32
    return this->getFeatureReportWindows(buffer, bufferLength);
#elif __linux__
    return this->getFeatureReportLinux(buffer, bufferLength);
#endif
}

bool VideoHid::sendFeatureReport(uint8_t* buffer, size_t bufferLength) {
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
#endif
}

void VideoHid::closeHIDDeviceHandle() {
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

bool VideoHid::beginTransaction() {
    if (m_inTransaction) {
        qCDebug(log_host_hid)  << "Transaction already in progress";
        return true;  // Already in a transaction
    }
    
#ifdef _WIN32
    bool success = openHIDDeviceHandle();
#elif __linux__
    bool success = openHIDDevice();
#endif

    if (success) {
        m_inTransaction = true;
        qCDebug(log_host_hid)  << "HID transaction started";
        return true;
    } else {
        qCDebug(log_host_hid)  << "Failed to start HID transaction";
        return false;
    }
}

void VideoHid::endTransaction() {
    if (m_inTransaction) {
        closeHIDDeviceHandle();
        m_inTransaction = false;
        qCDebug(log_host_hid)  << "HID transaction ended";
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
                    stop();
                    qCInfo(log_host_hid) << "✓ HID device stopped for unplugged device at port:" << device.portChain;
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
                    // Start the HID device
                    start();
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
    QList<DeviceInfo> devices = deviceManager.getDevicesByPortChain(portChain);
    
    if (devices.isEmpty()) {
        qCWarning(log_host_hid) << "No devices found for port chain:" << portChain;
        return QString();
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

    if (!selectedDevice.isValid() || selectedDevice.hidDevicePath.isEmpty()) {
        qCWarning(log_host_hid) << "No device with HID information found for port chain:" << portChain;
        return QString();
    }

    qCDebug(log_host_hid) << "Selected HID device path:" << selectedDevice.hidDevicePath;
    
    // Cache the found device path
#ifdef _WIN32
    const_cast<VideoHid*>(this)->m_cachedDevicePath = selectedDevice.hidDevicePath.toStdWString();
#elif __linux__
    const_cast<VideoHid*>(this)->m_cachedDevicePath = selectedDevice.hidDevicePath;
#endif
    
    return selectedDevice.hidDevicePath;
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

bool VideoHid::sendFeatureReportWindows(BYTE* reportBuffer, DWORD bufferSize) {
    bool openedForOperation = m_inTransaction || openHIDDeviceHandle();
    
    if (!openedForOperation) {
        qCDebug(log_host_hid)  << "Failed to open device handle for sending feature report.";
        return false;
    }

    // Send the Set Feature Report request
    bool result = HidD_SetFeature(deviceHandle, reportBuffer, bufferSize);

    if (!result) {
        qCDebug(log_host_hid)  << "Failed to send feature report.";
        if (!m_inTransaction) {
            closeHIDDeviceHandle();
        }
        return false;
    }

    if (!m_inTransaction) {
        closeHIDDeviceHandle();
    }
    return true;
}

bool VideoHid::openHIDDeviceHandle() {
    if (deviceHandle == INVALID_HANDLE_VALUE) {
        qCDebug(log_host_hid)  << "Opening HID device handle...";
        qCDebug(log_host_hid)  << "HID device path:" << QString::fromStdWString(getHIDDevicePath());
        deviceHandle = CreateFileW(getHIDDevicePath().c_str(),
                                 GENERIC_READ | GENERIC_WRITE,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE,
                                 NULL,
                                 OPEN_EXISTING,
                                 0,
                                 NULL);
        
        if (deviceHandle == INVALID_HANDLE_VALUE) {
            qCDebug(log_host_hid)  << "Failed to open device handle.";
            return false;
        }
    }
    return true;
}

bool VideoHid::getFeatureReportWindows(BYTE* reportBuffer, DWORD bufferSize) {
    bool openedForOperation = m_inTransaction || openHIDDeviceHandle();
    
    if (!openedForOperation) {
        qCDebug(log_host_hid)  << "Failed to open device handle for getting feature report.";
        return false;
    }

    // Send the Get Feature Report request
    bool result = HidD_GetFeature(deviceHandle, reportBuffer, bufferSize);

    if (!result) {
        qCDebug(log_host_hid)  << "Failed to get feature report.";
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
        
        if (!status) {
            qWarning() << "Failed to write chunk to address:" << QString("0x%1").arg(_address, 4, 16, QChar('0'));
            return false;
        }
        written_size += chunk_length;
        emit firmwareWriteChunkComplete(written_size);
        _address += chunkSize;
    }
    return true;
}

bool VideoHid::writeEeprom(quint16 address, const QByteArray &data) {
    const int MAX_CHUNK = 16;
    QByteArray remainingData = data;
    written_size = 0;
    
    // Begin transaction for the entire operation
    if (!beginTransaction()) {
        qCDebug(log_host_hid)  << "Failed to begin transaction for EEPROM write";
        return false;
    }
    
    bool success = true;
    while (!remainingData.isEmpty() && success) {
        QByteArray chunk = remainingData.left(MAX_CHUNK);
        success = writeChunk(address, chunk);
        
        if (success) {
            address += chunk.size();
            remainingData = remainingData.mid(MAX_CHUNK);
            if (written_size % 64 == 0) {
                qCDebug(log_host_hid)  << "Written size:" << written_size;
            }
            QThread::msleep(150); // Add 100ms delay between writes
        } else {
            qCDebug(log_host_hid)  << "Failed to write chunk to EEPROM";
            break;
        }
    }
    
    // End transaction
    endTransaction();
    
    return success;
}

void VideoHid::loadFirmwareToEeprom() {
    // Create firmware data
    if (networkFirmware.empty()){
        qCDebug(log_host_hid) << "No firmware data available to write";
        emit firmwareWriteComplete(false);
        return;
    }

    QByteArray firmware(reinterpret_cast<const char*>(networkFirmware.data()), networkFirmware.size());
    
    // Create a worker thread to handle firmware writing
    QThread* thread = new QThread();
    FirmwareWriter* worker = new FirmwareWriter(this, ADDR_EEPROM, firmware);
    worker->moveToThread(thread);
    
    // Connect signals/slots
    connect(thread, &QThread::started, worker, &FirmwareWriter::process);
    connect(worker, &FirmwareWriter::finished, thread, &QThread::quit);
    connect(worker, &FirmwareWriter::finished, worker, &FirmwareWriter::deleteLater);
    connect(thread, &QThread::finished, thread, &QThread::deleteLater);
    
    // Connect progress and status signals if needed
    connect(worker, &FirmwareWriter::progress, this, [this](int percent) {
        qCDebug(log_host_hid) << "Firmware write progress: " << percent << "%";
        emit firmwareWriteProgress(percent);
    });
    
    connect(worker, &FirmwareWriter::finished, this, [this](bool success) {
        if (success) {
            qCDebug(log_host_hid) << "Firmware write completed successfully";
            emit firmwareWriteComplete(true);
        } else {
            qCDebug(log_host_hid) << "Firmware write failed - user should try again";
            emit firmwareWriteComplete(false);
            emit firmwareWriteError("Firmware update failed. Please try again.");
        }
    });
    
    // Start the thread
    thread->start();
}

FirmwareResult VideoHid::isLatestFirmware() {
    qCDebug(log_host_hid) << "Checking for latest firmware...";
    QString firemwareFileName = getLatestFirmwareFilenName(firmwareURL);
    qCDebug(log_host_hid) << "Latest firmware file name:" << firemwareFileName;
    if (fireware_result == FirmwareResult::Timeout) {
        return FirmwareResult::Timeout;
    }
    qCDebug(log_host_hid) << "After timeout checking: " << firmwareURL;
    QString newURL = firmwareURL.replace("minikvm_latest_firmware.txt", firemwareFileName);
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
