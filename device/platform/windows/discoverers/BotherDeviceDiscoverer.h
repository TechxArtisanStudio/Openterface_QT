#ifndef BOTHERDEVICEDISCOVERER_H
#define BOTHERDEVICEDISCOVERER_H

#include "BaseDeviceDiscoverer.h"
#include "device/platform/DeviceConstants.h"

/**
 * @brief Bother Device Discoverer (Unified Gen1 & Gen2)
 * 
 * Handles discovery of Openterface devices with the same USB topological structure:
 * - Original generation devices (Gen1): VID:534D, PID:2109 integrated device with
 *   serial port VID:1A86, PID:7523 as sibling
 * - Newer generation devices (Gen2/USB 2.0): VID:1A86, PID:FE0C serial device with
 *   integrated device VID:345F, PID:2109/2132 as sibling
 * 
 * Both generations share the same topology: serial port and composite device
 * (camera, HID, audio) are siblings under the same USB hub.
 */
class BotherDeviceDiscoverer : public BaseDeviceDiscoverer
{
    Q_OBJECT

public:
    explicit BotherDeviceDiscoverer(std::shared_ptr<IDeviceEnumerator> enumerator, QObject* parent = nullptr);
    
    // IDeviceDiscoverer interface
    QVector<DeviceInfo> discoverDevices() override;
    QString getGenerationName() const override { return "Bother Devices"; }
    QVector<QPair<QString, QString>> getSupportedVidPidPairs() const override;
    bool supportsVidPid(const QString& vid, const QString& pid) const override;

private:
    /**
     * @brief Process Generation 1 style devices (integrated device first)
     * @param deviceInfo Device info to populate
     * @param integratedDevice USB device data for the integrated device
     */
    void processGeneration1Interfaces(DeviceInfo& deviceInfo, const USBDeviceData& integratedDevice);
    
    /**
     * @brief Process media interfaces (HID, camera, audio) for Generation 1 device
     * @param deviceInfo Device info to update
     * @param deviceData USB device data
     */
    void processGeneration1MediaInterfaces(DeviceInfo& deviceInfo, const USBDeviceData& deviceData);
    
    /**
     * @brief Find serial port from siblings of the integrated device
     * @param deviceInfo Device info to populate
     * @param integratedDevice Integrated device data
     */
    void findSerialPortFromSiblings(DeviceInfo& deviceInfo, const USBDeviceData& integratedDevice);
    
    /**
     * @brief Process Generation 2 style devices (serial device first)
     * @param deviceInfo Device info to populate
     * @param serialDevice USB device data for the serial device
     */
    void processGeneration2AsGeneration1(DeviceInfo& deviceInfo, const USBDeviceData& serialDevice);
    
    /**
     * @brief Find integrated device from siblings for Gen2 devices
     * @param deviceInfo Device info to populate
     * @param serialDevice Serial device data
     */
    void findIntegratedDeviceFromSiblings(DeviceInfo& deviceInfo, const USBDeviceData& serialDevice);
};

#endif // BOTHERDEVICEDISCOVERER_H
