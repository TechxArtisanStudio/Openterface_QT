#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <stdexcept>

// ---------------------------------------------------------------------------
// WCHHexParser
//
// Parses Intel HEX (.hex) or raw binary (.bin) firmware files into a
// contiguous byte array ready for flashing.
//
// Intel HEX record types handled:
//   0x00 — data
//   0x01 — end-of-file
//   0x02 — extended segment address
//   0x04 — extended linear address
//
// Gaps between data records are padded with 0xFF.
// ---------------------------------------------------------------------------
class WCHHexParser {
public:
    // Parse file at filePath; auto-detects .hex vs .bin by content.
    // Returns contiguous firmware bytes.
    // Throws WCHHexParseError on malformed input.
    static std::vector<uint8_t> parseFile(const std::string& filePath);

    // Parse from in-memory data
    static std::vector<uint8_t> parseData(const std::vector<uint8_t>& data);

private:
    static std::vector<uint8_t> parseIntelHex(const std::vector<uint8_t>& data);
    static std::vector<uint8_t> parseRawBinary(const std::vector<uint8_t>& data);

    static bool isIntelHex(const std::vector<uint8_t>& data);
};

struct WCHHexParseError : std::runtime_error {
    explicit WCHHexParseError(const std::string& msg) : std::runtime_error(msg) {}
};
