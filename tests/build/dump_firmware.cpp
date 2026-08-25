#include <cstdio>
#include <vector>
#include <cstdint>
#include "wch/WCHHexParser.h"

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <firmware.hex>\n", argv[0]);
        return 1;
    }

    WCHHexParser parser;
    auto fw = parser.parseFile(argv[1]);

    printf("Firmware size: %zu bytes\n", fw.size());

    // Show first 32 bytes (reset vector area)
    printf("\nFirst 32 bytes (reset vector):\n");
    for (int i = 0; i < 32 && i < (int)fw.size(); ++i) {
        printf("%02X ", fw[i]);
        if ((i + 1) % 16 == 0) printf("\n");
    }
    printf("\n");

    // Show bytes around segment boundaries
    printf("\nAround 0x0FFF0-0x10010 (segment 0→1 boundary):\n");
    for (uint32_t addr = 0x0FFF0; addr <= 0x10010 && addr < fw.size(); ++addr) {
        if (addr == 0x10000) printf("--- segment boundary at 0x10000 ---\n");
        printf("  [%06X] = %02X\n", addr, fw[addr]);
    }

    printf("\nAround 0x1FFF0-0x20010 (segment 1→2 boundary):\n");
    for (uint32_t addr = 0x1FFF0; addr <= 0x20010 && addr < fw.size(); ++addr) {
        if (addr == 0x20000) printf("--- segment boundary at 0x20000 ---\n");
        printf("  [%06X] = %02X\n", addr, fw[addr]);
    }

    // Count non-0xFF bytes
    size_t nonFF = 0;
    for (uint8_t b : fw) if (b != 0xFF) ++nonFF;
    printf("\nNon-0xFF bytes: %zu / %zu (%.1f%%)\n", nonFF, fw.size(), 100.0 * nonFF / fw.size());

    // Show last 16 bytes
    printf("\nLast 16 bytes:\n");
    size_t start = fw.size() >= 16 ? fw.size() - 16 : 0;
    for (size_t i = start; i < fw.size(); ++i) {
        printf("  [%06zX] = %02X\n", i, fw[i]);
    }

    // After padding to 1024 boundary
    size_t padded = ((fw.size() + 1023) / 1024) * 1024;
    printf("\nPadded size: %zu (added %zu bytes)\n", padded, padded - fw.size());

    return 0;
}
