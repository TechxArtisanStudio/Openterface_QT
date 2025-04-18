#ifndef VIDEOHID_H
#define VIDEOHID_H

#include <QObject>
#include <QTimer>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QEventLoop>
#include <vector>
#include <chrono>

#include "../ui/statusevents.h"
#ifdef _WIN32
#include <windows.h> 
#elif __linux__
#include <linux/hid.h>
#endif

class FirmwareWriter; // Forward declaration

class VideoHid : public QObject
{
    Q_OBJECT
    
    friend class FirmwareWriter; // Add FirmwareWriter as friend
    
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
    inline std::string getLatestFirmwareVersion(){ return m_firmwareVersion;}
    
    QString getLatestFirmwareFilenName(QString &url, int timeoutMs = 5000);
    void fetchBinFileToString(QString &url, int timeoutMs = 5000);

    bool isLatestFirmware();

    void switchToHost();
    void switchToTarget();

    void setEventCallback(StatusEventCallback* callback);
    void clearDevicePathCache();
    
    // Add declarations for openHIDDevice and closeHIDDevice
    bool openHIDDevice();

    // HDMI timing Pixel clock
    float getPixelclk();

    void loadFirmwareToEeprom();

    // Transaction-based HID access
    bool beginTransaction();
    void endTransaction();
    bool isInTransaction() const;

signals:
    // Add new signals
    void firmwareWriteProgress(int percent);
    void firmwareWriteComplete(bool success);
    void firmwareWriteChunkComplete(int writtenBytes);

    void inputResolutionChanged(int old_input_width, int old_input_height, int new_input_width, int new_input_height);

    void resolutionChangeUpdate(const int& width, const int& height, const float& fps, const float& pixelClk);
    void firmwareWriteError(const QString &errorMessage);
    
private:
    explicit VideoHid(QObject *parent = nullptr);
    std::vector<unsigned char> networkFirmware;
    std::string m_firmwareVersion;

#ifdef _WIN32
    HANDLE deviceHandle = INVALID_HANDLE_VALUE;
    std::wstring m_cachedDevicePath;
#elif __linux__
    int hidFd = -1;
    QString m_cachedDevicePath;
#endif

    bool openHIDDeviceHandle();
    void closeHIDDeviceHandle();
    using StringCallback = std::function<void(const QString&)>;
    QTimer *timer;
    QString firmwareURL = "http://download.openterface.com/openterface/firmware/minikvm_latest_firmware.txt";
    QString extractPortNumberFromPath(const QString& path);
    QPair<QByteArray, bool> usbXdataRead4Byte(quint16 u16_address);
    bool usbXdataWrite4Byte(quint16 u16_address, QByteArray data);
    QString devicePath;
    bool isHardSwitchOnTarget = false;
    
    StatusEventCallback* eventCallback = nullptr;

    bool getFeatureReport(uint8_t* buffer, size_t bufferLength);
    bool sendFeatureReport(uint8_t* buffer, size_t bufferLength);

    bool writeChunk(quint16 address, const QByteArray &data);
    bool writeEeprom(quint16 address, const QByteArray &data);
    uint16_t written_size = 0;

#ifdef _WIN32
    std::wstring getHIDDevicePath();
    bool sendFeatureReportWindows(BYTE* reportBuffer, DWORD bufferSize);
    bool getFeatureReportWindows(BYTE* reportBuffer, DWORD bufferSize);
#elif __linux__
    QString getHIDDevicePath();
    bool sendFeatureReportLinux(uint8_t* reportBuffer, int bufferSize);
    bool getFeatureReportLinux(uint8_t* reportBuffer, int bufferSize);
#endif

    std::chrono::time_point<std::chrono::steady_clock> m_lastPathQuery = std::chrono::steady_clock::now();
    bool m_inTransaction = false;
};

#endif // VIDEOHID_H
