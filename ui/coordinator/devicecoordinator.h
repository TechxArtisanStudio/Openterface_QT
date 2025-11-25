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

#ifndef DEVICECOORDINATOR_H
#define DEVICECOORDINATOR_H

#include <QObject>
#include <QMenu>
#include <QActionGroup>
#include <QString>
#include <QLoggingCategory>
#include "device/DeviceManager.h"

Q_DECLARE_LOGGING_CATEGORY(log_ui_devicecoordinator)

class CameraManager;

/**
 * @brief Coordinates device detection, selection, and menu management
 * 
 * This class handles all device-related UI operations including:
 * - Device menu setup and updates
 * - Device type detection (VID/PID matching)
 * - Device selection handling
 * - Hotplug monitor integration
 * - Camera coordination during device switching
 */
class DeviceCoordinator : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Construct a new Device Coordinator
     * @param deviceMenu Pointer to the device menu to manage
     * @param cameraManager Pointer to camera manager for device switching coordination
     * @param parent Parent QObject
     */
    explicit DeviceCoordinator(QMenu *deviceMenu, CameraManager *cameraManager, QObject *parent = nullptr);
    
    /**
     * @brief Destructor
     */
    ~DeviceCoordinator();

    /**
     * @brief Initialize device menu with current devices
     * Sets up the menu structure and populates with available devices
     */
    void setupDeviceMenu();
    
    /**
     * @brief Update device menu with latest device list
     * Refreshes the menu to reflect current device availability
     */
    void updateDeviceMenu();
    
    /**
     * @brief Get the currently selected device port chain
     * @return QString The port chain of the selected device, or empty if none
     */
    QString getCurrentDevicePortChain() const;
    
    /**
     * @brief Connect to hotplug monitor for device change events
     * @param hotplugMonitor Pointer to the hotplug monitor
     */
    void connectHotplugMonitor(HotplugMonitor *hotplugMonitor);

signals:
    /**
     * @brief Emitted when a device is successfully selected
     * @param portChain The port chain of the selected device
     * @param success Whether the device switch was successful
     * @param message Status message from the device switch operation
     */
    void deviceSelected(const QString &portChain, bool success, const QString &message);
    
    /**
     * @brief Emitted when device menu needs to be updated (e.g., after hotplug event)
     */
    void deviceMenuUpdateRequested();
    
    /**
     * @brief Emitted when device switch is completed to update UI
     */
    void deviceSwitchCompleted();

private slots:
    /**
     * @brief Handle device selection from menu
     * @param action The menu action that was triggered
     */
    void onDeviceSelected(QAction *action);
    
    /**
     * @brief Handle new device plugged in
     * @param device Information about the newly plugged device
     */
    void onDevicePluggedIn(const DeviceInfo &device);
    
    /**
     * @brief Handle device unplugged
     * @param device Information about the unplugged device
     */
    void onDeviceUnplugged(const DeviceInfo &device);

private:
    /**
     * @brief Get human-readable device type name from device info
     * 
     * Analyzes VID/PID information to determine device type:
     * - Mini-KVM (534D:2109)
     * - KVMGO (345F:2132)
     * - KVMVGA (345F:2109)
     * - Generic "Openterface" if VID/PID not recognized
     * 
     * @param device Device information to analyze
     * @return QString Device type name
     */
    QString getDeviceTypeName(const DeviceInfo &device);
    
    /**
     * @brief Check if a string contains specific VID/PID combination
     * @param str String to search (device ID, hardware ID, etc.)
     * @param vid Vendor ID to search for
     * @param pid Product ID to search for
     * @return bool True if both VID and PID are found
     */
    bool checkVidPidInString(const QString &str, const QString &vid, const QString &pid);
    
    /**
     * @brief Auto-select first device if no device is currently selected
     * @return bool True if a device was auto-selected
     */
    bool autoSelectFirstDevice();

    // Member variables
    QMenu *m_deviceMenu;                    ///< Pointer to device menu (not owned)
    CameraManager *m_cameraManager;         ///< Pointer to camera manager (not owned)
    QActionGroup *m_deviceMenuGroup;        ///< Action group for exclusive device selection
    bool m_deviceAutoSelected;              ///< Flag to prevent multiple auto-selections
    
    // VID/PID constants
    static const QString MINI_KVM_VID;      ///< Mini-KVM Vendor ID
    static const QString MINI_KVM_PID;      ///< Mini-KVM Product ID
    static const QString KVMGO_VID;         ///< KVMGO Vendor ID
    static const QString KVMGO_PID;         ///< KVMGO Product ID
    static const QString KVMVGA_VID;        ///< KVMVGA Vendor ID
    static const QString KVMVGA_PID;        ///< KVMVGA Product ID
};

#endif // DEVICECOORDINATOR_H
