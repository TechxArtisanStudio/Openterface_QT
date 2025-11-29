#ifndef VIDEOHID_H
#define VIDEOHID_H

#include <QObject>
#include <QTimer>
#include <atomic>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QEventLoop>
#include <vector>
#include <chrono>
#include <QMutex>
#include <QRecursiveMutex>

#include "../ui/statusevents.h"

// Chipset enumeration
enum class VideoChipType {
    MS2109,
    MS2109S,
    MS2130S,
    UNKNOWN
};

// Helper struct containing addresses for all relevant input registers for VideoHid
struct VideoHidRegisterSet {
    quint16 width_h{0};
    quint16 width_l{0};
    quint16 height_h{0};
    quint16 height_l{0};
    quint16 fps_h{0};
    quint16 fps_l{0};
    quint16 clk_h{0};
    quint16 clk_l{0};
};

// Helper struct containing the read values for resolution and timing for VideoHid
struct VideoHidResolutionInfo {
    quint32 width{0};
    quint32 height{0};
    float fps{0.0f};
    float pixclk{0.0f};
    bool hdmiConnected{false};
};
#ifdef _WIN32
#include <windows.h>
#elif __linux__
#include <linux/hid.h>
#endif

class FirmwareWriter; // Forward declaration
class FirmwareReader; // Forward declaration

enum class FirmwareResult {
    Latest,
    Upgradable,
    Timeout,
    CheckSuccess,
    CheckFailed,
    Checking
};

class VideoHid : public QObject
{
    Q_OBJECT

    friend class FirmwareWriter; // Add FirmwareWriter as friend
    friend class FirmwareReader; // Add FirmwareReader as friend

public:
    static VideoHid* getPointer(){
        static VideoHid *pointer;
        return pointer;
    }
    static VideoHid& getInstance()
    {
        static VideoHid instance; // Guaranteed to be destroyed.
        return instance;
    }

    VideoHid(VideoHid const&) = delete;             // Copy construct
    void operator=(VideoHid const&) = delete; // Copy assign

    void start();
    void stop();

    // Get resolution
    QPair<int, int> getResolution();
    float getFps();
    // Gpio0 bit0 reads the hard switch status, 1 means switchable usb connects to the host, 0 means switchable usb connects to the device
    bool getGpio0();
    // Spdifout bit5 reads the soft switch status, 1 means switchable usb connects to the host, 0 means switchable usb connects to the device
    void setSpdifout(bool enable);
    bool getSpdifout();

    bool isHdmiConnected();
    std::string getFirmwareVersion();
    inline std::string getLatestFirmwareVersion(){ return m_firmwareVersion;}
    inline std::string getCurrentFirmwareVersion(){ return m_currentfirmwareVersion;}

    FirmwareResult fireware_result;
    QString getLatestFirmwareFilenName(QString &url, int timeoutMs = 5000);
    void fetchBinFileToString(QString &url, int timeoutMs = 5000);

    FirmwareResult isLatestFirmware();

    void switchToHost();
    void switchToTarget();
    
    // New USB switch status query method using serial command (for CH32V208 chips)
    int getUsbStatusViaSerial();  // Returns: 0=host, 1=target, -1=error

    void setEventCallback(StatusEventCallback* callback);
    void clearDevicePathCache();
    void refreshHIDDevice();

    // Helper method to find matching HID device by port chain
    QString findMatchingHIDDevice(const QString& portChain) const;
    
    // HID device switching by port chain (similar to CameraManager)
    bool switchToHIDDeviceByPortChain(const QString& portChain);
    QString getCurrentHIDDevicePath() const;
    QString getCurrentHIDPortChain() const;

    // Add declarations for openHIDDevice and closeHIDDevice
    bool openHIDDevice();

    // HDMI timing Pixel clock
    float getPixelclk();
    
    // USB read/write methods for both chip types
    QPair<QByteArray, bool> usbXdataRead4ByteMS2109(quint16 u16_address);
    QPair<QByteArray, bool> usbXdataRead4ByteMS2130S(quint16 u16_address);

    void loadFirmwareToEeprom();

    void loadEepromToFile(const QString &filePath);

    quint32 readFirmwareSize();

    // Read firmware from EEPROM
    QByteArray readEeprom(quint16 address, quint32 size);

    // Transaction-based HID access
    bool beginTransaction();
    void endTransaction();
    bool isInTransaction() const;
    
    // Hotplug monitoring integration
    void connectToHotplugMonitor();
    void disconnectFromHotplugMonitor();

    // This method is invokable so we can safely dispatch eventCallback calls to the object's thread
    Q_INVOKABLE void dispatchSwitchableUsbToggle(bool isToTarget);

signals:
    // Add new signals
    int safe_stoi(std::string str, int defaultValue = 0);
    void firmwareWriteProgress(int percent);
    void firmwareWriteComplete(bool success);
    void firmwareWriteChunkComplete(int writtenBytes);
    void firmwareReadProgress(int percent);
    void firmwareReadComplete(bool success);
    void firmwareReadChunkComplete(int readBytes);
    void inputResolutionChanged(int old_input_width, int old_input_height, int new_input_width, int new_input_height);
    void resolutionChangeUpdate(const int& width, const int& height, const float& fps, const float& pixelClk);
    void firmwareWriteError(const QString &errorMessage);
    void firmwareReadError(const QString &errorMessage);
    void hidDeviceChanged(const QString& oldDevicePath, const QString& newDevicePath);
    void hidDeviceSwitched(const QString& fromPortChain, const QString& toPortChain);
    void hidDeviceConnected(const QString& devicePath);
    void hidDeviceDisconnected(const QString& devicePath);

private:
    explicit VideoHid(QObject *parent = nullptr);
    ~VideoHid();
    std::vector<unsigned char> networkFirmware;
    std::string m_firmwareVersion;
    std::string m_currentfirmwareVersion;
    
    // Helper method to start the monitoring timer
    void startMonitoringTimer();

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
    // Polling thread used to poll device status periodically. Replaces the previous QTimer-based approach.
    class PollingThread; // forward-declared below in the cpp file (no moc required)
    friend class PollingThread; // allow the nested polling thread to access private members
    PollingThread *m_pollingThread{nullptr};
    int m_pollIntervalMs{1000};
    QString firmwareURL = "https://assets.openterface.com/openterface/firmware/minikvm_latest_firmware.txt";
    QString extractPortNumberFromPath(const QString& path);
    QPair<QByteArray, bool> usbXdataRead4Byte(quint16 u16_address);
    bool usbXdataWrite4Byte(quint16 u16_address, QByteArray data);
    
    // Safe wrapper to read a single byte from USB Xdata - prevents crash on empty arrays
    quint8 safeReadByte(quint16 u16_address, quint8 defaultValue = 0);

    // Register set retrieval for the current chip
    VideoHidRegisterSet getRegisterSetForCurrentChip() const;

    // Read the full input status (resolution/fps/pixel clock) into a struct
    VideoHidResolutionInfo getInputStatus();

    // Normalize resolution based on chip specifics and pixel clock
    void normalizeResolution(VideoHidResolutionInfo &info);

    // A safe single-byte register reader used by high-level helpers
    quint8 readRegisterSafe(quint16 addr, quint8 defaultValue = 0, const QString& tag = QString());

    // A safe write helper for single-register writes; logs and returns success
    bool writeRegisterSafe(quint16 addr, const QByteArray &data, const QString &tag = QString());

    // Centralized SPDIF toggle handling
    void handleSpdifToggle(bool currentSwitchOnTarget);

    // Timer-driven polling implementation (previously a lambda in start())
    void pollDeviceStatus();
    
    QString devicePath;
    // Use atomic for cross-thread accesses between poll thread and main thread
    std::atomic_bool isHardSwitchOnTarget{false};

    StatusEventCallback* eventCallback = nullptr;

    bool getFeatureReport(uint8_t* buffer, size_t bufferLength);
    bool sendFeatureReport(uint8_t* buffer, size_t bufferLength);

    bool writeChunk(quint16 address, const QByteArray &data);
    bool writeEeprom(quint16 address, const QByteArray &data);
    bool readChunk(quint16 address, QByteArray &data, int chunkSize);
    uint16_t written_size = 0;
    uint32_t read_size = 0;

#ifdef _WIN32
    std::wstring getHIDDevicePath();
    std::wstring getProperDevicePath(const std::wstring& deviceInstancePath);
    bool sendFeatureReportWindows(BYTE* reportBuffer, DWORD bufferSize);
    bool getFeatureReportWindows(BYTE* reportBuffer, DWORD bufferSize);
#elif __linux__
    QString getHIDDevicePath();
    bool sendFeatureReportLinux(uint8_t* reportBuffer, int bufferSize);
    bool getFeatureReportLinux(uint8_t* reportBuffer, int bufferSize);
#endif

    std::chrono::time_point<std::chrono::steady_clock> m_lastPathQuery = std::chrono::steady_clock::now();
    bool m_inTransaction = false;
    
    // Mutex for thread-safe device handle operations
    QRecursiveMutex m_deviceHandleMutex;
    
    // Current HID device tracking
    QString m_currentHIDDevicePath;
    QString m_currentHIDPortChain;
    
    // Chipset identification and handling
    VideoChipType m_chipType = VideoChipType::UNKNOWN;
    Q_INVOKABLE void detectChipType();
    VideoChipType getChipType() const { return m_chipType; }
};

#endif // VIDEOHID_H