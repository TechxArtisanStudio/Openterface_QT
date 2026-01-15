#include "AbstractPlatformDeviceManager.h"

// Hardware identifiers for Openterface devices - Original generation
const QString AbstractPlatformDeviceManager::SERIAL_VID = "1A86";
const QString AbstractPlatformDeviceManager::SERIAL_PID = "7523";
const QString AbstractPlatformDeviceManager::OPENTERFACE_VID = "534D"; // MS2109
const QString AbstractPlatformDeviceManager::OPENTERFACE_PID = "2109"; // MS2109

// Hardware identifiers for Openterface devices - New generation (Companion device)
const QString AbstractPlatformDeviceManager::SERIAL_VID_V2 = "1A86";
const QString AbstractPlatformDeviceManager::SERIAL_PID_V2 = "FE0C";
const QString AbstractPlatformDeviceManager::OPENTERFACE_VID_V2 = "345F"; // MS2130S
const QString AbstractPlatformDeviceManager::OPENTERFACE_PID_V2 = "2132"; // MS2130S

// Hardware identifiers for Openterface devices - V3 generation
const QString AbstractPlatformDeviceManager::SERIAL_VID_V3 = "1A86";
const QString AbstractPlatformDeviceManager::SERIAL_PID_V3 = "FE0C";
const QString AbstractPlatformDeviceManager::OPENTERFACE_VID_V3 = "345F";
const QString AbstractPlatformDeviceManager::OPENTERFACE_PID_V3 = "2109";

AbstractPlatformDeviceManager::AbstractPlatformDeviceManager(QObject *parent)
    : QObject(parent)
{
}

QList<DeviceInfo> AbstractPlatformDeviceManager::getDevicesByPortChain(const QString& targetPortChain)
{
    qDebug() << "Getting devices by port chain:" << targetPortChain;
    QList<DeviceInfo> allDevices = discoverDevices();
    return filterDevicesByPortChain(allDevices, targetPortChain);
}

QList<DeviceInfo> AbstractPlatformDeviceManager::getDevicesByAnyPortChain(const QString& targetPortChain)
{
    qDebug() << "Getting devices by any port chain:" << targetPortChain;
    QList<DeviceInfo> allDevices = discoverDevices();
    qDebug() << "There are" << allDevices.size() << " devices discovered in total.";
    return filterDevicesByAnyPortChain(allDevices, targetPortChain);
}

QList<DeviceInfo> AbstractPlatformDeviceManager::getDevicesWithCompanionPortChain(const QString& companionPortChain)
{
    qDebug() << "Getting devices with companion port chain:" << companionPortChain;
    QList<DeviceInfo> allDevices = discoverDevices();
    return filterDevicesByCompanionPortChain(allDevices, companionPortChain);
}

QStringList AbstractPlatformDeviceManager::getAvailablePortChains()
{
    qDebug() << "Getting available port chains from discovered devices...";
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

QList<DeviceInfo> AbstractPlatformDeviceManager::filterDevicesByAnyPortChain(
    const QList<DeviceInfo>& devices, const QString& targetPortChain)
{
    if (targetPortChain.isEmpty()) {
        return devices.isEmpty() ? devices : QList<DeviceInfo>{ devices.first() };
    }

    QList<DeviceInfo> filtered;
    for (const DeviceInfo& device : devices) {
        // Check both main portChain and companionPortChain
        bool mainMatches = (device.portChain == targetPortChain) ||
                          (targetPortChain.startsWith(device.portChain + ".")) ||
                          (device.portChain.startsWith(targetPortChain + "."));
        
        bool companionMatches = (!device.companionPortChain.isEmpty()) &&
                               ((device.companionPortChain == targetPortChain) ||
                                (targetPortChain.startsWith(device.companionPortChain + ".")) ||
                                (device.companionPortChain.startsWith(targetPortChain + ".")));
        
        // For 2109 devices (original generation), do not check companionPortChain as they never go to 3.0
        if (device.pid == OPENTERFACE_PID) {
            companionMatches = false;
        }
        
        if (mainMatches || companionMatches) {
            filtered.append(device);
        }
    }

    return filtered;
}

#include <algorithm>

QList<DeviceInfo> AbstractPlatformDeviceManager::filterDevicesByCompanionPortChain(
    const QList<DeviceInfo>& devices, const QString& companionPortChain)
{
    if (companionPortChain.isEmpty()) {
        return QList<DeviceInfo>();
    }

    QList<DeviceInfo> filtered;
    for (const DeviceInfo& device : devices) {
        if (!device.companionPortChain.isEmpty()) {
            // Check companion PortChain match
            if ((device.companionPortChain == companionPortChain) ||
                (companionPortChain.startsWith(device.companionPortChain + ".")) ||
                (device.companionPortChain.startsWith(companionPortChain + "."))) {
                filtered.append(device);
            }
        }
    }

    return filtered;
}

QString AbstractPlatformDeviceManager::formatDeviceTreeFromDevices(const QList<DeviceInfo>& devices) const
{
    if (devices.isEmpty()) return QString("No devices found");

    QStringList lines;
    QList<DeviceInfo> sorted = devices;
    std::sort(sorted.begin(), sorted.end(), [](const DeviceInfo& a, const DeviceInfo& b){
        return a.portChain < b.portChain;
    });

    for (const DeviceInfo& d : sorted) {
        lines << QString("%1").arg(d.portChain);
        if (!d.vid.isEmpty() || !d.pid.isEmpty()) {
            lines << QString("  VID: %1 PID: %2").arg(d.vid, d.pid);
        }
        if (!d.serialPortPath.isEmpty()) lines << QString("  Serial: %1").arg(d.serialPortPath);
        if (!d.hidDevicePath.isEmpty()) lines << QString("  HID: %1").arg(d.hidDevicePath);
        if (!d.cameraDevicePath.isEmpty()) lines << QString("  Camera: %1").arg(d.cameraDevicePath);
        if (!d.audioDevicePath.isEmpty()) lines << QString("  Audio: %1").arg(d.audioDevicePath);
        if (!d.deviceInstanceId.isEmpty()) lines << QString("  DeviceInstanceId: %1").arg(d.deviceInstanceId);
    }

    return lines.join("\n");
} 
