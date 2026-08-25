#include <QCoreApplication>
#include <QThread>
#include <cstdio>
#include <vector>
#include <chrono>
#include <thread>
#include "wch/WCHFlasher.h"
#include "wch/WCHUSBTransport.h"
#include "wch/WCHHexParser.h"

void printHex(const std::vector<uint8_t>& data, const char* label) {
    printf("%s (%zu bytes): ", label, data.size());
    for (size_t i = 0; i < data.size() && i < 32; ++i) {
        printf("%02X ", data[i]);
    }
    if (data.size() > 32) printf("...");
    printf("\n");
}

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);

    if (argc < 2) {
        fprintf(stderr, "用法: %s <固件文件.hex>\n", argv[0]);
        return 1;
    }

    const char* firmwarePath = argv[1];
    printf("=========================================\n");
    printf("详细烧录诊断工具\n");
    printf("=========================================\n");
    printf("固件文件: %s\n\n", firmwarePath);
    fflush(stdout);

    try {
        // 加载固件
        printf("【步骤1】加载固件文件\n");
        WCHHexParser parser;
        std::vector<uint8_t> firmwareData = parser.parseFile(firmwarePath);
        printf("✓ 固件大小: %zu 字节\n\n", firmwareData.size());
        fflush(stdout);

        // 打开设备
        printf("【步骤2】打开USB设备\n");
        WCHUSBTransport transport;
        auto devices = transport.scanDevices();
        if (devices.empty()) {
            fprintf(stderr, "✗ 未找到WCH ISP设备\n");
            return 1;
        }
        printf("✓ 找到 %zu 个设备\n", devices.size());
        transport.open(0);
        printf("✓ 设备已打开\n\n");
        fflush(stdout);

        // 创建Flasher
        printf("【步骤3】初始化烧录器\n");
        WCHFlasher flasher(&transport);

        std::string chipInfo = flasher.getChipInfo();
        printf("✓ 芯片信息:\n%s\n", chipInfo.c_str());

        bool isProtected = flasher.isCodeFlashProtected();
        printf("✓ Flash保护: %s\n\n", isProtected ? "已保护" : "未保护");
        fflush(stdout);

        // 检查固件大小
        uint32_t flashSize = 0;
        size_t pos = chipInfo.find("Code Flash:");
        if (pos != std::string::npos) {
            size_t start = chipInfo.find("(", pos) + 1;
            size_t end = chipInfo.find("KiB", start);
            if (start != std::string::npos && end != std::string::npos) {
                std::string sizeStr = chipInfo.substr(start, end - start);
                sizeStr.erase(std::remove(sizeStr.begin(), sizeStr.end(), ' '), sizeStr.end());
                flashSize = std::stoi(sizeStr) * 1024;
            }
        }

        printf("Flash大小: %u 字节 (%u KiB)\n", flashSize, flashSize / 1024);
        printf("固件大小: %zu 字节 (%zu KiB)\n", firmwareData.size(), firmwareData.size() / 1024);

        if (firmwareData.size() > flashSize) {
            fprintf(stderr, "\n✗✗✗ 警告: 固件太大! ✗✗✗\n");
            fprintf(stderr, "固件 (%zu 字节) > Flash (%u 字节)\n", firmwareData.size(), flashSize);
            fprintf(stderr, "超出 %zu 字节\n", firmwareData.size() - flashSize);
            fprintf(stderr, "这可能导致烧录失败或固件不完整!\n\n");
        }
        printf("\n");
        fflush(stdout);

        // 开始烧录
        printf("【步骤4】开始烧录流程\n");
        printf("=========================================\n");

        auto progressCallback = [](int percent, const std::string& message) {
            printf("[%3d%%] %s\n", percent, message.c_str());
            fflush(stdout);
        };

        // 使用flash()方法执行完整流程
        printf("\n--- 执行完整烧录流程 ---\n");
        flasher.flash(firmwareData, progressCallback);

        printf("\n=========================================\n");
        printf("烧录流程完成!\n");
        printf("=========================================\n");
        printf("\n请等待5-10秒让设备启动新固件\n");
        printf("然后检查:\n");
        printf("  1. 设备是否重新枚举\n");
        printf("  2. 串口是否出现 (/dev/ttyACM0)\n");
        printf("  3. GET_INFO命令是否响应\n");
        printf("\n");

        transport.close();

    } catch (const std::exception& e) {
        fprintf(stderr, "\n✗✗✗ 烧录失败! ✗✗✗\n");
        fprintf(stderr, "错误: %s\n", e.what());
        return 1;
    }

    return 0;
}
