#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <stdexcept>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#else
// Forward-declare libusb opaque types (avoids pulling in libusb.h here)
struct libusb_context;
struct libusb_device_handle;
#endif

// ---------------------------------------------------------------------------
// WCHTransportError
// ---------------------------------------------------------------------------
class WCHTransportError : public std::runtime_error {
public:
    explicit WCHTransportError(const std::string& msg) : std::runtime_error(msg) {}
};

// ---------------------------------------------------------------------------
// WCHUSBTransport
//
// Windows  : uses CH375DLL64.DLL / CH375DLL.DLL loaded dynamically.
//            Works with the existing CH375_A64 driver shipped by WCH tools.
//            No Zadig / WinUSB driver change required.
// Linux/Mac: uses libusb-1.0 (user-space, no extra driver needed).
//
// Target device: VID 0x4348 or 0x1A86, PID 0x55E0
// Bulk EP OUT 0x02, EP IN 0x82
// ---------------------------------------------------------------------------
class WCHUSBTransport {
public:
    WCHUSBTransport();
    ~WCHUSBTransport();

    WCHUSBTransport(const WCHUSBTransport&)            = delete;
    WCHUSBTransport& operator=(const WCHUSBTransport&) = delete;

    // Scan for WCH ISP devices; returns human-readable device strings.
    std::vector<std::string> scanDevices();

    // Open device by index from the last scanDevices() call.
    void open(int deviceIndex = 0);

    void close();
    bool isOpen() const;

    // Send ISP packet and receive response (bulk write + bulk read).
    std::vector<uint8_t> transfer(const std::vector<uint8_t>& command);

private:
    // -----------------------------------------------------------------------
    // Platform-specific members
    // -----------------------------------------------------------------------
#ifdef _WIN32
    HMODULE m_dllModule = nullptr;
    int     m_openIndex = -1;       // CH375 device index currently open (-1 = none)

    // CH375DLL function pointer types (WINAPI = __stdcall / default on x64)
    using FnCH375OpenDevice      = HANDLE (WINAPI*)(ULONG iIndex);
    using FnCH375CloseDevice     = VOID   (WINAPI*)(ULONG iIndex);
    using FnCH375SetTimeout      = VOID   (WINAPI*)(ULONG iIndex,
                                                    ULONG iWriteTimeoutMS,
                                                    ULONG iReadTimeoutMS);
    // Generic data transfer (uses pre-configured default endpoints)
    using FnCH375WriteData       = BOOL   (WINAPI*)(ULONG iIndex,
                                                    PVOID iBuffer, PULONG ioLength);
    using FnCH375ReadData        = BOOL   (WINAPI*)(ULONG iIndex,
                                                    PVOID oBuffer, PULONG ioLength);
    // Endpoint-specific transfer (preferred — explicitly targets EP 0x02 / 0x82)
    using FnCH375WriteEndP       = BOOL   (WINAPI*)(ULONG iIndex, UCHAR iEndPoint,
                                                    PVOID iBuffer, PULONG ioLength);
    using FnCH375ReadEndP        = BOOL   (WINAPI*)(ULONG iIndex, UCHAR iEndPoint,
                                                    PVOID oBuffer, PULONG ioLength);
    // Endpoint configuration for WriteData/ReadData (optional)
    using FnCH375SetEndpointTx   = BOOL   (WINAPI*)(ULONG iIndex, UCHAR iEndPoint);
    using FnCH375SetEndpointRx   = BOOL   (WINAPI*)(ULONG iIndex, UCHAR iEndPoint);
    using FnCH375GetName         = BOOL   (WINAPI*)(ULONG iIndex, PCHAR oDevName);

    FnCH375OpenDevice    m_fnOpen      = nullptr;
    FnCH375CloseDevice   m_fnClose     = nullptr;
    FnCH375SetTimeout    m_fnTimeout   = nullptr;
    FnCH375WriteData     m_fnWrite     = nullptr;
    FnCH375ReadData      m_fnRead      = nullptr;
    FnCH375WriteEndP     m_fnWriteEP   = nullptr;  // optional, preferred
    FnCH375ReadEndP      m_fnReadEP    = nullptr;  // optional, preferred
    FnCH375SetEndpointTx m_fnSetTx     = nullptr;  // optional
    FnCH375SetEndpointRx m_fnSetRx     = nullptr;  // optional
    FnCH375GetName       m_fnName      = nullptr;  // optional

    void loadDll();   // called from constructor; throws WCHTransportError on failure

    struct DeviceEntry {
        ULONG       ch375Index  = 0;
        std::string displayName;
    };
#else
    libusb_context*       m_ctx    = nullptr;
    libusb_device_handle* m_handle = nullptr;

    struct DeviceEntry {
        uint16_t vid           = 0;
        uint16_t pid           = 0;
        uint8_t  busNumber     = 0;
        uint8_t  deviceAddress = 0;
    };
#endif

    std::vector<DeviceEntry> m_scannedDevices;

    // -----------------------------------------------------------------------
    // Shared constants
    // -----------------------------------------------------------------------
    static constexpr uint16_t k_vid1    = 0x4348;
    static constexpr uint16_t k_vid2    = 0x1A86;
    static constexpr uint16_t k_pid     = 0x55E0;
    static constexpr uint8_t  k_epOut   = 0x02;
    static constexpr uint8_t  k_epIn    = 0x82;
    static constexpr int      k_iface   = 0;
    static constexpr int      k_timeout = 5000;
    static constexpr int      k_maxPkt  = 64;
};
