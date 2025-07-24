#ifndef ABSTRACTPLATFORMDEVICEMANAGER_H
#define ABSTRACTPLATFORMDEVICEMANAGER_H

#include <QObject>
#include "../DeviceInfo.h"

class AbstractPlatformDeviceManager : public QObject
{
    Q_OBJECT
    
public:
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
    
    // Helper method for filtering devices
    QList<DeviceInfo> filterDevicesByPortChain(const QList<DeviceInfo>& devices, 
                                               const QString& targetPortChain);
};

#endif // ABSTRACTPLATFORMDEVICEMANAGER_H
