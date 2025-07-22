#include "AbstractPlatformDeviceManager.h"

// Hardware identifiers for Openterface devices
const QString AbstractPlatformDeviceManager::SERIAL_VID = "1A86";
const QString AbstractPlatformDeviceManager::SERIAL_PID = "7523";
const QString AbstractPlatformDeviceManager::HID_VID = "534D";
const QString AbstractPlatformDeviceManager::HID_PID = "2109";

AbstractPlatformDeviceManager::AbstractPlatformDeviceManager(QObject *parent)
    : QObject(parent)
{
}

QList<DeviceInfo> AbstractPlatformDeviceManager::getDevicesByPortChain(const QString& targetPortChain)
{
    QList<DeviceInfo> allDevices = discoverDevices();
    return filterDevicesByPortChain(allDevices, targetPortChain);
}

QStringList AbstractPlatformDeviceManager::getAvailablePortChains()
{
    QList<DeviceInfo> allDevices = discoverDevices();
    QStringList portChains;
    
    for (const DeviceInfo& device : allDevices) {
        if (!device.portChain.isEmpty() && !portChains.contains(device.portChain)) {
            portChains.append(device.portChain);
        }
    }
    
    return portChains;
}

QList<DeviceInfo> AbstractPlatformDeviceManager::filterDevicesByPortChain(
    const QList<DeviceInfo>& devices, const QString& targetPortChain)
{
    if (targetPortChain.isEmpty()) {
        if (!devices.isEmpty()) {
            // Return only the first device if the list is not empty
            return { devices.first() };
        } else {
            // Return the original (empty) list if devices is empty
            return devices;
        }
    }

    QList<DeviceInfo> filtered;
    for (const DeviceInfo& device : devices) {
        // Exact match
        if (device.portChain == targetPortChain) {
            filtered.append(device);
        }
        // Also match if targetPortChain is a more specific interface port of this device
        // e.g., device.portChain = "1-2" should match targetPortChain = "1-2.1" or "1-2.2"
        else if (targetPortChain.startsWith(device.portChain + ".")) {
            filtered.append(device);
        }
        // Also match if device.portChain is a more specific interface port of targetPortChain
        // e.g., targetPortChain = "1-2" should match device.portChain = "1-2.1"
        else if (device.portChain.startsWith(targetPortChain + ".")) {
            filtered.append(device);
        }
    }

    return filtered;
}
