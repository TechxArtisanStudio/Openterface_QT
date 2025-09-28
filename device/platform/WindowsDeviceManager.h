#ifdef _WIN32
#ifndef WINDOWSDEVICEMANAGER_H
#define WINDOWSDEVICEMANAGER_H

#include "AbstractPlatformDeviceManager.h"
#include <QLoggingCategory>
#include <QMap>
#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>

Q_DECLARE_LOGGING_CATEGORY(log_device_windows)

class WindowsDeviceManager : public AbstractPlatformDeviceManager
{
    Q_OBJECT
    
public:
    explicit WindowsDeviceManager(QObject *parent = nullptr);
    ~WindowsDeviceManager();
    
    QList<DeviceInfo> discoverDevices() override;
    QString getPlatformName() const override { return "Windows"; }
    void clearCache() override;
    
    // Debug methods
    void debugListAllUSBDevices();
    
private:
    struct USBDeviceData {
        QString portChain;
        QList<QVariantMap> siblings;
        QList<QVariantMap> children;
        QString deviceInstanceId;
        QVariantMap deviceInfo;
    };
    
    // Enhanced USB device discovery - enumerate all devices first
    QList<USBDeviceData> findUSBDevicesWithVidPid(const QString& vid, const QString& pid);
    QList<QVariantMap> enumerateAllDevices();
    QList<QVariantMap> findDevicesWithVidPid(const QList<QVariantMap>& allDevices, const QString& vid, const QString& pid);
    QList<QVariantMap> buildDeviceTree(const QList<QVariantMap>& allDevices);
    QList<QVariantMap> findChildDevicesInTree(const QList<QVariantMap>& deviceTree, const QString& parentDeviceId);
    QList<DWORD> findParentUSBDevices(const QString& vid, const QString& pid);
    QList<QVariantMap> getAllChildDevices(DWORD parentDevInst);
    QVariantMap getDeviceInfo(DWORD devInst);
    QString extractPortChainFromDeviceId(const QString& deviceId);
    
    // Python-compatible device discovery methods
    QString buildPythonCompatiblePortChain(DWORD devInst);
    QList<QVariantMap> getSiblingDevicesByParent(DWORD parentDevInst);
    QList<QVariantMap> getChildDevicesPython(DWORD devInst);
    QString getHardwareIdFromDevInst(DWORD devInst);
    
    // Device path matching
    void matchDevicePaths(DeviceInfo& deviceInfo);
    void matchDevicePathsFromChildren(DeviceInfo& deviceInfo, const QList<QVariantMap>& children);
    void matchDevicePathsToRealPaths(DeviceInfo& deviceInfo);
    void matchDevicePathsToRealPathsGeneration2(DeviceInfo& deviceInfo);
    QString findComPortByLocation(const QString& location);
    QString findComPortByDeviceId(const QString& deviceId);
    QString findComPortByPortChain(const QString& portChain);
    QString getPortChainForSerialPort(const QString& portName);
    QString findHIDByDeviceId(const QString& deviceId);
    QPair<QString, QString> findCameraAudioByDeviceInfo(const DeviceInfo& deviceInfo);
    
    // Enhanced camera/audio device path resolution
    QString findCameraDevicePathByDeviceId(const QString& deviceId);
    QString findAudioDevicePathByDeviceId(const QString& deviceId);
    bool isDeviceRelatedToPortChain(const QString& deviceId, const QString& portChain);
    
    // Windows API helpers
    QString getDeviceId(DWORD devInst);
    QString getDeviceProperty(HDEVINFO hDevInfo, SP_DEVINFO_DATA* devInfoData, DWORD property);
    QString getDevicePropertyByName(DWORD devInst, const QString& propertyName); // New overload for specific properties
    QString getHardwareId(HDEVINFO hDevInfo, SP_DEVINFO_DATA* devInfoData);
    QString getDeviceInterfacePath(HDEVINFO hDevInfo, SP_DEVINFO_DATA* devInfoData, const GUID& interfaceGuid);
    QList<QVariantMap> getSiblingDevices(DWORD parentDevInst);
    QList<QVariantMap> getChildDevices(DWORD devInst);
    QString getPortChain(DWORD devInst);
    
    // Registry helpers
    QString queryRegistryString(HKEY hKey, const QString& valueName);
    
    // Device enumeration by class
    QList<QVariantMap> enumerateDevicesByClass(const GUID& classGuid);
    
    // Enhanced device enumeration with parent information
    QList<QVariantMap> enumerateDevicesByClassWithParentInfo(const GUID& classGuid);
    
    // Device association verification methods
    bool isDeviceAssociatedWithPortChain(const QString& parentDeviceId, const QString& targetDeviceInstanceId, const QString& targetPortChain);
    bool verifyCameraDeviceAssociation(const QString& cameraDeviceId, const QString& targetDeviceInstanceId, const QString& targetPortChain);
    bool verifyAudioDeviceAssociation(const QString& audioDeviceId, const QString& targetDeviceInstanceId, const QString& targetPortChain);
    DWORD getDeviceInstanceFromId(const QString& deviceId);
    
    // Enhanced device matching and collection
    bool matchesVidPidAdvanced(const QString& deviceInstanceId, const QString& vid, const QString& pid);
    QList<QVariantMap> collectAllDevicesByPortChain(const QString& targetPortChain);
    QString normalizePortChain(const QString& portChain);
    
    // Device type discovery helpers
    QList<QVariantMap> findSerialPortsByPortChain(const QString& targetPortChain);
    QList<QVariantMap> findHIDDevicesByPortChain(const QString& targetPortChain);
    QList<QVariantMap> findVideoDevicesByPortChain(const QString& targetPortChain);
    QList<QVariantMap> findAudioDevicesByPortChain(const QString& targetPortChain);
    
    // Generation-specific device discovery methods
    QList<DeviceInfo> discoverGeneration1Devices();
    QList<DeviceInfo> discoverGeneration2Devices();
    
    // Optimized device discovery for USB 2.0/3.0 compatibility
    QList<DeviceInfo> discoverOptimizedDevices();
    void processGeneration2Interfaces(DeviceInfo& deviceInfo, const USBDeviceData& gen2Device);
    void processGeneration1Interfaces(DeviceInfo& deviceInfo, const USBDeviceData& gen1Device);
    void processGeneration1SerialInterface(DeviceInfo& deviceInfo, const USBDeviceData& deviceData);
    void processGeneration1MediaInterfaces(DeviceInfo& deviceInfo, const USBDeviceData& deviceData);
    void enhanceDeviceDetection(QMap<QString, DeviceInfo>& deviceMap);
    QString findSerialPortForPortChain(const QString& portChain);
    QString findHidDeviceForPortChain(const QString& portChain);
    
    // Generation 2 (Companion device) helper methods
    QString findSerialPortByCompanionDevice(const USBDeviceData& companionDevice);
    QString findCompanionPortChain(const USBDeviceData& companionDevice);
    DWORD getDeviceInstanceFromPortChain(const QString& portChain);
    
    // Integrated device helper methods (for USB 3.0 with separate serial port)
    QString findSerialPortByIntegratedDevice(const USBDeviceData& integratedDevice);
    bool isSerialAssociatedWithIntegratedDevice(const USBDeviceData& serialDevice, const USBDeviceData& integratedDevice);
    QString extractParentPortChain(const QString& portChain);
    QString getCompanionPortChainFromDevice(const USBDeviceData& integratedDevice);
    void processIntegratedDeviceInterfaces(DeviceInfo& deviceInfo, const USBDeviceData& integratedDevice);
    
    // Enhanced association methods for USB 3.0 compatibility
    bool isDevicesOnSameUSBHub(const USBDeviceData& serialDevice, const USBDeviceData& integratedDevice);
    bool areDevicesProximate(const USBDeviceData& serialDevice, const USBDeviceData& integratedDevice);
    bool matchesKnownUSB3Pattern(const USBDeviceData& serialDevice, const USBDeviceData& integratedDevice);
    QString getUSBHubPortChain(const USBDeviceData& device);
    int calculatePortChainDistance(const QString& portChain1, const QString& portChain2);
    
    // Enhanced companion port chain analysis
    QString analyzeParentDeviceForCompanionChain(const USBDeviceData& integratedDevice);
    QString inferCompanionChainFromUSBTopology(const USBDeviceData& integratedDevice);
    QString inferCompanionChainFromPortPattern(const USBDeviceData& integratedDevice);
    
    bool isSerialDeviceAssociatedWithCompanion(const USBDeviceData& serialDevice, const USBDeviceData& companionDevice);
    QString extractHubPortFromChain(const QString& portChain);
    QString calculateExpectedSerialHubPort(const QString& companionHubPort);
    bool arePortChainsRelated(const QString& portChain1, const QString& portChain2);
    
    // Cleanup
    void cleanup();
    
    // Member variables for caching
    QList<DeviceInfo> m_cachedDevices;
    QDateTime m_lastCacheUpdate;
    static const int CACHE_TIMEOUT_MS = 1000; // 1 second cache
};

#endif // WINDOWSDEVICEMANAGER_H
#endif // _WIN32
