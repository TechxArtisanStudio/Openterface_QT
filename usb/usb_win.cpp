#ifdef _WIN32
#include "usb_win.h"

std::string ConvertWideToUTF8(const std::wstring& wstr)
{
    std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> converter;
    return converter.to_bytes(wstr);
}

void GetUsbDevices()
{
    HDEVINFO hDevInfo;
    SP_DEVINFO_DATA DeviceInfoData;
    DWORD i;

    // Create a HDEVINFO with all present devices
    hDevInfo = SetupDiGetClassDevs(&GUID_DEVINTERFACE_USB_DEVICE, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (hDevInfo == INVALID_HANDLE_VALUE)
    {
        // Insert error handling here
        return;
    }
    // Enumerate through all devices in the set
    DeviceInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
    for (i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &DeviceInfoData); i++)
    {
        DWORD DataT;
        LPTSTR buffer = NULL;
        DWORD buffersize = 0;

        while (!SetupDiGetDeviceRegistryProperty(hDevInfo, &DeviceInfoData,
                                                 SPDRP_DEVICEDESC, &DataT, (PBYTE)buffer, buffersize, &buffersize))
        {
            if (GetLastError() == ERROR_INSUFFICIENT_BUFFER)
            {
                // Change the buffer size
                if (buffer) LocalFree(buffer);
                buffer = (LPTSTR)LocalAlloc(LPTR, buffersize * 2);
            }
            else
            {
                // Insert error handling here
                break;
            }
        }

        std::wstring wDeviceName(buffer);
        std::string deviceName = ConvertWideToUTF8(wDeviceName);
        
        printf("Device: %s\n", deviceName.c_str());

        if (buffer) LocalFree(buffer);
    }

    if (hDevInfo) SetupDiDestroyDeviceInfoList(hDevInfo);
}
#endif
