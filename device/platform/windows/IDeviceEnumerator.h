#ifndef IDEVICEENUMERATOR_H
#define IDEVICEENUMERATOR_H

#include <QString>
#include <QVector>
#include <QVariantMap>
#include <QPair>

#if defined(_WIN32) && !defined(Q_MOC_RUN)
#include <windows.h>
#include <guiddef.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#elif defined(Q_MOC_RUN)
// Provide minimal Windows type stubs so moc can parse header without including
// heavy system headers which sometimes crash moc on Windows.
typedef unsigned long DWORD;
typedef void* HDEVINFO;
struct SP_DEVINFO_DATA { int dummy; };
struct GUID { int dummy; };
#endif

/**
 * @brief Interface for platform-specific device enumeration
 * 
 * This interface abstracts the underlying platform-specific APIs for device enumeration,
 * making the code more testable and maintainable. It isolates Windows API calls and
 * provides a Qt-friendly interface.
 */
class IDeviceEnumerator
{
public:
    virtual ~IDeviceEnumerator() = default;
    
    /**
     * @brief Enumerate devices by device class GUID
     * @param classGuid Device class GUID to enumerate
     * @return Vector of device information maps
     */
    virtual QVector<QVariantMap> enumerateDevicesByClass(const GUID& classGuid) = 0;
    
    /**
     * @brief Enumerate devices by device class with parent information
     * @param classGuid Device class GUID to enumerate
     * @return Vector of device information maps including parent details
     */
    virtual QVector<QVariantMap> enumerateDevicesByClassWithParentInfo(const GUID& classGuid) = 0;
    
    /**
     * @brief Get detailed information about a device
     * @param devInst Device instance handle
     * @return Device information map
     */
    virtual QVariantMap getDeviceInfo(DWORD devInst) = 0;
    
    /**
     * @brief Get device ID string from device instance
     * @param devInst Device instance handle
     * @return Device ID string
     */
    virtual QString getDeviceId(DWORD devInst) = 0;
    
    /**
     * @brief Get hardware ID from device information
     * @param hDevInfo Device information set handle
     * @param devInfoData Device information data pointer
     * @return Hardware ID string
     */
    virtual QString getHardwareId(HDEVINFO hDevInfo, SP_DEVINFO_DATA* devInfoData) = 0;
    
    /**
     * @brief Get device property from registry
     * @param hDevInfo Device information set handle
     * @param devInfoData Device information data pointer
     * @param property Property identifier
     * @return Property value as string
     */
    virtual QString getDeviceProperty(HDEVINFO hDevInfo, SP_DEVINFO_DATA* devInfoData, DWORD property) = 0;
    
    /**
     * @brief Get device property by property name
     * @param devInst Device instance handle
     * @param propertyName Name of the property to retrieve
     * @return Property value as string
     */
    virtual QString getDevicePropertyByName(DWORD devInst, const QString& propertyName) = 0;
    
    /**
     * @brief Get child devices of a parent device
     * @param devInst Parent device instance handle
     * @return Vector of child device information maps
     */
    virtual QVector<QVariantMap> getChildDevices(DWORD devInst) = 0;
    
    /**
     * @brief Get all child devices recursively
     * @param parentDevInst Parent device instance handle
     * @return Vector of all child device information maps
     */
    virtual QVector<QVariantMap> getAllChildDevices(DWORD parentDevInst) = 0;
    
    /**
     * @brief Get sibling devices by parent device instance
     * @param parentDevInst Parent device instance handle
     * @return Vector of sibling device information maps
     */
    virtual QVector<QVariantMap> getSiblingDevicesByParent(DWORD parentDevInst) = 0;
    
    /**
     * @brief Build port chain string for a device
     * @param devInst Device instance handle
     * @return Port chain string (e.g., "1-2-3")
     */
    virtual QString buildPortChain(DWORD devInst) = 0;
    
    /**
     * @brief Get device instance from device ID string
     * @param deviceId Device ID string
     * @return Device instance handle (0 if not found)
     */
    virtual DWORD getDeviceInstanceFromId(const QString& deviceId) = 0;
    
    /**
     * @brief Get device instance from port chain
     * @param portChain Port chain string
     * @return Device instance handle (0 if not found)
     */
    virtual DWORD getDeviceInstanceFromPortChain(const QString& portChain) = 0;
    
    /**
     * @brief Get the parent device instance
     * @param devInst Child device instance handle
     * @return Parent device instance handle (0 if not found)
     */
    virtual DWORD getParentDevice(DWORD devInst) = 0;
    
    /**
     * @brief Get device interface path for a specific interface GUID
     * @param hDevInfo Device information set handle
     * @param devInfoData Device information data pointer
     * @param interfaceGuid Interface GUID to query
     * @return Device interface path string
     */
    virtual QString getDeviceInterfacePath(HDEVINFO hDevInfo, SP_DEVINFO_DATA* devInfoData, const GUID& interfaceGuid) = 0;
    
    /**
     * @brief Find HID device path by device ID
     * @param deviceId Device ID string
     * @return HID device path (empty if not found)
     */
    virtual QString findHIDDevicePathByDeviceId(const QString& deviceId) = 0;
    
    /**
     * @brief Find camera device path by device ID
     * @param deviceId Device ID string
     * @return Camera device path (empty if not found)
     */
    virtual QString findCameraDevicePathByDeviceId(const QString& deviceId) = 0;
    
    /**
     * @brief Find audio device path by device ID
     * @param deviceId Device ID string
     * @return Audio device path (empty if not found)
     */
    virtual QString findAudioDevicePathByDeviceId(const QString& deviceId) = 0;
    
    /**
     * @brief Find COM port by device ID
     * @param deviceId Device ID string
     * @return COM port name (e.g., "COM3", empty if not found)
     */
    virtual QString findComPortByDeviceId(const QString& deviceId) = 0;
    
    /**
     * @brief Enumerate devices by interface GUID
     * @param interfaceGuid Device interface GUID
     * @return Vector of device information maps
     */
    virtual QVector<QVariantMap> enumerateDevicesByInterface(const GUID& interfaceGuid) = 0;
    
    /**
     * @brief Enumerate all devices from all relevant classes
     * @return Vector of all device information maps
     */
    virtual QVector<QVariantMap> enumerateAllDevices() = 0;
    
    /**
     * @brief Get child devices Python-compatible (using CM_Get_Child and CM_Get_Sibling)
     * @param devInst Parent device instance handle
     * @return Vector of child device information maps
     */
    virtual QVector<QVariantMap> getChildDevicesPython(DWORD devInst) = 0;
    
    /**
     * @brief Find HID device path for a specific port chain
     * @param portChain Port chain string
     * @return HID device path (empty if not found)
     */
    virtual QString findHidDeviceForPortChain(const QString& portChain) = 0;
    
    /**
     * @brief Get port chain for a serial port name
     * @param portName Serial port name (e.g., "COM3")
     * @return Port chain string (empty if not found)
     */
    virtual QString getPortChainForSerialPort(const QString& portName) = 0;
};

#endif // IDEVICEENUMERATOR_H
