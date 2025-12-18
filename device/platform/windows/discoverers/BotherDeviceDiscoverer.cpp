#ifdef _WIN32
#include "BotherDeviceDiscoverer.h"
#include <QDebug>

Q_DECLARE_LOGGING_CATEGORY(log_device_discoverer)

BotherDeviceDiscoverer::BotherDeviceDiscoverer(std::shared_ptr<IDeviceEnumerator> enumerator, QObject* parent)
    : BaseDeviceDiscoverer(enumerator, parent)
{
    qCDebug(log_device_discoverer) << "BotherDeviceDiscoverer initialized";
}

QVector<DeviceInfo> BotherDeviceDiscoverer::discoverDevices()
{
    QVector<DeviceInfo> devices;
    
    qCDebug(log_device_discoverer) << "=== Bother Device Discovery Started ===";
    qCDebug(log_device_discoverer) << "Looking for Gen1 and Gen2 devices with same USB topology";
    
    // Phase 1: Find Generation 1 integrated devices (VID_534D&PID_2109)
    qCDebug(log_device_discoverer) << "Phase 1: Searching for Gen1 integrated devices (534D:2109)";
    QVector<USBDeviceData> integratedDevices = findUSBDevicesWithVidPid(
        OPENTERFACE_VID, 
        OPENTERFACE_PID
    );
    qCDebug(log_device_discoverer) << "Found" << integratedDevices.size() << "Gen1 integrated devices";
    
    for (int i = 0; i < integratedDevices.size(); ++i) {
        const USBDeviceData& integratedDevice = integratedDevices[i];
        
        qCDebug(log_device_discoverer) << "Processing Gen1 Integrated Device" << (i + 1) << "at port chain:" << integratedDevice.portChain;
        
        DeviceInfo deviceInfo;
        deviceInfo.portChain = integratedDevice.portChain;
        deviceInfo.deviceInstanceId = integratedDevice.deviceInstanceId;
        deviceInfo.vid = OPENTERFACE_VID;
        deviceInfo.pid = OPENTERFACE_PID;
        deviceInfo.lastSeen = QDateTime::currentDateTime();
        deviceInfo.platformSpecific = integratedDevice.deviceInfo;
        deviceInfo.platformSpecific["generation"] = "Generation 1";
        
        // Store siblings and children in platformSpecific for debugging
        QVariantList siblingVariants, childrenVariants;
        for (const QVariantMap& sibling : integratedDevice.siblings) {
            siblingVariants.append(sibling);
        }
        for (const QVariantMap& child : integratedDevice.children) {
            childrenVariants.append(child);
        }
        deviceInfo.platformSpecific["siblings"] = siblingVariants;
        deviceInfo.platformSpecific["children"] = childrenVariants;
        
        // Find serial port from siblings (Gen1 approach)
        findSerialPortFromSiblings(deviceInfo, integratedDevice);
        
        // Process integrated device interfaces (camera, HID, audio from children)
        processGeneration1Interfaces(deviceInfo, integratedDevice);
        
        // Convert device IDs to real paths
        matchDevicePathsToRealPaths(deviceInfo);
        
        devices.append(deviceInfo);
        qCDebug(log_device_discoverer) << "Gen1 device processing complete";
        qCDebug(log_device_discoverer) << "  Serial:" << (deviceInfo.hasSerialPort() ? deviceInfo.serialPortPath : "None");
        qCDebug(log_device_discoverer) << "  HID:" << (deviceInfo.hasHidDevice() ? "Found" : "None");
        qCDebug(log_device_discoverer) << "  Camera:" << (deviceInfo.hasCameraDevice() ? "Found" : "None");
        qCDebug(log_device_discoverer) << "  Audio:" << (deviceInfo.hasAudioDevice() ? "Found" : "None");
    }
    
    // Phase 2: Find Generation 2 serial devices (VID_1A86&PID_FE0C) on USB 2.0
    qCDebug(log_device_discoverer) << "Phase 2: Searching for Gen2 serial devices (1A86:FE0C)";
    QVector<USBDeviceData> gen2SerialDevices = findUSBDevicesWithVidPid(
        SERIAL_VID_V2, 
        SERIAL_PID_V2
    );
    qCDebug(log_device_discoverer) << "Found" << gen2SerialDevices.size() << "Gen2 serial devices";
    
    for (int i = 0; i < gen2SerialDevices.size(); ++i) {
        const USBDeviceData& gen2SerialDevice = gen2SerialDevices[i];
        
        qCDebug(log_device_discoverer) << "Processing Gen2 Serial Device" << (i + 1) << "at port chain:" << gen2SerialDevice.portChain;
        
        DeviceInfo deviceInfo;
        deviceInfo.portChain = gen2SerialDevice.portChain;
        deviceInfo.deviceInstanceId = gen2SerialDevice.deviceInstanceId;
        deviceInfo.vid = SERIAL_VID_V2;
        deviceInfo.pid = SERIAL_PID_V2;
        deviceInfo.lastSeen = QDateTime::currentDateTime();
        deviceInfo.platformSpecific = gen2SerialDevice.deviceInfo;
        deviceInfo.platformSpecific["generation"] = "Generation 2";
        
        // Store siblings and children in platformSpecific for debugging
        QVariantList siblingVariants, childrenVariants;
        for (const QVariantMap& sibling : gen2SerialDevice.siblings) {
            siblingVariants.append(sibling);
        }
        for (const QVariantMap& child : gen2SerialDevice.children) {
            childrenVariants.append(child);
        }
        deviceInfo.platformSpecific["siblings"] = siblingVariants;
        deviceInfo.platformSpecific["children"] = childrenVariants;
        
        // Process as Generation 2 device (serial-first approach)
        processGeneration2AsGeneration1(deviceInfo, gen2SerialDevice);
        
        // Convert device IDs to real paths
        matchDevicePathsToRealPaths(deviceInfo);
        
        devices.append(deviceInfo);
        qCDebug(log_device_discoverer) << "Gen2 device processing complete";
        qCDebug(log_device_discoverer) << "  Serial:" << (deviceInfo.hasSerialPort() ? deviceInfo.serialPortPath : "None");
        qCDebug(log_device_discoverer) << "  HID:" << (deviceInfo.hasHidDevice() ? "Found" : "None");
        qCDebug(log_device_discoverer) << "  Camera:" << (deviceInfo.hasCameraDevice() ? "Found" : "None");
        qCDebug(log_device_discoverer) << "  Audio:" << (deviceInfo.hasAudioDevice() ? "Found" : "None");
    }
    
    qCDebug(log_device_discoverer) << "=== Bother Device Discovery Complete - Found" << devices.size() << "devices ===";
    return devices;
}

QVector<QPair<QString, QString>> BotherDeviceDiscoverer::getSupportedVidPidPairs() const
{
    return {
        {OPENTERFACE_VID, OPENTERFACE_PID},  // 534D:2109 (Gen1 integrated)
        {SERIAL_VID, SERIAL_PID},            // 1A86:7523 (Gen1 serial)
        {SERIAL_VID_V2, SERIAL_PID_V2}       // 1A86:FE0C (Gen2 serial)
    };
}

bool BotherDeviceDiscoverer::supportsVidPid(const QString& vid, const QString& pid) const
{
    auto supportedPairs = getSupportedVidPidPairs();
    for (const auto& pair : supportedPairs) {
        if (pair.first.toUpper() == vid.toUpper() && pair.second.toUpper() == pid.toUpper()) {
            return true;
        }
    }
    return false;
}

void BotherDeviceDiscoverer::processGeneration1Interfaces(DeviceInfo& deviceInfo, const USBDeviceData& integratedDevice)
{
    qCDebug(log_device_discoverer) << "Processing Gen1 interfaces for integrated device:" << deviceInfo.portChain;
    
    // Process children of integrated device to find HID, camera, and audio interfaces
    processGeneration1MediaInterfaces(deviceInfo, integratedDevice);
}

void BotherDeviceDiscoverer::processGeneration1MediaInterfaces(DeviceInfo& deviceInfo, const USBDeviceData& deviceData)
{
    qCDebug(log_device_discoverer) << "Processing Gen1 media interfaces for composite device:" << deviceData.deviceInstanceId;
    qCDebug(log_device_discoverer) << "  Found" << deviceData.children.size() << "children under integrated device";
    
    // Store device IDs for later interface path resolution
    for (const QVariantMap& child : deviceData.children) {
        QString childHardwareId = child["hardwareId"].toString().toUpper();
        QString childDeviceId = child["deviceId"].toString();
        QString childClass = child["class"].toString();
        
        qCDebug(log_device_discoverer) << "    Integrated child - Device ID:" << childDeviceId;
        qCDebug(log_device_discoverer) << "      Hardware ID:" << childHardwareId;
        qCDebug(log_device_discoverer) << "      Class:" << childClass;
        
        // Skip interface endpoints we don't need
        if (childDeviceId.contains("&0002") || childDeviceId.contains("&0004")) {
            qCDebug(log_device_discoverer) << "      Skipping interface endpoint" << childDeviceId;
            continue;
        }
        
        // Store device IDs (paths will be resolved in matchDevicePathsToRealPaths)
        if (!deviceInfo.hasHidDevice() && (childHardwareId.contains("HID") || childHardwareId.contains("MI_04"))) {
            deviceInfo.hidDeviceId = childDeviceId;
            qCDebug(log_device_discoverer) << "      ✓ Found HID device ID:" << childDeviceId;
        }
        else if (!deviceInfo.hasCameraDevice() && (childHardwareId.contains("MI_00"))) {
            deviceInfo.cameraDeviceId = childDeviceId;
            qCDebug(log_device_discoverer) << "      ✓ Found camera device ID:" << childDeviceId;
        }
        
        // Check for audio device (MI_01 or Audio in hardware ID)
        if (!deviceInfo.hasAudioDevice() && (childHardwareId.contains("AUDIO") || childHardwareId.contains("MI_01"))) {
            deviceInfo.audioDeviceId = childDeviceId;
            qCDebug(log_device_discoverer) << "      ✓ Found audio device ID:" << childDeviceId;
        }
    }
    
    qCDebug(log_device_discoverer) << "  Integrated device interfaces summary:";
    qCDebug(log_device_discoverer) << "    HID ID:" << (deviceInfo.hasHidDevice() ? deviceInfo.hidDeviceId : "Not found");
    qCDebug(log_device_discoverer) << "    Camera ID:" << (deviceInfo.hasCameraDevice() ? deviceInfo.cameraDeviceId : "Not found");
    qCDebug(log_device_discoverer) << "    Audio ID:" << (deviceInfo.hasAudioDevice() ? deviceInfo.audioDeviceId : "Not found");
}

void BotherDeviceDiscoverer::findSerialPortFromSiblings(DeviceInfo& deviceInfo, const USBDeviceData& integratedDevice)
{
    qCDebug(log_device_discoverer) << "Searching for serial port in" << integratedDevice.siblings.size() << "siblings...";
    
    for (const QVariantMap& sibling : integratedDevice.siblings) {
        QString siblingHardwareId = sibling["hardwareId"].toString();
        QString siblingDeviceId = sibling["deviceId"].toString();
        
        qCDebug(log_device_discoverer) << "  Checking sibling - Hardware ID:" << siblingHardwareId;
        
        // Check if this sibling is a serial port with our target VID/PID (1A86:7523)
        if (siblingHardwareId.toUpper().contains(SERIAL_VID.toUpper()) &&
            siblingHardwareId.toUpper().contains(SERIAL_PID.toUpper())) {
            
            qCDebug(log_device_discoverer) << "  ✓ Found serial port sibling:" << siblingDeviceId;
            
            deviceInfo.serialPortId = siblingDeviceId;
            // Set port chain path as per original logic - use the integrated device's port chain
            deviceInfo.serialPortPath = integratedDevice.portChain;  
            
            qCDebug(log_device_discoverer) << "    Serial device ID:" << siblingDeviceId;
            qCDebug(log_device_discoverer) << "    Device location:" << integratedDevice.portChain;
            break;
        }
    }
    
    if (deviceInfo.serialPortId.isEmpty()) {
        qCDebug(log_device_discoverer) << "  ⚠ No serial port sibling found with VID/PID" << SERIAL_VID << "/" << SERIAL_PID;
    }
}

void BotherDeviceDiscoverer::processGeneration2AsGeneration1(DeviceInfo& deviceInfo, const USBDeviceData& gen2Device)
{
    qCDebug(log_device_discoverer) << "Processing Gen2 device using serial-first approach (USB 2.0 compatibility)";
    
    // Set serial port information
    deviceInfo.serialPortId = gen2Device.deviceInstanceId;
    
    // Find integrated device from siblings (similar to Gen1 but reversed)
    findIntegratedDeviceFromSiblings(deviceInfo, gen2Device);
}

void BotherDeviceDiscoverer::findIntegratedDeviceFromSiblings(DeviceInfo& deviceInfo, const USBDeviceData& serialDevice)
{
    qCDebug(log_device_discoverer) << "Searching for integrated device in" << serialDevice.siblings.size() << "siblings...";
    
    for (const QVariantMap& sibling : serialDevice.siblings) {
        QString siblingHardwareId = sibling["hardwareId"].toString();
        QString siblingDeviceId = sibling["deviceId"].toString();
        
        qCDebug(log_device_discoverer) << "  Checking sibling - Hardware ID:" << siblingHardwareId;
        
        // Check if this sibling is the integrated device (newer versions: 345F:2109 or 345F:2132)
        bool isIntegratedDevice = 
            (siblingHardwareId.toUpper().contains("345F") && 
             (siblingHardwareId.toUpper().contains("2109") || siblingHardwareId.toUpper().contains("2132")));
        
        if (isIntegratedDevice) {
            qCDebug(log_device_discoverer) << "  ✓ Found integrated device sibling:" << siblingDeviceId;
            
            // Get the device instance for this sibling to enumerate its children
            DWORD siblingDevInst = getDeviceInstanceFromId(siblingDeviceId);
            if (siblingDevInst != 0) {
                // Get all children of this integrated device
                QVector<QVariantMap> integratedChildren = getAllChildDevices(siblingDevInst);
                qCDebug(log_device_discoverer) << "  Found" << integratedChildren.size() << "children under integrated device";
                
                // Search through integrated device's children for camera, HID, and audio
                for (const QVariantMap& integratedChild : integratedChildren) {
                    QString childHardwareId = integratedChild["hardwareId"].toString();
                    QString childDeviceId = integratedChild["deviceId"].toString();
                    
                    qCDebug(log_device_discoverer) << "    Integrated child - Device ID:" << childDeviceId;
                    qCDebug(log_device_discoverer) << "      Hardware ID:" << childHardwareId;
                    
                    // Skip interface endpoints we don't need
                    if (childDeviceId.contains("&0002") || childDeviceId.contains("&0004")) {
                        continue;
                    }
                    
                    // Check for HID device (MI_04 interface)
                    if (!deviceInfo.hasHidDevice() && 
                        ((childHardwareId.toUpper().contains("HID") && childDeviceId.toUpper().contains("MI_04")) ||
                         (childDeviceId.toUpper().contains("MI_04")))) {
                        deviceInfo.hidDeviceId = childDeviceId;
                        qCDebug(log_device_discoverer) << "      ✓ Found HID device:" << childDeviceId;
                    }
                    // Check for camera device (MI_00 interface)
                    else if (!deviceInfo.hasCameraDevice() && 
                             (childHardwareId.toUpper().contains("MI_00") || 
                              childDeviceId.toUpper().contains("MI_00"))) {
                        deviceInfo.cameraDeviceId = childDeviceId;
                        qCDebug(log_device_discoverer) << "      ✓ Found camera device:" << childDeviceId;
                    }
                    // Check for audio device (MI_01 or Audio in hardware ID)
                    else if (!deviceInfo.hasAudioDevice() && 
                             (childHardwareId.toUpper().contains("AUDIO") ||
                              childHardwareId.toUpper().contains("MI_01") ||
                              childDeviceId.toUpper().contains("MI_01"))) {
                        deviceInfo.audioDeviceId = childDeviceId;
                        qCDebug(log_device_discoverer) << "      ✓ Found audio device:" << childDeviceId;
                    }
                }
                
                qCDebug(log_device_discoverer) << "  Integrated device interfaces summary:";
                qCDebug(log_device_discoverer) << "    HID:" << (deviceInfo.hasHidDevice() ? deviceInfo.hidDeviceId : "Not found");
                qCDebug(log_device_discoverer) << "    Camera:" << (deviceInfo.hasCameraDevice() ? deviceInfo.cameraDeviceId : "Not found");
                qCDebug(log_device_discoverer) << "    Audio:" << (deviceInfo.hasAudioDevice() ? deviceInfo.audioDeviceId : "Not found");
            } else {
                qCWarning(log_device_discoverer) << "  ⚠ Could not get device instance for integrated device sibling";
            }
            
            break; // Found the integrated device, no need to check other siblings
        }
    }
}

#endif // _WIN32
