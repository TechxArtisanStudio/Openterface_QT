/*
* ========================================================================== *
*                                                                            *
*    This file is part of the Openterface Mini KVM App QT version            *
*                                                                            *
*    Copyright (C) 2024   <info@openterface.com>                             *
*                                                                            *
*    This program is free software: you can redistribute it and/or modify    *
*    it under the terms of the GNU General Public License as published by    *
*    the Free Software Foundation version 3.                                 *
*                                                                            *
*    This program is distributed in the hope that it will be useful, but     *
*    WITHOUT ANY WARRANTY; without even the implied warranty of              *
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU        *
*    General Public License for more details.                                *
*                                                                            *
*    You should have received a copy of the GNU General Public License       *
*    along with this program. If not, see <http://www.gnu.org/licenses/>.    *
*                                                                            *
* ========================================================================== *
*/

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
