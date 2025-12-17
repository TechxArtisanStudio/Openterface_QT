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
        deviceInfo.deviceInstanceId = integratedDevice.deviceInstanceId;
        deviceInfo.vid = OPENTERFACE_VID_V2;
        deviceInfo.pid = OPENTERFACE_PID_V2;
        deviceInfo.lastSeen = QDateTime::currentDateTime();
        deviceInfo.platformSpecific = integratedDevice.deviceInfo;
        
        // For USB 3.0 integrated devices, set up companion PortChain association
        if (associatedPortChain != integratedDevice.portChain) {
            deviceInfo.companionPortChain = integratedDevice.portChain;
            deviceInfo.hasCompanionDevice = true;
            qCDebug(log_device_discoverer) << "USB 3.0 device - Serial PortChain:" << associatedPortChain 
                                          << "Companion PortChain:" << integratedDevice.portChain;
        }
        
        // Set the serial port information if found
        if (!associatedSerialPortId.isEmpty()) {
            deviceInfo.serialPortId = associatedSerialPortId;
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
        deviceInfo.deviceInstanceId = integratedDevice.deviceInstanceId;
        deviceInfo.vid = OPENTERFACE_VID_V3;
        deviceInfo.pid = OPENTERFACE_PID_V3;
        deviceInfo.lastSeen = QDateTime::currentDateTime();
        deviceInfo.platformSpecific = integratedDevice.deviceInfo;
        
        // For USB 3.0 integrated devices, set up companion PortChain association
        if (associatedPortChain != integratedDevice.portChain) {
            deviceInfo.companionPortChain = integratedDevice.portChain;
            deviceInfo.hasCompanionDevice = true;
            qCDebug(log_device_discoverer) << "V3 USB 3.0 device - Serial PortChain:" << associatedPortChain 
                                          << "Companion PortChain:" << integratedDevice.portChain;
        }
        
        // Set the serial port information if found
        if (!associatedSerialPortId.isEmpty()) {
            deviceInfo.serialPortId = associatedSerialPortId;
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
    qCDebug(log_device_discoverer) << "Processing interfaces for integrated device:" << integratedDevice.portChain;
    qCDebug(log_device_discoverer) << "Integrated device has" << integratedDevice.children.size() << "children";
    
    for (const QVariantMap& child : integratedDevice.children) {
        QString hardwareId = child["hardwareId"].toString();
        QString deviceId = child["deviceId"].toString();
        QString deviceClass = child["class"].toString();
        
        qCDebug(log_device_discoverer) << "  Child - Device ID:" << deviceId;
        qCDebug(log_device_discoverer) << "    Hardware ID:" << hardwareId;
        qCDebug(log_device_discoverer) << "    Class:" << deviceClass;
        
        // Skip interface endpoints we don't need
        if (deviceId.contains("&0002") || deviceId.contains("&0004")) {
            qCDebug(log_device_discoverer) << "    Skipping interface endpoint";
            continue;
        }
        
        // Check for HID device (MI_04 interface)
        if (hardwareId.toUpper().contains("HID") && deviceId.toUpper().contains("MI_04")) {
            deviceInfo.hidDeviceId = deviceId;
            qCDebug(log_device_discoverer) << "    Found HID device:" << deviceId;
        }
        // Check for camera device (MI_00 interface)
        else if (hardwareId.toUpper().contains("MI_00") || deviceId.toUpper().contains("MI_00")) {
            deviceInfo.cameraDeviceId = deviceId;
            qCDebug(log_device_discoverer) << "    Found camera device:" << deviceId;
        }
        // Check for audio device (MI_01 or Audio in hardware ID)
        else if (hardwareId.toUpper().contains("AUDIO") || 
                 hardwareId.toUpper().contains("MI_01") || 
                 deviceId.toUpper().contains("MI_01")) {
            deviceInfo.audioDeviceId = deviceId;
            qCDebug(log_device_discoverer) << "    Found audio device:" << deviceId;
        }
    }
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