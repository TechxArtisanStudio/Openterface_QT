/**
 * @file deviceaccessguard.cpp
 * @brief Implementation of DeviceAccessGuard — RAII exclusive device access for firmware ops.
 *
 * This file implements the constructor and destructor of DeviceAccessGuard, which together
 * manage the lifecycle of exclusive HID device access during firmware write operations.
 *
 * @see DeviceAccessGuard (declaration and full documentation in deviceaccessguard.h)
 */

#include "deviceaccessguard.h"
#include "videohid.h"
#include "log/opflogging.h"

Q_DECLARE_LOGGING_CATEGORY(log_hid_firmware)

/**
 * @brief Acquire exclusive device access by stopping the polling thread and setting
 *        the flash-in-progress flag.
 *
 * Implementation steps:
 * 1. Set `m_flashInProgress = true` (release semantics) — this causes all background
 *    register reads to bail out immediately, reducing USB bus contention.
 * 2. If the polling thread is running, request cooperative stop:
 *    a. `requestInterruption()` — sets QThread's internal interruption flag.
 *    b. `stop()` — sets PollingThread's `m_running` atomic to false.
 *    c. `quit()` — exits the thread's event loop (if any).
 * 3. `wait(5000)` — block until the thread finishes or 5 seconds elapse.
 *    The polling thread checks `m_running` every 100ms in its sleep loop, so
 *    it typically exits within 100–200ms.
 * 4. If the wait times out, log a warning and mark acquisition as failed.
 *    We deliberately do NOT call `terminate()` because:
 *    - The thread may hold a QMutex → deadlock
 *    - The thread may be mid-USB I/O → device corruption
 *    - A leaked thread is safer than a corrupted device
 */
DeviceAccessGuard::DeviceAccessGuard(VideoHid& hid) : m_hid(hid) {
    qCInfo(log_hid_firmware) << "DeviceAccessGuard: Acquiring exclusive device access";

    // Step 1: Set flash-in-progress flag.
    // This uses release semantics so that all threads reading the flag with
    // acquire semantics will see the update promptly.
    m_hid.m_flashInProgress.store(true, std::memory_order_release);
    qCDebug(log_hid_firmware) << "DeviceAccessGuard: m_flashInProgress set to true";

    // Step 2: Cooperatively stop the polling thread (if running).
    if (m_hid.m_pollingThread && m_hid.m_pollingThread->isRunning()) {
        m_hadPollingThread = true;
        qCInfo(log_hid_firmware) << "DeviceAccessGuard: Stopping polling thread cooperatively";

        // Request interruption via all three mechanisms for maximum compatibility:
        // - requestInterruption() sets QThread's internal flag (checked by isInterruptionRequested())
        // - stop() sets our custom m_running atomic (checked by PollingThread::run())
        // - quit() exits any running event loop on the thread
        m_hid.m_pollingThread->requestInterruption();
        m_hid.m_pollingThread->stop();
        m_hid.m_pollingThread->quit();

        // Step 3: Wait up to 5 seconds for graceful shutdown.
        // PollingThread::run() checks m_running every 100ms, so normal exit takes < 200ms.
        // The 5-second timeout is generous to account for a pollDeviceStatus() call that
        // may be blocked on a slow USB read (typical HID timeout is 1–2 seconds).
        if (!m_hid.m_pollingThread->wait(5000)) {
            qCWarning(log_hid_firmware)
                << "DeviceAccessGuard: Polling thread didn't exit within 5 seconds!"
                << "This may indicate a USB I/O deadlock. Device may need replug.";
            // Don't terminate — let the thread leak rather than risk mutex/USB corruption.
            // Mark as non-exclusive so the caller aborts the firmware operation.
            m_hadPollingThread = false;
            m_exclusive = false;
            return;
        }

        qCInfo(log_hid_firmware) << "DeviceAccessGuard: Polling thread stopped successfully";
    }

    // Step 4: Mark acquisition as successful.
    m_exclusive = true;
    qCInfo(log_hid_firmware) << "DeviceAccessGuard: Exclusive access acquired successfully";
}

/**
 * @brief Release exclusive device access: clear flash-in-progress flag and clean up
 *        the polling thread.
 *
 * Implementation steps:
 * 1. Clear `m_flashInProgress = false` (release semantics) — this allows background
 *    register reads to resume once the caller restarts the polling thread.
 * 2. If the constructor stopped a running polling thread (m_hadPollingThread == true),
 *    delete the QThread object and null out the pointer. The thread has already
 *    finished (we waited for it in the constructor), so this is safe.
 *
 * @note The caller is responsible for restarting the polling thread via
 *       VideoHid::start() if appropriate. For MS2130S firmware flash, the polling
 *       thread is typically NOT restarted because the device needs a power cycle.
 */
DeviceAccessGuard::~DeviceAccessGuard() {
    qCInfo(log_hid_firmware) << "DeviceAccessGuard: Releasing exclusive device access";

    // Step 1: Clear flash-in-progress flag to allow background reads to resume.
    m_hid.m_flashInProgress.store(false, std::memory_order_release);
    qCDebug(log_hid_firmware) << "DeviceAccessGuard: m_flashInProgress cleared";

    // Step 2: Clean up the stopped polling thread (if we stopped one).
    // The thread has already finished (wait() returned true in the constructor),
    // so delete is safe and will not block.
    if (m_hadPollingThread && m_hid.m_pollingThread) {
        qCDebug(log_hid_firmware) << "DeviceAccessGuard: Cleaning up stopped polling thread";
        delete m_hid.m_pollingThread;
        m_hid.m_pollingThread = nullptr;
    }

    qCInfo(log_hid_firmware) << "DeviceAccessGuard: Exclusive access released";
}
