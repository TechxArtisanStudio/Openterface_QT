#include "videohid.h"

#include <QDebug>
#include <QDir>
#include <QTimer>

#include "ms2109.h"
#include "../global.h"

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
    std::string captureCardFirmwareVersion = getFirmwareVersion();
    qDebug() << "MS2109 firmware VERSION:" << QString::fromStdString(captureCardFirmwareVersion);    //firmware VERSION
    GlobalVar::instance().setCaptureCardFirmwareVersion(captureCardFirmwareVersion);
    isHardSwitchOnTarget = getSpdifout();
    qDebug() << "SPDIFOUT:" << isHardSwitchOnTarget;    //SPDIFOUT
    if(eventCallback){
        eventCallback->onSwitchableUsbToggle(isHardSwitchOnTarget);
        setSpdifout(isHardSwitchOnTarget); //Follow the hard switch by default
    }

    //start a timer to get the HDMI connection status every 1 second
    timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [=](){
        bool currentSwitchOnTarget = getGpio0();

        if(eventCallback){
            if(isHdmiConnected()){
                auto resolution = getResolution();
                eventCallback->onResolutionChange(resolution.first, resolution.second, getFps());
            }else{
                eventCallback->onResolutionChange(0, 0, 0);
            }

            if(isHardSwitchOnTarget != currentSwitchOnTarget){ //Only handle change when hardware switch change
                qDebug() << "isHardSwitchOnTarget" << isHardSwitchOnTarget << "currentSwitchOnTarget" << currentSwitchOnTarget;
                eventCallback->onSwitchableUsbToggle(currentSwitchOnTarget);
                setSpdifout(currentSwitchOnTarget); 
                isHardSwitchOnTarget = currentSwitchOnTarget;
            }
        }
    });
    timer->start(1000);
}

void VideoHid::stop() {
    qDebug() << "Stopping VideoHid timer.";
    if (timer) {
        timer->stop();
        disconnect(timer, &QTimer::timeout, this, nullptr);
        delete timer;
        timer = nullptr;
    }
}

QPair<int, int> VideoHid::getResolution() {
    quint8 width_h = static_cast<quint8>(usbXdataRead4Byte(ADDR_WIDTH_H).first.at(0));
    quint8 width_l = static_cast<quint8>(usbXdataRead4Byte(ADDR_WIDTH_L).first.at(0));
    quint16 width = (width_h << 8) + width_l;
    quint8 height_h = static_cast<quint8>(usbXdataRead4Byte(ADDR_HEIGHT_H).first.at(0));
    quint8 height_l = static_cast<quint8>(usbXdataRead4Byte(ADDR_HEIGHT_L).first.at(0));
    quint16 height = (height_h << 8) + height_l;
    return qMakePair(width, height);
}

float VideoHid::getFps() {
    quint8 fps_h = static_cast<quint8>(usbXdataRead4Byte(ADDR_FPS_H).first.at(0));
    quint8 fps_l = static_cast<quint8>(usbXdataRead4Byte(ADDR_FPS_L).first.at(0));
    quint16 fps = (fps_h << 8) + fps_l;
    return static_cast<float>(fps) / 100;
}

/*
 * Address: 0xDF00 bit0 indicates the hard switch status,
 * true means switchable usb connects to the target,
 * false means switchable usb connects to the host
 */
bool VideoHid::getGpio0() {
    return usbXdataRead4Byte(ADDR_GPIO0).first.at(0) & 0x01;
}

bool VideoHid::getSpdifout() {
    int bit = 1;
    int mask = 0xFE;
    if (GlobalVar::instance().getCaptureCardFirmwareVersion() < "24081309") {
        qDebug() << "Firmware version is less than 24081309";
        bit = 0x10;
        mask = 0xEF;
    }
    return usbXdataRead4Byte(ADDR_GPIO0).first.at(0) & mask >> bit;
}

void VideoHid::switchToHost() {
    qDebug() << "Switch to host";
    setSpdifout(false);
    GlobalVar::instance().setSwitchOnTarget(false);
    if(eventCallback) eventCallback->onSwitchableUsbToggle(false);
}

void VideoHid::switchToTarget() {
    qDebug() << "Switch to target";
    setSpdifout(true);
    GlobalVar::instance().setSwitchOnTarget(true);
    if(eventCallback) eventCallback->onSwitchableUsbToggle(true);
}

/*
 * Address: 0xDF01 bitn indicates the soft switch status, the firmware before 24081309, it's bit5, after 24081309, it's bit0
 * true means switchable usb connects to the target,
 * false means switchable usb connects to the host
 */
void VideoHid::setSpdifout(bool enable) {
    int bit = 1;
    int mask = 0xFE;
    if (GlobalVar::instance().getCaptureCardFirmwareVersion() < "24081309") {
        qDebug() << "Firmware version is less than 24081309";
        bit = 0x10;
        mask = 0xEF;
    }

    quint8 spdifout = static_cast<quint8>(usbXdataRead4Byte(ADDR_SPDIFOUT).first.at(0));
    if (enable) {
        spdifout |= bit;
    } else {
        spdifout &= mask;
    }
    QByteArray data(4, 0); // Create a 4-byte array initialized to zero
    data[0] = spdifout;
    if(usbXdataWrite4Byte(ADDR_SPDIFOUT, data)){
        qDebug() << "SPDIFOUT set successfully";
    }else{
        qDebug() << "SPDIFOUT set failed";
    }
}

std::string VideoHid::getFirmwareVersion() {
    bool ok;
    int version_0 = usbXdataRead4Byte(ADDR_FIRMWARE_VERSION_0).first.toHex().toInt(&ok, 16);
    int version_1 = usbXdataRead4Byte(ADDR_FIRMWARE_VERSION_1).first.toHex().toInt(&ok, 16);
    int version_2 = usbXdataRead4Byte(ADDR_FIRMWARE_VERSION_2).first.toHex().toInt(&ok, 16);
    int version_3 = usbXdataRead4Byte(ADDR_FIRMWARE_VERSION_3).first.toHex().toInt(&ok, 16);
    return QString("%1%2%3%4").arg(version_0, 2, 10, QChar('0'))
                              .arg(version_1, 2, 10, QChar('0'))
                              .arg(version_2, 2, 10, QChar('0'))
                              .arg(version_3, 2, 10, QChar('0')).toStdString();
}

/*
 * Address: 0xFA8C bit0 indicates the HDMI connection status
 */
bool VideoHid::isHdmiConnected() {
    return usbXdataRead4Byte(ADDR_HDMI_CONNECTION_STATUS).first.at(0) & 0x01;
}

void VideoHid::setEventCallback(StatusEventCallback* callback) {
    eventCallback = callback;
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

bool VideoHid::usbXdataWrite4Byte(quint16 u16_address, QByteArray data) {
    QByteArray ctrlData(9, 0); // Initialize with 9 bytes set to 0

    ctrlData[1] = 0xB6;
    ctrlData[2] = static_cast<char>((u16_address >> 8) & 0xFF);
    ctrlData[3] = static_cast<char>(u16_address & 0xFF);
    ctrlData.replace(4, 4, data);

    qDebug() << "usbXdataWrite4Byte: " << ctrlData.toHex();

    return this->sendFeatureReport((uint8_t*)ctrlData.data(), ctrlData.size());
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

    if (hidrawDevices.isEmpty()) {
        qDebug() << "No Openterface device found.";
        return QString();
    }

    foreach (const QString &device, hidrawDevices) {\
        QString devicePath = "/sys/class/hidraw/" + device + "/device/uevent";

        QFile file(devicePath);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&file);
            
            while (!in.atEnd()) {
                QString line = in.readLine();
                qDebug() << "Line: " << line;
                if (line.isEmpty()) {
                    break;
                }
                if (line.contains("HID_NAME")) {
                    // Check if this is the device you are looking for
                    if (line.contains("Openterface")) {
                        return "/dev/" + device;
                    }
                }
            }
        } else {
            qDebug() << "Failed to open device path: " << devicePath;
        }   
    }

    return QString();
}

// Open the HID device and cache the file descriptor
bool VideoHid::openHIDDevice() {
    if (hidFd < 0) {
        QString devicePath = getHIDDevicePath();
        hidFd = open(devicePath.toStdString().c_str(), O_RDWR);
        if (hidFd < 0) {
            qDebug() << "Failed to open HID device (" << devicePath << "). Error:" << strerror(errno);
            return false;
        }
    }
    return true;
}

// Close the cached file descriptor
void VideoHid::closeHIDDevice() {
    if (hidFd >= 0) {
        close(hidFd);
        hidFd = -1;
    }
}

bool VideoHid::sendFeatureReportLinux(uint8_t* reportBuffer, int bufferSize) {
    if (!openHIDDevice()) {
        return false;
    }

    std::vector<uint8_t> buffer(bufferSize);
    std::copy(reportBuffer, reportBuffer + bufferSize, buffer.begin());
    int res = ioctl(hidFd, HIDIOCSFEATURE(buffer.size()), buffer.data());

    if (res < 0) {
        qDebug() << "Failed to send feature report. Error:" << strerror(errno);
        return false;
    }

    return true;
}

bool VideoHid::getFeatureReportLinux(uint8_t* reportBuffer, int bufferSize) {
    if (!openHIDDevice()) {
        return false;
    }

    std::vector<uint8_t> buffer(bufferSize);
    int res = ioctl(hidFd, HIDIOCGFEATURE(buffer.size()), buffer.data());

    if (res < 0) {
        qDebug() << "Failed to get feature report. Error:" << strerror(errno);
        return false;
    }

    std::copy(buffer.begin(), buffer.end(), reportBuffer);
    return true;
}
#endif
