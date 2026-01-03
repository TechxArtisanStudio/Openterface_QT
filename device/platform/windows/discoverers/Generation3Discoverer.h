#ifndef GENERATION3DISCOVERER_H
#define GENERATION3DISCOVERER_H

#include "BaseDeviceDiscoverer.h"
#include "device/platform/DeviceConstants.h"

/**
 * @brief Generation 3 device discoverer (USB 3.0 integrated devices)
 * 
 * Handles discovery of USB 3.0 integrated devices that contain camera, HID, 
 * and audio interfaces. The serial port is separate and connected via 
 * CompanionPortChain relationship.
 * 
 * Supported devices:
 * - VID: 345F, PID: 2132 (USB 3.0 integrated)
 * - VID: 345F, PID: 2109 (V3 USB 3.0 integrated)
 */
class Generation3Discoverer : public BaseDeviceDiscoverer
{
    Q_OBJECT

public:
    explicit Generation3Discoverer(std::shared_ptr<IDeviceEnumerator> enumerator, QObject* parent = nullptr);
    
    // IDeviceDiscoverer interface
    QVector<DeviceInfo> discoverDevices() override;
    QString getGenerationName() const override { return "Generation 3 (USB 3.0)"; }
    QVector<QPair<QString, QString>> getSupportedVidPidPairs() const override;
    bool supportsVidPid(const QString& vid, const QString& pid) const override;

private:
    /**
     * @brief Find serial port associated with integrated device
     * @param integratedDevice USB device data for integrated device
     * @return Serial port device ID
     */
    QString findSerialPortByIntegratedDevice(const USBDeviceData& integratedDevice);
    
    /**
     * @brief Process integrated device interfaces (camera, HID, audio)
     * @param deviceInfo Device info to populate
     * @param integratedDevice USB device data for integrated device
     */
    void processIntegratedDeviceInterfaces(DeviceInfo& deviceInfo, const USBDeviceData& integratedDevice);
    
    /**
     * @brief Check if serial device is associated with integrated device
     * @param serialDevice Serial device data
     * @param integratedDevice Integrated device data
     * @return True if devices are associated
     */
    bool isSerialAssociatedWithIntegratedDevice(const USBDeviceData& serialDevice, const USBDeviceData& integratedDevice);
    
    /**
     * @brief Check if devices are on same USB hub
     * @param serialDevice Serial device data
     * @param integratedDevice Integrated device data
     * @return True if on same hub
     */
    bool isDevicesOnSameUSBHub(const USBDeviceData& serialDevice, const USBDeviceData& integratedDevice);
    
    /**
     * @brief Check if devices are proximate (close in USB topology)
     * @param serialDevice Serial device data
     * @param integratedDevice Integrated device data
     * @return True if devices are close
     */
    bool areDevicesProximate(const USBDeviceData& serialDevice, const USBDeviceData& integratedDevice);
    
    /**
     * @brief Check if devices match known USB 3.0 pattern
     * @param serialDevice Serial device data
     * @param integratedDevice Integrated device data
     * @return True if pattern matches
     */
    bool matchesKnownUSB3Pattern(const USBDeviceData& serialDevice, const USBDeviceData& integratedDevice);
    
    /**
     * @brief Calculate port chain distance between devices
     * @param portChain1 First port chain
     * @param portChain2 Second port chain
     * @return Distance (0 = same, higher = farther)
     */
    int calculatePortChainDistance(const QString& portChain1, const QString& portChain2);
};

#endif // GENERATION3DISCOVERER_H