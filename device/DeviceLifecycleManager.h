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

#ifndef DEVICELIFECYCLEMANAGER_H
#define DEVICELIFECYCLEMANAGER_H

#include <QObject>
#include <QMap>
#include <QTimer>
#include <QLoggingCategory>
#include <memory>

#include "DeviceSession.h"

Q_DECLARE_LOGGING_CATEGORY(log_lifecycle)

// Forward declarations
class HotplugMonitor;
struct DeviceChangeEvent;
class DeviceInfo;

// DeviceLifecycleManager: Central authority for device lifecycle management.
//
// Sits above HotplugMonitor and coordinates SerialPortManager, VideoHid,
// CameraManager. Subsystems no longer decide *when* to reconnect — they
// only execute instructions from this manager and report results back.
//
// Thread model: Runs entirely on the main thread. All state mutations
// happen on the main thread — no mutex needed for m_sessions.
class DeviceLifecycleManager : public QObject {
    Q_OBJECT

public:
    static DeviceLifecycleManager& getInstance();

    // ── Query API (subsystems and UI read state) ──
    const DeviceSession* getSession(const QString& sessionKey) const;
    DeviceSessionState getSessionState(const QString& sessionKey) const;
    InterfaceState getInterfaceState(const QString& sessionKey, InterfaceType type) const;
    QList<DeviceSession> getAllSessions() const;
    bool hasActiveSession() const;
    QString getActiveSessionKey() const;

    // ── Report API (subsystems report results) ──
    void notifyInterfaceConnected(const QString& sessionKey, InterfaceType type);
    void notifyInterfaceFailed(const QString& sessionKey, InterfaceType type,
                                const QString& error);
    void notifyInterfaceDisconnected(const QString& sessionKey, InterfaceType type);

    // ── Manual control (user actions) ──
    void requestDisconnect(const QString& sessionKey);
    void requestReconnect(const QString& sessionKey);

    // ── Initial discovery (call once at startup to detect already-connected devices) ──
    void performInitialDiscovery();

    // ── Access to reconnect policy (for test framework) ──
    const ReconnectPolicy& getReconnectPolicy() const { return m_reconnectPolicy; }

signals:
    // ── Command signals (subsystems listen and execute) ──
    void shouldConnectSerial(const QString& sessionKey, const QString& portPath);
    void shouldConnectHid(const QString& sessionKey, const QString& hidPath);
    void shouldConnectCamera(const QString& sessionKey, const QString& cameraDeviceId);
    void shouldConnectAudio(const QString& sessionKey, const QString& audioDeviceId);

    void shouldDisconnectSerial(const QString& sessionKey);
    void shouldDisconnectHid(const QString& sessionKey);
    void shouldDisconnectCamera(const QString& sessionKey);
    void shouldDisconnectAudio(const QString& sessionKey);

    void shouldReleaseHidState(const QString& sessionKey);  // Release stuck keys/buttons

    // ── State change signals (UI and test framework listen) ──
    void sessionStateChanged(const QString& sessionKey, DeviceSessionState newState);
    void sessionAdded(const QString& sessionKey);
    void sessionRemoved(const QString& sessionKey);
    void interfaceStateChanged(const QString& sessionKey, InterfaceType type,
                                InterfaceState newState);

private:
    explicit DeviceLifecycleManager(QObject* parent = nullptr);
    ~DeviceLifecycleManager() override;

    DeviceLifecycleManager(const DeviceLifecycleManager&) = delete;
    DeviceLifecycleManager& operator=(const DeviceLifecycleManager&) = delete;

    QMap<QString, DeviceSession> m_sessions;
    HotplugMonitor* m_hotplugMonitor;
    ReconnectPolicy m_reconnectPolicy;

    // Fast reconnect window timer
    QTimer* m_fastReconnectTimer = nullptr;
    QTimer* m_staleCleanupTimer = nullptr;
    QString m_fastReconnectSessionKey;  // Which session to watch during fast reconnect

    // Connection sequence order
    static constexpr InterfaceType CONNECT_ORDER[] = {
        InterfaceType::Serial,
        InterfaceType::Hid,
        InterfaceType::Camera,
        InterfaceType::Audio
    };
    static constexpr int CONNECT_ORDER_COUNT = 4;

    // ── Hotplug event handling ──
    void onDeviceChangesDetected(const DeviceChangeEvent& event);
    void onDeviceDetected(const DeviceInfo& device);
    void onDeviceRemoved(const DeviceInfo& device);

    // ── State machine ──
    void tryAdvanceSession(const QString& sessionKey, InterfaceType completedType);
    void startConnectingInterfaces(const QString& sessionKey);
    void connectNextInterface(const QString& sessionKey, int orderIndex);
    void scheduleReconnect(const QString& sessionKey, InterfaceType type, int attemptIndex);
    void handleFullDisconnect(const QString& sessionKey);
    void startFastReconnectWindow(const QString& sessionKey);
    void stopFastReconnectWindow();

    // ── Session management ──
    DeviceSession& createSession(const DeviceInfo& device);
    void removeSession(const QString& sessionKey);
    void updateSessionFromDeviceInfo(DeviceSession& session, const DeviceInfo& device);
    void cleanupStaleSessions();

    // ── Interface helpers ──
    InterfaceInfo& getInterfaceInfo(DeviceSession& session, InterfaceType type);
    const InterfaceInfo& getInterfaceInfo(const DeviceSession& session, InterfaceType type) const;
    int connectOrderIndex(InterfaceType type) const;
    bool allPresentInterfacesAreAtLeastDisconnected(const DeviceSession& session) const;
    bool anyInterfaceInError(const DeviceSession& session) const;
    bool allRetriesExhausted(const DeviceSession& session) const;
    int nextFailedInterfaceIndex(const DeviceSession& session) const;
    void setInterfaceState(DeviceSession& session, InterfaceType type, InterfaceState newState);

    // ── Fast reconnect window ──
    void onFastReconnectTimerTimeout();

    // ── Session matching ──
    QString findSessionKeyForDevice(const DeviceInfo& device) const;
    QString findCompanionSessionForDevice(const DeviceInfo& device) const;
    void mergeCompanionDevice(const QString& sessionKey, const DeviceInfo& device);
    bool isSerialOnlyDevice(const DeviceInfo& device) const;
    bool isHidCameraOnlyDevice(const DeviceInfo& device) const;
};

#endif // DEVICELIFECYCLEMANAGER_H
