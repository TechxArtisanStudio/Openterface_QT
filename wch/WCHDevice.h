#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <stdexcept>

// ---------------------------------------------------------------------------
// WCHChip — describes one supported WCH microcontroller
// ---------------------------------------------------------------------------
struct WCHChip {
    std::string name;
    uint8_t     chipID;
    uint8_t     deviceType;
    uint32_t    flashSize;    // code flash in bytes
    uint32_t    eepromSize;   // data EEPROM in bytes (0 if absent)
    int         uidSize;      // 4 or 8 bytes
    bool        supportsCodeFlashProtect;

    // Alternate chip-ID for some families (0 = none)
    uint8_t     altChipID;
};

// ---------------------------------------------------------------------------
// WCHChipDB — hardcoded database of known WCH ISP chips
// ---------------------------------------------------------------------------
class WCHChipDB {
public:
    // Returns the static list of all known chips
    static const std::vector<WCHChip>& chips();

    // Find a chip by chipID + deviceType; throws std::runtime_error if not found
    static WCHChip findChip(uint8_t chipID, uint8_t deviceType);
};

struct WCHDeviceError : std::runtime_error {
    explicit WCHDeviceError(const std::string& msg) : std::runtime_error(msg) {}
};
