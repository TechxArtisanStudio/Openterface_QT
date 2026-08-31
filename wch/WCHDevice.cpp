#include "WCHDevice.h"

#include <sstream>
#include <iomanip>


// Hardcoded chip database — deviceType and chipID values from ch32-rs/wchisp
// (https://github.com/ch32-rs/wchisp) devices/*.yaml
// Rule: deviceType = mcuType + 0x10
// Each entry: { name, chipID, deviceType, flashSize(bytes),
//               eepromSize(bytes), uidSize, supportsCodeFlashProtect, altChipID }
// ---------------------------------------------------------------------------
static const std::vector<WCHChip> s_chips = {
    // CH55x series (mcuType=1, deviceType=0x11)
    { "CH551",       0x51, 0x11, 10*1024,  128,    4, false, 0x00 },
    { "CH552",       0x52, 0x11, 14*1024,  128,    4, false, 0x00 },
    { "CH554",       0x54, 0x11, 14*1024,  128,    4, false, 0x00 },

    // CH57x series (mcuType=3, deviceType=0x13)
    { "CH571",       0x71, 0x13, 192*1024, 32*1024, 8, false, 0x00 },
    { "CH573",       0x73, 0x13, 448*1024, 32*1024, 8, false, 0x00 },

    // CH32F103 series (mcuType=4, deviceType=0x14)
    { "CH32F103C6",  0x32, 0x14, 32*1024,    0, 8, false, 0x00 },
    { "CH32F103C8",  0x33, 0x14, 64*1024,    0, 8, false, 0x00 },

    // CH32V103 series (mcuType=5, deviceType=0x15)
    { "CH32V103C6",  0x32, 0x15, 32*1024,    0, 8, false, 0x00 },
    { "CH32V103C8",  0x33, 0x15, 64*1024,    0, 8, false, 0x00 },

    // CH58x series (mcuType=6, deviceType=0x16)
    { "CH581",       0x81, 0x16, 192*1024, 32*1024, 8, false, 0x00 },
    { "CH582",       0x82, 0x16, 448*1024, 32*1024, 8, false, 0x00 },
    { "CH583",       0x83, 0x16, 448*1024, 32*1024, 8, false, 0x00 },

    // CH32V30x series (mcuType=7, deviceType=0x17)
    { "CH32V305RB",  0x50, 0x17, 128*1024,  2*1024, 8, true, 0x00 },
    { "CH32V307VC",  0x70, 0x17, 256*1024,  2*1024, 8, true, 0x00 },
    { "CH32V307WC",  0x73, 0x17, 256*1024,  2*1024, 8, true, 0x00 },

    // CH32F20x series (mcuType=8, deviceType=0x18)
    { "CH32F205RB",  0x50, 0x18, 128*1024,   0, 8, true, 0x00 },
    { "CH32F208WB",  0x80, 0x18, 128*1024,   0, 8, true, 0x00 },
    { "CH32F208RB",  0x81, 0x18, 128*1024,   0, 8, true, 0x00 },

    // CH32V20x series (mcuType=9, deviceType=0x19)
    // chipID 0x3X = V4C core (CH32V203), 0x8X = V4B core (CH32V208)
    { "CH32V203C6",  0x33, 0x19, 32*1024,  10*1024, 8, true, 0x00 },
    { "CH32V203C8",  0x30, 0x19, 64*1024,  10*1024, 8, true, 0x00 },
    { "CH32V203K8",  0x32, 0x19, 64*1024,  10*1024, 8, true, 0x00 },
    { "CH32V203RB",  0x34, 0x19, 128*1024, 10*1024, 8, true, 0x00 },
    { "CH32V208WB",  0x80, 0x19, 128*1024, 32*1024, 8, true, 0x00 },
    { "CH32V208RB",  0x81, 0x19, 128*1024, 32*1024, 8, true, 0x00 },
    { "CH32V208CB",  0x82, 0x19, 128*1024, 32*1024, 8, true, 0x00 },
    { "CH32V208GB",  0x83, 0x19, 128*1024, 32*1024, 8, true, 0x00 },

    // CH56x series (mcuType=0, deviceType=0x10)
    { "CH561",       0x61, 0x10,  64*1024, 28*1024, 8, false, 0x00 },
    { "CH563",       0x63, 0x10, 224*1024, 28*1024, 8, false, 0x00 },
    { "CH565",       0x65, 0x10, 448*1024, 32*1024, 8, false, 0x00 },

    // CH59x series (mcuType=0x12, deviceType=0x22)
    { "CH591",       0x91, 0x22, 192*1024, 32*1024, 8, false, 0x00 },
    { "CH592",       0x92, 0x22, 448*1024, 32*1024, 8, false, 0x00 },

    // CH32F20x Compact / D6 series (mcuType=0x10, deviceType=0x20)
    { "CH32F203C8(D6)", 0x30, 0x20, 64*1024, 0, 8, true, 0x00 },

    // CH32V00x series (mcuType=0x11, deviceType=0x21)
    { "CH32V002F4",  0x20, 0x21, 16*1024, 0, 8, false, 0x00 },
    { "CH32V003A4",  0x32, 0x21, 16*1024, 0, 8, false, 0x00 },

    // CH32X03x series (mcuType=0x13, deviceType=0x23)
    { "CH32X035C8",  81,   0x23, 62*1024,  2*1024, 8, true, 0x00 },
    { "CH32X035R8",  80,   0x23, 62*1024,  2*1024, 8, true, 0x00 },

    // CH643 series (mcuType=0x14, deviceType=0x24)
    { "CH643",       48,   0x24, 62*1024,  0,      8, true, 0x00 },

    // CH32L103 series (mcuType=0x15, deviceType=0x25)
    { "CH32L103C8",  48,   0x25, 64*1024, 2*1024, 8, true, 0x00 },
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
            std::ostringstream oss;
            oss << "Unknown-0x"
                << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
                << static_cast<unsigned>(chipID)
                << "/DT-0x"
                << std::setw(2) << static_cast<unsigned>(deviceType);
            WCHChip generic = chip;
            generic.name = oss.str();
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
