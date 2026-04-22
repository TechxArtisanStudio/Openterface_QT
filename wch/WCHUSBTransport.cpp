#include "WCHUSBTransport.h"

#ifdef _WIN32
// ============================================================================
//  Windows -- CH375DLL dynamic binding
//  Works with the existing CH375_A64 driver installed by WCH tools.
//  No Zadig / WinUSB driver replacement needed.
// ============================================================================

#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cctype>

// Return lowercase copy of s
static std::string strLower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    return s;
}

// Check whether a device-name path contains the ISP VID/PID
static bool nameMatchesISP(const std::string& name)
{
    std::string lo = strLower(name);
    bool vid = (lo.find("vid_4348") != std::string::npos ||
                lo.find("vid_1a86") != std::string::npos);
    bool pid =  lo.find("pid_55e0") != std::string::npos;
    return vid && pid;
}

// ---------------------------------------------------------------------------
// loadDll
// ---------------------------------------------------------------------------
void WCHUSBTransport::loadDll()
{
    // Try 64-bit DLL first, then 32-bit name as fallback
    m_dllModule = LoadLibraryA("CH375DLL64.DLL");
    if (!m_dllModule)
        m_dllModule = LoadLibraryA("CH375DLL.DLL");
    if (!m_dllModule)
        throw WCHTransportError(
            "Cannot load CH375DLL64.DLL or CH375DLL.DLL.\n"
            "Install the WCH USB driver: https://www.wch.cn/downloads/CH375DRV_ZIP.html");

#define LOAD_REQUIRED(var, sym) \
    var = reinterpret_cast<decltype(var)>(GetProcAddress(m_dllModule, sym)); \
    if (!(var)) { \
        FreeLibrary(m_dllModule); m_dllModule = nullptr; \
        throw WCHTransportError(std::string("CH375DLL missing export: ") + (sym)); \
    }

    LOAD_REQUIRED(m_fnOpen,    "CH375OpenDevice")
    LOAD_REQUIRED(m_fnClose,   "CH375CloseDevice")
    LOAD_REQUIRED(m_fnTimeout, "CH375SetTimeout")
    LOAD_REQUIRED(m_fnWrite,   "CH375WriteData")
    LOAD_REQUIRED(m_fnRead,    "CH375ReadData")
#undef LOAD_REQUIRED

    // Optional functions -- preferred when available
    // CH375WriteEndP / CH375ReadEndP target a specific endpoint number,
    // which avoids relying on the driver's default endpoint configuration.
    m_fnWriteEP = reinterpret_cast<FnCH375WriteEndP>(
        GetProcAddress(m_dllModule, "CH375WriteEndP"));
    m_fnReadEP  = reinterpret_cast<FnCH375ReadEndP>(
        GetProcAddress(m_dllModule, "CH375ReadEndP"));
    // CH375SetEndpointTx / Rx configure which endpoint WriteData/ReadData use
    m_fnSetTx   = reinterpret_cast<FnCH375SetEndpointTx>(
        GetProcAddress(m_dllModule, "CH375SetEndpointTx"));
    m_fnSetRx   = reinterpret_cast<FnCH375SetEndpointRx>(
        GetProcAddress(m_dllModule, "CH375SetEndpointRx"));
    // Device-name query
    m_fnName    = reinterpret_cast<FnCH375GetName>(
        GetProcAddress(m_dllModule, "CH375GetDeviceName"));
}

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------
WCHUSBTransport::WCHUSBTransport()
{
    loadDll();
}

WCHUSBTransport::~WCHUSBTransport()
{
    close();
    if (m_dllModule) {
        FreeLibrary(m_dllModule);
        m_dllModule = nullptr;
    }
}

bool WCHUSBTransport::isOpen() const
{
    return m_openIndex >= 0;
}

// ---------------------------------------------------------------------------
// scanDevices -- probe CH375 device indices 0..15
// ---------------------------------------------------------------------------
std::vector<std::string> WCHUSBTransport::scanDevices()
{
    m_scannedDevices.clear();
    std::vector<std::string> names;

    for (ULONG idx = 0; idx < 16; ++idx) {
        HANDLE h = m_fnOpen(idx);
        if (h == INVALID_HANDLE_VALUE)
            continue;   // no device at this index

        // Read device name to filter VID/PID if the function is available
        std::string devName;
        if (m_fnName) {
            char buf[260] = {};
            if (m_fnName(idx, buf))
                devName = buf;
        }

        m_fnClose(idx);  // close immediately -- just checking existence

        // If we obtained a name, require it to match ISP VID/PID; otherwise accept
        if (!devName.empty() && !nameMatchesISP(devName))
            continue;

        DeviceEntry entry;
        entry.ch375Index = idx;

        std::ostringstream oss;
        oss << "WCH ISP Device " << m_scannedDevices.size()
            << " (CH375 index=" << idx;
        if (!devName.empty())
            oss << "\n  " << devName;
        oss << ")";
        entry.displayName = oss.str();

        m_scannedDevices.push_back(entry);
        names.push_back(entry.displayName);
    }

    return names;
}

// ---------------------------------------------------------------------------
// open
// ---------------------------------------------------------------------------
void WCHUSBTransport::open(int deviceIndex)
{
    close();

    if (deviceIndex < 0 ||
        deviceIndex >= static_cast<int>(m_scannedDevices.size()))
        throw WCHTransportError("Invalid device index. Call scanDevices() first.");

    ULONG ch375Idx = m_scannedDevices[static_cast<size_t>(deviceIndex)].ch375Index;

    HANDLE h = m_fnOpen(ch375Idx);
    if (h == INVALID_HANDLE_VALUE)
        throw WCHTransportError(
            "CH375OpenDevice failed.\n"
            "Ensure the WCH ISP device is connected and in bootloader mode.");

    // Set bulk transfer timeout (write ms, read ms)
    m_fnTimeout(ch375Idx,
                static_cast<ULONG>(k_timeout),
                static_cast<ULONG>(k_timeout));

    // Explicitly configure which endpoints WriteData/ReadData use.
    // WCH ISP bootloader: bulk OUT = 0x02, bulk IN = 0x82.
    // If the DLL supports SetEndpointTx/Rx, call them; otherwise the EndP
    // variant of write/read is used in transfer() for explicit targeting.
    if (m_fnSetTx) m_fnSetTx(ch375Idx, static_cast<UCHAR>(k_epOut));
    if (m_fnSetRx) m_fnSetRx(ch375Idx, static_cast<UCHAR>(k_epIn));

    m_openIndex = static_cast<int>(ch375Idx);
}

// ---------------------------------------------------------------------------
// close
// ---------------------------------------------------------------------------
void WCHUSBTransport::close()
{
    if (m_openIndex >= 0) {
        m_fnClose(static_cast<ULONG>(m_openIndex));
        m_openIndex = -1;
    }
}

// ---------------------------------------------------------------------------
// transfer -- bulk write then bulk read via CH375DLL
// Use CH375WriteData / CH375ReadData (most compatible).
// Endpoint selection is pre-configured in open() via CH375SetEndpointTx/Rx
// when those exports are available.
// ---------------------------------------------------------------------------
std::vector<uint8_t> WCHUSBTransport::transfer(const std::vector<uint8_t>& command)
{
    if (m_openIndex < 0)
        throw WCHTransportError("Device not open");

    ULONG idx = static_cast<ULONG>(m_openIndex);

    // ---- Write ----
    std::vector<uint8_t> wbuf(command);
    ULONG wlen = static_cast<ULONG>(wbuf.size());
    if (!m_fnWrite(idx, wbuf.data(), &wlen))
        throw WCHTransportError("CH375WriteData failed");
    if (wlen != static_cast<ULONG>(command.size()))
        throw WCHTransportError("CH375WriteData: incomplete transfer");

    // ---- Read ----
    std::vector<uint8_t> rbuf(static_cast<size_t>(k_maxPkt), 0);
    ULONG rlen = static_cast<ULONG>(k_maxPkt);
    if (!m_fnRead(idx, rbuf.data(), &rlen))
        throw WCHTransportError("CH375ReadData failed");

    rbuf.resize(static_cast<size_t>(rlen));
    return rbuf;
}

#else
// ============================================================================
//  macOS / Linux -- libusb-1.0
// ============================================================================

#include <libusb-1.0/libusb.h>

#include <cstring>
#include <sstream>
#include <iomanip>

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------
WCHUSBTransport::WCHUSBTransport()
{
    int rc = libusb_init(&m_ctx);
    if (rc < 0)
        throw WCHTransportError(std::string("libusb_init failed: ") +
                                libusb_error_name(rc));
}

WCHUSBTransport::~WCHUSBTransport()
{
    close();
    if (m_ctx) {
        libusb_exit(m_ctx);
        m_ctx = nullptr;
    }
}

bool WCHUSBTransport::isOpen() const
{
    return m_handle != nullptr;
}

// ---------------------------------------------------------------------------
// scanDevices
// ---------------------------------------------------------------------------
std::vector<std::string> WCHUSBTransport::scanDevices()
{
    m_scannedDevices.clear();
    std::vector<std::string> names;

    libusb_device** list = nullptr;
    ssize_t count = libusb_get_device_list(m_ctx, &list);
    if (count < 0)
        throw WCHTransportError(std::string("libusb_get_device_list failed: ") +
                                libusb_error_name(static_cast<int>(count)));

    for (ssize_t i = 0; i < count; ++i) {
        libusb_device* dev = list[i];
        libusb_device_descriptor desc{};
        if (libusb_get_device_descriptor(dev, &desc) < 0)
            continue;

        if ((desc.idVendor == k_vid1 || desc.idVendor == k_vid2) &&
            desc.idProduct == k_pid)
        {
            DeviceEntry entry;
            entry.vid           = desc.idVendor;
            entry.pid           = desc.idProduct;
            entry.busNumber     = libusb_get_bus_number(dev);
            entry.deviceAddress = libusb_get_device_address(dev);

            std::ostringstream oss;
            oss << "WCH ISP Device " << m_scannedDevices.size()
                << " (VID=0x" << std::hex << std::setw(4) << std::setfill('0')
                << entry.vid
                << " PID=0x" << std::setw(4) << entry.pid
                << " Bus=" << std::dec << static_cast<int>(entry.busNumber)
                << " Addr=" << static_cast<int>(entry.deviceAddress)
                << ")";

            m_scannedDevices.push_back(entry);
            names.push_back(oss.str());
        }
    }

    libusb_free_device_list(list, 1);
    return names;
}

// ---------------------------------------------------------------------------
// open
// ---------------------------------------------------------------------------
void WCHUSBTransport::open(int deviceIndex)
{
    if (m_handle)
        close();

    if (deviceIndex < 0 ||
        deviceIndex >= static_cast<int>(m_scannedDevices.size()))
        throw WCHTransportError("Invalid device index. Call scanDevices() first.");

    const DeviceEntry& entry = m_scannedDevices[static_cast<size_t>(deviceIndex)];

    libusb_device** list = nullptr;
    ssize_t count = libusb_get_device_list(m_ctx, &list);
    if (count < 0)
        throw WCHTransportError("libusb_get_device_list failed during open");

    libusb_device* target = nullptr;
    for (ssize_t i = 0; i < count; ++i) {
        libusb_device* dev = list[i];
        if (libusb_get_bus_number(dev) == entry.busNumber &&
            libusb_get_device_address(dev) == entry.deviceAddress)
        {
            target = dev;
            break;
        }
    }

    if (!target) {
        libusb_free_device_list(list, 1);
        throw WCHTransportError("Device not found. It may have been disconnected.");
    }

    libusb_device_handle* handle = nullptr;
    int rc = libusb_open(target, &handle);
    libusb_free_device_list(list, 1);

    if (rc < 0)
        throw WCHTransportError(std::string("libusb_open failed: ") +
                                libusb_error_name(rc));

    m_handle = handle;

#if defined(__linux__)
    if (libusb_kernel_driver_active(m_handle, k_iface) == 1) {
        rc = libusb_detach_kernel_driver(m_handle, k_iface);
        if (rc < 0) {
            libusb_close(m_handle);
            m_handle = nullptr;
            throw WCHTransportError(std::string("Failed to detach kernel driver: ") +
                                    libusb_error_name(rc));
        }
    }
#endif

    rc = libusb_set_configuration(m_handle, 1);
    if (rc < 0 && rc != LIBUSB_ERROR_BUSY) {
        libusb_close(m_handle);
        m_handle = nullptr;
        throw WCHTransportError(std::string("libusb_set_configuration failed: ") +
                                libusb_error_name(rc));
    }

    rc = libusb_claim_interface(m_handle, k_iface);
    if (rc < 0) {
        libusb_close(m_handle);
        m_handle = nullptr;
        throw WCHTransportError(std::string("libusb_claim_interface failed: ") +
                                libusb_error_name(rc));
    }
}

// ---------------------------------------------------------------------------
// close
// ---------------------------------------------------------------------------
void WCHUSBTransport::close()
{
    if (m_handle) {
        libusb_release_interface(m_handle, k_iface);
        libusb_close(m_handle);
        m_handle = nullptr;
    }
}

// ---------------------------------------------------------------------------
// transfer -- bulk OUT then bulk IN
// ---------------------------------------------------------------------------
std::vector<uint8_t> WCHUSBTransport::transfer(const std::vector<uint8_t>& command)
{
    if (!m_handle)
        throw WCHTransportError("Device not open");

    int transferred = 0;
    int rc = libusb_bulk_transfer(m_handle,
                                   k_epOut,
                                   const_cast<uint8_t*>(command.data()),
                                   static_cast<int>(command.size()),
                                   &transferred,
                                   k_timeout);
    if (rc < 0)
        throw WCHTransportError(std::string("Bulk write failed: ") +
                                libusb_error_name(rc));
    if (transferred != static_cast<int>(command.size()))
        throw WCHTransportError("Bulk write: incomplete transfer");

    std::vector<uint8_t> recvBuf(k_maxPkt, 0);
    int recvLen = 0;
    rc = libusb_bulk_transfer(m_handle,
                               k_epIn,
                               recvBuf.data(),
                               k_maxPkt,
                               &recvLen,
                               k_timeout);
    if (rc < 0)
        throw WCHTransportError(std::string("Bulk read failed: ") +
                                libusb_error_name(rc));

    recvBuf.resize(static_cast<size_t>(recvLen));
    return recvBuf;
}

#endif  // _WIN32
