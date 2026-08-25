#include <QCoreApplication>
#include <cstdio>
#include <vector>
#include "wch/WCHUSBTransport.h"
#include "wch/WCHProtocol.h"
#include "wch/WCHFlasher.h"

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);

    try {
        WCHUSBTransport transport;
        auto devices = transport.scanDevices();
        if (devices.empty()) {
            fprintf(stderr, "未找到WCH ISP设备\n");
            return 1;
        }
        transport.open(0);

        WCHFlasher flasher(&transport);
        printf("芯片: %s\n\n", flasher.getChipInfo().c_str());

        const uint32_t eepromBase = 0x00080000;

        // Read EEPROM in chunks
        printf("读取 EEPROM (32KB)...\n\n");

        std::vector<uint8_t> allData;
        for (uint32_t offset = 0; offset < 32768; offset += 56) {
            uint16_t chunkLen = 56;
            if (offset + chunkLen > 32768) chunkLen = 32768 - offset;

            auto packet = WCHPacketBuilder::dataRead(eepromBase + offset, chunkLen);
            auto raw = transport.transfer(packet);

            WCHResponse resp;
            if (!WCHResponse::parse(raw, resp) || !resp.ok) {
                printf("读取失败 at offset 0x%06X, status=0x%02X\n", offset, resp.status);
                break;
            }

            for (uint8_t b : resp.payload) allData.push_back(b);
        }

        printf("读取了 %zu 字节\n\n", allData.size());

        // Show first 256 bytes
        printf("EEPROM 前256字节:\n");
        for (size_t i = 0; i < 256 && i < allData.size(); ++i) {
            printf("%02X ", allData[i]);
            if ((i + 1) % 16 == 0) printf("\n");
        }
        printf("\n");

        // Count non-0xFF
        size_t nonFF = 0;
        for (uint8_t b : allData) if (b != 0xFF) ++nonFF;
        printf("非0xFF字节: %zu / %zu (%.1f%%)\n\n", nonFF, allData.size(),
               allData.empty() ? 0.0 : 100.0 * nonFF / allData.size());

        // Search for strings
        printf("字符串:\n");
        for (size_t i = 0; i < allData.size(); ++i) {
            if (allData[i] >= 32 && allData[i] < 127) {
                size_t start = i;
                while (i < allData.size() && allData[i] >= 32 && allData[i] < 127) ++i;
                if (i - start >= 3) {
                    printf("  +%05zX: ", start);
                    for (size_t j = start; j < i; ++j) printf("%c", allData[j]);
                    printf("\n");
                }
            }
        }

        transport.close();
    } catch (const std::exception& e) {
        fprintf(stderr, "错误: %s\n", e.what());
        return 1;
    }
    return 0;
}
