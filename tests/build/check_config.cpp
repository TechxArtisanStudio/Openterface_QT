#include <QCoreApplication>
#include <cstdio>
#include <vector>
#include "wch/WCHUSBTransport.h"
#include "wch/WCHProtocol.h"

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

        // 读取完整配置 (CFG_MASK_ALL = 0x1F)
        auto packet = WCHPacketBuilder::readConfig(0x1F);
        auto raw = transport.transfer(packet);

        WCHResponse resp;
        if (!WCHResponse::parse(raw, resp) || !resp.ok) {
            fprintf(stderr, "ReadConfig 失败\n");
            return 1;
        }

        printf("=========================================\n");
        printf("CH32V208 配置寄存器分析\n");
        printf("=========================================\n\n");

        printf("原始配置数据 (%zu 字节):\n", resp.payload.size());
        for (size_t i = 0; i < resp.payload.size(); ++i) {
            printf("%02X ", resp.payload[i]);
            if ((i + 1) % 16 == 0) printf("\n");
        }
        printf("\n\n");

        // payload layout (after 2 status bytes):
        // [0]=RDPR [1]=nRDPR [2]=USER [3]=nUSER
        // [4]=DATA0 [5]=nDATA0 [6]=DATA1 [7]=nDATA1
        // [8..11]=WPR (4 bytes)
        // [12..15]=BTVER (4 bytes)
        // [16..]=UID

        if (resp.payload.size() >= 12) {
            uint8_t rdpr = resp.payload[2];
            uint8_t nrdpr = resp.payload[3];
            uint8_t user = resp.payload[4];
            uint8_t nuser = resp.payload[5];

            printf("RDPR:  0x%02X (nRDPR: 0x%02X)\n", rdpr, nrdpr);
            printf("  %s\n", rdpr == 0xA5 ? "未保护" : "已保护");

            printf("USER:  0x%02X (nUSER: 0x%02X)\n", user, nuser);
            printf("  IWDG_SW:     %d (%s)\n", (user >> 1) & 1,
                   ((user >> 1) & 1) ? "软件控制" : "硬件启用");
            printf("  STOP_RST:    %d (%s)\n", (user >> 2) & 1,
                   ((user >> 2) & 1) ? "禁用" : "启用");
            printf("  STANDBY_RST: %d (%s)\n", (user >> 3) & 1,
                   ((user >> 3) & 1) ? "禁用" : "启用");

            uint8_t sram_mode = (user >> 6) & 0x03;
            printf("  SRAM_CODE_MODE: 0b%02d = ", sram_mode);
            switch (sram_mode) {
                case 0b00: printf("CODE-192KB+RAM-128KB / CODE-128KB+RAM-64KB\n"); break;
                case 0b01: printf("CODE-224KB+RAM-96KB / CODE-144KB+RAM-48KB\n"); break;
                case 0b10: printf("CODE-256KB+RAM-64KB / CODE-160KB+RAM-32KB\n"); break;
                case 0b11: printf("CODE-228KB+RAM-32KB / CODE-160KB+RAM-32KB\n"); break;
            }

            printf("\nWPR:   0x%02X%02X%02X%02X\n",
                   resp.payload[10], resp.payload[11],
                   resp.payload[12], resp.payload[13]);
            printf("  %s\n",
                   (resp.payload[10]==0xFF && resp.payload[11]==0xFF &&
                    resp.payload[12]==0xFF && resp.payload[13]==0xFF) ?
                   "未保护" : "部分保护");
        }

        if (resp.payload.size() >= 18) {
            printf("\nBTVER: %d.%d.%d.%d\n",
                   resp.payload[14], resp.payload[15],
                   resp.payload[16], resp.payload[17]);
        }

        if (resp.payload.size() > 18) {
            printf("UID:   ");
            for (size_t i = 18; i < resp.payload.size(); ++i) {
                printf("%02X", resp.payload[i]);
                if (i < resp.payload.size() - 1) printf("-");
            }
            printf("\n");
        }

        transport.close();

    } catch (const std::exception& e) {
        fprintf(stderr, "错误: %s\n", e.what());
        return 1;
    }

    return 0;
}
