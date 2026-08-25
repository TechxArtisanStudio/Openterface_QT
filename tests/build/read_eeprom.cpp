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

        // Read data flash (EEPROM) - 32KB at address 0x00080000
        // Use dataRead command: [cmd=0xAB][len=6][addr:u32LE][length:u16LE]
        printf("读取 Data Flash (EEPROM) ...\n");

        const uint32_t eepromBase = 0x00080000;
        const uint16_t readLen = 256;

        auto packet = WCHPacketBuilder::dataRead(eepromBase, readLen);
        auto raw = transport.transfer(packet);

        WCHResponse resp;
        if (!WCHResponse::parse(raw, resp)) {
            printf("解析失败, raw size=%zu\n", raw.size());
            printf("Raw: ");
            for (size_t i = 0; i < raw.size() && i < 32; ++i)
                printf("%02X ", raw[i]);
            printf("\n");
        } else {
            printf("Status: 0x%02X, payload size: %zu\n", resp.status, resp.payload.size());
            if (resp.ok && !resp.payload.empty()) {
                printf("EEPROM 前 %zu 字节:\n", resp.payload.size());
                for (size_t i = 0; i < resp.payload.size(); ++i) {
                    printf("%02X ", resp.payload[i]);
                    if ((i + 1) % 16 == 0) printf("\n");
                }
                printf("\n");

                // Check if all 0xFF (erased)
                bool allFF = true;
                for (uint8_t b : resp.payload) if (b != 0xFF) { allFF = false; break; }
                printf("EEPROM 状态: %s\n", allFF ? "全部擦除 (0xFF)" : "有数据");

                // Search for strings
                for (size_t i = 0; i + 4 < resp.payload.size(); ++i) {
                    if (resp.payload[i] >= 32 && resp.payload[i] < 127) {
                        size_t start = i;
                        while (i < resp.payload.size() && resp.payload[i] >= 32 && resp.payload[i] < 127) ++i;
                        if (i - start >= 3) {
                            printf("  字符串 at +%zu: ", start);
                            for (size_t j = start; j < i; ++j)
                                printf("%c", resp.payload[j]);
                            printf("\n");
                        }
                    }
                }
            } else {
                printf("DataRead 失败, status=0x%02X\n", resp.status);
            }
        }

        transport.close();
    } catch (const std::exception& e) {
        fprintf(stderr, "错误: %s\n", e.what());
        return 1;
    }
    return 0;
}
