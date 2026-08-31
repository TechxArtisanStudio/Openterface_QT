#ifndef MOCK_DEVICE_DISCOVERY_H
#define MOCK_DEVICE_DISCOVERY_H

#include "device/IDeviceDiscovery.h"
#include <QList>
#include <algorithm>

/**
 * @brief Mock device discovery for cross-platform testing.
 *
 * Returns configurable device lists on any platform (Windows/macOS/Linux),
 * simulating USB device plug/unplug without real hardware.
 */
class MockDeviceDiscovery : public IDeviceDiscovery {
public:
    QList<DeviceInfo> discoverDevices() override {
        m_discoveryCallCount++;
        return m_devices;
    }

    // Test helper methods

    void setDevices(const QList<DeviceInfo>& devices) {
        m_devices = devices;
    }

    void plugInDevice(const DeviceInfo& device) {
        m_devices.append(device);
    }

    void unplugDevice(const QString& portChain) {
        m_devices.erase(
            std::remove_if(m_devices.begin(), m_devices.end(),
                [&](const DeviceInfo& d) { return d.portChain == portChain; }),
            m_devices.end()
        );
    }

    void unplugAll() {
        m_devices.clear();
    }

    int discoveryCallCount() const { return m_discoveryCallCount; }
    void resetCallCount() { m_discoveryCallCount = 0; }

private:
    QList<DeviceInfo> m_devices;
    int m_discoveryCallCount = 0;
};

#endif // MOCK_DEVICE_DISCOVERY_H
