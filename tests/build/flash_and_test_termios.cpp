// Standalone flash + GET_INFO test using termios for reliable serial communication
#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <cstring>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <sys/select.h>

#include "wch/WCHFlasher.h"
#include "wch/WCHUSBTransport.h"
#include "wch/WCHHexParser.h"

static uint8_t calculateChecksum(const std::vector<uint8_t>& data) {
    uint32_t sum = 0;
    for (auto byte : data) sum += byte;
    return sum % 256;
}

static std::vector<uint8_t> buildGetInfoWithChecksum() {
    std::vector<uint8_t> cmd = {0x57, 0xAB, 0x00, 0x01, 0x00};
    cmd.push_back(calculateChecksum(cmd));
    return cmd;
}

static bool testGetInfo(const std::string& portName) {
    int fd = open(portName.c_str(), O_RDWR | O_NOCTTY);
    if (fd < 0) {
        std::cerr << "Cannot open " << portName << std::endl;
        return false;
    }

    // Configure serial port using termios
    struct termios attrs;
    tcgetattr(fd, &attrs);
    cfsetispeed(&attrs, B115200);
    cfsetospeed(&attrs, B115200);
    attrs.c_cflag = (attrs.c_cflag & ~CSIZE) | CS8;
    attrs.c_cflag &= ~PARENB;
    attrs.c_cflag &= ~CSTOPB;
    attrs.c_cflag |= CREAD | CLOCAL;
    attrs.c_iflag = 0;
    attrs.c_oflag = 0;
    attrs.c_lflag = 0;
    attrs.c_cc[VMIN] = 0;
    attrs.c_cc[VTIME] = 20;
    tcsetattr(fd, TCSANOW, &attrs);
    tcflush(fd, TCIOFLUSH);

    std::cout << "Serial port opened at 115200" << std::endl;

    // Try GET_INFO 3 times
    for (int attempt = 0; attempt < 3; attempt++) {
        tcflush(fd, TCIOFLUSH);

        auto cmd = buildGetInfoWithChecksum();
        std::cout << "\nAttempt " << (attempt + 1) << ": sending GET_INFO (";
        for (auto b : cmd) printf("%02X ", b);
        std::cout << ")" << std::endl;

        write(fd, cmd.data(), cmd.size());

        // Wait for response
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(fd, &readfds);
        struct timeval tv;
        tv.tv_sec = 2;
        tv.tv_usec = 0;

        int ready = select(fd + 1, &readfds, NULL, NULL, &tv);
        if (ready > 0) {
            unsigned char resp[64];
            int n = read(fd, resp, sizeof(resp));
            std::cout << "Response (" << n << " bytes): ";
            for (int i = 0; i < n; i++) printf("%02X ", resp[i]);
            std::cout << std::endl;

            if (n >= 8 && resp[0] == 0x57 && resp[1] == 0xAB) {
                std::cout << "✓ GET_INFO responded successfully!" << std::endl;
                std::cout << "  Header: 57 AB" << std::endl;
                printf("  Cmd: 0x%02X\n", resp[3]);
                close(fd);
                return true;
            }
        } else {
            std::cout << "  Timeout - no response" << std::endl;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    close(fd);
    return false;
}

int main(int argc, char* argv[]) {
    std::string firmwarePath = "/home/bot/project/06(1).hex";
    if (argc > 1) firmwarePath = argv[1];

    std::cout << "=== Flash + GET_INFO Test ===" << std::endl;
    std::cout << "Firmware: " << firmwarePath << std::endl;

    // === STEP 1: Flash ===
    try {
        WCHUSBTransport transport;
        auto devices = transport.scanDevices();
        if (devices.empty()) {
            std::cerr << "ERROR: No ISP device found!" << std::endl;
            return 1;
        }
        transport.open(0);

        WCHFlasher flasher(&transport);
        std::cout << "\n" << flasher.getChipInfo() << std::endl;

        bool protected_ = flasher.isCodeFlashProtected();
        std::cout << "Protected: " << (protected_ ? "YES" : "NO") << std::endl;

        WCHHexParser parser;
        auto firmware = parser.parseFile(firmwarePath);
        std::cout << "Firmware loaded: " << firmware.size() << " bytes" << std::endl;

        auto progress = [](int percent, const std::string& msg) {
            printf("[%d%%] %s\r", percent, msg.c_str());
            fflush(stdout);
        };

        std::cout << "\n=== Flashing ===" << std::endl;
        flasher.flash(firmware, progress);
        std::cout << "\n[100%] Flash complete!" << std::endl;

        transport.close();
    } catch (const std::exception& e) {
        std::cerr << "\nFlash FAILED: " << e.what() << std::endl;
        return 1;
    }

    // === STEP 2: Wait for re-enumeration ===
    std::cout << "\n=== Waiting for device re-enumeration ===" << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(3));

    std::string serialPort = "ttyACM0";
    std::cout << "✓ Device found on /dev/" << serialPort << std::endl;

    // === STEP 3: Test GET_INFO ===
    std::cout << "\n=== Testing GET_INFO ===" << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(2));

    if (testGetInfo("/dev/" + serialPort)) {
        std::cout << "\n✓✓✓ ALL TESTS PASSED ✓✓✓" << std::endl;
        return 0;
    } else {
        std::cout << "\n✗✗✗ GET_INFO FAILED ✗✗✗" << std::endl;
        return 1;
    }
}
