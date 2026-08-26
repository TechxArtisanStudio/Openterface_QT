// ISP deep diagnostic: config registers, full verify with logging, EEPROM dump
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <vector>
#include <algorithm>

#include "wch/WCHFlasher.h"
#include "wch/WCHUSBTransport.h"
#include "wch/WCHProtocol.h"
#include "wch/WCHHexParser.h"

// Access chip info via public API, derive XOR key manually for diagnostic verify
std::vector<uint8_t> deriveXorKeyFromChip(WCHFlasher& f) {
    // Parse UID from chipUID() string "AA-BB-CC-DD-EE-FF-00-11"
    std::string uidStr = f.chipUID();
    std::vector<uint8_t> uid;
    std::istringstream iss(uidStr);
    std::string byte;
    while (std::getline(iss, byte, '-')) {
        uid.push_back(static_cast<uint8_t>(std::stoul(byte, nullptr, 16)));
    }

    uint8_t uidSum = 0;
    for (auto b : uid) uidSum = static_cast<uint8_t>(uidSum + b);

    // Parse chipID from getChipInfo() — look for "chipID=0xNN"
    std::string info = f.getChipInfo();
    uint8_t chipID = 0;
    auto pos = info.find("chipID=0x");
    if (pos != std::string::npos) {
        chipID = static_cast<uint8_t>(std::stoul(info.substr(pos + 9, 2), nullptr, 16));
    }

    std::vector<uint8_t> key(8, uidSum);
    key[7] = static_cast<uint8_t>(key[7] + chipID);
    return key;
}

int main(int argc, char* argv[])
{
    std::string firmwarePath = "/home/bot/project/06(1).hex";
    if (argc > 1) firmwarePath = argv[1];

    try {
        WCHUSBTransport transport;
        auto devices = transport.scanDevices();
        if (devices.empty()) { std::cerr << "No ISP device!" << std::endl; return 1; }
        transport.open(0);

        WCHFlasher flasher(&transport);

        // === Config dump ===
        std::cout << "=== Chip Info ===" << std::endl;
        std::cout << flasher.getChipInfo() << std::endl;

        std::cout << "XOR Key: ";
        auto xorKey = deriveXorKeyFromChip(flasher);
        for (auto b : xorKey)
            std::cout << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << (int)b << " ";
        std::cout << std::endl;

        // Read full config (mask 0x1F = all)
        auto cfgPacket = WCHPacketBuilder::readConfig(0x1F);
        auto cfgRaw = transport.transfer(cfgPacket);
        WCHResponse cfgResp;
        WCHResponse::parse(cfgRaw, cfgResp);

        std::cout << "\n=== Raw Config Response ===" << std::endl;
        std::cout << "Status: 0x" << std::hex << (int)cfgResp.status << std::dec << std::endl;
        std::cout << "Payload (" << cfgResp.payload.size() << " bytes): ";
        for (size_t i = 0; i < cfgResp.payload.size(); i++)
            std::cout << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
                      << (int)cfgResp.payload[i] << " ";
        std::cout << std::dec << std::endl;

        if (cfgResp.payload.size() >= 18) {
            std::cout << "\n=== Config Register Breakdown ===" << std::endl;
            std::cout << "RDPR:  0x" << std::hex << std::setw(2) << (int)cfgResp.payload[2] << std::dec
                      << (cfgResp.payload[2] == 0xA5 ? " (unprotected)" : " (protected)") << std::endl;
            std::cout << "nRDPR: 0x" << std::hex << std::setw(2) << (int)cfgResp.payload[3] << std::dec << std::endl;
            std::cout << "USER:  0x" << std::hex << std::setw(2) << (int)cfgResp.payload[4] << std::dec << std::endl;
            std::cout << "  SRAM_CODE_MODE: 0b" << ((cfgResp.payload[4] >> 6) & 0x03) << std::endl;
            std::cout << "  IWDG_SW: " << ((cfgResp.payload[4] >> 0) & 0x01) << std::endl;
            std::cout << "  STOP_RST: " << ((cfgResp.payload[4] >> 1) & 0x01) << std::endl;
            std::cout << "  STANDBY_RST: " << ((cfgResp.payload[4] >> 2) & 0x01) << std::endl;
            std::cout << "nUSER: 0x" << std::hex << std::setw(2) << (int)cfgResp.payload[5] << std::dec << std::endl;
            std::cout << "DATA0:  0x" << std::hex << std::setw(2) << (int)cfgResp.payload[6] << std::dec << std::endl;
            std::cout << "nDATA0: 0x" << std::hex << std::setw(2) << (int)cfgResp.payload[7] << std::dec << std::endl;
            std::cout << "DATA1:  0x" << std::hex << std::setw(2) << (int)cfgResp.payload[8] << std::dec << std::endl;
            std::cout << "nDATA1: 0x" << std::hex << std::setw(2) << (int)cfgResp.payload[9] << std::dec << std::endl;
            std::cout << "WPR: 0x";
            for (int i = 10; i < 14; i++)
                std::cout << std::hex << std::setw(2) << (int)cfgResp.payload[i];
            std::cout << std::dec << std::endl;
            std::cout << "BTVER: ";
            for (int i = 14; i < 18; i++)
                std::cout << std::hex << std::setw(2) << (int)cfgResp.payload[i];
            std::cout << std::dec << std::endl;
            std::cout << "UID: ";
            for (size_t i = 18; i < cfgResp.payload.size(); i++)
                std::cout << std::hex << std::setw(2) << (int)cfgResp.payload[i] << " ";
            std::cout << std::dec << std::endl;
        }

        // Parse firmware and pad to sector boundary
        WCHHexParser parser;
        auto firmware = parser.parseFile(firmwarePath);
        std::cout << "\n=== Firmware ===" << std::endl;
        std::cout << "Original size: " << firmware.size() << " bytes" << std::endl;
        // Pad to sector boundary (same as flash() does)
        if (firmware.size() % 1024 != 0) {
            size_t remain = 1024 - (firmware.size() % 1024);
            firmware.insert(firmware.end(), remain, 0x00);
        }
        std::cout << "Padded size: " << firmware.size() << " bytes" << std::endl;

        // Show first 32 bytes of firmware (reset vector area)
        std::cout << "Firmware[0..31]: ";
        for (int i = 0; i < 32; i++)
            std::cout << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << (int)firmware[i] << " ";
        std::cout << std::dec << std::endl;

        // === Full verify with detailed logging ===
        std::cout << "\n=== Full Verify (detailed) ===" << std::endl;

        // Send ISP key
        std::vector<uint8_t> keyPayload(30, 0x00);
        auto kpacket = WCHPacketBuilder::ispKey(keyPayload);
        auto kraw = transport.transfer(kpacket);
        WCHResponse kresp;
        WCHResponse::parse(kraw, kresp);
        std::cout << "ISP key response: status=0x" << std::hex << (int)kresp.status
                  << " payload[0]=0x" << (kresp.payload.empty() ? 0xFF : (int)kresp.payload[0])
                  << std::dec << std::endl;

        // Verify first 10 chunks and last 3 chunks
        const int chunkSize = 56;
        size_t total = firmware.size();
        // Pad to sector boundary
        if (total % 1024 != 0) total += 1024 - (total % 1024);

        size_t totalChunks = (total + chunkSize - 1) / chunkSize;
        std::cout << "Total chunks: " << totalChunks << std::endl;

        int verifiedOk = 0;
        int verifiedFail = 0;
        int firstFailChunk = -1;
        size_t offset = 0;

        for (size_t chunkIdx = 0; offset < total; chunkIdx++) {
            size_t remaining = total - offset;
            size_t thisChunkSize = std::min(remaining, (size_t)chunkSize);

            std::vector<uint8_t> chunk(firmware.begin() + offset,
                                       firmware.begin() + offset + thisChunkSize);

            // XOR encrypt
            const auto& xorKeyRef = xorKey;
            std::vector<uint8_t> encrypted(thisChunkSize);
            for (size_t i = 0; i < thisChunkSize; i++)
                encrypted[i] = chunk[i] ^ xorKeyRef[(offset + i) % 8];

            uint8_t padding = 0x42;  // fixed for reproducibility
            auto packet = WCHPacketBuilder::verify(static_cast<uint32_t>(offset), padding, encrypted);
            auto raw = transport.transfer(packet);
            WCHResponse resp;
            WCHResponse::parse(raw, resp);

            bool ok = resp.ok && !resp.payload.empty() && resp.payload[0] == 0x00;

            // Log first 10, last 3, and any failures
            bool logThis = (chunkIdx < 10) || (chunkIdx >= totalChunks - 3) || !ok;

            if (ok) {
                verifiedOk++;
            } else {
                verifiedFail++;
                if (firstFailChunk < 0) firstFailChunk = chunkIdx;
            }

            if (logThis) {
                std::cout << "Chunk " << chunkIdx << " (offset 0x" << std::hex << offset
                          << ", " << std::dec << thisChunkSize << " bytes): "
                          << "status=0x" << std::hex << (int)resp.status
                          << " payload[0]=0x" << (resp.payload.empty() ? 0xFF : (int)resp.payload[0])
                          << std::dec << (ok ? " OK" : " FAIL")
                          << std::endl;

                if (!ok && chunkIdx < 5) {
                    // Show raw response
                    std::cout << "  Raw response (" << raw.size() << " bytes): ";
                    for (size_t i = 0; i < std::min(raw.size(), (size_t)16); i++)
                        std::cout << std::hex << std::setw(2) << (int)raw[i] << " ";
                    std::cout << std::dec << std::endl;

                    // Show original and encrypted data
                    std::cout << "  Original: ";
                    for (size_t i = 0; i < std::min(thisChunkSize, (size_t)16); i++)
                        std::cout << std::hex << std::setw(2) << (int)chunk[i] << " ";
                    std::cout << std::dec << std::endl;
                    std::cout << "  Encrypted: ";
                    for (size_t i = 0; i < std::min(thisChunkSize, (size_t)16); i++)
                        std::cout << std::hex << std::setw(2) << (int)encrypted[i] << " ";
                    std::cout << std::dec << std::endl;
                }
            }

            offset += thisChunkSize;
        }

        std::cout << "\n=== Verify Summary ===" << std::endl;
        std::cout << "OK: " << verifiedOk << "/" << (verifiedOk + verifiedFail) << std::endl;
        std::cout << "FAIL: " << verifiedFail << "/" << (verifiedOk + verifiedFail) << std::endl;
        if (firstFailChunk >= 0)
            std::cout << "First failure at chunk " << firstFailChunk << std::endl;

        // === EEPROM dump (first 256 bytes) ===
        std::cout << "\n=== EEPROM Dump (first 256 bytes) ===" << std::endl;
        for (uint32_t addr = 0; addr < 256; addr += 32) {
            auto packet = WCHPacketBuilder::dataRead(addr, 32);
            auto raw = transport.transfer(packet);
            WCHResponse resp;
            if (WCHResponse::parse(raw, resp) && resp.ok) {
                std::cout << "EEPROM[0x" << std::hex << std::setw(4) << std::setfill('0') << addr << "]: ";
                for (size_t i = 0; i < resp.payload.size(); i++)
                    std::cout << std::hex << std::setw(2) << (int)resp.payload[i] << " ";
                std::cout << std::dec << std::endl;
            }
        }

        transport.close();

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
