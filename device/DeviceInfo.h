#ifndef DEVICEINFO_H
#define DEVICEINFO_H

#include <QString>
#include <QVariantMap>
#include <QDateTime>

class DeviceInfo
{
public:
    DeviceInfo() = default;
    DeviceInfo(const QString& portChain);
    
    // Device identification
    QString portChain;
    QString deviceInstanceId;
    
    // Subdevice paths
    QString serialPortPath;
    QString hidDevicePath;
    QString cameraDevicePath;
    QString audioDevicePath;
    
    // Device IDs/Names
    QString serialPortId;
    QString hidDeviceId;
    QString cameraDeviceId;
    QString audioDeviceId;
    
    // Platform-specific data
    QVariantMap platformSpecific;
    
    // Timestamp for tracking
    QDateTime lastSeen;
    
    // Utility methods
    QVariantMap toMap() const;
    void fromMap(const QVariantMap& map);
    QString getUniqueKey() const;
    bool isValid() const;
    bool operator==(const DeviceInfo& other) const;
    bool operator!=(const DeviceInfo& other) const;
    
    // Device type checking
    bool hasSerialPort() const { return !serialPortPath.isEmpty(); }
    bool hasHidDevice() const { return !hidDevicePath.isEmpty(); }
    bool hasCameraDevice() const { return !cameraDevicePath.isEmpty(); }
    bool hasAudioDevice() const { return !audioDevicePath.isEmpty(); }
    
    // Complete device validation
    bool isCompleteDevice() const { return hasSerialPort() && hasHidDevice(); }
    int getInterfaceCount() const {
        int count = 0;
        if (hasSerialPort()) count++;
        if (hasHidDevice()) count++;
        if (hasCameraDevice()) count++;
        if (hasAudioDevice()) count++;
        return count;
    }
    
    // Device status and identification
    QString getDeviceDisplayName() const {
        return QString("Openterface Device - Port %1").arg(portChain);
    }
    
    QString getInterfaceSummary() const {
        QStringList interfaces;
        if (hasSerialPort()) interfaces << QString("Serial(%1)").arg(serialPortPath);
        if (hasHidDevice()) interfaces << "HID";
        if (hasCameraDevice()) interfaces << "Camera";
        if (hasAudioDevice()) interfaces << "Audio";
        return interfaces.join(" | ");
    }
    
    QString getDeviceStatus() const {
        return QString("%1/4 interfaces").arg(getInterfaceCount());
    }
    
    // User-friendly port chain display
    QString getPortChainDisplay() const {
        if (portChain.isEmpty()) {
            return "Unknown";
        }
        return QString("Port %1").arg(portChain);
    }
    
    // Hardware identifiers (from your Python code)
    static const QString SERIAL_VID;
    static const QString SERIAL_PID;
    static const QString HID_VID;
    static const QString HID_PID;
};

#endif // DEVICEINFO_H
