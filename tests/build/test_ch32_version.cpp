// Test CH32 firmware version reading via WCH ISP or Serial
#include <iostream>
#include <string>
#include "wch/WCHFlasher.h"
#include "wch/WCHUSBTransport.h"
#include "log/opflogging.h"

OPF_LOGGING_CATEGORY(log_core_serial, "opf.core.serial")

int main(int argc, char* argv[])
{
    std::cout << "=== CH32 Firmware Version Test ===" << std::endl;

    try {
        // Step 1: Create transport and scan for devices
        std::cout << "\n[1] Scanning for WCH ISP devices..." << std::endl;
        WCHUSBTransport transport;
        auto devices = transport.scanDevices();

        if (devices.empty()) {
            std::cerr << "WARNING: No WCH ISP devices found!" << std::endl;
            std::cerr << "Please connect device in ISP mode (hold BOOT button while plugging in)" << std::endl;
            std::cerr << "ISP mode VID:PID should be 1A86:55E0 or 4348:55E0" << std::endl;
            return 1;
        }

        std::cout << "Found " << devices.size() << " device(s):" << std::endl;
        for (size_t i = 0; i < devices.size(); ++i) {
            std::cout << "  [" << i << "] " << devices[i] << std::endl;
        }

        // Step 2: Open first device
        std::cout << "\n[2] Opening device 0..." << std::endl;
        transport.open(0);
        std::cout << "Device opened successfully." << std::endl;

        // Step 3: Create flasher and identify chip
        std::cout << "\n[3] Identifying chip..." << std::endl;
        WCHFlasher flasher(&transport);

        std::string chipInfo = flasher.getChipInfo();
        std::cout << "Chip Info:" << std::endl;
        std::cout << chipInfo << std::endl;

        // Step 4: Read bootloader version
        std::cout << "\n[4] Reading bootloader version..." << std::endl;
        std::string btver = flasher.bootloaderVersion();
        std::cout << "Bootloader Version: " << btver << std::endl;

        // Step 5: Try to read OTP area
        std::cout << "\n[5] Attempting to read OTP area..." << std::endl;
        try {
            std::string otpVer = flasher.readOTPVersion(0, 16);
            std::cout << "OTP Data (hex): " << otpVer << std::endl;
        } catch (const std::exception& e) {
            std::cout << "OTP read failed: " << e.what() << std::endl;
        }

        // Step 6: Close device
        std::cout << "\n[6] Closing device..." << std::endl;
        transport.close();

        std::cout << "\n=== Test Complete ===" << std::endl;
        std::cout << "SUCCESS: Firmware version = " << btver << std::endl;
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "\nERROR: " << e.what() << std::endl;
        return 1;
    }
}
