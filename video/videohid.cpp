#include "videohid.h"

#include <QDebug>
#include <QDir>

#ifdef _WIN32
#include <hidclass.h>
extern "C"
{
#include<hidsdi.h>
#include <setupapi.h>
}
#elif __linux__
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/hidraw.h>
#endif

VideoHid::VideoHid(QObject *parent) : QObject(parent){

}

void VideoHid::start() {
    qDebug() << "Current GPIO0 Value:" << usbXdataRead4Byte(0xDF00).first.toHex(' ');      //GPIO0
    qDebug() << "Current SPDIFOUT Value:" << usbXdataRead4Byte(0xDF01).first.toHex(' ');      //spdifout 0xDF01
    qDebug() << "Current HDMI connection status:" << usbXdataRead4Byte(0xFA8C).first.toHex(' ');      //HDMI CONNECTION 0xFA8C
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
    // 0: Some devices use report ID 0 to indicate that no specific report ID is used. 
    if (this->sendFeatureReport((uint8_t*)ctrlData.data(), ctrlData.size())) {
        if (this->getFeatureReport((uint8_t*)result.data(), result.size())) {
            //qDebug() << "usbXdataRead4Byte: " << result.toHex();
            return qMakePair(result.mid(4, 1), true);
        }
    } else {
        // 1: Some devices use report ID 1 to indicate that no specific report ID is used.
        ctrlData[0] = 0x01;
        if (this->sendFeatureReport((uint8_t*)ctrlData.data(), ctrlData.size())) {
            this->getFeatureReport((uint8_t*)result.data(), result.size());

            if (!result.isEmpty()) {
                return qMakePair(result.mid(3, 4), true);
            }
        }
    }
    return qMakePair(QByteArray(4, 0), false); // Return 4 bytes set to 0 and false
}

bool VideoHid::getFeatureReport(uint8_t* buffer, size_t bufferLength) {
#ifdef _WIN32
    return this->getFeatureReportWindows(buffer, bufferLength);
#elif __linux__
    return this->getFeatureReportLinux(buffer, bufferLength);
#endif
}

bool VideoHid::sendFeatureReport(uint8_t* buffer, size_t bufferLength) {
#ifdef _WIN32
    return this->sendFeatureReportWindows(buffer, bufferLength);
#elif __linux__
    // implementation
    return this->sendFeatureReportLinux(buffer, bufferLength);
#endif
}

#ifdef _WIN32
std::wstring VideoHid::getHIDDevicePath() {
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

bool VideoHid::sendFeatureReportWindows(BYTE* reportBuffer, DWORD bufferSize) {
    HANDLE deviceHandle = CreateFileW(getHIDDevicePath().c_str(),
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

    // Send the Set Feature Report request
    BOOL result = HidD_SetFeature(deviceHandle, reportBuffer, bufferSize);

    CloseHandle(deviceHandle); // Always close the handle when done

    if (!result) {
        qDebug() << "Failed to send feature report.";
        return false;
    }
    return true;
}

bool VideoHid::getFeatureReportWindows(BYTE* reportBuffer, DWORD bufferSize) {
    // Assuming devicePath is a member variable containing the device path
    HANDLE deviceHandle = CreateFileW(getHIDDevicePath().c_str(),
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
QString VideoHid::getHIDDevicePath() {
    QDir dir("/sys/class/hidraw");

    QStringList hidrawDevices = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);

    foreach (const QString &device, hidrawDevices) {\
        QString devicePath = "/sys/class/hidraw/" + device + "/device/uevent";

        QFile file(devicePath);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&file);
            while (!in.atEnd()) {
                QString line = in.readLine();
                if (line.isEmpty()) {
                    break;
                }
                if (line.contains("HID_NAME")) {
                    // Check if this is the device you are looking for
                    if (line.contains("Openterface")) { // Replace "YourDeviceName" with the actual device name
                        return "/dev/" + device;
                    }
                }
            }
        }
    }
    return QString();
}

bool VideoHid::sendFeatureReportLinux(uint8_t* reportBuffer, int bufferSize) {
    int fd = open(getHIDDevicePath().toStdString().c_str(), O_RDWR);
    if (fd < 0) {
        qDebug() << "Invalid file descriptor for sending feature report.";
        return false;
    }

    std::vector<uint8_t> buffer(bufferSize);
    std::copy(reportBuffer, reportBuffer + bufferSize, buffer.begin());
    int res = ioctl(fd, HIDIOCSFEATURE(buffer.size()), buffer.data());

    if (res < 0) {
        qDebug() << "Failed to send feature report.";
        return false;
    }

    return true;
}

bool VideoHid::getFeatureReportLinux(uint8_t* reportBuffer, int bufferSize) {
    //qDebug() << "Getting feature report, path: " << getHIDDevicePath();
    int fd = open(getHIDDevicePath().toStdString().c_str(), O_RDWR);
    if (fd < 0) {
        qDebug() << "Failed to open HID device.";
        return false;
    }

    std::vector<uint8_t> buffer(bufferSize);
    int res = ioctl(fd, HIDIOCGFEATURE(buffer.size()), buffer.data());

    //qDebug() << "Feature report size: " << buffer.size() << " data: " << QByteArray((char*)buffer.data(), buffer.size()).toHex();

    if (res < 0) {
        qDebug() << "Failed to get feature report.";
        return false;
    }

    std::copy(buffer.begin(), buffer.end(), reportBuffer);
    return true;
}
#endif
