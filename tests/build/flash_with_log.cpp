#include <QCoreApplication>
#include <QThread>
#include <cstdio>
#include <vector>
#include <algorithm>
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
    printf("固件烧录工具 (详细日志)\n");
    printf("=========================================\n");
    printf("固件文件: %s\n\n", firmwarePath);
    fflush(stdout);

    try {
        // 加载固件
        printf("步骤1: 加载固件文件...\n");
        WCHHexParser parser;
        std::vector<uint8_t> firmwareData = parser.parseFile(firmwarePath);
        printf("✓ 固件大小: %zu 字节\n\n", firmwareData.size());

        // 打开设备
        printf("步骤2: 打开USB设备...\n");
        WCHUSBTransport transport;
        auto devices = transport.scanDevices();
        if (devices.empty()) {
            fprintf(stderr, "✗ 未找到WCH ISP设备\n");
            return 1;
        }
        printf("✓ 找到 %zu 个设备\n", devices.size());
        transport.open(0);
        printf("✓ 设备已打开\n\n");

        // 创建Flasher
        printf("步骤3: 初始化烧录器...\n");
        WCHFlasher flasher(&transport);

        std::string chipInfo = flasher.getChipInfo();
        printf("✓ 芯片信息:\n%s\n", chipInfo.c_str());

        bool isProtected = flasher.isCodeFlashProtected();
        printf("✓ Flash保护: %s\n", isProtected ? "已保护" : "未保护");

        // 检查固件大小
        uint32_t flashSize = 0;
        // 从chipInfo中提取Flash大小
        size_t pos = chipInfo.find("Code Flash:");
        if (pos != std::string::npos) {
            size_t start = chipInfo.find("(", pos) + 1;
            size_t end = chipInfo.find("KiB", start);
            if (start != std::string::npos && end != std::string::npos) {
                std::string sizeStr = chipInfo.substr(start, end - start);
                // 移除空格
                sizeStr.erase(std::remove(sizeStr.begin(), sizeStr.end(), ' '), sizeStr.end());
                flashSize = std::stoi(sizeStr) * 1024;
            }
        }

        printf("✓ Flash大小: %u 字节 (%u KiB)\n", flashSize, flashSize / 1024);
        printf("✓ 固件大小: %zu 字节 (%zu KiB)\n", firmwareData.size(), firmwareData.size() / 1024);

        if (firmwareData.size() > flashSize) {
            fprintf(stderr, "\n✗✗✗ 错误: 固件太大! ✗✗✗\n");
            fprintf(stderr, "固件大小 (%zu 字节) 超过 Flash 大小 (%u 字节)\n",
                    firmwareData.size(), flashSize);
            fprintf(stderr, "超出 %zu 字节\n", firmwareData.size() - flashSize);
            fprintf(stderr, "\n请使用正确大小的固件文件。\n");
            transport.close();
            return 1;
        }
        printf("✓ 固件大小检查通过\n\n");

        // 烧录固件
        printf("步骤4: 开始烧录...\n");
        printf("-----------------------------------------\n");

        auto progressCallback = [](int percent, const std::string& message) {
            printf("[%3d%%] %s\n", percent, message.c_str());
            fflush(stdout);
        };

        flasher.flash(firmwareData, progressCallback);

        printf("-----------------------------------------\n");
        printf("✓ 烧录完成!\n\n");

        printf("步骤5: 设备将自动重置并启动新固件\n");
        printf("请等待5-10秒,然后使用GET_INFO命令验证\n");

        transport.close();

        printf("\n=========================================\n");
        printf("✓✓✓ 烧录成功完成! ✓✓✓\n");
        printf("=========================================\n");

    } catch (const std::exception& e) {
        fprintf(stderr, "\n✗✗✗ 烧录失败! ✗✗✗\n");
        fprintf(stderr, "错误: %s\n", e.what());
        return 1;
    }

    return 0;
}
