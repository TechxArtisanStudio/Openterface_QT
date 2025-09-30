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
    
    // USB 3.0 Companion PortChain Association
    // For USB 3.0 devices, the serial port and composite devices (camera, HID, audio) 
    // may appear on different PortChains. This field associates them.
    QString companionPortChain;  // PortChain where composite devices are located
    bool hasCompanionDevice;     // True if this device has associated composite devices on another PortChain
    
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
    
    // USB 3.0 Companion PortChain support
    bool hasCompanionPortChain() const { return !companionPortChain.isEmpty(); }
    QString getActiveCompanionPortChain() const { return hasCompanionPortChain() ? companionPortChain : portChain; }
    
    // Enhanced device validation for USB 3.0 dual PortChain devices
    bool isCompleteUSB3Device() const { 
        return hasSerialPort() && hasCompanionDevice && hasCompanionPortChain(); 
    }
    
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
        
        QString summary = interfaces.join(" | ");
        if (hasCompanionPortChain()) {
            summary += QString(" [Companion: %1]").arg(companionPortChain);
        }
        return summary;
    }
    
    QString getDeviceStatus() const {
        return QString("%1/4 interfaces").arg(getInterfaceCount());
    }
    
    // User-friendly port chain display
    QString getPortChainDisplay() const {
        if (portChain.isEmpty()) {
            return "Unknown";
        }
        
        QString display = QString("Port %1").arg(portChain);
        if (hasCompanionPortChain()) {
            display += QString(" + Companion %1").arg(companionPortChain);
        }
        return display;
    }
    
    // Get the appropriate PortChain for composite device access (camera, HID, audio)
    QString getCompositePortChain() const {
        return hasCompanionPortChain() ? companionPortChain : portChain;
    }
    
    // Get the serial PortChain (always the main portChain)
    QString getSerialPortChain() const {
        return portChain;
    }
};

#endif // DEVICEINFO_H
