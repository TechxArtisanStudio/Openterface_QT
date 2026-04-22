#ifdef _WIN32

#include "WindowsHIDTransport.h"
#include "../videohid.h"

#include <QDebug>
#include <QLoggingCategory>
#include <QMutexLocker>
#include <QString>
#include <QThread>
#include <QByteArray>
#include "../../ui/globalsetting.h"

#include <windows.h>
#include <setupapi.h>
extern "C" {
#include <hidsdi.h>
#include <hidpi.h>
}

Q_LOGGING_CATEGORY(log_win_transport, "opf.host.win_transport")

// ─────────────────────────────────────────────────────────────────────────────
// hidSendFeatureNoTimeout
// Replacement for HidD_SetFeature that has NO internal 5-second timeout.
//
// HidD_SetFeature is implemented in hid.dll as:
//   DeviceIoControl(handle, IOCTL_HID_SET_FEATURE, ..., lpOverlapped=NULL)
// When handle has FILE_FLAG_OVERLAPPED but lpOverlapped is NULL it waits
// synchronously with an internal WaitForSingleObject(event, 5000 ms), causing
// ERROR_SEM_TIMEOUT (121) for any USB control transfer that takes longer than
// 5 s (e.g. MS2130S sector erase ~5-8 s, 4096-byte burst data packets).
//
// By calling DeviceIoControl ourselves with an explicit OVERLAPPED and then
// WaitForSingleObject(event, timeoutMs) we control (and can remove) that limit.
//
// hDevice MUST be opened with FILE_FLAG_OVERLAPPED.
// timeoutMs: max wait in ms; pass INFINITE (0xFFFFFFFF) to wait forever.
// Returns TRUE on success. On timeout sets last error to ERROR_SEM_TIMEOUT (121).
// ─────────────────────────────────────────────────────────────────────────────
/* static */
BOOL WindowsHIDTransport::hidSendFeatureNoTimeout(HANDLE hDevice, void* buf, DWORD bufLen,
                                                   DWORD timeoutMs)
{
    // IOCTL_HID_SET_FEATURE = HID_IN_CTL_CODE(100)
    //   = CTL_CODE(FILE_DEVICE_KEYBOARD=0xb, 100, METHOD_IN_DIRECT=1, FILE_ANY_ACCESS=0)
    //   = (0xb<<16)|(0<<14)|(100<<2)|1 = 0x000B0191
    constexpr DWORD kSetFeatureIoctl = 0x000B0191UL;

    OVERLAPPED ol = {};
    ol.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!ol.hEvent) return FALSE;

    DWORD dummy = 0;
    // IOCTL_HID_SET_FEATURE uses METHOD_IN_DIRECT: pass same buffer as both input
    // and output so the MDL is present for large (4096-byte) feature reports.
    BOOL ok = DeviceIoControl(hDevice, kSetFeatureIoctl,
                              buf, bufLen,
                              buf, bufLen,
                              &dummy, &ol);
    if (!ok) {
        DWORD err = GetLastError();
        if (err == ERROR_IO_PENDING) {
            DWORD w = WaitForSingleObject(ol.hEvent, timeoutMs);
            if (w == WAIT_OBJECT_0) {
                ok = GetOverlappedResult(hDevice, &ol, &dummy, FALSE);
            } else {
                CancelIo(hDevice);
                GetOverlappedResult(hDevice, &ol, &dummy, TRUE);
                SetLastError(ERROR_SEM_TIMEOUT);
                ok = FALSE;
            }
        }
    }
    DWORD savedError = GetLastError();
    CloseHandle(ol.hEvent);
    if (!ok) SetLastError(savedError);
    return ok;
}

// ─────────────────────────────────────────────────────────────────────────────
WindowsHIDTransport::WindowsHIDTransport(VideoHid* owner)
    : m_owner(owner)
{
}

WindowsHIDTransport::~WindowsHIDTransport()
{
    closeLocked();
}

bool WindowsHIDTransport::isOpen() const
{
    QMutexLocker locker(&m_mutex);
    return m_handle != INVALID_HANDLE_VALUE;
}

// ── internal helpers ─────────────────────────────────────────────────────────

bool WindowsHIDTransport::openLocked()
{
    if (m_handle != INVALID_HANDLE_VALUE) return true;

    std::wstring devicePath = getHIDDevicePathW();
    if (devicePath.empty()) {
        qCWarning(log_win_transport) << "openLocked: failed to get valid HID device path";
        return false;
    }

    QString qDevicePath = QString::fromStdWString(devicePath);
    qCDebug(log_win_transport) << "Opening device with path:" << qDevicePath;

    if (!qDevicePath.startsWith("\\\\") && !qDevicePath.startsWith("\\\\.\\")
        && !qDevicePath.startsWith("\\\\?\\")) {
        qCWarning(log_win_transport) << "Invalid device path format:" << qDevicePath;
        return false;
    }

    // Open with GENERIC_READ + FILE_FLAG_OVERLAPPED.
    // GENERIC_WRITE causes the HID class driver to hold the write channel exclusively,
    // preventing the device from ACKing feature report IOCTLs promptly.
    m_handle = CreateFileW(devicePath.c_str(),
                           GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL,
                           OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
                           NULL);

    if (m_handle == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        qCWarning(log_win_transport) << "Failed to open device handle, error:" << error;
        return false;
    }
    return true;
}

void WindowsHIDTransport::closeLocked()
{
    if (m_handle != INVALID_HANDLE_VALUE) {
        qCDebug(log_win_transport) << "Closing HID device handle";
        CloseHandle(m_handle);
        m_handle = INVALID_HANDLE_VALUE;
    }
}

// ── IHIDTransport ─────────────────────────────────────────────────────────────

bool WindowsHIDTransport::open()
{
    QMutexLocker locker(&m_mutex);
    return openLocked();
}

void WindowsHIDTransport::close()
{
    QMutexLocker locker(&m_mutex);
    closeLocked();
}

bool WindowsHIDTransport::sendFeatureReport(uint8_t* buf, size_t len)
{
    QMutexLocker locker(&m_mutex);
    bool opened = false;
    if (m_handle == INVALID_HANDLE_VALUE) {
        if (!openLocked()) return false;
        opened = true;
    }

    int retries = 2;
    bool ok = false;
    while (retries-- > 0) {
        ok = HidD_SetFeature(m_handle, buf, static_cast<ULONG>(len));
        if (ok) break;
        DWORD err = GetLastError();
        if (err == ERROR_DEVICE_NOT_CONNECTED || err == 433) break;  // fatal
        qCDebug(log_win_transport) << "sendFeatureReport retry, error" << err;
    }
    if (!ok) {
        qCWarning(log_win_transport) << "sendFeatureReport failed, last error:" << GetLastError();
    }
    if (opened) closeLocked();
    return ok;
}

bool WindowsHIDTransport::getFeatureReport(uint8_t* buf, size_t len)
{
    QMutexLocker locker(&m_mutex);
    bool opened = false;
    if (m_handle == INVALID_HANDLE_VALUE) {
        if (!openLocked()) return false;
        opened = true;
    }

    bool ok = HidD_GetFeature(m_handle, buf, static_cast<ULONG>(len));
    if (!ok) {
        DWORD err = GetLastError();
        qCWarning(log_win_transport) << "getFeatureReport failed, error:" << err;

        // Fallback: try alternate report IDs
        for (BYTE rid : { (BYTE)0x00, (BYTE)0x01 }) {
            QByteArray tmp(reinterpret_cast<char*>(buf), static_cast<int>(len));
            tmp[0] = static_cast<char>(rid);
            BOOL got = HidD_GetFeature(m_handle, reinterpret_cast<BYTE*>(tmp.data()),
                                       static_cast<ULONG>(len));
            if (got) {
                memcpy(buf, tmp.data(), len);
                ok = true;
                qCDebug(log_win_transport) << "getFeatureReport succeeded with fallback reportID" << (int)rid;
                break;
            }
        }
    }

    if (opened) closeLocked();
    return ok;
}

bool WindowsHIDTransport::sendDirect(uint8_t* buf, size_t len)
{
    QMutexLocker locker(&m_mutex);
    if (m_handle == INVALID_HANDLE_VALUE) {
        qCWarning(log_win_transport) << "sendDirect: handle not open";
        return false;
    }
    BOOL ok = HidD_SetFeature(m_handle, buf, static_cast<ULONG>(len));
    if (!ok) {
        qCDebug(log_win_transport) << "sendDirect HidD_SetFeature failed, error" << GetLastError();
    }
    return ok != FALSE;
}

bool WindowsHIDTransport::getDirect(uint8_t* buf, size_t len)
{
    QMutexLocker locker(&m_mutex);
    if (m_handle == INVALID_HANDLE_VALUE) {
        qCWarning(log_win_transport) << "getDirect: handle not open";
        return false;
    }
    BOOL ok = HidD_GetFeature(m_handle, buf, static_cast<ULONG>(len));
    if (!ok) {
        qCDebug(log_win_transport) << "getDirect HidD_GetFeature failed, error" << GetLastError();
    }
    return ok != FALSE;
}

bool WindowsHIDTransport::reopenSync()
{
    QMutexLocker locker(&m_mutex);
    closeLocked();

    std::wstring devicePath = getHIDDevicePathW();
    if (devicePath.empty()) {
        qCWarning(log_win_transport) << "reopenSync: failed to get device path";
        return false;
    }

    qCDebug(log_win_transport) << "reopenSync: opening synchronous handle for flash ops";
    m_handle = CreateFileW(devicePath.c_str(),
                           GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, NULL);

    if (m_handle == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        qCWarning(log_win_transport) << "reopenSync: GENERIC_READ|WRITE failed, error" << err
                                     << "- retrying with GENERIC_READ only";
        m_handle = CreateFileW(devicePath.c_str(),
                               GENERIC_READ,
                               FILE_SHARE_READ | FILE_SHARE_WRITE,
                               NULL, OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL, NULL);

        if (m_handle == INVALID_HANDLE_VALUE) {
            qCWarning(log_win_transport) << "reopenSync: fallback also failed, error" << GetLastError();
            return false;
        }
        qCDebug(log_win_transport) << "reopenSync: opened with GENERIC_READ only (fallback)";
    } else {
        qCDebug(log_win_transport) << "reopenSync: opened with GENERIC_READ|GENERIC_WRITE";
    }
    return true;
}

// ── Path discovery ────────────────────────────────────────────────────────────

QString WindowsHIDTransport::getHIDDevicePath()
{
    return QString::fromStdWString(getHIDDevicePathW());
}

std::wstring WindowsHIDTransport::getHIDDevicePathW()
{
    if (m_owner) {
        QString portChain = GlobalSetting::instance().getOpenterfacePortChain();
        QString hidPath = m_owner->findMatchingHIDDevice(portChain);

        if (!hidPath.isEmpty()) {
            // For MS2130S devices we need the full \\?\hid... path
            if (hidPath.contains("VID_345F", Qt::CaseInsensitive) &&
                hidPath.contains("PID_2132", Qt::CaseInsensitive)) {
                std::wstring fullPath = getProperDevicePath(hidPath.toStdWString());
                if (!fullPath.empty())
                    return fullPath;
            }
            return hidPath.toStdWString();
        }
    }

    // Fallback: enumerate all HID devices looking for known VID/PID 0x534D:0x2109
    qCDebug(log_win_transport) << "Falling back to VID/PID enumeration for HID device discovery";

    GUID hidGuid;
    HidD_GetHidGuid(&hidGuid);

    HDEVINFO deviceInfoSet = SetupDiGetClassDevs(&hidGuid, NULL, NULL,
                                                 DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (deviceInfoSet == INVALID_HANDLE_VALUE) return L"";

    SP_DEVICE_INTERFACE_DATA deviceInterfaceData;
    deviceInterfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

    for (DWORD i = 0;
         SetupDiEnumDeviceInterfaces(deviceInfoSet, NULL, &hidGuid, i, &deviceInterfaceData);
         ++i) {
        DWORD requiredSize = 0;
        SetupDiGetDeviceInterfaceDetail(deviceInfoSet, &deviceInterfaceData,
                                        NULL, 0, &requiredSize, NULL);

        auto* detail = reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA>(malloc(requiredSize));
        if (!detail) continue;
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

        if (SetupDiGetDeviceInterfaceDetail(deviceInfoSet, &deviceInterfaceData,
                                            detail, requiredSize, NULL, NULL)) {
            HANDLE h = CreateFile(detail->DevicePath,
                                  GENERIC_READ | GENERIC_WRITE,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE,
                                  NULL, OPEN_EXISTING, 0, NULL);
            if (h != INVALID_HANDLE_VALUE) {
                HIDD_ATTRIBUTES attr; attr.Size = sizeof(attr);
                if (HidD_GetAttributes(h, &attr) &&
                    attr.VendorID == 0x534D && attr.ProductID == 0x2109) {
                    std::wstring path = detail->DevicePath;
                    CloseHandle(h);
                    free(detail);
                    SetupDiDestroyDeviceInfoList(deviceInfoSet);
                    return path;
                }
                CloseHandle(h);
            }
        }
        free(detail);
    }

    SetupDiDestroyDeviceInfoList(deviceInfoSet);
    return L"";
}

std::wstring WindowsHIDTransport::getProperDevicePath(const std::wstring& deviceInstancePath)
{
    GUID hidGuid;
    HidD_GetHidGuid(&hidGuid);

    QString instancePathStr = QString::fromStdWString(deviceInstancePath);
    qCDebug(log_win_transport) << "Looking for proper device path for:" << instancePathStr;

    // Already a proper device path?
    if (instancePathStr.startsWith("\\\\?\\hid", Qt::CaseInsensitive) ||
        instancePathStr.startsWith("\\\\.\\hid", Qt::CaseInsensitive)) {
        qCDebug(log_win_transport) << "Path already appears to be a proper device path";
        if (instancePathStr.contains("VID_", Qt::CaseInsensitive) &&
            instancePathStr.contains("PID_", Qt::CaseInsensitive))
            return deviceInstancePath;
    }

    // Extract VID, PID and optional MI from instance path
    uint16_t targetVid = 0x345F, targetPid = 0x2132;
    QString mi;

    if (instancePathStr.contains("VID_", Qt::CaseInsensitive) &&
        instancePathStr.contains("PID_", Qt::CaseInsensitive)) {
        int vidIdx = instancePathStr.indexOf("VID_", 0, Qt::CaseInsensitive) + 4;
        int pidIdx = instancePathStr.indexOf("PID_", 0, Qt::CaseInsensitive) + 4;
        if (vidIdx > 4 && pidIdx > 4) {
            bool ok1 = false, ok2 = false;
            uint16_t v = instancePathStr.mid(vidIdx, 4).toUShort(&ok1, 16);
            uint16_t p = instancePathStr.mid(pidIdx, 4).toUShort(&ok2, 16);
            if (ok1 && ok2) { targetVid = v; targetPid = p; }
        }
    }
    if (instancePathStr.contains("MI_", Qt::CaseInsensitive)) {
        int miIdx = instancePathStr.indexOf("MI_", 0, Qt::CaseInsensitive) + 3;
        if (miIdx > 3) mi = instancePathStr.mid(miIdx, 2);
    }

    HDEVINFO deviceInfoSet = SetupDiGetClassDevs(&hidGuid, NULL, NULL,
                                                 DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (deviceInfoSet == INVALID_HANDLE_VALUE) return L"";

    SP_DEVICE_INTERFACE_DATA deviceInterfaceData;
    deviceInterfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);
    std::wstring properPath;

    for (DWORD i = 0;
         SetupDiEnumDeviceInterfaces(deviceInfoSet, NULL, &hidGuid, i, &deviceInterfaceData);
         ++i) {
        DWORD requiredSize = 0;
        SetupDiGetDeviceInterfaceDetail(deviceInfoSet, &deviceInterfaceData,
                                        NULL, 0, &requiredSize, NULL);
        if (requiredSize == 0) continue;

        auto* detail = reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA>(malloc(requiredSize));
        if (!detail) continue;
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

        if (SetupDiGetDeviceInterfaceDetail(deviceInfoSet, &deviceInterfaceData,
                                            detail, requiredSize, NULL, NULL)) {
            QString curPath = QString::fromWCharArray(detail->DevicePath);
            HANDLE h = CreateFile(detail->DevicePath,
                                  GENERIC_READ | GENERIC_WRITE,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE,
                                  NULL, OPEN_EXISTING, 0, NULL);
            if (h != INVALID_HANDLE_VALUE) {
                HIDD_ATTRIBUTES attr; attr.Size = sizeof(attr);
                if (HidD_GetAttributes(h, &attr) &&
                    attr.VendorID == targetVid && attr.ProductID == targetPid) {
                    bool miMatch = mi.isEmpty() ||
                                   curPath.contains("MI_" + mi, Qt::CaseInsensitive);
                    if (miMatch) {
                        properPath = detail->DevicePath;
                        CloseHandle(h);
                        free(detail);
                        break;
                    }
                }
                CloseHandle(h);
            }
        }
        free(detail);
    }

    SetupDiDestroyDeviceInfoList(deviceInfoSet);

    if (properPath.empty())
        qCWarning(log_win_transport) << "Could not find proper device path for:" << instancePathStr;
    else
        qCDebug(log_win_transport) << "Found proper device path:"
                                   << QString::fromStdWString(properPath);
    return properPath;
}

#endif // _WIN32
