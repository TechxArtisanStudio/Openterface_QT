#ifdef __linux__
#include "LinuxDeviceManager.h"
#include <QDebug>
#include <QTextStream>
#include <QStandardPaths>
#include <QRegularExpression>

#ifdef HAVE_LIBUDEV
#include <libudev.h>
#include <cstring>
#include <memory>
#endif

Q_LOGGING_CATEGORY(log_device_linux, "opf.device.linux")

LinuxDeviceManager::LinuxDeviceManager(QObject *parent)
    : AbstractPlatformDeviceManager(parent)
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
    // Check cache first
    QDateTime now = QDateTime::currentDateTime();
    if (m_lastCacheUpdate.isValid() && 
        m_lastCacheUpdate.msecsTo(now) < CACHE_TIMEOUT_MS) {
        return m_cachedDevices;
    }

    qCDebug(log_device_linux) << "Discovering Openterface devices on Linux using libudev...";
    
    QList<DeviceInfo> devices;

#ifdef HAVE_LIBUDEV
    if (!m_udev) {
        qCWarning(log_device_linux) << "udev context not initialized";
        return devices;
    }

    try {
        // Create device map to group devices by their parent hub port chain
        // This ensures that all devices connected to the same physical Openterface unit
        // are grouped together regardless of their individual VID/PID
        QMap<QString, DeviceInfo> deviceMap;
        
        // Find USB devices with serial VID/PID (1A86:7523)
        QList<UdevDeviceData> serialDevices = findUdevDevicesByVidPid("usb", 
            AbstractPlatformDeviceManager::SERIAL_VID, 
            AbstractPlatformDeviceManager::SERIAL_PID);
        qCDebug(log_device_linux) << "Found" << serialDevices.size() << "serial USB devices";
        for (const auto& dev : serialDevices) {
            qCDebug(log_device_linux) << "  Serial device:" << dev.syspath << "port:" << dev.portChain;
        }
        
        // Find USB devices with HID VID/PID (534D:2109)
        QList<UdevDeviceData> hidUsbDevices = findUdevDevicesByVidPid("usb", 
            AbstractPlatformDeviceManager::HID_VID, 
            AbstractPlatformDeviceManager::HID_PID);
        qCDebug(log_device_linux) << "Found" << hidUsbDevices.size() << "HID USB devices";
        for (const auto& dev : hidUsbDevices) {
            qCDebug(log_device_linux) << "  HID device:" << dev.syspath << "port:" << dev.portChain;
        }
        
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
                deviceInfo.platformSpecific = serialDevice.properties;
                deviceInfo.lastSeen = QDateTime::currentDateTime();
                
                deviceMap[hubPort] = deviceInfo;
                qCDebug(log_device_linux) << "Serial device at device port" << devicePort << "-> hub port" << hubPort;
            }
        }
        
        // Process HID USB devices and add them to existing entries or create new ones
        for (const UdevDeviceData& hidUsbDevice : hidUsbDevices) {
            QString devicePort = hidUsbDevice.portChain; // e.g., "1-2.1"
            QString hubPort = extractHubPortFromDevicePort(devicePort); // e.g., "1-2"
            
            if (!hubPort.isEmpty()) {
                if (!deviceMap.contains(hubPort)) {
                    DeviceInfo deviceInfo;
                    deviceInfo.portChain = hubPort; // Use hub port as the common identifier
                    deviceInfo.deviceInstanceId = hidUsbDevice.syspath;
                    deviceInfo.platformSpecific = hidUsbDevice.properties;
                    deviceInfo.lastSeen = QDateTime::currentDateTime();
                    
                    deviceMap[hubPort] = deviceInfo;
                }
                qCDebug(log_device_linux) << "HID USB device at device port" << devicePort << "-> hub port" << hubPort;
            }
        }
        
        // Find associated tty devices for serial communication
        QList<UdevDeviceData> ttyDevices = findUdevDevices("tty", QVariantMap());
        qCDebug(log_device_linux) << "Found" << ttyDevices.size() << "tty devices";
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
                        
                        if (deviceVidStr == AbstractPlatformDeviceManager::SERIAL_VID.toUpper() && 
                            devicePidStr == AbstractPlatformDeviceManager::SERIAL_PID.toUpper()) {
                            
                            QString devicePortChain = extractPortChainFromSyspath(QString(udev_device_get_syspath(usb_device)));
                            QString hubPort = extractHubPortFromDevicePort(devicePortChain);
                            
                            if (!hubPort.isEmpty() && deviceMap.contains(hubPort)) {
                                QString devNode = ttyDevice.properties.value("DEVNAME").toString();
                                if (!devNode.isEmpty()) {
                                    deviceMap[hubPort].serialPortPath = devNode;
                                    deviceMap[hubPort].serialPortId = ttyDevice.syspath;
                                    qCDebug(log_device_linux) << "Found tty device:" << devNode << "at device port:" << devicePortChain << "for hub port:" << hubPort;
                                }
                            }
                        }
                    }
                    udev_device_unref(usb_device);
                }
                udev_device_unref(device);
            }
        }
        
        // Find hidraw devices for HID communication
        QList<UdevDeviceData> hidrawDevices = findUdevDevices("hidraw", QVariantMap());
        qCDebug(log_device_linux) << "Found" << hidrawDevices.size() << "hidraw devices";
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
                        
                        if (deviceVidStr == AbstractPlatformDeviceManager::HID_VID.toUpper() && 
                            devicePidStr == AbstractPlatformDeviceManager::HID_PID.toUpper()) {
                            
                            QString devicePortChain = extractPortChainFromSyspath(QString(udev_device_get_syspath(usb_device)));
                            QString hubPort = extractHubPortFromDevicePort(devicePortChain);
                            
                            if (!hubPort.isEmpty() && deviceMap.contains(hubPort)) {
                                QString devNode = hidrawDevice.properties.value("DEVNAME").toString();
                                if (!devNode.isEmpty()) {
                                    deviceMap[hubPort].hidDevicePath = devNode;
                                    deviceMap[hubPort].hidDeviceId = hidrawDevice.syspath;
                                    qCDebug(log_device_linux) << "Found HID device:" << devNode << "at device port:" << devicePortChain << "for hub port:" << hubPort;
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
        qCDebug(log_device_linux) << "Found" << videoDevices.size() << "video4linux devices";
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
                                qCDebug(log_device_linux) << "Set camera info for hub port" << hubPort 
                                                         << "- Path:" << devNode 
                                                         << "- ID:" << devNode;
                            }
                            qCDebug(log_device_linux) << "Found video device:" << devNode << "at device port:" << devicePortChain << "for hub port:" << hubPort;
                        }
                    }
                    udev_device_unref(usb_device);
                }
                udev_device_unref(device);
            }
        }
        
        // Find audio devices
        QList<UdevDeviceData> audioDevices = findUdevDevices("sound", QVariantMap());
        qCDebug(log_device_linux) << "Found" << audioDevices.size() << "sound devices";
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
                            qCDebug(log_device_linux) << "Found audio device:" << devNode << "at device port:" << devicePortChain << "for hub port:" << hubPort;
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
                qCDebug(log_device_linux) << "Found complete device with port chain:" << it.value().portChain
                                          << "serial:" << it.value().serialPortPath
                                          << "hid:" << it.value().hidDevicePath
                                          << "camera:" << it.value().cameraDevicePath
                                          << "audio:" << it.value().audioDevicePath;
            } else {
                qCDebug(log_device_linux) << "Found incomplete device with port chain:" << it.key()
                                          << "serial:" << it.value().serialPortPath
                                          << "hid:" << it.value().hidDevicePath
                                          << "camera:" << it.value().cameraDevicePath
                                          << "audio:" << it.value().audioDevicePath;
            }
        }
        
    } catch (const std::exception& e) {
        qCWarning(log_device_linux) << "Error discovering devices with libudev:" << e.what();
    }
#else
    qCWarning(log_device_linux) << "libudev not available, cannot discover devices";
#endif

    // Update cache
    m_cachedDevices = devices;
    m_lastCacheUpdate = now;
    
    qCDebug(log_device_linux) << "Found" << devices.size() << "Openterface devices";
    return devices;
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

void LinuxDeviceManager::clearCache()
{
    qCDebug(log_device_linux) << "Clearing device cache";
    m_cachedDevices.clear();
    m_lastCacheUpdate = QDateTime();
}

#endif // __linux__
