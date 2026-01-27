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
#include <memory>
#include "platformhidadapter.h"

#include "../ui/statusevents.h"

// Safe stoi helper declared as free function (was incorrectly placed in signals)
int safe_stoi(std::string str, int defaultValue = 0);

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
#if defined(_WIN32) && !defined(Q_MOC_RUN)
#include <windows.h>
#elif defined(_WIN32) && defined(Q_MOC_RUN)
// Minimal stubs for moc parsing on Windows to avoid including heavy system headers
typedef void* HANDLE;
typedef unsigned long DWORD;
typedef unsigned char BYTE;
#elif __linux__
#include <linux/hid.h>
#endif

class FirmwareWriter; // Forward declaration
class FirmwareReader; // Forward declaration
class VideoChip; // Forward declaration for chip abstraction

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
    friend class PlatformHidAdapter; // Allow platform adapter to call platform_* helpers
    // Also allow concrete adapters access (friend not inherited)
    friend class WindowsHidAdapter;
    friend class LinuxHidAdapter;
    // Allow chip implementations to call lower-level usb helpers
    friend class VideoChip;
    friend class Ms2109Chip;
    friend class Ms2109sChip;
    friend class Ms2130sChip;

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

    // Returns the latest firmware write percent reported (thread-safe)
    int getFirmwareWritePercent() const { return m_lastFirmwarePercent.load(); }

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
    QPair<QByteArray, bool> usbXdataRead4ByteMS2109S(quint16 u16_address);
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
    void gpio0StatusChanged(bool isToTarget);

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

    // Platform adapter for HID operations (introduced to encapsulate Windows/Linux differences)
    std::unique_ptr<PlatformHidAdapter> m_platformAdapter{nullptr};

    // Thin wrappers used by the adapter to call the existing platform-specific implementations
    bool platform_openDevice();
    void platform_closeDevice();
    bool platform_sendFeatureReport(uint8_t* reportBuffer, size_t bufferSize);
    bool platform_getFeatureReport(uint8_t* reportBuffer, size_t bufferSize);
    QString platform_getHIDDevicePath();

    using StringCallback = std::function<void(const QString&)>;
    // Polling thread used to poll device status periodically. Replaces the previous QTimer-based approach.
    class PollingThread; // forward-declared below in the cpp file (no moc required)
    friend class PollingThread; // allow the nested polling thread to access private members
    PollingThread *m_pollingThread{nullptr};
    int m_pollIntervalMs{1000};
    // Default index file (v2 supports CSV lines: version,filename,chip)
    QString firmwareURL = "https://assets.openterface.com/openterface/firmware/minikvm_latest_firmware2.txt";

    // Helper used to pick the correct firmware filename from an index file.
    // Format supported:
    //  - legacy single-line: "Openterface_Firmware_xxx.bin"
    //  - CSV multi-line: "<version>,<filename>,<chipToken>" (one per line)
    // Public for testing.
    static QString pickFirmwareFileNameFromIndex(const QString &indexContent, VideoChipType chip = VideoChipType::UNKNOWN);

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

    // Last firmware write percent (updated from writer thread and read by UI)
    std::atomic_int m_lastFirmwarePercent{0};


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

    // Abstraction for chip-specific behavior
    std::unique_ptr<VideoChip> m_chipImpl{nullptr};

    Q_INVOKABLE void detectChipType();
    VideoChipType getChipType() const { return m_chipType; }
    VideoChip* getChipImpl() const { return m_chipImpl.get(); }

public slots:
    // Called by FirmwareWriter via a queued/blocking invoke to ensure EEPROM writes run in VideoHid's thread
    bool performWriteEeprom(quint16 address, const QByteArray &data);

private slots:
    // Internal slots used to safely schedule hotplug-driven actions into VideoHid's thread
    void handleScheduledDisconnect(const QString &oldPath);
    void handleScheduledConnect();
};

#endif // VIDEOHID_H