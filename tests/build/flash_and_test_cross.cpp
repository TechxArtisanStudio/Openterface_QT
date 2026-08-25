// Standalone flash + GET_INFO test — cross-platform (Linux/macOS/Windows)
// Usage: ./flash_and_test_cross [firmware.hex] [--port <serial_port>]
//
// Serial port is auto-detected:
//   Linux:   /dev/ttyACM0
//   macOS:   /dev/cu.usbmodem* (first match)
//   Windows: COM port matching VID=1A86 PID=FE0C, or COM3 fallback

#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <cstring>
#include <cstdio>
#include <algorithm>

#include "wch/WCHFlasher.h"
#include "wch/WCHUSBTransport.h"
#include "wch/WCHHexParser.h"

// ============================================================================
// Platform abstraction for serial port
// ============================================================================
#ifdef _WIN32
// ---- Windows: Win32 serial API ----
#include <windows.h>

struct SerialPort {
    HANDLE handle = INVALID_HANDLE_VALUE;

    bool open(const std::string& portName) {
        // Windows expects "\\.\COMx" for port names >= COM10, but it works
        // for all.  Also accept "COMx" without the prefix.
        std::string fullPath = portName;
        if (fullPath.rfind("\\\\.\\", 0) != 0 && fullPath.rfind("COM", 0) == 0) {
            fullPath = "\\\\.\\" + portName;
        }

        handle = CreateFileA(fullPath.c_str(),
                             GENERIC_READ | GENERIC_WRITE,
                             0, NULL, OPEN_EXISTING,
                             FILE_ATTRIBUTE_NORMAL, NULL);
        if (handle == INVALID_HANDLE_VALUE) {
            std::cerr << "Cannot open " << portName
                      << " (error " << GetLastError() << ")" << std::endl;
            return false;
        }

        // Configure: 115200 8N1, no flow control
        DCB dcb = {};
        dcb.DCBlength = sizeof(dcb);
        if (!GetCommState(handle, &dcb)) {
            std::cerr << "GetCommState failed" << std::endl;
            CloseHandle(handle);
            handle = INVALID_HANDLE_VALUE;
            return false;
        }
        dcb.BaudRate = CBR_115200;
        dcb.ByteSize = 8;
        dcb.Parity    = NOPARITY;
        dcb.StopBits  = ONESTOPBIT;
        dcb.fBinary   = TRUE;
        dcb.fParity   = FALSE;
        dcb.fOutxCtsFlow = FALSE;
        dcb.fOutxDsrFlow = FALSE;
        dcb.fDtrControl  = DTR_CONTROL_ENABLE;
        dcb.fRtsControl  = RTS_CONTROL_ENABLE;
        dcb.fOutX = FALSE;
        dcb.fInX  = FALSE;
        if (!SetCommState(handle, &dcb)) {
            std::cerr << "SetCommState failed" << std::endl;
            CloseHandle(handle);
            handle = INVALID_HANDLE_VALUE;
            return false;
        }

        // Timeouts: 2 second read timeout
        COMMTIMEOUTS timeouts = {};
        timeouts.ReadIntervalTimeout         = 0;
        timeouts.ReadTotalTimeoutMultiplier  = 0;
        timeouts.ReadTotalTimeoutConstant    = 2000;
        timeouts.WriteTotalTimeoutMultiplier = 0;
        timeouts.WriteTotalTimeoutConstant   = 1000;
        SetCommTimeouts(handle, &timeouts);

        PurgeComm(handle, PURGE_RXCLEAR | PURGE_TXCLEAR);
        return true;
    }

    void flush() {
        if (handle != INVALID_HANDLE_VALUE)
            PurgeComm(handle, PURGE_RXCLEAR | PURGE_TXCLEAR);
    }

    bool write(const uint8_t* data, size_t len) {
        DWORD written = 0;
        BOOL ok = WriteFile(handle, data, (DWORD)len, &written, NULL);
        return ok && written == (DWORD)len;
    }

    // Read with timeout.  Returns bytes read (0 on timeout).
    int read(uint8_t* buf, size_t bufSize, int timeoutMs) {
        // Update read timeout for this call
        COMMTIMEOUTS timeouts = {};
        timeouts.ReadTotalTimeoutConstant = timeoutMs;
        SetCommTimeouts(handle, &timeouts);

        DWORD bytesRead = 0;
        ReadFile(handle, buf, (DWORD)bufSize, &bytesRead, NULL);
        return (int)bytesRead;
    }

    void close() {
        if (handle != INVALID_HANDLE_VALUE) {
            CloseHandle(handle);
            handle = INVALID_HANDLE_VALUE;
        }
    }

    ~SerialPort() { close(); }
};

// Enumerate COM ports and find one matching VID=1A86 PID=FE0C
static std::string detectSerialPort() {
    // Use SetupDi to find CDC ACM devices
    HDEVINFO devInfo = SetupDiGetClassDevsA(
        NULL, NULL, NULL,
        DIGCF_PRESENT | DIGCF_ALLCLASSES);
    if (devInfo == INVALID_HANDLE_VALUE)
        return "COM3";

    SP_DEVINFO_DATA devData = {};
    devData.cbSize = sizeof(devData);

    for (DWORD i = 0; SetupDiEnumDeviceInfo(devInfo, i, &devData); i++) {
        // Get hardware ID (contains VID/PID)
        char hwId[512] = {};
        if (!SetupDiGetDeviceRegistryPropertyA(
                devInfo, &devData, SPDRP_HARDWAREID,
                NULL, (PBYTE)hwId, sizeof(hwId), NULL))
            continue;

        // Check for VID_1A86&PID_FE0C
        std::string hw(hwId);
        std::string upper = hw;
        std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
        if (upper.find("VID_1A86") == std::string::npos ||
            upper.find("PID_FE0C") == std::string::npos)
            continue;

        // Get friendly name (e.g. "USB Serial Device (COM3)")
        char name[512] = {};
        if (!SetupDiGetDeviceRegistryPropertyA(
                devInfo, &devData, SPDRP_FRIENDLYNAME,
                NULL, (PBYTE)name, sizeof(name), NULL))
            continue;

        // Extract COM port number from name
        std::string fname(name);
        auto pos = fname.find("COM");
        if (pos != std::string::npos) {
            std::string port = fname.substr(pos);
            auto end = port.find(')');
            if (end != std::string::npos)
                port = port.substr(0, end);
            std::cout << "Auto-detected serial port: " << port << std::endl;
            SetupDiDestroyDeviceInfoList(devInfo);
            return port;
        }
    }

    SetupDiDestroyDeviceInfoList(devInfo);
    std::cout << "Could not auto-detect serial port, using COM3" << std::endl;
    return "COM3";
}

#else
// ---- POSIX (Linux / macOS): termios ----
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <sys/select.h>

struct SerialPort {
    int fd = -1;

    bool open(const std::string& portName) {
        fd = ::open(portName.c_str(), O_RDWR | O_NOCTTY);
        if (fd < 0) {
            std::cerr << "Cannot open " << portName << std::endl;
            return false;
        }

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
        attrs.c_cc[VMIN]  = 0;
        attrs.c_cc[VTIME] = 20;  // 2 second timeout
        tcsetattr(fd, TCSANOW, &attrs);
        tcflush(fd, TCIOFLUSH);
        return true;
    }

    void flush() {
        if (fd >= 0) tcflush(fd, TCIOFLUSH);
    }

    bool write(const uint8_t* data, size_t len) {
        return ::write(fd, data, len) == (ssize_t)len;
    }

    int read(uint8_t* buf, size_t bufSize, int timeoutMs) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(fd, &readfds);
        struct timeval tv;
        tv.tv_sec  = timeoutMs / 1000;
        tv.tv_usec = (timeoutMs % 1000) * 1000;

        int ready = select(fd + 1, &readfds, NULL, NULL, &tv);
        if (ready > 0) {
            int n = ::read(fd, buf, bufSize);
            return n > 0 ? n : 0;
        }
        return 0;  // timeout
    }

    void close() {
        if (fd >= 0) {
            ::close(fd);
            fd = -1;
        }
    }

    ~SerialPort() { close(); }
};

static std::string detectSerialPort() {
#ifdef __linux__
    return "/dev/ttyACM0";
#else  // macOS
    // Look for usbmodem device
    if (access("/dev/cu.usbmodem1", F_OK) == 0)
        return "/dev/cu.usbmodem1";
    return "/dev/cu.usbmodem0";
#endif
}
#endif  // _WIN32

// ============================================================================
// Protocol helpers (platform-independent)
// ============================================================================

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

static void printHex(const std::vector<uint8_t>& data) {
    for (auto b : data) printf("%02X ", b);
}

static void printHex(const uint8_t* data, int len) {
    for (int i = 0; i < len; i++) printf("%02X ", data[i]);
}

// ============================================================================
// GET_INFO test
// ============================================================================

static bool testGetInfo(const std::string& portName) {
    SerialPort serial;
    if (!serial.open(portName)) {
        return false;
    }

    std::cout << "Serial port opened: " << portName << " @ 115200 8N1" << std::endl;

    // Try GET_INFO 3 times
    for (int attempt = 0; attempt < 3; attempt++) {
        serial.flush();

        auto cmd = buildGetInfoWithChecksum();
        std::cout << "\nAttempt " << (attempt + 1) << ": sending GET_INFO (";
        printHex(cmd);
        std::cout << ")" << std::endl;

        serial.write(cmd.data(), cmd.size());

        // Wait for response (2 second timeout)
        uint8_t resp[64];
        int n = serial.read(resp, sizeof(resp), 2000);

        if (n > 0) {
            std::cout << "Response (" << n << " bytes): ";
            printHex(resp, n);
            std::cout << std::endl;

            if (n >= 8 && resp[0] == 0x57 && resp[1] == 0xAB) {
                std::cout << "GET_INFO responded successfully!" << std::endl;
                std::cout << "  Header: 57 AB" << std::endl;
                printf("  Cmd: 0x%02X\n", resp[3]);
                serial.close();
                return true;
            }
        } else {
            std::cout << "  Timeout - no response" << std::endl;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    serial.close();
    return false;
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char* argv[])
{
    std::string firmwarePath;
    std::string portOverride;

    // Parse arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) {
            portOverride = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: " << argv[0] << " [firmware.hex] [--port <serial_port>]" << std::endl;
            std::cout << "  firmware.hex   Path to Intel HEX firmware file" << std::endl;
            std::cout << "  --port         Serial port name (e.g. COM3, /dev/ttyACM0)" << std::endl;
            return 0;
        } else if (firmwarePath.empty()) {
            firmwarePath = arg;
        }
    }

    // Default firmware path (platform-specific)
    if (firmwarePath.empty()) {
#ifdef _WIN32
        firmwarePath = "C:\\firmware.hex";
#else
        firmwarePath = "/home/bot/project/06(1).hex";
#endif
    }

    std::cout << "=== Flash + GET_INFO Test (Cross-Platform) ===" << std::endl;
    std::cout << "Firmware: " << firmwarePath << std::endl;
#ifdef _WIN32
    std::cout << "Platform: Windows" << std::endl;
#elif defined(__APPLE__)
    std::cout << "Platform: macOS" << std::endl;
#else
    std::cout << "Platform: Linux" << std::endl;
#endif

    // === STEP 1: Flash ===
    try {
        WCHUSBTransport transport;
        auto devices = transport.scanDevices();
        if (devices.empty()) {
            std::cerr << "ERROR: No ISP device found!" << std::endl;
            return 1;
        }
        std::cout << "Found " << devices.size() << " ISP device(s)" << std::endl;
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

    // Determine serial port
    std::string serialPort = portOverride.empty() ? detectSerialPort() : portOverride;
    std::cout << "Using serial port: " << serialPort << std::endl;

    // === STEP 3: Test GET_INFO ===
    std::cout << "\n=== Testing GET_INFO ===" << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(2));

    if (testGetInfo(serialPort)) {
        std::cout << "\n=== ALL TESTS PASSED ===" << std::endl;
        return 0;
    } else {
        std::cout << "\n=== GET_INFO FAILED ===" << std::endl;
        return 1;
    }
}
