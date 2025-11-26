#ifdef __linux__
#include "LinuxDeviceManager.h"
#include <QDebug>
#include <QTextStream>
#include <QStandardPaths>
#include <QRegularExpression>
#include <cstdlib>
#include <QSerialPortInfo>

#ifdef HAVE_LIBUDEV
#include <libudev.h>
#include <cstring>
#include <memory>
#endif

Q_LOGGING_CATEGORY(log_device_linux, "opf.device.linux")

LinuxDeviceManager::LinuxDeviceManager(QObject *parent)
    : AbstractPlatformDeviceManager(parent)
    , m_futureWatcher(new QFutureWatcher<QList<DeviceInfo>>(this))
    , m_discoveryInProgress(false)
{
#ifdef HAVE_LIBUDEV
    // Initialize udev context
    m_udev = udev_new();
    qCDebug(log_device_linux) << "Creating udev context for Linux Device Manager";
    if (!m_udev) {
        qCWarning(log_device_linux) << "Failed to create udev context";
    } else {
        qCDebug(log_device_linux) << "Linux Device Manager initialized with libudev";
    }
#else
    qCWarning(log_device_linux) << "Linux Device Manager initialized without libudev support";
#endif

    // Setup async discovery watcher
    connect(m_futureWatcher, &QFutureWatcher<QList<DeviceInfo>>::finished,
            this, &LinuxDeviceManager::onAsyncDiscoveryFinished);
}

LinuxDeviceManager::~LinuxDeviceManager()
{
#ifdef HAVE_LIBUDEV
    if (m_udev) {
        udev_unref(m_udev);
        m_udev = nullptr;
    }
#endif
}

QList<DeviceInfo> LinuxDeviceManager::discoverDevices()
{
    QDateTime now = QDateTime::currentDateTime();
    
    // Check if cache is fresh (within timeout)
    if (m_lastCacheUpdate.isValid() && 
        m_lastCacheUpdate.msecsTo(now) < CACHE_TIMEOUT_MS) {
        return m_cachedDevices;
    }

    // Always return cached data if available and trigger async refresh to avoid blocking
    if (m_lastCacheUpdate.isValid() && !m_discoveryInProgress) {
        qCDebug(log_device_linux) << "Cache stale, returning cached data and triggering async refresh";
        // Trigger async discovery for next time
        discoverDevicesAsync();
        return m_cachedDevices;
    }

    // Only do blocking discovery on very first call when no cache exists
    if (!m_lastCacheUpdate.isValid() && !m_discoveryInProgress) {
        qCDebug(log_device_linux) << "No cache available, performing initial blocking discovery...";
        
        QList<DeviceInfo> devices = discoverDevicesBlocking();
        
        // Update cache
        m_cachedDevices = devices;
        m_lastCacheUpdate = now;
        
        qCDebug(log_device_linux) << "Initial discovery found" << devices.size() << "Openterface devices";
        return devices;
    }

    // If discovery is already in progress, return current cache
    qCDebug(log_device_linux) << "Discovery in progress, returning cached data";
    return m_cachedDevices;
}

QList<DeviceInfo> LinuxDeviceManager::discoverDevicesBlocking()
{
    qCDebug(log_device_linux) << "Discovering Openterface devices on Linux using libudev...";
    
    QList<DeviceInfo> devices;

#ifdef HAVE_LIBUDEV
    if (!m_udev) {
        qCWarning(log_device_linux) << "udev context not initialized";
        return devices;
    }

    try {
        // Search for Generation 1 devices
        qCDebug(log_device_linux) << "=== Searching for Generation 1 devices ===";
        QList<DeviceInfo> gen1Devices = discoverGeneration1DevicesLinux();
        devices.append(gen1Devices);
        qCDebug(log_device_linux) << "Found" << gen1Devices.size() << "Generation 1 devices";
        
        // Search for Generation 2 devices
        qCDebug(log_device_linux) << "=== Searching for Generation 2 devices ===";
        QList<DeviceInfo> gen2Devices = discoverGeneration2DevicesLinux();
        devices.append(gen2Devices);
        qCDebug(log_device_linux) << "Found" << gen2Devices.size() << "Generation 2 devices";
        
        // Search for Generation 3 devices
        qCDebug(log_device_linux) << "=== Searching for Generation 3 devices ===";
        QList<DeviceInfo> gen3Devices = discoverGeneration3DevicesLinux();
        devices.append(gen3Devices);
        qCDebug(log_device_linux) << "Found" << gen3Devices.size() << "Generation 3 devices";
        
    } catch (const std::exception& e) {
        qCWarning(log_device_linux) << "Error discovering devices with libudev:" << e.what();
    }
#else
    qCWarning(log_device_linux) << "libudev not available, cannot discover devices";
#endif

    return devices;
}

void LinuxDeviceManager::discoverDevicesAsync()
{
    if (m_discoveryInProgress) {
        qCDebug(log_device_linux) << "Async discovery already in progress, skipping";
        return;
    }
    
    qCDebug(log_device_linux) << "Starting async device discovery...";
    m_discoveryInProgress = true;
    
    // Start async discovery using QtConcurrent
    QFuture<QList<DeviceInfo>> future = QtConcurrent::run([this]() {
        return discoverDevicesBlocking();
    });
    
    m_futureWatcher->setFuture(future);
}

void LinuxDeviceManager::onAsyncDiscoveryFinished()
{
    m_discoveryInProgress = false;
    
    if (m_futureWatcher->isCanceled()) {
        qCDebug(log_device_linux) << "Async discovery was canceled";
        return;
    }
    
    try {
        QList<DeviceInfo> devices = m_futureWatcher->result();
        
        // Update cache
        m_cachedDevices = devices;
        m_lastCacheUpdate = QDateTime::currentDateTime();
        
        qCDebug(log_device_linux) << "Async discovery completed, found" << devices.size() << "devices";
        
        // Emit signal that devices were discovered
        emit devicesDiscovered(devices);
        
    } catch (const std::exception& e) {
        QString errorMsg = QString("Async device discovery failed: %1").arg(e.what());
        qCWarning(log_device_linux) << errorMsg;
        emit discoveryError(errorMsg);
    }
}

#ifdef HAVE_LIBUDEV
QList<LinuxDeviceManager::UdevDeviceData> LinuxDeviceManager::findUdevDevicesByVidPid(const QString& subsystem, const QString& vid, const QString& pid)
{
    QList<UdevDeviceData> devices;
    
    if (!m_udev) {
        return devices;
    }
    
    struct udev_enumerate *enumerate = udev_enumerate_new(m_udev);
    if (!enumerate) {
        return devices;
    }
    
    // Set up filters
    udev_enumerate_add_match_subsystem(enumerate, subsystem.toLocal8Bit().constData());
    
    // Scan devices
    udev_enumerate_scan_devices(enumerate);
    
    struct udev_list_entry *devices_list = udev_enumerate_get_list_entry(enumerate);
    struct udev_list_entry *dev_list_entry;
    
    udev_list_entry_foreach(dev_list_entry, devices_list) {
        const char *syspath = udev_list_entry_get_name(dev_list_entry);
        struct udev_device *device = udev_device_new_from_syspath(m_udev, syspath);
        
        if (!device) {
            continue;
        }
        
        // Walk up the device tree to find USB device with our VID/PID
        struct udev_device *usb_device = findUsbParentDevice(device);
        if (usb_device) {
            const char *device_vid = udev_device_get_sysattr_value(usb_device, "idVendor");
            const char *device_pid = udev_device_get_sysattr_value(usb_device, "idProduct");
            
            if (device_vid && device_pid) {
                QString deviceVidStr = QString(device_vid).toUpper();
                QString devicePidStr = QString(device_pid).toUpper();
                QString targetVidStr = vid.toUpper();
                QString targetPidStr = pid.toUpper();
                
                if (deviceVidStr == targetVidStr && devicePidStr == targetPidStr) {
                    UdevDeviceData deviceData;
                    deviceData.syspath = QString(syspath);
                    deviceData.portChain = extractPortChainFromSyspath(QString(udev_device_get_syspath(usb_device)));
                    deviceData.parentSyspath = QString(udev_device_get_syspath(usb_device));
                    
                    // Collect device properties
                    deviceData.properties = collectDeviceProperties(device);
                    deviceData.properties["VID"] = deviceVidStr;
                    deviceData.properties["PID"] = devicePidStr;
                    
                    devices.append(deviceData);
                    
                    qCDebug(log_device_linux) << "Found" << subsystem << "device:" << syspath 
                                             << "VID:PID" << deviceVidStr << ":" << devicePidStr
                                             << "Port chain:" << deviceData.portChain;
                }
            }
            udev_device_unref(usb_device);
        }
        
        udev_device_unref(device);
    }
    
    udev_enumerate_unref(enumerate);
    return devices;
}

QList<LinuxDeviceManager::UdevDeviceData> LinuxDeviceManager::findUdevDevices(const QString& subsystem, const QVariantMap& filters)
{
    QList<UdevDeviceData> devices;
    
    if (!m_udev) {
        return devices;
    }
    
    struct udev_enumerate *enumerate = udev_enumerate_new(m_udev);
    if (!enumerate) {
        return devices;
    }
    
    // Set up filters
    udev_enumerate_add_match_subsystem(enumerate, subsystem.toLocal8Bit().constData());
    
    // Add additional filters
    for (auto it = filters.begin(); it != filters.end(); ++it) {
        const QString key = it.key();
        const QString value = it.value().toString();
        udev_enumerate_add_match_property(enumerate, key.toLocal8Bit().constData(), 
                                        value.toLocal8Bit().constData());
    }
    
    // Scan devices
    udev_enumerate_scan_devices(enumerate);
    
    struct udev_list_entry *devices_list = udev_enumerate_get_list_entry(enumerate);
    struct udev_list_entry *dev_list_entry;
    
    udev_list_entry_foreach(dev_list_entry, devices_list) {
        const char *syspath = udev_list_entry_get_name(dev_list_entry);
        struct udev_device *device = udev_device_new_from_syspath(m_udev, syspath);
        
        if (!device) {
            continue;
        }
        
        UdevDeviceData deviceData;
        deviceData.syspath = QString(syspath);
        
        // Find USB parent
        struct udev_device *usb_device = findUsbParentDevice(device);
        if (usb_device) {
            deviceData.parentSyspath = QString(udev_device_get_syspath(usb_device));
            deviceData.portChain = extractPortChainFromSyspath(deviceData.parentSyspath);
            udev_device_unref(usb_device);
        }
        
        // Collect device properties
        deviceData.properties = collectDeviceProperties(device);
        
        devices.append(deviceData);
        udev_device_unref(device);
    }
    
    udev_enumerate_unref(enumerate);
    return devices;
}

struct udev_device* LinuxDeviceManager::findUsbParentDevice(struct udev_device* device)
{
    if (!device) {
        return nullptr;
    }
    
    struct udev_device *parent = device;
    udev_device_ref(parent); // Add reference for initial device
    
    while (parent) {
        const char *subsystem = udev_device_get_subsystem(parent);
        if (subsystem && strcmp(subsystem, "usb") == 0) {
            const char *devtype = udev_device_get_devtype(parent);
            if (devtype && strcmp(devtype, "usb_device") == 0) {
                return parent; // Found USB device, caller owns this reference
            }
        }
        
        struct udev_device *next_parent = udev_device_get_parent(parent);
        udev_device_unref(parent);
        
        if (!next_parent) {
            break;
        }
        
        parent = next_parent;
        udev_device_ref(parent); // Add reference for next iteration
    }
    
    return nullptr;
}

QVariantMap LinuxDeviceManager::collectDeviceProperties(struct udev_device* device)
{
    QVariantMap properties;
    
    if (!device) {
        return properties;
    }
    
    // Get basic device info
    const char *syspath = udev_device_get_syspath(device);
    const char *sysname = udev_device_get_sysname(device);
    const char *devnode = udev_device_get_devnode(device);
    const char *subsystem = udev_device_get_subsystem(device);
    const char *devtype = udev_device_get_devtype(device);
    
    if (syspath) properties["SYSPATH"] = QString(syspath);
    if (sysname) properties["SYSNAME"] = QString(sysname);
    if (devnode) properties["DEVNAME"] = QString(devnode);
    if (subsystem) properties["SUBSYSTEM"] = QString(subsystem);
    if (devtype) properties["DEVTYPE"] = QString(devtype);
    
    // Get properties
    struct udev_list_entry *properties_list = udev_device_get_properties_list_entry(device);
    struct udev_list_entry *property_entry;
    
    udev_list_entry_foreach(property_entry, properties_list) {
        const char *name = udev_list_entry_get_name(property_entry);
        const char *value = udev_list_entry_get_value(property_entry);
        
        if (name && value) {
            properties[QString(name)] = QString(value);
        }
    }
    
    return properties;
}

QString LinuxDeviceManager::extractPortChainFromSyspath(const QString& syspath)
{
    // Extract port chain from udev syspath, ensuring we get the actual USB device port
    // not the interface-specific paths like 1-2.1, 1-2.2, etc.
    // Example: /sys/devices/pci0000:00/0000:00:14.0/usb1/1-2/1-2.1 -> "1-2" (not "1-2.1")
    //          /sys/devices/pci0000:00/0000:00:14.0/usb1/1-2/1-2.2 -> "1-2" (not "1-2.2")
    
    qCDebug(log_device_linux) << "Extracting port chain from syspath:" << syspath;
    
    // Pattern to find USB device port in the path
    // This matches the actual USB device location, not interface sub-paths
    QRegularExpression usbDevicePattern(".*/usb\\d+/(\\d+-\\d+(?:\\.\\d+)*?)(?:/\\d+-\\d+\\.\\d+.*|/.*)?$");
    QRegularExpressionMatch match = usbDevicePattern.match(syspath);
    if (match.hasMatch()) {
        QString portChain = match.captured(1);
        
        // If this looks like an interface path (e.g., "1-2.1", "1-2.2"), 
        // extract the parent device port (e.g., "1-2")
        QRegularExpression interfacePattern("^(\\d+-\\d+(?:\\.\\d+)*?)\\.(\\d+)$");
        QRegularExpressionMatch interfaceMatch = interfacePattern.match(portChain);
        if (interfaceMatch.hasMatch()) {
            QString parentPort = interfaceMatch.captured(1);
            qCDebug(log_device_linux) << "Interface port" << portChain << "-> parent device port" << parentPort;
            return parentPort;
        }
        
        // qCDebug(log_device_linux) << "Extracted port chain" << portChain << "from" << syspath;
        return portChain;
    }
    
    // Alternative patterns for different syspath formats
    QRegularExpression patterns[] = {
        QRegularExpression(".*/usb\\d+/(\\d+-\\d+(?:\\.\\d+)*)(?:/.*)?$"),
        QRegularExpression(".*/(\\d+-\\d+(?:\\.\\d+)*)(?:/.*)?$")
    };
    
    for (const auto& pattern : patterns) {
        QRegularExpressionMatch altMatch = pattern.match(syspath);
        if (altMatch.hasMatch()) {
            QString portChain = altMatch.captured(1);
            
            // Remove interface suffix if present
            QRegularExpression interfacePattern("^(\\d+-\\d+(?:\\.\\d+)*?)\\.(\\d+)$");
            QRegularExpressionMatch interfaceMatch = interfacePattern.match(portChain);
            if (interfaceMatch.hasMatch()) {
                QString parentPort = interfaceMatch.captured(1);
                qCDebug(log_device_linux) << "Interface port" << portChain << "-> parent device port" << parentPort;
                return parentPort;
            }
            
            qCDebug(log_device_linux) << "Extracted port chain" << portChain << "from" << syspath;
            return portChain;
        }
    }
    
    // Fallback: extract components and find the actual USB device port
    QStringList pathParts = syspath.split('/');
    for (int i = pathParts.size() - 1; i >= 0; i--) {
        const QString& part = pathParts[i];
        if (QRegularExpression("^\\d+-\\d+(?:\\.\\d+)*$").match(part).hasMatch()) {
            // Check if this is an interface path and extract parent if so
            QRegularExpression interfacePattern("^(\\d+-\\d+(?:\\.\\d+)*?)\\.(\\d+)$");
            QRegularExpressionMatch interfaceMatch = interfacePattern.match(part);
            if (interfaceMatch.hasMatch()) {
                QString parentPort = interfaceMatch.captured(1);
                qCDebug(log_device_linux) << "Found interface port" << part << "-> parent device port" << parentPort;
                return parentPort;
            }
            qCDebug(log_device_linux) << "Found port chain in path part:" << part;
            return part;
        }
    }
    
    qCWarning(log_device_linux) << "Could not extract port chain from" << syspath;
    return QString();
}

QString LinuxDeviceManager::getPortChainFromSyspath(const QString& syspath)
{
    return extractPortChainFromSyspath(syspath);
}

QString LinuxDeviceManager::extractHubPortFromDevicePort(const QString& devicePort)
{
    // Extract hub port from device port
    // Example: "1-2.1" -> "1-2" (hub port)
    //          "1-2.2" -> "1-2" (hub port)
    //          "1-2" -> "1-2" (already a hub port)
    
    if (devicePort.isEmpty()) {
        return QString();
    }
    
    // Check if this is already a hub port (no dot in the last component)
    QStringList parts = devicePort.split('.');
    if (parts.size() <= 1) {
        // Already a hub port or invalid format
        return devicePort;
    }
    
    // Remove the last component after the last dot to get the hub port
    parts.removeLast();
    QString hubPort = parts.join('.');
    
    qCDebug(log_device_linux) << "Device port" << devicePort << "-> hub port" << hubPort;
    return hubPort;
}
#endif // HAVE_LIBUDEV

QList<DeviceInfo> LinuxDeviceManager::discoverGeneration1DevicesLinux()
{
    QList<DeviceInfo> devices;
    
    qCDebug(log_device_linux) << "Discovering Generation 1 devices (Original VID/PID approach)...";
    
    // Create device map to group devices by their parent hub port chain
    // This ensures that all devices connected to the same physical Openterface unit
    // are grouped together regardless of their individual VID/PID
    QMap<QString, DeviceInfo> deviceMap;
    
    // Find USB devices with serial VID/PID (1A86:7523)
    QList<UdevDeviceData> serialDevices = findUdevDevicesByVidPid("usb", 
        AbstractPlatformDeviceManager::SERIAL_VID, 
        AbstractPlatformDeviceManager::SERIAL_PID);
    qCDebug(log_device_linux) << "Found" << serialDevices.size() << "Gen1 serial USB devices";
    for (const auto& dev : serialDevices) {
        qCDebug(log_device_linux) << "  Serial device:" << dev.syspath << "port:" << dev.portChain;
    }
    
    // Find USB devices with HID VID/PID (534D:2109)
    QList<UdevDeviceData> hidUsbDevices = findUdevDevicesByVidPid("usb", 
        AbstractPlatformDeviceManager::OPENTERFACE_VID, 
        AbstractPlatformDeviceManager::OPENTERFACE_PID);
    qCDebug(log_device_linux) << "Found" << hidUsbDevices.size() << "Gen1 HID USB devices";
    for (const auto& dev : hidUsbDevices) {
        qCDebug(log_device_linux) << "  HID device:" << dev.syspath << "port:" << dev.portChain;
    }
    
    return processDeviceMap(serialDevices, hidUsbDevices, deviceMap, "Gen1");
}

QList<DeviceInfo> LinuxDeviceManager::discoverGeneration2DevicesLinux()
{
    QList<DeviceInfo> devices;
    
    qCDebug(log_device_linux) << "Discovering Generation 2 devices (Companion device approach)...";
    
    // Find USB devices with companion VID/PID (345F:2130) first
    // These are the Openterface devices that integrate camera, audio, and HID
    QList<UdevDeviceData> companionUsbDevices = findUdevDevicesByVidPid("usb", 
        AbstractPlatformDeviceManager::OPENTERFACE_VID_V2, 
        AbstractPlatformDeviceManager::OPENTERFACE_PID_V2);
    qCDebug(log_device_linux) << "Found" << companionUsbDevices.size() << "Gen2 companion USB devices";
    for (const auto& dev : companionUsbDevices) {
        qCDebug(log_device_linux) << "  Companion device:" << dev.syspath << "port:" << dev.portChain;
    }
    
    // Find USB devices with serial VID/PID (1A86:CH32V208)
    QList<UdevDeviceData> serialDevices = findUdevDevicesByVidPid("usb", 
        AbstractPlatformDeviceManager::SERIAL_VID_V2, 
        AbstractPlatformDeviceManager::SERIAL_PID_V2);
    qCDebug(log_device_linux) << "Found" << serialDevices.size() << "Gen2 serial USB devices";
    for (const auto& dev : serialDevices) {
        qCDebug(log_device_linux) << "  Serial device:" << dev.syspath << "port:" << dev.portChain;
    }
    
    // Process each companion device and find its associated serial device
    for (int i = 0; i < companionUsbDevices.size(); ++i) {
        const UdevDeviceData& companionDevice = companionUsbDevices[i];
        
        qCDebug(log_device_linux) << "=== Processing Gen2 Companion Device" << (i + 1) << "===";
        qCDebug(log_device_linux) << "Companion Port Chain:" << companionDevice.portChain;
        qCDebug(log_device_linux) << "Companion Syspath:" << companionDevice.syspath;
        
        DeviceInfo deviceInfo;
        deviceInfo.portChain = companionDevice.portChain;
        deviceInfo.deviceInstanceId = companionDevice.syspath;
        deviceInfo.vid = AbstractPlatformDeviceManager::OPENTERFACE_VID_V2;
        deviceInfo.pid = AbstractPlatformDeviceManager::OPENTERFACE_PID_V2;
        deviceInfo.lastSeen = QDateTime::currentDateTime();
        deviceInfo.platformSpecific = companionDevice.properties;
        
        // Find associated serial device using Generation 2 logic
        QString associatedSerialPortId = findSerialPortByCompanionDeviceLinux(companionDevice, serialDevices);
        if (!associatedSerialPortId.isEmpty()) {
            deviceInfo.serialPortId = associatedSerialPortId;
            qCDebug(log_device_linux) << "  ✓ Found associated serial port device ID:" << associatedSerialPortId;
        } else {
            qCDebug(log_device_linux) << "  ✗ Could not find associated serial port for companion device";
        }
        
        // Find associated interface devices (HID, camera, audio) for the companion device
        findAndAssociateInterfaceDevicesLinux(deviceInfo, companionDevice);
        
        devices.append(deviceInfo);
        qCDebug(log_device_linux) << "Gen2 Device" << (i + 1) << "processing complete";
        qCDebug(log_device_linux) << "  Final device summary:";
        qCDebug(log_device_linux) << "    Serial:" << (deviceInfo.hasSerialPort() ? deviceInfo.serialPortPath : "None");
        qCDebug(log_device_linux) << "    HID:" << (deviceInfo.hasHidDevice() ? deviceInfo.hidDevicePath : "None");
        qCDebug(log_device_linux) << "    Camera:" << (deviceInfo.hasCameraDevice() ? deviceInfo.cameraDevicePath : "None");
        qCDebug(log_device_linux) << "    Audio:" << (deviceInfo.hasAudioDevice() ? deviceInfo.audioDevicePath : "None");
    }
    
    return devices;
}

QList<DeviceInfo> LinuxDeviceManager::discoverGeneration3DevicesLinux()
{
    QList<DeviceInfo> devices;
    
    qCDebug(log_device_linux) << "Discovering Generation 3 devices (V3 VID/PID approach)...";
    qCDebug(log_device_linux) << "Looking for V3 companion devices with VID/PID 345F:2109";
    qCDebug(log_device_linux) << "Associated serial devices should have VID/PID 1A86:CH32V208";
    
    // Find USB devices with V3 companion VID/PID (345F:2109)
    QList<UdevDeviceData> companionUsbDevices = findUdevDevicesByVidPid("usb", 
        AbstractPlatformDeviceManager::OPENTERFACE_VID_V3, 
        AbstractPlatformDeviceManager::OPENTERFACE_PID_V3);
    qCDebug(log_device_linux) << "Found" << companionUsbDevices.size() << "V3 companion USB devices";
    for (const auto& dev : companionUsbDevices) {
        qCDebug(log_device_linux) << "  V3 Companion device:" << dev.syspath << "port:" << dev.portChain;
    }
    
    // Find USB devices with serial VID/PID (1A86:CH32V208) - same as Gen2
    QList<UdevDeviceData> serialDevices = findUdevDevicesByVidPid("usb", 
        AbstractPlatformDeviceManager::SERIAL_VID_V3, 
        AbstractPlatformDeviceManager::SERIAL_PID_V3);
    qCDebug(log_device_linux) << "Found" << serialDevices.size() << "V3 serial USB devices";
    for (const auto& dev : serialDevices) {
        qCDebug(log_device_linux) << "  V3 Serial device:" << dev.syspath << "port:" << dev.portChain;
    }
    
    // Process each V3 companion device and find its associated serial device
    for (int i = 0; i < companionUsbDevices.size(); ++i) {
        const UdevDeviceData& companionDevice = companionUsbDevices[i];
        
        qCDebug(log_device_linux) << "=== Processing V3 Companion Device" << (i + 1) << "===";
        qCDebug(log_device_linux) << "V3 Companion Port Chain:" << companionDevice.portChain;
        qCDebug(log_device_linux) << "V3 Companion Syspath:" << companionDevice.syspath;
        
        DeviceInfo deviceInfo;
        deviceInfo.portChain = companionDevice.portChain;
        deviceInfo.deviceInstanceId = companionDevice.syspath;
        deviceInfo.vid = AbstractPlatformDeviceManager::OPENTERFACE_VID_V3;
        deviceInfo.pid = AbstractPlatformDeviceManager::OPENTERFACE_PID_V3;
        deviceInfo.lastSeen = QDateTime::currentDateTime();
        deviceInfo.platformSpecific = companionDevice.properties;
        
        // Find associated serial device using Generation 2 logic (same approach)
        QString associatedSerialPortId = findSerialPortByCompanionDeviceLinux(companionDevice, serialDevices);
        if (!associatedSerialPortId.isEmpty()) {
            deviceInfo.serialPortId = associatedSerialPortId;
            qCDebug(log_device_linux) << "  ✓ Found associated V3 serial port device ID:" << associatedSerialPortId;
        } else {
            qCDebug(log_device_linux) << "  ✗ Could not find associated serial port for V3 companion device";
        }
        
        // Find associated interface devices (HID, camera, audio) for the V3 companion device
        findAndAssociateInterfaceDevicesLinux(deviceInfo, companionDevice);
        
        devices.append(deviceInfo);
        qCDebug(log_device_linux) << "V3 Device" << (i + 1) << "processing complete";
        qCDebug(log_device_linux) << "  Final V3 device summary:";
        qCDebug(log_device_linux) << "    Serial:" << (deviceInfo.hasSerialPort() ? deviceInfo.serialPortPath : "None");
        qCDebug(log_device_linux) << "    HID:" << (deviceInfo.hasHidDevice() ? deviceInfo.hidDevicePath : "None");
        qCDebug(log_device_linux) << "    Camera:" << (deviceInfo.hasCameraDevice() ? deviceInfo.cameraDevicePath : "None");
        qCDebug(log_device_linux) << "    Audio:" << (deviceInfo.hasAudioDevice() ? deviceInfo.audioDevicePath : "None");
    }
    
    return devices;
}

QList<DeviceInfo> LinuxDeviceManager::processDeviceMap(const QList<UdevDeviceData>& serialDevices, 
                                                      const QList<UdevDeviceData>& usbDevices, 
                                                      QMap<QString, DeviceInfo>& deviceMap, 
                                                      const QString& generation)
{
    QList<DeviceInfo> devices;
    
    // Create a map to track which hub ports have Openterface devices
    QMap<QString, QString> hubPortToDevicePort;
    
    // Process serial devices and map them to their parent hub ports
    for (const UdevDeviceData& serialDevice : serialDevices) {
        QString devicePort = serialDevice.portChain; // e.g., "1-2.2"
        QString hubPort = extractHubPortFromDevicePort(devicePort); // e.g., "1-2"
        
        if (!hubPort.isEmpty()) {
            hubPortToDevicePort[hubPort] = devicePort;
            
            DeviceInfo deviceInfo;
            deviceInfo.portChain = hubPort; // Use hub port as the common identifier
            deviceInfo.deviceInstanceId = serialDevice.syspath;
            deviceInfo.vid = AbstractPlatformDeviceManager::SERIAL_VID;
            deviceInfo.pid = AbstractPlatformDeviceManager::SERIAL_PID;
            deviceInfo.platformSpecific = serialDevice.properties;
            deviceInfo.lastSeen = QDateTime::currentDateTime();
            
            deviceMap[hubPort] = deviceInfo;
            qCDebug(log_device_linux) << generation << "Serial device at device port" << devicePort << "-> hub port" << hubPort;
        }
    }
    
    // Process USB devices and add them to existing entries or create new ones
    for (const UdevDeviceData& usbDevice : usbDevices) {
        QString devicePort = usbDevice.portChain; // e.g., "1-2.1"
        QString hubPort = extractHubPortFromDevicePort(devicePort); // e.g., "1-2"
        
        if (!hubPort.isEmpty()) {
            if (!deviceMap.contains(hubPort)) {
                DeviceInfo deviceInfo;
                deviceInfo.portChain = hubPort; // Use hub port as the common identifier
                deviceInfo.deviceInstanceId = usbDevice.syspath;
                deviceInfo.vid = AbstractPlatformDeviceManager::OPENTERFACE_VID;
                deviceInfo.pid = AbstractPlatformDeviceManager::OPENTERFACE_PID;
                deviceInfo.platformSpecific = usbDevice.properties;
                deviceInfo.lastSeen = QDateTime::currentDateTime();
                
                deviceMap[hubPort] = deviceInfo;
            }
            qCDebug(log_device_linux) << generation << "USB device at device port" << devicePort << "-> hub port" << hubPort;
        }
    }
    
    // Find associated tty devices for serial communication
    QList<UdevDeviceData> ttyDevices = findUdevDevices("tty", QVariantMap());
    qCDebug(log_device_linux) << "Found" << ttyDevices.size() << "tty devices for" << generation;
    for (const UdevDeviceData& ttyDevice : ttyDevices) {
        // Check if this tty device belongs to one of our USB devices
        struct udev_device *device = udev_device_new_from_syspath(m_udev, ttyDevice.syspath.toLocal8Bit().constData());
        if (device) {
            struct udev_device *usb_device = findUsbParentDevice(device);
            if (usb_device) {
                const char *vid = udev_device_get_sysattr_value(usb_device, "idVendor");
                const char *pid = udev_device_get_sysattr_value(usb_device, "idProduct");
                
                if (vid && pid) {
                    QString deviceVidStr = QString(vid).toUpper();
                    QString devicePidStr = QString(pid).toUpper();
                    
                    // Check for all generation VID/PID combinations
                    bool isMatchingSerial = false;
                    if (generation == "Gen1") {
                        isMatchingSerial = (deviceVidStr == AbstractPlatformDeviceManager::SERIAL_VID.toUpper() && 
                                          devicePidStr == AbstractPlatformDeviceManager::SERIAL_PID.toUpper());
                    } else if (generation == "Gen2" || generation == "Gen3") {
                        isMatchingSerial = (deviceVidStr == AbstractPlatformDeviceManager::SERIAL_VID_V2.toUpper() && 
                                          devicePidStr == AbstractPlatformDeviceManager::SERIAL_PID_V2.toUpper());
                    }
                    
                    if (isMatchingSerial) {
                        QString devicePortChain = extractPortChainFromSyspath(QString(udev_device_get_syspath(usb_device)));
                        QString hubPort = extractHubPortFromDevicePort(devicePortChain);
                        
                        if (!hubPort.isEmpty() && deviceMap.contains(hubPort)) {
                            QString devNode = ttyDevice.properties.value("DEVNAME").toString();
                            if (!devNode.isEmpty()) {
                                deviceMap[hubPort].serialPortPath = devNode;
                                deviceMap[hubPort].serialPortId = ttyDevice.syspath;
                                qCDebug(log_device_linux) << "Found" << generation << "tty device:" << devNode << "at device port:" << devicePortChain << "for hub port:" << hubPort;
                            }
                        }
                    }
                }
                udev_device_unref(usb_device);
            }
            udev_device_unref(device);
        }
    }
    
    // Fallback: Use QSerialPortInfo to find serial ports with matching VID/PID for devices that don't have tty paths yet
    const auto serialPorts = QSerialPortInfo::availablePorts();
    for (auto it = deviceMap.begin(); it != deviceMap.end(); ++it) {
        if (it.value().serialPortPath.isEmpty()) {
            for (const QSerialPortInfo& portInfo : serialPorts) {
                if (portInfo.hasVendorIdentifier() && portInfo.hasProductIdentifier()) {
                    QString vidStr = QString("%1").arg(portInfo.vendorIdentifier(), 4, 16, QChar('0')).toUpper();
                    QString pidStr = QString("%1").arg(portInfo.productIdentifier(), 4, 16, QChar('0')).toUpper();
                    
                    bool isMatchingSerial = false;
                    if (generation == "Gen1") {
                        isMatchingSerial = (vidStr == AbstractPlatformDeviceManager::SERIAL_VID.toUpper() && 
                                          pidStr == AbstractPlatformDeviceManager::SERIAL_PID.toUpper());
                    } else if (generation == "Gen2" || generation == "Gen3") {
                        isMatchingSerial = (vidStr == AbstractPlatformDeviceManager::SERIAL_VID_V2.toUpper() && 
                                          pidStr == AbstractPlatformDeviceManager::SERIAL_PID_V2.toUpper());
                    }
                    
                    if (isMatchingSerial) {
                        QString portPath = portInfo.systemLocation();
                        if (!portPath.isEmpty()) {
                            it.value().serialPortPath = portPath;
                            qCDebug(log_device_linux) << "Found" << generation << "serial port (fallback):" << portPath << "for hub port:" << it.key();
                            break; // Use the first matching port
                        }
                    }
                }
            }
        }
    }
    
    // Find hidraw devices for HID communication
    QList<UdevDeviceData> hidrawDevices = findUdevDevices("hidraw", QVariantMap());
    qCDebug(log_device_linux) << "Found" << hidrawDevices.size() << "hidraw devices for" << generation;
    for (const UdevDeviceData& hidrawDevice : hidrawDevices) {
        // Check if this hidraw device belongs to one of our USB devices
        struct udev_device *device = udev_device_new_from_syspath(m_udev, hidrawDevice.syspath.toLocal8Bit().constData());
        if (device) {
            struct udev_device *usb_device = findUsbParentDevice(device);
            if (usb_device) {
                const char *vid = udev_device_get_sysattr_value(usb_device, "idVendor");
                const char *pid = udev_device_get_sysattr_value(usb_device, "idProduct");
                
                if (vid && pid) {
                    QString deviceVidStr = QString(vid).toUpper();
                    QString devicePidStr = QString(pid).toUpper();
                    
                    // Check for both generation VID/PID combinations
                    bool isMatchingHid = false;
                    if (generation == "Gen1") {
                        isMatchingHid = (deviceVidStr == AbstractPlatformDeviceManager::OPENTERFACE_VID.toUpper() && 
                                       devicePidStr == AbstractPlatformDeviceManager::OPENTERFACE_PID.toUpper());
                    } else if (generation == "Gen2") {
                        isMatchingHid = (deviceVidStr == AbstractPlatformDeviceManager::OPENTERFACE_VID_V2.toUpper() && 
                                       devicePidStr == AbstractPlatformDeviceManager::OPENTERFACE_PID_V2.toUpper());
                    }
                    
                    if (isMatchingHid) {
                        QString devicePortChain = extractPortChainFromSyspath(QString(udev_device_get_syspath(usb_device)));
                        QString hubPort = extractHubPortFromDevicePort(devicePortChain);
                        
                        if (!hubPort.isEmpty() && deviceMap.contains(hubPort)) {
                            QString devNode = hidrawDevice.properties.value("DEVNAME").toString();
                            if (!devNode.isEmpty()) {
                                deviceMap[hubPort].hidDevicePath = devNode;
                                deviceMap[hubPort].hidDeviceId = hidrawDevice.syspath;
                                qCDebug(log_device_linux) << "Found" << generation << "HID device:" << devNode << "at device port:" << devicePortChain << "for hub port:" << hubPort;
                            }
                        }
                    }
                }
                udev_device_unref(usb_device);
            }
            udev_device_unref(device);
        }
    }
    
    // Find video devices
    QList<UdevDeviceData> videoDevices = findUdevDevices("video4linux", QVariantMap());
    qCDebug(log_device_linux) << "Found" << videoDevices.size() << "video4linux devices for" << generation;
    for (const UdevDeviceData& videoDevice : videoDevices) {
        // Check if this video device belongs to one of our USB devices
        struct udev_device *device = udev_device_new_from_syspath(m_udev, videoDevice.syspath.toLocal8Bit().constData());
        if (device) {
            struct udev_device *usb_device = findUsbParentDevice(device);
            if (usb_device) {
                QString devicePortChain = extractPortChainFromSyspath(QString(udev_device_get_syspath(usb_device)));
                QString hubPort = extractHubPortFromDevicePort(devicePortChain);
                
                if (!hubPort.isEmpty() && deviceMap.contains(hubPort)) {
                    QString devNode = videoDevice.properties.value("DEVNAME").toString();
                    if (!devNode.isEmpty() && devNode.contains("video")) {
                        // Prefer video0 over video1 for camera device
                        if (deviceMap[hubPort].cameraDevicePath.isEmpty() || devNode.contains("video0")) {
                            deviceMap[hubPort].cameraDevicePath = devNode;
                            deviceMap[hubPort].cameraDeviceId = devNode;  // Use device path as ID for Linux
                            qCDebug(log_device_linux) << "Set" << generation << "camera info for hub port" << hubPort 
                                                     << "- Path:" << devNode 
                                                     << "- ID:" << devNode;
                        }
                        qCDebug(log_device_linux) << "Found" << generation << "video device:" << devNode << "at device port:" << devicePortChain << "for hub port:" << hubPort;
                    }
                }
                udev_device_unref(usb_device);
            }
            udev_device_unref(device);
        }
    }
    
    // Find audio devices
    QList<UdevDeviceData> audioDevices = findUdevDevices("sound", QVariantMap());
    qCDebug(log_device_linux) << "Found" << audioDevices.size() << "sound devices for" << generation;
    for (const UdevDeviceData& audioDevice : audioDevices) {
        // Check if this audio device belongs to one of our USB devices
        struct udev_device *device = udev_device_new_from_syspath(m_udev, audioDevice.syspath.toLocal8Bit().constData());
        if (device) {
            struct udev_device *usb_device = findUsbParentDevice(device);
            if (usb_device) {
                QString devicePortChain = extractPortChainFromSyspath(QString(udev_device_get_syspath(usb_device)));
                QString hubPort = extractHubPortFromDevicePort(devicePortChain);
                
                if (!hubPort.isEmpty() && deviceMap.contains(hubPort)) {
                    QString devNode = audioDevice.properties.value("DEVNAME").toString();
                    if (!devNode.isEmpty() && (devNode.contains("pcm") || devNode.contains("control"))) {
                        deviceMap[hubPort].audioDevicePath = devNode;
                        deviceMap[hubPort].audioDeviceId = audioDevice.syspath;
                        qCDebug(log_device_linux) << "Found" << generation << "audio device:" << devNode << "at device port:" << devicePortChain << "for hub port:" << hubPort;
                    }
                }
                udev_device_unref(usb_device);
            }
            udev_device_unref(device);
        }
    }
    
    // Convert map to list
    for (auto it = deviceMap.begin(); it != deviceMap.end(); ++it) {
        if (it.value().isValid()) {
            devices.append(it.value());
            qCDebug(log_device_linux) << "Found complete" << generation << "device with port chain:" << it.value().portChain
                                      << "serial:" << it.value().serialPortPath
                                      << "hid:" << it.value().hidDevicePath
                                      << "camera:" << it.value().cameraDevicePath
                                      << "audio:" << it.value().audioDevicePath;
        } else {
            qCDebug(log_device_linux) << "Found incomplete" << generation << "device with port chain:" << it.key()
                                      << "serial:" << it.value().serialPortPath
                                      << "hid:" << it.value().hidDevicePath
                                      << "camera:" << it.value().cameraDevicePath
                                      << "audio:" << it.value().audioDevicePath;
        }
    }
    
    return devices;
}

void LinuxDeviceManager::clearCache()
{
    qCDebug(log_device_linux) << "Clearing device cache";
    m_cachedDevices.clear();
    m_lastCacheUpdate = QDateTime();
}

#ifdef HAVE_LIBUDEV
QString LinuxDeviceManager::findSerialPortByCompanionDeviceLinux(const UdevDeviceData& companionDevice, const QList<UdevDeviceData>& serialDevices)
{
    qCDebug(log_device_linux) << "Searching for serial port associated with companion device...";
    
    // Extract the companion port chain and analyze the hub structure
    QString companionPortChain = companionDevice.portChain;
    qCDebug(log_device_linux) << "Companion device port chain:" << companionPortChain;
    qCDebug(log_device_linux) << "Companion device syspath:" << companionDevice.syspath;
    
    // For Generation 2, the topology is:
    // USB Hub (General-purpose) -> contains both:
    //   1. Serial device (1A86:CH32V208) 
    //   2. Companion device (345F:2130) - this is the Openterface device with camera/audio/HID
    
    // Find the parent hub of the companion device
    QString companionHubPort = extractHubPortFromDevicePortLinux(companionPortChain);
    qCDebug(log_device_linux) << "Companion device hub port:" << companionHubPort;
    
    // For Generation 2, calculate the expected serial port hub by incrementing the last number
    // If companion is at "1-0", serial should be at "1-1"
    QString expectedSerialHubPort = calculateExpectedSerialHubPortLinux(companionHubPort);
    qCDebug(log_device_linux) << "Expected serial hub port:" << expectedSerialHubPort;
    
    for (const UdevDeviceData& serialDevice : serialDevices) {
        // For Generation 2, we need to get the actual USB device port chain
        // The serialDevice.portChain comes from the USB device with VID/PID 1A86:CH32V208
        QString serialPortChain = serialDevice.portChain;
        QString serialHubPort = extractHubPortFromDevicePortLinux(serialPortChain);
        
        qCDebug(log_device_linux) << "Checking serial device:";
        qCDebug(log_device_linux) << "  USB device syspath:" << serialDevice.syspath;
        qCDebug(log_device_linux) << "  USB device port chain:" << serialPortChain;
        qCDebug(log_device_linux) << "  USB device hub port:" << serialHubPort;
        
        // Primary check: does the serial hub port match our expected port (companion + 1)?
        if (!expectedSerialHubPort.isEmpty() && serialHubPort == expectedSerialHubPort) {
            qCDebug(log_device_linux) << "✓ Found serial device at expected hub port:" << expectedSerialHubPort;
            qCDebug(log_device_linux) << "  Companion hub port:" << companionHubPort;
            qCDebug(log_device_linux) << "  Serial hub port:" << serialHubPort;
            
            // Additional verification: check if they are indeed companion devices
            if (isSerialDeviceAssociatedWithCompanionLinux(serialDevice, companionDevice)) {
                qCDebug(log_device_linux) << "✓ Verified companion relationship";
                return serialDevice.syspath;
            }
        }
        
        // Fallback check: if both devices are under the same parent hub (old logic)
        if (!companionHubPort.isEmpty() && !serialHubPort.isEmpty() && 
            companionHubPort == serialHubPort) {
            qCDebug(log_device_linux) << "✓ Found serial device under same hub as companion device (fallback)";
            qCDebug(log_device_linux) << "  Shared hub port:" << companionHubPort;
            
            // Additional verification: check if they are indeed companion devices
            if (isSerialDeviceAssociatedWithCompanionLinux(serialDevice, companionDevice)) {
                qCDebug(log_device_linux) << "✓ Verified companion relationship (fallback)";
                return serialDevice.syspath;
            }
        }
        
        // Also check if they are direct siblings (same hub, different ports)
        if (arePortChainsRelatedLinux(serialPortChain, companionPortChain)) {
            qCDebug(log_device_linux) << "✓ Found related serial device (sibling relationship)";
            
            // Verify this is actually our target serial device
            if (isSerialDeviceAssociatedWithCompanionLinux(serialDevice, companionDevice)) {
                qCDebug(log_device_linux) << "✓ Verified sibling companion relationship";
                return serialDevice.syspath;
            }
        }
    }
    
    qCDebug(log_device_linux) << "✗ No associated serial device found for companion device";
    return QString();
}

void LinuxDeviceManager::findAndAssociateInterfaceDevicesLinux(DeviceInfo& deviceInfo, const UdevDeviceData& companionDevice)
{
    qCDebug(log_device_linux) << "Finding and associating interface devices for companion device...";
    
    // Find hidraw devices associated with this companion device
    QList<UdevDeviceData> hidrawDevices = findUdevDevices("hidraw", QVariantMap());
    for (const UdevDeviceData& hidrawDevice : hidrawDevices) {
        struct udev_device *device = udev_device_new_from_syspath(m_udev, hidrawDevice.syspath.toLocal8Bit().constData());
        if (device) {
            struct udev_device *usb_device = findUsbParentDevice(device);
            if (usb_device) {
                const char *vid = udev_device_get_sysattr_value(usb_device, "idVendor");
                const char *pid = udev_device_get_sysattr_value(usb_device, "idProduct");
                
                if (vid && pid) {
                    QString deviceVidStr = QString(vid).toUpper();
                    QString devicePidStr = QString(pid).toUpper();
                    
                    // Check if this HID device belongs to our companion device (Gen2 VID/PID)
                    if (deviceVidStr == AbstractPlatformDeviceManager::OPENTERFACE_VID_V2.toUpper() && 
                        devicePidStr == AbstractPlatformDeviceManager::OPENTERFACE_PID_V2.toUpper()) {
                        
                        QString devicePortChain = extractPortChainFromSyspath(QString(udev_device_get_syspath(usb_device)));
                        
                        // Check if this HID device is from the same companion device
                        if (devicePortChain == companionDevice.portChain) {
                            QString devNode = hidrawDevice.properties.value("DEVNAME").toString();
                            if (!devNode.isEmpty()) {
                                deviceInfo.hidDevicePath = devNode;
                                deviceInfo.hidDeviceId = hidrawDevice.syspath;
                                qCDebug(log_device_linux) << "Found Gen2 HID device:" << devNode << "for companion device";
                            }
                        }
                    }
                }
                udev_device_unref(usb_device);
            }
            udev_device_unref(device);
        }
    }
    
    // Find video devices associated with this companion device
    QList<UdevDeviceData> videoDevices = findUdevDevices("video4linux", QVariantMap());
    for (const UdevDeviceData& videoDevice : videoDevices) {
        struct udev_device *device = udev_device_new_from_syspath(m_udev, videoDevice.syspath.toLocal8Bit().constData());
        if (device) {
            struct udev_device *usb_device = findUsbParentDevice(device);
            if (usb_device) {
                QString devicePortChain = extractPortChainFromSyspath(QString(udev_device_get_syspath(usb_device)));
                
                // Check if this video device is from the same companion device
                if (devicePortChain == companionDevice.portChain) {
                    QString devNode = videoDevice.properties.value("DEVNAME").toString();
                    if (!devNode.isEmpty() && devNode.contains("video")) {
                        // Prefer video0 over video1 for camera device
                        if (deviceInfo.cameraDevicePath.isEmpty() || devNode.contains("video0")) {
                            deviceInfo.cameraDevicePath = devNode;
                            deviceInfo.cameraDeviceId = devNode;
                            qCDebug(log_device_linux) << "Found Gen2 camera device:" << devNode << "for companion device";
                        }
                    }
                }
                udev_device_unref(usb_device);
            }
            udev_device_unref(device);
        }
    }
    
    // Find audio devices associated with this companion device
    QList<UdevDeviceData> audioDevices = findUdevDevices("sound", QVariantMap());
    for (const UdevDeviceData& audioDevice : audioDevices) {
        struct udev_device *device = udev_device_new_from_syspath(m_udev, audioDevice.syspath.toLocal8Bit().constData());
        if (device) {
            struct udev_device *usb_device = findUsbParentDevice(device);
            if (usb_device) {
                QString devicePortChain = extractPortChainFromSyspath(QString(udev_device_get_syspath(usb_device)));
                
                // Check if this audio device is from the same companion device
                if (devicePortChain == companionDevice.portChain) {
                    QString devNode = audioDevice.properties.value("DEVNAME").toString();
                    if (!devNode.isEmpty() && (devNode.contains("pcm") || devNode.contains("control"))) {
                        deviceInfo.audioDevicePath = devNode;
                        deviceInfo.audioDeviceId = audioDevice.syspath;
                        qCDebug(log_device_linux) << "Found Gen2 audio device:" << devNode << "for companion device";
                    }
                }
                udev_device_unref(usb_device);
            }
            udev_device_unref(device);
        }
    }
    
    // Find tty devices for the associated serial device
    if (!deviceInfo.serialPortId.isEmpty()) {
        QList<UdevDeviceData> ttyDevices = findUdevDevices("tty", QVariantMap());
        for (const UdevDeviceData& ttyDevice : ttyDevices) {
            struct udev_device *device = udev_device_new_from_syspath(m_udev, ttyDevice.syspath.toLocal8Bit().constData());
            if (device) {
                struct udev_device *usb_device = findUsbParentDevice(device);
                if (usb_device) {
                    const char *vid = udev_device_get_sysattr_value(usb_device, "idVendor");
                    const char *pid = udev_device_get_sysattr_value(usb_device, "idProduct");
                    
                    if (vid && pid) {
                        QString deviceVidStr = QString(vid).toUpper();
                        QString devicePidStr = QString(pid).toUpper();
                        
                        // Check if this TTY device belongs to our serial device (Gen2/Gen3 VID/PID)
                        if ((deviceVidStr == AbstractPlatformDeviceManager::SERIAL_VID_V2.toUpper() && 
                             devicePidStr == AbstractPlatformDeviceManager::SERIAL_PID_V2.toUpper()) ||
                            (deviceVidStr == AbstractPlatformDeviceManager::SERIAL_VID_V3.toUpper() && 
                             devicePidStr == AbstractPlatformDeviceManager::SERIAL_PID_V3.toUpper())) {
                            
                            QString devicePortChain = extractPortChainFromSyspath(QString(udev_device_get_syspath(usb_device)));
                            QString deviceSyspath = QString(udev_device_get_syspath(usb_device));
                            
                            // Check if this TTY device belongs to our associated serial device
                            if (deviceSyspath == deviceInfo.serialPortId) {
                                QString devNode = ttyDevice.properties.value("DEVNAME").toString();
                                if (!devNode.isEmpty()) {
                                    deviceInfo.serialPortPath = devNode;
                                    qCDebug(log_device_linux) << "Found serial port:" << devNode << "for companion device (udev)";
                                }
                            }
                        }
                    }
                    udev_device_unref(usb_device);
                }
                udev_device_unref(device);
            }
        }
        
        // Fallback: Use QSerialPortInfo to find serial ports with matching VID/PID
        if (deviceInfo.serialPortPath.isEmpty()) {
            const auto serialPorts = QSerialPortInfo::availablePorts();
            for (const QSerialPortInfo& portInfo : serialPorts) {
                if (portInfo.hasVendorIdentifier() && portInfo.hasProductIdentifier()) {
                    QString vidStr = QString("%1").arg(portInfo.vendorIdentifier(), 4, 16, QChar('0')).toUpper();
                    QString pidStr = QString("%1").arg(portInfo.productIdentifier(), 4, 16, QChar('0')).toUpper();
                    
                    // Check for CH32V208 devices (Gen2/Gen3)
                    if (vidStr == AbstractPlatformDeviceManager::SERIAL_VID_V3.toUpper() && 
                        pidStr == AbstractPlatformDeviceManager::SERIAL_PID_V3.toUpper()) {
                        
                        QString portPath = portInfo.systemLocation();
                        if (!portPath.isEmpty()) {
                            deviceInfo.serialPortPath = portPath;
                            qCDebug(log_device_linux) << "Found serial port:" << portPath << "for companion device (QSerialPortInfo fallback)";
                            break; // Use the first matching port
                        }
                    }
                }
            }
        }
    }
}

QString LinuxDeviceManager::calculateExpectedSerialHubPortLinux(const QString& companionHubPort)
{
    // For Generation 2 devices, calculate the expected serial port hub 
    // by incrementing the last number in the companion hub port
    // Example: if companion is at "1-0", serial should be at "1-1"
    
    if (companionHubPort.isEmpty()) {
        return QString();
    }
    
    qCDebug(log_device_linux) << "Calculating expected serial hub port from companion hub port:" << companionHubPort;
    
    // Look for patterns like "1-0", "2-3", etc.
    QRegularExpression pattern(R"(^(.+-)(\d+)$)");
    QRegularExpressionMatch match = pattern.match(companionHubPort);
    
    if (match.hasMatch()) {
        QString prefix = match.captured(1);  // e.g., "1-"
        int lastNumber = match.captured(2).toInt();  // e.g., 0
        int expectedSerialNumber = lastNumber + 1;  // e.g., 1
        
        QString expectedSerialHubPort = prefix + QString::number(expectedSerialNumber);
        qCDebug(log_device_linux) << "Expected serial hub port:" << expectedSerialHubPort 
                                 << "(prefix:" << prefix << "companion number:" << lastNumber 
                                 << "serial number:" << expectedSerialNumber << ")";
        return expectedSerialHubPort;
    }
    
    // Try alternative pattern matching for different port formats
    // Look for patterns that might contain port numbers that we can increment
    if (companionHubPort.contains("-")) {
        QStringList parts = companionHubPort.split('-');
        if (parts.size() >= 2) {
            QString lastPart = parts.last();
            bool ok;
            int lastNumber = lastPart.toInt(&ok);
            if (ok) {
                parts.removeLast();
                parts.append(QString::number(lastNumber + 1));
                QString expectedSerialHubPort = parts.join('-');
                qCDebug(log_device_linux) << "Expected serial hub port (alternative):" << expectedSerialHubPort;
                return expectedSerialHubPort;
            }
        }
    }
    
    qCDebug(log_device_linux) << "Could not calculate expected serial hub port from:" << companionHubPort;
    return QString();
}

QString LinuxDeviceManager::extractHubPortFromDevicePortLinux(const QString& devicePort)
{
    // This is the same as the existing extractHubPortFromDevicePort method
    // but with a different name for clarity in Generation 2 context
    return extractHubPortFromDevicePort(devicePort);
}

bool LinuxDeviceManager::arePortChainsRelatedLinux(const QString& portChain1, const QString& portChain2)
{
    if (portChain1.isEmpty() || portChain2.isEmpty()) {
        return false;
    }
    
    qCDebug(log_device_linux) << "Checking port chain relationship between:" << portChain1 << "and" << portChain2;
    
    // Check if the port chains are exactly the same
    if (portChain1 == portChain2) {
        qCDebug(log_device_linux) << "✓ Exact match";
        return true;
    }
    
    // Check if they share the same base hub
    QString hubPort1 = extractHubPortFromDevicePortLinux(portChain1);
    QString hubPort2 = extractHubPortFromDevicePortLinux(portChain2);
    
    if (!hubPort1.isEmpty() && !hubPort2.isEmpty() && hubPort1 == hubPort2) {
        qCDebug(log_device_linux) << "✓ Same base hub:" << hubPort1;
        return true;
    }
    
    // Check if one is a sub-device of the other
    if (portChain1.startsWith(portChain2 + ".") || portChain2.startsWith(portChain1 + ".")) {
        qCDebug(log_device_linux) << "✓ Sub-device relationship";
        return true;
    }
    
    // Check if they are sequential ports (e.g., "1-0" and "1-1")
    QRegularExpression pattern(R"(^(.+-)(\d+)$)");
    QRegularExpressionMatch match1 = pattern.match(portChain1);
    QRegularExpressionMatch match2 = pattern.match(portChain2);
    
    if (match1.hasMatch() && match2.hasMatch()) {
        QString prefix1 = match1.captured(1);
        QString prefix2 = match2.captured(1);
        
        if (prefix1 == prefix2) {
            int num1 = match1.captured(2).toInt();
            int num2 = match2.captured(2).toInt();
            
            // Consider them related if they are within 2 port numbers of each other
            if (std::abs(num1 - num2) <= 2) {
                qCDebug(log_device_linux) << "✓ Sequential ports within range:" << num1 << "and" << num2;
                return true;
            }
        }
    }
    
    // For Generation 2, check for different bus/root hub but potentially related
    // Some Generation 2 devices may appear on different USB controllers but still be related
    // Check if both devices have simple port patterns and could be companion devices
    QRegularExpression simplePattern(R"(^(\d+)-(\d+)$)");
    QRegularExpressionMatch simple1 = simplePattern.match(portChain1);
    QRegularExpressionMatch simple2 = simplePattern.match(portChain2);
    
    if (simple1.hasMatch() && simple2.hasMatch()) {
        int bus1 = simple1.captured(1).toInt();
        int bus2 = simple2.captured(1).toInt();
        
        // Check if they could be companion devices on different buses
        // This is a heuristic for Generation 2 devices that might be on different controllers
        if (bus1 != bus2) {
            qCDebug(log_device_linux) << "✓ Different buses detected - treating as potentially related Gen2 devices";
            return true;
        }
    }
    
    qCDebug(log_device_linux) << "✗ No relationship found";
    return false;
}

bool LinuxDeviceManager::isSerialDeviceAssociatedWithCompanionLinux(const UdevDeviceData& serialDevice, const UdevDeviceData& companionDevice)
{
    // For the companion device approach, we need to check if the serial device
    // and companion device are related through their port chains or parent devices
    
    QString serialPortChain = serialDevice.portChain;
    QString companionPortChain = companionDevice.portChain;
    
    qCDebug(log_device_linux) << "Comparing port chains - Serial:" << serialPortChain << "Companion:" << companionPortChain;
    
    // Method 1: Check if serial port is at expected hub port (companion + 1)
    // Extract the hub part of the port chain (everything before the last dot)
    QString serialHubPort = extractHubPortFromDevicePortLinux(serialPortChain);
    QString companionHubPort = extractHubPortFromDevicePortLinux(companionPortChain);
    qCDebug(log_device_linux) << "Serial hub port:" << serialHubPort << "Companion hub port:" << companionHubPort;
    
    // For Generation 2, calculate the expected serial hub port by incrementing companion port
    QString expectedSerialHubPort = calculateExpectedSerialHubPortLinux(companionHubPort);
    if (!expectedSerialHubPort.isEmpty() && serialHubPort == expectedSerialHubPort) {
        qCDebug(log_device_linux) << "✓ Serial device at expected hub port (companion + 1):" << expectedSerialHubPort;
        return true;
    }
    
    // Fallback: Check if they share the same hub (legacy logic for edge cases)
    if (!serialHubPort.isEmpty() && !companionHubPort.isEmpty() && serialHubPort == companionHubPort) {
        qCDebug(log_device_linux) << "✓ Devices share the same hub port (fallback):" << serialHubPort;
        return true;
    }
    
    // Method 3: Check if the port numbers are adjacent or have a specific relationship
    if (arePortChainsRelatedLinux(serialPortChain, companionPortChain)) {
        qCDebug(log_device_linux) << "✓ Port chains appear to be related";
        return true;
    }
    
    qCDebug(log_device_linux) << "✗ No relationship found between devices";
    return false;
}
#endif // HAVE_LIBUDEV

#endif // __linux__
