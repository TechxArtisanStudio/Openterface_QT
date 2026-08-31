#include <QCoreApplication>
#include <QThread>
#include <cstdio>
#include <vector>
#include <chrono>
#include <thread>
#include "wch/WCHFlasher.h"
#include "wch/WCHUSBTransport.h"
#include "wch/WCHHexParser.h"

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);

    if (argc < 2) {
        fprintf(stderr, "用法: %s <固件文件.hex>\n", argv[0]);
        return 1;
    }

    const char* firmwarePath = argv[1];
    printf("=========================================\n");
    printf("WCH 固件烧录测试\n");
    printf("=========================================\n");
    printf("固件文件: %s\n\n", firmwarePath);
    fflush(stdout);

    try {
        // 加载固件
        printf("[1/5] 加载固件...\n");
        WCHHexParser parser;
        std::vector<uint8_t> firmwareData = parser.parseFile(firmwarePath);
        printf("  ✓ 固件大小: %zu 字节\n\n", firmwareData.size());
        fflush(stdout);

        // 打开设备
        printf("[2/5] 打开USB设备...\n");
        WCHUSBTransport transport;
        auto devices = transport.scanDevices();
        if (devices.empty()) {
            fprintf(stderr, "  ✗ 未找到WCH ISP设备\n");
            return 1;
        }
        printf("  ✓ 找到 %zu 个设备\n", devices.size());
        transport.open(0);
        printf("  ✓ 设备已打开\n\n");
        fflush(stdout);

        // 创建Flasher
        printf("[3/5] 识别芯片...\n");
        WCHFlasher flasher(&transport);
        std::string chipInfo = flasher.getChipInfo();
        printf("  ✓ %s\n", chipInfo.c_str());
        printf("  ✓ Flash保护: %s\n\n",
               flasher.isCodeFlashProtected() ? "已保护" : "未保护");
        fflush(stdout);

        // 烧录
        printf("[4/5] 开始烧录...\n");
        printf("-----------------------------------------\n");

        auto progressCallback = [](int percent, const std::string& message) {
            printf("  [%3d%%] %s\n", percent, message.c_str());
            fflush(stdout);
        };

        flasher.flash(firmwareData, progressCallback);

        printf("-----------------------------------------\n");
        printf("  ✓ 烧录完成!\n\n");

        printf("[5/5] 等待设备重新枚举...\n");
        transport.close();

        // 等待并检查设备重新枚举
        for (int i = 0; i < 10; ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            printf("  等待... %d秒\n", i + 1);
            fflush(stdout);
        }

        printf("\n=========================================\n");
        printf("✓✓✓ 烧录成功! ✓✓✓\n");
        printf("=========================================\n");
        printf("\n请检查:\n");
        printf("  1. lsusb 是否看到新设备\n");
        printf("  2. /dev/ttyACM0 是否出现\n");

    } catch (const std::exception& e) {
        fprintf(stderr, "\n✗✗✗ 失败! ✗✗✗\n");
        fprintf(stderr, "错误: %s\n", e.what());
        return 1;
    }

    return 0;
}
