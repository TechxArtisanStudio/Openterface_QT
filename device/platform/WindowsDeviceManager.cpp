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
extern "C" __declspec(dllimport) CONFIGRET WINAPI CM_Get_Sibling(
    PDEVINST pdnDevInstSibling,
    DEVINST dnDevInst,
    ULONG ulFlags
);

Q_LOGGING_CATEGORY(log_device_windows, "opf.host.windows")

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

    qCDebug(log_device_windows) << "Discovering Openterface devices with optimized USB 2.0/3.0 detection...";
    
    QList<DeviceInfo> devices;
    
    try {
        // Use optimized discovery that supports both USB 2.0 and USB 3.0 devices
        // This handles devices that can work with both generation methods
        devices = discoverOptimizedDevices();
        
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
                 hardwareId.contains(QString("VID_%1").arg(AbstractPlatformDeviceManager::OPENTERFACE_VID), Qt::CaseInsensitive) ||
                 hardwareId.contains(QString("VID_%1").arg(AbstractPlatformDeviceManager::OPENTERFACE_VID_V2), Qt::CaseInsensitive) ||
                 hardwareId.contains(QString("VID_%1").arg(AbstractPlatformDeviceManager::OPENTERFACE_VID_V3), Qt::CaseInsensitive)) {
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
                // Check for any supported Openterface VID/PID combinations:
                // V1: 534D:2109 (MS2109)
                // V2: 345F:2132 (MS2130S)
                // V3: 345F:2109
                bool isOpenterfaceDevice = 
                    (hardwareId.contains(AbstractPlatformDeviceManager::OPENTERFACE_VID, Qt::CaseInsensitive) &&
                     hardwareId.contains(AbstractPlatformDeviceManager::OPENTERFACE_PID, Qt::CaseInsensitive)) ||
                    (hardwareId.contains(AbstractPlatformDeviceManager::OPENTERFACE_VID_V2, Qt::CaseInsensitive) &&
                     hardwareId.contains(AbstractPlatformDeviceManager::OPENTERFACE_PID_V2, Qt::CaseInsensitive)) ||
                    (hardwareId.contains(AbstractPlatformDeviceManager::OPENTERFACE_VID_V3, Qt::CaseInsensitive) &&
                     hardwareId.contains(AbstractPlatformDeviceManager::OPENTERFACE_PID_V3, Qt::CaseInsensitive));
                
                if (isOpenterfaceDevice) {
                    QString devicePath = QString::fromWCharArray(interfaceDetail->DevicePath);
                    qCDebug(log_device_windows) << "Found HID device path:" << devicePath << "for device ID:" << deviceId;
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
    qCDebug(log_device_windows) << "Camera device ID to find:" << deviceInfo.cameraDeviceId;
    qCDebug(log_device_windows) << "Audio device ID to find:" << deviceInfo.audioDeviceId;
    
    // Enhanced camera finding: try multiple approaches
    if (!deviceInfo.cameraDeviceId.isEmpty()) {
        // Approach 1: Direct device ID to path conversion
        cameraPath = findCameraDevicePathByDeviceId(deviceInfo.cameraDeviceId);
        if (!cameraPath.isEmpty()) {
            qCDebug(log_device_windows) << "    ✓ Found camera via direct device ID lookup:" << cameraPath;
        }
    }
    
    // Approach 2: Find camera devices with parent device verification (fallback)
    if (cameraPath.isEmpty()) {
        QList<QVariantMap> cameras = enumerateDevicesByClassWithParentInfo(GUID_DEVCLASS_CAMERA);
        for (const QVariantMap& camera : cameras) {
            QString cameraDeviceId = camera.value("deviceId").toString();
            QString parentDeviceId = camera.value("parentDeviceId").toString();
            
            qCDebug(log_device_windows) << "  Checking camera device:" << cameraDeviceId;
            qCDebug(log_device_windows) << "    Parent device ID:" << parentDeviceId;
            
            // Check if this camera matches our hardware identifiers with valid VID:PID combinations
            // Must be one of: 345F:2109, 345F:2132, or 534D:2109
            bool isValidCameraVidPid = 
                (cameraDeviceId.contains("345F", Qt::CaseInsensitive) && 
                 (cameraDeviceId.contains("2109", Qt::CaseInsensitive) || 
                  cameraDeviceId.contains("2132", Qt::CaseInsensitive))) ||
                (cameraDeviceId.contains("534D", Qt::CaseInsensitive) && 
                 cameraDeviceId.contains("2109", Qt::CaseInsensitive));
            
            if (isValidCameraVidPid) {
                
                // Enhanced verification: try multiple association methods
                if (isDeviceAssociatedWithPortChain(parentDeviceId, deviceInfo.deviceInstanceId, deviceInfo.portChain) ||
                    isDeviceRelatedToPortChain(cameraDeviceId, deviceInfo.portChain) ||
                    (!deviceInfo.cameraDeviceId.isEmpty() && cameraDeviceId.contains(deviceInfo.cameraDeviceId))) {
                    cameraPath = camera.value("devicePath").toString();
                    qCDebug(log_device_windows) << "    ✓ Found matching camera device via enhanced verification:" << cameraPath;
                    qCDebug(log_device_windows) << "      Association verified for port chain:" << deviceInfo.portChain;
                    break;
                } else {
                    qCDebug(log_device_windows) << "    ✗ Camera device verification failed - trying next";
                }
            } else {
                qCDebug(log_device_windows) << "    ✗ Camera device does not have valid VID:PID (must be 345F:2109, 345F:2132, or 534D:2109)";
            }
        }
    }
    
    // Enhanced audio finding: try multiple approaches
    if (!deviceInfo.audioDeviceId.isEmpty()) {
        // Approach 1: Direct device ID to path conversion
        audioPath = findAudioDevicePathByDeviceId(deviceInfo.audioDeviceId);
        if (!audioPath.isEmpty()) {
            qCDebug(log_device_windows) << "    ✓ Found audio via direct device ID lookup:" << audioPath;
        }
    }
    
    // Approach 2: Find audio devices with parent device verification (fallback)
    if (audioPath.isEmpty()) {
        QList<QVariantMap> audioDevices = enumerateDevicesByClassWithParentInfo(GUID_DEVCLASS_MEDIA);
        for (const QVariantMap& audio : audioDevices) {
            QString audioDeviceId = audio.value("deviceId").toString();
            QString parentDeviceId = audio.value("parentDeviceId").toString();
            
            qCDebug(log_device_windows) << "  Checking audio device:" << audioDeviceId;
            qCDebug(log_device_windows) << "    Parent device ID:" << parentDeviceId;
            
            // Check if this audio device matches our hardware identifiers
            if (audioDeviceId.contains("345F", Qt::CaseInsensitive) ||
                audioDeviceId.contains("534D", Qt::CaseInsensitive)) {
                
                // Enhanced verification: try multiple association methods
                if (isDeviceAssociatedWithPortChain(parentDeviceId, deviceInfo.deviceInstanceId, deviceInfo.portChain) ||
                    isDeviceRelatedToPortChain(audioDeviceId, deviceInfo.portChain) ||
                    (!deviceInfo.audioDeviceId.isEmpty() && audioDeviceId.contains(deviceInfo.audioDeviceId))) {
                    audioPath = audio.value("devicePath").toString();
                    qCDebug(log_device_windows) << "    ✓ Found matching audio device via enhanced verification:" << audioPath;
                    qCDebug(log_device_windows) << "      Association verified for port chain:" << deviceInfo.portChain;
                    break;
                } else {
                    qCDebug(log_device_windows) << "    ✗ Audio device verification failed - trying next";
                }
            }
        }
    }
    
    qCDebug(log_device_windows) << "=== Camera/audio search complete ===";
    qCDebug(log_device_windows) << "Final camera path:" << cameraPath;
    qCDebug(log_device_windows) << "Final audio path:" << audioPath;
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
    qCDebug(log_device_windows) << "=== Converting device IDs to real paths (Generation 1) ===";
    
    // Convert serial port device ID to COM port path using location-based approach
    if (!deviceInfo.serialPortId.isEmpty()) {
        // For Generation 1, try location-based search first since serial port 
        // is connected to the same port chain as the Openterface device
        QString comPort = findComPortByLocation(deviceInfo.portChain);
        if (!comPort.isEmpty()) {
            deviceInfo.serialPortPath = comPort;
            qCDebug(log_device_windows) << "  ✓ Serial Port (by location):" << deviceInfo.serialPortPath;
        } else {
            // Fallback to device ID search
            comPort = findComPortByDeviceId(deviceInfo.serialPortId);
            if (!comPort.isEmpty()) {
                deviceInfo.serialPortPath = comPort;
                qCDebug(log_device_windows) << "  ✓ Serial Port (by device ID):" << deviceInfo.serialPortPath;
            } else {
                qCDebug(log_device_windows) << "  ✗ Could not find COM port for device ID:" << deviceInfo.serialPortId << "or location:" << deviceInfo.portChain;
            }
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
        qCDebug(log_device_windows) << "HID Device:" << deviceInfo.hidDevicePath;
    }
    
    // Convert camera and audio device IDs to actual device paths
    if (!deviceInfo.cameraDeviceId.isEmpty() || !deviceInfo.audioDeviceId.isEmpty()) {
        QPair<QString, QString> paths = findCameraAudioByDeviceInfo(deviceInfo);
        
        if (!paths.first.isEmpty()) {
            deviceInfo.cameraDevicePath = paths.first;
            qCDebug(log_device_windows) << "Camera Device:" << deviceInfo.cameraDevicePath;
        } else {
            qCDebug(log_device_windows) << "Could not find camera path for:" << deviceInfo.cameraDeviceId;
        }
        
        if (!paths.second.isEmpty()) {
            deviceInfo.audioDevicePath = paths.second;
            qCDebug(log_device_windows) << "Audio Device:" << deviceInfo.audioDevicePath;
        } else {
            qCDebug(log_device_windows) << "Could not find audio path for:" << deviceInfo.audioDeviceId;
        }
    }
    
    qCDebug(log_device_windows) << "=== End path conversion ===";
}

void WindowsDeviceManager::matchDevicePathsToRealPathsGeneration2(DeviceInfo& deviceInfo)
{
    qCDebug(log_device_windows) << "=== Converting Generation 2 device IDs to real paths ===";
    
    // For Generation 2, the serial port has already been found by findSerialPortByCompanionDevice
    // and should have a different approach since it's not directly connected to the same port chain
    qCDebug(log_device_windows) << "Finding COM port for Generation 2 device ID:" << deviceInfo.serialPortId;
    if (!deviceInfo.serialPortId.isEmpty()) {
        // The serial port ID was found through companion device association
        // We need to find the actual COM port using the device ID
        QString comPort = findComPortByDeviceId(deviceInfo.serialPortId);
        if (!comPort.isEmpty()) {
            deviceInfo.serialPortPath = comPort;
            qCDebug(log_device_windows) << "Generation 2 Serial Port:" << deviceInfo.serialPortPath;
        } else {
            qCDebug(log_device_windows) << "Could not find COM port for Generation 2 device ID:" << deviceInfo.serialPortId;
        }
    }
    
    // For HID, camera, and audio devices, the logic is the same as Generation 1
    // since they are direct children of the companion device
    
    // HID device path matching
    if (!deviceInfo.hidDeviceId.isEmpty()) {
        deviceInfo.hidDevicePath = findHIDByDeviceId(deviceInfo.hidDeviceId);
        if (deviceInfo.hidDevicePath.isEmpty()) {
            deviceInfo.hidDevicePath = deviceInfo.hidDeviceId; // Fallback
        }
        qCDebug(log_device_windows) << "Generation 2 HID Device:" << deviceInfo.hidDevicePath;
    }
    
    // Camera and audio device path matching using the companion device's port chain
    if (!deviceInfo.cameraDeviceId.isEmpty() || !deviceInfo.audioDeviceId.isEmpty()) {
        QPair<QString, QString> paths = findCameraAudioByDeviceInfo(deviceInfo);
        
        if (!paths.first.isEmpty()) {
            deviceInfo.cameraDevicePath = paths.first;
            qCDebug(log_device_windows) << "Generation 2 Camera Device:" << deviceInfo.cameraDevicePath;
        } else if (!deviceInfo.cameraDeviceId.isEmpty()) {
            qCDebug(log_device_windows) << "Could not find Generation 2 camera path for:" << deviceInfo.cameraDeviceId;
        }
        
        if (!paths.second.isEmpty()) {
            deviceInfo.audioDevicePath = paths.second;
            qCDebug(log_device_windows) << "Generation 2 Audio Device:" << deviceInfo.audioDevicePath;
        } else if (!deviceInfo.audioDeviceId.isEmpty()) {
            qCDebug(log_device_windows) << "Could not find Generation 2 audio path for:" << deviceInfo.audioDeviceId;
        }
    }
    
    qCDebug(log_device_windows) << "=== End Generation 2 path conversion ===";
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

QList<DeviceInfo> WindowsDeviceManager::discoverGeneration1Devices()
{
    QList<DeviceInfo> devices;
    
    qCDebug(log_device_windows) << "Discovering Generation 1 devices (Original VID/PID approach)...";
    
    // Use Python-compatible approach: Find USB devices with HID VID/PID first
    QList<USBDeviceData> usbDevices = findUSBDevicesWithVidPid(AbstractPlatformDeviceManager::OPENTERFACE_VID, AbstractPlatformDeviceManager::OPENTERFACE_PID);
    qCDebug(log_device_windows) << "Found" << usbDevices.size() << "USB devices with VID/PID" << AbstractPlatformDeviceManager::OPENTERFACE_VID << "/" << AbstractPlatformDeviceManager::OPENTERFACE_PID;
    
    // Process each USB device found
    for (int i = 0; i < usbDevices.size(); ++i) {
        const USBDeviceData& usbDevice = usbDevices[i];
        
        qCDebug(log_device_windows) << "=== Processing Gen1 Device" << (i + 1) << "===";
        qCDebug(log_device_windows) << "Port Chain:" << usbDevice.portChain;
        qCDebug(log_device_windows) << "Device Instance ID:" << usbDevice.deviceInstanceId;
        
        DeviceInfo deviceInfo;
        deviceInfo.portChain = usbDevice.portChain;
        deviceInfo.deviceInstanceId = usbDevice.deviceInstanceId;
        deviceInfo.vid = AbstractPlatformDeviceManager::OPENTERFACE_VID;
        deviceInfo.pid = AbstractPlatformDeviceManager::OPENTERFACE_PID;
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
        qCDebug(log_device_windows) << "Gen1 Device" << (i + 1) << "processing complete";
    }
    
    return devices;
}

QList<DeviceInfo> WindowsDeviceManager::discoverGeneration2Devices()
{
    QList<DeviceInfo> devices;
    
    qCDebug(log_device_windows) << "Discovering Generation 2 devices (Companion device approach)...";
    
    // Find companion devices with the new VID/PID first (345F:2130)
    // These are the Openterface devices that integrate camera, audio, and HID
    QList<USBDeviceData> companionDevices = findUSBDevicesWithVidPid(AbstractPlatformDeviceManager::OPENTERFACE_VID_V2, AbstractPlatformDeviceManager::OPENTERFACE_PID_V2);
    qCDebug(log_device_windows) << "Found" << companionDevices.size() << "companion Openterface devices with VID/PID" << AbstractPlatformDeviceManager::OPENTERFACE_VID_V2 << "/" << AbstractPlatformDeviceManager::OPENTERFACE_PID_V2;
    
    for (int i = 0; i < companionDevices.size(); ++i) {
        const USBDeviceData& companionDevice = companionDevices[i];
        
        qCDebug(log_device_windows) << "=== Processing Gen2 Companion Device" << (i + 1) << "===";
        qCDebug(log_device_windows) << "Companion Port Chain:" << companionDevice.portChain;
        qCDebug(log_device_windows) << "Companion Device Instance ID:" << companionDevice.deviceInstanceId;
        
        DeviceInfo deviceInfo;
        deviceInfo.portChain = companionDevice.portChain;
        deviceInfo.deviceInstanceId = companionDevice.deviceInstanceId;
        deviceInfo.vid = AbstractPlatformDeviceManager::OPENTERFACE_VID_V2;
        deviceInfo.pid = AbstractPlatformDeviceManager::OPENTERFACE_PID_V2;
        deviceInfo.lastSeen = QDateTime::currentDateTime();
        deviceInfo.platformSpecific = companionDevice.deviceInfo;
        
        // Process children to find HID, camera, and audio devices from the companion device
        qCDebug(log_device_windows) << "Processing" << companionDevice.children.size() << "companion child devices...";
        for (const QVariantMap& child : companionDevice.children) {
            QString hardwareId = child["hardwareId"].toString();
            QString deviceId = child["deviceId"].toString();
            
            qCDebug(log_device_windows) << "  Companion Child Hardware ID:" << hardwareId;
            qCDebug(log_device_windows) << "  Companion Child Device ID:" << deviceId;
            
            // Skip interface endpoints we don't need
            if (deviceId.contains("&0002") || deviceId.contains("&0004")) {
                qCDebug(log_device_windows) << "    Skipping interface endpoint";
                continue;
            }
            
            // Check for HID device (MI_04 interface)
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
        
        // For Generation 2, the serial port is located under a General-purpose USB hub
        // and the Openterface device is a Companion device of that hub
        // We need to find the serial port using the hub relationship and CompanionPortNumber
        QString serialPortId = findSerialPortByCompanionDevice(companionDevice);
        if (!serialPortId.isEmpty()) {
            deviceInfo.serialPortId = serialPortId;
            qCDebug(log_device_windows) << "  ✓ Found associated serial port device ID:" << serialPortId;
        } else {
            qCDebug(log_device_windows) << "  ✗ Could not find associated serial port for companion device";
        }
        
        matchDevicePaths(deviceInfo);
        
        // Convert device IDs to actual device paths using Generation 2 logic
        matchDevicePathsToRealPathsGeneration2(deviceInfo);
        
        devices.append(deviceInfo);
        qCDebug(log_device_windows) << "Gen2 Device" << (i + 1) << "processing complete";
        qCDebug(log_device_windows) << "  Final device summary:";
        qCDebug(log_device_windows) << "    Serial:" << (deviceInfo.hasSerialPort() ? deviceInfo.serialPortPath : "None");
        qCDebug(log_device_windows) << "    HID:" << (deviceInfo.hasHidDevice() ? "Available" : "None");
        qCDebug(log_device_windows) << "    Camera:" << (deviceInfo.hasCameraDevice() ? "Available" : "None");
        qCDebug(log_device_windows) << "    Audio:" << (deviceInfo.hasAudioDevice() ? "Available" : "None");
    }
    
    return devices;
}

QList<DeviceInfo> WindowsDeviceManager::discoverOptimizedDevices()
{
    QList<DeviceInfo> devices;
    QMap<QString, DeviceInfo> deviceMap; // Use port chain as key to prevent duplicates
    
    qCDebug(log_device_windows) << "Starting optimized device discovery for USB 2.0/3.0 compatibility...";
    qCDebug(log_device_windows) << "Looking for Original generation: 1A86:7523 (USB 2.0/3.0 with Gen1 method)";
    qCDebug(log_device_windows) << "Looking for New generation USB 2.0: 1A86:CH32V208 (USB 2.0 with Gen1 method)";
    qCDebug(log_device_windows) << "Looking for New generation USB 3.0: 345F:2132 (USB 3.0 with Gen2 method)";
    qCDebug(log_device_windows) << "Looking for V3 generation USB 3.0: 345F:2109 (USB 3.0 with Gen2 method)";
    
    // Phase 1: Search for Original generation devices (VID_1A86&PID_7523)
    // These work on both USB 2.0 and 3.0 using integrated approach (Generation 1 method)
    qCDebug(log_device_windows) << "=== Phase 1: Searching for Original generation devices (1A86:7523) ===";
    QList<USBDeviceData> originalDevices = findUSBDevicesWithVidPid(AbstractPlatformDeviceManager::SERIAL_VID, AbstractPlatformDeviceManager::SERIAL_PID);
    qCDebug(log_device_windows) << "Found" << originalDevices.size() << "Original generation devices";
    
    for (int i = 0; i < originalDevices.size(); ++i) {
        const USBDeviceData& originalDevice = originalDevices[i];
        
        qCDebug(log_device_windows) << "Processing Original Device" << (i + 1) << "at port chain:" << originalDevice.portChain;
        
        DeviceInfo deviceInfo;
        deviceInfo.portChain = originalDevice.portChain;
        deviceInfo.deviceInstanceId = originalDevice.deviceInstanceId;
        deviceInfo.vid = AbstractPlatformDeviceManager::SERIAL_VID;
        deviceInfo.pid = AbstractPlatformDeviceManager::SERIAL_PID;
        deviceInfo.lastSeen = QDateTime::currentDateTime();
        deviceInfo.platformSpecific = originalDevice.deviceInfo;
        
        // Convert sibling and children lists to QVariantList for storage
        QVariantList siblingVariants, childrenVariants;
        for (const QVariantMap& sibling : originalDevice.siblings) {
            siblingVariants.append(sibling);
        }
        for (const QVariantMap& child : originalDevice.children) {
            childrenVariants.append(child);
        }
        deviceInfo.platformSpecific["siblings"] = siblingVariants;
        deviceInfo.platformSpecific["children"] = childrenVariants;
        
        // Process as Generation 1 device (integrated interfaces)
        processGeneration1Interfaces(deviceInfo, originalDevice);
        
        // Convert device IDs to real paths
        matchDevicePathsToRealPaths(deviceInfo);
        
        // Add to device map
        deviceMap[deviceInfo.portChain] = deviceInfo;
        qCDebug(log_device_windows) << "Original generation device added with port chain:" << deviceInfo.portChain;
    }
    
    // Phase 2: Search for New generation USB 2.0 devices (VID_1A86&PID_CH32V208)
    // These behave like original generation when on USB 2.0 (use Generation 1 method)
    qCDebug(log_device_windows) << "=== Phase 2: Searching for New generation USB 2.0 devices (1A86:CH32V208) ===";
    QList<USBDeviceData> newGen2Devices = findUSBDevicesWithVidPid(AbstractPlatformDeviceManager::SERIAL_VID_V2, AbstractPlatformDeviceManager::SERIAL_PID_V2);
    qCDebug(log_device_windows) << "Found" << newGen2Devices.size() << "New generation USB 2.0 devices";
    
    for (int i = 0; i < newGen2Devices.size(); ++i) {
        const USBDeviceData& newGen2Device = newGen2Devices[i];
        
        // Check if we already have a device at this port chain
        if (deviceMap.contains(newGen2Device.portChain)) {
            qCDebug(log_device_windows) << "Skipping New Gen USB 2.0 device at port chain" << newGen2Device.portChain << "- already found";
            continue;
        }
        
        qCDebug(log_device_windows) << "Processing New Gen USB 2.0 Device" << (i + 1) << "at port chain:" << newGen2Device.portChain;
        
        DeviceInfo deviceInfo;
        deviceInfo.portChain = newGen2Device.portChain;
        deviceInfo.deviceInstanceId = newGen2Device.deviceInstanceId;
        deviceInfo.vid = AbstractPlatformDeviceManager::SERIAL_VID_V2;
        deviceInfo.pid = AbstractPlatformDeviceManager::SERIAL_PID_V2;
        deviceInfo.lastSeen = QDateTime::currentDateTime();
        deviceInfo.platformSpecific = newGen2Device.deviceInfo;
        
        // Convert sibling and children lists to QVariantList for storage
        QVariantList siblingVariants, childrenVariants;
        for (const QVariantMap& sibling : newGen2Device.siblings) {
            siblingVariants.append(sibling);
        }
        for (const QVariantMap& child : newGen2Device.children) {
            childrenVariants.append(child);
        }
        deviceInfo.platformSpecific["siblings"] = siblingVariants;
        deviceInfo.platformSpecific["children"] = childrenVariants;
        
        // Process as Generation 1 device (integrated interfaces on USB 2.0)
        processGeneration1Interfaces(deviceInfo, newGen2Device);
        
        // Convert device IDs to real paths
        matchDevicePathsToRealPaths(deviceInfo);
        
        // Add to device map
        deviceMap[deviceInfo.portChain] = deviceInfo;
        qCDebug(log_device_windows) << "New generation USB 2.0 device added with port chain:" << deviceInfo.portChain;
    }
    
    // Phase 3: Search for New generation USB 3.0 integrated devices (VID_345F&PID_2132)  
    // These are integrated devices that contain camera, HID, and audio interfaces
    // The serial port is separate (1A86:CH32V208) and connected via CompanionPortChain
    qCDebug(log_device_windows) << "=== Phase 3: Searching for New generation USB 3.0 integrated devices (345F:2132) ===";
    QList<USBDeviceData> integratedDevices = findUSBDevicesWithVidPid(AbstractPlatformDeviceManager::OPENTERFACE_VID_V2, AbstractPlatformDeviceManager::OPENTERFACE_PID_V2);
    qCDebug(log_device_windows) << "Found" << integratedDevices.size() << "integrated devices (345F:2132)";
    
    for (int i = 0; i < integratedDevices.size(); ++i) {
        const USBDeviceData& integratedDevice = integratedDevices[i];
        
        qCDebug(log_device_windows) << "Processing Integrated Device" << (i + 1) << "at port chain:" << integratedDevice.portChain;
        qCDebug(log_device_windows) << "  Device Instance ID:" << integratedDevice.deviceInstanceId;
        
        // For integrated devices, we need to:
        // 1. Find the associated serial port using CompanionPortChain relationship
        // 2. Use the integrated device's children for camera, HID, and audio
        
        // Find associated serial port through CompanionPortChain
        QString associatedSerialPortId = findSerialPortByIntegratedDevice(integratedDevice);
        QString associatedPortChain;
        
        if (!associatedSerialPortId.isEmpty()) {
            qCDebug(log_device_windows) << "  ✓ Found associated serial port:" << associatedSerialPortId;
            // Get the port chain of the serial port to use as the device identifier
            DWORD serialDevInst = getDeviceInstanceFromId(associatedSerialPortId);
            if (serialDevInst != 0) {
                associatedPortChain = buildPythonCompatiblePortChain(serialDevInst);
                qCDebug(log_device_windows) << "  Associated serial port chain:" << associatedPortChain;
            }
        } else {
            qCWarning(log_device_windows) << "  ✗ No associated serial port found for integrated device";
            // Use the integrated device's port chain as fallback
            associatedPortChain = integratedDevice.portChain;
        }
        
        // Check if we already have a device for this port chain
        if (deviceMap.contains(associatedPortChain)) {
            qCDebug(log_device_windows) << "Enhancing existing device with integrated device interfaces at port chain:" << associatedPortChain;
            DeviceInfo& existingDevice = deviceMap[associatedPortChain];
            
            // Add integrated device interfaces to existing device
            processIntegratedDeviceInterfaces(existingDevice, integratedDevice);
            matchDevicePathsToRealPathsGeneration2(existingDevice);
            
            qCDebug(log_device_windows) << "Enhanced device interfaces - HID:" << (existingDevice.hasHidDevice() ? "✓" : "✗")
                                       << "Camera:" << (existingDevice.hasCameraDevice() ? "✓" : "✗") 
                                       << "Audio:" << (existingDevice.hasAudioDevice() ? "✓" : "✗");
            continue;
        }
        
        // Create new device info
        DeviceInfo deviceInfo;
        deviceInfo.portChain = associatedPortChain;
        deviceInfo.deviceInstanceId = integratedDevice.deviceInstanceId;
        deviceInfo.vid = AbstractPlatformDeviceManager::OPENTERFACE_VID_V2;
        deviceInfo.pid = AbstractPlatformDeviceManager::OPENTERFACE_PID_V2;
        deviceInfo.lastSeen = QDateTime::currentDateTime();
        deviceInfo.platformSpecific = integratedDevice.deviceInfo;
        
        // For USB 3.0 integrated devices, set up companion PortChain association
        // The serial port is on the main PortChain, composite devices on companionPortChain
        if (associatedPortChain != integratedDevice.portChain) {
            deviceInfo.companionPortChain = integratedDevice.portChain;
            deviceInfo.hasCompanionDevice = true;
            qCDebug(log_device_windows) << "  USB 3.0 device - Serial PortChain:" << associatedPortChain 
                                       << "Companion PortChain:" << integratedDevice.portChain;
        }
        
        // Set the serial port information if found
        if (!associatedSerialPortId.isEmpty()) {
            deviceInfo.serialPortId = associatedSerialPortId;
        }
        
        // Convert sibling and children lists to QVariantList for storage
        QVariantList siblingVariants, childrenVariants;
        for (const QVariantMap& sibling : integratedDevice.siblings) {
            siblingVariants.append(sibling);
        }
        for (const QVariantMap& child : integratedDevice.children) {
            childrenVariants.append(child);
        }
        deviceInfo.platformSpecific["siblings"] = siblingVariants;
        deviceInfo.platformSpecific["children"] = childrenVariants;
        
        // Process the integrated device interfaces (camera, HID, audio)
        qCDebug(log_device_windows) << "Processing interfaces for integrated device (345F:2132)";
        qCDebug(log_device_windows) << "  Integrated device has" << integratedDevice.children.size() << "children and" << integratedDevice.siblings.size() << "siblings";
        processIntegratedDeviceInterfaces(deviceInfo, integratedDevice);
        
        qCDebug(log_device_windows) << "After integrated device interface processing:";
        qCDebug(log_device_windows) << "  HID Device ID:" << (deviceInfo.hasHidDevice() ? deviceInfo.hidDeviceId : "Not found");
        qCDebug(log_device_windows) << "  Camera Device ID:" << (deviceInfo.hasCameraDevice() ? deviceInfo.cameraDeviceId : "Not found");
        qCDebug(log_device_windows) << "  Audio Device ID:" << (deviceInfo.hasAudioDevice() ? deviceInfo.audioDeviceId : "Not found");
        
        // Convert device IDs to real paths
        matchDevicePathsToRealPathsGeneration2(deviceInfo);
        
        // Add to device map
        deviceMap[deviceInfo.portChain] = deviceInfo;
        qCDebug(log_device_windows) << "Integrated device added with port chain:" << deviceInfo.portChain;
    }
    
    // Phase 3B: Search for V3 generation USB 3.0 integrated devices (VID_345F&PID_2109)  
    // These are integrated devices that contain camera, HID, and audio interfaces
    // The serial port is separate (1A86:CH32V208) and connected via CompanionPortChain
    qCDebug(log_device_windows) << "=== Phase 3B: Searching for V3 generation USB 3.0 integrated devices (345F:2109) ===";
    QList<USBDeviceData> v3IntegratedDevices = findUSBDevicesWithVidPid(AbstractPlatformDeviceManager::OPENTERFACE_VID_V3, AbstractPlatformDeviceManager::OPENTERFACE_PID_V3);
    qCDebug(log_device_windows) << "Found" << v3IntegratedDevices.size() << "V3 integrated devices (345F:2109)";
    
    for (int i = 0; i < v3IntegratedDevices.size(); ++i) {
        const USBDeviceData& integratedDevice = v3IntegratedDevices[i];
        
        qCDebug(log_device_windows) << "Processing V3 Integrated Device" << (i + 1) << "at port chain:" << integratedDevice.portChain;
        qCDebug(log_device_windows) << "  Device Instance ID:" << integratedDevice.deviceInstanceId;
        
        // For integrated devices, we need to:
        // 1. Find the associated serial port using CompanionPortChain relationship
        // 2. Use the integrated device's children for camera, HID, and audio
        
        // Find associated serial port through CompanionPortChain
        QString associatedSerialPortId = findSerialPortByIntegratedDevice(integratedDevice);
        QString associatedPortChain;
        
        if (!associatedSerialPortId.isEmpty()) {
            qCDebug(log_device_windows) << "  ✓ Found associated serial port:" << associatedSerialPortId;
            // Get the port chain of the serial port to use as the device identifier
            DWORD serialDevInst = getDeviceInstanceFromId(associatedSerialPortId);
            if (serialDevInst != 0) {
                associatedPortChain = buildPythonCompatiblePortChain(serialDevInst);
                qCDebug(log_device_windows) << "  Associated serial port chain:" << associatedPortChain;
            }
        } else {
            qCWarning(log_device_windows) << "  ✗ No associated serial port found for V3 integrated device";
            // Use the integrated device's port chain as fallback
            associatedPortChain = integratedDevice.portChain;
        }
        
        // Check if we already have a device for this port chain
        if (deviceMap.contains(associatedPortChain)) {
            qCDebug(log_device_windows) << "Enhancing existing device with V3 integrated device interfaces at port chain:" << associatedPortChain;
            DeviceInfo& existingDevice = deviceMap[associatedPortChain];
            
            // Add integrated device interfaces to existing device
            processIntegratedDeviceInterfaces(existingDevice, integratedDevice);
            matchDevicePathsToRealPathsGeneration2(existingDevice);
            
            qCDebug(log_device_windows) << "Enhanced device interfaces - HID:" << (existingDevice.hasHidDevice() ? "✓" : "✗")
                                       << "Camera:" << (existingDevice.hasCameraDevice() ? "✓" : "✗") 
                                       << "Audio:" << (existingDevice.hasAudioDevice() ? "✓" : "✗");
            continue;
        }
        
        // Create new device info
        DeviceInfo deviceInfo;
        deviceInfo.portChain = associatedPortChain;
        deviceInfo.deviceInstanceId = integratedDevice.deviceInstanceId;
        deviceInfo.vid = AbstractPlatformDeviceManager::OPENTERFACE_VID_V3;
        deviceInfo.pid = AbstractPlatformDeviceManager::OPENTERFACE_PID_V3;
        deviceInfo.lastSeen = QDateTime::currentDateTime();
        deviceInfo.platformSpecific = integratedDevice.deviceInfo;
        
        // For USB 3.0 integrated devices, set up companion PortChain association
        // The serial port is on the main PortChain, composite devices on companionPortChain
        if (associatedPortChain != integratedDevice.portChain) {
            deviceInfo.companionPortChain = integratedDevice.portChain;
            deviceInfo.hasCompanionDevice = true;
            qCDebug(log_device_windows) << "  V3 USB 3.0 device - Serial PortChain:" << associatedPortChain 
                                       << "Companion PortChain:" << integratedDevice.portChain;
        }
        
        // Set the serial port information if found
        if (!associatedSerialPortId.isEmpty()) {
            deviceInfo.serialPortId = associatedSerialPortId;
        }
        
        // Convert sibling and children lists to QVariantList for storage
        QVariantList siblingVariants, childrenVariants;
        for (const QVariantMap& sibling : integratedDevice.siblings) {
            siblingVariants.append(sibling);
        }
        for (const QVariantMap& child : integratedDevice.children) {
            childrenVariants.append(child);
        }
        deviceInfo.platformSpecific["siblings"] = siblingVariants;
        deviceInfo.platformSpecific["children"] = childrenVariants;
        
        // Process the integrated device interfaces (camera, HID, audio)
        qCDebug(log_device_windows) << "Processing interfaces for V3 integrated device (345F:2109)";
        qCDebug(log_device_windows) << "  V3 Integrated device has" << integratedDevice.children.size() << "children and" << integratedDevice.siblings.size() << "siblings";
        processIntegratedDeviceInterfaces(deviceInfo, integratedDevice);
        
        qCDebug(log_device_windows) << "After V3 integrated device interface processing:";
        qCDebug(log_device_windows) << "  HID Device ID:" << (deviceInfo.hasHidDevice() ? deviceInfo.hidDeviceId : "Not found");
        qCDebug(log_device_windows) << "  Camera Device ID:" << (deviceInfo.hasCameraDevice() ? deviceInfo.cameraDeviceId : "Not found");
        qCDebug(log_device_windows) << "  Audio Device ID:" << (deviceInfo.hasAudioDevice() ? deviceInfo.audioDeviceId : "Not found");
        
        // Convert device IDs to real paths
        matchDevicePathsToRealPathsGeneration2(deviceInfo);
        
        // Add to device map
        deviceMap[deviceInfo.portChain] = deviceInfo;
        qCDebug(log_device_windows) << "V3 Integrated device added with port chain:" << deviceInfo.portChain;
    }
    
    // Phase 1B: Use Generation 1 approach for integrated devices (345F:2132) 
    // This handles cases where integrated devices appear with Gen1-style interface layout
    qCDebug(log_device_windows) << "Phase 1B: Generation 1 approach for integrated devices";
    QList<USBDeviceData> integratedDevicesLegacy = findUSBDevicesWithVidPid(AbstractPlatformDeviceManager::OPENTERFACE_VID_V2, AbstractPlatformDeviceManager::OPENTERFACE_PID_V2);
    qCDebug(log_device_windows) << "Re-processing" << integratedDevicesLegacy.size() << "integrated devices with legacy approach";
    
    for (int i = 0; i < integratedDevicesLegacy.size(); ++i) {
        const USBDeviceData& integratedDevice = integratedDevicesLegacy[i];
        
        // For integrated devices, we need to find the associated serial port first
        QString associatedSerialPortId = findSerialPortByIntegratedDevice(integratedDevice);
        QString associatedPortChain;
        
        if (!associatedSerialPortId.isEmpty()) {
            DWORD serialDevInst = getDeviceInstanceFromId(associatedSerialPortId);
            if (serialDevInst != 0) {
                associatedPortChain = buildPythonCompatiblePortChain(serialDevInst);
            }
        } else {
            associatedPortChain = integratedDevice.portChain;
        }
        
        // Check if we already have this device from the integrated approach
        if (deviceMap.contains(associatedPortChain)) {
            DeviceInfo& existingDevice = deviceMap[associatedPortChain];
            qCDebug(log_device_windows) << "Enhancing existing integrated device at port chain:" << associatedPortChain;
            
            // Try to fill missing interfaces using Generation 1 approach
            if (!existingDevice.hasSerialPort()) {
                processGeneration1SerialInterface(existingDevice, integratedDevice);
            }
            if (!existingDevice.hasHidDevice() || !existingDevice.hasCameraDevice() || !existingDevice.hasAudioDevice()) {
                processGeneration1MediaInterfaces(existingDevice, integratedDevice);
            }
            
            // Re-convert device IDs to real paths in case new interfaces were found
            matchDevicePathsToRealPaths(existingDevice);
        }
    }
    
    // Phase 1C: Use Generation 1 approach for V3 integrated devices (345F:2109) 
    // This handles cases where V3 integrated devices appear with Gen1-style interface layout
    qCDebug(log_device_windows) << "Phase 1C: Generation 1 approach for V3 integrated devices";
    QList<USBDeviceData> v3IntegratedDevicesLegacy = findUSBDevicesWithVidPid(AbstractPlatformDeviceManager::OPENTERFACE_VID_V3, AbstractPlatformDeviceManager::OPENTERFACE_PID_V3);
    qCDebug(log_device_windows) << "Re-processing" << v3IntegratedDevicesLegacy.size() << "V3 integrated devices with legacy approach";
    
    for (int i = 0; i < v3IntegratedDevicesLegacy.size(); ++i) {
        const USBDeviceData& integratedDevice = v3IntegratedDevicesLegacy[i];
        
        // For integrated devices, we need to find the associated serial port first
        QString associatedSerialPortId = findSerialPortByIntegratedDevice(integratedDevice);
        QString associatedPortChain;
        
        if (!associatedSerialPortId.isEmpty()) {
            DWORD serialDevInst = getDeviceInstanceFromId(associatedSerialPortId);
            if (serialDevInst != 0) {
                associatedPortChain = buildPythonCompatiblePortChain(serialDevInst);
            }
        } else {
            associatedPortChain = integratedDevice.portChain;
        }
        
        // Check if we already have this device from the integrated approach
        if (deviceMap.contains(associatedPortChain)) {
            DeviceInfo& existingDevice = deviceMap[associatedPortChain];
            qCDebug(log_device_windows) << "Enhancing existing V3 integrated device at port chain:" << associatedPortChain;
            
            // Try to fill missing interfaces using Generation 1 approach
            if (!existingDevice.hasSerialPort()) {
                processGeneration1SerialInterface(existingDevice, integratedDevice);
            }
            if (!existingDevice.hasHidDevice() || !existingDevice.hasCameraDevice() || !existingDevice.hasAudioDevice()) {
                processGeneration1MediaInterfaces(existingDevice, integratedDevice);
            }
            
            // Re-convert device IDs to real paths in case new interfaces were found
            matchDevicePathsToRealPaths(existingDevice);
        }
    }
    
    // Phase 4: Enhanced detection for devices that might work with both approaches
    qCDebug(log_device_windows) << "=== Phase 4: Enhanced detection for hybrid devices ===";
    enhanceDeviceDetection(deviceMap);
    
    // Convert map to list
    for (auto it = deviceMap.begin(); it != deviceMap.end(); ++it) {
        devices.append(it.value());
    }
    
    qCDebug(log_device_windows) << "=== Optimized Discovery Summary ===";
    qCDebug(log_device_windows) << "Total unique devices found:" << devices.size();
    for (int i = 0; i < devices.size(); ++i) {
        const DeviceInfo& device = devices[i];
        qCDebug(log_device_windows) << "Device" << (i + 1) << ":";
        qCDebug(log_device_windows) << "  Port Chain:" << device.portChain;
        qCDebug(log_device_windows) << "  Interfaces:" << device.getInterfaceSummary();
        qCDebug(log_device_windows) << "  Complete:" << (device.isCompleteDevice() ? "Yes" : "No");
    }
    
    return devices;
}

void WindowsDeviceManager::processGeneration2Interfaces(DeviceInfo& deviceInfo, const USBDeviceData& gen2Device)
{
    qCDebug(log_device_windows) << "Processing Generation 2 interfaces for device:" << deviceInfo.portChain;
    qCDebug(log_device_windows) << "Using CompanionPortChain approach for USB 3.0 device discovery";
    
    // For USB 3.0 Generation 2 devices, we need to:
    // 1. Find the CompanionPortChain (parent hub device)
    // 2. Check the child devices of that CompanionPortChain to find camera, HID, and audio
    
    // First, try direct children approach (fallback)
    qCDebug(log_device_windows) << "Checking direct children of companion device...";
    for (const QVariantMap& child : gen2Device.children) {
        QString hardwareId = child["hardwareId"].toString();
        QString deviceId = child["deviceId"].toString();
        
        qCDebug(log_device_windows) << "  Direct child - Hardware ID:" << hardwareId << "Device ID:" << deviceId;
        
        // Skip interface endpoints we don't need
        if (deviceId.contains("&0002") || deviceId.contains("&0004")) {
            qCDebug(log_device_windows) << "    Skipping interface endpoint";
            continue;
        }
        
        // Check for HID device (MI_04 interface)
        if (hardwareId.toUpper().contains("HID") && deviceId.toUpper().contains("MI_04")) {
            deviceInfo.hidDeviceId = deviceId;
            qCDebug(log_device_windows) << "  ✓ Found HID interface:" << deviceId;
        }
        // Check for camera device (MI_00 interface)
        else if (hardwareId.toUpper().contains("MI_00")) {
            deviceInfo.cameraDeviceId = deviceId;
            qCDebug(log_device_windows) << "  ✓ Found camera interface:" << deviceId;
        }
        // Check for audio device (Audio in hardware ID)
        else if (hardwareId.toUpper().contains("AUDIO")) {
            deviceInfo.audioDeviceId = deviceId;
            qCDebug(log_device_windows) << "  ✓ Found audio interface:" << deviceId;
        }
    }
    
    // Main approach: Find CompanionPortChain and check its child devices
    qCDebug(log_device_windows) << "Finding CompanionPortChain and checking its child devices...";
    QString companionPortChain = findCompanionPortChain(gen2Device);
    
    if (!companionPortChain.isEmpty()) {
        qCDebug(log_device_windows) << "Found CompanionPortChain:" << companionPortChain;
        
        // Get the device instance for the CompanionPortChain
        DWORD companionDevInst = getDeviceInstanceFromPortChain(companionPortChain);
        if (companionDevInst != 0) {
            qCDebug(log_device_windows) << "Getting child devices of CompanionPortChain...";
            QList<QVariantMap> companionChildren = getAllChildDevices(companionDevInst);
            qCDebug(log_device_windows) << "Found" << companionChildren.size() << "child devices in CompanionPortChain";
            
            // Check each child device of the CompanionPortChain
            for (const QVariantMap& child : companionChildren) {
                QString hardwareId = child["hardwareId"].toString();
                QString deviceId = child["deviceId"].toString();
                QString friendlyName = child["friendlyName"].toString();
                
                qCDebug(log_device_windows) << "  CompanionPortChain child - Device ID:" << deviceId;
                qCDebug(log_device_windows) << "    Hardware ID:" << hardwareId;
                qCDebug(log_device_windows) << "    Friendly Name:" << friendlyName;
                
                // Skip interface endpoints we don't need
                if (deviceId.contains("&0002") || deviceId.contains("&0004")) {
                    qCDebug(log_device_windows) << "    Skipping interface endpoint";
                    continue;
                }
                
                // Check for HID device (MI_04 interface or HID in hardware ID)
                if (!deviceInfo.hasHidDevice() && 
                    ((hardwareId.toUpper().contains("HID") && deviceId.toUpper().contains("MI_04")) ||
                     (hardwareId.toUpper().contains(AbstractPlatformDeviceManager::OPENTERFACE_VID_V2.toUpper()) && 
                      hardwareId.toUpper().contains(AbstractPlatformDeviceManager::OPENTERFACE_PID_V2.toUpper()) && 
                      deviceId.toUpper().contains("MI_04")))) {
                    deviceInfo.hidDeviceId = deviceId;
                    qCDebug(log_device_windows) << "  ✓ Found HID device in CompanionPortChain:" << deviceId;
                }
                
                // Check for camera device (MI_00 interface)
                else if (!deviceInfo.hasCameraDevice() && 
                         ((hardwareId.toUpper().contains("MI_00")) ||
                          (hardwareId.toUpper().contains(AbstractPlatformDeviceManager::OPENTERFACE_VID_V2.toUpper()) && 
                           hardwareId.toUpper().contains(AbstractPlatformDeviceManager::OPENTERFACE_PID_V2.toUpper()) && 
                           deviceId.toUpper().contains("MI_00")))) {
                    deviceInfo.cameraDeviceId = deviceId;
                    qCDebug(log_device_windows) << "  ✓ Found camera device in CompanionPortChain:" << deviceId;
                }
                
                // Check for audio device (MI_01 interface or Audio in hardware ID)
                else if (!deviceInfo.hasAudioDevice() && 
                         ((hardwareId.toUpper().contains("AUDIO")) ||
                          (hardwareId.toUpper().contains("MI_01")) ||
                          (hardwareId.toUpper().contains(AbstractPlatformDeviceManager::OPENTERFACE_VID_V2.toUpper()) && 
                           hardwareId.toUpper().contains(AbstractPlatformDeviceManager::OPENTERFACE_PID_V2.toUpper()) && 
                           deviceId.toUpper().contains("MI_01")))) {
                    deviceInfo.audioDeviceId = deviceId;
                    qCDebug(log_device_windows) << "  ✓ Found audio device in CompanionPortChain:" << deviceId;
                }
            }
        } else {
            qCWarning(log_device_windows) << "Could not get device instance for CompanionPortChain:" << companionPortChain;
        }
    } else {
        qCWarning(log_device_windows) << "Could not find CompanionPortChain for companion device:" << gen2Device.portChain;
    }
    
    qCDebug(log_device_windows) << "Generation 2 interface discovery complete - HID:" << (deviceInfo.hasHidDevice() ? "✓" : "✗")
                               << "Camera:" << (deviceInfo.hasCameraDevice() ? "✓" : "✗") 
                               << "Audio:" << (deviceInfo.hasAudioDevice() ? "✓" : "✗");
}

void WindowsDeviceManager::processGeneration1Interfaces(DeviceInfo& deviceInfo, const USBDeviceData& gen1Device)
{
    qCDebug(log_device_windows) << "Processing Generation 1 interfaces for device:" << deviceInfo.portChain;
    
    // Process serial interfaces first
    processGeneration1SerialInterface(deviceInfo, gen1Device);
    
    // Process media interfaces (HID, camera, audio)
    processGeneration1MediaInterfaces(deviceInfo, gen1Device);
}

void WindowsDeviceManager::processGeneration1SerialInterface(DeviceInfo& deviceInfo, const USBDeviceData& deviceData)
{
    // Process siblings to find serial port devices (Python-compatible logic)
    for (const QVariantMap& sibling : deviceData.siblings) {
        QString hardwareId = sibling["hardwareId"].toString();
        QString deviceId = sibling["deviceId"].toString();
        
        // Check if this sibling is a serial port with our target VID/PID (1A86:7523)
        if (hardwareId.toUpper().contains(AbstractPlatformDeviceManager::SERIAL_VID.toUpper()) &&
            hardwareId.toUpper().contains(AbstractPlatformDeviceManager::SERIAL_PID.toUpper())) {
            deviceInfo.serialPortId = deviceId;
            deviceInfo.serialPortPath = deviceData.portChain;
            qCDebug(log_device_windows) << "  ✓ Found serial port sibling:" << deviceId;
            break;
        }
    }
    
    // Also check for Generation 2 serial devices (1A86:CH32V208) in siblings
    for (const QVariantMap& sibling : deviceData.siblings) {
        QString hardwareId = sibling["hardwareId"].toString();
        QString deviceId = sibling["deviceId"].toString();
        
        // Check if this sibling is a Gen2 serial port (1A86:CH32V208)
        if (hardwareId.toUpper().contains(AbstractPlatformDeviceManager::SERIAL_VID_V2.toUpper()) &&
            hardwareId.toUpper().contains(AbstractPlatformDeviceManager::SERIAL_PID_V2.toUpper())) {
            deviceInfo.serialPortId = deviceId;
            deviceInfo.serialPortPath = deviceData.portChain;
            qCDebug(log_device_windows) << "  ✓ Found Gen2 serial port sibling:" << deviceId;
            break;
        }
    }
}

void WindowsDeviceManager::processGeneration1MediaInterfaces(DeviceInfo& deviceInfo, const USBDeviceData& deviceData)
{
    // Process children to find HID, camera, and audio devices
    for (const QVariantMap& child : deviceData.children) {
        QString hardwareId = child["hardwareId"].toString();
        QString deviceId = child["deviceId"].toString();
        
        // Skip interface endpoints we don't need
        if (deviceId.contains("&0002") || deviceId.contains("&0004")) {
            continue;
        }
        
        // Check for HID device
        if (hardwareId.toUpper().contains("HID") && deviceId.toUpper().contains("MI_04")) {
            if (deviceInfo.hidDeviceId.isEmpty()) {  // Don't overwrite if already found
                deviceInfo.hidDeviceId = deviceId;
                qCDebug(log_device_windows) << "  ✓ Found HID interface:" << deviceId;
            }
        }
        // Check for camera device (MI_00 interface)
        else if (hardwareId.toUpper().contains("MI_00")) {
            if (deviceInfo.cameraDeviceId.isEmpty()) {  // Don't overwrite if already found
                deviceInfo.cameraDeviceId = deviceId;
                qCDebug(log_device_windows) << "  ✓ Found camera interface:" << deviceId;
            }
        }
        // Check for audio device (MI_01 interface or Audio in hardware ID)
        else if (hardwareId.toUpper().contains("MI_01") || hardwareId.toUpper().contains("AUDIO")) {
            if (deviceInfo.audioDeviceId.isEmpty()) {  // Don't overwrite if already found
                deviceInfo.audioDeviceId = deviceId;
                qCDebug(log_device_windows) << "  ✓ Found audio interface:" << deviceId;
            }
        }
    }
}

void WindowsDeviceManager::enhanceDeviceDetection(QMap<QString, DeviceInfo>& deviceMap)
{
    qCDebug(log_device_windows) << "Enhancing device detection for USB 2.0/3.0 compatibility...";
    
    // Iterate through all devices and try to fill missing interfaces
    for (auto it = deviceMap.begin(); it != deviceMap.end(); ++it) {
        DeviceInfo& device = it.value();
        
        qCDebug(log_device_windows) << "Enhancing device at port chain:" << device.portChain;
        
        // Try to find missing serial port using Generation 2 method if not found
        if (!device.hasSerialPort()) {
            qCDebug(log_device_windows) << "  Attempting to find missing serial port...";
            QString serialPortId = findSerialPortForPortChain(device.portChain);
            if (!serialPortId.isEmpty()) {
                device.serialPortId = serialPortId;
                qCDebug(log_device_windows) << "  ✓ Enhanced: Found serial port" << serialPortId;
            }
        }
        
        // Try alternative HID detection if not found
        if (!device.hasHidDevice()) {
            qCDebug(log_device_windows) << "  Attempting to find missing HID device...";
            QString hidDeviceId = findHidDeviceForPortChain(device.portChain);
            if (!hidDeviceId.isEmpty()) {
                device.hidDeviceId = hidDeviceId;
                qCDebug(log_device_windows) << "  ✓ Enhanced: Found HID device" << hidDeviceId;
            }
        }
        
        // Convert any newly found device IDs to real paths
        if (device.deviceInstanceId.contains(AbstractPlatformDeviceManager::OPENTERFACE_VID_V2)) {
            matchDevicePathsToRealPathsGeneration2(device);
        } else {
            matchDevicePathsToRealPaths(device);
        }
        
        qCDebug(log_device_windows) << "  Enhancement result - Serial:" << (device.hasSerialPort() ? "✓" : "✗")
                                   << "HID:" << (device.hasHidDevice() ? "✓" : "✗")
                                   << "Camera:" << (device.hasCameraDevice() ? "✓" : "✗")
                                   << "Audio:" << (device.hasAudioDevice() ? "✓" : "✗");
    }
}

QString WindowsDeviceManager::findSerialPortForPortChain(const QString& portChain)
{
    qCDebug(log_device_windows) << "Searching for serial port associated with port chain:" << portChain;
    
    // Try Generation 2 approach first - look for 1A86:CH32V208 devices
    QList<USBDeviceData> serialDevicesV2 = findUSBDevicesWithVidPid(AbstractPlatformDeviceManager::SERIAL_VID_V2, AbstractPlatformDeviceManager::SERIAL_PID_V2);
    for (const USBDeviceData& serialDevice : serialDevicesV2) {
        if (arePortChainsRelated(portChain, serialDevice.portChain)) {
            qCDebug(log_device_windows) << "  ✓ Found related Generation 2 serial port:" << serialDevice.deviceInstanceId;
            return serialDevice.deviceInstanceId;
        }
    }
    
    // Try Generation 1 approach - look for 1A86:7523 devices
    QList<USBDeviceData> serialDevicesV1 = findUSBDevicesWithVidPid(AbstractPlatformDeviceManager::SERIAL_VID, AbstractPlatformDeviceManager::SERIAL_PID);
    for (const USBDeviceData& serialDevice : serialDevicesV1) {
        if (arePortChainsRelated(portChain, serialDevice.portChain)) {
            qCDebug(log_device_windows) << "  ✓ Found related Generation 1 serial port:" << serialDevice.deviceInstanceId;
            return serialDevice.deviceInstanceId;
        }
    }
    
    return QString();
}

QString WindowsDeviceManager::findHidDeviceForPortChain(const QString& portChain)
{
    qCDebug(log_device_windows) << "Searching for HID device associated with port chain:" << portChain;
    
    // Enumerate HID devices and look for ones that match our port chain
    GUID hidGuid;
    HidD_GetHidGuid(&hidGuid);
    
    HDEVINFO hDevInfo = SetupDiGetClassDevs(&hidGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (hDevInfo == INVALID_HANDLE_VALUE) {
        return QString();
    }
    
    SP_DEVICE_INTERFACE_DATA interfaceData;
    interfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);
    
    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(hDevInfo, nullptr, &hidGuid, i, &interfaceData); i++) {
        SP_DEVINFO_DATA devInfoData;
        devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
        
        DWORD requiredSize = 0;
        SetupDiGetDeviceInterfaceDetail(hDevInfo, &interfaceData, nullptr, 0, &requiredSize, &devInfoData);
        
        if (requiredSize > 0) {
            std::vector<BYTE> buffer(requiredSize);
            PSP_DEVICE_INTERFACE_DETAIL_DATA detailData = reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA>(buffer.data());
            detailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);
            
            if (SetupDiGetDeviceInterfaceDetail(hDevInfo, &interfaceData, detailData, requiredSize, nullptr, &devInfoData)) {
                QString deviceId = getDeviceId(devInfoData.DevInst);
                QString devicePortChain = buildPythonCompatiblePortChain(devInfoData.DevInst);
                
                // Check if this HID device is associated with our target port chain
                if (arePortChainsRelated(portChain, devicePortChain) && deviceId.toUpper().contains("MI_04")) {
                    qCDebug(log_device_windows) << "  ✓ Found related HID device:" << deviceId;
                    SetupDiDestroyDeviceInfoList(hDevInfo);
                    return deviceId;
                }
            }
        }
    }
    
    SetupDiDestroyDeviceInfoList(hDevInfo);
    return QString();
}

QString WindowsDeviceManager::findSerialPortByCompanionDevice(const USBDeviceData& companionDevice)
{
    qCDebug(log_device_windows) << "Searching for serial port associated with companion device...";
    
    // Extract the companion port chain and analyze the hub structure
    QString companionPortChain = companionDevice.portChain;
    qCDebug(log_device_windows) << "Companion device port chain:" << companionPortChain;
    
    // For Generation 2, the topology is:
    // USB Hub (General-purpose) -> contains both:
    //   1. Serial device (1A86:CH32V208) 
    //   2. Companion device (345F:2130) - this is the Openterface device with camera/audio/HID
    
    // Find the parent hub of the companion device
    QString companionHubPort = extractHubPortFromChain(companionPortChain);
    qCDebug(log_device_windows) << "Companion device hub port:" << companionHubPort;
    
    // For Generation 2, calculate the expected serial port hub by incrementing the last number
    // If companion is at "1-0", serial should be at "1-1"
    QString expectedSerialHubPort = calculateExpectedSerialHubPort(companionHubPort);
    qCDebug(log_device_windows) << "Expected serial hub port:" << expectedSerialHubPort;
    
    // Find all serial devices with the V2 VID/PID (1A86:CH32V208)
    QList<USBDeviceData> serialDevices = findUSBDevicesWithVidPid(AbstractPlatformDeviceManager::SERIAL_VID_V2, AbstractPlatformDeviceManager::SERIAL_PID_V2);
    qCDebug(log_device_windows) << "Found" << serialDevices.size() << "serial devices with VID/PID" << AbstractPlatformDeviceManager::SERIAL_VID_V2 << "/" << AbstractPlatformDeviceManager::SERIAL_PID_V2;
    
    for (const USBDeviceData& serialDevice : serialDevices) {
        QString serialPortChain = serialDevice.portChain;
        QString serialHubPort = extractHubPortFromChain(serialPortChain);
        
        qCDebug(log_device_windows) << "Checking serial device:";
        qCDebug(log_device_windows) << "  Port chain:" << serialPortChain;
        qCDebug(log_device_windows) << "  Hub port:" << serialHubPort;
        
        // Primary check: does the serial hub port match our expected port (companion + 1)?
        if (!expectedSerialHubPort.isEmpty() && serialHubPort == expectedSerialHubPort) {
            qCDebug(log_device_windows) << "✓ Found serial device at expected hub port:" << expectedSerialHubPort;
            qCDebug(log_device_windows) << "  Companion hub port:" << companionHubPort;
            qCDebug(log_device_windows) << "  Serial hub port:" << serialHubPort;
            
            // Additional verification: check if they are indeed companion devices
            if (isSerialDeviceAssociatedWithCompanion(serialDevice, companionDevice)) {
                qCDebug(log_device_windows) << "✓ Verified companion relationship";
                return serialDevice.deviceInstanceId;
            }
        }
        
        // Fallback check: if both devices are under the same parent hub (old logic)
        if (!companionHubPort.isEmpty() && !serialHubPort.isEmpty() && 
            companionHubPort == serialHubPort) {
            qCDebug(log_device_windows) << "✓ Found serial device under same hub as companion device (fallback)";
            qCDebug(log_device_windows) << "  Shared hub port:" << companionHubPort;
            
            // Additional verification: check if they are indeed companion devices
            if (isSerialDeviceAssociatedWithCompanion(serialDevice, companionDevice)) {
                qCDebug(log_device_windows) << "✓ Verified companion relationship (fallback)";
                return serialDevice.deviceInstanceId;
            }
        }
        
        // Also check if they are direct siblings (same hub, different ports)
        if (arePortChainsRelated(serialPortChain, companionPortChain)) {
            qCDebug(log_device_windows) << "✓ Found related serial device (sibling relationship)";
            
            // Verify this is actually our target serial device
            if (isSerialDeviceAssociatedWithCompanion(serialDevice, companionDevice)) {
                qCDebug(log_device_windows) << "✓ Verified sibling companion relationship";
                return serialDevice.deviceInstanceId;
            }
        }
    }
    
    qCDebug(log_device_windows) << "✗ No associated serial device found for companion device";
    return QString();
}

QString WindowsDeviceManager::extractHubPortFromChain(const QString& portChain)
{
    // Extract the hub portion from a port chain
    // For example, from "USB\\ROOT_HUB30\\4&1234&0&0#USB\\VID_1234&PID_5678\\5&67890&0&1"
    // we want to extract the hub identifier
    
    if (portChain.isEmpty()) {
        return QString();
    }
    
    // Find the hub portion (usually the first part before the device-specific part)
    QStringList parts = portChain.split('#');
    if (parts.size() >= 1) {
        QString hubPart = parts[0];
        // Extract meaningful hub identifier
        if (hubPart.contains("ROOT_HUB") || hubPart.contains("HUB")) {
            return hubPart;
        }
    }
    
    // Fallback: Extract hub port from a port chain like "1-2.3" -> "1-2"
    int lastDotIndex = portChain.lastIndexOf('.');
    if (lastDotIndex > 0) {
        return portChain.left(lastDotIndex);
    }
    
    return portChain; // If no patterns match, return the whole chain
}

QString WindowsDeviceManager::calculateExpectedSerialHubPort(const QString& companionHubPort)
{
    // For Generation 2 devices, calculate the expected serial port hub 
    // by incrementing the last number in the companion hub port
    // Example: if companion is at "1-0", serial should be at "1-1"
    
    if (companionHubPort.isEmpty()) {
        return QString();
    }
    
    qCDebug(log_device_windows) << "Calculating expected serial hub port from companion hub port:" << companionHubPort;
    
    // Look for patterns like "1-0", "2-3", etc.
    QRegularExpression pattern(R"(^(.+-)(\d+)$)");
    QRegularExpressionMatch match = pattern.match(companionHubPort);
    
    if (match.hasMatch()) {
        QString prefix = match.captured(1);  // e.g., "1-"
        int lastNumber = match.captured(2).toInt();  // e.g., 0
        int expectedSerialNumber = lastNumber + 1;  // e.g., 1
        
        QString expectedSerialHubPort = prefix + QString::number(expectedSerialNumber);
        qCDebug(log_device_windows) << "Expected serial hub port:" << expectedSerialHubPort 
                                   << "(prefix:" << prefix << "companion number:" << lastNumber 
                                   << "serial number:" << expectedSerialNumber << ")";
        return expectedSerialHubPort;
    }
    
    // Try alternative pattern matching for Windows device IDs
    // Look for patterns in Windows device instance IDs that might contain port numbers
    if (companionHubPort.contains("\\")) {
        QStringList parts = companionHubPort.split('\\');
        for (QString& part : parts) {
            // Look for parts that end with numbers that we can increment
            QRegularExpression numberPattern(R"(^(.+)(\d+)$)");
            QRegularExpressionMatch numberMatch = numberPattern.match(part);
            if (numberMatch.hasMatch()) {
                QString partPrefix = numberMatch.captured(1);
                int number = numberMatch.captured(2).toInt();
                int expectedNumber = number + 1;
                
                // Replace this part with the incremented version
                part = partPrefix + QString::number(expectedNumber);
                
                QString expectedSerialHubPort = parts.join('\\');
                qCDebug(log_device_windows) << "Expected serial hub port (Windows ID pattern):" << expectedSerialHubPort;
                return expectedSerialHubPort;
            }
        }
    }
    
    qCDebug(log_device_windows) << "Could not calculate expected serial hub port from:" << companionHubPort;
    return QString();
}

bool WindowsDeviceManager::arePortChainsRelated(const QString& portChain1, const QString& portChain2)
{
    // Check if two port chains are related (e.g., devices on the same hub)
    
    if (portChain1.isEmpty() || portChain2.isEmpty()) {
        return false;
    }
    
    QString hub1 = extractHubPortFromChain(portChain1);
    QString hub2 = extractHubPortFromChain(portChain2);
    
    if (hub1.isEmpty() || hub2.isEmpty()) {
        return false;
    }
    
    // If they share the same hub, they are related
    if (hub1 == hub2) {
        return true;
    }
    
    // Check if one is a direct child of the other's hub
    if (portChain1.startsWith(hub2 + ".") || portChain2.startsWith(hub1 + ".")) {
        return true;
    }
    
    return false;
}

bool WindowsDeviceManager::isSerialDeviceAssociatedWithCompanion(const USBDeviceData& serialDevice, const USBDeviceData& companionDevice)
{
    // For the companion device approach, we need to check if the serial device
    // and companion device are related through their port chains or parent devices
    
    QString serialPortChain = serialDevice.portChain;
    QString companionPortChain = companionDevice.portChain;
    
    qCDebug(log_device_windows) << "Comparing port chains - Serial:" << serialPortChain << "Companion:" << companionPortChain;
    
    // Method 1: Check if serial port is at expected hub port (companion + 1)
    // Extract the hub part of the port chain (everything before the last dot)
    QString serialHubPort = extractHubPortFromChain(serialPortChain);
    QString companionHubPort = extractHubPortFromChain(companionPortChain);
    qCDebug(log_device_windows) << "Serial hub port:" << serialHubPort << "Companion hub port:" << companionHubPort;
    
    // For Generation 2, calculate the expected serial hub port by incrementing companion port
    QString expectedSerialHubPort = calculateExpectedSerialHubPort(companionHubPort);
    if (!expectedSerialHubPort.isEmpty() && serialHubPort == expectedSerialHubPort) {
        qCDebug(log_device_windows) << "✓ Serial device at expected hub port (companion + 1):" << expectedSerialHubPort;
        return true;
    }
    
    // Fallback: Check if they share the same hub (legacy logic for edge cases)
    if (!serialHubPort.isEmpty() && !companionHubPort.isEmpty() && serialHubPort == companionHubPort) {
        qCDebug(log_device_windows) << "✓ Devices share the same hub port (fallback):" << serialHubPort;
        return true;
    }
    
    // Method 2: Check CompanionPortNumber relationship (if available in device properties)
    // This would require accessing Windows-specific companion device properties
    // For now, we'll use a simpler heuristic based on port proximity
    
    // Method 3: Check if the port numbers are adjacent or have a specific relationship
    if (arePortChainsRelated(serialPortChain, companionPortChain)) {
        qCDebug(log_device_windows) << "✓ Port chains appear to be related";
        return true;
    }
    
    qCDebug(log_device_windows) << "✗ No relationship found between devices";
    return false;
}

QString WindowsDeviceManager::getDevicePropertyByName(DWORD devInst, const QString& propertyName)
{
    // Get a specific property from a device instance
    // This is a helper method to query device-specific properties
    
    if (devInst == 0) {
        return QString();
    }
    
    // For this implementation, we'll use a simplified approach
    // In a full implementation, this would query specific device properties
    // based on the propertyName parameter using CM_Get_DevNode_Property or similar
    
    // Common properties that might be relevant:
    // - "CompanionPortNumber"
    // - "PortNumber" 
    // - "LocationInformation"
    
    ULONG bufferSize = 1024;
    wchar_t buffer[1024];
    CONFIGRET result;
    
    // This is a simplified implementation - in practice you'd need to 
    // map propertyName to specific DEVPKEY values
    if (propertyName == "LocationInformation") {
        result = CM_Get_DevNode_Registry_Property(devInst, CM_DRP_LOCATION_INFORMATION, 
                                                 nullptr, buffer, &bufferSize, 0);
        if (result == CR_SUCCESS) {
            return QString::fromWCharArray(buffer);
        }
    }
    
    return QString(); // Property not found or not implemented
}

QString WindowsDeviceManager::findCompanionPortChain(const USBDeviceData& companionDevice)
{
    qCDebug(log_device_windows) << "Finding CompanionPortChain for companion device:" << companionDevice.portChain;
    
    // The CompanionPortChain is typically the parent hub of the companion device
    // For USB 3.0, the companion device (345F:2130) is connected to a hub,
    // and that hub contains all the interface devices (camera, HID, audio)
    
    // Get the device instance of the companion device
    DWORD companionDevInst = getDeviceInstanceFromId(companionDevice.deviceInstanceId);
    if (companionDevInst == 0) {
        qCWarning(log_device_windows) << "Could not get device instance for companion device";
        return QString();
    }
    
    // Get the parent device (hub) of the companion device
    DWORD parentDevInst;
    if (CM_Get_Parent(&parentDevInst, companionDevInst, 0) != CR_SUCCESS) {
        qCWarning(log_device_windows) << "Could not get parent device for companion device";
        return QString();
    }
    
    QString parentDeviceId = getDeviceId(parentDevInst);
    QString parentPortChain = buildPythonCompatiblePortChain(parentDevInst);
    
    qCDebug(log_device_windows) << "Found parent hub device ID:" << parentDeviceId;
    qCDebug(log_device_windows) << "Parent hub port chain:" << parentPortChain;
    
    // For USB 3.0, we want to use the parent hub as the CompanionPortChain
    // because the interface devices are children of that hub
    return parentPortChain;
}

DWORD WindowsDeviceManager::getDeviceInstanceFromPortChain(const QString& portChain)
{
    qCDebug(log_device_windows) << "Getting device instance from port chain:" << portChain;
    
    // This is a simplified approach - we'll enumerate all USB devices
    // and find one that has a matching port chain
    HDEVINFO hDevInfo = SetupDiGetClassDevs(&GUID_DEVCLASS_USB, nullptr, nullptr, DIGCF_PRESENT);
    if (hDevInfo == INVALID_HANDLE_VALUE) {
        qCWarning(log_device_windows) << "Failed to get USB device list";
        return 0;
    }
    
    SP_DEVINFO_DATA devInfoData;
    devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
    
    for (DWORD i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &devInfoData); i++) {
        QString devicePortChain = buildPythonCompatiblePortChain(devInfoData.DevInst);
        
        if (devicePortChain == portChain) {
            SetupDiDestroyDeviceInfoList(hDevInfo);
            qCDebug(log_device_windows) << "Found device instance for port chain:" << portChain;
            return devInfoData.DevInst;
        }
    }
    
    SetupDiDestroyDeviceInfoList(hDevInfo);
    qCWarning(log_device_windows) << "Could not find device instance for port chain:" << portChain;
    return 0;
}

QString WindowsDeviceManager::findSerialPortByIntegratedDevice(const USBDeviceData& integratedDevice)
{
    qCDebug(log_device_windows) << "Finding serial port associated with integrated device:" << integratedDevice.portChain;
    qCDebug(log_device_windows) << "  Integrated device ID:" << integratedDevice.deviceInstanceId;
    
    // For the integrated device approach:
    // The integrated device (345F:2132) is at a different port chain than the serial port (1A86:CH32V208)
    // We need to find serial ports that have a CompanionPortChain relationship
    
    // Find all serial devices with VID/PID 1A86:CH32V208
    QList<USBDeviceData> serialDevices = findUSBDevicesWithVidPid(AbstractPlatformDeviceManager::SERIAL_VID_V2, AbstractPlatformDeviceManager::SERIAL_PID_V2);
    qCDebug(log_device_windows) << "Found" << serialDevices.size() << "serial devices (1A86:CH32V208)";
    
    for (const USBDeviceData& serialDevice : serialDevices) {
        qCDebug(log_device_windows) << "  Checking serial device at port chain:" << serialDevice.portChain;
        qCDebug(log_device_windows) << "    Serial device ID:" << serialDevice.deviceInstanceId;
        
        // Check if this serial device is associated with the integrated device through CompanionPortChain
        if (isSerialAssociatedWithIntegratedDevice(serialDevice, integratedDevice)) {
            qCDebug(log_device_windows) << "  ✓ Found associated serial device:" << serialDevice.deviceInstanceId;
            return serialDevice.deviceInstanceId;
        }
    }
    
    qCDebug(log_device_windows) << "  ✗ No associated serial device found";
    return QString();
}

bool WindowsDeviceManager::isSerialAssociatedWithIntegratedDevice(const USBDeviceData& serialDevice, const USBDeviceData& integratedDevice)
{
    qCDebug(log_device_windows) << "Checking if serial device is associated with integrated device";
    qCDebug(log_device_windows) << "  Serial port chain:" << serialDevice.portChain;
    qCDebug(log_device_windows) << "  Integrated port chain:" << integratedDevice.portChain;
    
    // Multiple association methods for robustness
    
    // Method 1: CompanionPortChain matching (primary method)
    QString serialParentChain = extractParentPortChain(serialDevice.portChain);
    QString integratedCompanionChain = getCompanionPortChainFromDevice(integratedDevice);
    qCDebug(log_device_windows) << "  Serial parent chain:" << serialParentChain;
    qCDebug(log_device_windows) << "  Integrated CompanionPortChain:" << integratedCompanionChain;
    
    if (!serialParentChain.isEmpty() && !integratedCompanionChain.isEmpty() && 
        serialParentChain == integratedCompanionChain) {
        qCDebug(log_device_windows) << "  ✓ Association confirmed via CompanionPortChain matching";
        return true;
    }
    
    // Method 2: Hub hierarchy analysis (fallback method)
    if (isDevicesOnSameUSBHub(serialDevice, integratedDevice)) {
        qCDebug(log_device_windows) << "  ✓ Association confirmed via USB hub analysis";
        return true;
    }
    
    // Method 3: Proximity-based matching (for devices with similar timing)
    if (areDevicesProximate(serialDevice, integratedDevice)) {
        qCDebug(log_device_windows) << "  ✓ Association confirmed via proximity matching";
        return true;
    }
    
    // Method 4: Pattern-based matching for known USB 3.0 configurations
    if (matchesKnownUSB3Pattern(serialDevice, integratedDevice)) {
        qCDebug(log_device_windows) << "  ✓ Association confirmed via known USB 3.0 pattern";
        return true;
    }
    
    qCDebug(log_device_windows) << "  ✗ No association found between devices";
    return false;
}

QString WindowsDeviceManager::extractParentPortChain(const QString& portChain)
{
    // Extract parent from port chain like "1-2.2" -> "1-2"
    int lastDotIndex = portChain.lastIndexOf('.');
    if (lastDotIndex > 0) {
        return portChain.left(lastDotIndex);
    }
    return QString();
}

QString WindowsDeviceManager::getCompanionPortChainFromDevice(const USBDeviceData& integratedDevice)
{
    // This would need to query the Windows USB API to get CompanionPortChain information
    // For now, we'll use a enhanced approach based on the port chain relationship and USB topology
    
    DWORD devInst = getDeviceInstanceFromId(integratedDevice.deviceInstanceId);
    if (devInst == 0) {
        return QString();
    }
    
    // Try to get CompanionPortChain property - this is a simplified approach
    // In a full implementation, you'd need to query the specific USB properties
    QString companionPortChain = getDevicePropertyByName(devInst, "CompanionPortChain");
    
    if (companionPortChain.isEmpty()) {
        qCDebug(log_device_windows) << "CompanionPortChain property not available, using enhanced fallback methods";
        
        // Enhanced fallback method 1: Parent device analysis
        companionPortChain = analyzeParentDeviceForCompanionChain(integratedDevice);
        
        if (companionPortChain.isEmpty()) {
            // Enhanced fallback method 2: Pattern-based inference using USB topology knowledge
            companionPortChain = inferCompanionChainFromUSBTopology(integratedDevice);
        }
        
        if (companionPortChain.isEmpty()) {
            // Enhanced fallback method 3: Use improved port chain pattern matching
            companionPortChain = inferCompanionChainFromPortPattern(integratedDevice);
        }
    }
    
    return companionPortChain;
}

void WindowsDeviceManager::processIntegratedDeviceInterfaces(DeviceInfo& deviceInfo, const USBDeviceData& integratedDevice)
{
    qCDebug(log_device_windows) << "Processing integrated device interfaces for:" << deviceInfo.portChain;
    
    // Process children of the integrated device to find HID, camera, and audio interfaces
    for (const QVariantMap& child : integratedDevice.children) {
        QString hardwareId = child["hardwareId"].toString();
        QString deviceId = child["deviceId"].toString();
        QString friendlyName = child["friendlyName"].toString();
        
        qCDebug(log_device_windows) << "  Integrated device child - Device ID:" << deviceId;
        qCDebug(log_device_windows) << "    Hardware ID:" << hardwareId;
        qCDebug(log_device_windows) << "    Friendly Name:" << friendlyName;
        
        // Skip interface endpoints we don't need
        if (deviceId.contains("&0002") || deviceId.contains("&0004")) {
            qCDebug(log_device_windows) << "    Skipping interface endpoint";
            continue;
        }
        
        // Check for HID device (MI_04 interface or HID in hardware ID)
        if (!deviceInfo.hasHidDevice() && 
            ((hardwareId.toUpper().contains("HID") && deviceId.toUpper().contains("MI_04")) ||
             (hardwareId.toUpper().contains(AbstractPlatformDeviceManager::OPENTERFACE_VID_V2.toUpper()) && 
              hardwareId.toUpper().contains(AbstractPlatformDeviceManager::OPENTERFACE_PID_V2.toUpper()) && 
              deviceId.toUpper().contains("MI_04")))) {
            deviceInfo.hidDeviceId = deviceId;
            qCDebug(log_device_windows) << "  ✓ Found HID device in integrated device:" << deviceId;
        }
        
        // Check for camera device (MI_00 interface)
        else if (!deviceInfo.hasCameraDevice() && 
                 ((hardwareId.toUpper().contains("MI_00")) ||
                  (hardwareId.toUpper().contains(AbstractPlatformDeviceManager::OPENTERFACE_VID_V2.toUpper()) && 
                   hardwareId.toUpper().contains(AbstractPlatformDeviceManager::OPENTERFACE_PID_V2.toUpper()) && 
                   deviceId.toUpper().contains("MI_00")))) {
            deviceInfo.cameraDeviceId = deviceId;
            qCDebug(log_device_windows) << "  ✓ Found camera device in integrated device:" << deviceId;
        }
        
        // Check for audio device (MI_01 interface or Audio in hardware ID)
        else if (!deviceInfo.hasAudioDevice() && 
                 ((hardwareId.toUpper().contains("AUDIO")) ||
                  (hardwareId.toUpper().contains("MI_01")) ||
                  (hardwareId.toUpper().contains(AbstractPlatformDeviceManager::OPENTERFACE_VID_V2.toUpper()) && 
                   hardwareId.toUpper().contains(AbstractPlatformDeviceManager::OPENTERFACE_PID_V2.toUpper()) && 
                   deviceId.toUpper().contains("MI_01")))) {
            deviceInfo.audioDeviceId = deviceId;
            qCDebug(log_device_windows) << "  ✓ Found audio device in integrated device:" << deviceId;
        }
    }
}

bool WindowsDeviceManager::isDevicesOnSameUSBHub(const USBDeviceData& serialDevice, const USBDeviceData& integratedDevice)
{
    qCDebug(log_device_windows) << "Checking if devices are on the same USB hub";
    
    // Get the USB hub port chain for both devices
    QString serialHubChain = getUSBHubPortChain(serialDevice);
    QString integratedHubChain = getUSBHubPortChain(integratedDevice);
    
    qCDebug(log_device_windows) << "  Serial hub chain:" << serialHubChain;
    qCDebug(log_device_windows) << "  Integrated hub chain:" << integratedHubChain;
    
    // If both devices are connected to the same USB hub (or related hubs), they are likely associated
    if (!serialHubChain.isEmpty() && !integratedHubChain.isEmpty()) {
        // Check for exact hub match
        if (serialHubChain == integratedHubChain) {
            qCDebug(log_device_windows) << "    ✓ Devices on same USB hub";
            return true;
        }
        
        // Check for related hubs (USB 3.0 often has USB 2.0/3.0 controller pairs)
        if (arePortChainsRelated(serialHubChain, integratedHubChain)) {
            qCDebug(log_device_windows) << "    ✓ Devices on related USB hubs";
            return true;
        }
    }
    
    qCDebug(log_device_windows) << "    ✗ Devices not on same/related USB hubs";
    return false;
}

bool WindowsDeviceManager::areDevicesProximate(const USBDeviceData& serialDevice, const USBDeviceData& integratedDevice)
{
    qCDebug(log_device_windows) << "Checking device proximity based on port chain patterns";
    
    // Calculate "distance" between port chains to determine if devices are physically close
    int distance = calculatePortChainDistance(serialDevice.portChain, integratedDevice.portChain);
    qCDebug(log_device_windows) << "  Port chain distance:" << distance;
    
    // Consider devices "proximate" if they're within reasonable distance
    // This accounts for USB 3.0 configurations where related devices may appear at different ports
    bool isProximate = (distance >= 0 && distance <= 3); // Allow up to 3 "hops" difference
    
    qCDebug(log_device_windows) << "  Proximity result:" << (isProximate ? "✓ Proximate" : "✗ Not proximate");
    return isProximate;
}

bool WindowsDeviceManager::matchesKnownUSB3Pattern(const USBDeviceData& serialDevice, const USBDeviceData& integratedDevice)
{
    qCDebug(log_device_windows) << "Checking against known USB 3.0 patterns";
    
    QString serialChain = serialDevice.portChain;
    QString integratedChain = integratedDevice.portChain;
    
    // Known pattern 1: Serial at x-y.z, Integrated at x-w (where w is much larger than y)
    // Example: Serial at "1-2.2", Integrated at "1-18"
    QRegularExpression serialPattern(R"(^(\d+)-(\d+)\.(\d+)$)");
    QRegularExpression integratedPattern(R"(^(\d+)-(\d+)$)");
    
    QRegularExpressionMatch serialMatch = serialPattern.match(serialChain);
    QRegularExpressionMatch integratedMatch = integratedPattern.match(integratedChain);
    
    if (serialMatch.hasMatch() && integratedMatch.hasMatch()) {
        QString serialHubGroup = serialMatch.captured(1);
        int serialHub = serialMatch.captured(2).toInt();
        int serialPort = serialMatch.captured(3).toInt();
        
        QString integratedHubGroup = integratedMatch.captured(1);
        int integratedHub = integratedMatch.captured(2).toInt();
        
        qCDebug(log_device_windows) << "  Pattern analysis:";
        qCDebug(log_device_windows) << "    Serial: Hub group" << serialHubGroup << "Hub" << serialHub << "Port" << serialPort;
        qCDebug(log_device_windows) << "    Integrated: Hub group" << integratedHubGroup << "Hub" << integratedHub;
        
        // Check if they match known USB 3.0 patterns
        if (serialHubGroup == integratedHubGroup) {
            // Pattern: Low hub number with high hub number (USB 2.0/3.0 controller pair)
            if ((serialHub <= 4 && integratedHub >= 15) || (integratedHub <= 4 && serialHub >= 15)) {
                qCDebug(log_device_windows) << "    ✓ Matches USB 2.0/3.0 controller pair pattern";
                return true;
            }
            
            // Pattern: Sequential or nearby hub numbers
            int hubDistance = qAbs(serialHub - integratedHub);
            if (hubDistance <= 2) {
                qCDebug(log_device_windows) << "    ✓ Matches nearby hub pattern (distance:" << hubDistance << ")";
                return true;
            }
        }
    }
    
    qCDebug(log_device_windows) << "    ✗ No known USB 3.0 pattern match";
    return false;
}

QString WindowsDeviceManager::getUSBHubPortChain(const USBDeviceData& device)
{
    // Extract the USB hub portion from a port chain
    // Example: "1-2.2" -> "1-2", "1-18" -> "1-18"
    
    QString portChain = device.portChain;
    int lastDotIndex = portChain.lastIndexOf('.');
    
    if (lastDotIndex > 0) {
        // Device is connected through a hub port (has .x at the end)
        return portChain.left(lastDotIndex);
    } else {
        // Device is directly connected to the hub
        return portChain;
    }
}

int WindowsDeviceManager::calculatePortChainDistance(const QString& portChain1, const QString& portChain2)
{
    if (portChain1.isEmpty() || portChain2.isEmpty()) {
        return -1; // Invalid distance
    }
    
    // Simple distance calculation based on port chain components
    // This could be enhanced with more sophisticated USB topology analysis
    
    QStringList parts1 = portChain1.split(QRegularExpression("[-.]"));
    QStringList parts2 = portChain2.split(QRegularExpression("[-.]"));
    
    int distance = 0;
    int maxParts = qMax(parts1.size(), parts2.size());
    
    for (int i = 0; i < maxParts; ++i) {
        int val1 = (i < parts1.size()) ? parts1[i].toInt() : 0;
        int val2 = (i < parts2.size()) ? parts2[i].toInt() : 0;
        
        distance += qAbs(val1 - val2);
    }
    
    return distance;
}

QString WindowsDeviceManager::analyzeParentDeviceForCompanionChain(const USBDeviceData& integratedDevice)
{
    qCDebug(log_device_windows) << "Analyzing parent device for companion chain";
    
    DWORD devInst = getDeviceInstanceFromId(integratedDevice.deviceInstanceId);
    if (devInst == 0) {
        return QString();
    }
    
    // Walk up the parent chain to find USB hub devices
    DWORD currentInst = devInst;
    int depth = 0;
    
    while (currentInst && depth < 5) { // Limit depth to prevent infinite loops
        DWORD parentInst;
        if (CM_Get_Parent(&parentInst, currentInst, 0) != CR_SUCCESS) {
            break;
        }
        
        QString parentHardwareId = getHardwareIdFromDevInst(parentInst);
        QString parentPortChain = buildPythonCompatiblePortChain(parentInst);
        
        qCDebug(log_device_windows) << "  Parent at depth" << depth << ":" << parentHardwareId << "Port chain:" << parentPortChain;
        
        // Look for USB hub indicators in the parent hardware ID
        if (parentHardwareId.toUpper().contains("USB") && parentHardwareId.toUpper().contains("ROOT")) {
            qCDebug(log_device_windows) << "    Found USB root hub, using port chain:" << parentPortChain;
            return parentPortChain;
        }
        
        currentInst = parentInst;
        depth++;
    }
    
    return QString();
}

QString WindowsDeviceManager::inferCompanionChainFromUSBTopology(const USBDeviceData& integratedDevice)
{
    qCDebug(log_device_windows) << "Inferring companion chain from USB topology";
    
    QString portChain = integratedDevice.portChain;
    qCDebug(log_device_windows) << "  Integrated device port chain:" << portChain;
    
    // Analyze USB 3.0 topology patterns
    // USB 3.0 controllers often have paired 2.0/3.0 root hubs
    // High-numbered ports (15+) on USB 3.0 often correspond to low-numbered ports (1-4) on USB 2.0
    
    QRegularExpression pattern(R"(^(\d+)-(\d+)(?:\.(\d+))?$)");
    QRegularExpressionMatch match = pattern.match(portChain);
    
    if (match.hasMatch()) {
        QString rootHub = match.captured(1);
        int hubPort = match.captured(2).toInt();
        QString devicePort = match.captured(3);
        
        qCDebug(log_device_windows) << "  Port chain analysis: Root hub" << rootHub << "Hub port" << hubPort << "Device port" << devicePort;
        
        // USB 3.0 to USB 2.0 companion mapping heuristics
        QString companionChain;
        
        if (hubPort >= 15) {
            // High port numbers typically map to low companion ports
            int companionPort = ((hubPort - 15) % 4) + 1; // Map 15,16,17,18 -> 1,2,3,4
            companionChain = QString("%1-%2").arg(rootHub).arg(companionPort);
            qCDebug(log_device_windows) << "    USB 3.0 high port mapping:" << hubPort << "->" << companionPort;
        } else if (hubPort >= 10) {
            // Medium port numbers
            int companionPort = ((hubPort - 10) % 6) + 1; // Map 10-15 -> 1-6
            companionChain = QString("%1-%2").arg(rootHub).arg(companionPort);
            qCDebug(log_device_windows) << "    USB 3.0 medium port mapping:" << hubPort << "->" << companionPort;
        } else if (hubPort >= 5) {
            // Map 5-9 to 1-5 (common for some controllers)
            int companionPort = hubPort - 4;
            companionChain = QString("%1-%2").arg(rootHub).arg(companionPort);
            qCDebug(log_device_windows) << "    USB 3.0 mid-range port mapping:" << hubPort << "->" << companionPort;
        }
        
        if (!companionChain.isEmpty()) {
            qCDebug(log_device_windows) << "  Inferred companion chain:" << companionChain;
            return companionChain;
        }
    }
    
    return QString();
}

QString WindowsDeviceManager::inferCompanionChainFromPortPattern(const USBDeviceData& integratedDevice)
{
    qCDebug(log_device_windows) << "Inferring companion chain from port pattern analysis";
    
    QString portChain = integratedDevice.portChain;
    
    // Enhanced pattern-based inference using common USB 3.0 controller configurations
    // This method looks for common patterns in USB controller designs
    
    // Pattern 1: Intel USB 3.0 controllers (common pattern)
    // USB 3.0 ports 1-18 -> USB 2.0 companion ports 1-2
    QRegularExpression intelPattern(R"(^(\d+)-(\d+)$)");
    QRegularExpressionMatch intelMatch = intelPattern.match(portChain);
    
    if (intelMatch.hasMatch()) {
        QString rootHub = intelMatch.captured(1);
        int hubPort = intelMatch.captured(2).toInt();
        
        // Intel-style mapping (commonly seen in many systems)
        if (hubPort >= 17) {
            return QString("%1-2").arg(rootHub); // High ports -> port 2
        } else if (hubPort >= 9) {
            return QString("%1-1").arg(rootHub); // Medium ports -> port 1
        }
    }
    
    // Pattern 2: AMD USB 3.0 controllers (alternative pattern)
    // Different manufacturers may have different port mapping schemes
    
    // Pattern 3: Generic USB 3.0 hub pattern
    // Many USB 3.0 hubs use a pattern where the companion port is calculated as:
    // companion_port = (usb3_port - 1) / N + 1, where N is typically 2-4
    
    if (intelMatch.hasMatch()) {
        QString rootHub = intelMatch.captured(1);
        int hubPort = intelMatch.captured(2).toInt();
        
        // Generic calculation: divide high port numbers into groups
        if (hubPort >= 8) {
            int companionPort = ((hubPort - 1) / 8) + 1;
            if (companionPort <= 4) { // Reasonable companion port range
                QString result = QString("%1-%2").arg(rootHub).arg(companionPort);
                qCDebug(log_device_windows) << "  Generic pattern mapping:" << hubPort << "->" << companionPort;
                return result;
            }
        }
    }
    
    qCDebug(log_device_windows) << "  No pattern-based companion chain found";
    return QString();
}

QString WindowsDeviceManager::findCameraDevicePathByDeviceId(const QString& deviceId)
{
    qCDebug(log_device_windows) << "Finding camera path for device ID:" << deviceId;
    
    // Use the same approach as enumerateDevicesByClassWithParentInfo for consistency
    QList<QVariantMap> cameras = enumerateDevicesByClassWithParentInfo(GUID_DEVCLASS_CAMERA);
    
    for (const QVariantMap& camera : cameras) {
        QString cameraDeviceId = camera.value("deviceId").toString();
        
        qCDebug(log_device_windows) << "  Checking camera - Device ID:" << cameraDeviceId;
        
        // Validate that this is a camera device with correct VID:PID
        // Must be one of: 345F:2109, 345F:2132, or 534D:2109
        bool isValidCameraVidPid = 
            (cameraDeviceId.contains("345F", Qt::CaseInsensitive) && 
             (cameraDeviceId.contains("2109", Qt::CaseInsensitive) || 
              cameraDeviceId.contains("2132", Qt::CaseInsensitive))) ||
            (cameraDeviceId.contains("534D", Qt::CaseInsensitive) && 
             cameraDeviceId.contains("2109", Qt::CaseInsensitive));
        
        if (!isValidCameraVidPid) {
            qCDebug(log_device_windows) << "  ✗ Camera device does not have valid VID:PID (must be 345F:2109, 345F:2132, or 534D:2109)";
            continue;
        }
        
        // Check if this device matches our target device ID (exact match or contains)
        if (cameraDeviceId.compare(deviceId, Qt::CaseInsensitive) == 0 ||
            cameraDeviceId.contains(deviceId, Qt::CaseInsensitive) ||
            deviceId.contains(cameraDeviceId, Qt::CaseInsensitive)) {
            
            QString devicePath = camera.value("devicePath").toString();
            
            qCDebug(log_device_windows) << "  ✓ Found matching camera device with path:" << devicePath;
            return devicePath;
        }
    }
    
    qCDebug(log_device_windows) << "Could not find camera path for device ID:" << deviceId;
    
    // Fallback: Return a simplified device identifier that can be used for matching
    // The camera manager can use this for device identification
    if (!deviceId.isEmpty()) {
        qCDebug(log_device_windows) << "Returning device ID as fallback path:" << deviceId;
        return deviceId;
    }
    
    return QString();
}

QString WindowsDeviceManager::findAudioDevicePathByDeviceId(const QString& deviceId)
{
    qCDebug(log_device_windows) << "Finding audio path for device ID:" << deviceId;
    
    // For audio devices, we typically return a symbolic name rather than a device path
    // since audio devices are accessed differently than cameras
    
    // Enumerate audio devices and find matching device ID
    HDEVINFO hDevInfo = SetupDiGetClassDevs(&GUID_DEVCLASS_MEDIA, nullptr, nullptr, DIGCF_PRESENT);
    if (hDevInfo == INVALID_HANDLE_VALUE) {
        qCWarning(log_device_windows) << "Failed to get audio device list";
        return QString();
    }
    
    SP_DEVINFO_DATA devInfoData;
    devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
    
    for (DWORD index = 0; SetupDiEnumDeviceInfo(hDevInfo, index, &devInfoData); index++) {
        QString currentDeviceId = getDeviceId(devInfoData.DevInst);
        
        // Check if this device matches our target device ID
        if (currentDeviceId.compare(deviceId, Qt::CaseInsensitive) == 0 ||
            currentDeviceId.contains(deviceId, Qt::CaseInsensitive)) {
            
            // Get the friendly name for audio devices
            QString friendlyName = getDeviceProperty(hDevInfo, &devInfoData, SPDRP_FRIENDLYNAME);
            SetupDiDestroyDeviceInfoList(hDevInfo);
            
            qCDebug(log_device_windows) << "Found audio device:" << friendlyName;
            return friendlyName;
        }
    }
    
    SetupDiDestroyDeviceInfoList(hDevInfo);
    qCDebug(log_device_windows) << "Could not find audio device for device ID:" << deviceId;
    return QString();
}

bool WindowsDeviceManager::isDeviceRelatedToPortChain(const QString& deviceId, const QString& portChain)
{
    qCDebug(log_device_windows) << "Checking if device is related to port chain";
    qCDebug(log_device_windows) << "  Device ID:" << deviceId;
    qCDebug(log_device_windows) << "  Port chain:" << portChain;
    
    if (deviceId.isEmpty() || portChain.isEmpty()) {
        return false;
    }
    
    // Get device instance from ID
    DWORD deviceInst = getDeviceInstanceFromId(deviceId);
    if (deviceInst == 0) {
        return false;
    }
    
    // Build port chain for this device
    QString devicePortChain = buildPythonCompatiblePortChain(deviceInst);
    qCDebug(log_device_windows) << "  Device port chain:" << devicePortChain;
    
    // Check for exact match
    if (devicePortChain == portChain) {
        qCDebug(log_device_windows) << "  ✓ Exact port chain match";
        return true;
    }
    
    // Check for related port chains (same hub, different ports)
    if (arePortChainsRelated(devicePortChain, portChain)) {
        qCDebug(log_device_windows) << "  ✓ Related port chains";
        return true;
    }
    
    // Check parent device relationships
    QString parentChain = extractParentPortChain(devicePortChain);
    QString targetParentChain = extractParentPortChain(portChain);
    
    if (!parentChain.isEmpty() && !targetParentChain.isEmpty() && parentChain == targetParentChain) {
        qCDebug(log_device_windows) << "  ✓ Same parent hub";
        return true;
    }
    
    qCDebug(log_device_windows) << "  ✗ No relationship found";
    return false;
}

#endif // _WIN32
