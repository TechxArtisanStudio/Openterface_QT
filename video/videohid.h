#ifndef VIDEOHID_H
#define VIDEOHID_H

#include <QObject>
#include <QTimer>

#include "../ui/statusevents.h"
#ifdef _WIN32
#include <windows.h> 
#elif __linux__
#include <linux/hid.h>
#endif

class VideoHid : public QObject
{
public:
    static VideoHid& getInstance()
    {
        static VideoHid instance; // Guaranteed to be destroyed.
            // Instantiated on first use.
        return instance;
    }


    VideoHid(VideoHid const&) = delete;             // Copy construct
    void operator=(VideoHid const&)  = delete; // Copy assign

    void start();
    void stop();

    //get resolution
    QPair<int, int> getResolution();
    float getFps();
    //Gpio0 bit0 reads the hard switch status, 1 means switchable usb connects to the host, 0 means switchable usb connects to the device
    bool getGpio0();
    //Spdifout bit5 reads the soft switch status, 1 means switchable usb connects to the host, 0 means switchable usb connects to the device
    void setSpdifout(bool enable);
    bool getSpdifout();

    bool isHdmiConnected();
    std::string getFirmwareVersion();

    void switchToHost();
    void switchToTarget();

    void setEventCallback(StatusEventCallback* callback);
    void clearDevicePathCache();
    
    // Add declarations for openHIDDevice and closeHIDDevice
    bool openHIDDevice();
    void closeHIDDevice();

private:
    explicit VideoHid(QObject *parent = nullptr);

    QTimer *timer;

    QString extractPortNumberFromPath(const QString& path);
    QPair<QByteArray, bool> usbXdataRead4Byte(quint16 u16_address);
    bool usbXdataWrite4Byte(quint16 u16_address, QByteArray data);
    QString devicePath;
    bool isHardSwitchOnTarget = false;
    
    StatusEventCallback* eventCallback = nullptr;

    bool getFeatureReport(uint8_t* buffer, size_t bufferLength);
    bool sendFeatureReport(uint8_t* buffer, size_t bufferLength);

#ifdef _WIN32
    std::wstring m_cachedDevicePath;
    std::wstring getHIDDevicePath();
    bool sendFeatureReportWindows(uint8_t* reportBuffer, DWORD bufferSize);
    bool getFeatureReportWindows(uint8_t* reportBuffer, DWORD bufferSize);
#elif __linux__
    QString m_cachedDevicePath;
    QString getHIDDevicePath();
    bool sendFeatureReportLinux(uint8_t* reportBuffer, int bufferSize);
    bool getFeatureReportLinux(uint8_t* reportBuffer, int bufferSize);
    int hidFd = -1; // Add the file descriptor for Linux
#endif

};

#endif // VIDEOHID_H
