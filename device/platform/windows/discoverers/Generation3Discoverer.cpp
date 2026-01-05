#ifdef _WIN32
#include "Generation3Discoverer.h"
#include <QDebug>

Q_DECLARE_LOGGING_CATEGORY(log_device_discoverer)

Generation3Discoverer::Generation3Discoverer(std::shared_ptr<IDeviceEnumerator> enumerator, QObject* parent)
    : BaseDeviceDiscoverer(enumerator, parent)
{
    qCDebug(log_device_discoverer) << "Generation3Discoverer initialized";
}

QVector<DeviceInfo> Generation3Discoverer::discoverDevices()
{
    QVector<DeviceInfo> devices;
    QMap<QString, DeviceInfo> deviceMap; // Use port chain as key to prevent duplicates
    
    qCDebug(log_device_discoverer) << "=== Generation 3 Discovery Started ===";
    
    // Phase 1: Search for USB 3.0 integrated devices (VID_345F&PID_2132)  
    qCDebug(log_device_discoverer) << "Phase 1: Searching for USB 3.0 integrated devices (345F:2132)";
    QVector<USBDeviceData> integratedDevices = findUSBDevicesWithVidPid(
        OPENTERFACE_VID_V2, 
        OPENTERFACE_PID_V2
    );
    qCDebug(log_device_discoverer) << "Found" << integratedDevices.size() << "integrated devices (345F:2132)";
    
    for (int i = 0; i < integratedDevices.size(); ++i) {
        const USBDeviceData& integratedDevice = integratedDevices[i];
        
        qCDebug(log_device_discoverer) << "Processing Integrated Device" << (i + 1) << "at port chain:" << integratedDevice.portChain;
        
        // Find associated serial port through CompanionPortChain
        QString associatedSerialPortId = findSerialPortByIntegratedDevice(integratedDevice);
        QString associatedPortChain;
        
        if (!associatedSerialPortId.isEmpty()) {
            qCDebug(log_device_discoverer) << "Found associated serial port:" << associatedSerialPortId;
            // Get the port chain of the serial port to use as the device identifier
            DWORD serialDevInst = getDeviceInstanceFromId(associatedSerialPortId);
            if (serialDevInst != 0) {
                associatedPortChain = buildPythonCompatiblePortChain(serialDevInst);
                qCDebug(log_device_discoverer) << "Associated serial port chain:" << associatedPortChain;
            }
        } else {
            qCWarning(log_device_discoverer) << "No associated serial port found for integrated device";
            // Use the integrated device's port chain as fallback
            associatedPortChain = integratedDevice.portChain;
        }
        
        // Check if we already have a device for this port chain
        if (deviceMap.contains(associatedPortChain)) {
            qCDebug(log_device_discoverer) << "Enhancing existing device with integrated device interfaces at port chain:" << associatedPortChain;
            DeviceInfo& existingDevice = deviceMap[associatedPortChain];
            
            // Add integrated device interfaces to existing device
            processIntegratedDeviceInterfaces(existingDevice, integratedDevice);
            matchDevicePathsToRealPaths(existingDevice);
            
            qCDebug(log_device_discoverer) << "Enhanced device interfaces - HID:" << (existingDevice.hasHidDevice() ? "YES" : "NO")
                                          << "Camera:" << (existingDevice.hasCameraDevice() ? "YES" : "NO") 
                                          << "Audio:" << (existingDevice.hasAudioDevice() ? "YES" : "NO");
            continue;
        }
        
        // Create new device info
        DeviceInfo deviceInfo;
        deviceInfo.portChain = associatedPortChain;
        
        // CRITICAL: Use the COMPOSITE device's instance ID for interface enumeration
        // The composite device (345F:2132) has HID/camera/audio as child interfaces
        // The serial port is a separate sibling device for serial communication
        deviceInfo.deviceInstanceId = integratedDevice.deviceInstanceId;
        
        deviceInfo.vid = OPENTERFACE_VID_V2;
        deviceInfo.pid = OPENTERFACE_PID_V2;
        deviceInfo.lastSeen = QDateTime::currentDateTime();
        deviceInfo.platformSpecific = integratedDevice.deviceInfo;
        
        qCDebug(log_device_discoverer) << "Using COMPOSITE device ID for interface paths:" << deviceInfo.deviceInstanceId;
        
        // For USB 3.0 integrated devices, set up companion PortChain association
        if (associatedPortChain != integratedDevice.portChain) {
            deviceInfo.companionPortChain = integratedDevice.portChain;
            deviceInfo.hasCompanionDevice = true;
            qCDebug(log_device_discoverer) << "USB 3.0 device - Serial PortChain:" << associatedPortChain 
                                          << "Companion PortChain:" << integratedDevice.portChain;
        }
        
        // Set the serial port information if found (separate from composite device)
        if (!associatedSerialPortId.isEmpty()) {
            deviceInfo.serialPortId = associatedSerialPortId;
            qCDebug(log_device_discoverer) << "Associated serial port ID:" << associatedSerialPortId;
        }
        
        // Store siblings and children in platformSpecific
        QVariantList siblingVariants, childrenVariants;
        for (const QVariantMap& sibling : integratedDevice.siblings) {
            siblingVariants.append(sibling);
        }
        for (const QVariantMap& child : integratedDevice.children) {
            childrenVariants.append(child);
        }
        deviceInfo.platformSpecific["siblings"] = siblingVariants;
        deviceInfo.platformSpecific["children"] = childrenVariants;
        
        // Process the integrated device interfaces
        processIntegratedDeviceInterfaces(deviceInfo, integratedDevice);
        
        // Convert device IDs to real paths
        matchDevicePathsToRealPaths(deviceInfo);
        
        // Add to device map
        deviceMap[deviceInfo.portChain] = deviceInfo;
        qCDebug(log_device_discoverer) << "Integrated device added with port chain:" << deviceInfo.portChain;
    }
    
    // Phase 2: Search for V3 generation USB 3.0 integrated devices (VID_345F&PID_2109)
    qCDebug(log_device_discoverer) << "Phase 2: Searching for V3 USB 3.0 integrated devices (345F:2109)";
    QVector<USBDeviceData> v3IntegratedDevices = findUSBDevicesWithVidPid(
        OPENTERFACE_VID_V3, 
        OPENTERFACE_PID_V3
    );
    qCDebug(log_device_discoverer) << "Found" << v3IntegratedDevices.size() << "V3 integrated devices (345F:2109)";
    
    for (int i = 0; i < v3IntegratedDevices.size(); ++i) {
        const USBDeviceData& integratedDevice = v3IntegratedDevices[i];
        
        qCDebug(log_device_discoverer) << "Processing V3 Integrated Device" << (i + 1) << "at port chain:" << integratedDevice.portChain;
        
        // Find associated serial port through CompanionPortChain
        QString associatedSerialPortId = findSerialPortByIntegratedDevice(integratedDevice);
        QString associatedPortChain;
        
        if (!associatedSerialPortId.isEmpty()) {
            qCDebug(log_device_discoverer) << "Found associated serial port:" << associatedSerialPortId;
            // Get the port chain of the serial port to use as the device identifier
            DWORD serialDevInst = getDeviceInstanceFromId(associatedSerialPortId);
            if (serialDevInst != 0) {
                associatedPortChain = buildPythonCompatiblePortChain(serialDevInst);
                qCDebug(log_device_discoverer) << "Associated serial port chain:" << associatedPortChain;
            }
        } else {
            qCWarning(log_device_discoverer) << "No associated serial port found for V3 integrated device";
            // Use the integrated device's port chain as fallback
            associatedPortChain = integratedDevice.portChain;
        }
        
        // Check if we already have a device for this port chain
        if (deviceMap.contains(associatedPortChain)) {
            qCDebug(log_device_discoverer) << "Enhancing existing device with V3 integrated device interfaces at port chain:" << associatedPortChain;
            DeviceInfo& existingDevice = deviceMap[associatedPortChain];
            
            // Add integrated device interfaces to existing device
            processIntegratedDeviceInterfaces(existingDevice, integratedDevice);
            matchDevicePathsToRealPaths(existingDevice);
            
            qCDebug(log_device_discoverer) << "Enhanced device interfaces - HID:" << (existingDevice.hasHidDevice() ? "YES" : "NO")
                                          << "Camera:" << (existingDevice.hasCameraDevice() ? "YES" : "NO") 
                                          << "Audio:" << (existingDevice.hasAudioDevice() ? "YES" : "NO");
            continue;
        }
        
        // Create new device info
        DeviceInfo deviceInfo;
        deviceInfo.portChain = associatedPortChain;
        
        // CRITICAL: Use the COMPOSITE device's instance ID for interface enumeration
        // The composite device (345F:2109) has HID/camera/audio as child interfaces
        // The serial port is a separate sibling device for serial communication
        deviceInfo.deviceInstanceId = integratedDevice.deviceInstanceId;
        
        deviceInfo.vid = OPENTERFACE_VID_V3;
        deviceInfo.pid = OPENTERFACE_PID_V3;
        deviceInfo.lastSeen = QDateTime::currentDateTime();
        deviceInfo.platformSpecific = integratedDevice.deviceInfo;
        
        qCDebug(log_device_discoverer) << "Using V3 COMPOSITE device ID for interface paths:" << deviceInfo.deviceInstanceId;
        
        // For USB 3.0 integrated devices, set up companion PortChain association
        if (associatedPortChain != integratedDevice.portChain) {
            deviceInfo.companionPortChain = integratedDevice.portChain;
            deviceInfo.hasCompanionDevice = true;
            qCDebug(log_device_discoverer) << "V3 USB 3.0 device - Serial PortChain:" << associatedPortChain 
                                          << "Companion PortChain:" << integratedDevice.portChain;
        }
        
        // Set the serial port information if found (separate from composite device)
        if (!associatedSerialPortId.isEmpty()) {
            deviceInfo.serialPortId = associatedSerialPortId;
            qCDebug(log_device_discoverer) << "Associated V3 serial port ID:" << associatedSerialPortId;
        }
        
        // Store siblings and children in platformSpecific
        QVariantList siblingVariants, childrenVariants;
        for (const QVariantMap& sibling : integratedDevice.siblings) {
            siblingVariants.append(sibling);
        }
        for (const QVariantMap& child : integratedDevice.children) {
            childrenVariants.append(child);
        }
        deviceInfo.platformSpecific["siblings"] = siblingVariants;
        deviceInfo.platformSpecific["children"] = childrenVariants;
        
        // Process the integrated device interfaces
        processIntegratedDeviceInterfaces(deviceInfo, integratedDevice);
        
        // Convert device IDs to real paths
        matchDevicePathsToRealPaths(deviceInfo);
        
        // Add to device map
        deviceMap[deviceInfo.portChain] = deviceInfo;
        qCDebug(log_device_discoverer) << "V3 integrated device added with port chain:" << deviceInfo.portChain;
    }
    
    // Convert map to vector
    for (auto it = deviceMap.begin(); it != deviceMap.end(); ++it) {
        devices.append(it.value());
    }
    
    qCDebug(log_device_discoverer) << "=== Generation 3 Discovery Complete - Found" << devices.size() << "devices ===";
    return devices;
}

QVector<QPair<QString, QString>> Generation3Discoverer::getSupportedVidPidPairs() const
{
    return {
        {OPENTERFACE_VID_V2, OPENTERFACE_PID_V2},  // 345F:2132
        {OPENTERFACE_VID_V3, OPENTERFACE_PID_V3}   // 345F:2109
    };
}

bool Generation3Discoverer::supportsVidPid(const QString& vid, const QString& pid) const
{
    auto supportedPairs = getSupportedVidPidPairs();
    for (const auto& pair : supportedPairs) {
        if (pair.first.toUpper() == vid.toUpper() && pair.second.toUpper() == pid.toUpper()) {
            return true;
        }
    }
    return false;
}

QString Generation3Discoverer::findSerialPortByIntegratedDevice(const USBDeviceData& integratedDevice)
{
    qCDebug(log_device_discoverer) << "Finding serial port for integrated device:" << integratedDevice.portChain;
    
    // Search for serial ports with the compatible VID/PID
    QVector<USBDeviceData> serialDevices = findUSBDevicesWithVidPid(
        SERIAL_VID_V2, 
        SERIAL_PID_V2
    );
    
    QString bestMatchDeviceId;
    int bestScore = -1;
    
    for (const USBDeviceData& serialDevice : serialDevices) {
        qCDebug(log_device_discoverer) << "Checking serial device:" << serialDevice.portChain;
        
        // Check various association criteria
        bool isAssociated = isSerialAssociatedWithIntegratedDevice(serialDevice, integratedDevice);
        
        if (isAssociated) {
            // Calculate a score based on how close the devices are
            int score = 0;
            
            if (isDevicesOnSameUSBHub(serialDevice, integratedDevice)) {
                score += 10;
            }
            
            if (areDevicesProximate(serialDevice, integratedDevice)) {
                score += 5;
            }
            
            if (matchesKnownUSB3Pattern(serialDevice, integratedDevice)) {
                score += 8;
            }
            
            // Calculate distance penalty
            int distance = calculatePortChainDistance(serialDevice.portChain, integratedDevice.portChain);
            score -= distance;
            
            qCDebug(log_device_discoverer) << "Serial device score:" << score;
            
            if (score > bestScore) {
                bestScore = score;
                bestMatchDeviceId = serialDevice.deviceInstanceId;
            }
        }
    }
    
    if (!bestMatchDeviceId.isEmpty()) {
        qCDebug(log_device_discoverer) << "Found best matching serial port:" << bestMatchDeviceId << "(score:" << bestScore << ")";
    }
    
    return bestMatchDeviceId;
}

void Generation3Discoverer::processIntegratedDeviceInterfaces(DeviceInfo& deviceInfo, const USBDeviceData& integratedDevice)
{
    qCDebug(log_device_discoverer) << "Processing Gen3 media interfaces for composite device:" << integratedDevice.deviceInstanceId;
    qCDebug(log_device_discoverer) << "  Found" << integratedDevice.children.size() << "children under integrated device";
    
    // Store device IDs for later interface path resolution
    for (const QVariantMap& child : integratedDevice.children) {
        QString childHardwareId = child["hardwareId"].toString().toUpper();
        QString childDeviceId = child["deviceId"].toString();
        QString childClass = child["class"].toString();
        
        qCDebug(log_device_discoverer) << "    Integrated child - Device ID:" << childDeviceId;
        qCDebug(log_device_discoverer) << "      Hardware ID:" << childHardwareId;
        qCDebug(log_device_discoverer) << "      Class:" << childClass;
        
        // Store device IDs (paths will be resolved in matchDevicePathsToRealPaths)
        // MI_00 = Camera, MI_02 = Audio (Gen3), MI_04 = HID
        if (!deviceInfo.hasHidDevice() && (childHardwareId.contains("HID") || childHardwareId.contains("MI_04"))) {
            deviceInfo.hidDeviceId = childDeviceId;
            qCDebug(log_device_discoverer) << "      ✓ Found HID device ID:" << childDeviceId;
        }
        else if (!deviceInfo.hasCameraDevice() && (childHardwareId.contains("MI_00"))) {
            deviceInfo.cameraDeviceId = childDeviceId;
            qCDebug(log_device_discoverer) << "      ✓ Found camera device ID:" << childDeviceId;
        }
        
        // Check for audio device (MI_02 for Gen3, MI_01 for older, or Audio in hardware ID, or MEDIA class)
        if (!deviceInfo.hasAudioDevice() && (childHardwareId.contains("AUDIO") || childHardwareId.contains("MI_02") || 
            childHardwareId.contains("MI_01") || childClass.toUpper().contains("MEDIA"))) {
            deviceInfo.audioDeviceId = childDeviceId;
            qCDebug(log_device_discoverer) << "      ✓ Found audio device ID:" << childDeviceId;
        }
    }
    
    qCDebug(log_device_discoverer) << "  Integrated device interfaces summary:";
    qCDebug(log_device_discoverer) << "    HID ID:" << (deviceInfo.hasHidDevice() ? deviceInfo.hidDeviceId : "Not found");
    qCDebug(log_device_discoverer) << "    Camera ID:" << (deviceInfo.hasCameraDevice() ? deviceInfo.cameraDeviceId : "Not found");
    qCDebug(log_device_discoverer) << "    Audio ID:" << (deviceInfo.hasAudioDevice() ? deviceInfo.audioDeviceId : "Not found");
}

bool Generation3Discoverer::isSerialAssociatedWithIntegratedDevice(const USBDeviceData& serialDevice, const USBDeviceData& integratedDevice)
{
    // Check various criteria for association
    if (isDevicesOnSameUSBHub(serialDevice, integratedDevice)) {
        return true;
    }
    
    if (areDevicesProximate(serialDevice, integratedDevice)) {
        return true;
    }
    
    if (matchesKnownUSB3Pattern(serialDevice, integratedDevice)) {
        return true;
    }
    
    return false;
}

bool Generation3Discoverer::isDevicesOnSameUSBHub(const USBDeviceData& serialDevice, const USBDeviceData& integratedDevice)
{
    // Extract hub portion of port chains and compare
    QStringList serialPorts = serialDevice.portChain.split("-");
    QStringList integratedPorts = integratedDevice.portChain.split("-");
    
    // If they have the same hub prefix (first part), they're on the same hub
    if (!serialPorts.isEmpty() && !integratedPorts.isEmpty()) {
        return serialPorts.first() == integratedPorts.first();
    }
    
    return false;
}

bool Generation3Discoverer::areDevicesProximate(const USBDeviceData& serialDevice, const USBDeviceData& integratedDevice)
{
    int distance = calculatePortChainDistance(serialDevice.portChain, integratedDevice.portChain);
    return distance <= 2; // Devices are close if distance is 2 or less
}

bool Generation3Discoverer::matchesKnownUSB3Pattern(const USBDeviceData& serialDevice, const USBDeviceData& integratedDevice)
{
    // Check for known USB 3.0 patterns where serial and integrated devices
    // are related through specific port arrangements
    
    QStringList serialPorts = serialDevice.portChain.split("-");
    QStringList integratedPorts = integratedDevice.portChain.split("-");
    
    if (serialPorts.size() >= 2 && integratedPorts.size() >= 2) {
        // Pattern: Serial on X-Y, Integrated on X-Z where Z = Y+1 or Y-1
        if (serialPorts[0] == integratedPorts[0] && serialPorts.size() == integratedPorts.size()) {
            bool lastPortNumeric1, lastPortNumeric2;
            int serialLastPort = serialPorts.last().toInt(&lastPortNumeric1);
            int integratedLastPort = integratedPorts.last().toInt(&lastPortNumeric2);
            
            if (lastPortNumeric1 && lastPortNumeric2) {
                int difference = qAbs(serialLastPort - integratedLastPort);
                return difference == 1; // Adjacent ports
            }
        }
    }
    
    return false;
}

int Generation3Discoverer::calculatePortChainDistance(const QString& portChain1, const QString& portChain2)
{
    QStringList ports1 = portChain1.split("-");
    QStringList ports2 = portChain2.split("-");
    
    if (ports1.isEmpty() || ports2.isEmpty()) {
        return 100; // Very high distance for invalid port chains
    }
    
    // Calculate distance based on common prefix and difference in last parts
    int commonPrefixLength = 0;
    int maxLength = qMin(ports1.size(), ports2.size());
    
    for (int i = 0; i < maxLength; ++i) {
        if (ports1[i] == ports2[i]) {
            commonPrefixLength++;
        } else {
            break;
        }
    }
    
    // Distance is based on how different the port chains are
    int lengthDifference = qAbs(ports1.size() - ports2.size());
    int prefixPenalty = maxLength - commonPrefixLength;
    
    return lengthDifference + prefixPenalty;
}

#endif // _WIN32