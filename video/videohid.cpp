#include "videohid.h"

#include <QDebug>

#ifdef _WIN32
#include <hidclass.h>
extern "C"
{
#include<hidsdi.h>
#include <setupapi.h>
}
#elif __linux__
#endif

VideoHid::VideoHid(QObject *parent) : QObject(parent){

}

void VideoHid::start() {
    // qDebug() << usbXdataRead4Byte(0xDF00).first.toHex(' ');      //GPIO0
    // qDebug() << usbXdataRead4Byte(0xDF01).first.toHex(' ');      //spdifout 0xDF01
    // qDebug() << usbXdataRead4Byte(0xFA8C).first.toHex(' ');      //HDMI CONNECTION 0xFA8C

    qDebug() << "Resolution:" << getResolution();    //HDMI resolution

    //MS2109 firmware version
    QString version_0 = usbXdataRead4Byte(0xCBDC).first.toHex();
    QString version_1 = usbXdataRead4Byte(0xCBDD).first.toHex();
    QString version_2 = usbXdataRead4Byte(0xCBDE).first.toHex();
    QString version_3 = usbXdataRead4Byte(0xCBDF).first.toHex();
    qDebug() << "MS2109 firmware VERSION:" << version_0 + version_1 + version_2 + version_3;    //firmware VERSION
}

void VideoHid::stop() {
    // implementation
}

QPair<int, int> VideoHid::getResolution() {
    quint8 width_h = static_cast<quint8>(usbXdataRead4Byte(0xC738).first.at(0));
    quint8 width_l = static_cast<quint8>(usbXdataRead4Byte(0xC739).first.at(0));
    quint16 width = (width_h << 8) + width_l;
    quint8 height_h = static_cast<quint8>(usbXdataRead4Byte(0xC73A).first.at(0));
    quint8 height_l = static_cast<quint8>(usbXdataRead4Byte(0xC73B).first.at(0));
    quint16 height = (height_h << 8) + height_l;
    return qMakePair(width, height);
}

float VideoHid::getFps() {
    quint8 fps_h = static_cast<quint8>(usbXdataRead4Byte(0xC73E).first.at(0));
    quint8 fps_l = static_cast<quint8>(usbXdataRead4Byte(0xC73F).first.at(0));
    quint16 fps = (fps_h << 8) + fps_l;
    return static_cast<float>(fps) / 100;
}

QPair<QByteArray, bool> VideoHid::usbXdataRead4Byte(quint16 u16_address) {
    QByteArray ctrlData(9, 0); // Initialize with 9 bytes set to 0
    QByteArray result(9, 0);
    ctrlData[1] = 0xB5;
    ctrlData[2] = static_cast<char>((u16_address >> 8) & 0xFF);
    ctrlData[3] = static_cast<char>(u16_address & 0xFF);


#ifdef _WIN32
    if (this->sendFeatureReport(ctrlData[0], (BYTE*)ctrlData.data(), ctrlData.size())) {
        if (this->getFeatureReport(ctrlData[0], (BYTE*)result.data(), result.size())) {
            return qMakePair(result.mid(4, 1), true);
        }
    } else {
        ctrlData[0] = 0x01;
        if (this->sendFeatureReport(ctrlData[0], (BYTE*)ctrlData.data(), ctrlData.size())) {
            this->getFeatureReport(result[0], (BYTE*)result.data(), result.size());
            if (!result.isEmpty()) {
                return qMakePair(result.mid(3, 4), true);
            }
        }
    }
#elif __linux__
    // implementation
#endif
    return qMakePair(QByteArray(4, 0), false); // Return 4 bytes set to 0 and false
}

std::wstring VideoHid::GetHIDDevicePath() {
    GUID hidGuid;
    HidD_GetHidGuid(&hidGuid); // Get the HID GUID

    // Get a handle to a device information set for all present HID devices.
    HDEVINFO deviceInfoSet = SetupDiGetClassDevs(&hidGuid, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);

    if (deviceInfoSet == INVALID_HANDLE_VALUE) {
        return L"";
    }

    SP_DEVICE_INTERFACE_DATA deviceInterfaceData;
    deviceInterfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

    // Enumerate through all devices in the set
    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(deviceInfoSet, NULL, &hidGuid, i, &deviceInterfaceData); i++) {
        DWORD requiredSize = 0;

        // Get the size required for the device interface detail
        SetupDiGetDeviceInterfaceDetail(deviceInfoSet, &deviceInterfaceData, NULL, 0, &requiredSize, NULL);

        PSP_DEVICE_INTERFACE_DETAIL_DATA deviceInterfaceDetailData = (PSP_DEVICE_INTERFACE_DETAIL_DATA)malloc(requiredSize);
        deviceInterfaceDetailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

        // Now retrieve the device interface detail which includes the device path
        if (SetupDiGetDeviceInterfaceDetail(deviceInfoSet, &deviceInterfaceData, deviceInterfaceDetailData, requiredSize, NULL, NULL)) {
            HANDLE deviceHandle = CreateFile(deviceInterfaceDetailData->DevicePath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);

            if (deviceHandle != INVALID_HANDLE_VALUE) {
                HIDD_ATTRIBUTES attributes;
                attributes.Size = sizeof(attributes);

                if (HidD_GetAttributes(deviceHandle, &attributes)) {
                    if (attributes.VendorID == 0x534D && attributes.ProductID == 0x2109) {
                        std::wstring devicePath = deviceInterfaceDetailData->DevicePath;
                        CloseHandle(deviceHandle);
                        free(deviceInterfaceDetailData);
                        SetupDiDestroyDeviceInfoList(deviceInfoSet);
                        return devicePath; // Found the device
                    }
                }
                CloseHandle(deviceHandle);
            }
        }
        free(deviceInterfaceDetailData);
    }

    SetupDiDestroyDeviceInfoList(deviceInfoSet);
    return L""; // Device not found
}



#ifdef _WIN32
bool VideoHid::sendFeatureReport(BYTE reportId, BYTE* reportBuffer, DWORD bufferSize) {
    HANDLE deviceHandle = CreateFileW(GetHIDDevicePath().c_str(),
                                      GENERIC_READ | GENERIC_WRITE,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE,
                                      NULL,
                                      OPEN_EXISTING,
                                      0,
                                      NULL);
    if (deviceHandle == INVALID_HANDLE_VALUE) {
        qDebug() << "Failed to open device handle for sending feature report.";
        return false;
    }

    // The first byte of the buffer should be the report ID
    reportBuffer[0] = reportId;

    // Send the Set Feature Report request
    BOOL result = HidD_SetFeature(deviceHandle, reportBuffer, bufferSize);

    CloseHandle(deviceHandle); // Always close the handle when done

    if (!result) {
        qDebug() << "Failed to send feature report.";
        return false;
    }
    return true;
}

bool VideoHid::getFeatureReport(BYTE reportId, BYTE* reportBuffer, DWORD bufferSize) {
    // Assuming devicePath is a member variable containing the device path
    HANDLE deviceHandle = CreateFileW(GetHIDDevicePath().c_str(),
                                      GENERIC_READ | GENERIC_WRITE,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE,
                                      NULL,
                                      OPEN_EXISTING,
                                      0,
                                      NULL);
    if (deviceHandle == INVALID_HANDLE_VALUE) {
        qDebug() << "Failed to open device handle.";
        return false;
    }

    // Prepare the report buffer
    // The first byte of the buffer should be the report ID
    reportBuffer[0] = reportId;

    // Send the Get Feature Report request
    BOOL result = HidD_GetFeature(deviceHandle, reportBuffer, bufferSize);

    if (!result) {
        qDebug() << "Failed to get feature report.";
    }

    // Close the device handle
    CloseHandle(deviceHandle);

    return result;
}
#elif __linux__
bool VideoHid::sendFeatureReport(int fd, unsigned char* reportBuffer, int bufferSize) {
    // implementation
    return false;
}

bool VideoHid::getFeatureReport(int fd, unsigned char* reportBuffer, int bufferSize) {
    // implementation
    return false;
}

#endif