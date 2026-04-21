#include "WCHDevice.h"

#include <sstream>
#include <iomanip>


// Hardcoded chip database ported from wchisp-mac Device.swift
// Each entry: { name, chipID, deviceType, flashSize(bytes),
//               eepromSize(bytes), uidSize, supportsCodeFlashProtect, altChipID }
// ---------------------------------------------------------------------------
static const std::vector<WCHChip> s_chips = {
    // CH32V103 series (deviceType=0x11)
    { "CH32V103x6", 0x3F, 0x11, 32*1024,     0, 8, false, 0x00 },
    { "CH32V103x8", 0x21, 0x11, 64*1024,     0, 8, false, 0x00 },

    // CH32V20x series (deviceType=0x14)
    { "CH32V203x6", 0x3A, 0x14, 32*1024,  10*1024, 8, true, 0x00 },
    { "CH32V203x8", 0x20, 0x14, 64*1024,  10*1024, 8, true, 0x00 },
    { "CH32V203C8",  0x20, 0x14, 64*1024,  10*1024, 8, true, 0x00 },
    { "CH32V203RB",  0x23, 0x14, 128*1024, 10*1024, 8, true, 0x00 },

    // CH32V208 series (deviceType=0x14) — BLE+USB target
    { "CH32V208WB",  0x22, 0x14, 480*1024, 32*1024, 8, true, 0x00 },
    { "CH32V208RB",  0x24, 0x14, 480*1024, 32*1024, 8, true, 0x00 },
    { "CH32V208CB",  0x25, 0x14, 480*1024, 32*1024, 8, true, 0x00 },
    { "CH32V208GB",  0x26, 0x14, 480*1024, 32*1024, 8, true, 0x00 },

    // CH32V30x series (deviceType=0x15)
    { "CH32V305FB",  0x25, 0x15, 256*1024,  2*1024, 8, true, 0x00 },
    { "CH32V305RB",  0x26, 0x15, 128*1024,  2*1024, 8, true, 0x00 },
    { "CH32V307WC",  0x35, 0x15, 480*1024,  2*1024, 8, true, 0x00 },
    { "CH32V307VC",  0x30, 0x15, 256*1024,  2*1024, 8, true, 0x00 },

    // CH57x series (deviceType=0x13)
    { "CH571",       0x71, 0x13, 192*1024, 32*1024, 8, false, 0x00 },
    { "CH573",       0x73, 0x13, 448*1024, 32*1024, 8, false, 0x00 },

    // CH58x series (deviceType=0x13)
    { "CH581",       0x81, 0x13, 192*1024, 32*1024, 8, false, 0x00 },
    { "CH582",       0x82, 0x13, 448*1024, 32*1024, 8, false, 0x00 },
    { "CH583",       0x83, 0x13, 448*1024, 32*1024, 8, false, 0x00 },

    // CH59x series (deviceType=0x13)
    { "CH591",       0x91, 0x13, 192*1024, 32*1024, 8, false, 0x00 },
    { "CH592",       0x92, 0x13, 448*1024, 32*1024, 8, false, 0x00 },

    // CH32X035 series (deviceType=0x14)
    { "CH32X035C8",  0x20, 0x14, 62*1024,  2*1024, 8, true, 0x00 },

    // CH55x series (deviceType=0x11)
    { "CH551",       0x51, 0x11, 14*1024,  256,    4, false, 0x00 },
    { "CH552",       0x52, 0x11, 14*1024,  128,    4, false, 0x00 },
    { "CH554",       0x54, 0x11, 14*1024,  128,    4, false, 0x00 },

    // CH32F103 series (deviceType=0x12)
    { "CH32F103x8",  0x21, 0x12, 64*1024,    0, 8, false, 0x00 },

    // CH32F20x series (deviceType=0x15)
    { "CH32F205RB",  0x26, 0x15, 128*1024,   0, 8, true, 0x00 },

    // CH32L series (deviceType=0x14)
    { "CH32L103x8",  0x20, 0x14, 64*1024, 2*1024, 8, true, 0x00 },
};

const std::vector<WCHChip>& WCHChipDB::chips()
{
    return s_chips;
}

WCHChip WCHChipDB::findChip(uint8_t chipID, uint8_t deviceType)
{
    // First pass: exact match
    for (const auto& chip : s_chips) {
        if (chip.chipID == chipID && chip.deviceType == deviceType)
            return chip;
    }
    // Second pass: alternate chip-ID match
    for (const auto& chip : s_chips) {
        if (chip.altChipID != 0 && chip.altChipID == chipID &&
            chip.deviceType == deviceType)
            return chip;
    }
    // Third pass: chipID match regardless of deviceType
    // (handles ROM revisions or undocumented deviceType variants)
    for (const auto& chip : s_chips) {
        if (chip.chipID == chipID) {
            WCHChip generic = chip;
            generic.chipID     = chipID;
            generic.deviceType = deviceType;
            return generic;
        }
    }
    // Fourth pass: deviceType family match
    for (const auto& chip : s_chips) {
        if (chip.deviceType == deviceType) {
            WCHChip generic = chip;
            generic.name = "Unknown-0x" + std::to_string(chipID) +
                           "/DT-0x" + std::to_string(deviceType);
            generic.chipID = chipID;
            return generic;
        }
    }
    // Format error message with proper hex representation
    std::ostringstream oss;
    oss << "Unknown chip: chipID=0x"
        << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
        << static_cast<unsigned>(chipID)
        << " deviceType=0x"
        << std::setw(2) << static_cast<unsigned>(deviceType);
    throw WCHDeviceError(oss.str());
}
