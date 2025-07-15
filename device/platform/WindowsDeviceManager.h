#ifdef _WIN32
#ifndef WINDOWSDEVICEMANAGER_H
#define WINDOWSDEVICEMANAGER_H

#include "AbstractPlatformDeviceManager.h"
#include <QLoggingCategory>
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
    QString findComPortByLocation(const QString& location);
    QString findComPortByDeviceId(const QString& deviceId);
    QString findComPortByPortChain(const QString& portChain);
    QString getPortChainForSerialPort(const QString& portName);
    QString findHIDByDeviceId(const QString& deviceId);
    QPair<QString, QString> findCameraAudioByDeviceInfo(const DeviceInfo& deviceInfo);
    
    // Windows API helpers
    QString getDeviceId(DWORD devInst);
    QString getDeviceProperty(HDEVINFO hDevInfo, SP_DEVINFO_DATA* devInfoData, DWORD property);
    QString getHardwareId(HDEVINFO hDevInfo, SP_DEVINFO_DATA* devInfoData);
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
    
    // Cleanup
    void cleanup();
    
    // Member variables for caching
    QList<DeviceInfo> m_cachedDevices;
    QDateTime m_lastCacheUpdate;
    static const int CACHE_TIMEOUT_MS = 1000; // 1 second cache
};

#endif // WINDOWSDEVICEMANAGER_H
#endif // _WIN32
