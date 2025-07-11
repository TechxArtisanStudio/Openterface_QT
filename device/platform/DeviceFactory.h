#ifndef DEVICEFACTORY_H
#define DEVICEFACTORY_H

#include "AbstractPlatformDeviceManager.h"

class DeviceFactory
{
public:
    // Create platform-appropriate device manager
    static AbstractPlatformDeviceManager* createDeviceManager(QObject* parent = nullptr);
    
    // Platform detection utilities
    static QString getCurrentPlatform();
    static bool isPlatformSupported(const QString& platformName = QString());
    static QStringList getSupportedPlatforms();
    
private:
    DeviceFactory() = delete; // Static class only
};

#endif // DEVICEFACTORY_H
