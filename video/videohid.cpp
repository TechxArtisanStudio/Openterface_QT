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

#include "firmwareoperationmanager.h"
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

    // Create firmware operation manager (used by loadFirmwareToEeprom).
    m_firmwareOpManager = new FirmwareOperationManager(this, ADDR_EEPROM, this);
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

FirmwareOperationManager* VideoHid::getFirmwareOperationManager() const {
    return m_firmwareOpManager;
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

Ms2130sChip* VideoHid::getMs2130sChip() const {
    return dynamic_cast<Ms2130sChip*>(m_chipImpl.get());
}

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

    // Wire a default no-op progress tracker for Ms2130sChip firmware writes.
    // The actual per-write callback is injected via writeEeprom() at write time.
    if (auto* ms2130s = dynamic_cast<Ms2130sChip*>(m_chipImpl.get())) {
        ms2130s->onChunkWritten = [this](quint32 n) {
            written_size = n;
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
        (void)QtConcurrent::run([this]() {
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

// Legacy per-chip read helpers moved to Ms2xxxChip::read4Byte().


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
        
        success = m_deviceTransport ? m_deviceTransport->open() : false;
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
        
        if (m_deviceTransport) m_deviceTransport->close();
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


QString VideoHid::findMatchingHIDDevice(const QString& portChain) const
{
    // Check if we have a cached path that's still valid
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - m_lastPathQuery).count();
    
    if (!m_cachedDevicePath.isEmpty() && elapsed < 10) {
        qCDebug(log_host_hid) << "Using cached HID device path:" << m_cachedDevicePath;
        return m_cachedDevicePath;
    }

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
            const_cast<VideoHid*>(this)->m_cachedDevicePath = selectedDevice.hidDevicePath;
            
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
        
        m_cachedDevicePath = targetHIDPath;

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

void VideoHid::loadFirmwareToEeprom() {
    qCDebug(log_host_hid) << "loadFirmwareToEeprom() called";
    
    if (m_netClient.getNetworkFirmware().empty()) {
        qCDebug(log_host_hid) << "No firmware data available to write - networkFirmware is empty";
        return;
    }

    const auto& rawFirmware = m_netClient.getNetworkFirmware();
    qCDebug(log_host_hid) << "networkFirmware size:" << rawFirmware.size() << "bytes";

    QByteArray firmware(reinterpret_cast<const char*>(rawFirmware.data()), rawFirmware.size());
    qCDebug(log_host_hid) << "Delegating to FirmwareOperationManager, firmware size:" << firmware.size() << "bytes";

    m_firmwareOpManager->writeFirmware(firmware, QString());
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
