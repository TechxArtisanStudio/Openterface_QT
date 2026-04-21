#pragma once

#include <cstdint>
#include <vector>
#include <stdexcept>
#include <string>

// ---------------------------------------------------------------------------
// WCH ISP Protocol command opcodes
// ---------------------------------------------------------------------------
namespace WCHCommands {
    constexpr uint8_t Identify    = 0xA1;
    constexpr uint8_t IspEnd      = 0xA2;
    constexpr uint8_t IspKey      = 0xA3;
    constexpr uint8_t Erase       = 0xA4;
    constexpr uint8_t Program     = 0xA5;
    constexpr uint8_t Verify      = 0xA6;
    constexpr uint8_t ReadConfig  = 0xA7;
    constexpr uint8_t WriteConfig = 0xA8;
    constexpr uint8_t DataErase   = 0xA9;
    constexpr uint8_t DataProgram = 0xAA;
    constexpr uint8_t DataRead    = 0xAB;
    constexpr uint8_t WriteOTP    = 0xC3;
    constexpr uint8_t ReadOTP     = 0xC4;
    constexpr uint8_t SetBaud     = 0xC5;
}

// ---------------------------------------------------------------------------
// Configuration register bit masks
// ---------------------------------------------------------------------------
namespace WCHConstants {
    constexpr int     MaxPacketSize          = 64;
    constexpr int     SectorSize             = 1024;  // 1 KB
    constexpr int     ProgramChunkSize       = 56;    // bytes per program/verify chunk
    constexpr uint8_t CfgMaskRDPRUserDataWPR = 0x07;
    constexpr uint8_t CfgMaskBTVER          = 0x08;
    constexpr uint8_t CfgMaskUID            = 0x10;
    constexpr uint8_t CfgMaskAll            = 0x1F;
    constexpr int     UsbTimeoutMs          = 5000;
}

// ---------------------------------------------------------------------------
// Build a WCH ISP packet from raw fields.
// Packet layout: [cmd:u8][len_lo:u8][len_hi:u8][payload...]
// ---------------------------------------------------------------------------
class WCHPacketBuilder {
public:
    // Identify: payload = [deviceID, deviceType, "MCU ISP & WCH.CN"]
    static std::vector<uint8_t> identify(uint8_t deviceID = 0, uint8_t deviceType = 0);

    // Exit bootloader
    static std::vector<uint8_t> ispEnd(uint8_t reason = 1);

    // Send ISP key seed (30 zero bytes)
    static std::vector<uint8_t> ispKey(const std::vector<uint8_t>& key);

    // Erase N sectors
    static std::vector<uint8_t> erase(uint32_t sectors);

    // Write encrypted chunk to code flash
    static std::vector<uint8_t> program(uint32_t address, uint8_t padding,
                                         const std::vector<uint8_t>& data);

    // Verify encrypted chunk in code flash
    static std::vector<uint8_t> verify(uint32_t address, uint8_t padding,
                                        const std::vector<uint8_t>& data);

    // Read config registers
    static std::vector<uint8_t> readConfig(uint8_t bitMask);

    // Write config registers
    static std::vector<uint8_t> writeConfig(uint8_t bitMask,
                                             const std::vector<uint8_t>& data);

    // Read data EEPROM / dump
    static std::vector<uint8_t> dataRead(uint32_t address, uint16_t length);

private:
    static void appendU32LE(std::vector<uint8_t>& buf, uint32_t v);
    static void appendU16LE(std::vector<uint8_t>& buf, uint16_t v);
};

// ---------------------------------------------------------------------------
// WCH ISP response
// Response layout: [cmd:u8][status:u8][len_lo:u8][len_hi:u8][payload...]
// ---------------------------------------------------------------------------
struct WCHResponse {
    bool      ok;        // true when status == 0x00 and payload length matches
    uint8_t   status;    // raw status byte
    std::vector<uint8_t> payload;

    // Parse raw bytes received from device; returns false if malformed
    static bool parse(const std::vector<uint8_t>& raw, WCHResponse& out);
};

// ---------------------------------------------------------------------------
// Exceptions
// ---------------------------------------------------------------------------
struct WCHProtocolError : std::runtime_error {
    explicit WCHProtocolError(const std::string& msg) : std::runtime_error(msg) {}
};
