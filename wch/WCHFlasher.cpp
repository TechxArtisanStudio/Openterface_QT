#include "WCHFlasher.h"
#include "WCHProtocol.h"

#include <sstream>
#include <iomanip>
#include <cstring>
#include <thread>
#include <chrono>
#include <cstdlib>

// ---------------------------------------------------------------------------
// Constructor: identify device and read config
// ---------------------------------------------------------------------------
WCHFlasher::WCHFlasher(WCHUSBTransport* transport)
    : m_transport(transport)
{
    if (!m_transport || !m_transport->isOpen())
        throw WCHFlashError("Transport is not open");

    identify();
    readConfig();
    deriveXorKey();
}

// ---------------------------------------------------------------------------
// identify
// ---------------------------------------------------------------------------
void WCHFlasher::identify()
{
    auto packet = WCHPacketBuilder::identify(0, 0);
    auto raw = doTransfer(packet, "identify");

    WCHResponse resp;
    if (!WCHResponse::parse(raw, resp) || !resp.ok)
        throw WCHFlashError("Identify failed: bad response");
    if (resp.payload.size() < 2)
        throw WCHFlashError("Identify failed: response too short");

    uint8_t chipID     = resp.payload[0];
    uint8_t deviceType = resp.payload[1];

    m_chip = WCHChipDB::findChip(chipID, deviceType);
}

// ---------------------------------------------------------------------------
// readConfig
// ---------------------------------------------------------------------------
void WCHFlasher::readConfig()
{
    auto packet = WCHPacketBuilder::readConfig(WCHConstants::CfgMaskAll);
    auto raw = doTransfer(packet, "readConfig");

    WCHResponse resp;
    if (!WCHResponse::parse(raw, resp) || !resp.ok)
        throw WCHFlashError("ReadConfig failed: bad response");
    if (resp.payload.size() < 18)
        throw WCHFlashError("ReadConfig: response too short");

    // Layout: [2 status bytes][RDPR @ 2][...][BTVER 4 bytes @ 14][UID @ 18...]
    m_codeFlashProtected = m_chip.supportsCodeFlashProtect &&
                           (resp.payload[2] != 0xA5);

    m_btver.assign(resp.payload.begin() + 14, resp.payload.begin() + 18);

    size_t uidStart = 18;
    size_t uidEnd   = std::min(uidStart + static_cast<size_t>(m_chip.uidSize),
                               resp.payload.size());
    m_uid.assign(resp.payload.begin() + uidStart, resp.payload.begin() + uidEnd);
}

// ---------------------------------------------------------------------------
// deriveXorKey  — sum = UID[0..uidSize-1].reduce(+); key = [sum×8]; key[7] += chipID
// ---------------------------------------------------------------------------
void WCHFlasher::deriveXorKey()
{
    uint8_t uidSum = 0;
    for (size_t i = 0; i < static_cast<size_t>(m_chip.uidSize) && i < m_uid.size(); ++i)
        uidSum = static_cast<uint8_t>(uidSum + m_uid[i]);

    m_xorKey.assign(8, uidSum);
    m_xorKey[7] = static_cast<uint8_t>(m_xorKey[7] + m_chip.chipID);
}

// ---------------------------------------------------------------------------
// Information helpers
// ---------------------------------------------------------------------------
std::string WCHFlasher::getChipInfo() const
{
    std::ostringstream oss;
    oss << "Chip: " << m_chip.name
        << " (Code Flash: " << (m_chip.flashSize / 1024) << " KiB";
    if (m_chip.eepromSize > 0) {
        if (m_chip.eepromSize % 1024 == 0)
            oss << ", EEPROM: " << (m_chip.eepromSize / 1024) << " KiB";
        else
            oss << ", EEPROM: " << m_chip.eepromSize << " B";
    }
    oss << ")\n";
    oss << "UID: " << chipUID() << "\n";
    oss << "BTVER: " << bootloaderVersion();
    if (m_chip.supportsCodeFlashProtect)
        oss << "\nFlash Protected: " << (m_codeFlashProtected ? "Yes" : "No");
    return oss.str();
}

std::string WCHFlasher::bootloaderVersion() const
{
    if (m_btver.size() < 4) return "?";
    std::ostringstream oss;
    oss << std::hex << static_cast<int>(m_btver[0])
        << static_cast<int>(m_btver[1])
        << "."
        << static_cast<int>(m_btver[2])
        << static_cast<int>(m_btver[3]);
    return oss.str();
}

std::string WCHFlasher::chipUID() const
{
    std::ostringstream oss;
    for (size_t i = 0; i < m_uid.size(); ++i) {
        if (i) oss << "-";
        oss << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
            << static_cast<int>(m_uid[i]);
    }
    return oss.str();
}

bool WCHFlasher::isCodeFlashProtected() const
{
    return m_codeFlashProtected && m_chip.supportsCodeFlashProtect;
}

// ---------------------------------------------------------------------------
// XOR encrypt/decrypt — rotating key window
// ---------------------------------------------------------------------------
std::vector<uint8_t> WCHFlasher::xorChunk(const std::vector<uint8_t>& data,
                                            size_t startOffset) const
{
    std::vector<uint8_t> out(data.size());
    for (size_t i = 0; i < data.size(); ++i)
        out[i] = data[i] ^ m_xorKey[(startOffset + i) % 8];
    return out;
}

// ---------------------------------------------------------------------------
// doTransfer — send packet, receive raw bytes
// ---------------------------------------------------------------------------
std::vector<uint8_t> WCHFlasher::doTransfer(const std::vector<uint8_t>& packet,
                                              const std::string& opName)
{
    try {
        return m_transport->transfer(packet);
    } catch (const WCHTransportError& e) {
        throw WCHFlashError(opName + " transfer error: " + e.what());
    }
}

void WCHFlasher::reportProgress(const WCHProgressCallback& cb, int pct,
                                 const std::string& msg)
{
    if (cb) cb(pct, msg);
}

// ---------------------------------------------------------------------------
// unprotect
// ---------------------------------------------------------------------------
void WCHFlasher::unprotect()
{
    if (!m_codeFlashProtected) return;

    reportProgress(nullptr, 0, "Reading config for unprotect...");

    auto packet = WCHPacketBuilder::readConfig(WCHConstants::CfgMaskRDPRUserDataWPR);
    auto raw = doTransfer(packet, "readConfig(unprotect)");

    WCHResponse resp;
    if (!WCHResponse::parse(raw, resp) || !resp.ok)
        throw WCHFlashError("Unprotect readConfig failed");
    if (resp.payload.size() < 14)
        throw WCHFlashError("Unprotect readConfig: response too short");

    // Patch: RDPR=0xA5 (unprotected), WPR=0xFFFFFFFF
    std::vector<uint8_t> config(resp.payload.begin() + 2,
                                resp.payload.begin() + 14);
    config[0] = 0xA5;
    config[1] = 0x5A;
    config[8] = 0xFF;
    config[9] = 0xFF;
    config[10] = 0xFF;
    config[11] = 0xFF;

    auto wpacket = WCHPacketBuilder::writeConfig(WCHConstants::CfgMaskRDPRUserDataWPR,
                                                  config);
    auto wraw = doTransfer(wpacket, "writeConfig(unprotect)");
    WCHResponse wresp;
    if (!WCHResponse::parse(wraw, wresp) || !wresp.ok)
        throw WCHFlashError("Unprotect writeConfig failed");

    m_codeFlashProtected = false;
}

// ---------------------------------------------------------------------------
// erase
// ---------------------------------------------------------------------------
void WCHFlasher::erase(uint32_t firmwareSize)
{
    uint32_t sectors = (firmwareSize + WCHConstants::SectorSize - 1) /
                        WCHConstants::SectorSize;
    if (sectors == 0) sectors = 1;

    auto packet = WCHPacketBuilder::erase(sectors);
    auto raw = doTransfer(packet, "erase");

    WCHResponse resp;
    if (!WCHResponse::parse(raw, resp) || !resp.ok)
        throw WCHFlashError("Erase failed");
}

// ---------------------------------------------------------------------------
// program
// ---------------------------------------------------------------------------
void WCHFlasher::program(const std::vector<uint8_t>& firmware,
                          const WCHProgressCallback& progress)
{
    // Stage 1: send ISP key (30 zero bytes)
    reportProgress(progress, 0, "Sending ISP key...");
    std::vector<uint8_t> keyPayload(30, 0x00);
    auto kpacket = WCHPacketBuilder::ispKey(keyPayload);
    auto kraw = doTransfer(kpacket, "ispKey");
    WCHResponse kresp;
    if (!WCHResponse::parse(kraw, kresp) || !kresp.ok)
        throw WCHFlashError("ISP key exchange failed");

    // Validate returned checksum = sum of xorKey bytes
    if (!kresp.payload.empty()) {
        uint8_t expected = 0;
        for (uint8_t b : m_xorKey) expected = static_cast<uint8_t>(expected + b);
        if (kresp.payload[0] != expected)
            throw WCHFlashError("ISP key checksum mismatch");
    }

    // Stage 2: program in 56-byte chunks
    const int chunkSize = WCHConstants::ProgramChunkSize;
    size_t total = firmware.size();
    size_t offset = 0;
    int    chunkIndex = 0;
    size_t totalChunks = (total + chunkSize - 1) / chunkSize;

    while (offset < total) {
        size_t remaining = total - offset;
        size_t thisChunkSize = (remaining < static_cast<size_t>(chunkSize))
                                   ? remaining
                                   : static_cast<size_t>(chunkSize);

        std::vector<uint8_t> chunk(firmware.begin() + offset,
                                   firmware.begin() + offset + thisChunkSize);
        std::vector<uint8_t> encrypted = xorChunk(chunk, offset);

        uint8_t padding = static_cast<uint8_t>(std::rand() & 0xFF);
        auto packet = WCHPacketBuilder::program(static_cast<uint32_t>(offset),
                                                 padding, encrypted);
        auto raw = doTransfer(packet, "program");
        WCHResponse resp;
        if (!WCHResponse::parse(raw, resp) || !resp.ok)
            throw WCHFlashError("Program failed at offset 0x" +
                                 std::to_string(offset));

        offset += thisChunkSize;
        ++chunkIndex;

        int pct = static_cast<int>(chunkIndex * 50 / totalChunks); // 0–50%
        reportProgress(progress, pct,
                       "Programming: " + std::to_string(offset) + "/" +
                       std::to_string(total) + " bytes");
    }

    // Final empty chunk
    uint8_t padding = static_cast<uint8_t>(std::rand() & 0xFF);
    auto fpacket = WCHPacketBuilder::program(static_cast<uint32_t>(offset),
                                              padding, {});
    auto fraw = doTransfer(fpacket, "program(final)");
    WCHResponse fresp;
    if (!WCHResponse::parse(fraw, fresp) || !fresp.ok)
        throw WCHFlashError("Final program chunk failed");

    reportProgress(progress, 50, "Programming complete");
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
}

// ---------------------------------------------------------------------------
// verify
// ---------------------------------------------------------------------------
void WCHFlasher::verify(const std::vector<uint8_t>& firmware,
                         const WCHProgressCallback& progress)
{
    // Re-send ISP key for verify phase
    reportProgress(progress, 50, "Sending ISP key for verify...");
    std::vector<uint8_t> keyPayload(30, 0x00);
    auto kpacket = WCHPacketBuilder::ispKey(keyPayload);
    auto kraw = doTransfer(kpacket, "ispKey(verify)");
    WCHResponse kresp;
    if (!WCHResponse::parse(kraw, kresp) || !kresp.ok)
        throw WCHFlashError("ISP key exchange failed (verify phase)");

    const int chunkSize = WCHConstants::ProgramChunkSize;
    size_t total = firmware.size();
    size_t offset = 0;
    int    chunkIndex = 0;
    size_t totalChunks = (total + chunkSize - 1) / chunkSize;

    while (offset < total) {
        size_t remaining = total - offset;
        size_t thisChunkSize = (remaining < static_cast<size_t>(chunkSize))
                                   ? remaining
                                   : static_cast<size_t>(chunkSize);

        std::vector<uint8_t> chunk(firmware.begin() + offset,
                                   firmware.begin() + offset + thisChunkSize);
        std::vector<uint8_t> encrypted = xorChunk(chunk, offset);

        uint8_t padding = static_cast<uint8_t>(std::rand() & 0xFF);
        auto packet = WCHPacketBuilder::verify(static_cast<uint32_t>(offset),
                                                padding, encrypted);
        auto raw = doTransfer(packet, "verify");
        WCHResponse resp;
        if (!WCHResponse::parse(raw, resp) || !resp.ok)
            throw WCHFlashError("Verify failed at offset 0x" +
                                 std::to_string(offset));

        offset += thisChunkSize;
        ++chunkIndex;

        int pct = 50 + static_cast<int>(chunkIndex * 45 / totalChunks); // 50–95%
        reportProgress(progress, pct,
                       "Verifying: " + std::to_string(offset) + "/" +
                       std::to_string(total) + " bytes");
    }

    reportProgress(progress, 95, "Verify complete");
}

// ---------------------------------------------------------------------------
// protect
// ---------------------------------------------------------------------------
void WCHFlasher::protect()
{
    if (!m_chip.supportsCodeFlashProtect) return;

    auto packet = WCHPacketBuilder::readConfig(WCHConstants::CfgMaskRDPRUserDataWPR);
    auto raw = doTransfer(packet, "readConfig(protect)");
    WCHResponse resp;
    if (!WCHResponse::parse(raw, resp) || !resp.ok)
        throw WCHFlashError("Protect readConfig failed");
    if (resp.payload.size() < 14)
        throw WCHFlashError("Protect readConfig: response too short");

    std::vector<uint8_t> config(resp.payload.begin() + 2,
                                resp.payload.begin() + 14);
    config[0] = 0x00;
    config[1] = 0x00;
    config[8] = 0x00;
    config[9] = 0x00;
    config[10] = 0x00;
    config[11] = 0x00;

    auto wpacket = WCHPacketBuilder::writeConfig(WCHConstants::CfgMaskRDPRUserDataWPR,
                                                  config);
    auto wraw = doTransfer(wpacket, "writeConfig(protect)");
    WCHResponse wresp;
    if (!WCHResponse::parse(wraw, wresp) || !wresp.ok)
        throw WCHFlashError("Protect writeConfig failed");

    m_codeFlashProtected = true;
}

// ---------------------------------------------------------------------------
// reset
// ---------------------------------------------------------------------------
void WCHFlasher::reset()
{
    auto packet = WCHPacketBuilder::ispEnd(1);
    try {
        m_transport->transfer(packet);
    } catch (const WCHTransportError&) {
        // After ispEnd the device reboots immediately and drops the USB
        // connection.  The read side of the transfer will therefore fail —
        // this is expected and not an error.
    }

    std::this_thread::sleep_for(std::chrono::seconds(3));
}

// ---------------------------------------------------------------------------
// flash — full pipeline
// ---------------------------------------------------------------------------
void WCHFlasher::flash(const std::vector<uint8_t>& firmware,
                        const WCHProgressCallback& progress)
{
    if (firmware.empty())
        throw WCHFlashError("Firmware data is empty");

    // Stage 0: unprotect if needed
    reportProgress(progress, 0, "Checking flash protection...");
    if (m_codeFlashProtected) {
        reportProgress(progress, 2, "Unprotecting flash...");
        unprotect();
    }

    // Stage 1: erase
    reportProgress(progress, 5, "Erasing flash...");
    erase(static_cast<uint32_t>(firmware.size()));
    reportProgress(progress, 10, "Erase complete");

    // Stage 2: program (progress 10–50%)
    program(firmware, progress);

    // Stage 3: verify (progress 50–95%)
    verify(firmware, progress);

    // Stage 4: protect
    reportProgress(progress, 96, "Re-protecting flash...");
    protect();

    // Stage 5: reset
    reportProgress(progress, 98, "Resetting device...");
    reset();

    reportProgress(progress, 100, "Flash complete! Reconnect the device.");
}
