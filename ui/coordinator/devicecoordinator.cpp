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
#include "../advance/edid/edididentitycache.h"
#include "host/cameramanager.h"
#include "device/HotplugMonitor.h"
#include "device/DeviceLifecycleManager.h"
#include "serial/SerialPortManager.h"
#include "ui/globalsetting.h"
#include <QAction>
#include <QMap>
#include <QDebug>
#include <QtConcurrent>
#include <QMetaObject>
#include <QPointer>
#include <QThread>
#include <QTimer>
#include "log/opflogging.h"

OPF_LOGGING_CATEGORY(log_ui_devicecoordinator, "opf.ui.devicecoordinator")

DeviceCoordinator::DeviceCoordinator(QMenu *deviceMenu, CameraManager *cameraManager, QObject *parent)
    : QObject(parent)
    , m_deviceMenu(deviceMenu)
    , m_cameraManager(cameraManager)
    , m_deviceMenuGroup(nullptr)
    , m_deviceAutoSelected(false)
{
    // Re-label the menu when a unit's EDID name becomes known.
    connect(&edid::EdidIdentityCache::instance(), &edid::EdidIdentityCache::identityChanged,
            this, [this](const QString &, const edid::EdidIdentity &) { updateDeviceMenu(); });
    connect(&edid::EdidIdentityCache::instance(), &edid::EdidIdentityCache::currentChanged,
            this, [this](const QString &) { updateDeviceMenu(); });

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
    
    // One entry per unit, shared with the selector dialog and MCP device_list
    DeviceManager& deviceManager = DeviceManager::getInstance();
    QList<DeviceInfo> selectable = deviceManager.listSelectableDevices();

    // Get currently selected device port chain
    QString currentPortChain = GlobalSetting::instance().getOpenterfacePortChain();
    qCDebug(log_ui_devicecoordinator) << "Updating device menu with" << selectable.size()
                                      << "devices. Current port chain:" << currentPortChain;

    if (selectable.isEmpty()) {
        // Add "No devices available" placeholder
        QAction *noDevicesAction = new QAction(tr("No devices available"), this);
        noDevicesAction->setEnabled(false);
        m_deviceMenu->addAction(noDevicesAction);
        return;
    }
    QMap<QString, DeviceInfo> uniqueDevicesByPortChain;
    for (const auto& device : selectable) {
        uniqueDevicesByPortChain.insert(device.portChain, device);
    }

    // Device-type merging removed: we only deduplicate by companion port chain

    // Auto-select first device if there's exactly one device and not already auto-selected
    if (uniqueDevicesByPortChain.size() == 1 && !m_deviceAutoSelected) {
        QString firstPortChain = uniqueDevicesByPortChain.firstKey();

        // HOTPLUG FIX: Skip auto-select if DeviceLifecycleManager is already managing
        // this session. During rapid hotplug, DeviceLifecycleManager detects the device
        // change first and starts the connection sequence (shouldConnectSerial → ...).
        // If we also call scheduleAutoSelectFirstDevice here, it triggers a second
        // switchToDeviceByPortChainWithCamera which races with the lifecycle-managed
        // path. Both paths call switchSerialPortByPortChain, causing the serial port
        // to enter CLOSING state and then reject the second open attempt.
        auto& lifecycle = DeviceLifecycleManager::getInstance();
        bool lifecycleManaging = false;
        for (const auto& session : lifecycle.getAllSessions()) {
            if (session.portChain == firstPortChain
                && session.state != DeviceSessionState::Disconnected) {
                lifecycleManaging = true;
                break;
            }
        }

        if (lifecycleManaging) {
            qCDebug(log_ui_devicecoordinator) << "Skipping auto-select: DeviceLifecycleManager already managing session for" << firstPortChain;
            // Still set the port chain in settings so the UI reflects the selection
            GlobalSetting::instance().setOpenterfacePortChain(firstPortChain);
            currentPortChain = firstPortChain;
            m_deviceAutoSelected = true;
        } else {
            // Immediately set the selection in settings so the UI can reflect it
            GlobalSetting::instance().setOpenterfacePortChain(firstPortChain);
            currentPortChain = firstPortChain;
            m_deviceAutoSelected = true; // mark scheduled so we don't schedule multiple times
            scheduleAutoSelectFirstDevice(firstPortChain);
        }
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
        // Prefix the EDID display name when this unit has been identified
        // (see EdidIdentityCache): "BRAIN-G4-KVM - Port 1-3 (/dev/ttyACM0)".
        displayText = edid::decorateLabel(
            displayText, edid::EdidIdentityCache::instance().displayName(device.portChain));

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
    selectDevice(action->data().toString());
}

DeviceManager::DeviceSwitchResult DeviceCoordinator::selectDevice(const QString &portChain)
{
    qCDebug(log_ui_devicecoordinator) << "Device selected:" << portChain;
    DeviceManager::DeviceSwitchResult result{};
    if (portChain.isEmpty()) {
        qCWarning(log_ui_devicecoordinator) << "Empty port chain selected";
        result.statusMessage = "Empty port chain";
        emit deviceSelected("", false, result.statusMessage);
        return result;
    }

    // HOTPLUG FIX: Clear any stale m_openInProgress lock before the legacy switch.
    // Without this, if DeviceLifecycleManager has a concurrent operation (e.g., a hotplug
    // reconnect in progress), switchSerialPortByPortChain rejects the menu's switch with
    // "Open already in progress" → menu selection fails silently.
    SerialPortManager::getInstance().forceResetSerialOpen();

    // Use the centralized device switching function
    DeviceManager& deviceManager = DeviceManager::getInstance();
    result = deviceManager.switchToDeviceByPortChainWithCamera(portChain, m_cameraManager);

    // Log the result
    if (result.success) {
        qCInfo(log_ui_devicecoordinator) << "✓ Device switch successful:" << result.statusMessage;
        emit deviceSelected(portChain, true, result.statusMessage);
    } else {
        qCWarning(log_ui_devicecoordinator) << "Device switch failed or partial:" << result.statusMessage;
        emit deviceSelected(portChain, false, result.statusMessage);
    }
    // The unit's identity follows the HID device, so refresh the cache
    // whenever that part of the switch succeeded -- also on "partial" results
    // (e.g. units without an audio interface). VideoHid does not always emit
    // hidDeviceConnected on a switch (it returns early when it is already on
    // the matching hidraw), hence the explicit refresh.
    if (result.hidSuccess) {
        edid::EdidIdentityCache::instance().refresh(portChain);
    }
    return result;
}

void DeviceCoordinator::onDevicePluggedIn(const DeviceInfo &device)
{
    qCDebug(log_ui_devicecoordinator) << "Device plugged in:" << device.portChain;

    // FIX: Defer updateDeviceMenu() to next event loop iteration.
    // updateDeviceMenu() calls discoverDevices() which performs expensive USB
    // enumeration (SetupAPI across 5 device classes) on the main thread. During
    // hotplug, Windows USB subsystem is busy installing drivers, making this
    // enumeration extremely slow and freezing the entire UI.
    QTimer::singleShot(0, this, [this]() {
        updateDeviceMenu();
        emit deviceMenuUpdateRequested();
    });
}

void DeviceCoordinator::onDeviceUnplugged(const DeviceInfo &device)
{
    qCDebug(log_ui_devicecoordinator) << "Device unplugged:" << device.portChain;

    // FIX: Defer updateDeviceMenu() to next event loop iteration.
    // updateDeviceMenu() calls discoverDevices() which performs expensive USB
    // enumeration on the main thread. During hotplug, Windows USB subsystem is
    // busy installing drivers, making this enumeration extremely slow and
    // freezing the entire UI.
    QTimer::singleShot(0, this, [this, device]() {
        updateDeviceMenu();

        // Use HotplugMonitor's cached snapshot instead of calling discoverDevices()
        // which performs expensive USB enumeration on the main thread.
        DeviceManager& deviceManager = DeviceManager::getInstance();
        HotplugMonitor* monitor = deviceManager.getHotplugMonitor();
        if (monitor) {
            QList<DeviceInfo> cachedDevices = monitor->getLastSnapshot();
            if (cachedDevices.isEmpty()) {
                m_deviceAutoSelected = false;
            }
        } else {
            qCWarning(log_ui_devicecoordinator) << "HotplugMonitor not available, skipping device check";
        }

        emit deviceMenuUpdateRequested();
    });
}

QString DeviceCoordinator::formatPortChain(const QString &portChain)
{
    return DeviceInfo::displayPortChain(portChain);
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

    // HOTPLUG FIX: Clear stale m_openInProgress before legacy switch (same reason as onDeviceSelected)
    SerialPortManager::getInstance().forceResetSerialOpen();

    // Use the centralized device switching function to actually switch to the first device
    auto result = deviceManager.switchToDeviceByPortChainWithCamera(firstPortChain, m_cameraManager);

    if (result.success) {
        qCInfo(log_ui_devicecoordinator) << "✓ Auto-selected device successfully:" << result.statusMessage;
        emit deviceSelected(firstPortChain, true, result.statusMessage);
        emit deviceSwitchCompleted();
        edid::EdidIdentityCache::instance().refresh(firstPortChain);
        return true;
    } else {
        qCWarning(log_ui_devicecoordinator) << "Auto-selection failed, retrying in 2 seconds:" << result.statusMessage;
        // Retry after 2 seconds in case serial/audio becomes available
        QTimer::singleShot(500, this, [this, firstPortChain]() {
            DeviceManager& deviceManager = DeviceManager::getInstance();
            // HOTPLUG FIX: Reset lock again before retry
            SerialPortManager::getInstance().forceResetSerialOpen();
            auto retryResult = deviceManager.switchToDeviceByPortChainWithCamera(firstPortChain, m_cameraManager);
            if (retryResult.success) {
                qCInfo(log_ui_devicecoordinator) << "✓ Auto-selected device successfully on retry:" << retryResult.statusMessage;
                emit deviceSelected(firstPortChain, true, retryResult.statusMessage);
                emit deviceSwitchCompleted();
                edid::EdidIdentityCache::instance().refresh(firstPortChain);
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
    QPointer<CameraManager> safeCameraManager(m_cameraManager);  // QPointer for safe cross-thread access

    // Run device switching scheduled on DeviceManager thread; do not block UI
    (void)QtConcurrent::run([portChain, safeCameraManager, safeThis]() {
        if (!safeThis) {
            qCWarning(log_ui_devicecoordinator) << "DeviceCoordinator destroyed before scheduling auto-select";
            return;
        }

        if (!safeCameraManager) {
            qCWarning(log_ui_devicecoordinator) << "CameraManager destroyed before scheduling auto-select";
            return;
        }

        // Optional small delay to allow UI to reflect selection and other threads (HID) to settle
        QThread::msleep(10);

        DeviceManager &deviceManager = DeviceManager::getInstance();

        // Exponential backoff retry: 10ms, 500ms, 1000ms, 2000ms, 3000ms
        const int retryDelays[] = {10, 500, 1000, 2000, 3000};
        const int maxRetries = 5;

        for (int attempt = 0; attempt < maxRetries; attempt++) {
            if (attempt > 0) {
                qCDebug(log_ui_devicecoordinator) << "Auto-select retry attempt" << (attempt + 1)
                                                   << "/" << maxRetries << "after" << retryDelays[attempt] << "ms";
                QThread::msleep(retryDelays[attempt]);
            }

            // Check QPointer validity before each attempt
            if (!safeCameraManager) {
                qCWarning(log_ui_devicecoordinator) << "CameraManager destroyed during auto-select retry";
                return;
            }

            // Schedule the actual switching to run in the DeviceManager's QObject thread via queued invocation
            bool success = false;
            QMetaObject::invokeMethod(&deviceManager, [portChain, safeCameraManager, &success]() {
                if (!safeCameraManager) {
                    qCWarning(log_ui_devicecoordinator) << "CameraManager destroyed before switch execution";
                    return;
                }
                qCDebug(log_ui_devicecoordinator) << "Queued auto-select switch to port chain:" << portChain;
                DeviceManager &dm = DeviceManager::getInstance();
                // HOTPLUG FIX: Reset lock before each retry attempt
                SerialPortManager::getInstance().forceResetSerialOpen();
                auto result = dm.switchToDeviceByPortChainWithCamera(portChain, safeCameraManager.data());
                success = result.success || result.hidSuccess;
            }, Qt::BlockingQueuedConnection);

            if (success) {
                qCInfo(log_ui_devicecoordinator) << "✓ Auto-select succeeded on attempt" << (attempt + 1);
                return;
            }
        }

        qCWarning(log_ui_devicecoordinator) << "Auto-select failed after" << maxRetries << "attempts for port chain:" << portChain;
    });
}
