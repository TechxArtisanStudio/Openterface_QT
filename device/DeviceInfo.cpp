#include "DeviceInfo.h"

DeviceInfo::DeviceInfo(const QString& portChain)
    : portChain(portChain)
    , hasCompanionDevice(false)
    , lastSeen(QDateTime::currentDateTime())
{
}

QVariantMap DeviceInfo::toMap() const
{
    QVariantMap map;
    map["portChain"] = portChain;
    map["deviceInstanceId"] = deviceInstanceId;
    map["companionPortChain"] = companionPortChain;
    map["hasCompanionDevice"] = hasCompanionDevice;
    map["serialPortPath"] = serialPortPath;
    map["hidDevicePath"] = hidDevicePath;
    map["cameraDevicePath"] = cameraDevicePath;
    map["audioDevicePath"] = audioDevicePath;
    map["serialPortId"] = serialPortId;
    map["hidDeviceId"] = hidDeviceId;
    map["cameraDeviceId"] = cameraDeviceId;
    map["audioDeviceId"] = audioDeviceId;
    map["platformSpecific"] = platformSpecific;
    map["lastSeen"] = lastSeen;
    return map;
}

void DeviceInfo::fromMap(const QVariantMap& map)
{
    portChain = map.value("portChain").toString();
    deviceInstanceId = map.value("deviceInstanceId").toString();
    companionPortChain = map.value("companionPortChain").toString();
    hasCompanionDevice = map.value("hasCompanionDevice", false).toBool();
    serialPortPath = map.value("serialPortPath").toString();
    hidDevicePath = map.value("hidDevicePath").toString();
    cameraDevicePath = map.value("cameraDevicePath").toString();
    audioDevicePath = map.value("audioDevicePath").toString();
    serialPortId = map.value("serialPortId").toString();
    hidDeviceId = map.value("hidDeviceId").toString();
    cameraDeviceId = map.value("cameraDeviceId").toString();
    audioDeviceId = map.value("audioDeviceId").toString();
    platformSpecific = map.value("platformSpecific").toMap();
    lastSeen = map.value("lastSeen").toDateTime();
}

QString DeviceInfo::getUniqueKey() const
{
    if (!portChain.isEmpty()) {
        return portChain;
    }
    if (!deviceInstanceId.isEmpty()) {
        return deviceInstanceId;
    }
    return serialPortPath + "|" + hidDevicePath;
}

bool DeviceInfo::isValid() const
{
    return !portChain.isEmpty() || !deviceInstanceId.isEmpty() || 
           !serialPortPath.isEmpty() || !hidDevicePath.isEmpty();
}

bool DeviceInfo::operator==(const DeviceInfo& other) const
{
    return portChain == other.portChain &&
           deviceInstanceId == other.deviceInstanceId &&
           companionPortChain == other.companionPortChain &&
           hasCompanionDevice == other.hasCompanionDevice &&
           serialPortPath == other.serialPortPath &&
           serialPortId == other.serialPortId &&
           hidDevicePath == other.hidDevicePath &&
           hidDeviceId == other.hidDeviceId &&
           cameraDevicePath == other.cameraDevicePath &&
           cameraDeviceId == other.cameraDeviceId &&
           audioDevicePath == other.audioDevicePath &&
           audioDeviceId == other.audioDeviceId &&
           platformSpecific == other.platformSpecific;
}

bool DeviceInfo::operator!=(const DeviceInfo& other) const
{
    return !(*this == other);
}
