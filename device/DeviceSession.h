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

#ifndef DEVICESESSION_H
#define DEVICESESSION_H

#include <QString>
#include <QDateTime>
#include <QList>
#include <cstdint>

// Interface type enumeration
// Represents the four interfaces a KVM device can expose
enum class InterfaceType : uint8_t {
    Serial,    // Serial port (control channel)
    Hid,       // HID device (keyboard/mouse)
    Camera,    // Camera (video capture)
    Audio      // Audio device
};

// Per-interface state
enum class InterfaceState : uint8_t {
    Absent,          // Interface not present on this device
    Disconnected,    // Interface present but not connected
    Connecting,      // Connection in progress
    Connected,       // Connected and operational
    Error            // Connection failed (retries exhausted)
};

// Overall device session state
enum class DeviceSessionState : uint8_t {
    Disconnected,    // Device not present
    Enumerating,     // Device detected, waiting for interfaces to appear
    Connecting,      // Interfaces being opened in sequence
    Ready,           // All present interfaces operational
    Degraded,        // Some interfaces operational, others failed
    Recovering       // Device was disconnected, attempting recovery
};

// Information about a single interface's connection state
struct InterfaceInfo {
    InterfaceState state = InterfaceState::Absent;
    QString path;                    // e.g. "COM3", "\\.\hid#...", camera device id
    int retryCount = 0;
    int maxRetries = 7;
    QDateTime lastStateChange;
    QString lastError;

    bool isActive() const {
        return state == InterfaceState::Connected || state == InterfaceState::Connecting;
    }

    bool isTerminal() const {
        return state == InterfaceState::Connected || state == InterfaceState::Error;
    }
};

// Represents one physical KVM device and its complete lifecycle.
// The central data structure owned by DeviceLifecycleManager.
struct DeviceSession {
    // Identity
    QString sessionKey;              // = portChain (or composite key for USB 3.0)
    QString portChain;               // Serial port chain
    QString companionPortChain;      // USB 3.0 companion (camera/HID may be on different chain)
    QString vid;
    QString pid;

    // Overall state
    DeviceSessionState state = DeviceSessionState::Disconnected;

    // Four interfaces
    InterfaceInfo serial;
    InterfaceInfo hid;
    InterfaceInfo camera;
    InterfaceInfo audio;

    // Lifecycle tracking
    QDateTime firstSeen;
    QDateTime lastActivity;
    int totalReconnects = 0;

    // Computed helpers
    int presentInterfaceCount() const {
        int count = 0;
        if (serial.state != InterfaceState::Absent) count++;
        if (hid.state != InterfaceState::Absent) count++;
        if (camera.state != InterfaceState::Absent) count++;
        if (audio.state != InterfaceState::Absent) count++;
        return count;
    }

    int readyInterfaceCount() const {
        int count = 0;
        if (serial.state == InterfaceState::Connected) count++;
        if (hid.state == InterfaceState::Connected) count++;
        if (camera.state == InterfaceState::Connected
            || camera.state == InterfaceState::Absent) count++;
        if (audio.state == InterfaceState::Connected
            || audio.state == InterfaceState::Absent) count++;
        return count;
    }

    bool isFullyReady() const {
        return state == DeviceSessionState::Ready;
    }

    bool hasAnyConnectedInterface() const {
        return serial.state == InterfaceState::Connected
            || hid.state == InterfaceState::Connected
            || camera.state == InterfaceState::Connected
            || audio.state == InterfaceState::Connected;
    }

    bool hasAnyInterfaceInError() const {
        return serial.state == InterfaceState::Error
            || hid.state == InterfaceState::Error
            || camera.state == InterfaceState::Error
            || audio.state == InterfaceState::Error;
    }

    bool allRetriesExhausted() const {
        auto exhausted = [](const InterfaceInfo& iface) {
            return iface.state != InterfaceState::Error || iface.retryCount >= iface.maxRetries;
        };
        return (serial.state == InterfaceState::Absent || exhausted(serial))
            && (hid.state == InterfaceState::Absent || exhausted(hid))
            && (camera.state == InterfaceState::Absent || exhausted(camera))
            && (audio.state == InterfaceState::Absent || exhausted(audio));
    }
};

// Reconnect policy with exponential backoff
// Total window: ~26.5s — covers most target reboot scenarios
struct ReconnectPolicy {
    QList<int> intervals = {500, 1000, 2000, 3000, 5000, 5000, 10000};
    int maxAttempts = 7;

    int getDelayMs(int attemptIndex) const {
        if (attemptIndex < 0) return 0;
        if (attemptIndex < intervals.size()) return intervals[attemptIndex];
        return intervals.last();
    }

    bool shouldRetry(int attemptIndex) const {
        return attemptIndex < maxAttempts;
    }
};

// Convert enum to string for logging
inline QString interfaceTypeToString(InterfaceType type) {
    switch (type) {
    case InterfaceType::Serial:  return "Serial";
    case InterfaceType::Hid:     return "HID";
    case InterfaceType::Camera:  return "Camera";
    case InterfaceType::Audio:   return "Audio";
    }
    return "Unknown";
}

inline QString interfaceStateToString(InterfaceState state) {
    switch (state) {
    case InterfaceState::Absent:        return "Absent";
    case InterfaceState::Disconnected:  return "Disconnected";
    case InterfaceState::Connecting:    return "Connecting";
    case InterfaceState::Connected:     return "Connected";
    case InterfaceState::Error:         return "Error";
    }
    return "Unknown";
}

inline QString sessionStateToString(DeviceSessionState state) {
    switch (state) {
    case DeviceSessionState::Disconnected:  return "Disconnected";
    case DeviceSessionState::Enumerating:   return "Enumerating";
    case DeviceSessionState::Connecting:    return "Connecting";
    case DeviceSessionState::Ready:         return "Ready";
    case DeviceSessionState::Degraded:      return "Degraded";
    case DeviceSessionState::Recovering:    return "Recovering";
    }
    return "Unknown";
}

#endif // DEVICESESSION_H
