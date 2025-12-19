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
#include <QtConcurrent>
#include <QMetaObject>
#include <QPointer>
#include <QThread>

Q_LOGGING_CATEGORY(log_ui_devicecoordinator, "opf.ui.devicecoordinator")

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
    qCDebug(log_ui_devicecoordinator) << "Updating device menu";
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
    for (const auto& device : devices) {
        if (!device.companionPortChain.isEmpty()) {
            companionPortChains.insert(device.companionPortChain);
            qCDebug(log_ui_devicecoordinator) << "Companion port chain found:" << device.companionPortChain << "for device:" << device.portChain;
        } else {
            qCDebug(log_ui_devicecoordinator) << "No companion port chain for device:" << device.portChain;
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
                qCDebug(log_ui_devicecoordinator) << "Added device:" << device.portChain;
            } else {
                qCDebug(log_ui_devicecoordinator) << "Skipping duplicate port chain:" << device.portChain;
            }
        }
    }
    
    // Device-type merging removed: we only deduplicate by companion port chain
    
    // Auto-select first device if there's exactly one device and not already auto-selected
    if (uniqueDevicesByPortChain.size() == 1 && !m_deviceAutoSelected) {
        // Immediately set the selection in settings so the UI can reflect it
        QString firstPortChain = uniqueDevicesByPortChain.firstKey();
        GlobalSetting::instance().setOpenterfacePortChain(firstPortChain);
        currentPortChain = firstPortChain;
        m_deviceAutoSelected = true; // mark scheduled so we don't schedule multiple times
        scheduleAutoSelectFirstDevice(firstPortChain);
    }
    
    // Add action for each unique device
    for (auto it = uniqueDevicesByPortChain.begin(); it != uniqueDevicesByPortChain.end(); ++it) {
        const DeviceInfo& device = it.value();

        // Determine serial info to show (prefer path, fall back to ID)
        QString serialInfo;
        if (!device.serialPortPath.isEmpty()) {
            serialInfo = device.serialPortPath;
        } else if (!device.serialPortId.isEmpty()) {
            serialInfo = device.serialPortId;
        }

        QString displayText = QString("Port %1").arg(formatPortChain(device.portChain));
        if (!serialInfo.isEmpty()) {
            displayText += QString(" (%1)").arg(serialInfo);
        }

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

QString DeviceCoordinator::formatPortChain(const QString &portChain)
{
    // Remove any '0' characters and then place '-' between remaining digits.
    // For example: "010203" -> remove zeros -> "123" -> "1-2-3"
    QString filtered;
    for (QChar c : portChain) {
        if (c.isDigit() && c != QChar('0')) {
            filtered.append(c);
        }
    }
    if (filtered.isEmpty()) {
        // Fallback: if nothing remains after removing zeros, return the original
        return portChain;
    }
    QStringList parts;
    for (QChar c : filtered) {
        parts.append(QString(c));
    }
    return parts.join('-');
}

bool DeviceCoordinator::autoSelectFirstDevice()
{
    qCDebug(log_ui_devicecoordinator) << "Auto-selecting first available device";
    
    DeviceManager& deviceManager = DeviceManager::getInstance();
    QList<DeviceInfo> devices = deviceManager.discoverDevices();
    
    // Deduplicate devices by port chain (skip companion devices)
    QSet<QString> companionPortChains;
    for (const auto& device : devices) {
        if (!device.companionPortChain.isEmpty()) {
            companionPortChains.insert(device.companionPortChain);
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

void DeviceCoordinator::scheduleAutoSelectFirstDevice(const QString &portChain)
{
    qCDebug(log_ui_devicecoordinator) << "Scheduling auto-select for port chain:" << portChain;

    // Use QPointer to safely reference this object from background threads
    QPointer<DeviceCoordinator> safeThis(this);
    CameraManager *cameraManager = m_cameraManager;

    // Run device switching scheduled on DeviceManager thread; do not block UI
    (void)QtConcurrent::run([portChain, cameraManager, safeThis]() {
        if (!safeThis) {
            qCWarning(log_ui_devicecoordinator) << "DeviceCoordinator destroyed before scheduling auto-select";
            return;
        }

        // Optional small delay to allow UI to reflect selection and other threads (HID) to settle
        QThread::msleep(10);

        DeviceManager &deviceManager = DeviceManager::getInstance();
        // Schedule the actual switching to run in the DeviceManager's QObject thread via queued invocation
        QMetaObject::invokeMethod(&deviceManager, [portChain, cameraManager]() {
            qCDebug(log_ui_devicecoordinator) << "Queued auto-select switch to port chain:" << portChain;
            // Resolve deviceManager inside the queued functor to ensure correct context
            DeviceManager &dm = DeviceManager::getInstance();
            auto result = dm.switchToDeviceByPortChainWithCamera(portChain, cameraManager);
            Q_UNUSED(result);
        }, Qt::QueuedConnection);

        // We're intentionally not waiting for switch completion — UI should already be updated
    });
}
