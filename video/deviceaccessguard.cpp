#include "deviceaccessguard.h"
#include "videohid.h"
#include "log/opflogging.h"

Q_DECLARE_LOGGING_CATEGORY(log_hid_firmware)

DeviceAccessGuard::DeviceAccessGuard(VideoHid& hid) : m_hid(hid) {
    qCInfo(log_hid_firmware) << "DeviceAccessGuard: Acquiring exclusive device access";

    // 1. Set flash-in-progress flag to suppress background reads
    m_hid.m_flashInProgress.store(true, std::memory_order_release);
    qCDebug(log_hid_firmware) << "DeviceAccessGuard: m_flashInProgress set to true";

    // 2. Cooperatively stop polling thread
    if (m_hid.m_pollingThread && m_hid.m_pollingThread->isRunning()) {
        m_hadPollingThread = true;
        qCInfo(log_hid_firmware) << "DeviceAccessGuard: Stopping polling thread cooperatively";

        // Request interruption and stop (sets m_running = false)
        m_hid.m_pollingThread->requestInterruption();
        m_hid.m_pollingThread->stop();
        m_hid.m_pollingThread->quit();

        // Wait up to 5 seconds for graceful shutdown
        // PollingThread's run() checks m_running every 100ms, so it should exit quickly
        if (!m_hid.m_pollingThread->wait(5000)) {
            qCWarning(log_hid_firmware)
                << "DeviceAccessGuard: Polling thread didn't exit within 5 seconds!"
                << "This may indicate a USB I/O deadlock. Device may need replug.";
            // Don't terminate - let the thread leak rather than risk corruption
            // Mark as non-exclusive to signal failure
            m_hadPollingThread = false;
            m_exclusive = false;
            return;
        }

        qCInfo(log_hid_firmware) << "DeviceAccessGuard: Polling thread stopped successfully";
    }

    m_exclusive = true;
    qCInfo(log_hid_firmware) << "DeviceAccessGuard: Exclusive access acquired successfully";
}

DeviceAccessGuard::~DeviceAccessGuard() {
    qCInfo(log_hid_firmware) << "DeviceAccessGuard: Releasing exclusive device access";

    // 1. Clear flash-in-progress flag
    m_hid.m_flashInProgress.store(false, std::memory_order_release);
    qCDebug(log_hid_firmware) << "DeviceAccessGuard: m_flashInProgress cleared";

    // 2. Clean up polling thread if we stopped it
    if (m_hadPollingThread && m_hid.m_pollingThread) {
        qCDebug(log_hid_firmware) << "DeviceAccessGuard: Cleaning up stopped polling thread";
        delete m_hid.m_pollingThread;
        m_hid.m_pollingThread = nullptr;
    }

    qCInfo(log_hid_firmware) << "DeviceAccessGuard: Exclusive access released";
}
