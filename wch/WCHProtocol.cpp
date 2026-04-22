#include "WCHProtocol.h"

#include <cstring>
#include <stdexcept>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
void WCHPacketBuilder::appendU32LE(std::vector<uint8_t>& buf, uint32_t v)
{
    buf.push_back(static_cast<uint8_t>(v & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

void WCHPacketBuilder::appendU16LE(std::vector<uint8_t>& buf, uint16_t v)
{
    buf.push_back(static_cast<uint8_t>(v & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}

// Header: [cmd][len_lo][len_hi][payload...]
static std::vector<uint8_t> makePacket(uint8_t cmd, const std::vector<uint8_t>& payload)
{
    std::vector<uint8_t> buf;
    buf.reserve(3 + payload.size());
    buf.push_back(cmd);
    uint16_t len = static_cast<uint16_t>(payload.size());
    buf.push_back(static_cast<uint8_t>(len & 0xFF));
    buf.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
    buf.insert(buf.end(), payload.begin(), payload.end());
    return buf;
}

// ---------------------------------------------------------------------------
// identify  [cmd=0xA1][len=0x12][0x00][deviceID][deviceType]["MCU ISP & WCH.CN"]
// ---------------------------------------------------------------------------
std::vector<uint8_t> WCHPacketBuilder::identify(uint8_t deviceID, uint8_t deviceType)
{
    std::vector<uint8_t> payload;
    payload.push_back(deviceID);
    payload.push_back(deviceType);
    const char* magic = "MCU ISP & WCH.CN";
    for (int i = 0; magic[i] != '\0'; ++i)
        payload.push_back(static_cast<uint8_t>(magic[i]));
    return makePacket(WCHCommands::Identify, payload);
}

// ---------------------------------------------------------------------------
// ispEnd  [cmd=0xA2][len=1][0x00][reason]
// ---------------------------------------------------------------------------
std::vector<uint8_t> WCHPacketBuilder::ispEnd(uint8_t reason)
{
    return makePacket(WCHCommands::IspEnd, {reason});
}

// ---------------------------------------------------------------------------
// ispKey  [cmd=0xA3][len=key.size()][key bytes]
// ---------------------------------------------------------------------------
std::vector<uint8_t> WCHPacketBuilder::ispKey(const std::vector<uint8_t>& key)
{
    return makePacket(WCHCommands::IspKey, key);
}

// ---------------------------------------------------------------------------
// erase  [cmd=0xA4][len=4][sectors as uint32 LE]
// ---------------------------------------------------------------------------
std::vector<uint8_t> WCHPacketBuilder::erase(uint32_t sectors)
{
    std::vector<uint8_t> payload;
    appendU32LE(payload, sectors);
    return makePacket(WCHCommands::Erase, payload);
}

// ---------------------------------------------------------------------------
// program / verify  [cmd][len][addr:u32LE][padding:u8][data...]
// ---------------------------------------------------------------------------
static std::vector<uint8_t> makeProgramVerify(uint8_t cmd,
                                               uint32_t address,
                                               uint8_t padding,
                                               const std::vector<uint8_t>& data)
{
    std::vector<uint8_t> payload;
    // address (4 bytes LE)
    payload.push_back(static_cast<uint8_t>(address & 0xFF));
    payload.push_back(static_cast<uint8_t>((address >> 8) & 0xFF));
    payload.push_back(static_cast<uint8_t>((address >> 16) & 0xFF));
    payload.push_back(static_cast<uint8_t>((address >> 24) & 0xFF));
    payload.push_back(padding);
    payload.insert(payload.end(), data.begin(), data.end());
    return makePacket(cmd, payload);
}

std::vector<uint8_t> WCHPacketBuilder::program(uint32_t address, uint8_t padding,
                                                 const std::vector<uint8_t>& data)
{
    return makeProgramVerify(WCHCommands::Program, address, padding, data);
}

std::vector<uint8_t> WCHPacketBuilder::verify(uint32_t address, uint8_t padding,
                                                const std::vector<uint8_t>& data)
{
    return makeProgramVerify(WCHCommands::Verify, address, padding, data);
}

// ---------------------------------------------------------------------------
// readConfig  [cmd=0xA7][len=2][bitMask][0x00]
// ---------------------------------------------------------------------------
std::vector<uint8_t> WCHPacketBuilder::readConfig(uint8_t bitMask)
{
    return makePacket(WCHCommands::ReadConfig, {bitMask, 0x00});
}

// ---------------------------------------------------------------------------
// writeConfig  [cmd=0xA8][len=2+data.size()][bitMask][0x00][data...]
// ---------------------------------------------------------------------------
std::vector<uint8_t> WCHPacketBuilder::writeConfig(uint8_t bitMask,
                                                    const std::vector<uint8_t>& data)
{
    std::vector<uint8_t> payload = {bitMask, 0x00};
    payload.insert(payload.end(), data.begin(), data.end());
    return makePacket(WCHCommands::WriteConfig, payload);
}

// ---------------------------------------------------------------------------
// dataRead  [cmd=0xAB][len=6][addr:u32LE][length:u16LE]
// ---------------------------------------------------------------------------
std::vector<uint8_t> WCHPacketBuilder::dataRead(uint32_t address, uint16_t length)
{
    std::vector<uint8_t> payload;
    appendU32LE(payload, address);
    appendU16LE(payload, length);
    return makePacket(WCHCommands::DataRead, payload);
}

// ---------------------------------------------------------------------------
// WCHResponse::parse
// Response: [cmd:u8][status:u8][len_lo:u8][len_hi:u8][payload...]
// ---------------------------------------------------------------------------
bool WCHResponse::parse(const std::vector<uint8_t>& raw, WCHResponse& out)
{
    if (raw.size() < 4)
        return false;

    out.status = raw[1];
    uint16_t expectedLen = static_cast<uint16_t>(raw[2]) |
                           (static_cast<uint16_t>(raw[3]) << 8);
    if (raw.size() < static_cast<size_t>(4 + expectedLen))
        return false;

    out.payload.assign(raw.begin() + 4, raw.begin() + 4 + expectedLen);
    out.ok = (out.status == 0x00);
    return true;
}
