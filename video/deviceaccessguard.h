#ifndef DEVICEACCESSGUARD_H
#define DEVICEACCESSGUARD_H

#include <QThread>
#include <atomic>
#include "log/opflogging.h"

class VideoHid;

/**
 * @class DeviceAccessGuard
 * @brief RAII guard that acquires exclusive access to the HID device for firmware operations.
 *
 * This class implements the RAII (Resource Acquisition Is Initialization) pattern to manage
 * exclusive device access during firmware write operations. It solves the problem of multiple
 * threads (polling thread, UI thread, firmware worker thread) competing for the same HID
 * device handle.
 *
 * ## Lifecycle
 *
 * - **Constructor**: Sets the `m_flashInProgress` atomic flag (which suppresses background
 *   register reads from the polling thread and any concurrent QtConcurrent operations), then
 *   cooperatively stops the polling thread by setting its `m_running` flag to false and
 *   waiting up to 5 seconds for it to exit gracefully.
 *
 * - **Destructor**: Clears the `m_flashInProgress` flag and deletes the stopped polling
 *   thread object (if one was stopped). This happens automatically when the guard goes out
 *   of scope — including on early returns and exceptions.
 *
 * ## Why Cooperative Cancellation?
 *
 * The previous implementation used `QThread::terminate()` as a fallback when the polling
 * thread didn't exit within a timeout. This is dangerous because:
 * - The thread may hold a `QMutex` → deadlock for all waiting threads
 * - The thread may be mid-USB I/O → device enters an inconsistent state
 * - The thread may be writing shared data → data corruption
 *
 * Instead, this class relies on the fact that `PollingThread::run()` checks its `m_running`
 * atomic flag every 100ms during its sleep loop, so it will always exit promptly when
 * asked to stop cooperatively.
 *
 * ## Thread Safety
 *
 * This class is **not thread-safe** and must be constructed and destroyed on the same
 * thread that owns the firmware operation (typically the FirmwareWriter worker thread).
 * The guard is a stack-local object — its lifetime is tied to the enclosing scope.
 *
 * ## Usage Example
 *
 * @code
 * bool VideoHid::writeEeprom(quint16 address, const QByteArray &data,
 *                            std::function<void(int)> chunkCallback)
 * {
 *     DeviceAccessGuard guard(*this);
 *     if (!guard.isExclusive()) {
 *         qCWarning(log_hid_firmware) << "Failed to acquire exclusive access";
 *         return false;
 *     }
 *
 *     // ... perform firmware write (polling thread is stopped, no contention) ...
 *
 * }  // guard destructor: clears m_flashInProgress, deletes polling thread
 * @endcode
 *
 * @note This class is declared as a friend of `VideoHid` to access its private members
 *       (`m_flashInProgress`, `m_pollingThread`).
 *
 * @see VideoHid::writeEeprom()
 * @see VideoHid::PollingThread
 */
class DeviceAccessGuard {
public:
    /**
     * @brief Construct a DeviceAccessGuard and acquire exclusive device access.
     *
     * Performs the following steps in order:
     * 1. Sets `VideoHid::m_flashInProgress` to `true` (release semantics), which causes
     *    the polling thread's `pollDeviceStatus()` and `usbXdataRead4Byte()` to return
     *    immediately without performing any USB I/O.
     * 2. If the polling thread is running, requests interruption via
     *    `QThread::requestInterruption()`, sets its `m_running` flag to false, and calls
     *    `QThread::quit()` to exit its event loop.
     * 3. Waits up to 5000ms for the polling thread to finish. The polling thread checks
     *    `m_running` every 100ms, so it should exit well within this timeout.
     * 4. If the thread doesn't exit within 5 seconds, logs a warning and marks the guard
     *    as non-exclusive (acquisition failed). The thread is **not** force-terminated —
     *    it is allowed to continue running to avoid mutex/USB corruption.
     *
     * @param hid  Reference to the `VideoHid` singleton that owns the polling thread
     *             and flash-in-progress flag.
     *
     * @post If `isExclusive()` returns true, the polling thread has stopped and
     *       `m_flashInProgress` is set. The caller has exclusive access to the device.
     * @post If `isExclusive()` returns false, acquisition failed and the caller should
     *       abort the firmware operation.
     */
    explicit DeviceAccessGuard(VideoHid& hid);

    /**
     * @brief Destructor — releases exclusive device access.
     *
     * Performs the following steps in order:
     * 1. Clears `VideoHid::m_flashInProgress` (sets to `false` with release semantics),
     *    allowing background register reads to resume.
     * 2. If the constructor successfully stopped the polling thread, deletes the
     *    `QThread` object and sets `VideoHid::m_pollingThread` to `nullptr`.
     *
     * @note The caller is responsible for restarting the polling thread (via
     *       `VideoHid::start()`) after the firmware operation completes, if appropriate.
     *       For MS2130S devices, the polling thread is typically not restarted because
     *       the device needs a power cycle after firmware flash.
     */
    ~DeviceAccessGuard();

    /**
     * @brief Check whether exclusive access was successfully acquired.
     *
     * @return `true` if the constructor successfully stopped the polling thread and set
     *         the flash-in-progress flag; `false` if the polling thread failed to exit
     *         within the 5-second timeout.
     *
     * @note If this returns `false`, the caller must abort the firmware operation and
     *       must not access the HID device.
     */
    bool isExclusive() const { return m_exclusive; }

    // Non-copyable: a guard's ownership is tied to its stack scope.
    DeviceAccessGuard(const DeviceAccessGuard&) = delete;
    DeviceAccessGuard& operator=(const DeviceAccessGuard&) = delete;

private:
    /**
     * @brief Reference to the VideoHid singleton whose device access is being managed.
     *
     * Used to access `m_flashInProgress` (atomic flag) and `m_pollingThread` (QThread).
     * The reference is valid for the lifetime of the guard because VideoHid is a singleton.
     */
    VideoHid& m_hid;

    /**
     * @brief Whether exclusive access was successfully acquired by the constructor.
     *
     * Set to `true` if the polling thread stopped within the 5-second timeout (or was
     * not running). Set to `false` if the polling thread failed to stop in time.
     * Accessed via `isExclusive()`.
     */
    bool m_exclusive = false;

    /**
     * @brief Whether the constructor stopped a running polling thread.
     *
     * If `true`, the destructor will delete the stopped `QThread` object and set
     * `VideoHid::m_pollingThread` to `nullptr`. If `false` (either because there was
     * no polling thread or because it failed to stop), the destructor skips cleanup.
     */
    bool m_hadPollingThread = false;
};

#endif // DEVICEACCESSGUARD_H
