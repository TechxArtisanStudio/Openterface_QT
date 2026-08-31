#ifndef IHIDTRANSPORT_H
#define IHIDTRANSPORT_H

#include <cstddef>
#include <cstdint>
#include <QString>

// Pure abstract interface for HID feature-report I/O.
// Chip implementations depend only on this interface, not on VideoHid directly.
//
// sendFeatureReport / getFeatureReport — may include retry logic (public API used by
//   high-level callers such as readRegisterSafe / writeRegisterSafe).
// sendDirect / getDirect — single-shot, no retry; used by chip protocol engines
//   (MS2109S, MS2130S read strategies) where re-entrance must be controlled manually.
//
// getHIDDevicePath — returns the current OS device path string.
// reopenSync      — Windows: closes the overlapped handle and re-opens a synchronous
//                   handle suitable for SPI flash operations.  Default is a no-op.
class IHIDTransport {
public:
    virtual ~IHIDTransport() = default;

    // Device lifecycle
    virtual bool isOpen() const = 0;
    virtual bool open() = 0;
    virtual void close() = 0;

    // High-level report I/O (with retry where appropriate)
    virtual bool sendFeatureReport(uint8_t* buf, size_t len) = 0;
    virtual bool getFeatureReport(uint8_t* buf, size_t len) = 0;

    // Low-level direct report I/O (single attempt, no retry wrappers)
    virtual bool sendDirect(uint8_t* buf, size_t len) = 0;
    virtual bool getDirect(uint8_t* buf, size_t len) = 0;

    // Device path retrieval
    virtual QString getHIDDevicePath() { return {}; }

    // Re-open as synchronous handle for flash operations.
    // Default no-op (returns true). Override in WindowsHIDTransport.
    virtual bool reopenSync() { return true; }
};

#endif // IHIDTRANSPORT_H
