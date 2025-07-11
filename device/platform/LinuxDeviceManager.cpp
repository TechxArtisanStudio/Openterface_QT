#ifdef __linux__
#include "LinuxDeviceManager.h"
#include <QDebug>
#include <QDirIterator>
#include <QTextStream>
#include <QStandardPaths>
#include <QRegularExpression>

Q_LOGGING_CATEGORY(log_device_linux, "opf.device.linux")

LinuxDeviceManager::LinuxDeviceManager(QObject *parent)
    : AbstractPlatformDeviceManager(parent)
{
    qCDebug(log_device_linux) << "Linux Device Manager initialized";
}

LinuxDeviceManager::~LinuxDeviceManager()
{
}

QList<DeviceInfo> LinuxDeviceManager::discoverDevices()
{
    // Check cache first
    QDateTime now = QDateTime::currentDateTime();
    if (m_lastCacheUpdate.isValid() && 
        m_lastCacheUpdate.msecsTo(now) < CACHE_TIMEOUT_MS) {
        return m_cachedDevices;
    }

    qCDebug(log_device_linux) << "Discovering Openterface devices on Linux...";
    
    QList<DeviceInfo> devices;
    
    try {
        // Find all USB devices with our serial VID/PID (1A86:7523)
        QList<USBDeviceData> usbDevices = findUSBDevicesWithVidPid(SERIAL_VID, SERIAL_PID);
        
        for (const USBDeviceData& usbData : usbDevices) {
            DeviceInfo deviceInfo;
            deviceInfo.portChain = usbData.portChain;
            deviceInfo.deviceInstanceId = usbData.devicePath;
            deviceInfo.platformSpecific = usbData.deviceInfo;
            deviceInfo.lastSeen = QDateTime::currentDateTime();
            
            // Find serial ports by port chain
            QList<QVariantMap> serialPorts = findSerialPortsByPortChain(SERIAL_VID, SERIAL_PID, deviceInfo.portChain);
            if (!serialPorts.isEmpty()) {
                deviceInfo.serialPortPath = serialPorts.first().value("device_node").toString();
                deviceInfo.serialPortId = serialPorts.first().value("device_path").toString();
            }
            
            // Find HID devices by port chain
            QList<QVariantMap> hidDevices = findHIDDevicesByPortChain(HID_VID, HID_PID, deviceInfo.portChain);
            if (!hidDevices.isEmpty()) {
                deviceInfo.hidDevicePath = hidDevices.first().value("device_node").toString();
                deviceInfo.hidDeviceId = hidDevices.first().value("device_path").toString();
            }
            
            // Find video devices by port chain
            QList<QVariantMap> videoDevices = findVideoDevicesByPortChain(deviceInfo.portChain);
            if (!videoDevices.isEmpty()) {
                deviceInfo.cameraDevicePath = videoDevices.first().value("device_node").toString();
                deviceInfo.cameraDeviceId = videoDevices.first().value("device_path").toString();
            }
            
            // Find audio devices by port chain
            QList<QVariantMap> audioDevices = findAudioDevicesByPortChain(deviceInfo.portChain);
            if (!audioDevices.isEmpty()) {
                deviceInfo.audioDevicePath = audioDevices.first().value("device_node").toString();
                deviceInfo.audioDeviceId = audioDevices.first().value("device_path").toString();
            }
            
            if (deviceInfo.isValid()) {
                devices.append(deviceInfo);
                qCDebug(log_device_linux) << "Found device:" << deviceInfo.portChain;
            }
        }
    } catch (const std::exception& e) {
        qCWarning(log_device_linux) << "Error discovering devices:" << e.what();
    }
    
    // Update cache
    m_cachedDevices = devices;
    m_lastCacheUpdate = now;
    
    qCDebug(log_device_linux) << "Found" << devices.size() << "Openterface devices";
    return devices;
}

QList<LinuxDeviceManager::USBDeviceData> LinuxDeviceManager::findUSBDevicesWithVidPid(const QString& vid, const QString& pid)
{
    QList<USBDeviceData> devices;
    
    // Search in /sys/bus/usb/devices
    QDir usbDevicesDir("/sys/bus/usb/devices");
    if (!usbDevicesDir.exists()) {
        qCWarning(log_device_linux) << "USB devices directory not found";
        return devices;
    }
    
    QStringList deviceDirs = usbDevicesDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    
    for (const QString& deviceDir : deviceDirs) {
        QString devicePath = usbDevicesDir.absoluteFilePath(deviceDir);
        
        // Check if this device matches our VID/PID
        if (matchesVidPid(devicePath, vid, pid)) {
            USBDeviceData usbData;
            usbData.devicePath = devicePath;
            usbData.portChain = buildLinuxPortChain(devicePath);
            
            // Read device information
            usbData.deviceInfo["vendor_id"] = readSysfsAttribute(devicePath, "idVendor");
            usbData.deviceInfo["product_id"] = readSysfsAttribute(devicePath, "idProduct");
            usbData.deviceInfo["manufacturer"] = readSysfsAttribute(devicePath, "manufacturer");
            usbData.deviceInfo["product"] = readSysfsAttribute(devicePath, "product");
            usbData.deviceInfo["serial"] = readSysfsAttribute(devicePath, "serial");
            usbData.deviceInfo["device_path"] = devicePath;
            
            // Find child and sibling devices
            usbData.children = findChildDevices(devicePath).value("children").toList();
            usbData.siblings = findSiblingDevices(devicePath).value("siblings").toList();
            
            devices.append(usbData);
            
            qCDebug(log_device_linux) << "Found USB device:" << devicePath 
                                     << "Port chain:" << usbData.portChain;
        }
    }
    
    return devices;
}

QString LinuxDeviceManager::buildLinuxPortChain(const QString& devpath)
{
    // Extract port chain from device path, similar to Python implementation
    // Example: /sys/bus/usb/devices/1-1.2.3 -> "1-1.2.3"
    // Also handle: /sys/devices/pci0000:00/0000:00:14.0/usb1/1-1/1-1.2/1-1.2.3
    
    QRegularExpression patterns[] = {
        // Match patterns like: /sys/bus/usb/devices/1-1.2.3
        QRegularExpression(".*/(\\d+(?:-\\d+(?:\\.\\d+)*))$"),
        // Match patterns like: /sys/devices/.../usb1/1-1/1-1.2/1-1.2.3
        QRegularExpression(".*/usb\\d+/(\\d+(?:-\\d+(?:\\.\\d+)*))(?:/.*)?$"),
        // Match the final component if it looks like a port chain
        QRegularExpression(".*/(\\d+-\\d+(?:\\.\\d+)*)$")
    };
    
    for (const auto& pattern : patterns) {
        QRegularExpressionMatch match = pattern.match(devpath);
        if (match.hasMatch()) {
            QString portChain = match.captured(1);
            qCDebug(log_device_linux) << "Extracted port chain" << portChain << "from" << devpath;
            return portChain;
        }
    }
    
    // Enhanced fallback: try to parse from directory structure
    QStringList pathParts = devpath.split('/');
    for (int i = pathParts.size() - 1; i >= 0; i--) {
        const QString& part = pathParts[i];
        if (QRegularExpression("^\\d+-\\d+(?:\\.\\d+)*$").match(part).hasMatch()) {
            qCDebug(log_device_linux) << "Found port chain in path part:" << part;
            return part;
        }
    }
    
    // Final fallback: use basename
    QString basename = QFileInfo(devpath).baseName();
    qCWarning(log_device_linux) << "Could not extract port chain from" << devpath << "using basename:" << basename;
    return basename;
}

QList<QVariantMap> LinuxDeviceManager::findSerialPortsByPortChain(const QString& serialVid, const QString& serialPid, const QString& targetPortChain)
{
    QList<QVariantMap> serialPorts;
    QString mainPort = extractMainPortFromChain(targetPortChain);
    
    // Search in /sys/class/tty
    QDir ttyDir("/sys/class/tty");
    if (!ttyDir.exists()) {
        return serialPorts;
    }
    
    QStringList ttyDevices = ttyDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    
    for (const QString& ttyDevice : ttyDevices) {
        // Skip non-USB serial devices
        if (!ttyDevice.startsWith("ttyUSB") && !ttyDevice.startsWith("ttyACM")) {
            continue;
        }
        
        QString ttyPath = ttyDir.absoluteFilePath(ttyDevice);
        QString devicePath = QDir(ttyPath).canonicalPath();
        
        // Follow symlink to get actual device path
        QFileInfo linkInfo(ttyPath);
        if (linkInfo.isSymLink()) {
            QString target = linkInfo.symLinkTarget();
            if (target.contains(mainPort)) {
                // Check if this device matches our VID/PID by traversing up the device tree
                QString currentPath = devicePath;
                while (!currentPath.isEmpty() && currentPath != "/") {
                    if (matchesVidPid(currentPath, serialVid, serialPid)) {
                        QString portChain = getDevicePortChain(currentPath);
                        if (portChain == targetPortChain) {
                            QVariantMap serialPort;
                            serialPort["device_node"] = "/dev/" + ttyDevice;
                            serialPort["device_path"] = currentPath;
                            serialPort["port_chain"] = portChain;
                            serialPorts.append(serialPort);
                            break;
                        }
                    }
                    // Move up one level
                    currentPath = QDir(currentPath).absoluteFilePath("..");
                    currentPath = QDir(currentPath).canonicalPath();
                }
            }
        }
    }
    
    return serialPorts;
}

QList<QVariantMap> LinuxDeviceManager::findHIDDevicesByPortChain(const QString& hidVid, const QString& hidPid, const QString& targetPortChain)
{
    QList<QVariantMap> hidDevices;
    QString mainPort = extractMainPortFromChain(targetPortChain);
    
    // Search in /sys/class/hidraw
    QDir hidrawDir("/sys/class/hidraw");
    if (!hidrawDir.exists()) {
        return hidDevices;
    }
    
    QStringList hidrawDevices = hidrawDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    
    for (const QString& hidrawDevice : hidrawDevices) {
        QString hidrawPath = hidrawDir.absoluteFilePath(hidrawDevice);
        QString devicePath = QDir(hidrawPath).canonicalPath();
        
        // Follow symlink and traverse up to find USB device
        QFileInfo linkInfo(hidrawPath);
        if (linkInfo.isSymLink()) {
            QString target = linkInfo.symLinkTarget();
            if (target.contains(mainPort)) {
                QString currentPath = devicePath;
                while (!currentPath.isEmpty() && currentPath != "/") {
                    if (matchesVidPid(currentPath, hidVid, hidPid)) {
                        QString portChain = getDevicePortChain(currentPath);
                        if (portChain == targetPortChain) {
                            QVariantMap hidDevice;
                            hidDevice["device_node"] = "/dev/" + hidrawDevice;
                            hidDevice["device_path"] = currentPath;
                            hidDevice["port_chain"] = portChain;
                            hidDevices.append(hidDevice);
                            break;
                        }
                    }
                    currentPath = QDir(currentPath).absoluteFilePath("..");
                    currentPath = QDir(currentPath).canonicalPath();
                }
            }
        }
    }
    
    return hidDevices;
}

QList<QVariantMap> LinuxDeviceManager::findVideoDevicesByPortChain(const QString& targetPortChain)
{
    QList<QVariantMap> videoDevices;
    QString mainPort = extractMainPortFromChain(targetPortChain);
    
    // Search in /sys/class/video4linux
    QDir videoDir("/sys/class/video4linux");
    if (!videoDir.exists()) {
        return videoDevices;
    }
    
    QStringList videoDeviceList = videoDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    
    for (const QString& videoDevice : videoDeviceList) {
        QString videoPath = videoDir.absoluteFilePath(videoDevice);
        QString devicePath = QDir(videoPath).canonicalPath();
        
        QFileInfo linkInfo(videoPath);
        if (linkInfo.isSymLink()) {
            QString target = linkInfo.symLinkTarget();
            if (target.contains(mainPort)) {
                QString currentPath = devicePath;
                while (!currentPath.isEmpty() && currentPath != "/") {
                    QString portChain = getDevicePortChain(currentPath);
                    if (portChain == targetPortChain) {
                        QVariantMap videoDeviceInfo;
                        videoDeviceInfo["device_node"] = "/dev/" + videoDevice;
                        videoDeviceInfo["device_path"] = currentPath;
                        videoDeviceInfo["port_chain"] = portChain;
                        videoDevices.append(videoDeviceInfo);
                        break;
                    }
                    currentPath = QDir(currentPath).absoluteFilePath("..");
                    currentPath = QDir(currentPath).canonicalPath();
                }
            }
        }
    }
    
    return videoDevices;
}

QList<QVariantMap> LinuxDeviceManager::findAudioDevicesByPortChain(const QString& targetPortChain)
{
    QList<QVariantMap> audioDevices;
    QString mainPort = extractMainPortFromChain(targetPortChain);
    
    // Search in /sys/class/sound
    QDir soundDir("/sys/class/sound");
    if (!soundDir.exists()) {
        return audioDevices;
    }
    
    QStringList soundDeviceList = soundDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    
    for (const QString& soundDevice : soundDeviceList) {
        // Look for card devices
        if (!soundDevice.startsWith("card")) {
            continue;
        }
        
        QString soundPath = soundDir.absoluteFilePath(soundDevice);
        QString devicePath = QDir(soundPath).canonicalPath();
        
        QFileInfo linkInfo(soundPath);
        if (linkInfo.isSymLink()) {
            QString target = linkInfo.symLinkTarget();
            if (target.contains(mainPort)) {
                QString currentPath = devicePath;
                while (!currentPath.isEmpty() && currentPath != "/") {
                    QString portChain = getDevicePortChain(currentPath);
                    if (portChain == targetPortChain) {
                        QVariantMap audioDevice;
                        audioDevice["device_node"] = "/dev/snd/" + soundDevice;
                        audioDevice["device_path"] = currentPath;
                        audioDevice["port_chain"] = portChain;
                        audioDevices.append(audioDevice);
                        break;
                    }
                    currentPath = QDir(currentPath).absoluteFilePath("..");
                    currentPath = QDir(currentPath).canonicalPath();
                }
            }
        }
    }
    
    return audioDevices;
}

// Utility method implementations
QString LinuxDeviceManager::extractMainPortFromChain(const QString& portChain)
{
    // Extract main port number from chain like "1-1.2.3" -> "1-1"
    QRegularExpression re("^(\\d+-\\d+)");
    QRegularExpressionMatch match = re.match(portChain);
    
    if (match.hasMatch()) {
        return match.captured(1);
    }
    
    return portChain;
}

QVariantMap LinuxDeviceManager::findChildDevices(const QString& devicePath)
{
    QVariantMap result;
    QList<QVariantMap> children;
    
    QDir deviceDir(devicePath);
    QStringList subdirs = deviceDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    
    for (const QString& subdir : subdirs) {
        QString childPath = deviceDir.absoluteFilePath(subdir);
        
        // Check if this looks like a USB device
        if (subdir.contains(QRegularExpression("^\\d+-\\d+"))) {
            QVariantMap child;
            child["device_path"] = childPath;
            child["vendor_id"] = readSysfsAttribute(childPath, "idVendor");
            child["product_id"] = readSysfsAttribute(childPath, "idProduct");
            child["port_chain"] = buildLinuxPortChain(childPath);
            children.append(child);
        }
    }
    
    result["children"] = children;
    return result;
}

QVariantMap LinuxDeviceManager::findSiblingDevices(const QString& devicePath)
{
    QVariantMap result;
    QList<QVariantMap> siblings;
    
    QDir parentDir = QFileInfo(devicePath).dir();
    QStringList subdirs = parentDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    
    for (const QString& subdir : subdirs) {
        QString siblingPath = parentDir.absoluteFilePath(subdir);
        
        // Skip self
        if (siblingPath == devicePath) {
            continue;
        }
        
        // Check if this looks like a USB device
        if (subdir.contains(QRegularExpression("^\\d+-\\d+"))) {
            QVariantMap sibling;
            sibling["device_path"] = siblingPath;
            sibling["vendor_id"] = readSysfsAttribute(siblingPath, "idVendor");
            sibling["product_id"] = readSysfsAttribute(siblingPath, "idProduct");
            sibling["port_chain"] = buildLinuxPortChain(siblingPath);
            siblings.append(sibling);
        }
    }
    
    result["siblings"] = siblings;
    return result;
}

// sysfs helper implementations
QString LinuxDeviceManager::readSysfsAttribute(const QString& devicePath, const QString& attribute)
{
    QFile file(devicePath + "/" + attribute);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream stream(&file);
        return stream.readLine().trimmed();
    }
    return QString();
}

QStringList LinuxDeviceManager::listSysfsDirectory(const QString& path)
{
    QDir dir(path);
    return dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
}

bool LinuxDeviceManager::deviceExists(const QString& devicePath)
{
    return QDir(devicePath).exists();
}

bool LinuxDeviceManager::matchesVidPid(const QString& devicePath, const QString& vid, const QString& pid)
{
    QString deviceVid = getDeviceVendorId(devicePath);
    QString devicePid = getDeviceProductId(devicePath);
    
    return (deviceVid.compare(vid, Qt::CaseInsensitive) == 0 &&
            devicePid.compare(pid, Qt::CaseInsensitive) == 0);
}

bool LinuxDeviceManager::matchesVidPidAdvanced(const QString& devicePath, const QString& vid, const QString& pid)
{
    // Enhanced VID/PID matching with better error handling
    QString deviceVid = readSysfsAttribute(devicePath, "idVendor");
    QString devicePid = readSysfsAttribute(devicePath, "idProduct");
    
    if (deviceVid.isEmpty() || devicePid.isEmpty()) {
        return false;
    }
    
    // Normalize VID/PID format (remove 0x prefix if present, convert to uppercase)
    auto normalizeId = [](const QString& id) -> QString {
        QString normalized = id.trimmed().toUpper();
        if (normalized.startsWith("0X")) {
            normalized = normalized.mid(2);
        }
        return normalized.rightJustified(4, '0'); // Pad to 4 digits
    };
    
    QString normalizedDeviceVid = normalizeId(deviceVid);
    QString normalizedDevicePid = normalizeId(devicePid);
    QString normalizedTargetVid = normalizeId(vid);
    QString normalizedTargetPid = normalizeId(pid);
    
    bool matches = (normalizedDeviceVid == normalizedTargetVid) && 
                   (normalizedDevicePid == normalizedTargetPid);
    
    if (matches) {
        qCDebug(log_device_linux) << "Device" << devicePath << "matches VID:PID" 
                                 << normalizedTargetVid << ":" << normalizedTargetPid;
    }
    
    return matches;
}

QString LinuxDeviceManager::getDeviceVendorId(const QString& devicePath)
{
    return readSysfsAttribute(devicePath, "idVendor");
}

QString LinuxDeviceManager::getDeviceProductId(const QString& devicePath)
{
    return readSysfsAttribute(devicePath, "idProduct");
}

QList<QVariantMap> LinuxDeviceManager::collectAllDevicesByPortChain(const QString& targetPortChain)
{
    QList<QVariantMap> allDevices;
    
    // Search for serial ports
    auto serialPorts = findSerialPortsByPortChain(SERIAL_VID, SERIAL_PID, targetPortChain);
    for (auto& port : serialPorts) {
        port["device_type"] = "serial";
        allDevices.append(port);
    }
    
    // Search for HID devices  
    auto hidDevices = findHIDDevicesByPortChain(HID_VID, HID_PID, targetPortChain);
    for (auto& device : hidDevices) {
        device["device_type"] = "hid";
        allDevices.append(device);
    }
    
    // Search for video devices
    auto videoDevices = findVideoDevicesByPortChain(targetPortChain);
    for (auto& device : videoDevices) {
        device["device_type"] = "video";
        allDevices.append(device);
    }
    
    // Search for audio devices
    auto audioDevices = findAudioDevicesByPortChain(targetPortChain);
    for (auto& device : audioDevices) {
        device["device_type"] = "audio";
        allDevices.append(device);
    }
    
    qCDebug(log_device_linux) << "Collected" << allDevices.size() << "devices for port chain" << targetPortChain;
    return allDevices;
}

void LinuxDeviceManager::clearCache()
{
    qCDebug(log_device_linux) << "Clearing device cache";
    m_cachedDevices.clear();
    m_lastCacheUpdate = QDateTime();
}

#endif // __linux__
