#ifndef IDEVICEDISCOVERER_H
#define IDEVICEDISCOVERER_H

#include "device/DeviceInfo.h"
#include <QVector>
#include <QString>
#include <QPair>
#include <QVariantMap>
#include <QLoggingCategory>
#include <memory>

#if defined(_WIN32) && !defined(Q_MOC_RUN)
#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#endif

/**
 * @brief Interface for device discoverers by generation
 * 
 * This interface abstracts device discovery logic for different generations
 * of Openterface devices, allowing each generation to implement its own
 * discovery strategy while maintaining a common interface.
 */
class IDeviceDiscoverer
{
public:
    virtual ~IDeviceDiscoverer() = default;
    
    /**
     * @brief Discover devices of this generation
     * @return Vector of discovered device information
     */
    virtual QVector<DeviceInfo> discoverDevices() = 0;
    
    /**
     * @brief Get the generation name for logging/debugging
     * @return Human-readable generation name
     */
    virtual QString getGenerationName() const = 0;
    
    /**
     * @brief Get the VID/PID pairs that this discoverer handles
     * @return Vector of VID/PID pairs as QString pairs
     */
    virtual QVector<QPair<QString, QString>> getSupportedVidPidPairs() const = 0;
    
    /**
     * @brief Check if this discoverer can handle the given VID/PID
     * @param vid Vendor ID
     * @param pid Product ID
     * @return True if this discoverer handles this VID/PID
     */
    virtual bool supportsVidPid(const QString& vid, const QString& pid) const = 0;

protected:
    /**
     * @brief USB Device Data structure for internal processing
     */
    struct USBDeviceData {
        QString portChain;
        QVector<QVariantMap> siblings;
        QVector<QVariantMap> children;
        QString deviceInstanceId;
        QVariantMap deviceInfo;
    };
};

#endif // IDEVICEDISCOVERER_H