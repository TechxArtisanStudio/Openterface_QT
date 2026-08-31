#ifndef IDEVICEDISCOVERY_H
#define IDEVICEDISCOVERY_H

#include <QList>
#include "DeviceInfo.h"

/**
 * @brief Abstract interface for device discovery.
 *
 * Decouples HotplugMonitor from the concrete DeviceManager implementation,
 * enabling cross-platform unit testing with mock device lists.
 */
class IDeviceDiscovery {
public:
    virtual ~IDeviceDiscovery() = default;
    virtual QList<DeviceInfo> discoverDevices() = 0;
};

#endif // IDEVICEDISCOVERY_H
