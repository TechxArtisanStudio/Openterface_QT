#include "videohid.h"

#include <QDebug>
#include <QDir>
#include <QTimer>
#include <QThread>

#include "ms2109.h"
#include "firmwarewriter.h"
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
                auto fps = getFps();
                auto pixclk = getPixelclk();
                auto aspectRatio = resolution.second ? static_cast<float>(resolution.first) / resolution.second : 0;
                GlobalVar::instance().setInputAspectRatio(aspectRatio);

                if (pixclk > 185) {
                    resolution.first *= 2;
                    resolution.second *= 2;
                }

                if(GlobalVar::instance().getInputWidth()!=resolution.first || GlobalVar::instance().getInputHeight()!=resolution.second){
                    emit inputResolutionChanged(GlobalVar::instance().getInputWidth(), GlobalVar::instance().getInputHeight(), resolution.first, resolution.second);
                }
                // qDebug() << "onResolutionChange: " << resolution.first << "x" << resolution.second << "fps: " << fps << " pixclk: " << pixclk << "MHz, aspectRatio: " << aspectRatio;
                // eventCallback->onResolutionChange(resolution.first, resolution.second, fps);
                emit resolutionChangeUpdate(resolution.first, resolution.second, fps, pixclk);
            }else{
                emit resolutionChangeUpdate(0, 0, 0, 0);
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


/*
Get the input resolution from capture card. 
*/
QPair<int, int> VideoHid::getResolution() {
    quint8 width_h = static_cast<quint8>(usbXdataRead4Byte(ADDR_INPUT_WIDTH_H).first.at(0));
    quint8 width_l = static_cast<quint8>(usbXdataRead4Byte(ADDR_INPUT_WIDTH_L).first.at(0));
    quint16 width = (width_h << 8) + width_l;
    quint8 height_h = static_cast<quint8>(usbXdataRead4Byte(ADDR_INPUT_HEIGHT_H).first.at(0));
    quint8 height_l = static_cast<quint8>(usbXdataRead4Byte(ADDR_INPUT_HEIGHT_L).first.at(0));
    quint16 height = (height_h << 8) + height_l;
    return qMakePair(width, height);
}

float VideoHid::getFps() {
    quint8 fps_h = static_cast<quint8>(usbXdataRead4Byte(ADDR_INPUT_FPS_H).first.at(0));
    quint8 fps_l = static_cast<quint8>(usbXdataRead4Byte(ADDR_INPUT_FPS_L).first.at(0));
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

float VideoHid::getPixelclk() {
    quint8 clk_h = static_cast<quint8>(usbXdataRead4Byte(ADDR_INPUT_PIXELCLK_H).first.at(0));
    quint8 clk_l = static_cast<quint8>(usbXdataRead4Byte(ADDR_INPUT_PIXELCLK_L).first.at(0));
    return ((clk_h << 8) + clk_l)/100.0;
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



void VideoHid::fetchBinFileToString(QString &url, int timeoutMs){
    QNetworkAccessManager manager; // Create a network access manager
    QNetworkRequest request(url);  // Set up the request with the given URL
    QNetworkReply *reply = manager.get(request); // Send a GET request

    // Use QEventLoop to wait for the request to complete
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec(); // Block until the request finishes

    std::string result; // Initialize an empty std::string to hold the result
    if (reply->error() == QNetworkReply::NoError) {
        // Read the binary data into a QByteArray
        QByteArray data = reply->readAll();
        // Convert QByteArray to std::string
        result = std::string(data.constData(), data.size());
        networkFirmware.assign(data.begin(), data.end());
        qDebug() << "Successfully read file, size:" << data.size() << "bytes";
    } else {
        qDebug() << "Failed to fetch:" << reply->errorString();
    }
    int version_0 = result.length() > 12 ? (unsigned char)result[12] : 0;
    int version_1 = result.length() > 13 ? (unsigned char)result[13] : 0;
    int version_2 = result.length() > 14 ? (unsigned char)result[14] : 0;
    int version_3 = result.length() > 15 ? (unsigned char)result[15] : 0;
    m_firmwareVersion = QString("%1%2%3%4").arg(version_0, 2, 10, QChar('0'))
                                            .arg(version_1, 2, 10, QChar('0'))
                                            .arg(version_2, 2, 10, QChar('0'))
                                            .arg(version_3, 2, 10, QChar('0')).toStdString();
    reply->deleteLater(); // Clean up the reply object
}

QString VideoHid::getLatestFirmwareFilenName(QString &url, int timeoutMs){
    QNetworkAccessManager manager;
    QNetworkRequest request(url);
    QNetworkReply *reply = manager.get(request);

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    
    timer.start(timeoutMs); // Set the timeout duration (in milliseconds)
    loop.exec();

    QString result;
    if (reply->isFinished() && reply->error() == QNetworkReply::NoError) {
        result = QString::fromUtf8(reply->readAll());
    } else if (!reply->isFinished()) {
        qDebug() << "Request time out";
        reply->abort(); // 超时后中止请求
    } else {
        qDebug() << "fail to get file name" << reply->errorString();
    }

    reply->deleteLater();
    return result;
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

    ctrlData[1] = CMD_XDATA_WRITE;
    ctrlData[2] = static_cast<char>((u16_address >> 8) & 0xFF);
    ctrlData[3] = static_cast<char>(u16_address & 0xFF);
    ctrlData.replace(4, 4, data);

    qDebug() << "usbXdataWrite4Byte: " << ctrlData.toHex();

    return this->sendFeatureReport((uint8_t*)ctrlData.data(), ctrlData.size());
}

bool VideoHid::getFeatureReport(uint8_t* buffer, size_t bufferLength, bool autoCloseHandle) {
#ifdef _WIN32
    return this->getFeatureReportWindows(buffer, bufferLength, autoCloseHandle);
#elif __linux__
    return this->getFeatureReportLinux(buffer, bufferLength, autoCloseHandle);
#endif
}

bool VideoHid::getFeatureReport(uint8_t* buffer, size_t bufferLength) {
    return this->getFeatureReport(buffer, bufferLength, true);
}

bool VideoHid::sendFeatureReport(uint8_t* buffer, size_t bufferLength, bool autoCloseHandle) {
#ifdef _WIN32
    int retries = 2;
    while (retries-- > 0) {
        if (sendFeatureReportWindows(buffer, bufferLength, autoCloseHandle)) {
            return true;
        }
        qDebug() << "Retrying sendFeatureReportWindows...";
    }
    return false;
#elif __linux__
    // implementation
    int retries = 2;
    while (retries-- > 0) {
        if (sendFeatureReportLinux(buffer, bufferLength, autoCloseHandle)) {
            return true;
        }
        qDebug() << "Retrying sendFeatureReportLinux...";
    }
    return false;
#endif
}

bool VideoHid::sendFeatureReport(uint8_t* buffer, size_t bufferLength) {
    return this->sendFeatureReport(buffer, bufferLength, true);
}

void VideoHid::closeHIDDeviceHandle() {
    #ifdef _WIN32
        if (deviceHandle != INVALID_HANDLE_VALUE) {
            CloseHandle(deviceHandle);
            deviceHandle = INVALID_HANDLE_VALUE;
        }
    #elif __linux__
        if (hidFd >= 0) {
            close(hidFd);
            hidFd = -1;
        }
    #endif
}

#ifdef _WIN32
std::wstring VideoHid::getHIDDevicePath() {
    // Check if we have a cached path that's still valid
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - m_lastPathQuery).count();
    
    if (!m_cachedDevicePath.empty() && elapsed < 10) {
        // Return cached path if it's less than 10 seconds old
        return m_cachedDevicePath;
    }
    
    // Update the last query time
    m_lastPathQuery = now;
    
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
                        
                        // Cache the found device path
                        m_cachedDevicePath = devicePath;
                        
                        return devicePath; // Found the device
                    }
                }
                CloseHandle(deviceHandle);
            }
        }
        free(deviceInterfaceDetailData);
    }

    SetupDiDestroyDeviceInfoList(deviceInfoSet);
    
    // Clear the cache if no device was found
    m_cachedDevicePath.clear();
    
    return L""; // Device not found
}

bool VideoHid::sendFeatureReportWindows(BYTE* reportBuffer, DWORD bufferSize, bool autoCloseHandle) {
    if (!openHIDDeviceHandle()) {
        qDebug() << "Failed to open device handle for sending feature report.";
        return false;
    }

    // Send the Set Feature Report request
    bool result = HidD_SetFeature(deviceHandle, reportBuffer, bufferSize);

    if (!result) {
        qDebug() << "Failed to send feature report.";
        if (autoCloseHandle) {
            closeHIDDeviceHandle();
        }
        return false;
    }

    if (autoCloseHandle) {
        closeHIDDeviceHandle();
    }
    return true;
}

// Overload method to maintain existing calls
bool VideoHid::sendFeatureReportWindows(BYTE* reportBuffer, DWORD bufferSize) {
    return sendFeatureReportWindows(reportBuffer, bufferSize, true);
}

bool VideoHid::openHIDDeviceHandle() {
    if (deviceHandle == INVALID_HANDLE_VALUE) {
        deviceHandle = CreateFileW(getHIDDevicePath().c_str(),
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
    }
    return true;
}

bool VideoHid::getFeatureReportWindows(BYTE* reportBuffer, DWORD bufferSize, bool autoCloseHandle) {
    if (!openHIDDeviceHandle()) {
        qDebug() << "Failed to open device handle for getting feature report.";
        return false;
    }

    // Send the Get Feature Report request
    bool result = HidD_GetFeature(deviceHandle, reportBuffer, bufferSize);

    if (!result) {
        qDebug() << "Failed to get feature report.";
    }

    if (autoCloseHandle) {
        closeHIDDeviceHandle();
    }

    return result;
}

bool VideoHid::getFeatureReportWindows(BYTE* reportBuffer, DWORD bufferSize) {
    return getFeatureReportWindows(reportBuffer, bufferSize, true);
}

#elif __linux__
QString VideoHid::getHIDDevicePath() {
    // Check if we have a cached path that's still valid
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - m_lastPathQuery).count();
    
    if (!m_cachedDevicePath.isEmpty() && elapsed < 10) {
        // Return cached path if it's less than 10 seconds old
        return m_cachedDevicePath;
    }
    
    // Update the last query time
    m_lastPathQuery = now;
    
    QDir dir("/sys/class/hidraw");

    QStringList hidrawDevices = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);

    if (hidrawDevices.isEmpty()) {
        qDebug() << "No Openterface device found.";
        
        // Clear the cache if no devices found
        m_cachedDevicePath.clear();
        
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
                        QString foundPath = "/dev/" + device;
                        
                        // Cache the found device path
                        m_cachedDevicePath = foundPath;
                        
                        return foundPath;
                    }
                }
            }
        } else {
            qDebug() << "Failed to open device path: " << devicePath;
        }   
    }

    // Clear the cache if no device was found
    m_cachedDevicePath.clear();
    
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

bool VideoHid::sendFeatureReportLinux(uint8_t* reportBuffer, int bufferSize, bool autoCloseHandle) {
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

    if (autoCloseHandle) {
        closeHIDDeviceHandle();
    }

    return true;
}

bool VideoHid::sendFeatureReportLinux(uint8_t* reportBuffer, int bufferSize) {
    return sendFeatureReportLinux(reportBuffer, bufferSize, true);
}

bool VideoHid::getFeatureReportLinux(uint8_t* reportBuffer, int bufferSize, bool autoCloseHandle) {
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

    if (autoCloseHandle) {
        closeHIDDeviceHandle();
    }
    return true;
}

bool VideoHid::getFeatureReportLinux(uint8_t* reportBuffer, int bufferSize) {
    return getFeatureReportLinux(reportBuffer, bufferSize, true);
}
#endif

bool VideoHid::writeChunk(quint16 address, const QByteArray &data) {
    const int chunkSize = 1;
    const int REPORT_SIZE = 9;

    int length = data.size();

    quint16 _address = address;
    bool status = false;
    for (int i = 0; i < length; i += chunkSize) {
        QByteArray chunk = data.mid(i, chunkSize);
        int chunk_length = chunk.size();
        QByteArray report(REPORT_SIZE, 0);
        report[1] = CMD_EEPROM_WRITE;
        report[2] = (_address >> 8) & 0xFF;
        report[3] = _address & 0xFF;
        report.replace(4, chunk_length, chunk);
        qDebug() << "Report:" << report.toHex(' ').toUpper();
        status = sendFeatureReport((uint8_t*)report.data(), report.size(), false);
        if (!status) {
            qWarning() << "Failed to write chunk to address:" << QString("0x%1").arg(_address, 4, 16, QChar('0'));
            return false;
        }
        written_size += chunk_length;
        emit firmwareWriteChunkComplete(written_size); // Add this line
        _address += chunkSize;
    }
    return true;
}

bool VideoHid::writeEeprom(quint16 address, const QByteArray &data) {
    const int MAX_CHUNK = 16;
    QByteArray remainingData = data;
    written_size = 0;
    while (!remainingData.isEmpty()) {
        QByteArray chunk = remainingData.left(MAX_CHUNK);
        if(!writeChunk(address, chunk)){
            return false;
        }
        address += chunk.size();
        remainingData = remainingData.mid(MAX_CHUNK);
        if (written_size % 64 == 0) {
            qDebug() << "Written size:" << written_size;
        }
        QThread::msleep(100); // Add 10ms delay between writes
    }
    closeHIDDeviceHandle();
    return true;
}

void VideoHid::loadFirmwareToEeprom() {
    // Create firmware data
    if (networkFirmware.empty()){
        return;
    }

    QByteArray firmware(reinterpret_cast<const char*>(networkFirmware.data()), networkFirmware.size());
    
    // Create a worker thread to handle firmware writing
    QThread* thread = new QThread();
    FirmwareWriter* worker = new FirmwareWriter(this, ADDR_EEPROM, firmware);
    worker->moveToThread(thread);
    
    // Connect signals/slots
    connect(thread, &QThread::started, worker, &FirmwareWriter::process);
    connect(worker, &FirmwareWriter::finished, thread, &QThread::quit);
    connect(worker, &FirmwareWriter::finished, worker, &FirmwareWriter::deleteLater);
    connect(thread, &QThread::finished, thread, &QThread::deleteLater);
    
    // Connect progress and status signals if needed
    connect(worker, &FirmwareWriter::progress, this, [this](int percent) {
        qDebug() << "Firmware write progress: " << percent << "%";
        emit firmwareWriteProgress(percent);
    });
    
    connect(worker, &FirmwareWriter::finished, this, [this](bool success) {
        qDebug() << "Firmware write " << (success ? "completed successfully" : "failed");
        emit firmwareWriteComplete(success);
    });
    
    // Start the thread
    thread->start();
}

bool VideoHid::isLatestFirmware() {
    QString firemwareFileName = getLatestFirmwareFilenName(firmwareURL);
    QString newURL = firmwareURL.replace("minikvm_latest_firmware.txt", firemwareFileName);
    fetchBinFileToString(newURL);
    qDebug() << "Firmware version:" << QString::fromStdString(getFirmwareVersion());
    qDebug() << "Lateset firmware version:" << QString::fromStdString(m_firmwareVersion);
    return getFirmwareVersion() == m_firmwareVersion;
}

