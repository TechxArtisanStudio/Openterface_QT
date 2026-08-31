#ifndef LINUXHIDTRANSPORT_H
#define LINUXHIDTRANSPORT_H

#ifdef __linux__

#include "IHIDTransport.h"
#include <QString>

class VideoHid;

// ─────────────────────────────────────────────────────────────────────────────
// LinuxHIDTransport
//
// Concrete IHIDTransport implementation for Linux hidraw.
// Owns the file descriptor and all platform-specific HID logic that previously
// lived inside VideoHid.
// ─────────────────────────────────────────────────────────────────────────────
class LinuxHIDTransport : public IHIDTransport
{
public:
    explicit LinuxHIDTransport(VideoHid* owner);
    ~LinuxHIDTransport() override;

    // ── IHIDTransport ─────────────────────────────────────────────────────────
    bool isOpen() const override;
    bool open() override;
    void close() override;

    bool sendFeatureReport(uint8_t* buf, size_t len) override;
    bool getFeatureReport(uint8_t* buf, size_t len) override;

    // On Linux sendDirect/getDirect are identical to sendFeatureReport/getFeatureReport.
    bool sendDirect(uint8_t* buf, size_t len) override;
    bool getDirect(uint8_t* buf, size_t len) override;

    // Returns the discovered hidraw device path.
    QString getHIDDevicePath() override;
    // ─────────────────────────────────────────────────────────────────────────

private:
    int       m_hidFd{-1};
    VideoHid* m_owner{nullptr};
};

#endif // __linux__
#endif // LINUXHIDTRANSPORT_H
