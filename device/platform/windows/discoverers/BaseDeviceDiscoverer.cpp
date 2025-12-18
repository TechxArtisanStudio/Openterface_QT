#ifdef _WIN32
#include "BaseDeviceDiscoverer.h"
#include <QDebug>
#include <QRegularExpression>
#include <QSerialPortInfo>
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

Q_LOGGING_CATEGORY(log_device_discoverer, "opf.host.windows.discoverer")

BaseDeviceDiscoverer::BaseDeviceDiscoverer(std::shared_ptr<IDeviceEnumerator> enumerator, QObject* parent)
    : QObject(parent), m_enumerator(enumerator)
{
    if (!m_enumerator) {
        qCWarning(log_device_discoverer) << "Enumerator is null in BaseDeviceDiscoverer";
    }
}

QVector<BaseDeviceDiscoverer::USBDeviceData> BaseDeviceDiscoverer::findUSBDevicesWithVidPid(const QString& vid, const QString& pid)
{
    QVector<USBDeviceData> devices;
    
    qCDebug(log_device_discoverer) << "Finding USB devices with VID:" << vid << "PID:" << pid;
    
    QString targetHwid = QString("VID_%1&PID_%2").arg(vid.toUpper()).arg(pid.toUpper());
    qCDebug(log_device_discoverer) << "Target Hardware ID pattern:" << targetHwid;
    
    // Use enumerator to get USB devices by interface
    QVector<QVariantMap> usbDevices = m_enumerator->enumerateDevicesByInterface(GUID_DEVINTERFACE_USB_DEVICE);
    
    for (const QVariantMap& deviceMap : usbDevices) {
        QString hardwareId = deviceMap.value("hardwareId").toString();
        
        // Check if this device matches our target VID/PID
        if (hardwareId.toUpper().contains(targetHwid)) {
            qCDebug(log_device_discoverer) << "Found matching USB device:" << hardwareId;
            
            DWORD devInst = deviceMap.value("devInst").toUInt();
            
            USBDeviceData usbData;
            usbData.deviceInstanceId = deviceMap.value("deviceId").toString();
            usbData.deviceInfo = deviceMap;
            
            qCDebug(log_device_discoverer) << "Device Instance ID:" << usbData.deviceInstanceId;
            qCDebug(log_device_discoverer) << "Friendly Name:" << deviceMap.value("friendlyName").toString();
            
            // Build port chain
            usbData.portChain = buildPythonCompatiblePortChain(devInst);
            qCDebug(log_device_discoverer) << "Port Chain:" << usbData.portChain;
            
            // Get parent device for sibling enumeration
            DWORD parentDevInst = m_enumerator->getParentDevice(devInst);
            if (parentDevInst != 0) {
                usbData.siblings = getSiblingDevicesByParent(parentDevInst);
                qCDebug(log_device_discoverer) << "Found" << usbData.siblings.size() << "sibling devices";
            }
            
            // Get child devices
            usbData.children = getChildDevicesPython(devInst);
            qCDebug(log_device_discoverer) << "Found" << usbData.children.size() << "child devices";
            
            devices.append(usbData);
        }
    }
    
    qCDebug(log_device_discoverer) << "Found" << devices.size() << "USB devices with VID/PID" << vid << "/" << pid;
    return devices;
}

QString BaseDeviceDiscoverer::buildPythonCompatiblePortChain(DWORD devInst)
{
    return m_enumerator->buildPortChain(devInst);
}

QString BaseDeviceDiscoverer::getDeviceId(DWORD devInst)
{
    return m_enumerator->getDeviceId(devInst);
}

DWORD BaseDeviceDiscoverer::getDeviceInstanceFromId(const QString& deviceId)
{
    return m_enumerator->getDeviceInstanceFromId(deviceId);
}

QVector<QVariantMap> BaseDeviceDiscoverer::getSiblingDevicesByParent(DWORD parentDevInst)
{
    return m_enumerator->getSiblingDevicesByParent(parentDevInst);
}

QVector<QVariantMap> BaseDeviceDiscoverer::getChildDevicesPython(DWORD devInst)
{
    return m_enumerator->getChildDevicesPython(devInst);
}

QVector<QVariantMap> BaseDeviceDiscoverer::getAllChildDevices(DWORD parentDevInst)
{
    return m_enumerator->getAllChildDevices(parentDevInst);
}

void BaseDeviceDiscoverer::matchDevicePaths(DeviceInfo& deviceInfo)
{
    // Find COM port by location
    if (!deviceInfo.serialPortId.isEmpty()) {
        QString comPortPath = findComPortByDeviceId(deviceInfo.serialPortId);
        if (!comPortPath.isEmpty()) {
            deviceInfo.serialPortPath = comPortPath;
            qCDebug(log_device_discoverer) << "Matched serial port path:" << comPortPath;
        }
    }
    
    // Find HID device path
    if (!deviceInfo.hidDeviceId.isEmpty()) {
        QString hidPath = m_enumerator->findHIDDevicePathByDeviceId(deviceInfo.hidDeviceId);
        if (!hidPath.isEmpty()) {
            deviceInfo.hidDevicePath = hidPath;
            qCDebug(log_device_discoverer) << "Matched HID device path:" << hidPath;
        }
    }
    
    // Find camera device path
    if (!deviceInfo.cameraDeviceId.isEmpty()) {
        QString cameraPath = m_enumerator->findCameraDevicePathByDeviceId(deviceInfo.cameraDeviceId);
        if (!cameraPath.isEmpty()) {
            deviceInfo.cameraDevicePath = cameraPath;
            qCDebug(log_device_discoverer) << "Matched camera device path:" << cameraPath;
        }
    }
    
    // Find audio device path
    if (!deviceInfo.audioDeviceId.isEmpty()) {
        QString audioPath = m_enumerator->findAudioDevicePathByDeviceId(deviceInfo.audioDeviceId);
        if (!audioPath.isEmpty()) {
            deviceInfo.audioDevicePath = audioPath;
            qCDebug(log_device_discoverer) << "Matched audio device path:" << audioPath;
        }
    }
}

void BaseDeviceDiscoverer::matchDevicePathsToRealPaths(DeviceInfo& deviceInfo)
{
    qCDebug(log_device_discoverer) << "=== Converting device IDs to real interface paths ===";
    
    // Get the composite device instance
    DWORD compositeDevInst = getDeviceInstanceFromId(deviceInfo.deviceInstanceId);
    if (compositeDevInst == 0) {
        qCWarning(log_device_discoverer) << "Failed to get composite device instance";
        return;
    }
    
    // Get all interface paths for this composite device
    QMap<QString, QString> interfacePaths = m_enumerator->getAllInterfacePathsForDevice(compositeDevInst);
    
    // Assign interface paths to device info
    if (interfacePaths.contains("HID")) {
        deviceInfo.hidDevicePath = interfacePaths["HID"];
        qCDebug(log_device_discoverer) << "  ✓ HID path:" << deviceInfo.hidDevicePath;
    }
    
    if (interfacePaths.contains("Camera")) {
        deviceInfo.cameraDevicePath = interfacePaths["Camera"];
        qCDebug(log_device_discoverer) << "  ✓ Camera path:" << deviceInfo.cameraDevicePath;
    }
    
    if (interfacePaths.contains("Audio")) {
        deviceInfo.audioDevicePath = interfacePaths["Audio"];
        qCDebug(log_device_discoverer) << "  ✓ Audio path:" << deviceInfo.audioDevicePath;
    }
    
    // Handle serial port separately
    if (!deviceInfo.serialPortId.isEmpty()) {
        QString comPortPath = findComPortByDeviceId(deviceInfo.serialPortId);
        if (!comPortPath.isEmpty()) {
            deviceInfo.serialPortPath = comPortPath;
            qCDebug(log_device_discoverer) << "  ✓ Serial port:" << deviceInfo.serialPortPath;
        }
    }
    
    qCDebug(log_device_discoverer) << "=== End path conversion ===";
}

QString BaseDeviceDiscoverer::findComPortByPortChain(const QString& portChain)
{
    // Enumerate all serial port devices and match by port chain
    QList<QSerialPortInfo> serialPorts = QSerialPortInfo::availablePorts();
    
    for (const QSerialPortInfo& portInfo : serialPorts) {
        QString serialPortChain = m_enumerator->getPortChainForSerialPort(portInfo.portName());
        if (serialPortChain == portChain) {
            qCDebug(log_device_discoverer) << "Found COM port" << portInfo.portName() << "for port chain" << portChain;
            return portInfo.portName();
        }
    }
    
    return QString();
}

QString BaseDeviceDiscoverer::findComPortByDeviceId(const QString& deviceId)
{
    return m_enumerator->findComPortByDeviceId(deviceId);
}

#endif // _WIN32