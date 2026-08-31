#include <QCoreApplication>
#include <QThread>
#include <cstdio>
#include "wch/WCHFlasher.h"
#include "wch/WCHUSBTransport.h"
#include "wch/WCHHexParser.h"

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);

    printf("=========================================\n");
    printf("Flash保护诊断工具\n");
    printf("=========================================\n\n");
    fflush(stdout);

    try {
        // 打开USB传输层
        WCHUSBTransport transport;
        auto devices = transport.scanDevices();

        if (devices.empty()) {
            fprintf(stderr, "✗ 未找到WCH ISP设备\n");
            return 1;
        }

        printf("✓ 找到 %zu 个设备\n", devices.size());
        transport.open(0);
        printf("✓ 已打开设备\n\n");

        // 创建Flasher
        WCHFlasher flasher(&transport);

        // 获取芯片信息
        std::string chipInfo = flasher.getChipInfo();
        printf("=== 芯片信息 ===\n");
        printf("%s\n", chipInfo.c_str());

        // 检查保护状态
        bool isProtected = flasher.isCodeFlashProtected();
        printf("\n=== Flash保护状态 ===\n");
        printf("保护状态: %s\n", isProtected ? "⚠️  已保护" : "✓ 未保护");

        if (isProtected) {
            printf("\n=== 尝试解除保护 ===\n");
            printf("这将写入配置并重置设备...\n");

            try {
                flasher.unprotect();
                printf("✓ 解除保护命令已发送\n");
                printf("设备正在重置,请等待...\n");
                QThread::msleep(3000);

                // 重新连接
                printf("\n=== 重新连接设备 ===\n");
                transport.close();

                bool reconnected = false;
                for (int attempt = 0; attempt < 5; ++attempt) {
                    printf("尝试 %d/5...\n", attempt + 1);
                    QThread::msleep(2000);

                    auto devs = transport.scanDevices();
                    if (!devs.empty()) {
                        transport.open(0);
                        WCHFlasher newFlasher(&transport);
                        bool stillProtected = newFlasher.isCodeFlashProtected();
                        printf("保护状态: %s\n", stillProtected ? "仍已保护" : "✓ 已解除");

                        if (!stillProtected) {
                            reconnected = true;
                            break;
                        }
                    }
                }

                if (!reconnected) {
                    fprintf(stderr, "✗ 重连失败或保护未解除\n");
                    return 1;
                }
            } catch (const std::exception& e) {
                fprintf(stderr, "✗ 解除保护失败: %s\n", e.what());
                return 1;
            }
        }

        printf("\n=========================================\n");
        printf("✓ 诊断完成\n");
        printf("=========================================\n");

    } catch (const std::exception& e) {
        fprintf(stderr, "\n✗ 错误: %s\n", e.what());
        return 1;
    }

    return 0;
}
