#ifdef _WIN32
#include "WindowsDeviceManager.h"
#include <QDebug>
#include <QSettings>
#include <QDir>
#include <QRegularExpression>
#include <QSerialPortInfo>
#include <vector>
#include <initguid.h>
#include <devguid.h>
#include <cfgmgr32.h>
#include <objbase.h>
#include <hidclass.h>

extern "C"
{
#include <hidsdi.h>
}

// USB Device Interface GUID definition
DEFINE_GUID(GUID_DEVINTERFACE_USB_DEVICE, 
    0xA5DCBF10, 0x6530, 0x11D2, 0x90, 0x1F, 0x00, 0xC0, 0x4F, 0xB9, 0x51, 0xED);

// Camera interface GUID - DirectShow video capture devices
DEFINE_GUID(GUID_DEVINTERFACE_CAMERA, 
    0x65E8773D, 0x8F56, 0x11D0, 0xA3, 0xB9, 0x00, 0xA0, 0xC9, 0x22, 0x31, 0x96);

// UVC (USB Video Class) interface GUID
DEFINE_GUID(GUID_DEVINTERFACE_UVC, 
    0xA5DCBF10, 0x6530, 0x11D2, 0x90, 0x1F, 0x00, 0xC0, 0x4F, 0xB9, 0x51, 0xED);

// Function declaration for CM_Get_Sibling (if not available in headers)
extern "C" CONFIGRET WINAPI CM_Get_Sibling(
    PDEVINST pdnDevInstSibling,
    DEVINST dnDevInst,
    ULONG ulFlags
);

Q_LOGGING_CATEGORY(log_device_windows, "opf.device.windows")

WindowsDeviceManager::WindowsDeviceManager(QObject *parent)
    : AbstractPlatformDeviceManager(parent)
{
    qCDebug(log_device_windows) << "Windows Device Manager initialized";
}

WindowsDeviceManager::~WindowsDeviceManager()
{
    cleanup();
}

QList<DeviceInfo> WindowsDeviceManager::discoverDevices()
{
    // Check cache first
    QDateTime now = QDateTime::currentDateTime();
    if (m_lastCacheUpdate.isValid() && 
        m_lastCacheUpdate.msecsTo(now) < CACHE_TIMEOUT_MS) {
        qCDebug(log_device_windows) << "Returning cached devices:" << m_cachedDevices.size();
        return m_cachedDevices;
    }

    qCDebug(log_device_windows) << "Discovering Openterface devices using Python-compatible logic...";
    
    QList<DeviceInfo> devices;
    
    try {
        // Use Python-compatible approach: Find USB devices with HID VID/PID first
        QList<USBDeviceData> usbDevices = findUSBDevicesWithVidPid(AbstractPlatformDeviceManager::HID_VID, AbstractPlatformDeviceManager::HID_PID);
        qCDebug(log_device_windows) << "Found" << usbDevices.size() << "USB devices with VID/PID" << AbstractPlatformDeviceManager::HID_VID << "/" << AbstractPlatformDeviceManager::HID_PID;
        
        // Process each USB device found
        for (int i = 0; i < usbDevices.size(); ++i) {
            const USBDeviceData& usbDevice = usbDevices[i];
            
            qCDebug(log_device_windows) << "=== Processing Device" << (i + 1) << "===";
            qCDebug(log_device_windows) << "Port Chain:" << usbDevice.portChain;
            qCDebug(log_device_windows) << "Device Instance ID:" << usbDevice.deviceInstanceId;
            
            DeviceInfo deviceInfo;
            deviceInfo.portChain = usbDevice.portChain;
            deviceInfo.deviceInstanceId = usbDevice.deviceInstanceId;
            deviceInfo.lastSeen = QDateTime::currentDateTime();
            deviceInfo.platformSpecific = usbDevice.deviceInfo;
            
            // Process siblings to find serial port devices (Python-compatible logic)
            qCDebug(log_device_windows) << "Processing" << usbDevice.siblings.size() << "sibling devices...";
            for (const QVariantMap& sibling : usbDevice.siblings) {
                QString hardwareId = sibling["hardwareId"].toString();
                QString deviceId = sibling["deviceId"].toString();
                
                qCDebug(log_device_windows) << "  Sibling Hardware ID:" << hardwareId;
                qCDebug(log_device_windows) << "  Sibling Device ID:" << deviceId;
                
                // Check if this sibling is a serial port with our target VID/PID (1A86:7523)
                // This matches the Python logic: Serial_vid.upper() in sibling['hardware_id'] and Serial_pid.upper() in sibling['hardware_id']
                if (hardwareId.toUpper().contains(AbstractPlatformDeviceManager::SERIAL_VID.toUpper()) &&
                    hardwareId.toUpper().contains(AbstractPlatformDeviceManager::SERIAL_PID.toUpper())) {
                    deviceInfo.serialPortId = deviceId;
                    deviceInfo.serialPortPath = usbDevice.portChain;  // Set port chain path as per Python logic
                    qCDebug(log_device_windows) << "  ✓ Found serial port device:" << deviceId;
                    qCDebug(log_device_windows) << "    Device location:" << usbDevice.portChain;
                    break;
                }
            }
            
            // Process children to find HID, camera, and audio devices (Python-compatible logic)
            qCDebug(log_device_windows) << "Processing" << usbDevice.children.size() << "child devices...";
            for (const QVariantMap& child : usbDevice.children) {
                QString hardwareId = child["hardwareId"].toString();
                QString deviceId = child["deviceId"].toString();
                
                qCDebug(log_device_windows) << "  Child Hardware ID:" << hardwareId;
                qCDebug(log_device_windows) << "  Child Device ID:" << deviceId;
                
                // Skip interface endpoints we don't need (Python logic: skip "&0002" and "&0004")
                if (deviceId.contains("&0002") || deviceId.contains("&0004")) {
                    qCDebug(log_device_windows) << "    Skipping interface endpoint (Python compatibility)";
                    continue;
                }
                
                // Check for HID device
                if (hardwareId.toUpper().contains("HID") && deviceId.toUpper().contains("MI_04")) {
                    deviceInfo.hidDeviceId = deviceId;
                    qCDebug(log_device_windows) << "Found HID device ID:" << deviceId << "with hardware ID:" << hardwareId;
                }
                // Check for camera device (MI_00 interface)
                else if (hardwareId.toUpper().contains("MI_00")) {
                    deviceInfo.cameraDeviceId = deviceId;
                    qCDebug(log_device_windows) << "Found camera device ID:" << deviceId;
                }
                // Check for audio device (Audio in hardware ID)
                else if (hardwareId.toUpper().contains("AUDIO")) {
                    deviceInfo.audioDeviceId = deviceId;
                    qCDebug(log_device_windows) << "Found audio device ID:" << deviceId;
                }
            }
            
            // Match device paths to real system paths (COM ports, HID paths, etc.) using Python-compatible logic
            // For serial ports, find COM port by port chain location (Python-compatible approach)
            if (!deviceInfo.serialPortId.isEmpty()) {
                QString comPortPath = findComPortByPortChain(deviceInfo.portChain);
                if (!comPortPath.isEmpty()) {
                    deviceInfo.serialPortPath = comPortPath;
                    qCDebug(log_device_windows) << "  ✓ Matched serial port path:" << comPortPath;
                } else {
                    qCDebug(log_device_windows) << "  ✗ Could not find COM port for port chain:" << deviceInfo.portChain;
                }
            }
            
            matchDevicePaths(deviceInfo);
            
            // Convert device IDs to actual device paths
            matchDevicePathsToRealPaths(deviceInfo);
            
            devices.append(deviceInfo);
            qCDebug(log_device_windows) << "Device" << (i + 1) << "processing complete";
        }
        
    } catch (const std::exception& e) {
        qCWarning(log_device_windows) << "Error discovering devices:" << e.what();
    }
    
    // Update cache
    m_cachedDevices = devices;
    m_lastCacheUpdate = now;
    
    qCDebug(log_device_windows) << "=== DEVICE DISCOVERY SUMMARY ===";
    qCDebug(log_device_windows) << "Total Openterface devices found:" << devices.size();
    for (int i = 0; i < devices.size(); ++i) {
        const DeviceInfo& device = devices[i];
        qCDebug(log_device_windows) << "Device[" << i << "] Summary:";
        qCDebug(log_device_windows) << "  Port Chain:" << device.portChain;
        qCDebug(log_device_windows) << "  Instance ID:" << device.deviceInstanceId;
        qCDebug(log_device_windows) << "  Interfaces:" << device.getInterfaceSummary();
        qCDebug(log_device_windows) << "  Serial:" << (device.hasSerialPort() ? device.serialPortPath : "None");
        qCDebug(log_device_windows) << "  HID:" << (device.hasHidDevice() ? "Available" : "None");
        qCDebug(log_device_windows) << "  Camera:" << (device.hasCameraDevice() ? "Available" : "None");
        qCDebug(log_device_windows) << "  Audio:" << (device.hasAudioDevice() ? "Available" : "None");
        qCDebug(log_device_windows) << "  Complete Device:" << (device.isCompleteDevice() ? "Yes" : "No");
    }
    qCDebug(log_device_windows) << "=== END DISCOVERY SUMMARY ===";
    
    return devices;
}

QList<WindowsDeviceManager::USBDeviceData> WindowsDeviceManager::findUSBDevicesWithVidPid(const QString& vid, const QString& pid)
{
    QList<USBDeviceData> devices;
    
    qCDebug(log_device_windows) << "=== Finding USB devices with VID:" << vid << "PID:" << pid << "(Python-compatible approach) ===";
    
    QString targetHwid = QString("VID_%1&PID_%2").arg(vid.toUpper()).arg(pid.toUpper());
    qCDebug(log_device_windows) << "Target Hardware ID pattern:" << targetHwid;
    
    // Use the USB device interface GUID to enumerate USB devices
    HDEVINFO hDevInfo = SetupDiGetClassDevs(
        &GUID_DEVINTERFACE_USB_DEVICE, 
        nullptr, 
        nullptr,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE
    );
    
    if (hDevInfo == INVALID_HANDLE_VALUE) {
        qCWarning(log_device_windows) << "Failed to get USB device interface list";
        return devices;
    }
    
    SP_DEVINFO_DATA devInfoData;
    devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
    
    for (DWORD index = 0; SetupDiEnumDeviceInfo(hDevInfo, index, &devInfoData); index++) {
        QString hardwareId = getHardwareId(hDevInfo, &devInfoData);
        
        // Check if this device matches our target VID/PID
        if (hardwareId.toUpper().contains(targetHwid)) {
            qCDebug(log_device_windows) << "Found matching USB device:" << hardwareId;
            
            USBDeviceData usbData;
            usbData.deviceInstanceId = getDeviceId(devInfoData.DevInst);
            usbData.deviceInfo = getDeviceInfo(devInfoData.DevInst);
            
            // Get additional device properties
            usbData.deviceInfo["friendlyName"] = getDeviceProperty(hDevInfo, &devInfoData, SPDRP_FRIENDLYNAME);
            usbData.deviceInfo["hardwareId"] = hardwareId;
            
            qCDebug(log_device_windows) << "Device Instance ID:" << usbData.deviceInstanceId;
            qCDebug(log_device_windows) << "Friendly Name:" << usbData.deviceInfo["friendlyName"].toString();
            
            // Build port chain (Python-compatible format)
            usbData.portChain = buildPythonCompatiblePortChain(devInfoData.DevInst);
            qCDebug(log_device_windows) << "Port Chain:" << usbData.portChain;
            
            // Get parent device for sibling enumeration
            DWORD parentDevInst;
            if (CM_Get_Parent(&parentDevInst, devInfoData.DevInst, 0) == CR_SUCCESS) {
                usbData.siblings = getSiblingDevicesByParent(parentDevInst);
                qCDebug(log_device_windows) << "Found" << usbData.siblings.size() << "sibling devices";
            }
            
            // Get child devices
            usbData.children = getChildDevicesPython(devInfoData.DevInst);
            qCDebug(log_device_windows) << "Found" << usbData.children.size() << "child devices";
            
            devices.append(usbData);
        }
    }
    
    SetupDiDestroyDeviceInfoList(hDevInfo);
    
    qCDebug(log_device_windows) << "Found" << devices.size() << "USB devices with VID/PID" << vid << "/" << pid;
    return devices;
}

QString WindowsDeviceManager::buildPythonCompatiblePortChain(DWORD devInst)
{
    // Build port chain similar to Python get_port_chain function
    QStringList portChain;
    DWORD currentInst = devInst;
    int depth = 0;
    
    while (currentInst && depth < 3) { // Limit to 3 levels like Python
        QString deviceId = getDeviceId(currentInst);
        portChain.append(deviceId);
        
        DWORD parentInst;
        if (CM_Get_Parent(&parentInst, currentInst, 0) != CR_SUCCESS) {
            break;
        }
        currentInst = parentInst;
        depth++;
    }
    
    // Reverse to match Python order
    std::reverse(portChain.begin(), portChain.end());
    
    // Build Python-compatible port chain string
    QString result;
    QString tmp;
    
    for (int j = 0; j < portChain.size(); j++) {
        QString devId = portChain[j];
        if (j == 0) {
            // Extract last digit and add 1, then add "-"
            if (!devId.isEmpty()) {
                QChar lastChar = devId[devId.length() - 1];
                if (lastChar.isDigit()) {
                    tmp = QString::number(lastChar.digitValue() + 1) + "-";
                }
            }
        } else if (j == 1) {
            // Use last character and combine with tmp
            if (!devId.isEmpty()) {
                result = tmp + devId[devId.length() - 1];
            }
        } else if (j > 1 && j < portChain.size() - 1) {
            // Add dash and last character for middle elements
            if (!devId.isEmpty()) {
                result += "-" + QString(devId[devId.length() - 1]);
            }
        } else if (j == portChain.size() - 1) {
            // Add ".2" at the end like Python
            result += ".2";
        }
    }
    
    return result;
}

QList<QVariantMap> WindowsDeviceManager::getSiblingDevicesByParent(DWORD parentDevInst)
{
    QList<QVariantMap> siblings;
    
    // Enumerate all devices to find siblings with the same parent
    HDEVINFO hDevInfo = SetupDiGetClassDevs(nullptr, nullptr, nullptr, DIGCF_PRESENT | DIGCF_ALLCLASSES);
    if (hDevInfo == INVALID_HANDLE_VALUE) {
        return siblings;
    }
    
    SP_DEVINFO_DATA devInfoData;
    devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
    
    for (DWORD index = 0; SetupDiEnumDeviceInfo(hDevInfo, index, &devInfoData); index++) {
        DWORD currentParent;
        if (CM_Get_Parent(&currentParent, devInfoData.DevInst, 0) == CR_SUCCESS) {
            if (currentParent == parentDevInst) {
                QVariantMap sibling;
                sibling["hardware_id"] = getHardwareId(hDevInfo, &devInfoData);
                sibling["device_id"] = getDeviceId(devInfoData.DevInst);
                sibling["hardwareId"] = sibling["hardware_id"]; // Duplicate key for compatibility
                sibling["deviceId"] = sibling["device_id"];     // Duplicate key for compatibility
                siblings.append(sibling);
            }
        }
    }
    
    SetupDiDestroyDeviceInfoList(hDevInfo);
    return siblings;
}

QList<QVariantMap> WindowsDeviceManager::getChildDevicesPython(DWORD devInst)
{
    QList<QVariantMap> children;
    
    // Get first child
    DWORD childDevInst;
    if (CM_Get_Child(&childDevInst, devInst, 0) == CR_SUCCESS) {
        while (true) {
            QVariantMap child;
            child["hardware_id"] = getHardwareIdFromDevInst(childDevInst);
            child["device_id"] = getDeviceId(childDevInst);
            child["hardwareId"] = child["hardware_id"]; // Duplicate for compatibility  
            child["deviceId"] = child["device_id"];     // Duplicate for compatibility
            children.append(child);
            
            // Get grandchildren recursively
            QList<QVariantMap> grandChildren = getChildDevicesPython(childDevInst);
            children.append(grandChildren);
            
            // Get next sibling
            DWORD nextSibling;
            if (CM_Get_Sibling(&nextSibling, childDevInst, 0) != CR_SUCCESS) {
                break;
            }
            childDevInst = nextSibling;
        }
    }
    
    return children;
}

QString WindowsDeviceManager::getHardwareIdFromDevInst(DWORD devInst)
{
    // Find the device in all device classes and get its hardware ID
    HDEVINFO hDevInfo = SetupDiGetClassDevs(nullptr, nullptr, nullptr, DIGCF_PRESENT | DIGCF_ALLCLASSES);
    if (hDevInfo == INVALID_HANDLE_VALUE) {
        return "Unknown";
    }
    
    SP_DEVINFO_DATA devInfoData;
    devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
    
    for (DWORD index = 0; SetupDiEnumDeviceInfo(hDevInfo, index, &devInfoData); index++) {
        if (devInfoData.DevInst == devInst) {
            QString hardwareId = getHardwareId(hDevInfo, &devInfoData);
            SetupDiDestroyDeviceInfoList(hDevInfo);
            return hardwareId;
        }
    }
    
    SetupDiDestroyDeviceInfoList(hDevInfo);
    return "Unknown";
}

QList<QVariantMap> WindowsDeviceManager::enumerateAllDevices()
{
    QList<QVariantMap> allDevices;
    
    // Enumerate devices from all relevant device classes
    const GUID deviceClasses[] = {
        GUID_DEVCLASS_USB,
        GUID_DEVCLASS_PORTS,
        GUID_DEVCLASS_HIDCLASS,
        GUID_DEVCLASS_CAMERA,
        GUID_DEVCLASS_MEDIA
        // Note: GUID_DEVCLASS_SYSTEM might not be available on all systems
    };
    
    for (const GUID& classGuid : deviceClasses) {
        HDEVINFO hDevInfo = SetupDiGetClassDevs(&classGuid, nullptr, nullptr, DIGCF_PRESENT);
        if (hDevInfo == INVALID_HANDLE_VALUE) {
            continue;
        }
        
        SP_DEVINFO_DATA devInfoData;
        devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
        
        for (DWORD i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &devInfoData); i++) {
            QVariantMap deviceInfo = getDeviceInfo(devInfoData.DevInst);
            
            // Add additional setup API properties
            deviceInfo["friendlyName"] = getDeviceProperty(hDevInfo, &devInfoData, SPDRP_FRIENDLYNAME);
            deviceInfo["locationInfo"] = getDeviceProperty(hDevInfo, &devInfoData, SPDRP_LOCATION_INFORMATION);
            deviceInfo["manufacturer"] = getDeviceProperty(hDevInfo, &devInfoData, SPDRP_MFG);
            deviceInfo["service"] = getDeviceProperty(hDevInfo, &devInfoData, SPDRP_SERVICE);
            
            // Get parent device instance
            DWORD parentDevInst;
            if (CM_Get_Parent(&parentDevInst, devInfoData.DevInst, 0) == CR_SUCCESS) {
                deviceInfo["parentDeviceId"] = getDeviceId(parentDevInst);
            }
            
            allDevices.append(deviceInfo);
        }
        
        SetupDiDestroyDeviceInfoList(hDevInfo);
    }
    
    qCDebug(log_device_windows) << "Enumerated" << allDevices.size() << "devices from all classes";
    return allDevices;
}

QList<QVariantMap> WindowsDeviceManager::findDevicesWithVidPid(const QList<QVariantMap>& allDevices, const QString& vid, const QString& pid)
{
    QList<QVariantMap> matchingDevices;
    
    QString targetVidString = QString("VID_%1").arg(vid);
    QString targetPidString = QString("PID_%1").arg(pid);
    
    qCDebug(log_device_windows) << "Searching for devices with VID:" << targetVidString << "PID:" << targetPidString;
    qCDebug(log_device_windows) << "Scanning" << allDevices.size() << "total enumerated devices";
    
    for (int i = 0; i < allDevices.size(); ++i) {
        const QVariantMap& device = allDevices[i];
        QString hardwareId = device["hardwareId"].toString();
        QString deviceId = device["deviceId"].toString();
        QString friendlyName = device["friendlyName"].toString();
        
        // Check if this device matches our VID/PID
        if (hardwareId.contains(targetVidString, Qt::CaseInsensitive) &&
            hardwareId.contains(targetPidString, Qt::CaseInsensitive)) {
            
            matchingDevices.append(device);
            qCDebug(log_device_windows) << "MATCH[" << matchingDevices.size()-1 << "]:" << friendlyName;
            qCDebug(log_device_windows) << "  Hardware ID:" << hardwareId;
            qCDebug(log_device_windows) << "  Device ID:" << deviceId;
        }
    }
    
    qCDebug(log_device_windows) << "Found" << matchingDevices.size() << "matching devices";
    return matchingDevices;
}

QList<QVariantMap> WindowsDeviceManager::buildDeviceTree(const QList<QVariantMap>& allDevices)
{
    QList<QVariantMap> deviceTree;
    
    // Create a map for quick parent-child lookup
    QMap<QString, QList<QVariantMap>> parentToChildren;
    QMap<QString, QVariantMap> deviceMap;
    
    // First pass: build device map and parent-child relationships
    for (const QVariantMap& device : allDevices) {
        QString deviceId = device["deviceId"].toString();
        QString parentId = device["parentDeviceId"].toString();
        
        deviceMap[deviceId] = device;
        
        if (!parentId.isEmpty()) {
            parentToChildren[parentId].append(device);
        }
    }
    
    // Second pass: build tree structure with children embedded
    for (const QVariantMap& device : allDevices) {
        QVariantMap deviceNode = device;
        QString deviceId = device["deviceId"].toString();
        
        // Add children to this device node
        if (parentToChildren.contains(deviceId)) {
            QList<QVariant> children;
            for (const QVariantMap& child : parentToChildren[deviceId]) {
                children.append(child);
            }
            deviceNode["children"] = children;
        }
        
        deviceTree.append(deviceNode);
    }
    
    return deviceTree;
}

QList<QVariantMap> WindowsDeviceManager::findChildDevicesInTree(const QList<QVariantMap>& deviceTree, const QString& parentDeviceId)
{
    QList<QVariantMap> childDevices;
    
    // Find the parent device in the tree
    for (const QVariantMap& device : deviceTree) {
        QString deviceId = device["deviceId"].toString();
        
        if (deviceId == parentDeviceId) {
            // Found the parent, get all its children recursively
            if (device.contains("children")) {
                QList<QVariant> children = device["children"].toList();
                for (const QVariant& child : children) {
                    QVariantMap childMap = child.toMap();
                    childDevices.append(childMap);
                    
                    // Recursively get children of children
                    QString childId = childMap["deviceId"].toString();
                    QList<QVariantMap> grandChildren = findChildDevicesInTree(deviceTree, childId);
                    childDevices.append(grandChildren);
                }
            }
            break;
        }
        
        // Also check if this device's parent matches (in case of multi-level relationships)
        QString parentId = device["parentDeviceId"].toString();
        if (parentId == parentDeviceId) {
            childDevices.append(device);
            
            // Recursively get children of this child
            QString childId = device["deviceId"].toString();
            QList<QVariantMap> grandChildren = findChildDevicesInTree(deviceTree, childId);
            childDevices.append(grandChildren);
        }
    }
    
    return childDevices;
}

QList<DWORD> WindowsDeviceManager::findParentUSBDevices(const QString& vid, const QString& pid)
{
    QList<DWORD> parentDevices;
    
    HDEVINFO hDevInfo = SetupDiGetClassDevs(
        &GUID_DEVCLASS_USB,
        nullptr,
        nullptr,
        DIGCF_PRESENT);
        
    if (hDevInfo == INVALID_HANDLE_VALUE) {
        qCWarning(log_device_windows) << "Failed to get USB device list";
        return parentDevices;
    }
    
    SP_DEVINFO_DATA devInfoData;
    devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
    
    QString targetVidString = QString("VID_%1").arg(vid);
    QString targetPidString = QString("PID_%1").arg(pid);
    
    for (DWORD i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &devInfoData); i++) {
        QString hardwareId = getHardwareId(hDevInfo, &devInfoData);
        
        // Debug: show all USB devices being checked
        if (hardwareId.contains("USB\\VID_", Qt::CaseInsensitive)) {
            qCDebug(log_device_windows) << "Checking USB device:" << hardwareId;
        }
        
        // Check if this device matches our VID/PID
        if (hardwareId.contains(targetVidString, Qt::CaseInsensitive) &&
            hardwareId.contains(targetPidString, Qt::CaseInsensitive)) {
            
            parentDevices.append(devInfoData.DevInst);
            qCDebug(log_device_windows) << "Found matching parent USB device:" << hardwareId;
        }
    }
    
    SetupDiDestroyDeviceInfoList(hDevInfo);
    return parentDevices;
}

QString WindowsDeviceManager::extractPortChainFromDeviceId(const QString& deviceId)
{
    // Extract port chain directly from device ID
    // Example: "USB\VID_534D&PID_2109\1&2&3" becomes "1-2-3"
    if (deviceId.isEmpty()) {
        return QString();
    }
    
    QStringList portNumbers;
    QRegularExpression portRegex("\\\\(\\d+)");
    QRegularExpressionMatchIterator matches = portRegex.globalMatch(deviceId);
    while (matches.hasNext()) {
        QRegularExpressionMatch match = matches.next();
        portNumbers.append(match.captured(1));
    }
    
    QString portChain = portNumbers.join("-");
    qCDebug(log_device_windows) << "Extracted port chain from device ID:" << deviceId << "-> " << portChain;
    return portChain;
}

void WindowsDeviceManager::matchDevicePaths(DeviceInfo& deviceInfo)
{
    // qCDebug(log_device_windows) << "Matching device paths for device:" << deviceInfo.deviceInstanceId;
    
    // This method is now primarily called from the main discovery logic
    // which already aggregates all interfaces into the physical device.
    // This is a fallback method for compatibility.
    
    // First, try to use child device data from platformSpecific if available
    QVariantMap platformData = deviceInfo.platformSpecific;
    if (platformData.contains("children")) {
        QList<QVariant> childrenVariantList = platformData["children"].toList();
        QList<QVariantMap> children;
        for (const QVariant& child : childrenVariantList) {
            children.append(child.toMap());
        }
        
        // qCDebug(log_device_windows) << "Using child device data from platformSpecific (" << children.size() << " children)";
        matchDevicePathsFromChildren(deviceInfo, children);
    } else {
        qCDebug(log_device_windows) << "No child device data in platformSpecific, using enhanced enumeration";
        
        // Use the enhanced enumeration approach for consistency
        QList<QVariantMap> allDevices = enumerateAllDevices();
        QList<QVariantMap> deviceTree = buildDeviceTree(allDevices);
        QList<QVariantMap> childDevices = findChildDevicesInTree(deviceTree, deviceInfo.deviceInstanceId);
        
        matchDevicePathsFromChildren(deviceInfo, childDevices);
    }
    
    // qCDebug(log_device_windows) << "Device matching complete:"
    //                            << "Serial:" << deviceInfo.serialPortPath
    //                            << "HID:" << deviceInfo.hidDevicePath
    //                            << "Camera:" << deviceInfo.cameraDevicePath
    //                            << "Audio:" << deviceInfo.audioDevicePath;
}

void WindowsDeviceManager::matchDevicePathsFromChildren(DeviceInfo& deviceInfo, const QList<QVariantMap>& children)
{
    qCDebug(log_device_windows) << "=== Matching device paths from" << children.size() << "child devices for:" << deviceInfo.deviceInstanceId << "===";
    
    // Search child devices for specific types
    for (int i = 0; i < children.size(); ++i) {
        const auto& child = children[i];
        QString deviceClass = child["class"].toString();
        QString hardwareId = child["hardwareId"].toString();
        QString friendlyName = child["friendlyName"].toString();
        QString deviceId = child["deviceId"].toString();
        QString description = child["description"].toString();
        
        // qCDebug(log_device_windows) << "  Child[" << i << "] Analysis:";
        // qCDebug(log_device_windows) << "    Device ID:" << deviceId;
        // qCDebug(log_device_windows) << "    Hardware ID:" << hardwareId;
        // qCDebug(log_device_windows) << "    Friendly Name:" << friendlyName;
        // qCDebug(log_device_windows) << "    Description:" << description;
        // qCDebug(log_device_windows) << "    Class:" << deviceClass;
        
        // Check for serial port (COM port) - this is a child device
        if (deviceClass.compare("Ports", Qt::CaseInsensitive) == 0 ||
            friendlyName.contains("COM", Qt::CaseInsensitive) ||
            hardwareId.contains("VID_" + AbstractPlatformDeviceManager::SERIAL_VID, Qt::CaseInsensitive)) {
            
            // qCDebug(log_device_windows) << "    → Identified as SERIAL PORT device";
            
            // Extract COM port number from friendly name
            QRegularExpression comRegex("\\(COM(\\d+)\\)");
            QRegularExpressionMatch match = comRegex.match(friendlyName);
            if (match.hasMatch()) {
                deviceInfo.serialPortPath = QString("COM%1").arg(match.captured(1));
                deviceInfo.serialPortId = deviceId;
                // qCDebug(log_device_windows) << "    ✓ Found serial port (primary match):" << deviceInfo.serialPortPath << "ID:" << deviceInfo.serialPortId;
            } else {
                // Fallback: look for COM followed by digits anywhere in the string
                QRegularExpression comRegex2("COM(\\d+)");
                QRegularExpressionMatch match2 = comRegex2.match(friendlyName);
                if (match2.hasMatch()) {
                    deviceInfo.serialPortPath = QString("COM%1").arg(match2.captured(1));
                    deviceInfo.serialPortId = deviceId;
                    // qCDebug(log_device_windows) << "    ✓ Found serial port (fallback match):" << deviceInfo.serialPortPath << "ID:" << deviceInfo.serialPortId;
                } else {
                    qCDebug(log_device_windows) << "    ✗ Could not extract COM port number from:" << friendlyName;
                }
            }
        }
        
        // Check for HID device
        else if (deviceClass.compare("HIDClass", Qt::CaseInsensitive) == 0 ||
                 hardwareId.contains(QString("VID_%1").arg(AbstractPlatformDeviceManager::HID_VID), Qt::CaseInsensitive)) {
            if (deviceId.toUpper().contains("HID")){
                deviceInfo.hidDeviceId = deviceId;
                qCDebug(log_device_windows) << "Found HID device ID:" << deviceInfo.hidDeviceId << "with hardware ID:" << hardwareId;
            }
        }
        
        // Check for camera device
        else if (deviceClass.compare("Camera", Qt::CaseInsensitive) == 0 ||
                 deviceClass.compare("Image", Qt::CaseInsensitive) == 0 ||
                 hardwareId.contains("534D", Qt::CaseInsensitive) ||
                 friendlyName.contains("MacroSilicon", Qt::CaseInsensitive)) {
            
            qCDebug(log_device_windows) << "    → Identified as CAMERA device";
            
            // Verify this camera device is associated with the correct port chain
            if (verifyCameraDeviceAssociation(deviceId, deviceInfo.deviceInstanceId, deviceInfo.portChain)) {
                deviceInfo.cameraDeviceId = deviceId;
                // For camera devices, we need to find the actual device path, not just store the device ID
                // The device path will be resolved later in matchDevicePathsToRealPaths
                qCDebug(log_device_windows) << "    ✓ Found CAMERA device ID with verified association:" << deviceInfo.cameraDeviceId;
            } else {
                qCDebug(log_device_windows) << "    ✗ Camera device association verification failed for port chain:" << deviceInfo.portChain;
            }
        }
        
        // Check for audio device
        else if (deviceClass.compare("Media", Qt::CaseInsensitive) == 0 ||
                 deviceClass.compare("AudioEndpoint", Qt::CaseInsensitive) == 0 ||
                 hardwareId.contains("534D", Qt::CaseInsensitive) ||
                 friendlyName.contains("MacroSilicon", Qt::CaseInsensitive)) {
            
            qCDebug(log_device_windows) << "    → Identified as AUDIO device";
            
            // Verify this audio device is associated with the correct port chain
            if (verifyAudioDeviceAssociation(deviceId, deviceInfo.deviceInstanceId, deviceInfo.portChain)) {
                deviceInfo.audioDeviceId = deviceId;
                // For audio devices, we need to find the actual device path, not just store the device ID
                // The device path will be resolved later in matchDevicePathsToRealPaths
                qCDebug(log_device_windows) << "    ✓ Found AUDIO device ID with verified association:" << deviceInfo.audioDeviceId;
            } else {
                qCDebug(log_device_windows) << "    ✗ Audio device association verification failed for port chain:" << deviceInfo.portChain;
            }
        } else {
            qCDebug(log_device_windows) << "    - No specific match found for this device";
        }
    }
    
    qCDebug(log_device_windows) << "=== End device path matching ===";
}


QString WindowsDeviceManager::findComPortByLocation(const QString& location)
{
    QList<QVariantMap> ports = enumerateDevicesByClass(GUID_DEVCLASS_PORTS);
    
    for (const QVariantMap& port : ports) {
        QString portLocation = port.value("locationInfo").toString();
        if (portLocation.contains(location, Qt::CaseInsensitive)) {
            return port.value("friendlyName").toString();
        }
    }
    
    return QString();
}

QString WindowsDeviceManager::findHIDByDeviceId(const QString& deviceId)
{
    // For HID devices, we need to enumerate using the HID GUID to get device interface paths
    GUID hidGuid;
    HidD_GetHidGuid(&hidGuid);
    
    HDEVINFO hDevInfo = SetupDiGetClassDevs(&hidGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (hDevInfo == INVALID_HANDLE_VALUE) {
        return QString();
    }
    
    SP_DEVICE_INTERFACE_DATA interfaceData;
    interfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);
    
    // Enumerate device interfaces
    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(hDevInfo, nullptr, &hidGuid, i, &interfaceData); i++) {
        DWORD requiredSize = 0;
        
        // Get required buffer size
        SetupDiGetDeviceInterfaceDetail(hDevInfo, &interfaceData, nullptr, 0, &requiredSize, nullptr);
        
        if (requiredSize == 0) {
            continue;
        }
        
        // Allocate buffer and get interface detail
        std::vector<BYTE> buffer(requiredSize);
        PSP_DEVICE_INTERFACE_DETAIL_DATA interfaceDetail = reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA>(buffer.data());
        interfaceDetail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);
        
        SP_DEVINFO_DATA interfaceDevInfoData;
        interfaceDevInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
        
        if (SetupDiGetDeviceInterfaceDetail(hDevInfo, &interfaceData, interfaceDetail, 
                                          requiredSize, nullptr, &interfaceDevInfoData)) {
            QString interfaceDeviceId = getDeviceId(interfaceDevInfoData.DevInst);
            
            // Check if this interface belongs to our target device
            if (interfaceDeviceId == deviceId) {
                // Verify this is our HID device by checking VID/PID
                QString hardwareId = getHardwareId(hDevInfo, &interfaceDevInfoData);
                if (hardwareId.contains(AbstractPlatformDeviceManager::HID_VID, Qt::CaseInsensitive) &&
                    hardwareId.contains(AbstractPlatformDeviceManager::HID_PID, Qt::CaseInsensitive)) {
                    QString devicePath = QString::fromWCharArray(interfaceDetail->DevicePath);
                    SetupDiDestroyDeviceInfoList(hDevInfo);
                    return devicePath;
                }
            }
        }
    }
    
    SetupDiDestroyDeviceInfoList(hDevInfo);
    return QString();
}

QPair<QString, QString> WindowsDeviceManager::findCameraAudioByDeviceInfo(const DeviceInfo& deviceInfo)
{
    QString cameraPath, audioPath;
    
    qCDebug(log_device_windows) << "=== Finding camera/audio devices for port chain:" << deviceInfo.portChain << "===";
    qCDebug(log_device_windows) << "Target device instance ID:" << deviceInfo.deviceInstanceId;
    
    // Find camera devices with parent device verification
    QList<QVariantMap> cameras = enumerateDevicesByClassWithParentInfo(GUID_DEVCLASS_CAMERA);
    for (const QVariantMap& camera : cameras) {
        QString cameraDeviceId = camera.value("deviceId").toString();
        QString parentDeviceId = camera.value("parentDeviceId").toString();
        
        qCDebug(log_device_windows) << "  Checking camera device:" << cameraDeviceId;
        qCDebug(log_device_windows) << "    Parent device ID:" << parentDeviceId;
        
        // Check if this camera matches our hardware identifiers
        if (cameraDeviceId.contains("MacroSilicon", Qt::CaseInsensitive) ||
            cameraDeviceId.contains("534D", Qt::CaseInsensitive)) {
            
            // Verify the parent device is associated with our target device
            if (isDeviceAssociatedWithPortChain(parentDeviceId, deviceInfo.deviceInstanceId, deviceInfo.portChain)) {
                cameraPath = camera.value("devicePath").toString();
                qCDebug(log_device_windows) << "    ✓ Found matching camera device:" << cameraPath;
                qCDebug(log_device_windows) << "      Parent verification passed for port chain:" << deviceInfo.portChain;
                break;
            } else {
                qCDebug(log_device_windows) << "    ✗ Camera device parent does not match target port chain";
            }
        }
    }
    
    // Find audio devices with parent device verification
    QList<QVariantMap> audioDevices = enumerateDevicesByClassWithParentInfo(GUID_DEVCLASS_MEDIA);
    for (const QVariantMap& audio : audioDevices) {
        QString audioDeviceId = audio.value("deviceId").toString();
        QString parentDeviceId = audio.value("parentDeviceId").toString();
        
        qCDebug(log_device_windows) << "  Checking audio device:" << audioDeviceId;
        qCDebug(log_device_windows) << "    Parent device ID:" << parentDeviceId;
        
        // Check if this audio device matches our hardware identifiers
        if (audioDeviceId.contains("MacroSilicon", Qt::CaseInsensitive) ||
            audioDeviceId.contains("534D", Qt::CaseInsensitive)) {
            
            // Verify the parent device is associated with our target device
            if (isDeviceAssociatedWithPortChain(parentDeviceId, deviceInfo.deviceInstanceId, deviceInfo.portChain)) {
                audioPath = audio.value("devicePath").toString();
                qCDebug(log_device_windows) << "    ✓ Found matching audio device:" << audioPath;
                qCDebug(log_device_windows) << "      Parent verification passed for port chain:" << deviceInfo.portChain;
                break;
            } else {
                qCDebug(log_device_windows) << "    ✗ Audio device parent does not match target port chain";
            }
        }
    }
    
    qCDebug(log_device_windows) << "=== Camera/audio search complete ===";
    return qMakePair(cameraPath, audioPath);
}

// Windows API Helper implementations
QString WindowsDeviceManager::getDeviceId(DWORD devInst)
{
    WCHAR deviceId[MAX_PATH];
    if (CM_Get_Device_ID(devInst, deviceId, MAX_PATH, 0) == CR_SUCCESS) {
        return QString::fromWCharArray(deviceId);
    }
    return QString();
}

QString WindowsDeviceManager::getDeviceProperty(HDEVINFO hDevInfo, SP_DEVINFO_DATA* devInfoData, DWORD property)
{
    DWORD dataType;
    DWORD bufferSize = 0;
    
    // Get required buffer size
    SetupDiGetDeviceRegistryProperty(hDevInfo, devInfoData, property, &dataType, nullptr, 0, &bufferSize);
    
    if (bufferSize == 0) {
        return QString();
    }
    
    std::vector<BYTE> buffer(bufferSize);
    if (SetupDiGetDeviceRegistryProperty(hDevInfo, devInfoData, property, &dataType, 
                                        buffer.data(), bufferSize, nullptr)) {
        // Windows device properties are typically UTF-16 strings
        if (dataType == REG_SZ || dataType == REG_MULTI_SZ) {
            return QString::fromUtf16(reinterpret_cast<const char16_t*>(buffer.data()));
        } else {
            // Fallback for other data types
            return QString::fromLocal8Bit(reinterpret_cast<const char*>(buffer.data()));
        }
    }
    
    return QString();
}

QString WindowsDeviceManager::getHardwareId(HDEVINFO hDevInfo, SP_DEVINFO_DATA* devInfoData)
{
    return getDeviceProperty(hDevInfo, devInfoData, SPDRP_HARDWAREID);
}

QString WindowsDeviceManager::getDeviceInterfacePath(HDEVINFO hDevInfo, SP_DEVINFO_DATA* devInfoData, const GUID& interfaceGuid)
{
    // Create a new device info set for device interfaces
    HDEVINFO hInterfaceDevInfo = SetupDiGetClassDevs(&interfaceGuid, nullptr, nullptr, 
                                                     DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (hInterfaceDevInfo == INVALID_HANDLE_VALUE) {
        return QString();
    }
    
    SP_DEVICE_INTERFACE_DATA interfaceData;
    interfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);
    
    QString targetDeviceId = getDeviceId(devInfoData->DevInst);
    
    // Enumerate device interfaces
    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(hInterfaceDevInfo, nullptr, &interfaceGuid, i, &interfaceData); i++) {
        DWORD requiredSize = 0;
        
        // Get required buffer size
        SetupDiGetDeviceInterfaceDetail(hInterfaceDevInfo, &interfaceData, nullptr, 0, &requiredSize, nullptr);
        
        if (requiredSize == 0) {
            continue;
        }
        
        // Allocate buffer and get interface detail
        std::vector<BYTE> buffer(requiredSize);
        PSP_DEVICE_INTERFACE_DETAIL_DATA interfaceDetail = reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA>(buffer.data());
        interfaceDetail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);
        
        SP_DEVINFO_DATA interfaceDevInfoData;
        interfaceDevInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
        
        if (SetupDiGetDeviceInterfaceDetail(hInterfaceDevInfo, &interfaceData, interfaceDetail, 
                                          requiredSize, nullptr, &interfaceDevInfoData)) {
            QString interfaceDeviceId = getDeviceId(interfaceDevInfoData.DevInst);
            
            // Check if this interface belongs to our target device
            if (interfaceDeviceId == targetDeviceId) {
                QString devicePath = QString::fromWCharArray(interfaceDetail->DevicePath);
                SetupDiDestroyDeviceInfoList(hInterfaceDevInfo);
                return devicePath;
            }
        }
    }
    
    SetupDiDestroyDeviceInfoList(hInterfaceDevInfo);
    return QString();
}

QList<QVariantMap> WindowsDeviceManager::getSiblingDevices(DWORD parentDevInst)
{
    QList<QVariantMap> siblings;
    // Implementation for getting sibling devices
    // This would involve enumerating child devices of the parent
    return siblings;
}

QList<QVariantMap> WindowsDeviceManager::getChildDevices(DWORD devInst)
{
    QList<QVariantMap> children;
    // Implementation for getting child devices
    // This would involve using CM_Get_Child and CM_Get_Sibling
    return children;
}

QList<QVariantMap> WindowsDeviceManager::getAllChildDevices(DWORD parentDevInst)
{
    QList<QVariantMap> allChildren;
    
    // Get first child
    DWORD childDevInst;
    if (CM_Get_Child(&childDevInst, parentDevInst, 0) != CR_SUCCESS) {
        return allChildren; // No children
    }
    
    // Process first child and all its siblings
    DWORD currentChild = childDevInst;
    do {
        QVariantMap childInfo = getDeviceInfo(currentChild);
        allChildren.append(childInfo);
        
        QString deviceClass = childInfo["class"].toString();
        QString hardwareId = childInfo["hardwareId"].toString();
        
        qCDebug(log_device_windows) << "Found child device:" << hardwareId 
                                   << "Class:" << deviceClass;
        
        // Recursively get children of this child
        QList<QVariantMap> grandChildren = getAllChildDevices(currentChild);
        allChildren.append(grandChildren);
        
        // Move to next sibling
    } while (CM_Get_Sibling(&currentChild, currentChild, 0) == CR_SUCCESS);
    
    return allChildren;
}

QVariantMap WindowsDeviceManager::getDeviceInfo(DWORD devInst)
{
    QVariantMap deviceInfo;
    
    QString deviceId = getDeviceId(devInst);
    deviceInfo["deviceId"] = deviceId;
    deviceInfo["devInst"] = static_cast<uint>(devInst);
    
    // Get device class
    GUID classGuid;
    ULONG classGuidSize = sizeof(classGuid);
    if (CM_Get_DevNode_Registry_Property(devInst, CM_DRP_CLASSGUID, nullptr, 
                                        &classGuid, &classGuidSize, 0) == CR_SUCCESS) {
        WCHAR guidString[128];
        if (StringFromGUID2(classGuid, guidString, 128) > 0) {
            deviceInfo["classGuid"] = QString::fromWCharArray(guidString);
        }
    }
    
    // Get device class name
    WCHAR className[256];
    ULONG classNameSize = sizeof(className);
    if (CM_Get_DevNode_Registry_Property(devInst, CM_DRP_CLASS, nullptr,
                                        className, &classNameSize, 0) == CR_SUCCESS) {
        deviceInfo["class"] = QString::fromWCharArray(className);
    }
    
    // Get hardware ID
    WCHAR hardwareId[1024];
    ULONG hardwareIdSize = sizeof(hardwareId);
    if (CM_Get_DevNode_Registry_Property(devInst, CM_DRP_HARDWAREID, nullptr,
                                        hardwareId, &hardwareIdSize, 0) == CR_SUCCESS) {
        deviceInfo["hardwareId"] = QString::fromWCharArray(hardwareId);
    }
    
    // Get friendly name
    WCHAR friendlyName[256];
    ULONG friendlyNameSize = sizeof(friendlyName);
    if (CM_Get_DevNode_Registry_Property(devInst, CM_DRP_FRIENDLYNAME, nullptr,
                                        friendlyName, &friendlyNameSize, 0) == CR_SUCCESS) {
        deviceInfo["friendlyName"] = QString::fromWCharArray(friendlyName);
    }
    
    // Get device description
    WCHAR description[256];
    ULONG descriptionSize = sizeof(description);
    if (CM_Get_DevNode_Registry_Property(devInst, CM_DRP_DEVICEDESC, nullptr,
                                        description, &descriptionSize, 0) == CR_SUCCESS) {
        deviceInfo["description"] = QString::fromWCharArray(description);
    }
    
    return deviceInfo;
}

QString WindowsDeviceManager::getPortChain(DWORD devInst)
{
    QStringList portChainList;
    
    DWORD currentInst = devInst;
    DWORD parentInst;
    
    // Walk up the device tree to build port chain
    while (CM_Get_Parent(&parentInst, currentInst, 0) == CR_SUCCESS) {
        QString deviceId = getDeviceId(currentInst);
        if (deviceId.contains("USB", Qt::CaseInsensitive)) {
            // Extract port number from device ID
            QRegularExpression re("\\\\(\\d+)$");
            QRegularExpressionMatch match = re.match(deviceId);
            if (match.hasMatch()) {
                portChainList.prepend(match.captured(1));
            }
        }
        currentInst = parentInst;
    }
    
    return portChainList.join("-");
}

QList<QVariantMap> WindowsDeviceManager::enumerateDevicesByClass(const GUID& classGuid)
{
    QList<QVariantMap> devices;
    
    HDEVINFO hDevInfo = SetupDiGetClassDevs(&classGuid, nullptr, nullptr, DIGCF_PRESENT);
    if (hDevInfo == INVALID_HANDLE_VALUE) {
        return devices;
    }
    
    SP_DEVINFO_DATA devInfoData;
    devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
    
    for (DWORD i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &devInfoData); i++) {
        QVariantMap device;
        device["deviceId"] = getDeviceId(devInfoData.DevInst);
        device["friendlyName"] = getDeviceProperty(hDevInfo, &devInfoData, SPDRP_FRIENDLYNAME);
        device["description"] = getDeviceProperty(hDevInfo, &devInfoData, SPDRP_DEVICEDESC);
        device["locationInfo"] = getDeviceProperty(hDevInfo, &devInfoData, SPDRP_LOCATION_INFORMATION);
        device["hardwareId"] = getDeviceProperty(hDevInfo, &devInfoData, SPDRP_HARDWAREID);
        devices.append(device);
    }
    
    SetupDiDestroyDeviceInfoList(hDevInfo);
    return devices;
}

void WindowsDeviceManager::cleanup()
{
    // Clean up any resources if needed
}

void WindowsDeviceManager::clearCache()
{
    qCDebug(log_device_windows) << "Clearing device cache";
    m_cachedDevices.clear();
    m_lastCacheUpdate = QDateTime();
}

void WindowsDeviceManager::debugListAllUSBDevices()
{
    qCDebug(log_device_windows) << "=== Debugging: All USB devices ===";
    
    HDEVINFO hDevInfo = SetupDiGetClassDevs(
        &GUID_DEVCLASS_USB,
        NULL,
        NULL,
        DIGCF_PRESENT
    );
    
    if (hDevInfo == INVALID_HANDLE_VALUE) {
        qCWarning(log_device_windows) << "Failed to get USB device list";
        return;
    }
    
    SP_DEVINFO_DATA devInfoData;
    devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
    
    for (DWORD i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &devInfoData); i++) {
        QString hardwareId = getHardwareId(hDevInfo, &devInfoData);
        QString description = getDeviceProperty(hDevInfo, &devInfoData, SPDRP_DEVICEDESC);
        
        // Only show USB devices
        if (hardwareId.contains("USB\\VID_", Qt::CaseInsensitive)) {
            qCDebug(log_device_windows) << "USB Device:" << hardwareId;
            qCDebug(log_device_windows) << "  Description:" << description;
        }
    }
    
    SetupDiDestroyDeviceInfoList(hDevInfo);
    qCDebug(log_device_windows) << "=== End USB device list ===";
}

void WindowsDeviceManager::matchDevicePathsToRealPaths(DeviceInfo& deviceInfo)
{
    qCDebug(log_device_windows) << "=== Converting device IDs to real paths ===";
    
    // Convert serial port device ID to COM port path
    if (!deviceInfo.serialPortId.isEmpty()) {
        QString comPort = findComPortByLocation(deviceInfo.portChain);
        if (!comPort.isEmpty()) {
            deviceInfo.serialPortPath = comPort;
            qCDebug(log_device_windows) << "  ✓ Serial Port:" << deviceInfo.serialPortPath;
        } else {
            qCDebug(log_device_windows) << "  ✗ Could not find COM port for location:" << deviceInfo.portChain;
        }
    }
    
    // For HID, the device ID is already the path we need
    if (!deviceInfo.hidDeviceId.isEmpty()) {
        // Extract instance ID for HID path matching
        QString instanceId = deviceInfo.hidDeviceId;
        if (instanceId.contains('\\')) {
            QStringList parts = instanceId.split('\\');
            if (!parts.isEmpty()) {
                instanceId = parts.last();
            }
        }
        
        // For Windows, HID path is typically the device interface path
        // We can use the device ID as-is or find the HID device path
        deviceInfo.hidDevicePath = findHIDByDeviceId(deviceInfo.hidDeviceId);
        if (deviceInfo.hidDevicePath.isEmpty()) {
            deviceInfo.hidDevicePath = deviceInfo.hidDeviceId; // Fallback
        }
        qCDebug(log_device_windows) << "  ✓ HID Device:" << deviceInfo.hidDevicePath;
    }
    
    // Convert camera and audio device IDs to actual device paths
    if (!deviceInfo.cameraDeviceId.isEmpty() || !deviceInfo.audioDeviceId.isEmpty()) {
        QPair<QString, QString> paths = findCameraAudioByDeviceInfo(deviceInfo);
        
        if (!paths.first.isEmpty()) {
            deviceInfo.cameraDevicePath = paths.first;
            qCDebug(log_device_windows) << "  ✓ Camera Device:" << deviceInfo.cameraDevicePath;
        } else {
            qCDebug(log_device_windows) << "  ✗ Could not find camera path for:" << deviceInfo.cameraDeviceId;
        }
        
        if (!paths.second.isEmpty()) {
            deviceInfo.audioDevicePath = paths.second;
            qCDebug(log_device_windows) << "  ✓ Audio Device:" << deviceInfo.audioDevicePath;
        } else {
            qCDebug(log_device_windows) << "  ✗ Could not find audio path for:" << deviceInfo.audioDeviceId;
        }
    }
    
    qCDebug(log_device_windows) << "=== End path conversion ===";
}

QString WindowsDeviceManager::findComPortByDeviceId(const QString& deviceId)
{
    qCDebug(log_device_windows) << "Finding COM port for device ID:" << deviceId;
    
    // Enumerate all serial port devices
    HDEVINFO hDevInfo = SetupDiGetClassDevs(&GUID_DEVCLASS_PORTS, nullptr, nullptr, DIGCF_PRESENT);
    if (hDevInfo == INVALID_HANDLE_VALUE) {
        qCWarning(log_device_windows) << "Failed to get serial ports device list";
        return QString();
    }
    
    SP_DEVINFO_DATA devInfoData;
    devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
    
    for (DWORD i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &devInfoData); i++) {
        QString portDeviceId = getDeviceId(devInfoData.DevInst);
        QString friendlyName = getDeviceProperty(hDevInfo, &devInfoData, SPDRP_FRIENDLYNAME);
        QString hardwareId = getDeviceProperty(hDevInfo, &devInfoData, SPDRP_HARDWAREID);
        
        qCDebug(log_device_windows) << "  Checking port:" << friendlyName << "Device ID:" << portDeviceId;
        
        // Check if this port matches our target device ID
        // Compare using the instance ID part (last part after the last backslash)
        QString targetInstanceId = deviceId;
        QString portInstanceId = portDeviceId;
        
        if (targetInstanceId.contains('\\')) {
            QStringList parts = targetInstanceId.split('\\');
            if (!parts.isEmpty()) {
                targetInstanceId = parts.last();
            }
        }
        
        if (portInstanceId.contains('\\')) {
            QStringList parts = portInstanceId.split('\\');
            if (!parts.isEmpty()) {
                portInstanceId = parts.last();
            }
        }
        
        // Also check if the hardware ID contains our serial VID/PID
        bool hardwareIdMatches = hardwareId.toUpper().contains(AbstractPlatformDeviceManager::SERIAL_VID.toUpper()) &&
                                hardwareId.toUpper().contains(AbstractPlatformDeviceManager::SERIAL_PID.toUpper());
        
        if (portInstanceId == targetInstanceId || hardwareIdMatches) {
            qCDebug(log_device_windows) << "    Found matching port:" << friendlyName;
            
            // Extract COM port number from friendly name
            QRegularExpression comRegex("\\(COM(\\d+)\\)");
            QRegularExpressionMatch match = comRegex.match(friendlyName);
            if (match.hasMatch()) {
                QString comPort = QString("COM%1").arg(match.captured(1));
                qCDebug(log_device_windows) << "    ✓ Extracted COM port:" << comPort;
                SetupDiDestroyDeviceInfoList(hDevInfo);
                return comPort;
            } else {
                // Fallback: look for COM followed by digits anywhere in the string
                QRegularExpression comRegex2("COM(\\d+)");
                QRegularExpressionMatch match2 = comRegex2.match(friendlyName);
                if (match2.hasMatch()) {
                    QString comPort = QString("COM%1").arg(match2.captured(1));
                    qCDebug(log_device_windows) << "    ✓ Extracted COM port (fallback):" << comPort;
                    SetupDiDestroyDeviceInfoList(hDevInfo);
                    return comPort;
                }
            }
        }
    }
    
    SetupDiDestroyDeviceInfoList(hDevInfo);
    qCDebug(log_device_windows) << "  ✗ No matching COM port found for device ID:" << deviceId;
    return QString();
}

QString WindowsDeviceManager::findComPortByPortChain(const QString& portChain)
{
    qCDebug(log_device_windows) << "Finding COM port for port chain:" << portChain;
    
    // Use QSerialPortInfo to enumerate COM ports, similar to Python's serial.tools.list_ports.comports()
    const auto serialPorts = QSerialPortInfo::availablePorts();
    
    for (const QSerialPortInfo& portInfo : serialPorts) {
        QString portLocation = portInfo.description(); // Try description first
        QString systemLocation = portInfo.systemLocation();
        
        qCDebug(log_device_windows) << "  Checking port:" << portInfo.portName() 
                                   << "Description:" << portLocation
                                   << "System Location:" << systemLocation;
        
        // Check if this is our target serial device by VID/PID
        if (portInfo.hasVendorIdentifier() && portInfo.hasProductIdentifier()) {
            QString vidStr = QString("%1").arg(portInfo.vendorIdentifier(), 4, 16, QChar('0')).toUpper();
            QString pidStr = QString("%1").arg(portInfo.productIdentifier(), 4, 16, QChar('0')).toUpper();
            
            // Match our serial VID/PID (1A86:7523)
            if (vidStr == AbstractPlatformDeviceManager::SERIAL_VID.toUpper() && 
                pidStr == AbstractPlatformDeviceManager::SERIAL_PID.toUpper()) {
                
                qCDebug(log_device_windows) << "    Found matching VID/PID:" << vidStr << ":" << pidStr;
                
                // Now check if the port chain matches using location information
                // Unfortunately, QSerialPortInfo doesn't directly provide the port chain,
                // so we need to use Windows APIs to get the device location
                QString devicePortChain = getPortChainForSerialPort(portInfo.portName());
                
                qCDebug(log_device_windows) << "    Port chain for" << portInfo.portName() << ":" << devicePortChain;
                
                if (devicePortChain == portChain) {
                    qCDebug(log_device_windows) << "    ✓ Port chain matches! Found COM port:" << portInfo.portName();
                    return portInfo.portName();
                }
            }
        }
    }
    
    qCDebug(log_device_windows) << "  ✗ No matching COM port found for port chain:" << portChain;
    return QString();
}

QString WindowsDeviceManager::getPortChainForSerialPort(const QString& portName)
{
    // Find the device instance for this COM port and build its port chain
    HDEVINFO hDevInfo = SetupDiGetClassDevs(&GUID_DEVCLASS_PORTS, nullptr, nullptr, DIGCF_PRESENT);
    if (hDevInfo == INVALID_HANDLE_VALUE) {
        return QString();
    }
    
    SP_DEVINFO_DATA devInfoData;
    devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
    
    for (DWORD i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &devInfoData); i++) {
        QString friendlyName = getDeviceProperty(hDevInfo, &devInfoData, SPDRP_FRIENDLYNAME);
        
        // Check if this is our target port by friendly name
        if (friendlyName.contains(QString("(%1)").arg(portName), Qt::CaseInsensitive)) {
            // Build the port chain for this device
            QString portChain = buildPythonCompatiblePortChain(devInfoData.DevInst);
            SetupDiDestroyDeviceInfoList(hDevInfo);
            return portChain;
        }
    }
    
    SetupDiDestroyDeviceInfoList(hDevInfo);
    return QString();
}

QList<QVariantMap> WindowsDeviceManager::enumerateDevicesByClassWithParentInfo(const GUID& classGuid)
{
    QList<QVariantMap> devices;
    
    HDEVINFO hDevInfo = SetupDiGetClassDevs(&classGuid, nullptr, nullptr, DIGCF_PRESENT);
    if (hDevInfo == INVALID_HANDLE_VALUE) {
        return devices;
    }
    
    SP_DEVINFO_DATA devInfoData;
    devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
    
    for (DWORD i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &devInfoData); i++) {
        QVariantMap device;
        device["deviceId"] = getDeviceId(devInfoData.DevInst);
        device["friendlyName"] = getDeviceProperty(hDevInfo, &devInfoData, SPDRP_FRIENDLYNAME);
        device["description"] = getDeviceProperty(hDevInfo, &devInfoData, SPDRP_DEVICEDESC);
        device["locationInfo"] = getDeviceProperty(hDevInfo, &devInfoData, SPDRP_LOCATION_INFORMATION);
        device["hardwareId"] = getDeviceProperty(hDevInfo, &devInfoData, SPDRP_HARDWAREID);
        
        // Try to get device interface path for camera and audio devices
        QString devicePath;
        if (classGuid == GUID_DEVCLASS_CAMERA) {
            // For camera devices, try multiple interface GUIDs
            devicePath = getDeviceInterfacePath(hDevInfo, &devInfoData, GUID_DEVINTERFACE_CAMERA);
            if (devicePath.isEmpty()) {
                // Try UVC interface GUID as fallback
                devicePath = getDeviceInterfacePath(hDevInfo, &devInfoData, GUID_DEVINTERFACE_UVC);
            }
            // For camera devices, if no interface path is found, we'll use a symbolic name
            // based on the friendly name which can be used by applications like Qt's QCamera
            if (devicePath.isEmpty()) {
                QString friendlyName = device["friendlyName"].toString();
                if (!friendlyName.isEmpty()) {
                    device["devicePath"] = friendlyName; // Use friendly name as device "path" for cameras
                } else {
                    device["devicePath"] = device["deviceId"].toString();
                }
            } else {
                device["devicePath"] = devicePath;
            }
        } else if (classGuid == GUID_DEVCLASS_HIDCLASS) {
            // For HID devices, use the HID interface GUID
            GUID hidGuid;
            HidD_GetHidGuid(&hidGuid);
            devicePath = getDeviceInterfacePath(hDevInfo, &devInfoData, hidGuid);
            if (!devicePath.isEmpty()) {
                device["devicePath"] = devicePath;
            } else {
                device["devicePath"] = device["deviceId"].toString();
            }
        } else if (classGuid == GUID_DEVCLASS_MEDIA) {
            // For audio devices, the device ID might be sufficient
            // Audio devices are typically accessed through Windows Audio APIs using device IDs
            device["devicePath"] = device["deviceId"].toString();
        } else {
            // For other device classes, try to get interface path with the class GUID
            devicePath = getDeviceInterfacePath(hDevInfo, &devInfoData, classGuid);
            if (!devicePath.isEmpty()) {
                device["devicePath"] = devicePath;
            } else {
                device["devicePath"] = device["deviceId"].toString();
            }
        }
        
        // Get parent device instance
        DWORD parentDevInst;
        if (CM_Get_Parent(&parentDevInst, devInfoData.DevInst, 0) == CR_SUCCESS) {
            device["parentDeviceId"] = getDeviceId(parentDevInst);
        }
        
        devices.append(device);
    }
    
    SetupDiDestroyDeviceInfoList(hDevInfo);
    return devices;
}

bool WindowsDeviceManager::isDeviceAssociatedWithPortChain(const QString& parentDeviceId, const QString& targetDeviceInstanceId, const QString& targetPortChain)
{
    if (parentDeviceId.isEmpty() || targetDeviceInstanceId.isEmpty()) {
        return false;
    }
    
    qCDebug(log_device_windows) << "      Verifying device association:";
    qCDebug(log_device_windows) << "        Parent device ID:" << parentDeviceId;
    qCDebug(log_device_windows) << "        Target device instance ID:" << targetDeviceInstanceId;
    qCDebug(log_device_windows) << "        Target port chain:" << targetPortChain;
    
    // Direct parent match
    if (parentDeviceId == targetDeviceInstanceId) {
        qCDebug(log_device_windows) << "        ✓ Direct parent match found";
        return true;
    }
    
    // Check if the parent device is in the same device hierarchy (port chain)
    // Walk up the parent device tree to find a match
    QString currentParentId = parentDeviceId;
    int depth = 0;
    const int maxDepth = 5; // Prevent infinite loops
    
    while (!currentParentId.isEmpty() && depth < maxDepth) {
        qCDebug(log_device_windows) << "        Checking hierarchy level" << depth << ":" << currentParentId;
        
        if (currentParentId == targetDeviceInstanceId) {
            qCDebug(log_device_windows) << "        ✓ Hierarchy match found at level" << depth;
            return true;
        }
        
        // Get the parent of the current parent
        DWORD parentDevInst = getDeviceInstanceFromId(currentParentId);
        if (parentDevInst == 0) {
            break;
        }
        
        DWORD grandParentDevInst;
        if (CM_Get_Parent(&grandParentDevInst, parentDevInst, 0) != CR_SUCCESS) {
            break;
        }
        
        currentParentId = getDeviceId(grandParentDevInst);
        depth++;
    }
    
    // Additional verification: check if devices share the same port chain
    if (!targetPortChain.isEmpty()) {
        DWORD parentDevInst = getDeviceInstanceFromId(parentDeviceId);
        if (parentDevInst != 0) {
            QString parentPortChain = buildPythonCompatiblePortChain(parentDevInst);
            if (parentPortChain == targetPortChain) {
                qCDebug(log_device_windows) << "        ✓ Port chain match found:" << parentPortChain;
                return true;
            }
        }
    }
    
    qCDebug(log_device_windows) << "        ✗ No association found";
    return false;
}

bool WindowsDeviceManager::verifyCameraDeviceAssociation(const QString& cameraDeviceId, const QString& targetDeviceInstanceId, const QString& targetPortChain)
{
    qCDebug(log_device_windows) << "      Verifying camera device association for:" << cameraDeviceId;
    
    // Get the camera device instance
    DWORD cameraDevInst = getDeviceInstanceFromId(cameraDeviceId);
    if (cameraDevInst == 0) {
        qCDebug(log_device_windows) << "        ✗ Could not get device instance for camera";
        return false;
    }
    
    // Get the parent device of the camera
    DWORD parentDevInst;
    if (CM_Get_Parent(&parentDevInst, cameraDevInst, 0) != CR_SUCCESS) {
        qCDebug(log_device_windows) << "        ✗ Could not get parent device for camera";
        return false;
    }
    
    QString parentDeviceId = getDeviceId(parentDevInst);
    return isDeviceAssociatedWithPortChain(parentDeviceId, targetDeviceInstanceId, targetPortChain);
}

bool WindowsDeviceManager::verifyAudioDeviceAssociation(const QString& audioDeviceId, const QString& targetDeviceInstanceId, const QString& targetPortChain)
{
    qCDebug(log_device_windows) << "      Verifying audio device association for:" << audioDeviceId;
    
    // Get the audio device instance
    DWORD audioDevInst = getDeviceInstanceFromId(audioDeviceId);
    if (audioDevInst == 0) {
        qCDebug(log_device_windows) << "        ✗ Could not get device instance for audio";
        return false;
    }
    
    // Get the parent device of the audio device
    DWORD parentDevInst;
    if (CM_Get_Parent(&parentDevInst, audioDevInst, 0) != CR_SUCCESS) {
        qCDebug(log_device_windows) << "        ✗ Could not get parent device for audio";
        return false;
    }
    
    QString parentDeviceId = getDeviceId(parentDevInst);
    return isDeviceAssociatedWithPortChain(parentDeviceId, targetDeviceInstanceId, targetPortChain);
}

DWORD WindowsDeviceManager::getDeviceInstanceFromId(const QString& deviceId)
{
    if (deviceId.isEmpty()) {
        return 0;
    }
    
    // Use CM_Locate_DevNode to get device instance from device ID
    DWORD devInst;
    std::wstring wDeviceId = deviceId.toStdWString();
    
    if (CM_Locate_DevNode(&devInst, const_cast<DEVINSTID>(wDeviceId.c_str()), CM_LOCATE_DEVNODE_NORMAL) == CR_SUCCESS) {
        return devInst;
    }
    
    return 0;
}

// ...existing code...
#endif // _WIN32
