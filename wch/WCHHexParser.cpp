#include "WCHHexParser.h"

#include <fstream>
#include <sstream>
#include <map>
#include <algorithm>
#include <stdexcept>
#include <cctype>

// ---------------------------------------------------------------------------
// Detect Intel HEX by checking first byte == ':' (0x3A)
// ---------------------------------------------------------------------------
bool WCHHexParser::isIntelHex(const std::vector<uint8_t>& data)
{
    if (data.empty())
        return false;
    return data[0] == 0x3A; // ':'
}

// ---------------------------------------------------------------------------
// parseFile
// ---------------------------------------------------------------------------
std::vector<uint8_t> WCHHexParser::parseFile(const std::string& filePath)
{
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open())
        throw WCHHexParseError("Cannot open file: " + filePath);

    std::vector<uint8_t> raw((std::istreambuf_iterator<char>(file)),
                              std::istreambuf_iterator<char>());
    file.close();

    return parseData(raw);
}

// ---------------------------------------------------------------------------
// parseData — dispatches to Intel HEX or binary parser
// ---------------------------------------------------------------------------
std::vector<uint8_t> WCHHexParser::parseData(const std::vector<uint8_t>& data)
{
    if (data.empty())
        throw WCHHexParseError("Firmware data is empty");

    if (isIntelHex(data))
        return parseIntelHex(data);
    else
        return parseRawBinary(data);
}

// ---------------------------------------------------------------------------
// parseRawBinary — return as-is
// ---------------------------------------------------------------------------
std::vector<uint8_t> WCHHexParser::parseRawBinary(const std::vector<uint8_t>& data)
{
    return data;
}

// ---------------------------------------------------------------------------
// parseIntelHex
// ---------------------------------------------------------------------------
std::vector<uint8_t> WCHHexParser::parseIntelHex(const std::vector<uint8_t>& data)
{
    // Convert binary data to string for line-by-line parsing
    std::string text(reinterpret_cast<const char*>(data.data()), data.size());
    std::istringstream stream(text);

    // Sparse address → byte map
    std::map<uint32_t, uint8_t> sparseMap;

    uint32_t baseAddress = 0;  // extended linear address (upper 16 bits)
    std::string line;

    while (std::getline(stream, line)) {
        // Strip CR/LF
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
            line.pop_back();

        if (line.empty())
            continue;

        if (line[0] != ':')
            throw WCHHexParseError("Invalid Intel HEX record (no ':')");

        // Minimum: ':'(1) + bytecount(2) + addr(4) + type(2) + checksum(2) = 11 chars
        if (line.size() < 11)
            throw WCHHexParseError("Intel HEX record too short");

        // Parse byte count
        auto hexByte = [&](size_t offset) -> uint8_t {
            if (offset + 2 > line.size())
                throw WCHHexParseError("Intel HEX record truncated");
            int val = 0;
            for (int i = 0; i < 2; ++i) {
                char c = line[offset + i];
                if (c >= '0' && c <= '9')      val = val * 16 + (c - '0');
                else if (c >= 'A' && c <= 'F') val = val * 16 + (c - 'A' + 10);
                else if (c >= 'a' && c <= 'f') val = val * 16 + (c - 'a' + 10);
                else throw WCHHexParseError("Invalid hex character in record");
            }
            return static_cast<uint8_t>(val);
        };

        uint8_t byteCount = hexByte(1);
        uint8_t addrHi    = hexByte(3);
        uint8_t addrLo    = hexByte(5);
        uint16_t addr16   = (static_cast<uint16_t>(addrHi) << 8) | addrLo;
        uint8_t recType   = hexByte(7);

        // Verify checksum
        uint8_t checksum = 0;
        for (size_t i = 0; i < static_cast<size_t>(byteCount) + 4; ++i) {
            checksum += hexByte(1 + i * 2);
        }
        uint8_t recordChecksum = hexByte(9 + byteCount * 2);
        uint8_t calcChecksum = static_cast<uint8_t>((~checksum) + 1);
        if (calcChecksum != recordChecksum)
            throw WCHHexParseError("Intel HEX checksum mismatch");

        switch (recType) {
        case 0x00: { // Data
            uint32_t fullAddr = baseAddress + addr16;
            for (uint8_t i = 0; i < byteCount; ++i) {
                sparseMap[fullAddr + i] = hexByte(9 + i * 2);
            }
            break;
        }
        case 0x01: // End of File
            goto done;
        case 0x02: { // Extended Segment Address
            if (byteCount != 2) throw WCHHexParseError("Invalid extended segment address record");
            uint16_t seg = (static_cast<uint16_t>(hexByte(9)) << 8) | hexByte(11);
            baseAddress = static_cast<uint32_t>(seg) << 4;
            break;
        }
        case 0x04: { // Extended Linear Address
            if (byteCount != 2) throw WCHHexParseError("Invalid extended linear address record");
            uint16_t upper = (static_cast<uint16_t>(hexByte(9)) << 8) | hexByte(11);
            baseAddress = static_cast<uint32_t>(upper) << 16;
            break;
        }
        default:
            // Ignore other record types (start segment, start linear address)
            break;
        }
    }
done:
    if (sparseMap.empty())
        throw WCHHexParseError("No data records found in Intel HEX file");

    uint32_t minAddr = sparseMap.begin()->first;
    uint32_t maxAddr = sparseMap.rbegin()->first;
    size_t totalSize = static_cast<size_t>(maxAddr - minAddr + 1);

    // Build contiguous array padded with 0xFF
    std::vector<uint8_t> result(totalSize, 0xFF);
    for (const auto& kv : sparseMap) {
        result[kv.first - minAddr] = kv.second;
    }
    return result;
}
