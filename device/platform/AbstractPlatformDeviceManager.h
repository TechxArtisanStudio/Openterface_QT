#ifndef ABSTRACTPLATFORMDEVICEMANAGER_H
#define ABSTRACTPLATFORMDEVICEMANAGER_H

#include <QObject>
#include "../DeviceInfo.h"

class AbstractPlatformDeviceManager : public QObject
{
    Q_OBJECT
    
public:
    // Public getters for platform VID/PID constants; these expose protected static constants.
    static QString getOpenterfaceVid() { return OPENTERFACE_VID; }
    static QString getOpenterfacePid() { return OPENTERFACE_PID; }
    static QString getOpenterfaceVidV2() { return OPENTERFACE_VID_V2; }
    static QString getOpenterfacePidV2() { return OPENTERFACE_PID_V2; }
    static QString getOpenterfaceVidV3() { return OPENTERFACE_VID_V3; }
    static QString getOpenterfacePidV3() { return OPENTERFACE_PID_V3; }
    explicit AbstractPlatformDeviceManager(QObject *parent = nullptr);
    virtual ~AbstractPlatformDeviceManager() = default;
    
    // Pure virtual methods that platform implementations must provide
    virtual QList<DeviceInfo> discoverDevices() = 0;
    virtual QString getPlatformName() const = 0;
    
    // Optional platform-specific methods
    virtual void clearCache() {} // Default implementation does nothing
    
    // Common functionality
    QList<DeviceInfo> getDevicesByPortChain(const QString& targetPortChain);
    QStringList getAvailablePortChains();
    
    // USB 3.0 Companion PortChain support
    QList<DeviceInfo> getDevicesByAnyPortChain(const QString& targetPortChain);
    QList<DeviceInfo> getDevicesWithCompanionPortChain(const QString& companionPortChain);
    
protected:
    // Common VID/PID constants for all platforms - Original generation
    static const QString SERIAL_VID;
    static const QString SERIAL_PID; 
    static const QString OPENTERFACE_VID;
    static const QString OPENTERFACE_PID;
    
    // Common VID/PID constants for all platforms - New generation (Companion device)
    static const QString SERIAL_VID_V2;
    static const QString SERIAL_PID_V2;
    static const QString OPENTERFACE_VID_V2;
    static const QString OPENTERFACE_PID_V2;
    
    // Common VID/PID constants for all platforms - V3 generation
    static const QString SERIAL_VID_V3;
    static const QString SERIAL_PID_V3;
    static const QString OPENTERFACE_VID_V3;
    static const QString OPENTERFACE_PID_V3;
    
    // Helper method for filtering devices
    QList<DeviceInfo> filterDevicesByPortChain(const QList<DeviceInfo>& devices, 
                                               const QString& targetPortChain);
    QList<DeviceInfo> filterDevicesByAnyPortChain(const QList<DeviceInfo>& devices,
                                                  const QString& targetPortChain);
    QList<DeviceInfo> filterDevicesByCompanionPortChain(const QList<DeviceInfo>& devices,
                                                       const QString& companionPortChain);
};

#endif // ABSTRACTPLATFORMDEVICEMANAGER_H
