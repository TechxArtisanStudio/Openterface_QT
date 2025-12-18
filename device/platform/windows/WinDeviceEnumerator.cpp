#ifdef _WIN32
#include "WinDeviceEnumerator.h"
#include <QDebug>
#include <vector>
#include <initguid.h>
#include <devguid.h>
#include <objbase.h>
#include <hidclass.h>

extern "C"
{
#include <hidsdi.h>
}

// USB Device Interface GUID definition
DEFINE_GUID(GUID_DEVINTERFACE_USB_DEVICE, 
    0xA5DCBF10, 0x6530, 0x11D2, 0x90, 0x1F, 0x00, 0xC0, 0x4F, 0xB9, 0x51, 0xED);

// Audio Interface GUID
DEFINE_GUID(GUID_DEVINTERFACE_AUDIO,
    0x33D9A762, 0x90C8, 0x11D0, 0xBD, 0x43, 0x00, 0xA0, 0xC9, 0x11, 0xCE, 0x86);

// Camera Interface GUID (KSCATEGORY_VIDEO_CAMERA)
DEFINE_GUID(GUID_DEVINTERFACE_CAMERA_KSCATEGORY,
    0x65E8773D, 0x8F56, 0x11D0, 0xA3, 0xB9, 0x00, 0xA0, 0xC9, 0x22, 0x31, 0x96);

// HID Interface GUID is already defined in hidclass.h

Q_LOGGING_CATEGORY(log_win_enumerator, "opf.host.windows.enumerator")

WinDeviceEnumerator::WinDeviceEnumerator(QObject* parent)
    : QObject(parent)
{
    qCDebug(log_win_enumerator) << "Windows Device Enumerator initialized";
}

WinDeviceEnumerator::~WinDeviceEnumerator()
{
    qCDebug(log_win_enumerator) << "Windows Device Enumerator destroyed";
}

QVector<QVariantMap> WinDeviceEnumerator::enumerateDevicesByClass(const GUID& classGuid)
{
    QVector<QVariantMap> devices;
    
    HDEVINFO hDevInfo = SetupDiGetClassDevs(&classGuid, nullptr, nullptr, DIGCF_PRESENT);
    if (hDevInfo == INVALID_HANDLE_VALUE) {
        qCWarning(log_win_enumerator) << "Failed to get device class list";
        return devices;
    }
    
    SP_DEVINFO_DATA devInfoData;
    devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
    
    for (DWORD i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &devInfoData); i++) {
        QVariantMap deviceMap = getDeviceInfo(devInfoData.DevInst);
        if (!deviceMap.isEmpty()) {
            devices.append(deviceMap);
        }
    }
    
    SetupDiDestroyDeviceInfoList(hDevInfo);
    return devices;
}

QVector<QVariantMap> WinDeviceEnumerator::enumerateDevicesByClassWithParentInfo(const GUID& classGuid)
{
    QVector<QVariantMap> devices;
    
    HDEVINFO hDevInfo = SetupDiGetClassDevs(&classGuid, nullptr, nullptr, DIGCF_PRESENT);
    if (hDevInfo == INVALID_HANDLE_VALUE) {
        qCWarning(log_win_enumerator) << "Failed to get device class list with parent info";
        return devices;
    }
    
    SP_DEVINFO_DATA devInfoData;
    devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
    
    for (DWORD i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &devInfoData); i++) {
        QVariantMap deviceMap = getDeviceInfo(devInfoData.DevInst);
        
        if (!deviceMap.isEmpty()) {
            // Add parent device information
            DWORD parentDevInst;
            if (CM_Get_Parent(&parentDevInst, devInfoData.DevInst, 0) == CR_SUCCESS) {
                QString parentDeviceId = getDeviceId(parentDevInst);
                deviceMap["parentDeviceId"] = parentDeviceId;
                deviceMap["parentDevInst"] = static_cast<uint>(parentDevInst);
            }
            
            // Add location information
            WCHAR locationInfo[256];
            ULONG locationInfoSize = sizeof(locationInfo);
            if (CM_Get_DevNode_Registry_Property(devInfoData.DevInst, CM_DRP_LOCATION_INFORMATION, 
                                                nullptr, locationInfo, &locationInfoSize, 0) == CR_SUCCESS) {
                deviceMap["locationInformation"] = wideToQString(locationInfo);
            }
            
            devices.append(deviceMap);
        }
    }
    
    SetupDiDestroyDeviceInfoList(hDevInfo);
    return devices;
}

QVariantMap WinDeviceEnumerator::getDeviceInfo(DWORD devInst)
{
    QVariantMap deviceInfo;
    
    QString deviceId = getDeviceId(devInst);
    deviceInfo["deviceId"] = deviceId;
    deviceInfo["devInst"] = static_cast<uint>(devInst);
    
    // Get device class GUID
    GUID classGuid;
    ULONG classGuidSize = sizeof(classGuid);
    if (CM_Get_DevNode_Registry_Property(devInst, CM_DRP_CLASSGUID, nullptr, 
                                        &classGuid, &classGuidSize, 0) == CR_SUCCESS) {
        WCHAR guidString[256];
        StringFromGUID2(classGuid, guidString, 256);
        deviceInfo["classGuid"] = wideToQString(guidString);
    }
    
    // Get device class name
    WCHAR className[256];
    ULONG classNameSize = sizeof(className);
    if (CM_Get_DevNode_Registry_Property(devInst, CM_DRP_CLASS, nullptr,
                                        className, &classNameSize, 0) == CR_SUCCESS) {
        deviceInfo["className"] = wideToQString(className);
    }
    
    // Get hardware ID
    WCHAR hardwareId[1024];
    ULONG hardwareIdSize = sizeof(hardwareId);
    if (CM_Get_DevNode_Registry_Property(devInst, CM_DRP_HARDWAREID, nullptr,
                                        hardwareId, &hardwareIdSize, 0) == CR_SUCCESS) {
        deviceInfo["hardwareId"] = wideToQString(hardwareId);
    }
    
    // Get friendly name
    WCHAR friendlyName[256];
    ULONG friendlyNameSize = sizeof(friendlyName);
    if (CM_Get_DevNode_Registry_Property(devInst, CM_DRP_FRIENDLYNAME, nullptr,
                                        friendlyName, &friendlyNameSize, 0) == CR_SUCCESS) {
        deviceInfo["friendlyName"] = wideToQString(friendlyName);
    }
    
    // Get device description
    WCHAR description[256];
    ULONG descriptionSize = sizeof(description);
    if (CM_Get_DevNode_Registry_Property(devInst, CM_DRP_DEVICEDESC, nullptr,
                                        description, &descriptionSize, 0) == CR_SUCCESS) {
        deviceInfo["description"] = wideToQString(description);
    }
    
    return deviceInfo;
}

QString WinDeviceEnumerator::getDeviceId(DWORD devInst)
{
    WCHAR deviceId[MAX_PATH];
    if (CM_Get_Device_ID(devInst, deviceId, MAX_PATH, 0) == CR_SUCCESS) {
        return wideToQString(deviceId);
    }
    return QString();
}

QString WinDeviceEnumerator::getHardwareId(HDEVINFO hDevInfo, SP_DEVINFO_DATA* devInfoData)
{
    return getDeviceProperty(hDevInfo, devInfoData, SPDRP_HARDWAREID);
}

QString WinDeviceEnumerator::getDeviceProperty(HDEVINFO hDevInfo, SP_DEVINFO_DATA* devInfoData, DWORD property)
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
        if (dataType == REG_SZ || dataType == REG_MULTI_SZ) {
            return wideToQString(reinterpret_cast<const wchar_t*>(buffer.data()));
        } else if (dataType == REG_DWORD) {
            DWORD value = *reinterpret_cast<DWORD*>(buffer.data());
            return QString::number(value);
        }
    }
    
    return QString();
}

QString WinDeviceEnumerator::getDevicePropertyByName(DWORD devInst, const QString& propertyName)
{
    if (devInst == 0) {
        return QString();
    }
    
    ULONG bufferSize = 1024;
    wchar_t buffer[1024];
    
    if (propertyName == "LocationInformation") {
        if (CM_Get_DevNode_Registry_Property(devInst, CM_DRP_LOCATION_INFORMATION, nullptr,
                                            buffer, &bufferSize, 0) == CR_SUCCESS) {
            return wideToQString(buffer);
        }
    } else if (propertyName == "FriendlyName") {
        if (CM_Get_DevNode_Registry_Property(devInst, CM_DRP_FRIENDLYNAME, nullptr,
                                            buffer, &bufferSize, 0) == CR_SUCCESS) {
            return wideToQString(buffer);
        }
    } else if (propertyName == "HardwareID") {
        if (CM_Get_DevNode_Registry_Property(devInst, CM_DRP_HARDWAREID, nullptr,
                                            buffer, &bufferSize, 0) == CR_SUCCESS) {
            return wideToQString(buffer);
        }
    }
    
    return QString();
}

QVector<QVariantMap> WinDeviceEnumerator::getChildDevices(DWORD devInst)
{
    QVector<QVariantMap> children;
    
    // Get first child
    DWORD childDevInst;
    if (CM_Get_Child(&childDevInst, devInst, 0) != CR_SUCCESS) {
        return children;
    }
    
    // Process first child
    children.append(getDeviceInfo(childDevInst));
    
    // Process siblings of first child
    DWORD currentChild = childDevInst;
    while (CM_Get_Sibling(&currentChild, currentChild, 0) == CR_SUCCESS) {
        children.append(getDeviceInfo(currentChild));
    }
    
    return children;
}

QVector<QVariantMap> WinDeviceEnumerator::getAllChildDevices(DWORD parentDevInst)
{
    QVector<QVariantMap> allChildren;
    
    // Get first child
    DWORD childDevInst;
    if (CM_Get_Child(&childDevInst, parentDevInst, 0) != CR_SUCCESS) {
        return allChildren;
    }
    
    // Process first child and all its siblings
    DWORD currentChild = childDevInst;
    do {
        QVariantMap childInfo = getDeviceInfo(currentChild);
        if (!childInfo.isEmpty()) {
            allChildren.append(childInfo);
        }
        
        // Recursively get children of this child
        QVector<QVariantMap> grandChildren = getAllChildDevices(currentChild);
        if (!grandChildren.isEmpty()) {
            allChildren.append(grandChildren);
        }
    } while (CM_Get_Sibling(&currentChild, currentChild, 0) == CR_SUCCESS);
    
    return allChildren;
}

QVector<QVariantMap> WinDeviceEnumerator::getSiblingDevicesByParent(DWORD parentDevInst)
{
    QVector<QVariantMap> siblings;
    
    // Enumerate all devices to find siblings with the same parent
    HDEVINFO hDevInfo = SetupDiGetClassDevs(nullptr, nullptr, nullptr, DIGCF_PRESENT | DIGCF_ALLCLASSES);
    if (hDevInfo == INVALID_HANDLE_VALUE) {
        return siblings;
    }
    
    SP_DEVINFO_DATA devInfoData;
    devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
    
    for (DWORD index = 0; SetupDiEnumDeviceInfo(hDevInfo, index, &devInfoData); index++) {
        DWORD deviceParentInst;
        if (CM_Get_Parent(&deviceParentInst, devInfoData.DevInst, 0) == CR_SUCCESS) {
            if (deviceParentInst == parentDevInst) {
                siblings.append(getDeviceInfo(devInfoData.DevInst));
            }
        }
    }
    
    SetupDiDestroyDeviceInfoList(hDevInfo);
    return siblings;
}

QString WinDeviceEnumerator::buildPortChain(DWORD devInst)
{
    QStringList portChainList;
    
    DWORD currentInst = devInst;
    DWORD parentInst;
    int depth = 0;
    const int maxDepth = 10;
    
    // Walk up the device tree to build port chain
    while (CM_Get_Parent(&parentInst, currentInst, 0) == CR_SUCCESS && depth < maxDepth) {
        // Get location information which often contains port numbers
        WCHAR locationInfo[256];
        ULONG locationInfoSize = sizeof(locationInfo);
        if (CM_Get_DevNode_Registry_Property(currentInst, CM_DRP_LOCATION_INFORMATION, 
                                            nullptr, locationInfo, &locationInfoSize, 0) == CR_SUCCESS) {
            QString location = wideToQString(locationInfo);
            // Extract port number from location string if present
            if (location.contains("Port_#")) {
                QString portNum = location.split("Port_#").last().split(".").first();
                if (!portNum.isEmpty()) {
                    portChainList.prepend(portNum);
                }
            }
        }
        
        currentInst = parentInst;
        depth++;
    }
    
    return portChainList.join("-");
}

DWORD WinDeviceEnumerator::getDeviceInstanceFromId(const QString& deviceId)
{
    if (deviceId.isEmpty()) {
        return 0;
    }
    
    DWORD devInst;
    std::wstring wDeviceId = deviceId.toStdWString();
    
    if (CM_Locate_DevNode(&devInst, const_cast<DEVINSTID>(wDeviceId.c_str()), CM_LOCATE_DEVNODE_NORMAL) == CR_SUCCESS) {
        return devInst;
    }
    
    return 0;
}

DWORD WinDeviceEnumerator::getDeviceInstanceFromPortChain(const QString& portChain)
{
    qCDebug(log_win_enumerator) << "Getting device instance from port chain:" << portChain;
    
    // Enumerate all USB devices and find one that has a matching port chain
    HDEVINFO hDevInfo = SetupDiGetClassDevs(&GUID_DEVCLASS_USB, nullptr, nullptr, DIGCF_PRESENT);
    if (hDevInfo == INVALID_HANDLE_VALUE) {
        return 0;
    }
    
    SP_DEVINFO_DATA devInfoData;
    devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
    
    for (DWORD i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &devInfoData); i++) {
        QString devicePortChain = buildPortChain(devInfoData.DevInst);
        if (devicePortChain == portChain) {
            SetupDiDestroyDeviceInfoList(hDevInfo);
            return devInfoData.DevInst;
        }
    }
    
    SetupDiDestroyDeviceInfoList(hDevInfo);
    return 0;
}

DWORD WinDeviceEnumerator::getParentDevice(DWORD devInst)
{
    DWORD parentDevInst;
    if (CM_Get_Parent(&parentDevInst, devInst, 0) == CR_SUCCESS) {
        return parentDevInst;
    }
    return 0;
}

QString WinDeviceEnumerator::getDeviceInterfacePath(HDEVINFO hDevInfo, SP_DEVINFO_DATA* devInfoData, const GUID& interfaceGuid)
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
        SetupDiGetDeviceInterfaceDetail(hInterfaceDevInfo, &interfaceData, nullptr, 0, &requiredSize, nullptr);
        
        if (requiredSize > 0) {
            std::vector<BYTE> buffer(requiredSize);
            PSP_DEVICE_INTERFACE_DETAIL_DATA detailData = reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA>(buffer.data());
            detailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);
            
            SP_DEVINFO_DATA interfaceDevInfoData;
            interfaceDevInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
            
            if (SetupDiGetDeviceInterfaceDetail(hInterfaceDevInfo, &interfaceData, detailData, 
                                               requiredSize, nullptr, &interfaceDevInfoData)) {
                QString interfaceDeviceId = getDeviceId(interfaceDevInfoData.DevInst);
                if (interfaceDeviceId == targetDeviceId) {
                    QString path = wideToQString(detailData->DevicePath);
                    SetupDiDestroyDeviceInfoList(hInterfaceDevInfo);
                    return path;
                }
            }
        }
    }
    
    SetupDiDestroyDeviceInfoList(hInterfaceDevInfo);
    return QString();
}

QString WinDeviceEnumerator::findHIDDevicePathByDeviceId(const QString& deviceId)
{
    GUID hidGuid;
    HidD_GetHidGuid(&hidGuid);
    
    HDEVINFO hDevInfo = SetupDiGetClassDevs(&hidGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (hDevInfo == INVALID_HANDLE_VALUE) {
        return QString();
    }
    
    SP_DEVICE_INTERFACE_DATA interfaceData;
    interfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);
    
    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(hDevInfo, nullptr, &hidGuid, i, &interfaceData); i++) {
        DWORD requiredSize = 0;
        SetupDiGetDeviceInterfaceDetail(hDevInfo, &interfaceData, nullptr, 0, &requiredSize, nullptr);
        
        if (requiredSize > 0) {
            std::vector<BYTE> buffer(requiredSize);
            PSP_DEVICE_INTERFACE_DETAIL_DATA detailData = reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA>(buffer.data());
            detailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);
            
            SP_DEVINFO_DATA devInfoData;
            devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
            
            if (SetupDiGetDeviceInterfaceDetail(hDevInfo, &interfaceData, detailData, 
                                               requiredSize, nullptr, &devInfoData)) {
                QString currentDeviceId = getDeviceId(devInfoData.DevInst);
                if (currentDeviceId == deviceId) {
                    QString path = wideToQString(detailData->DevicePath);
                    SetupDiDestroyDeviceInfoList(hDevInfo);
                    return path;
                }
            }
        }
    }
    
    SetupDiDestroyDeviceInfoList(hDevInfo);
    return QString();
}

QString WinDeviceEnumerator::findCameraDevicePathByDeviceId(const QString& deviceId)
{
    qCDebug(log_win_enumerator) << "Finding camera path for device ID:" << deviceId;
    
    QVector<QVariantMap> cameras = enumerateDevicesByClassWithParentInfo(GUID_DEVCLASS_CAMERA);
    
    for (const QVariantMap& camera : cameras) {
        QString cameraDeviceId = camera.value("deviceId").toString();
        if (cameraDeviceId == deviceId) {
            // Return a symbolic name or identifier for the camera
            QString friendlyName = camera.value("friendlyName").toString();
            if (!friendlyName.isEmpty()) {
                return friendlyName;
            }
            return cameraDeviceId;
        }
    }
    
    if (!deviceId.isEmpty()) {
        return deviceId; // Fallback to device ID for matching
    }
    
    return QString();
}

QString WinDeviceEnumerator::findAudioDevicePathByDeviceId(const QString& deviceId)
{
    qCDebug(log_win_enumerator) << "Finding audio path for device ID:" << deviceId;
    
    // Handle MMDEVAPI devices (software audio endpoints)
    if (deviceId.startsWith("SWD\\MMDEVAPI", Qt::CaseInsensitive)) {
        qCDebug(log_win_enumerator) << "MMDEVAPI device detected, returning device ID as path:" << deviceId;
        return deviceId;
    }
    
    HDEVINFO hDevInfo = SetupDiGetClassDevs(&GUID_DEVCLASS_MEDIA, nullptr, nullptr, DIGCF_PRESENT);
    if (hDevInfo == INVALID_HANDLE_VALUE) {
        return QString();
    }
    
    SP_DEVINFO_DATA devInfoData;
    devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
    
    for (DWORD index = 0; SetupDiEnumDeviceInfo(hDevInfo, index, &devInfoData); index++) {
        QString currentDeviceId = getDeviceId(devInfoData.DevInst);
        if (currentDeviceId == deviceId) {
            QString friendlyName = getDeviceProperty(hDevInfo, &devInfoData, SPDRP_FRIENDLYNAME);
            SetupDiDestroyDeviceInfoList(hDevInfo);
            return friendlyName.isEmpty() ? deviceId : friendlyName;
        }
    }
    
    SetupDiDestroyDeviceInfoList(hDevInfo);
    return QString();
}

QString WinDeviceEnumerator::findComPortByDeviceId(const QString& deviceId)
{
    qCDebug(log_win_enumerator) << "Finding COM port for device ID:" << deviceId;
    
    HDEVINFO hDevInfo = SetupDiGetClassDevs(&GUID_DEVCLASS_PORTS, nullptr, nullptr, DIGCF_PRESENT);
    if (hDevInfo == INVALID_HANDLE_VALUE) {
        return QString();
    }
    
    SP_DEVINFO_DATA devInfoData;
    devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
    
    for (DWORD i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &devInfoData); i++) {
        QString currentDeviceId = getDeviceId(devInfoData.DevInst);
        
        if (currentDeviceId == deviceId) {
            // Get the COM port name from the registry
            HKEY hKey = SetupDiOpenDevRegKey(hDevInfo, &devInfoData, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
            if (hKey != INVALID_HANDLE_VALUE) {
                WCHAR portName[256];
                DWORD portNameSize = sizeof(portName);
                
                if (RegQueryValueEx(hKey, L"PortName", nullptr, nullptr, 
                                   reinterpret_cast<LPBYTE>(portName), &portNameSize) == ERROR_SUCCESS) {
                    QString comPort = wideToQString(portName);
                    RegCloseKey(hKey);
                    SetupDiDestroyDeviceInfoList(hDevInfo);
                    return comPort;
                }
                RegCloseKey(hKey);
            }
        }
    }
    
    SetupDiDestroyDeviceInfoList(hDevInfo);
    return QString();
}

QVector<QVariantMap> WinDeviceEnumerator::enumerateDevicesByInterface(const GUID& interfaceGuid)
{
    qCDebug(log_win_enumerator) << "Enumerating devices by interface GUID";
    
    QVector<QVariantMap> devices;
    
    HDEVINFO hDevInfo = SetupDiGetClassDevs(&interfaceGuid, nullptr, nullptr, 
                                            DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (hDevInfo == INVALID_HANDLE_VALUE) {
        return devices;
    }
    
    SP_DEVINFO_DATA devInfoData;
    devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
    
    for (DWORD i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &devInfoData); i++) {
        QVariantMap deviceInfo = getDeviceInfo(devInfoData.DevInst);
        devices.append(deviceInfo);
    }
    
    SetupDiDestroyDeviceInfoList(hDevInfo);
    qCDebug(log_win_enumerator) << "Found" << devices.size() << "devices";
    return devices;
}

QVector<QVariantMap> WinDeviceEnumerator::enumerateAllDevices()
{
    qCDebug(log_win_enumerator) << "Enumerating all devices from all classes";
    
    QVector<QVariantMap> allDevices;
    
    // Enumerate devices from all relevant device classes
    const GUID deviceClasses[] = {
        GUID_DEVCLASS_USB,
        GUID_DEVCLASS_PORTS,
        GUID_DEVCLASS_HIDCLASS,
        GUID_DEVCLASS_CAMERA,
        GUID_DEVCLASS_MEDIA
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
            
            // Get parent device information
            DWORD parentDevInst;
            if (CM_Get_Parent(&parentDevInst, devInfoData.DevInst, 0) == CR_SUCCESS) {
                QString parentDeviceId = getDeviceId(parentDevInst);
                deviceInfo["parentDeviceId"] = parentDeviceId;
                deviceInfo["parentDevInst"] = static_cast<uint>(parentDevInst);
            }
            
            allDevices.append(deviceInfo);
        }
        
        SetupDiDestroyDeviceInfoList(hDevInfo);
    }
    
    qCDebug(log_win_enumerator) << "Enumerated" << allDevices.size() << "devices from all classes";
    return allDevices;
}

QVector<QVariantMap> WinDeviceEnumerator::getChildDevicesPython(DWORD devInst)
{
    qCDebug(log_win_enumerator) << "Getting child devices (Python-compatible) for device instance:" << devInst;
    
    QVector<QVariantMap> children;
    
    // Get first child
    DWORD childDevInst;
    if (CM_Get_Child(&childDevInst, devInst, 0) == CR_SUCCESS) {
        while (true) {
            QVariantMap childInfo = getDeviceInfo(childDevInst);
            children.append(childInfo);
            
            // Get next sibling
            DWORD nextSibling;
            if (CM_Get_Sibling(&nextSibling, childDevInst, 0) != CR_SUCCESS) {
                break;
            }
            childDevInst = nextSibling;
        }
    }
    
    qCDebug(log_win_enumerator) << "Found" << children.size() << "child devices";
    return children;
}

QString WinDeviceEnumerator::findHidDeviceForPortChain(const QString& portChain)
{
    qCDebug(log_win_enumerator) << "Searching for HID device with port chain:" << portChain;
    
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
        // Get device instance for this interface
        SP_DEVINFO_DATA devInfoData;
        devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
        
        DWORD requiredSize = 0;
        SetupDiGetDeviceInterfaceDetail(hDevInfo, &interfaceData, nullptr, 0, &requiredSize, &devInfoData);
        
        if (requiredSize > 0) {
            QVector<BYTE> buffer(requiredSize);
            PSP_DEVICE_INTERFACE_DETAIL_DATA detailData = 
                reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA>(buffer.data());
            detailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);
            
            if (SetupDiGetDeviceInterfaceDetail(hDevInfo, &interfaceData, detailData, 
                                               requiredSize, nullptr, &devInfoData)) {
                QString devicePortChain = buildPortChain(devInfoData.DevInst);
                if (devicePortChain.startsWith(portChain)) {
                    QString devicePath = wideToQString(detailData->DevicePath);
                    SetupDiDestroyDeviceInfoList(hDevInfo);
                    qCDebug(log_win_enumerator) << "Found HID device:" << devicePath;
                    return devicePath;
                }
            }
        }
    }
    
    SetupDiDestroyDeviceInfoList(hDevInfo);
    return QString();
}

QString WinDeviceEnumerator::getPortChainForSerialPort(const QString& portName)
{
    qCDebug(log_win_enumerator) << "Getting port chain for serial port:" << portName;
    
    // Find the device instance for this COM port and build its port chain
    HDEVINFO hDevInfo = SetupDiGetClassDevs(&GUID_DEVCLASS_PORTS, nullptr, nullptr, DIGCF_PRESENT);
    if (hDevInfo == INVALID_HANDLE_VALUE) {
        return QString();
    }
    
    SP_DEVINFO_DATA devInfoData;
    devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
    
    for (DWORD i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &devInfoData); i++) {
        // Get the COM port name from the registry
        HKEY hKey = SetupDiOpenDevRegKey(hDevInfo, &devInfoData, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
        if (hKey != INVALID_HANDLE_VALUE) {
            WCHAR regPortName[256];
            DWORD regPortNameSize = sizeof(regPortName);
            
            if (RegQueryValueEx(hKey, L"PortName", nullptr, nullptr, 
                               reinterpret_cast<LPBYTE>(regPortName), &regPortNameSize) == ERROR_SUCCESS) {
                QString comPort = wideToQString(regPortName);
                if (comPort == portName) {
                    QString portChain = buildPortChain(devInfoData.DevInst);
                    RegCloseKey(hKey);
                    SetupDiDestroyDeviceInfoList(hDevInfo);
                    qCDebug(log_win_enumerator) << "Found port chain:" << portChain;
                    return portChain;
                }
            }
            RegCloseKey(hKey);
        }
    }
    
    SetupDiDestroyDeviceInfoList(hDevInfo);
    return QString();
}

bool WinDeviceEnumerator::getDevNodeProperty(DWORD devInst, ULONG property, void* buffer, ULONG* bufferSize)
{
    return CM_Get_DevNode_Registry_Property(devInst, property, nullptr, buffer, bufferSize, 0) == CR_SUCCESS;
}

QString WinDeviceEnumerator::wideToQString(const wchar_t* wstr)
{
    if (!wstr) {
        return QString();
    }
    return QString::fromWCharArray(wstr);
}

QString WinDeviceEnumerator::findDeviceInterfacePathByDevInst(DWORD devInst, const GUID& interfaceGuid)
{
    qCDebug(log_win_enumerator) << "Finding device interface path for devInst:" << devInst;
    
    // Get the device interface path by enumerating interfaces
    HDEVINFO hDevInfo = SetupDiGetClassDevs(&interfaceGuid, nullptr, nullptr, 
                                            DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (hDevInfo == INVALID_HANDLE_VALUE) {
        qCWarning(log_win_enumerator) << "Failed to get device interfaces";
        return QString();
    }
    
    SP_DEVICE_INTERFACE_DATA interfaceData;
    interfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);
    
    // Enumerate all device interfaces
    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(hDevInfo, nullptr, &interfaceGuid, i, &interfaceData); i++) {
        // Get the device instance for this interface
        SP_DEVINFO_DATA devInfoData;
        devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
        
        DWORD requiredSize = 0;
        SetupDiGetDeviceInterfaceDetail(hDevInfo, &interfaceData, nullptr, 0, &requiredSize, &devInfoData);
        
        if (requiredSize > 0) {
            std::vector<BYTE> buffer(requiredSize);
            PSP_DEVICE_INTERFACE_DETAIL_DATA detailData = 
                reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA>(buffer.data());
            detailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);
            
            if (SetupDiGetDeviceInterfaceDetail(hDevInfo, &interfaceData, detailData, 
                                               requiredSize, nullptr, &devInfoData)) {
                // Check if this is our target device instance
                if (devInfoData.DevInst == devInst) {
                    QString devicePath = wideToQString(detailData->DevicePath);
                    SetupDiDestroyDeviceInfoList(hDevInfo);
                    qCDebug(log_win_enumerator) << "Found device interface path:" << devicePath;
                    return devicePath;
                }
                
                // Also check parent device instance (for composite devices)
                DWORD parentDevInst = getParentDevice(devInfoData.DevInst);
                if (parentDevInst == devInst) {
                    QString devicePath = wideToQString(detailData->DevicePath);
                    SetupDiDestroyDeviceInfoList(hDevInfo);
                    qCDebug(log_win_enumerator) << "Found device interface path (via parent):" << devicePath;
                    return devicePath;
                }
            }
        }
    }
    
    SetupDiDestroyDeviceInfoList(hDevInfo);
    qCDebug(log_win_enumerator) << "No device interface path found for devInst:" << devInst;
    return QString();
}

QMap<QString, QString> WinDeviceEnumerator::getAllInterfacePathsForDevice(DWORD devInst)
{
    QMap<QString, QString> interfacePaths;
    
    qCDebug(log_win_enumerator) << "Getting all interface paths for device:" << devInst;
    
    // Get all child devices
    QVector<QVariantMap> children = getAllChildDevices(devInst);
    
    for (const QVariantMap& child : children) {
        DWORD childDevInst = child.value("devInst").toUInt();
        QString hardwareId = child.value("hardwareId").toString().toUpper();
        QString deviceClass = child.value("className").toString();
        
        qCDebug(log_win_enumerator) << "  Checking child:" << child.value("deviceId").toString();
        qCDebug(log_win_enumerator) << "    Class:" << deviceClass;
        qCDebug(log_win_enumerator) << "    Hardware ID:" << hardwareId;
        
        // Check for HID interface (MI_04)
        if (hardwareId.contains("HID") || hardwareId.contains("MI_04")) {
            QString hidPath = findDeviceInterfacePathByDevInst(childDevInst, GUID_DEVINTERFACE_HID);
            if (!hidPath.isEmpty()) {
                interfacePaths["HID"] = hidPath;
                qCDebug(log_win_enumerator) << "    ✓ Found HID path:" << hidPath;
            }
        }
        
        // Check for Camera interface (MI_00)
        if (hardwareId.contains("MI_00") || deviceClass.contains("Camera", Qt::CaseInsensitive)) {
            QString cameraPath = findDeviceInterfacePathByDevInst(childDevInst, GUID_DEVINTERFACE_CAMERA_KSCATEGORY);
            if (!cameraPath.isEmpty()) {
                interfacePaths["Camera"] = cameraPath;
                qCDebug(log_win_enumerator) << "    ✓ Found Camera path:" << cameraPath;
            }
        }
        
        // Check for Audio interface (MI_01)
        if (hardwareId.contains("AUDIO") || hardwareId.contains("MI_01")) {
            QString audioPath = findDeviceInterfacePathByDevInst(childDevInst, GUID_DEVINTERFACE_AUDIO);
            if (!audioPath.isEmpty()) {
                interfacePaths["Audio"] = audioPath;
                qCDebug(log_win_enumerator) << "    ✓ Found Audio path:" << audioPath;
            }
        }
    }
    
    qCDebug(log_win_enumerator) << "Found" << interfacePaths.size() << "interface paths";
    return interfacePaths;
}

#endif // _WIN32
