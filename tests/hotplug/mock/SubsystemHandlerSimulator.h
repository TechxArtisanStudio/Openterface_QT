#ifndef SUBSYSTEM_HANDLER_SIMULATOR_H
#define SUBSYSTEM_HANDLER_SIMULATOR_H

#include <QObject>
#include <QString>
#include "device/HotplugMonitor.h"
#include "device/DeviceInfo.h"

/**
 * @brief Simulates a single subsystem's hotplug handler behavior.
 *
 * Replicates the signal/slot connection pattern and portChain matching
 * logic of the real subsystem handlers (SerialHotplugHandler,
 * FFmpegHotplugHandler, VideoHid, AudioManager).
 *
 * Used for integration testing without real hardware dependencies.
 */
class SubsystemHandlerSimulator : public QObject {
    Q_OBJECT
public:
    enum class SubsystemType { Serial, Camera, Hid, Audio };

    explicit SubsystemHandlerSimulator(SubsystemType type, QObject* parent = nullptr)
        : QObject(parent), m_type(type) {}

    /**
     * @brief Connect to HotplugMonitor's plug/unplug signals.
     * Mirrors how each real subsystem connects in its connectToHotplugMonitor().
     */
    void connectToMonitor(HotplugMonitor* monitor) {
        connect(monitor, &HotplugMonitor::newDevicePluggedIn,
                this, &SubsystemHandlerSimulator::onDevicePluggedIn);
        connect(monitor, &HotplugMonitor::deviceUnplugged,
                this, &SubsystemHandlerSimulator::onDeviceUnplugged);
    }

    bool isActive() const { return m_active; }
    QString currentPortChain() const { return m_currentPortChain; }
    int activateCount() const { return m_activateCount; }
    int deactivateCount() const { return m_deactivateCount; }
    SubsystemType type() const { return m_type; }

    /**
     * @brief Check if this simulator would handle the given device.
     * Copies the exact matching logic from each real subsystem handler.
     */
    bool matchesDevice(const DeviceInfo& device) const {
        switch (m_type) {
        case SubsystemType::Serial:
            return device.hasSerialPort();
        case SubsystemType::Camera:
            return device.hasCameraDevice();
        case SubsystemType::Hid:
            return device.hasHidDevice();
        case SubsystemType::Audio:
            // AudioManager also checks for "Openterface" in audioDeviceId
            return device.hasAudioDevice() && device.audioDeviceId.contains("Openterface");
        }
        return false;
    }

    void reset() {
        m_currentPortChain.clear();
        m_active = false;
        m_activateCount = 0;
        m_deactivateCount = 0;
    }

public slots:
    /**
     * @brief Handle device plugged in.
     * Mirrors the real handler pattern: check if device has our interface type,
     * then activate and record the port chain.
     */
    void onDevicePluggedIn(const DeviceInfo& device) {
        if (!matchesDevice(device)) return;
        m_currentPortChain = device.portChain;
        m_active = true;
        m_activateCount++;
    }

    /**
     * @brief Handle device unplugged.
     * Mirrors the real handler pattern: only deactivate if the unplugged
     * device's portChain matches our current port chain.
     */
    void onDeviceUnplugged(const DeviceInfo& device) {
        if (!m_active) return;
        if (m_currentPortChain != device.portChain) return;
        if (!matchesDevice(device)) return;
        m_active = false;
        m_currentPortChain.clear();
        m_deactivateCount++;
    }

private:
    SubsystemType m_type;
    QString m_currentPortChain;
    bool m_active = false;
    int m_activateCount = 0;
    int m_deactivateCount = 0;
};

#endif // SUBSYSTEM_HANDLER_SIMULATOR_H
