#include "WCHFlasher.h"
#include "WCHProtocol.h"

#include <sstream>
#include <iomanip>
#include <cstring>
#include <thread>
#include <chrono>
#include <cstdlib>
#include <algorithm>

// ---------------------------------------------------------------------------
// extendFirmwareToSectorBoundary — pad firmware to 1024-byte boundary.
// Mirrors wchisp src/main.rs extend_firmware_to_sector_boundary().
// Padding uses 0x00 to match wchisp behavior (erased flash is 0xFF;
// after XOR with the key, the actual byte written differs, but the
// resulting flash content is correct).
// ---------------------------------------------------------------------------
static std::vector<uint8_t> extendFirmwareToSectorBoundary(
    const std::vector<uint8_t>& firmware)
{
    constexpr size_t kSectorSize = 1024;
    if (firmware.size() % kSectorSize == 0)
        return firmware;  // already aligned

    std::vector<uint8_t> padded = firmware;
    size_t remain = kSectorSize - (firmware.size() % kSectorSize);
    padded.insert(padded.end(), remain, 0x00);  // 0x00 padding, matches wchisp
    return padded;
}

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

    m_rawChipID     = resp.payload[0];
    m_rawDeviceType = resp.payload[1];

    m_chip = WCHChipDB::findChip(m_rawChipID, m_rawDeviceType);
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

    // Layout: [2 status bytes][RDPR @ 2][nRDPR @ 3][USER @ 4][nUSER @ 5]
    //         [DATA0 @ 6][nDATA0 @ 7][DATA1 @ 8][nDATA1 @ 9]
    //         [WPR @ 10..13][BTVER @ 14..17][UID @ 18...]
    m_codeFlashProtected = m_chip.supportsCodeFlashProtect &&
                           (resp.payload[2] != 0xA5);

    m_btver.assign(resp.payload.begin() + 14, resp.payload.begin() + 18);

    size_t uidStart = 18;
    size_t uidEnd   = std::min(uidStart + static_cast<size_t>(m_chip.uidSize),
                               resp.payload.size());
    m_uid.assign(resp.payload.begin() + uidStart, resp.payload.begin() + uidEnd);

    // For CH32V20x (deviceType 0x19), adjust flash size based on SRAM_CODE_MODE.
    // The USER byte bits [7:6] select how total silicon area is split between
    // Code Flash and SRAM.  The chip database stores the default (smallest)
    // flash size; we patch it up here if the user configuration maps more
    // area to code flash.
    //
    // Reference: CH32V20x RM "32.6 User option bytes" → SRAM_CODE_MODE field,
    // and wchisp devices/0x19-CH32V20x.yaml.
    if (m_chip.deviceType == 0x19) {
        uint8_t userByte = resp.payload[4];
        uint8_t sramCodeMode = (userByte >> 6) & 0x03;

        // V4B (chipID 0x80-0x83, CH32V208 family): larger total flash
        // V4C (chipID 0x30-0x37, CH32V203 family): smaller total flash
        bool isV4B = (m_rawChipID >= 0x80 && m_rawChipID <= 0x83);

        uint32_t newFlashSize = 0;
        if (isV4B) {
            // CH32V208 variants (first value in each SRAM_CODE_MODE row)
            switch (sramCodeMode) {
                case 0b00: newFlashSize = 192 * 1024; break;
                case 0b01: newFlashSize = 224 * 1024; break;
                case 0b10: newFlashSize = 256 * 1024; break;
                case 0b11: newFlashSize = 228 * 1024; break;
            }
        } else {
            // CH32V203 variants (second value in each SRAM_CODE_MODE row)
            switch (sramCodeMode) {
                case 0b00: newFlashSize = 128 * 1024; break;
                case 0b01: newFlashSize = 144 * 1024; break;
                case 0b10: newFlashSize = 160 * 1024; break;
                case 0b11: newFlashSize = 160 * 1024; break;
            }
        }

        if (newFlashSize > 0 && newFlashSize != m_chip.flashSize) {
            m_chip.flashSize = newFlashSize;
        }
    }
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
    // Raw device-reported IDs for diagnostics
    oss << "Device ID: chipID=0x"
        << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
        << static_cast<unsigned>(m_rawChipID)
        << " deviceType=0x"
        << std::setw(2) << static_cast<unsigned>(m_rawDeviceType) << "\n";
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
// unprotect — write config (RDPR=0xA5, WPR=0xFF..FF), then RESET the device.
//
// The WCH ISP bootloader only applies the new flash-protection config after
// a device reset.  Without the reset the protection remains unchanged even
// though writeConfig returned success.  After the reset the USB connection
// drops; the caller must close+reopen the transport and call reidentify()
// before continuing with erase/program.
//
// Flow mirrors wchisp (ch32-rs/wchisp) src/flashing.rs unprotect().
// ---------------------------------------------------------------------------
void WCHFlasher::unprotect()
{
    if (!m_codeFlashProtected) return;

    // Step 1: Read current config
    auto packet = WCHPacketBuilder::readConfig(WCHConstants::CfgMaskRDPRUserDataWPR);
    auto raw = doTransfer(packet, "readConfig(unprotect)");

    WCHResponse resp;
    if (!WCHResponse::parse(raw, resp) || !resp.ok)
        throw WCHFlashError("Unprotect readConfig failed");
    if (resp.payload.size() < 14)
        throw WCHFlashError("Unprotect readConfig: response too short");

    // Patch: RDPR=0xA5 (unprotected), nRDPR=0x5A, WPR=0xFFFFFFFF
    std::vector<uint8_t> config(resp.payload.begin() + 2,
                                resp.payload.begin() + 14);
    config[0] = 0xA5;
    config[1] = 0x5A;
    config[8]  = 0xFF;
    config[9]  = 0xFF;
    config[10] = 0xFF;
    config[11] = 0xFF;

    // Step 2: Write the unprotect config
    auto wpacket = WCHPacketBuilder::writeConfig(WCHConstants::CfgMaskRDPRUserDataWPR,
                                                  config);
    auto wraw = doTransfer(wpacket, "writeConfig(unprotect)");
    WCHResponse wresp;
    if (!WCHResponse::parse(wraw, wresp) || !wresp.ok)
        throw WCHFlashError("Unprotect writeConfig failed");

    // Step 3: Reset device so the config change takes effect.
    // On WCH ISP bootloader, config changes (especially RDPR) only apply after
    // a device reset.  Without this, the flash remains protected and subsequent
    // erase/program will silently fail.
    m_codeFlashProtected = false;
    reset();

    // Step 4: Reconnect — the device reboots and re-enumerates on USB.
    // We close the stale transport handle, wait for re-enumeration, then
    // reopen and re-identify before continuing with erase/program.
    m_transport->close();

    constexpr int kMaxAttempts = 10;
    bool reconnected = false;
    for (int attempt = 0; attempt < kMaxAttempts && !reconnected; ++attempt) {
        std::this_thread::sleep_for(std::chrono::seconds(2));
        auto names = m_transport->scanDevices();
        if (names.empty()) continue;
        for (size_t i = 0; i < names.size(); ++i) {
            try {
                m_transport->open(static_cast<int>(i));
                auto pkt = WCHPacketBuilder::identify(m_rawChipID, m_rawDeviceType);
                auto raw = m_transport->transfer(pkt);
                WCHResponse r;
                if (WCHResponse::parse(raw, r) && r.ok &&
                    r.payload.size() >= 2 &&
                    r.payload[0] == m_rawChipID &&
                    r.payload[1] == m_rawDeviceType) {
                    reconnected = true;
                    break;
                }
                m_transport->close();
            } catch (const std::exception&) {
                m_transport->close();
            }
        }
    }
    if (!reconnected)
        throw WCHFlashError("Device did not come back after unprotect reset.");

    // Re-read config and derive fresh XOR key
    readConfig();
    deriveXorKey();
}

// ---------------------------------------------------------------------------
// reidentify — send identify with known chip IDs, then read config.
// Called after the device has been reset (e.g. after unprotect) and the
// transport has been reopened.  This mirrors wchisp's reidentify().
// ---------------------------------------------------------------------------
void WCHFlasher::reidentify()
{
    auto packet = WCHPacketBuilder::identify(m_chip.chipID, m_chip.deviceType);
    auto raw = doTransfer(packet, "reidentify");

    WCHResponse resp;
    if (!WCHResponse::parse(raw, resp) || !resp.ok)
        throw WCHFlashError("Re-identify failed: bad response");
    if (resp.payload.size() < 2)
        throw WCHFlashError("Re-identify: response too short");

    if (resp.payload[0] != m_chip.chipID)
        throw WCHFlashError("Re-identify: chip ID mismatch");
    if (resp.payload[1] != m_chip.deviceType)
        throw WCHFlashError("Re-identify: device type mismatch");

    readConfig();
    deriveXorKey();
}

// ---------------------------------------------------------------------------
// erase — erase code flash to fit `firmwareSize` bytes of firmware.
// Matches wchisp: sectors = binary.len() / SECTOR_SIZE + 1, with a minimum
// of min_erase_sector_number (8 for deviceType 0x19, 4 for 0x10).
// ---------------------------------------------------------------------------
void WCHFlasher::erase(uint32_t firmwareSize)
{
    uint32_t sectors = firmwareSize / WCHConstants::SectorSize + 1;

    // Enforce minimum erase sector count (matches wchisp min_erase_sector_number)
    uint32_t minSectors = (m_chip.deviceType == 0x10) ? 4 : 8;
    if (sectors < minSectors) sectors = minSectors;

    auto packet = WCHPacketBuilder::erase(sectors);
    auto raw = doTransfer(packet, "erase");

    WCHResponse resp;
    if (!WCHResponse::parse(raw, resp) || !resp.ok)
        throw WCHFlashError("Erase failed");
}

// ---------------------------------------------------------------------------
// eraseData — erase `sectors` 1KiB sectors of data flash (EEPROM).
// ---------------------------------------------------------------------------
void WCHFlasher::eraseData(uint16_t sectors)
{
    if (sectors == 0) return;

    auto packet = WCHPacketBuilder::dataErase(sectors);
    auto raw = doTransfer(packet, "dataErase");

    WCHResponse resp;
    if (!WCHResponse::parse(raw, resp) || !resp.ok)
        throw WCHFlashError("Data flash erase failed");
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

        // Match wchisp (flashing.rs:452): the device signals verify match/mismatch
        // in payload[0], NOT in the status byte.  wchisp's Response::from_raw has
        // an `if true` that ignores the status byte entirely, so the payload check
        // is the only real verification.  Without this, a mismatch (payload[0]!=0)
        // with status=0x00 would silently pass.
        if (resp.payload.empty() || resp.payload[0] != 0x00) {
            std::ostringstream msg;
            msg << "Verify MISMATCH at offset 0x" << std::hex << offset
                << " (payload[0]=0x"
                << (resp.payload.empty() ? std::string("??")
                                          : ([&]{
                                                char b[3];
                                                std::snprintf(b, sizeof(b), "%02X",
                                                              resp.payload[0]);
                                                return std::string(b);
                                            })())
                << ")";
            throw WCHFlashError(msg.str());
        }

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
// reset — send ispEnd(1) to reboot the device into user firmware.
// reason=0 resets to ISP mode, reason=1 resets to user firmware.
// The device may drop the USB connection before responding, which is expected.
// ---------------------------------------------------------------------------
void WCHFlasher::reset()
{
    auto packet = WCHPacketBuilder::ispEnd(1);  // Use 1 to boot user firmware
    try {
        auto raw = doTransfer(packet, "ispEnd");
        WCHResponse resp;
        if (!WCHResponse::parse(raw, resp) || !resp.ok) {
            // Non-fatal: device may have rebooted before responding
        }
    } catch (const WCHFlashError&) {
        // Expected: device drops USB immediately after ispEnd
    }
    std::this_thread::sleep_for(std::chrono::seconds(2));
}

// ---------------------------------------------------------------------------
// flash — full pipeline
//
// Matches wchisp main.rs Flash subcommand:
//   [unprotect if needed] → erase → sleep(1s) → program → sleep(500ms)
//   → verify → reset
// ---------------------------------------------------------------------------
void WCHFlasher::flash(const std::vector<uint8_t>& rawFirmware,
                        const WCHProgressCallback& progress)
{
    if (rawFirmware.empty())
        throw WCHFlashError("Firmware data is empty");

    // Pad firmware to 1024-byte sector boundary (matches wchisp)
    auto firmware = extendFirmwareToSectorBoundary(rawFirmware);

    // Stage 0: unprotect if needed (wchisp Flash subcommand does NOT call
    // unprotect; but we keep it for compatibility with protected devices
    // since the reference config unprotect is a separate subcommand)
    reportProgress(progress, 0, "Checking flash protection...");
    if (m_codeFlashProtected) {
        reportProgress(progress, 2, "Unprotecting flash...");
        unprotect();
    }

    // Stage 1: erase
    reportProgress(progress, 5, "Erasing flash...");
    erase(static_cast<uint32_t>(firmware.size()));
    reportProgress(progress, 10, "Erase complete");

    // wchisp: sleep 1s after erase
    std::this_thread::sleep_for(std::chrono::seconds(1));

    // Stage 2: program (progress 10–50%)
    program(firmware, progress);

    // wchisp: sleep 500ms between flash and verify
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Stage 3: verify (progress 50–95%)
    verify(firmware, progress);

    // NOTE: we intentionally do NOT call protect() here.
    // On WCH CH32V chips, changing RDPR from 0xA5 to 0x00 triggers an
    // automatic code flash erase as a security side-effect.
    // wchisp also does NOT call protect after flashing.

    // Stage 4: reset
    reportProgress(progress, 96, "Resetting device...");
    reset();

    reportProgress(progress, 100, "Flash complete! Reconnect the device.");
}
