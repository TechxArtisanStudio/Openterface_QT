#ifdef _WIN32

#include <windows.h>
#include <setupapi.h>
#include <initguid.h>
#include <usbiodef.h>
#include <locale>
#include <codecvt>
#include <string>
#include <iostream>

#pragma comment(lib, "setupapi.lib")

std::string ConvertWideToUTF8(const std::wstring& wstr);
void GetUsbDevices();

#endif