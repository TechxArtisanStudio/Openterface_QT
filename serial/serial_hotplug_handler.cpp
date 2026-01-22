#include "serial_hotplug_handler.h"
#include "../device/DeviceManager.h"
#include "../device/HotplugMonitor.h"
#include "../device/DeviceInfo.h"

#include <QDebug>
#include <QTimer>
#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(log_core_serial)

SerialHotplugHandler::SerialHotplugHandler(QObject* parent)
    : QObject(parent)
    , hotplug_monitor_(nullptr)
    , serial_open_(false)
    , shutting_down_(false)
    , auto_connect_timer_1_(nullptr)
    , auto_connect_timer_2_(nullptr)
    , allow_auto_connect_(false)
    , pending_auto_connect_(false)
{
    auto_connect_timer_1_ = new QTimer(this);
    auto_connect_timer_1_->setSingleShot(true);
    connect(auto_connect_timer_1_, &QTimer::timeout, this, &SerialHotplugHandler::OnAutoConnectTimer1);

    auto_connect_timer_2_ = new QTimer(this);
    auto_connect_timer_2_->setSingleShot(true);
    connect(auto_connect_timer_2_, &QTimer::timeout, this, &SerialHotplugHandler::OnAutoConnectTimer2);
}

SerialHotplugHandler::~SerialHotplugHandler()
{
    DisconnectFromHotplugMonitor();
}

void SerialHotplugHandler::ConnectToHotplugMonitor()
{
    qCDebug(log_core_serial) << "SerialHotplugHandler: Connecting to hotplug monitor";

    DeviceManager& deviceManager = DeviceManager::getInstance();
    hotplug_monitor_ = deviceManager.getHotplugMonitor();

    if (!hotplug_monitor_) {
        qCWarning(log_core_serial) << "SerialHotplugHandler: Failed to get hotplug monitor from device manager";
        return;
    }

    connect(hotplug_monitor_, &HotplugMonitor::deviceUnplugged,
            this, &SerialHotplugHandler::OnDeviceUnplugged);

    connect(hotplug_monitor_, &HotplugMonitor::newDevicePluggedIn,
            this, &SerialHotplugHandler::OnDevicePluggedIn);

    qCDebug(log_core_serial) << "SerialHotplugHandler successfully connected to hotplug monitor";
}

void SerialHotplugHandler::DisconnectFromHotplugMonitor()
{
    qCDebug(log_core_serial) << "SerialHotplugHandler: Disconnecting from hotplug monitor";
    if (hotplug_monitor_) {
        disconnect(hotplug_monitor_, nullptr, this, nullptr);
        hotplug_monitor_ = nullptr;
        qCDebug(log_core_serial) << "SerialHotplugHandler disconnected from hotplug monitor";
    }
    CancelAutoConnectAttempts();
    pending_auto_connect_ = false;
    pending_port_chain_.clear();
    allow_auto_connect_ = false;
}

void SerialHotplugHandler::SetCurrentSerialPortPortChain(const QString& portChain)
{
    current_port_chain_ = portChain;
    qCDebug(log_core_serial) << "SerialHotplugHandler: Set current port chain to" << current_port_chain_;
}

void SerialHotplugHandler::SetSerialOpen(bool open)
{
    serial_open_ = open;
    if (serial_open_) {
        CancelAutoConnectAttempts();
    }
}

void SerialHotplugHandler::SetShuttingDown(bool shuttingDown)
{
    shutting_down_ = shuttingDown;
    if (shutting_down_) {
        CancelAutoConnectAttempts();
        pending_auto_connect_ = false;
        pending_port_chain_.clear();
    }
}

void SerialHotplugHandler::SetAllowAutoConnect(bool allow)
{
    allow_auto_connect_ = allow;
    qCDebug(log_core_serial) << "SerialHotplugHandler: SetAllowAutoConnect(" << allow << ")";

    if (allow_auto_connect_ && pending_auto_connect_ && !pending_port_chain_.isEmpty()) {
        qCInfo(log_core_serial) << "SerialHotplugHandler: Processing deferred auto-connect for port chain:" << pending_port_chain_;
        // Schedule the auto-connect attempts now using the stored pending port chain
        // Use same scheduling as OnDevicePluggedIn
        const int initialDelayMs = 250;
        const int retryDelayMs = 750;

        CancelAutoConnectAttempts();
        current_port_chain_ = pending_port_chain_;
        auto_connect_timer_1_->start(initialDelayMs);
        auto_connect_timer_2_->setInterval(initialDelayMs + retryDelayMs);

        // Clear pending state
        pending_auto_connect_ = false;
        pending_port_chain_.clear();
    } else if (!allow_auto_connect_) {
        // If disabling, cancel any active timers
        CancelAutoConnectAttempts();
    }
}

void SerialHotplugHandler::CancelAutoConnectAttempts()
{
    if (auto_connect_timer_1_->isActive()) {
        auto_connect_timer_1_->stop();
    }
    if (auto_connect_timer_2_->isActive()) {
        auto_connect_timer_2_->stop();
    }
    // Clear any in-progress/pending state
    auto_connect_in_progress_.store(false);
    pending_auto_connect_ = false;
    pending_port_chain_.clear();
}

void SerialHotplugHandler::OnDeviceUnplugged(const DeviceInfo& device)
{
    qCInfo(log_core_serial) << "SerialHotplugHandler: Device unplugged event received";
    qCInfo(log_core_serial) << "  Port Chain:" << device.portChain;
    qCInfo(log_core_serial) << "  Current port chain:" << current_port_chain_;
    qCInfo(log_core_serial) << "  Serial open:" << serial_open_;

    if (!current_port_chain_.isEmpty() && current_port_chain_ == device.portChain) {
        qCInfo(log_core_serial) << "  → Our current serial device was unplugged, notifying manager";

        // Use a queued single-shot to avoid blocking hotplug thread
        QTimer::singleShot(0, this, [this, device]() {
            emit SerialPortUnplugged(device.portChain);
        });
    } else {
        qCDebug(log_core_serial) << "  → Unplugged device is not our current serial device, ignoring";
    }
}

void SerialHotplugHandler::OnDevicePluggedIn(const DeviceInfo& device)
{
    qCInfo(log_core_serial) << "SerialHotplugHandler: New device plugged in event received";
    qCInfo(log_core_serial) << "  Port Chain:" << device.portChain;
    qCInfo(log_core_serial) << "  Serial open:" << serial_open_;

    if (shutting_down_) {
        qCDebug(log_core_serial) << "  → Shutting down, ignoring plugged-in device";
        return;
    }

    if (!serial_open_) {
        // If auto-connect is not yet allowed (e.g., manager still initializing), save pending request
        if (!allow_auto_connect_) {
            pending_auto_connect_ = true;
            pending_port_chain_ = device.portChain;
            qCInfo(log_core_serial) << "  → Auto-connect deferred until manager initialization completes for port chain:" << device.portChain;
            return;
        }

        // If an auto-connect flow for this port chain is already in progress, ignore duplicate insertion events
        if (auto_connect_in_progress_.load() && pending_port_chain_ == device.portChain) {
            qCDebug(log_core_serial) << "  → Auto-connect already in progress for" << device.portChain << "- ignoring duplicate";
            return;
        }

        // Schedule two attempts like existing SerialPortManager behavior
        const int initialDelayMs = 250;
        const int retryDelayMs = 750;

        // Cancel previous attempts if any
        CancelAutoConnectAttempts();

        // Mark that an auto-connect flow is now in progress
        auto_connect_in_progress_.store(true);

        auto_connect_timer_1_->start(initialDelayMs);
        // timer 2 will be started when timer1 fires to preserve same spacing behavior
        auto_connect_timer_2_->setInterval(initialDelayMs + retryDelayMs);

        // For safety, store last seen port chain
        current_port_chain_ = device.portChain;

        qCInfo(log_core_serial) << "  → Scheduled auto-connect attempts for port chain:" << device.portChain;
    } else {
        qCDebug(log_core_serial) << "  → Serial already open, not auto-connecting";
    }
}

void SerialHotplugHandler::OnAutoConnectTimer1()
{
    if (!current_port_chain_.isEmpty()) {
        // Avoid emitting auto-connect requests if auto-connect is no longer allowed
        if (!allow_auto_connect_) {
            qCInfo(log_core_serial) << "SerialHotplugHandler: Auto-connect attempt skipped because auto-connect disabled for" << current_port_chain_;
            return;
        }

        qCDebug(log_core_serial) << "SerialHotplugHandler: Auto-connect attempt #1 for" << current_port_chain_;
        emit AutoConnectRequested(current_port_chain_);

        // Start second timer for retry (if not canceled)
        if (!auto_connect_timer_2_->isActive()) {
            // Remaining time already set in OnDevicePluggedIn to initial+retry; compute the delay for the second event relative to now
            int delay = auto_connect_timer_2_->interval() - auto_connect_timer_1_->interval();
            if (delay < 0) delay = 750; // fallback
            auto_connect_timer_2_->start(delay);
        }
    }
}

void SerialHotplugHandler::OnAutoConnectTimer2()
{
    if (!current_port_chain_.isEmpty()) {
        if (!allow_auto_connect_) {
            qCInfo(log_core_serial) << "SerialHotplugHandler: Auto-connect attempt #2 skipped because auto-connect disabled for" << current_port_chain_;
            // Clear in-progress since auto-connect is disabled
            auto_connect_in_progress_.store(false);
            return;
        }
        qCDebug(log_core_serial) << "SerialHotplugHandler: Auto-connect attempt #2 for" << current_port_chain_;
        emit AutoConnectRequested(current_port_chain_);

        // Completed both scheduled attempts; clear in-progress flag so future plug events may schedule new attempts
        auto_connect_in_progress_.store(false);
    }
}
