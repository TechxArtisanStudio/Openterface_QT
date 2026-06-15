/*
* ========================================================================== *
*                                                                            *
*    This file is part of the Openterface Mini KVM App QT version            *
*                                                                            *
* ========================================================================== *
*/

#ifndef SERIAL_HOTPLUG_HANDLER_H
#define SERIAL_HOTPLUG_HANDLER_H

#include <QObject>
#include <QString>
#include <QTimer>
#include <QDateTime>

// Forward declarations
class HotplugMonitor;
struct DeviceInfo;

/**
 * @brief SerialHotplugHandler
 *
 * Encapsulates hotplug monitoring logic for serial devices. Behaviors:
 * - Connect/disconnect to the global HotplugMonitor
 * - Observe device plugged/unplugged events
 * - Emit concise signals for SerialPortManager to act on
 * - Provide auto-connect retry scheduling (two attempts) and cancellation
 */
class SerialHotplugHandler : public QObject
{
    Q_OBJECT
public:
    explicit SerialHotplugHandler(QObject* parent = nullptr);
    ~SerialHotplugHandler();

    // Hotplug monitor connection
    void ConnectToHotplugMonitor();
    void DisconnectFromHotplugMonitor();

    // Current serial device tracking (kept for matching on unplug)
    void SetCurrentSerialPortPortChain(const QString& portChain);
    void SetSerialOpen(bool open);
    void SetShuttingDown(bool shuttingDown);

    // Control whether auto-connect attempts are allowed (helps avoid races during initialization)
    void SetAllowAutoConnect(bool allow);
    bool IsAutoConnectAllowed() const { return allow_auto_connect_; }

    // Cancel any pending auto-connect attempts
    void CancelAutoConnectAttempts();

signals:
    // Emitted when a device that matches current port chain is unplugged
    void SerialPortUnplugged(const QString& portChain);

    // Emitted when a new device is plugged in and auto-connect attempts are scheduled
    void AutoConnectRequested(const QString& portChain);

private slots:
    void OnDeviceUnplugged(const DeviceInfo& device);
    void OnDevicePluggedIn(const DeviceInfo& device);
    void OnAutoConnectTimer1();
    void OnAutoConnectTimer2();

private:
    HotplugMonitor* hotplug_monitor_;
    QString current_port_chain_;
    bool serial_open_;
    bool shutting_down_;

    // Timers for scheduled auto-connect attempts
    QTimer* auto_connect_timer_1_;
    QTimer* auto_connect_timer_2_;

    // Auto-connect control
    bool allow_auto_connect_;
    bool pending_auto_connect_;
    QString pending_port_chain_;

    // Flag to indicate an auto-connect flow is currently scheduled/running
    std::atomic_bool auto_connect_in_progress_{false};

    // Hotplug debounce state to prevent rapid plug/unplug from flooding auto-connect requests
    std::atomic_bool m_debounceActive{false};
    QDateTime m_lastUnplugTime;
    int m_hotplugFailCount = 0;  // Consecutive hotplug failure count for exponential backoff

    // Debounce timing configuration
    static constexpr int DEBOUNCE_WINDOW_MS = 1000;      // Plug events within 1s of unplug are considered bounce
    static constexpr int STABILIZATION_DELAY_MS = 1500;  // Wait time for USB stack to stabilize
};

#endif // SERIAL_HOTPLUG_HANDLER_H
