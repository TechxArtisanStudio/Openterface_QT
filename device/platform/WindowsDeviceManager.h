#ifdef _WIN32
#ifndef WINDOWSDEVICEMANAGER_H
#define WINDOWSDEVICEMANAGER_H

#include "AbstractPlatformDeviceManager.h"
#include "windows/IDeviceEnumerator.h"
#include "windows/discoverers/DeviceDiscoveryManager.h"
#include <QLoggingCategory>
#include <QMap>
#include <memory>
#if defined(_WIN32) && !defined(Q_MOC_RUN)
#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#endif

Q_DECLARE_LOGGING_CATEGORY(log_device_windows)

class WindowsDeviceManager : public AbstractPlatformDeviceManager
{
    Q_OBJECT
    
public:
    explicit WindowsDeviceManager(QObject *parent = nullptr);
    explicit WindowsDeviceManager(std::unique_ptr<IDeviceEnumerator> enumerator, QObject *parent = nullptr);
    ~WindowsDeviceManager();
    
    QVector<DeviceInfo> discoverDevices() override;
    QString getPlatformName() const override { return "Windows"; }
    void clearCache() override;
    
    // Debug methods
    void debugListAllUSBDevices();
    
private:
    struct USBDeviceData {
        QString portChain;
        QVector<QVariantMap> siblings;
        QVector<QVariantMap> children;
        QString deviceInstanceId;
        QVariantMap deviceInfo;
    };
    
    // Device enumerator (abstraction layer for Windows API)
    std::unique_ptr<IDeviceEnumerator> m_enumerator;
    
    // Device discovery manager (coordinates generation-specific discoverers)
    std::unique_ptr<DeviceDiscoveryManager> m_discoveryManager;
    
    // Enhanced USB device discovery - enumerate all devices first
    QVector<USBDeviceData> findUSBDevicesWithVidPid(const QString& vid, const QString& pid);
    QVector<QVariantMap> enumerateAllDevices();
    QVector<QVariantMap> findDevicesWithVidPid(const QVector<QVariantMap>& allDevices, const QString& vid, const QString& pid);
    QVector<QVariantMap> buildDeviceTree(const QVector<QVariantMap>& allDevices);
    QVector<QVariantMap> findChildDevicesInTree(const QVector<QVariantMap>& deviceTree, const QString& parentDeviceId);
    QVector<DWORD> findParentUSBDevices(const QString& vid, const QString& pid);
    QString extractPortChainFromDeviceId(const QString& deviceId);
    
    // Python-compatible device discovery methods
    QString buildPythonCompatiblePortChain(DWORD devInst);
    
    // Device path matching
    void matchDevicePaths(DeviceInfo& deviceInfo);
    void matchDevicePathsFromChildren(DeviceInfo& deviceInfo, const QVector<QVariantMap>& children);
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
    
    // Windows API helpers (deprecated - use m_enumerator instead)
    QString getHardwareIdFromDevInst(DWORD devInst);
    QString getDeviceId(DWORD devInst);
    QString getHardwareId(HDEVINFO hDevInfo, SP_DEVINFO_DATA* devInfoData);
    QString getDeviceProperty(HDEVINFO hDevInfo, SP_DEVINFO_DATA* devInfoData, DWORD property);
    QString getDevicePropertyByName(DWORD devInst, const QString& propertyName);
    QVariantMap getDeviceInfo(DWORD devInst);
    QString getDeviceInterfacePath(HDEVINFO hDevInfo, SP_DEVINFO_DATA* devInfoData, const GUID& interfaceGuid);
    QVector<QVariantMap> getSiblingDevicesByParent(DWORD parentDevInst);
    QVector<QVariantMap> getSiblingDevices(DWORD parentDevInst);
    QVector<QVariantMap> getChildDevicesPython(DWORD devInst);
    QVector<QVariantMap> getChildDevices(DWORD devInst);
    QVector<QVariantMap> getAllChildDevices(DWORD parentDevInst);
    QVector<QVariantMap> enumerateDevicesByClass(const GUID& classGuid);
    QVector<QVariantMap> enumerateDevicesByClassWithParentInfo(const GUID& classGuid);
    DWORD getDeviceInstanceFromId(const QString& deviceId);
    DWORD getDeviceInstanceFromPortChain(const QString& portChain);
    QString getPortChain(DWORD devInst);
    
    // Registry helpers
    QString queryRegistryString(HKEY hKey, const QString& valueName);
    
    // Device association verification methods
    bool isDeviceAssociatedWithPortChain(const QString& parentDeviceId, const QString& targetDeviceInstanceId, const QString& targetPortChain);
    bool verifyCameraDeviceAssociation(const QString& cameraDeviceId, const QString& targetDeviceInstanceId, const QString& targetPortChain);
    bool verifyAudioDeviceAssociation(const QString& audioDeviceId, const QString& targetDeviceInstanceId, const QString& targetPortChain);
    
    // Enhanced device matching and collection
    bool matchesVidPidAdvanced(const QString& deviceInstanceId, const QString& vid, const QString& pid);
    QVector<QVariantMap> collectAllDevicesByPortChain(const QString& targetPortChain);
    QString normalizePortChain(const QString& portChain);
    
    // Device type discovery helpers
    QVector<QVariantMap> findSerialPortsByPortChain(const QString& targetPortChain);
    QVector<QVariantMap> findHIDDevicesByPortChain(const QString& targetPortChain);
    QVector<QVariantMap> findVideoDevicesByPortChain(const QString& targetPortChain);
    QVector<QVariantMap> findAudioDevicesByPortChain(const QString& targetPortChain);
    
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
    
    // Discovery system initialization
    void initializeDiscoveryManager();
    
    // Member variables for caching
    QVector<DeviceInfo> m_cachedDevices;
    QDateTime m_lastCacheUpdate;
    static const int CACHE_TIMEOUT_MS = 5000; // 1 second cache
};

#endif // WINDOWSDEVICEMANAGER_H
#endif // _WIN32
