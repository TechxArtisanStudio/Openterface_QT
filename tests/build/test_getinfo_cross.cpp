// Standalone GET_INFO test — cross-platform (Linux/macOS/Windows)
// Usage: ./test_getinfo_cross [--port <serial_port>]
//
// Tests CDC ACM GET_INFO command without flashing.
// Serial port is auto-detected or specified with --port.

#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <cstring>
#include <cstdio>
#include <algorithm>

// ============================================================================
// Platform abstraction for serial port
// ============================================================================
#ifdef _WIN32
#include <windows.h>

struct SerialPort {
    HANDLE handle = INVALID_HANDLE_VALUE;

    bool open(const std::string& portName) {
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

        DCB dcb = {};
        dcb.DCBlength = sizeof(dcb);
        GetCommState(handle, &dcb);
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
        SetCommState(handle, &dcb);

        COMMTIMEOUTS timeouts = {};
        timeouts.ReadTotalTimeoutConstant    = 2000;
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
        return WriteFile(handle, data, (DWORD)len, &written, NULL) && written == (DWORD)len;
    }

    int read(uint8_t* buf, size_t bufSize, int timeoutMs) {
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

static std::string detectSerialPort() {
    HDEVINFO devInfo = SetupDiGetClassDevsA(
        NULL, NULL, NULL, DIGCF_PRESENT | DIGCF_ALLCLASSES);
    if (devInfo == INVALID_HANDLE_VALUE) return "COM3";

    SP_DEVINFO_DATA devData = {};
    devData.cbSize = sizeof(devData);

    for (DWORD i = 0; SetupDiEnumDeviceInfo(devInfo, i, &devData); i++) {
        char hwId[512] = {};
        if (!SetupDiGetDeviceRegistryPropertyA(
                devInfo, &devData, SPDRP_HARDWAREID,
                NULL, (PBYTE)hwId, sizeof(hwId), NULL))
            continue;

        std::string upper(hwId);
        std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
        if (upper.find("VID_1A86") == std::string::npos ||
            upper.find("PID_FE0C") == std::string::npos)
            continue;

        char name[512] = {};
        if (!SetupDiGetDeviceRegistryPropertyA(
                devInfo, &devData, SPDRP_FRIENDLYNAME,
                NULL, (PBYTE)name, sizeof(name), NULL))
            continue;

        std::string fname(name);
        auto pos = fname.find("COM");
        if (pos != std::string::npos) {
            std::string port = fname.substr(pos);
            auto end = port.find(')');
            if (end != std::string::npos) port = port.substr(0, end);
            std::cout << "Auto-detected: " << port << std::endl;
            SetupDiDestroyDeviceInfoList(devInfo);
            return port;
        }
    }
    SetupDiDestroyDeviceInfoList(devInfo);
    return "COM3";
}

#else
// POSIX (Linux / macOS)
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
        attrs.c_cc[VTIME] = 20;
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
        return 0;
    }

    void close() {
        if (fd >= 0) { ::close(fd); fd = -1; }
    }

    ~SerialPort() { close(); }
};

static std::string detectSerialPort() {
#ifdef __linux__
    return "/dev/ttyACM0";
#else
    if (access("/dev/cu.usbmodem1", F_OK) == 0) return "/dev/cu.usbmodem1";
    return "/dev/cu.usbmodem0";
#endif
}
#endif  // _WIN32

// ============================================================================
// Protocol
// ============================================================================

static uint8_t calculateChecksum(const std::vector<uint8_t>& data) {
    uint32_t sum = 0;
    for (auto b : data) sum += b;
    return sum % 256;
}

static std::vector<uint8_t> buildGetInfoWithChecksum() {
    std::vector<uint8_t> cmd = {0x57, 0xAB, 0x00, 0x01, 0x00};
    cmd.push_back(calculateChecksum(cmd));
    return cmd;
}

static void printHex(const uint8_t* data, int len) {
    for (int i = 0; i < len; i++) printf("%02X ", data[i]);
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char* argv[])
{
    std::string portOverride;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) {
            portOverride = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: " << argv[0] << " [--port <serial_port>]" << std::endl;
            std::cout << "  --port  Serial port (e.g. COM3, /dev/ttyACM0)" << std::endl;
            return 0;
        }
    }

    std::string serialPort = portOverride.empty() ? detectSerialPort() : portOverride;

    std::cout << "=== GET_INFO Test (Cross-Platform) ===" << std::endl;
    std::cout << "Port: " << serialPort << std::endl;
#ifdef _WIN32
    std::cout << "Platform: Windows" << std::endl;
#elif defined(__APPLE__)
    std::cout << "Platform: macOS" << std::endl;
#else
    std::cout << "Platform: Linux" << std::endl;
#endif

    SerialPort serial;
    if (!serial.open(serialPort)) {
        return 1;
    }
    std::cout << "Opened at 115200 8N1" << std::endl;

    // Try 3 times
    bool success = false;
    for (int attempt = 0; attempt < 3; attempt++) {
        serial.flush();

        auto cmd = buildGetInfoWithChecksum();
        std::cout << "\nAttempt " << (attempt + 1) << ": sending GET_INFO (";
        for (auto b : cmd) printf("%02X ", b);
        std::cout << ")" << std::endl;

        serial.write(cmd.data(), cmd.size());

        uint8_t resp[64];
        int n = serial.read(resp, sizeof(resp), 2000);

        if (n > 0) {
            std::cout << "Response (" << n << " bytes): ";
            printHex(resp, n);
            std::cout << std::endl;

            if (n >= 8 && resp[0] == 0x57 && resp[1] == 0xAB) {
                std::cout << "\n=== GET_INFO SUCCESS ===" << std::endl;
                std::cout << "  Header: 57 AB" << std::endl;
                printf("  Cmd: 0x%02X\n", resp[3]);
                success = true;
                break;
            }
        } else {
            std::cout << "  Timeout - no response" << std::endl;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    serial.close();

    if (!success) {
        std::cout << "\n=== GET_INFO FAILED ===" << std::endl;
        return 1;
    }
    return 0;
}
