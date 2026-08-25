#pragma once

#include "WCHDevice.h"
#include "WCHUSBTransport.h"

#include <cstdint>
#include <vector>
#include <string>
#include <functional>
#include <stdexcept>

// ---------------------------------------------------------------------------
// WCHFlasher
//
// High-level WCH ISP flash engine.  Ported from wchisp-mac WCHFlashing.swift.
//
// Typical usage:
//   WCHUSBTransport transport;
//   transport.open(0);
//   WCHFlasher flasher(&transport);       // identify + readConfig
//   flasher.flash(firmwareBytes,          // erase + program + verify
//                 progressCallback);
//   transport.close();
// ---------------------------------------------------------------------------

// Progress callback: (percent 0–100, status message)
using WCHProgressCallback = std::function<void(int, const std::string&)>;

class WCHFlasher {
public:
    explicit WCHFlasher(WCHUSBTransport* transport);

    // --- Information ---

    // Raw chipID and deviceType reported by the device (for diagnostics)
    uint8_t rawChipID() const { return m_rawChipID; }
    uint8_t rawDeviceType() const { return m_rawDeviceType; }

    // Human-readable chip info string (includes raw IDs for diagnostics)
    std::string getChipInfo() const;

    // Raw chip struct
    const WCHChip& chip() const { return m_chip; }

    // Bootloader version string (e.g. "2402")
    std::string bootloaderVersion() const;

    // UID as hex string (e.g. "AA-BB-CC-DD-EE-FF-00-11")
    std::string chipUID() const;

    bool isCodeFlashProtected() const;

    // --- Operations ---

    // Full flash + verify + reset pipeline.
    // Calls progressCallback(percent, message) during operation.
    // Throws WCHFlashError on failure.
    void flash(const std::vector<uint8_t>& firmware,
               const WCHProgressCallback& progress = nullptr);

    // Individual stages (available for fine-grained control)
    void unprotect();
    void erase(uint32_t firmwareSize);  // erase code flash to fit firmwareSize bytes
    void eraseData(uint16_t sectors);  // erase N 1KiB sectors of data flash (EEPROM)
    void program(const std::vector<uint8_t>& firmware,
                 const WCHProgressCallback& progress = nullptr);
    void verify(const std::vector<uint8_t>& firmware,
                const WCHProgressCallback& progress = nullptr);
    void protect();
    void reset();

private:
    WCHUSBTransport* m_transport;
    WCHChip          m_chip;
    uint8_t          m_rawChipID = 0;         // raw chipID from device
    uint8_t          m_rawDeviceType = 0;     // raw deviceType from device
    std::vector<uint8_t> m_uid;            // raw UID bytes
    std::vector<uint8_t> m_btver;          // bootloader version bytes [4]
    bool             m_codeFlashProtected = false;
    std::vector<uint8_t> m_xorKey;        // 8-byte XOR encryption key

    void identify();
    void readConfig();
    void deriveXorKey();
    void reidentify();   // re-identify after device reset (unprotect)

    // Send command and parse response; throws on error
    std::vector<uint8_t> doTransfer(const std::vector<uint8_t>& packet,
                                     const std::string& opName);

    // Encrypt/decrypt a chunk with XOR key (rotating offset)
    std::vector<uint8_t> xorChunk(const std::vector<uint8_t>& data,
                                   size_t startOffset) const;

    static void reportProgress(const WCHProgressCallback& cb, int pct,
                                const std::string& msg);
};

struct WCHFlashError : std::runtime_error {
    explicit WCHFlashError(const std::string& msg) : std::runtime_error(msg) {}
};
