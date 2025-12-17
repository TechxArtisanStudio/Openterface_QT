#ifdef _WIN32
#ifndef WINDEVICEENUMERATOR_H
#define WINDEVICEENUMERATOR_H

#include "IDeviceEnumerator.h"
#include <QObject>
#include <QLoggingCategory>
#if defined(_WIN32) && !defined(Q_MOC_RUN)
#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#endif

Q_DECLARE_LOGGING_CATEGORY(log_win_enumerator)

/**
 * @brief Windows-specific implementation of device enumeration
 * 
 * This class encapsulates all Windows API calls for device enumeration,
 * providing a clean Qt-friendly interface. It isolates platform-specific
 * details and makes the code easier to test and maintain.
 */
class WinDeviceEnumerator : public QObject, public IDeviceEnumerator
{
    Q_OBJECT

public:
    explicit WinDeviceEnumerator(QObject* parent = nullptr);
    ~WinDeviceEnumerator() override;

    // IDeviceEnumerator interface implementation
    QVector<QVariantMap> enumerateDevicesByClass(const GUID& classGuid) override;
    QVector<QVariantMap> enumerateDevicesByClassWithParentInfo(const GUID& classGuid) override;
    QVariantMap getDeviceInfo(DWORD devInst) override;
    QString getDeviceId(DWORD devInst) override;
    QString getHardwareId(HDEVINFO hDevInfo, SP_DEVINFO_DATA* devInfoData) override;
    QString getDeviceProperty(HDEVINFO hDevInfo, SP_DEVINFO_DATA* devInfoData, DWORD property) override;
    QString getDevicePropertyByName(DWORD devInst, const QString& propertyName) override;
    QVector<QVariantMap> getChildDevices(DWORD devInst) override;
    QVector<QVariantMap> getAllChildDevices(DWORD parentDevInst) override;
    QVector<QVariantMap> getSiblingDevicesByParent(DWORD parentDevInst) override;
    QString buildPortChain(DWORD devInst) override;
    DWORD getDeviceInstanceFromId(const QString& deviceId) override;
    DWORD getDeviceInstanceFromPortChain(const QString& portChain) override;
    DWORD getParentDevice(DWORD devInst) override;
    QString getDeviceInterfacePath(HDEVINFO hDevInfo, SP_DEVINFO_DATA* devInfoData, const GUID& interfaceGuid) override;
    QString findHIDDevicePathByDeviceId(const QString& deviceId) override;
    QString findCameraDevicePathByDeviceId(const QString& deviceId) override;
    QString findAudioDevicePathByDeviceId(const QString& deviceId) override;
    QString findComPortByDeviceId(const QString& deviceId) override;
    QVector<QVariantMap> enumerateDevicesByInterface(const GUID& interfaceGuid) override;
    QVector<QVariantMap> enumerateAllDevices() override;
    QVector<QVariantMap> getChildDevicesPython(DWORD devInst) override;
    QString findHidDeviceForPortChain(const QString& portChain) override;
    QString getPortChainForSerialPort(const QString& portName) override;

private:
    /**
     * @brief Internal helper to safely get device property from registry
     * @param devInst Device instance
     * @param property CM_DRP property constant
     * @param buffer Output buffer
     * @param bufferSize Buffer size
     * @return True if successful
     */
    bool getDevNodeProperty(DWORD devInst, ULONG property, void* buffer, ULONG* bufferSize);
    
    /**
     * @brief Convert wide string to QString
     * @param wstr Wide string
     * @return QString
     */
    QString wideToQString(const wchar_t* wstr);
};

#endif // WINDEVICEENUMERATOR_H
#endif // _WIN32
