#ifndef WINDOWSHIDTRANSPORT_H
#define WINDOWSHIDTRANSPORT_H

#ifdef _WIN32

#include <windows.h>
extern "C" {
#include <hidsdi.h>
}
#include "IHIDTransport.h"
#include <QString>
#include <QRecursiveMutex>
#include <string>

// Forward declaration — only used to call findMatchingHIDDevice()
class VideoHid;

// ─────────────────────────────────────────────────────────────────────────────
// WindowsHIDTransport
//
// Concrete IHIDTransport implementation for Windows.
// Owns the HANDLE and all platform-specific HID logic that previously lived
// inside VideoHid / sendFeatureReportWindows / getFeatureReportWindows.
//
// Thread safety: all methods acquire m_mutex internally.
// ─────────────────────────────────────────────────────────────────────────────
class WindowsHIDTransport : public IHIDTransport
{
public:
    // owner is used ONLY to call findMatchingHIDDevice() for path discovery.
    explicit WindowsHIDTransport(VideoHid* owner);
    ~WindowsHIDTransport() override;

    // ── IHIDTransport ─────────────────────────────────────────────────────────
    bool isOpen() const override;
    bool open() override;           // opens with FILE_FLAG_OVERLAPPED (normal use)
    void close() override;          // CloseHandle

    // sendFeatureReport: generic open-if-needed / send / close-if-opened.
    bool sendFeatureReport(uint8_t* buf, size_t len) override;
    bool getFeatureReport(uint8_t* buf, size_t len) override;

    // sendDirect / getDirect: HidD_SetFeature / HidD_GetFeature with no 
    // open/close management.  Callers must ensure the handle is already open.
    bool sendDirect(uint8_t* buf, size_t len) override;
    bool getDirect(uint8_t* buf, size_t len) override;

    // Returns the discovered device path as a QString.
    QString getHIDDevicePath() override;
    // Returns the discovered device path as wstring (used internally & by reopenSync).
    std::wstring getHIDDevicePathW();

    // Close the current overlapped handle and re-open a synchronous handle
    // suitable for MS2130S SPI flash operations.
    bool reopenSync() override;
    // ─────────────────────────────────────────────────────────────────────────

private:
    // Internal open without re-acquiring m_mutex (must already be held).
    bool openLocked();
    // Internal close without re-acquiring m_mutex.
    void closeLocked();

    // Enumerate HID devices to find the proper device path for a given instance path.
    std::wstring getProperDevicePath(const std::wstring& deviceInstancePath);

    // IOCTL_HID_SET_FEATURE wrapper that bypasses the 5-second timeout baked into
    // HidD_SetFeature.  Handle must be opened with FILE_FLAG_OVERLAPPED.
    static BOOL hidSendFeatureNoTimeout(HANDLE hDevice, void* buf, DWORD bufLen,
                                        DWORD timeoutMs = INFINITE);

    HANDLE        m_handle{INVALID_HANDLE_VALUE};
    std::wstring  m_cachedPath;
    VideoHid*     m_owner{nullptr};
    mutable QRecursiveMutex m_mutex;
};

#endif // _WIN32
#endif // WINDOWSHIDTRANSPORT_H
