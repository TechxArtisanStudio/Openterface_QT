/*
* ========================================================================== *
*                                                                            *
*    This file is part of the Openterface Mini KVM App QT version            *
*                                                                            *
*    Copyright (C) 2024   <info@openterface.com>                             *
*                                                                            *
*    This program is free software: you can redistribute it and/or modify    *
*    it under the terms of the GNU General Public License as published by    *
*    the Free Software Foundation version 3.                                 *
*                                                                            *
*    This program is distributed in the hope that it will be useful, but     *
*    WITHOUT ANY WARRANTY; without even the implied warranty of              *
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU        *
*    General Public License for more details.                                *
*                                                                            *
*    You should have received a copy of the GNU General Public License       *
*    along with this program. If not, see <http://www.gnu.org/licenses/>.    *
*                                                                            *
* ========================================================================== *
*/

#include "devicecoordinator.h"
#include "host/cameramanager.h"
#include "device/HotplugMonitor.h"
#include "ui/globalsetting.h"
#include <QAction>
#include <QMap>
#include <QDebug>

Q_LOGGING_CATEGORY(log_ui_devicecoordinator, "opf.ui.devicecoordinator")

// VID/PID constants definition
const QString DeviceCoordinator::MINI_KVM_VID = "534D";
const QString DeviceCoordinator::MINI_KVM_PID = "2109";
const QString DeviceCoordinator::KVMGO_VID = "345F";
const QString DeviceCoordinator::KVMGO_PID = "2132";
const QString DeviceCoordinator::KVMVGA_VID = "345F";
const QString DeviceCoordinator::KVMVGA_PID = "2109";

DeviceCoordinator::DeviceCoordinator(QMenu *deviceMenu, CameraManager *cameraManager, QObject *parent)
    : QObject(parent)
    , m_deviceMenu(deviceMenu)
    , m_cameraManager(cameraManager)
    , m_deviceMenuGroup(nullptr)
    , m_deviceAutoSelected(false)
{
    qCDebug(log_ui_devicecoordinator) << "DeviceCoordinator created";
}

DeviceCoordinator::~DeviceCoordinator()
{
    qCDebug(log_ui_devicecoordinator) << "DeviceCoordinator destroyed";
    
    // Clean up action group
    if (m_deviceMenuGroup) {
        delete m_deviceMenuGroup;
        m_deviceMenuGroup = nullptr;
    }
}

void DeviceCoordinator::setupDeviceMenu()
{
    qCDebug(log_ui_devicecoordinator) << "Setting up device menu";
    
    // Initialize device menu group
    if (!m_deviceMenuGroup) {
        m_deviceMenuGroup = new QActionGroup(this);
        m_deviceMenuGroup->setExclusive(true);
        
        // Connect to device menu group
        connect(m_deviceMenuGroup, &QActionGroup::triggered, this, &DeviceCoordinator::onDeviceSelected);
    }
    
    // Initial population of device menu
    updateDeviceMenu();
}

void DeviceCoordinator::updateDeviceMenu()
{
    if (!m_deviceMenuGroup || !m_deviceMenu) {
        qCWarning(log_ui_devicecoordinator) << "Device menu or action group not initialized";
        return;
    }
    
    // Clear existing device actions
    m_deviceMenu->clear();
    qDeleteAll(m_deviceMenuGroup->actions());
    
    // Get available devices from DeviceManager
    DeviceManager& deviceManager = DeviceManager::getInstance();
    QList<DeviceInfo> devices = deviceManager.discoverDevices(); // Force discovery for up-to-date list
    
    // Get currently selected device port chain
    QString currentPortChain = GlobalSetting::instance().getOpenterfacePortChain();
    
    qCDebug(log_ui_devicecoordinator) << "Updating device menu with" << devices.size() 
                                      << "devices. Current port chain:" << currentPortChain;
    
    if (devices.isEmpty()) {
        // Add "No devices available" placeholder
        QAction *noDevicesAction = new QAction(tr("No devices available"), this);
        noDevicesAction->setEnabled(false);
        m_deviceMenu->addAction(noDevicesAction);
        return;
    }
    
    // Deduplicate devices by port chain (similar to DeviceSelectorDialog)
    // First, collect all companion port chains to skip companion devices
    QSet<QString> companionPortChains;
    bool hasKVMGO = false;
    for (const auto& device : devices) {
        if (!device.companionPortChain.isEmpty()) {
            companionPortChains.insert(device.companionPortChain);
            qCDebug(log_ui_devicecoordinator) << "Companion port chain found:" << device.companionPortChain << "for device:" << device.portChain;
        } else {
            qCDebug(log_ui_devicecoordinator) << "No companion port chain for device:" << device.portChain;
        }
        QString deviceType = getDeviceTypeName(device);
        if (deviceType == "KVMGO") {
            hasKVMGO = true;
        }
    }
    
    QMap<QString, DeviceInfo> uniqueDevicesByPortChain;
    for (const auto& device : devices) {
        if (!device.portChain.isEmpty()) {
            // Skip companion devices that are represented by their main device
            if (companionPortChains.contains(device.portChain)) {
                qCDebug(log_ui_devicecoordinator) << "Skipping companion device:" << device.portChain;
                continue;
            }
            // Only keep first occurrence of each port chain
            if (!uniqueDevicesByPortChain.contains(device.portChain)) {
                uniqueDevicesByPortChain.insert(device.portChain, device);
                qCDebug(log_ui_devicecoordinator) << "Added device:" << device.portChain 
                                                  << "Type:" << getDeviceTypeName(device);
            } else {
                qCDebug(log_ui_devicecoordinator) << "Skipping duplicate port chain:" << device.portChain;
            }
        }
    }
    
    // If there is a KVMGO device, merge any non-KVMGO device info into it and remove the non-KVMGO entries
    if (hasKVMGO) {
        QList<QString> nonKvmgoPortChains;
        for (auto it = uniqueDevicesByPortChain.begin(); it != uniqueDevicesByPortChain.end(); ++it) {
            if (getDeviceTypeName(it.value()) != "KVMGO") {
                nonKvmgoPortChains.append(it.key());
            }
        }
        for (const QString& portChain : nonKvmgoPortChains) {
            DeviceInfo nonKvmgoDevice = uniqueDevicesByPortChain.value(portChain);
            // Find the KVMGO device and merge
            for (auto it = uniqueDevicesByPortChain.begin(); it != uniqueDevicesByPortChain.end(); ++it) {
                if (getDeviceTypeName(it.value()) == "KVMGO") {
                    DeviceInfo& kvmgoDevice = it.value();
                    // Merge serial information
                    if (!nonKvmgoDevice.serialPortId.isEmpty()) {
                        kvmgoDevice.serialPortId = nonKvmgoDevice.serialPortId;
                        qCDebug(log_ui_devicecoordinator) << "Merged serial port ID from" << getDeviceTypeName(nonKvmgoDevice) << "to KVMGO:" << nonKvmgoDevice.serialPortId;
                    }
                    if (!nonKvmgoDevice.serialPortPath.isEmpty()) {
                        kvmgoDevice.serialPortPath = nonKvmgoDevice.serialPortPath;
                        qCDebug(log_ui_devicecoordinator) << "Merged serial port path from" << getDeviceTypeName(nonKvmgoDevice) << "to KVMGO:" << nonKvmgoDevice.serialPortPath;
                    }
                    // Also merge other IDs if not set
                    if (kvmgoDevice.hidDeviceId.isEmpty() && !nonKvmgoDevice.hidDeviceId.isEmpty()) {
                        kvmgoDevice.hidDeviceId = nonKvmgoDevice.hidDeviceId;
                        kvmgoDevice.hidDevicePath = nonKvmgoDevice.hidDevicePath;
                    }
                    if (kvmgoDevice.cameraDeviceId.isEmpty() && !nonKvmgoDevice.cameraDeviceId.isEmpty()) {
                        kvmgoDevice.cameraDeviceId = nonKvmgoDevice.cameraDeviceId;
                        kvmgoDevice.cameraDevicePath = nonKvmgoDevice.cameraDevicePath;
                    }
                    if (kvmgoDevice.audioDeviceId.isEmpty() && !nonKvmgoDevice.audioDeviceId.isEmpty()) {
                        kvmgoDevice.audioDeviceId = nonKvmgoDevice.audioDeviceId;
                        kvmgoDevice.audioDevicePath = nonKvmgoDevice.audioDevicePath;
                    }
                    break; // Assuming only one KVMGO
                }
            }
            // Remove the non-KVMGO device
            uniqueDevicesByPortChain.remove(portChain);
            qCDebug(log_ui_devicecoordinator) << "Removed" << getDeviceTypeName(nonKvmgoDevice) << "device after merging:" << portChain;
        }
    }
    
    // Auto-select first device if there's exactly one device and not already auto-selected
    if (uniqueDevicesByPortChain.size() == 1 && !m_deviceAutoSelected) {
        autoSelectFirstDevice();
        currentPortChain = GlobalSetting::instance().getOpenterfacePortChain();
        m_deviceAutoSelected = true;
    }
    
    // Add action for each unique device
    for (auto it = uniqueDevicesByPortChain.begin(); it != uniqueDevicesByPortChain.end(); ++it) {
        const DeviceInfo& device = it.value();
        
        // Determine device type based on VID/PID from platformSpecific data
        QString deviceType = getDeviceTypeName(device);
        QString displayText = QString("Port %1 - %2").arg(device.portChain, deviceType);
        
        QAction *deviceAction = new QAction(displayText, this);
        deviceAction->setCheckable(true);
        deviceAction->setData(device.portChain);
        
        // Mark current device with a checkmark
        if (device.portChain == currentPortChain) {
            deviceAction->setChecked(true);
            qCDebug(log_ui_devicecoordinator) << "Marked current device:" << device.portChain;
        }
        
        m_deviceMenu->addAction(deviceAction);
        m_deviceMenuGroup->addAction(deviceAction);
    }
    
    qCDebug(log_ui_devicecoordinator) << "Device menu updated with" 
                                      << uniqueDevicesByPortChain.size() << "unique devices";
}

QString DeviceCoordinator::getCurrentDevicePortChain() const
{
    return GlobalSetting::instance().getOpenterfacePortChain();
}

void DeviceCoordinator::connectHotplugMonitor(HotplugMonitor *hotplugMonitor)
{
    if (!hotplugMonitor) {
        qCWarning(log_ui_devicecoordinator) << "Cannot connect to null hotplug monitor";
        return;
    }
    
    // Connect hotplug events to update device menu
    connect(hotplugMonitor, &HotplugMonitor::newDevicePluggedIn,
            this, &DeviceCoordinator::onDevicePluggedIn);
    connect(hotplugMonitor, &HotplugMonitor::deviceUnplugged,
            this, &DeviceCoordinator::onDeviceUnplugged);
    
    qCDebug(log_ui_devicecoordinator) << "Connected to hotplug monitor";
}

void DeviceCoordinator::onDeviceSelected(QAction *action)
{
    QString portChain = action->data().toString();
    qCDebug(log_ui_devicecoordinator) << "Device selected from menu:" << portChain;
    
    if (portChain.isEmpty()) {
        qCWarning(log_ui_devicecoordinator) << "Empty port chain selected";
        emit deviceSelected("", false, "Empty port chain");
        return;
    }
    
    // Use the centralized device switching function
    DeviceManager& deviceManager = DeviceManager::getInstance();
    auto result = deviceManager.switchToDeviceByPortChainWithCamera(portChain, m_cameraManager);
    
    // Log the result
    if (result.success) {
        qCInfo(log_ui_devicecoordinator) << "✓ Device switch successful:" << result.statusMessage;
        emit deviceSelected(portChain, true, result.statusMessage);
    } else {
        qCWarning(log_ui_devicecoordinator) << "Device switch failed or partial:" << result.statusMessage;
        emit deviceSelected(portChain, false, result.statusMessage);
    }
    
    // Update device menu to reflect current selection
    updateDeviceMenu();
}

void DeviceCoordinator::onDevicePluggedIn(const DeviceInfo &device)
{
    qCDebug(log_ui_devicecoordinator) << "Device plugged in:" << device.portChain;
    
    // Update device menu when new device is plugged in
    updateDeviceMenu();
    emit deviceMenuUpdateRequested();
}

void DeviceCoordinator::onDeviceUnplugged(const DeviceInfo &device)
{
    qCDebug(log_ui_devicecoordinator) << "Device unplugged:" << device.portChain;
    
    // Update device menu when device is unplugged
    updateDeviceMenu();
    
    // Reset auto-selection flag if no devices are left
    DeviceManager& deviceManager = DeviceManager::getInstance();
    QList<DeviceInfo> devices = deviceManager.discoverDevices();
    if (devices.isEmpty()) {
        m_deviceAutoSelected = false;
    }
    
    emit deviceMenuUpdateRequested();
}

QString DeviceCoordinator::getDeviceTypeName(const DeviceInfo &device)
{
    // Helper lambda to check VID/PID and return device type
    auto checkDeviceType = [this](const QString& str) -> QString {
        if (str.isEmpty()) {
            return "";
        }
        
        // Check for KVMVGA (345F:2109)
        if (checkVidPidInString(str, KVMVGA_VID, KVMVGA_PID)) {
            return "KVMVGA";
        }
        
        // Check for KVMGO (345F:2132)
        if (checkVidPidInString(str, KVMGO_VID, KVMGO_PID)) {
            return "KVMGO";
        }
        
        // Check for Mini-KVM (534D:2109)
        if (checkVidPidInString(str, MINI_KVM_VID, MINI_KVM_PID)) {
            return "Mini-KVM";
        }
        
        return "";
    };
    
    // Check main device sources
    QStringList sources = {
        device.platformSpecific.value("hardwareId").toString(),
        device.platformSpecific.value("hardware_id").toString(),
        device.platformSpecific.value("vidPid").toString(),
        device.deviceInstanceId,
        device.hidDeviceId,
        device.cameraDeviceId,
        device.audioDeviceId,
        device.serialPortId
    };
    
    for (const QString& source : sources) {
        QString result = checkDeviceType(source);
        if (!result.isEmpty()) {
            return result;
        }
    }
    
    // Check sibling and child devices
    auto checkRelatedDevices = [&](const QVariantList& devices) -> QString {
        for (const QVariant& deviceVar : devices) {
            QVariantMap deviceMap = deviceVar.toMap();
            QStringList relatedSources = {
                deviceMap.value("hardwareId").toString(),
                deviceMap.value("hardware_id").toString(),
                deviceMap.value("deviceInstanceId").toString()
            };
            for (const QString& source : relatedSources) {
                QString result = checkDeviceType(source);
                if (!result.isEmpty()) {
                    return result;
                }
            }
        }
        return "";
    };
    
    if (device.platformSpecific.contains("siblings")) {
        QString result = checkRelatedDevices(device.platformSpecific["siblings"].toList());
        if (!result.isEmpty()) {
            return result;
        }
    }
    
    if (device.platformSpecific.contains("children")) {
        QString result = checkRelatedDevices(device.platformSpecific["children"].toList());
        if (!result.isEmpty()) {
            return result;
        }
    }
    
    return "Openterface";
}

bool DeviceCoordinator::checkVidPidInString(const QString &str, const QString &vid, const QString &pid)
{
    if (str.isEmpty()) {
        return false;
    }
    
    // Check for VID_XXXX and PID_XXXX format (Windows style)
    bool hasVidFormat = str.contains(QString("VID_%1").arg(vid), Qt::CaseInsensitive);
    bool hasPidFormat = str.contains(QString("PID_%1").arg(pid), Qt::CaseInsensitive);
    
    // Check for simple VID:PID or just presence of both strings
    bool hasVid = str.contains(vid, Qt::CaseInsensitive);
    bool hasPid = str.contains(pid, Qt::CaseInsensitive);
    
    return (hasVidFormat && hasPidFormat) || (hasVid && hasPid);
}

bool DeviceCoordinator::autoSelectFirstDevice()
{
    qCDebug(log_ui_devicecoordinator) << "Auto-selecting first available device";
    
    DeviceManager& deviceManager = DeviceManager::getInstance();
    QList<DeviceInfo> devices = deviceManager.discoverDevices();
    
    // Deduplicate devices by port chain
    // First, collect all companion port chains to skip companion devices
    QSet<QString> companionPortChains;
    bool hasKVMGO = false;
    for (const auto& device : devices) {
        if (!device.companionPortChain.isEmpty()) {
            companionPortChains.insert(device.companionPortChain);
        }
        QString deviceType = getDeviceTypeName(device);
        if (deviceType == "KVMGO") {
            hasKVMGO = true;
        }
    }
    
    QMap<QString, DeviceInfo> uniqueDevicesByPortChain;
    for (const auto& device : devices) {
        if (!device.portChain.isEmpty() && !companionPortChains.contains(device.portChain)) {
            if (!uniqueDevicesByPortChain.contains(device.portChain)) {
                uniqueDevicesByPortChain.insert(device.portChain, device);
            }
        }
    }
    
    // If there is a KVMGO device, merge any non-KVMGO device info into it and remove the non-KVMGO entries
    if (hasKVMGO) {
        QList<QString> nonKvmgoPortChains;
        for (auto it = uniqueDevicesByPortChain.begin(); it != uniqueDevicesByPortChain.end(); ++it) {
            if (getDeviceTypeName(it.value()) != "KVMGO") {
                nonKvmgoPortChains.append(it.key());
            }
        }
        for (const QString& portChain : nonKvmgoPortChains) {
            DeviceInfo nonKvmgoDevice = uniqueDevicesByPortChain.value(portChain);
            // Find the KVMGO device and merge
            for (auto it = uniqueDevicesByPortChain.begin(); it != uniqueDevicesByPortChain.end(); ++it) {
                if (getDeviceTypeName(it.value()) == "KVMGO") {
                    DeviceInfo& kvmgoDevice = it.value();
                    // Merge serial information
                    if (!nonKvmgoDevice.serialPortId.isEmpty()) {
                        kvmgoDevice.serialPortId = nonKvmgoDevice.serialPortId;
                    }
                    if (!nonKvmgoDevice.serialPortPath.isEmpty()) {
                        kvmgoDevice.serialPortPath = nonKvmgoDevice.serialPortPath;
                    }
                    // Also merge other IDs if not set
                    if (kvmgoDevice.hidDeviceId.isEmpty() && !nonKvmgoDevice.hidDeviceId.isEmpty()) {
                        kvmgoDevice.hidDeviceId = nonKvmgoDevice.hidDeviceId;
                        kvmgoDevice.hidDevicePath = nonKvmgoDevice.hidDevicePath;
                    }
                    if (kvmgoDevice.cameraDeviceId.isEmpty() && !nonKvmgoDevice.cameraDeviceId.isEmpty()) {
                        kvmgoDevice.cameraDeviceId = nonKvmgoDevice.cameraDeviceId;
                        kvmgoDevice.cameraDevicePath = nonKvmgoDevice.cameraDevicePath;
                    }
                    if (kvmgoDevice.audioDeviceId.isEmpty() && !nonKvmgoDevice.audioDeviceId.isEmpty()) {
                        kvmgoDevice.audioDeviceId = nonKvmgoDevice.audioDeviceId;
                        kvmgoDevice.audioDevicePath = nonKvmgoDevice.audioDevicePath;
                    }
                    break; // Assuming only one KVMGO
                }
            }
            // Remove the non-KVMGO device
            uniqueDevicesByPortChain.remove(portChain);
        }
    }
    
    if (uniqueDevicesByPortChain.isEmpty()) {
        qCWarning(log_ui_devicecoordinator) << "No devices available for auto-selection";
        return false;
    }
    
    QString firstPortChain = uniqueDevicesByPortChain.firstKey();
    qCDebug(log_ui_devicecoordinator) << "Auto-selecting first device with port chain:" << firstPortChain;
    
    // Set the first device as current
    GlobalSetting::instance().setOpenterfacePortChain(firstPortChain);
    
    // Use the centralized device switching function to actually switch to the first device
    auto result = deviceManager.switchToDeviceByPortChainWithCamera(firstPortChain, m_cameraManager);
    
    if (result.success) {
        qCInfo(log_ui_devicecoordinator) << "✓ Auto-selected device successfully:" << result.statusMessage;
        emit deviceSelected(firstPortChain, true, result.statusMessage);
        emit deviceSwitchCompleted();
        return true;
    } else {
        qCWarning(log_ui_devicecoordinator) << "Auto-selection failed, retrying in 2 seconds:" << result.statusMessage;
        // Retry after 2 seconds in case serial/audio becomes available
        QTimer::singleShot(500, this, [this, firstPortChain]() {
            DeviceManager& deviceManager = DeviceManager::getInstance();
            auto retryResult = deviceManager.switchToDeviceByPortChainWithCamera(firstPortChain, m_cameraManager);
            if (retryResult.success) {
                qCInfo(log_ui_devicecoordinator) << "✓ Auto-selected device successfully on retry:" << retryResult.statusMessage;
                emit deviceSelected(firstPortChain, true, retryResult.statusMessage);
                emit deviceSwitchCompleted();
            } else {
                qCWarning(log_ui_devicecoordinator) << "Auto-selection failed on retry:" << retryResult.statusMessage;
                emit deviceSelected(firstPortChain, false, retryResult.statusMessage);
                emit deviceSwitchCompleted();
            }
        });
        emit deviceSwitchCompleted();
        return false; // Return false immediately, but retry happens asynchronously
    }
}
