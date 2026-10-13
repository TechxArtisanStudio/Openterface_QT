#ifndef DEVICEACCESSGUARD_H
#define DEVICEACCESSGUARD_H

#include <QThread>
#include <atomic>
#include "log/opflogging.h"

class VideoHid;

/**
 * @brief RAII guard for exclusive device access during firmware operations.
 *
 * This class ensures that firmware write operations have exclusive access to the HID device
 * by stopping the polling thread and setting the flash-in-progress flag. When the guard is
 * destroyed, it automatically releases exclusive access and clears the flash-in-progress flag.
 *
 * Usage:
 * @code
 * bool VideoHid::writeEeprom(...) {
 *     DeviceAccessGuard guard(*this);
 *     if (!guard.isExclusive()) {
 *         return false;  // Failed to acquire exclusive access
 *     }
 *     // ... perform firmware write ...
 * }  // guard destructor automatically releases exclusive access
 * @endcode
 *
 * Key features:
 * - Sets m_flashInProgress flag to suppress background reads
 * - Cooperatively stops polling thread (no terminate())
 * - Waits up to 5 seconds for graceful shutdown
 * - Automatically cleans up on destruction
 */
class DeviceAccessGuard {
public:
    /**
     * @brief Acquire exclusive device access.
     * @param hid Reference to VideoHid instance
     *
     * Sets m_flashInProgress flag and stops polling thread cooperatively.
     * If polling thread doesn't stop within 5 seconds, marks access as non-exclusive.
     */
    explicit DeviceAccessGuard(VideoHid& hid);

    /**
     * @brief Release exclusive device access.
     *
     * Clears m_flashInProgress flag and cleans up polling thread if it was stopped.
     */
    ~DeviceAccessGuard();

    /**
     * @brief Check if exclusive access was successfully acquired.
     * @return true if exclusive access is held, false if acquisition failed
     */
    bool isExclusive() const { return m_exclusive; }

    // Prevent copying
    DeviceAccessGuard(const DeviceAccessGuard&) = delete;
    DeviceAccessGuard& operator=(const DeviceAccessGuard&) = delete;

private:
    VideoHid& m_hid;
    bool m_exclusive = false;
    bool m_hadPollingThread = false;
};

#endif // DEVICEACCESSGUARD_H
