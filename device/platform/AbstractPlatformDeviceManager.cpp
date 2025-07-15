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
        if (device.portChain == targetPortChain) {
            filtered.append(device);
        }
    }

    return filtered;
}
