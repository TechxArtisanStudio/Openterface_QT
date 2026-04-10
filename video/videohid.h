#ifndef VIDEOHID_H
#define VIDEOHID_H

#include <QObject>
#include <QTimer>
#include <atomic>
#include "firmware/FirmwareNetworkClient.h"
#include <vector>
#include <chrono>
#include <functional>
#include <QMutex>
#include <QRecursiveMutex>
#include <memory>
#include "transport/IHIDTransport.h"

class FirmwareOperationManager;
#ifdef _WIN32
#include "transport/WindowsHIDTransport.h"
#elif __linux__
#include "transport/LinuxHIDTransport.h"
#endif

#include "../ui/statusevents.h"

// Safe stoi helper declared as free function (was incorrectly placed in signals)
int safe_stoi(std::string str, int defaultValue = 0);

// VideoChipType, VideoHidRegisterSet are now defined in videohidchip.h.
// Re-include here so callers of videohid.h continue to compile unchanged.
#include "videohidchip.h"

// Helper struct containing the read values for resolution and timing for VideoHid
struct VideoHidResolutionInfo {
    quint32 width{0};
    quint32 height{0};
    float fps{0.0f};
    float pixclk{0.0f};
    bool hdmiConnected{false};
};

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

class VideoHid : public QObject, public IHIDTransport
{
    Q_OBJECT

    friend class FirmwareWriter;
    friend class FirmwareReader;

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
    void stopPollingOnly(); // Stop polling but keep HID connection for firmware update

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
    inline std::string getLatestFirmwareVersion()  { return m_netClient.getLatestVersion(); }
    inline std::string getCurrentFirmwareVersion() { return m_currentfirmwareVersion; }

    FirmwareResult fireware_result;
    FirmwareResult isLatestFirmware();

    // Static helper exposed for testing (delegates to FirmwareNetworkClient).
    static QString pickFirmwareFileNameFromIndex(const QString& indexContent,
                                                  VideoChipType chip = VideoChipType::UNKNOWN);

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

    bool openHIDDevice();

    // HDMI timing Pixel clock
    float getPixelclk();

    void loadFirmwareToEeprom();
    void loadEepromToFile(const QString &filePath);
    quint32 readFirmwareSize();

    // Read firmware from EEPROM (MS2109/MS2109S).
    // Optional progressCallback is called with percent [0,100] after each chunk.
    QByteArray readEeprom(quint16 address, quint32 size,
                          std::function<void(int)> progressCallback = nullptr);

    // Returns the FirmwareOperationManager used by loadFirmwareToEeprom().
    FirmwareOperationManager* getFirmwareOperationManager() const;

    // Typed accessor for Ms2130sChip flash operations; returns nullptr for non-MS2130S chips
    Ms2130sChip* getMs2130sChip() const;


    // Transaction-based HID access
    bool beginTransaction();
    void endTransaction();
    bool isInTransaction() const;

    // ── IHIDTransport implementation ──────────────────
    bool isOpen()  const override { return m_deviceTransport && m_deviceTransport->isOpen(); }
    bool open()    override;          // opens device handle; sets m_inTransaction
    void close()   override;          // closes device handle; clears m_inTransaction
    bool sendFeatureReport(uint8_t* buf, size_t len) override;
    bool getFeatureReport (uint8_t* buf, size_t len) override;
    // Single-attempt (no retry) — used by chip protocol engines
    bool sendDirect(uint8_t* buf, size_t len) override;
    bool getDirect (uint8_t* buf, size_t len) override;
    // ─────────────────────────────────────────────────
    
    // Hotplug monitoring integration
    void connectToHotplugMonitor();
    void disconnectFromHotplugMonitor();

    // This method is invokable so we can safely dispatch eventCallback calls to the object's thread
    Q_INVOKABLE void dispatchSwitchableUsbToggle(bool isToTarget);

signals:
    void inputResolutionChanged(int old_input_width, int old_input_height, int new_input_width, int new_input_height);
    void resolutionChangeUpdate(const int& width, const int& height, const float& fps, const float& pixelClk);
    void hidDeviceChanged(const QString& oldDevicePath, const QString& newDevicePath);
    void hidDeviceSwitched(const QString& fromPortChain, const QString& toPortChain);
    void hidDeviceConnected(const QString& devicePath);
    void hidDeviceDisconnected(const QString& devicePath);
    void gpio0StatusChanged(bool isToTarget);

private:
    explicit VideoHid(QObject *parent = nullptr);
    ~VideoHid();
    // Network firmware client — owns networkFirmware, latestVersion, lastResult.
    FirmwareNetworkClient m_netClient;
    std::string m_currentfirmwareVersion;
    
    // Helper method to start the monitoring timer
    void startMonitoringTimer();

    // Platform-specific HID transport — owns device handle and all platform I/O.
    // Created in VideoHid constructor.
    std::unique_ptr<IHIDTransport> m_deviceTransport{nullptr};

    // IHIDTransport override — re-open as synchronous handle for flash ops.
    bool reopenSync() override;

    // Cache for the discovered HID device path (cleared by clearDevicePathCache).
    QString m_cachedDevicePath;

    // Polling thread used to poll device status periodically.
    class PollingThread; // forward-declared in videohid.cpp
    friend class PollingThread;
    PollingThread *m_pollingThread{nullptr};
    int m_pollIntervalMs{1000};

    // Firmware index URL (v2 CSV: version,filename,chip)
    static constexpr const char* firmwareURL = FirmwareNetworkClient::DEFAULT_INDEX_URL;

    QString extractPortNumberFromPath(const QString& path);
    QPair<QByteArray, bool> usbXdataRead4Byte(quint16 u16_address);
    bool usbXdataWrite4Byte(quint16 u16_address, QByteArray data);

    // Register set retrieval for the current chip
    VideoHidRegisterSet getRegisterSetForCurrentChip() const;

    // Read the full input status (resolution/fps/pixel clock) into a struct
    VideoHidResolutionInfo getInputStatus();

    // Normalize resolution based on chip specifics and pixel clock
    void normalizeResolution(VideoHidResolutionInfo &info);

    // Safe single-byte register reader
    quint8 readRegisterSafe(quint16 addr, quint8 defaultValue = 0, const QString& tag = QString());

    // Safe register write helper; logs and returns success
    bool writeRegisterSafe(quint16 addr, const QByteArray &data, const QString &tag = QString());

    // Centralized SPDIF toggle handling
    void handleSpdifToggle(bool currentSwitchOnTarget);

    // Timer-driven polling implementation
    void pollDeviceStatus();
    
    QString devicePath;
    std::atomic_bool isHardSwitchOnTarget{false};

    StatusEventCallback* eventCallback = nullptr;

    bool writeChunk(quint16 address, const QByteArray &data,
                    const std::function<void(int)>& chunkCallback = {});
    bool writeEeprom(quint16 address, const QByteArray &data,
                     std::function<void(int)> chunkCallback = nullptr);
    bool readChunk(quint16 address, QByteArray &data, int chunkSize);
    uint32_t read_size = 0;

    // Last firmware write percent (updated from writer thread, read by UI)
    std::atomic_int m_lastFirmwarePercent{0};
    // Set true during firmware flash to suppress all background register reads
    std::atomic_bool m_flashInProgress{false};

    std::chrono::time_point<std::chrono::steady_clock> m_lastPathQuery = std::chrono::steady_clock::now();
    bool m_inTransaction = false;

    // Mutex for thread-safe device handle operations
    QRecursiveMutex m_deviceHandleMutex;

    // Current HID device tracking
    QString m_currentHIDDevicePath;
    QString m_currentHIDPortChain;

    // Chip detection and implementation
    VideoChipType m_chipType = VideoChipType::UNKNOWN;
    std::unique_ptr<VideoChip> m_chipImpl{nullptr};

    // Firmware operation manager for loadFirmwareToEeprom()
    FirmwareOperationManager* m_firmwareOpManager{nullptr};

    // Chunk-write progress for firmware write
    quint32 written_size = 0;

public:
    Q_INVOKABLE void detectChipType();
    VideoChipType getChipType() const { return m_chipType; }
    VideoChip* getChipImpl() const { return m_chipImpl.get(); }

public slots:
    bool performWriteEeprom(quint16 address, const QByteArray &data);

private slots:
    // Internal slots used to safely schedule hotplug-driven actions into VideoHid's thread
    void handleScheduledDisconnect(const QString &oldPath);
    void handleScheduledConnect();
};

#endif // VIDEOHID_H