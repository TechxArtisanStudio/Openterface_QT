#ifndef BASEDEVICEDISCOVERER_H
#define BASEDEVICEDISCOVERER_H

#include "IDeviceDiscoverer.h"
#include "device/platform/windows/IDeviceEnumerator.h"
#include "device/platform/DeviceConstants.h"
#include <QObject>
#include <QLoggingCategory>
#include <memory>

#if defined(_WIN32) && !defined(Q_MOC_RUN)
#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#endif

Q_DECLARE_LOGGING_CATEGORY(log_device_discoverer)

/**
 * @brief Base class for device discoverers
 * 
 * This class provides common functionality for all device discoverers,
 * including USB device enumeration, port chain building, and device
 * path resolution utilities.
 */
class BaseDeviceDiscoverer : public QObject, public IDeviceDiscoverer
{
    Q_OBJECT

public:
    explicit BaseDeviceDiscoverer(std::shared_ptr<IDeviceEnumerator> enumerator, QObject* parent = nullptr);
    virtual ~BaseDeviceDiscoverer() = default;

protected:
    /**
     * @brief Find USB devices with specific VID/PID
     * @param vid Vendor ID
     * @param pid Product ID
     * @return Vector of USB device data
     */
    QVector<USBDeviceData> findUSBDevicesWithVidPid(const QString& vid, const QString& pid);
    
    /**
     * @brief Build Python-compatible port chain
     * @param devInst Device instance handle
     * @return Port chain string
     */
    QString buildPythonCompatiblePortChain(DWORD devInst);
    
    /**
     * @brief Get device ID from device instance
     * @param devInst Device instance handle
     * @return Device ID string
     */
    QString getDeviceId(DWORD devInst);
    
    /**
     * @brief Get device instance from device ID
     * @param deviceId Device ID string
     * @return Device instance handle
     */
    DWORD getDeviceInstanceFromId(const QString& deviceId);
    
    /**
     * @brief Get sibling devices by parent device instance
     * @param parentDevInst Parent device instance
     * @return Vector of sibling device information
     */
    QVector<QVariantMap> getSiblingDevicesByParent(DWORD parentDevInst);
    
    /**
     * @brief Get child devices (Python-compatible)
     * @param devInst Device instance handle
     * @return Vector of child device information
     */
    QVector<QVariantMap> getChildDevicesPython(DWORD devInst);
    
    /**
     * @brief Get all child devices recursively
     * @param parentDevInst Parent device instance
     * @return Vector of all child device information
     */
    QVector<QVariantMap> getAllChildDevices(DWORD parentDevInst);
    
    /**
     * @brief Match device paths to real system paths
     * @param deviceInfo Device info to update with real paths
     */
    void matchDevicePaths(DeviceInfo& deviceInfo);
    
    /**
     * @brief Convert device IDs to real device paths
     * @param deviceInfo Device info to update with real paths
     */
    virtual void matchDevicePathsToRealPaths(DeviceInfo& deviceInfo);
    
    /**
     * @brief Find COM port by port chain
     * @param portChain Port chain string
     * @return COM port path
     */
    QString findComPortByPortChain(const QString& portChain);
    
    /**
     * @brief Find COM port by device ID
     * @param deviceId Device ID string
     * @return COM port path
     */
    QString findComPortByDeviceId(const QString& deviceId);

protected:
    std::shared_ptr<IDeviceEnumerator> m_enumerator;
};

#endif // BASEDEVICEDISCOVERER_H