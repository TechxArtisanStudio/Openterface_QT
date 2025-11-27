#include "videohid.h"

#include <QDebug>
#include <QDir>
#include <QTimer>
#include <QThread>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QEventLoop>

#include "ms2109.h"
#include "ms2130s.h"
#include "firmwarewriter.h"
#include "firmwarereader.h"
#include "../global.h"
#include "../device/DeviceManager.h"
#include "../device/HotplugMonitor.h"
#include "../ui/globalsetting.h"
#include "../device/platform/AbstractPlatformDeviceManager.h"

#ifdef _WIN32
#include <hidclass.h>
extern "C"
{
#include <hidsdi.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <devpkey.h>
}
#elif __linux__
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/hidraw.h>
#endif

Q_LOGGING_CATEGORY(log_host_hid, "opf.device.hid");

VideoHid::VideoHid(QObject *parent) : QObject(parent), m_inTransaction(false) {
    // Initialize current device tracking
    m_currentHIDDevicePath.clear();
    m_currentHIDPortChain.clear();
    
    // Connect to hotplug monitor for automatic device management
    connectToHotplugMonitor();
}

// Detect chipset type based on VID/PID or other identifiers
void VideoHid::detectChipType() {
    // Store previous chip type for comparison
    VideoChipType previousChipType = m_chipType;
    
    // Default to UNKNOWN
    m_chipType = VideoChipType::UNKNOWN;
    
    // Check if we have a valid device path
    if (m_currentHIDDevicePath.isEmpty()) {
        qCDebug(log_host_hid) << "No valid HID device path to detect chip type";
        return;
    }
    
    // Extract VID/PID from device path
    QString devicePath = m_currentHIDDevicePath;
    qCDebug(log_host_hid) << "Detecting chip type from device path:" << devicePath;
    qCDebug(log_host_hid) << "Current port chain:" << m_currentHIDPortChain;
    
    // We need to check both hidDevicePath and DeviceInfo's VID/PID since different platforms
    // format the device path differently. On some systems the VID/PID might not be in the path.
    
    bool isMS2130S = false;
    bool isMS2109 = false;
    
    // We'll rely on the device path instead of trying to access protected VID/PIDs
    
    // Look for MS2130S identifiers (VID: 345F, PID: 2132)
    if (devicePath.contains("345F", Qt::CaseInsensitive) && 
        devicePath.contains("2132", Qt::CaseInsensitive)) {
        isMS2130S = true;
        qCDebug(log_host_hid) << "Detected MS2130S chipset from path (345F:2132)";
    }
    // Look for MS2109 identifiers (VID: 534D, PID: 2109)
    else if (devicePath.contains("534D", Qt::CaseInsensitive) && 
             devicePath.contains("2109", Qt::CaseInsensitive)) {
        isMS2109 = true;
        qCDebug(log_host_hid) << "Detected MS2109 chipset from path (534D:2109)";
    }
    // Also check for Windows style VID/PID format
    else if (devicePath.contains("vid_345f", Qt::CaseInsensitive) && 
             devicePath.contains("pid_2132", Qt::CaseInsensitive)) {
        isMS2130S = true;
        qCDebug(log_host_hid) << "Detected MS2130S chipset from Windows-style path";
    }
    else if (devicePath.contains("vid_534d", Qt::CaseInsensitive) && 
             devicePath.contains("pid_2109", Qt::CaseInsensitive)) {
        isMS2109 = true;
        qCDebug(log_host_hid) << "Detected MS2109 chipset from Windows-style path";
    }
    
    if (isMS2130S) {
        m_chipType = VideoChipType::MS2130S;
    } else if (isMS2109) {
        m_chipType = VideoChipType::MS2109;
    } else {
        qCDebug(log_host_hid) << "Unknown chipset in device path:" << devicePath;
        // If we couldn't detect the type but had a previous valid type, keep using it
        if (previousChipType != VideoChipType::UNKNOWN) {
            m_chipType = previousChipType;
            qCDebug(log_host_hid) << "Falling back to previous chip type";
        }
    }
    
    // Log when chip type changes
    if (previousChipType != m_chipType) {
        qCDebug(log_host_hid) << "Chip type changed from" 
            << (previousChipType == VideoChipType::MS2109 ? "MS2109" :
                previousChipType == VideoChipType::MS2130S ? "MS2130S" : "Unknown") 
            << "to" 
            << (m_chipType == VideoChipType::MS2109 ? "MS2109" :
                m_chipType == VideoChipType::MS2130S ? "MS2130S" : "Unknown");
    }
}

// Update the start method to keep HID device continuously open
void VideoHid::start() {
    // Prevent multiple starts
    if (timer) {
        qCDebug(log_host_hid) << "VideoHid already started, ignoring duplicate start call";
        return;
    }
    
    // Initialize current device tracking from global settings
    QString currentPortChain = GlobalSetting::instance().getOpenterfacePortChain();
    if (!currentPortChain.isEmpty()) {
        m_currentHIDPortChain = currentPortChain;
        QString hidPath = findMatchingHIDDevice(currentPortChain);
        
        // Set the current HID device path and detect chip type
        if (!hidPath.isEmpty()) {
            m_currentHIDDevicePath = hidPath;
            detectChipType();
        }
        if (!hidPath.isEmpty()) {
            m_currentHIDDevicePath = hidPath;
            qCDebug(log_host_hid) << "Initialized HID device with port chain:" << currentPortChain 
                                 << "device path:" << hidPath;
        }
    }
    
    std::string captureCardFirmwareVersion = getFirmwareVersion();
    qCDebug(log_host_hid) << "MS2109 firmware VERSION:" << QString::fromStdString(captureCardFirmwareVersion);    //firmware VERSION
    
    GlobalVar::instance().setCaptureCardFirmwareVersion(captureCardFirmwareVersion);
    isHardSwitchOnTarget = getSpdifout();
    qCDebug(log_host_hid)  << "SPDIFOUT:" << isHardSwitchOnTarget;    //SPDIFOUT
    if(eventCallback){
        eventCallback->onSwitchableUsbToggle(isHardSwitchOnTarget);
        setSpdifout(isHardSwitchOnTarget); //Follow the hard switch by default
    }

    // Open HID device once and keep it open for continuous monitoring
    if (!beginTransaction()) {
        qCWarning(log_host_hid) << "Failed to open HID device for continuous monitoring";
        return;
    }

    // Add longer delay to allow device to fully stabilize after opening
    qCDebug(log_host_hid) << "Waiting for device stabilization before starting timer...";
    QThread::msleep(500);

    // Log the detected chip type
    qCDebug(log_host_hid) << "Starting timer with chip type:" << 
                         (m_chipType == VideoChipType::MS2109 ? "MS2109" :
                          m_chipType == VideoChipType::MS2130S ? "MS2130S" : "Unknown");
                          
    //start a timer to get the HDMI connection status every 1 second
    timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [=](){
        // Device is already open - no need for beginTransaction/endTransaction
        try {
            bool currentSwitchOnTarget = getGpio0();
            bool hdmiConnected = isHdmiConnected();

            if (eventCallback) {
                auto safeRead = [this](quint16 addr, quint8 defaultValue = 0) -> quint8 {
                    auto result = usbXdataRead4Byte(addr);
                    if (!result.second || result.first.isEmpty()) {
                        qCWarning(log_host_hid) << "HID READ FAILED from address:" << QString("0x%1").arg(addr, 4, 16, QChar('0'))
                                               << "result.second:" << result.second 
                                               << "result.first.size():" << result.first.size()
                                               << "chip type:" << (m_chipType == VideoChipType::MS2109 ? "MS2109" : 
                                                                  m_chipType == VideoChipType::MS2130S ? "MS2130S" : "Unknown")
                                               << "returning default value:" << defaultValue;
                        return defaultValue;
                    }
                    quint8 value = static_cast<quint8>(result.first.at(0));
                    qCDebug(log_host_hid) << "HID READ SUCCESS from address:" << QString("0x%1").arg(addr, 4, 16, QChar('0'))
                                         << "value:" << QString("0x%1").arg(value, 2, 16, QChar('0')) << "(" << value << ")";
                    return value;
                };
                
                if (hdmiConnected) {
                    // Determine which registers to use based on chip type
                    uint16_t width_h_addr, width_l_addr, height_h_addr, height_l_addr;
                    uint16_t fps_h_addr, fps_l_addr, clk_h_addr, clk_l_addr;
                    
                    if (m_chipType == VideoChipType::MS2130S) {
                        // Use MS2130S registers
                        width_h_addr = MS2130S_ADDR_INPUT_WIDTH_H;
                        width_l_addr = MS2130S_ADDR_INPUT_WIDTH_L;
                        height_h_addr = MS2130S_ADDR_INPUT_HEIGHT_H;
                        height_l_addr = MS2130S_ADDR_INPUT_HEIGHT_L;
                        fps_h_addr = MS2130S_ADDR_INPUT_FPS_H;
                        fps_l_addr = MS2130S_ADDR_INPUT_FPS_L;
                        clk_h_addr = MS2130S_ADDR_INPUT_PIXELCLK_H;
                        clk_l_addr = MS2130S_ADDR_INPUT_PIXELCLK_L;
                        qCDebug(log_host_hid) << "Using MS2130S registers for resolution";
                    } else {
                        // Default to MS2109 registers
                        width_h_addr = ADDR_INPUT_WIDTH_H;
                        width_l_addr = ADDR_INPUT_WIDTH_L;
                        height_h_addr = ADDR_INPUT_HEIGHT_H;
                        height_l_addr = ADDR_INPUT_HEIGHT_L;
                        fps_h_addr = ADDR_INPUT_FPS_H;
                        fps_l_addr = ADDR_INPUT_FPS_L;
                        clk_h_addr = ADDR_INPUT_PIXELCLK_H;
                        clk_l_addr = ADDR_INPUT_PIXELCLK_L;
                        qCDebug(log_host_hid) << "Using MS2109 registers for resolution";
                    }
                    
                    // Get resolution-related information using the appropriate registers
                    qCDebug(log_host_hid) << "Reading width_h from address:" << QString("0x%1").arg(width_h_addr, 4, 16, QChar('0'));
                    quint8 width_h = safeRead(width_h_addr);
                    qCDebug(log_host_hid) << "Width high byte raw value:" << width_h << "(" << QString("0x%1").arg(width_h, 2, 16, QChar('0')) << ")";
                    
                    qCDebug(log_host_hid) << "Reading width_l from address:" << QString("0x%1").arg(width_l_addr, 4, 16, QChar('0'));
                    quint8 width_l = safeRead(width_l_addr);
                    qCDebug(log_host_hid) << "Width low byte raw value:" << width_l << "(" << QString("0x%1").arg(width_l, 2, 16, QChar('0')) << ")";
                    quint16 width = (width_h << 8) + width_l;
                    
                    qCDebug(log_host_hid) << "Reading height_h from address:" << QString("0x%1").arg(height_h_addr, 4, 16, QChar('0'));
                    quint8 height_h = safeRead(height_h_addr);
                    qCDebug(log_host_hid) << "Height high byte raw value:" << height_h << "(" << QString("0x%1").arg(height_h, 2, 16, QChar('0')) << ")";
                    
                    qCDebug(log_host_hid) << "Reading height_l from address:" << QString("0x%1").arg(height_l_addr, 4, 16, QChar('0'));
                    quint8 height_l = safeRead(height_l_addr);
                    qCDebug(log_host_hid) << "Height low byte raw value:" << height_l << "(" << QString("0x%1").arg(height_l, 2, 16, QChar('0')) << ")";
                    quint16 height = (height_h << 8) + height_l;
                    
                    qCDebug(log_host_hid) << "Reading fps_h from address:" << QString("0x%1").arg(fps_h_addr, 4, 16, QChar('0'));
                    quint8 fps_h = safeRead(fps_h_addr);
                    qCDebug(log_host_hid) << "FPS high byte raw value:" << fps_h << "(" << QString("0x%1").arg(fps_h, 2, 16, QChar('0')) << ")";
                    
                    qCDebug(log_host_hid) << "Reading fps_l from address:" << QString("0x%1").arg(fps_l_addr, 4, 16, QChar('0'));
                    quint8 fps_l = safeRead(fps_l_addr);
                    qCDebug(log_host_hid) << "FPS low byte raw value:" << fps_l << "(" << QString("0x%1").arg(fps_l, 2, 16, QChar('0')) << ")";
                    quint16 fps_raw = (fps_h << 8) + fps_l;
                    float fps = static_cast<float>(fps_raw) / 100;
                    
                    qCDebug(log_host_hid) << "Reading pixel clock high byte from address:" << QString("0x%1").arg(clk_h_addr, 4, 16, QChar('0'));
                    quint8 clk_h = safeRead(clk_h_addr);
                    qCDebug(log_host_hid) << "Pixel clock high byte raw value:" << clk_h << "(" << QString("0x%1").arg(clk_h, 2, 16, QChar('0')) << ")";
                    
                    qCDebug(log_host_hid) << "Reading pixel clock low byte from address:" << QString("0x%1").arg(clk_l_addr, 4, 16, QChar('0'));
                    quint8 clk_l = safeRead(clk_l_addr);
                    qCDebug(log_host_hid) << "Pixel clock low byte raw value:" << clk_l << "(" << QString("0x%1").arg(clk_l, 2, 16, QChar('0')) << ")";
                    quint16 clk_raw = (clk_h << 8) + clk_l;
                    float pixclk = clk_raw / 100.0;
                    
                    // Enhanced logging with register addresses and calculations
                    if (m_chipType == VideoChipType::MS2130S) {
                        qCDebug(log_host_hid) << "MS2130S Resolution Calculation:";
                        qCDebug(log_host_hid) << "  Width = (width_h << 8) + width_l = (" 
                                             << width_h << " << 8) + " << width_l << " = " << width;
                        qCDebug(log_host_hid) << "  Height = (height_h << 8) + height_l = (" 
                                             << height_h << " << 8) + " << height_l << " = " << height;
                        qCDebug(log_host_hid) << "  FPS raw = (fps_h << 8) + fps_l = (" 
                                             << fps_h << " << 8) + " << fps_l << " = " << fps_raw;
                        qCDebug(log_host_hid) << "  FPS = fps_raw / 100 = " << fps_raw << " / 100 = " << fps;
                        qCDebug(log_host_hid) << "  Pixel Clock raw = (clk_h << 8) + clk_l = (" 
                                             << clk_h << " << 8) + " << clk_l << " = " << clk_raw;
                        qCDebug(log_host_hid) << "  Pixel Clock = clk_raw / 100 = " << clk_raw << " / 100 = " << pixclk << " MHz";
                        
                        qCDebug(log_host_hid) << "MS2130S Read resolution:" << width << "x" << height 
                            << ", FPS:" << fps << ", PixelClk:" << pixclk << " MHz"
                            << " (From registers: width=" << QString::number(width_h_addr, 16).toUpper() << "/"
                            << QString::number(width_l_addr, 16).toUpper()
                            << ", height=" << QString::number(height_h_addr, 16).toUpper() << "/"
                            << QString::number(height_l_addr, 16).toUpper()
                            << ", fps=" << QString::number(fps_h_addr, 16).toUpper() << "/"
                            << QString::number(fps_l_addr, 16).toUpper() << ")";
                    } else {
                        qCDebug(log_host_hid) << "MS2109 Resolution Calculation:";
                        qCDebug(log_host_hid) << "  Width = (width_h << 8) + width_l = (" 
                                             << width_h << " << 8) + " << width_l << " = " << width;
                        qCDebug(log_host_hid) << "  Height = (height_h << 8) + height_l = (" 
                                             << height_h << " << 8) + " << height_l << " = " << height;
                        qCDebug(log_host_hid) << "  FPS raw = (fps_h << 8) + fps_l = (" 
                                             << fps_h << " << 8) + " << fps_l << " = " << fps_raw;
                        qCDebug(log_host_hid) << "  FPS = fps_raw / 100 = " << fps_raw << " / 100 = " << fps;
                        qCDebug(log_host_hid) << "  Pixel Clock raw = (clk_h << 8) + clk_l = (" 
                                             << clk_h << " << 8) + " << clk_l << " = " << clk_raw;
                        qCDebug(log_host_hid) << "  Pixel Clock = clk_raw / 100 = " << clk_raw << " / 100 = " << pixclk << " MHz";
                        
                        qCDebug(log_host_hid) << "MS2109 Read resolution:" << width << "x" << height 
                            << ", FPS:" << fps << ", PixelClk:" << pixclk << " MHz"
                            << " (From registers: width=" << QString::number(width_h_addr, 16).toUpper() << "/"
                            << QString::number(width_l_addr, 16).toUpper()
                            << ", height=" << QString::number(height_h_addr, 16).toUpper() << "/"
                            << QString::number(height_l_addr, 16).toUpper()
                            << ", fps=" << QString::number(fps_h_addr, 16).toUpper() << "/"
                            << QString::number(fps_l_addr, 16).toUpper() << ")";
                    }
                    
                    float aspectRatio = height ? static_cast<float>(width) / height : 0;
                    GlobalVar::instance().setInputAspectRatio(aspectRatio);

                    // Apply resolution adjustment based on pixel clock and chip type
                    // Following the same logic from the other implementation
                    if (m_chipType == VideoChipType::MS2109) {
                        if (pixclk > 189) { // The magic value for MS2109 4K resolution correction
                            qCDebug(log_host_hid) << "MS2109 with high pixel clock detected (" << pixclk 
                                                 << " MHz > 189 MHz), adjusting resolution";
                            
                            // Only double width if it's not already 4096
                            if (width != 4096) {
                                width *= 2;
                                qCDebug(log_host_hid) << "Doubling width to " << width;
                            }
                            
                            // Only double height if it's not already 2160
                            if (height != 2160) {
                                height *= 2;
                                qCDebug(log_host_hid) << "Doubling height to " << height;
                            }
                        }
                    } else {
                        // Special handling for MS2130S with specific resolutions
                        if (width == 3840 && height == 1080) {
                            qCDebug(log_host_hid) << "Detected special case: 3840x1080, adjusting height to 2160";
                            height = 2160;
                        }
                    }

                    if (GlobalVar::instance().getInputWidth() != width || GlobalVar::instance().getInputHeight() != height) {
                        qCDebug(log_host_hid) << "Resolution changed from " 
                                             << GlobalVar::instance().getInputWidth() << "x" << GlobalVar::instance().getInputHeight()
                                             << " to " << width << "x" << height;
                        emit inputResolutionChanged(GlobalVar::instance().getInputWidth(), GlobalVar::instance().getInputHeight(), width, height);
                    }
                    
                    qCDebug(log_host_hid) << "Emitting resolution update - Width:" << width 
                                         << "Height:" << height << "FPS:" << fps 
                                         << "PixelClock:" << pixclk << "MHz";
                    emit resolutionChangeUpdate(width, height, fps, pixclk);
                } else {
                    qCDebug(log_host_hid) << "No HDMI connection detected, emitting zero resolution";
                    emit resolutionChangeUpdate(0, 0, 0, 0);
                }

                // Handle hardware switch status changes
                if (isHardSwitchOnTarget != currentSwitchOnTarget) {
                    qCDebug(log_host_hid)  << "isHardSwitchOnTarget" << isHardSwitchOnTarget << "currentSwitchOnTarget" << currentSwitchOnTarget;
                    eventCallback->onSwitchableUsbToggle(currentSwitchOnTarget);
                    
                    // Set SPDIF out
                    int bit = 1;
                    int mask = 0xFE;
                    if (GlobalVar::instance().getCaptureCardFirmwareVersion() < "24081309") {
                        bit = 0x10;
                        mask = 0xEF;
                    }

                    quint8 spdifout = safeRead(ADDR_SPDIFOUT);
                    if (currentSwitchOnTarget) {
                        spdifout |= bit;
                    } else {
                        spdifout &= mask;
                    }
                    QByteArray data(4, 0);
                    data[0] = spdifout;
                    usbXdataWrite4Byte(ADDR_SPDIFOUT, data);
                    
                    isHardSwitchOnTarget = currentSwitchOnTarget;
                }
            }
        }
        catch (...) {
            qCDebug(log_host_hid)  << "Exception occurred during timer processing";
        }
    });

    timer->start(1000);
}

void VideoHid::stop() {
    qCDebug(log_host_hid)  << "Stopping VideoHid timer.";
    if (timer) {
        timer->stop();
        disconnect(timer, &QTimer::timeout, this, nullptr);
        delete timer;
        timer = nullptr;
    }
    
    // Close the HID device when stopping
    endTransaction();
}

/*
Get the input resolution from capture card. 
*/
QPair<int, int> VideoHid::getResolution() {
    uint16_t width_h_addr, width_l_addr, height_h_addr, height_l_addr;
    
    // Use the appropriate registers based on chip type
    if (m_chipType == VideoChipType::MS2130S) {
        width_h_addr = MS2130S_ADDR_INPUT_WIDTH_H;
        width_l_addr = MS2130S_ADDR_INPUT_WIDTH_L;
        height_h_addr = MS2130S_ADDR_INPUT_HEIGHT_H;
        height_l_addr = MS2130S_ADDR_INPUT_HEIGHT_L;
        qCDebug(log_host_hid) << "getResolution: Using MS2130S registers" 
                            << QString::number(width_h_addr, 16).toUpper() << "/"
                            << QString::number(width_l_addr, 16).toUpper() << "/"
                            << QString::number(height_h_addr, 16).toUpper() << "/"
                            << QString::number(height_l_addr, 16).toUpper();
    } else {
        // Default to MS2109 registers
        width_h_addr = ADDR_INPUT_WIDTH_H;
        width_l_addr = ADDR_INPUT_WIDTH_L;
        height_h_addr = ADDR_INPUT_HEIGHT_H;
        height_l_addr = ADDR_INPUT_HEIGHT_L;
        qCDebug(log_host_hid) << "getResolution: Using MS2109 registers"
                            << QString::number(width_h_addr, 16).toUpper() << "/"
                            << QString::number(width_l_addr, 16).toUpper() << "/"
                            << QString::number(height_h_addr, 16).toUpper() << "/"
                            << QString::number(height_l_addr, 16).toUpper();
    }
    
    auto safeRead = [this](quint16 addr, quint8 defaultValue = 0) -> quint8 {
        auto result = usbXdataRead4Byte(addr);
        if (!result.second || result.first.isEmpty()) {
            qCWarning(log_host_hid) << "Failed to read from address:" << QString("0x%1").arg(addr, 4, 16, QChar('0'))
                                   << "returning default value:" << defaultValue;
            return defaultValue;
        }
        return static_cast<quint8>(result.first.at(0));
    };
    
    quint8 width_h = safeRead(width_h_addr);
    quint8 width_l = safeRead(width_l_addr);
    quint16 width = (width_h << 8) + width_l;
    quint8 height_h = safeRead(height_h_addr);
    quint8 height_l = safeRead(height_l_addr);
    quint16 height = (height_h << 8) + height_l;
    
    qCDebug(log_host_hid) << "getResolution: Read values width_h=" << width_h 
                        << " width_l=" << width_l << " height_h=" << height_h 
                        << " height_l=" << height_l << " --> " << width << "x" << height;
    
    return qMakePair(width, height);
}

float VideoHid::getFps() {
    uint16_t fps_h_addr, fps_l_addr;
    
    // Use the appropriate registers based on chip type
    if (m_chipType == VideoChipType::MS2130S) {
        fps_h_addr = MS2130S_ADDR_INPUT_FPS_H;
        fps_l_addr = MS2130S_ADDR_INPUT_FPS_L;
        qCDebug(log_host_hid) << "getFps: Using MS2130S registers" 
                            << QString::number(fps_h_addr, 16).toUpper() << "/"
                            << QString::number(fps_l_addr, 16).toUpper();
    } else {
        // Default to MS2109 registers
        fps_h_addr = ADDR_INPUT_FPS_H;
        fps_l_addr = ADDR_INPUT_FPS_L;
        qCDebug(log_host_hid) << "getFps: Using MS2109 registers"
                            << QString::number(fps_h_addr, 16).toUpper() << "/"
                            << QString::number(fps_l_addr, 16).toUpper();
    }
    
    auto safeRead = [this](quint16 addr, quint8 defaultValue = 0) -> quint8 {
        auto result = usbXdataRead4Byte(addr);
        if (!result.second || result.first.isEmpty()) {
            qCWarning(log_host_hid) << "Failed to read from address:" << QString("0x%1").arg(addr, 4, 16, QChar('0'))
                                   << "returning default value:" << defaultValue;
            return defaultValue;
        }
        return static_cast<quint8>(result.first.at(0));
    };
    
    quint8 fps_h = safeRead(fps_h_addr);
    quint8 fps_l = safeRead(fps_l_addr);
    quint16 fps_raw = (fps_h << 8) + fps_l;
    float fps = static_cast<float>(fps_raw) / 100;
    
    qCDebug(log_host_hid) << "getFps: Read values fps_h=" << fps_h 
                        << " fps_l=" << fps_l << " raw=" << fps_raw
                        << " --> fps=" << fps;
    
    return fps;
}

/*
 * Address: 0xDF00 bit0 indicates the hard switch status,
 * true means switchable usb connects to the target,
 * false means switchable usb connects to the host
 */
bool VideoHid::getGpio0() {
    uint16_t gpio_addr;
    
    if (m_chipType == VideoChipType::MS2130S) {
        gpio_addr = MS2130S_ADDR_GPIO0;
    } else {
        gpio_addr = ADDR_GPIO0;
    }
    
    auto safeRead = [this](quint16 addr, quint8 defaultValue = 0) -> quint8 {
        auto result = usbXdataRead4Byte(addr);
        if (!result.second || result.first.isEmpty()) {
            qCWarning(log_host_hid) << "Failed to read from address:" << QString("0x%1").arg(addr, 4, 16, QChar('0'))
                                   << "returning default value:" << defaultValue;
            return defaultValue;
        }
        return static_cast<quint8>(result.first.at(0));
    };
    
    bool result = safeRead(gpio_addr) & 0x01;
    return result;
}

float VideoHid::getPixelclk() {
    uint16_t clk_h_addr, clk_l_addr;
    
    // Use the appropriate registers based on chip type
    if (m_chipType == VideoChipType::MS2130S) {
        clk_h_addr = MS2130S_ADDR_INPUT_PIXELCLK_H;
        clk_l_addr = MS2130S_ADDR_INPUT_PIXELCLK_L;
        qCDebug(log_host_hid) << "getPixelclk: Using MS2130S registers" 
                            << QString::number(clk_h_addr, 16).toUpper() << "/"
                            << QString::number(clk_l_addr, 16).toUpper();
    } else {
        // Default to MS2109 registers
        clk_h_addr = ADDR_INPUT_PIXELCLK_H;
        clk_l_addr = ADDR_INPUT_PIXELCLK_L;
        qCDebug(log_host_hid) << "getPixelclk: Using MS2109 registers"
                            << QString::number(clk_h_addr, 16).toUpper() << "/"
                            << QString::number(clk_l_addr, 16).toUpper();
    }
    
    qCDebug(log_host_hid) << "getPixelclk: Reading high byte from address:" << QString("0x%1").arg(clk_h_addr, 4, 16, QChar('0'));
    auto safeRead = [this](quint16 addr, quint8 defaultValue = 0) -> quint8 {
        auto result = usbXdataRead4Byte(addr);
        if (!result.second || result.first.isEmpty()) {
            qCWarning(log_host_hid) << "Failed to read from address:" << QString("0x%1").arg(addr, 4, 16, QChar('0'))
                                   << "returning default value:" << defaultValue;
            return defaultValue;
        }
        return static_cast<quint8>(result.first.at(0));
    };
    
    quint8 clk_h = safeRead(clk_h_addr);
    
    qCDebug(log_host_hid) << "getPixelclk: Reading low byte from address:" << QString("0x%1").arg(clk_l_addr, 4, 16, QChar('0'));
    quint8 clk_l = safeRead(clk_l_addr);
    
    quint16 clk_raw = (clk_h << 8) + clk_l;
    float result = clk_raw/100.0;
    
    qCDebug(log_host_hid) << "getPixelclk: Read values clk_h=" << clk_h 
                        << " clk_l=" << clk_l << " raw=" << clk_raw
                        << " --> Pixel Clock=" << result << "MHz";
    
    return result;
}


bool VideoHid::getSpdifout() {
    int bit = 1;
    int mask = 0xFE;  // Mask for potential future use
    uint16_t spdifout_addr;
    
    if (m_chipType == VideoChipType::MS2130S) {
        spdifout_addr = MS2130S_ADDR_SPDIFOUT;
    } else {
        spdifout_addr = ADDR_SPDIFOUT;
    }
    
    if (GlobalVar::instance().getCaptureCardFirmwareVersion() < "24081309") {
        qCDebug(log_host_hid)  << "Firmware version is less than 24081309";
        bit = 0x10;
        mask = 0xEF;
    }
    Q_UNUSED(mask)  // Suppress unused variable warning
    
    auto safeRead = [this](quint16 addr, quint8 defaultValue = 0) -> quint8 {
        auto result = usbXdataRead4Byte(addr);
        if (!result.second || result.first.isEmpty()) {
            qCWarning(log_host_hid) << "Failed to read from address:" << QString("0x%1").arg(addr, 4, 16, QChar('0'))
                                   << "returning default value:" << defaultValue;
            return defaultValue;
        }
        return static_cast<quint8>(result.first.at(0));
    };
    
    bool result = safeRead(spdifout_addr) & bit;
    return result;
}

void VideoHid::switchToHost() {
    qCDebug(log_host_hid)  << "Switch to host";
    setSpdifout(false);
    GlobalVar::instance().setSwitchOnTarget(false);
    if(eventCallback) eventCallback->onSwitchableUsbToggle(false);
}

void VideoHid::switchToTarget() {
    qCDebug(log_host_hid)  << "Switch to target";
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
    uint16_t spdifout_addr;
    
    // Select the appropriate register based on chip type
    if (m_chipType == VideoChipType::MS2130S) {
        spdifout_addr = MS2130S_ADDR_SPDIFOUT;
    } else {
        spdifout_addr = ADDR_SPDIFOUT;
    }
    
    if (GlobalVar::instance().getCaptureCardFirmwareVersion() < "24081309") {
        qCDebug(log_host_hid)  << "Firmware version is less than 24081309";
        bit = 0x10;
        mask = 0xEF;
    }

    auto safeRead = [this](quint16 addr, quint8 defaultValue = 0) -> quint8 {
        auto result = usbXdataRead4Byte(addr);
        if (!result.second || result.first.isEmpty()) {
            qCWarning(log_host_hid) << "Failed to read from address:" << QString("0x%1").arg(addr, 4, 16, QChar('0'))
                                   << "returning default value:" << defaultValue;
            return defaultValue;
        }
        return static_cast<quint8>(result.first.at(0));
    };
    
    quint8 spdifout = safeRead(spdifout_addr);
    if (enable) {
        spdifout |= bit;
    } else {
        spdifout &= mask;
    }
    QByteArray data(4, 0); // Create a 4-byte array initialized to zero
    data[0] = spdifout;
    if(usbXdataWrite4Byte(spdifout_addr, data)){
        qCDebug(log_host_hid)  << "SPDIFOUT set successfully";
    }else{
        qCDebug(log_host_hid)  << "SPDIFOUT set failed";
    }
}

std::string VideoHid::getFirmwareVersion() {
    bool ok;
    int version_0 = 0, version_1 = 0, version_2 = 0, version_3 = 0;
    bool wasInTransaction = m_inTransaction;
    
    // Only begin transaction if not already in one
    if (!wasInTransaction && !beginTransaction()) {
        qCDebug(log_host_hid)  << "Failed to begin transaction for getFirmwareVersion";
        return "00000000";
    }
    
    try {
        // Define register addresses based on chip type
        uint16_t ver0_addr, ver1_addr, ver2_addr, ver3_addr;
        
        if (m_chipType == VideoChipType::MS2130S) {
            ver0_addr = MS2130S_ADDR_FIRMWARE_VERSION_0;
            ver1_addr = MS2130S_ADDR_FIRMWARE_VERSION_1;
            ver2_addr = MS2130S_ADDR_FIRMWARE_VERSION_2;
            ver3_addr = MS2130S_ADDR_FIRMWARE_VERSION_3;
        } else {
            // Default to MS2109 registers
            ver0_addr = ADDR_FIRMWARE_VERSION_0;
            ver1_addr = ADDR_FIRMWARE_VERSION_1;
            ver2_addr = ADDR_FIRMWARE_VERSION_2;
            ver3_addr = ADDR_FIRMWARE_VERSION_3;
        }
        
        // Read all version bytes
        version_0 = usbXdataRead4Byte(ver0_addr).first.toHex().toInt(&ok, 16);
        version_1 = usbXdataRead4Byte(ver1_addr).first.toHex().toInt(&ok, 16);
        version_2 = usbXdataRead4Byte(ver2_addr).first.toHex().toInt(&ok, 16);
        version_3 = usbXdataRead4Byte(ver3_addr).first.toHex().toInt(&ok, 16);
    }
    catch (...) {
        qCDebug(log_host_hid)  << "Exception occurred during firmware version read";
    }
    
    // Only end the transaction if we started it
    if (!wasInTransaction) {
        endTransaction();
    }
    
    return QString("%1%2%3%4").arg(version_0, 2, 10, QChar('0'))
                              .arg(version_1, 2, 10, QChar('0'))
                              .arg(version_2, 2, 10, QChar('0'))
                              .arg(version_3, 2, 10, QChar('0')).toStdString();
}

void VideoHid::fetchBinFileToString(QString &url, int timeoutMs){
    QNetworkAccessManager manager; // Create a network access manager
    QNetworkRequest request(url);  // Set up the request with the given URL
    QNetworkReply *reply = manager.get(request); // Send a GET request

    // Use QEventLoop to wait for the request to complete with timeout
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);

    QObject::connect(reply, &QNetworkReply::finished, &loop, [&]() {
        qCDebug(log_host_hid) << "Firmware download finished";
        loop.quit();
    });

    QObject::connect(&timer, &QTimer::timeout, &loop, [&]() {
        qCDebug(log_host_hid) << "Firmware download timed out";
        fireware_result = FirmwareResult::Timeout;
        reply->abort();
        loop.quit();
    });

    timer.start(timeoutMs > 0 ? timeoutMs : 5000); // Use provided timeout or default to 5 seconds
    loop.exec(); // Block until the request finishes or times out

    if (timer.isActive()) {
        timer.stop(); // Stop the timer if not triggered
    }

    // Check if timeout occurred before processing response
    if (fireware_result == FirmwareResult::Timeout) {
        qCDebug(log_host_hid) << "Firmware download timed out";
        reply->deleteLater();
        return;
    }

    std::string result; // Initialize an empty std::string to hold the result
    if (reply->error() == QNetworkReply::NoError) {
        // Read the binary data into a QByteArray
        QByteArray data = reply->readAll();
        // Convert QByteArray to std::string
        result = std::string(data.constData(), data.size());
        networkFirmware.assign(data.begin(), data.end());
        qCDebug(log_host_hid)  << "Successfully read file, size:" << data.size() << "bytes";
    } else {
        qCDebug(log_host_hid)  << "Failed to fetch latest firmware:" << reply->errorString();
        fireware_result = FirmwareResult::CheckFailed; // Set the result to check failed
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

QString VideoHid::getLatestFirmwareFilenName(QString &url, int timeoutMs) {
    QNetworkAccessManager manager;
    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", "MyFirmwareChecker/1.0");

    QNetworkReply *reply = manager.get(request);
    if (!reply) {
        qCDebug(log_host_hid) << "Failed to create network reply";
        fireware_result = FirmwareResult::CheckFailed;
        return QString();
    }else {
        fireware_result = FirmwareResult::Checking; // Set the initial state to checking
        qCDebug(log_host_hid) << "Network reply created successfully";
    }

    qCDebug(log_host_hid) << "Fetching latest firmware file name from" << url;

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);

    QObject::connect(reply, &QNetworkReply::finished, &loop, [&]() {
        qDebug(log_host_hid) << "Network reply finished";
        loop.quit();
    });

    QObject::connect(&timer, &QTimer::timeout, &loop, [&]() {
        qCDebug(log_host_hid) << "Request timed out";
        fireware_result = FirmwareResult::Timeout;
        reply->abort();
        reply->deleteLater();
        loop.quit();
    });

    timer.start(timeoutMs);
    loop.exec();

    if (timer.isActive()) {
        timer.stop(); // Stop the timer if not triggered
    }

    if (fireware_result == FirmwareResult::Timeout) {
        qCDebug(log_host_hid) << "Firmware check timed out";
        return QString(); // Already handled in timeout handler
    }

    if (reply->error() == QNetworkReply::NoError) {
        qCDebug(log_host_hid) << "Successfully fetched latest firmware";
        QString result = QString::fromUtf8(reply->readAll());
        fireware_result = FirmwareResult::CheckSuccess;
        reply->deleteLater();
        return result;
    } else {
        qCDebug(log_host_hid) << "Fail to get file name:" << reply->errorString();
        fireware_result = FirmwareResult::CheckFailed;
        reply->deleteLater();
        return QString();
    }
}

/*
 * Address: 0xFA8C bit0 indicates the HDMI connection status
 */
bool VideoHid::isHdmiConnected() {
    uint16_t status_addr;
    
    // Use the appropriate register based on chip type
    if (m_chipType == VideoChipType::MS2130S) {
        status_addr = MS2130S_ADDR_HDMI_CONNECTION_STATUS;
        qCDebug(log_host_hid) << "Using MS2130S HDMI status register:" << QString::number(status_addr, 16);
    } else {
        // Default to MS2109 register
        status_addr = ADDR_HDMI_CONNECTION_STATUS;
        qCDebug(log_host_hid) << "Using MS2109 HDMI status register:" << QString::number(status_addr, 16);
    }
    
    QPair<QByteArray, bool> result = usbXdataRead4Byte(status_addr);
    
    if (!result.second || result.first.isEmpty()) {
        qCWarning(log_host_hid) << "Failed to read HDMI connection status from address:" << QString::number(status_addr, 16);
        return false;
    }
    
    bool connected = result.first.at(0) & 0x01;
    qCDebug(log_host_hid) << "HDMI connected:" << connected << ", raw value:" << (int)result.first.at(0);
    return connected;
}

void VideoHid::setEventCallback(StatusEventCallback* callback) {
    eventCallback = callback;
}

void VideoHid::clearDevicePathCache() {
    qCDebug(log_host_hid) << "Clearing HID device path cache";
    m_cachedDevicePath.clear();
    m_lastPathQuery = std::chrono::steady_clock::now() - std::chrono::seconds(11); // Force refresh on next call
}

void VideoHid::refreshHIDDevice() {
    qCDebug(log_host_hid) << "Refreshing HID device connection";
    
    // Clear cached device path to force re-discovery
    clearDevicePathCache();
    
    // If we're currently in a transaction, restart it with the new device
    bool wasInTransaction = m_inTransaction;
    if (wasInTransaction) {
        endTransaction();
        if (!beginTransaction()) {
            qCWarning(log_host_hid) << "Failed to restart HID transaction after refresh";
        }
    }
}

QPair<QByteArray, bool> VideoHid::usbXdataRead4Byte(quint16 u16_address) {
    // Different approaches for different chip types
    qCDebug(log_host_hid) << "usbXdataRead4Byte called for address:" << QString("0x%1").arg(u16_address, 4, 16, QChar('0'))
                         << "chip type:" << (m_chipType == VideoChipType::MS2109 ? "MS2109" : 
                                           m_chipType == VideoChipType::MS2130S ? "MS2130S" : "Unknown");
    auto result = (m_chipType == VideoChipType::MS2130S) ? usbXdataRead4ByteMS2130S(u16_address) : usbXdataRead4ByteMS2109(u16_address);
    
    // Normalize the result to have the data byte at position 0
    if (result.second && !result.first.isEmpty()) {
        quint8 dataByte;
        if (m_chipType == VideoChipType::MS2130S) {
            // MS2130S method already extracts data to position 0
            dataByte = result.first.at(0);
        } else {
            // MS2109 returns data at position 0
            dataByte = result.first.at(0);
        }
        return qMakePair(QByteArray(1, dataByte), true);
    } else {
        return qMakePair(QByteArray(1, 0), false);
    }
}

QPair<QByteArray, bool> VideoHid::usbXdataRead4ByteMS2130S(quint16 u16_address) {
    // For MS2130S chip - use multiple approaches with strict error handling
    bool wasInTransaction = m_inTransaction;
    
    if (!wasInTransaction) {
        if (!beginTransaction()) {
            qCWarning(log_host_hid) << "Failed to begin transaction for MS2130S read";
            return qMakePair(QByteArray(4, 0), false);
        }
    }
    
    // Define several strategies with different buffer sizes and report IDs
    QByteArray readResult(1, 0);
    bool success = false;
    QString valueHex;
    
    qCDebug(log_host_hid) << "MS2130S reading from address:" << QString("0x%1").arg(u16_address, 4, 16, QChar('0'));
    
    // Strategy 1: Standard buffer (11 bytes) with report ID 0
    if (!success) {
        QByteArray ctrlData(11, 0);
        QByteArray result(11, 0);
        
        ctrlData[0] = 0x00;  // Report ID
        ctrlData[1] = MS2130S_CMD_XDATA_READ;
        ctrlData[2] = static_cast<char>((u16_address >> 8) & 0xFF);
        ctrlData[3] = static_cast<char>(u16_address & 0xFF);
        
        // Use Windows-specific methods for more direct control
        #ifdef _WIN32
        bool openedForOperation = m_inTransaction || openHIDDeviceHandle();
        if (openedForOperation) {
            BYTE buffer[11] = {0};
            memcpy(buffer, ctrlData.data(), 11);
            
            if (sendFeatureReportWindows(buffer, 11)) {
                // Clear the receive buffer
                memset(buffer, 0, sizeof(buffer));
                buffer[0] = 0x00;  // Report ID must be preserved
                
                if (getFeatureReportWindows(buffer, 11)) {
                    readResult[0] = buffer[4];
                    valueHex = QString("0x%1").arg((quint8)readResult[0], 2, 16, QChar('0'));
                    qCDebug(log_host_hid) << "MS2130S direct Windows read success from address:" 
                                         << QString("0x%1").arg(u16_address, 4, 16, QChar('0')) 
                                         << "value:" << valueHex;
                    success = true;
                }
            }
            
            if (!m_inTransaction) {
                closeHIDDeviceHandle();
            }
        }
        #else
        // Standard approach for other platforms
        if (this->sendFeatureReport((uint8_t*)ctrlData.data(), ctrlData.size())) {
            if (this->getFeatureReport((uint8_t*)result.data(), result.size())) {
                readResult[0] = result[4];
                valueHex = QString("0x%1").arg((quint8)readResult[0], 2, 16, QChar('0'));
                qCDebug(log_host_hid) << "MS2130S read success (standard buffer) from address:" 
                                     << QString("0x%1").arg(u16_address, 4, 16, QChar('0')) 
                                     << "value:" << valueHex;
                success = true;
            }
        }
        #endif
    }
    
    // Strategy 2: Try with report ID 1 if strategy 1 failed
    if (!success) {
        QByteArray ctrlData(11, 0);
        QByteArray result(11, 0);
        
        ctrlData[0] = 0x01;  // Report ID 1
        ctrlData[1] = MS2130S_CMD_XDATA_READ;
        ctrlData[2] = static_cast<char>((u16_address >> 8) & 0xFF);
        ctrlData[3] = static_cast<char>(u16_address & 0xFF);
        
        #ifdef _WIN32
        bool openedForOperation = m_inTransaction || openHIDDeviceHandle();
        if (openedForOperation) {
            BYTE buffer[11] = {0};
            memcpy(buffer, ctrlData.data(), 11);
            
            if (sendFeatureReportWindows(buffer, 11)) {
                // Clear the receive buffer
                memset(buffer, 0, sizeof(buffer));
                buffer[0] = 0x01;  // Report ID must be preserved
                
                if (getFeatureReportWindows(buffer, 11)) {
                    readResult[0] = buffer[4];
                    valueHex = QString("0x%1").arg((quint8)readResult[0], 2, 16, QChar('0'));
                    qCDebug(log_host_hid) << "MS2130S direct Windows read success (report ID 1) from address:" 
                                         << QString("0x%1").arg(u16_address, 4, 16, QChar('0')) 
                                         << "value:" << valueHex;
                    success = true;
                }
            }
            
            if (!m_inTransaction) {
                closeHIDDeviceHandle();
            }
        }
        #else
        if (this->sendFeatureReport((uint8_t*)ctrlData.data(), ctrlData.size())) {
            if (this->getFeatureReport((uint8_t*)result.data(), result.size())) {
                readResult[0] = result[4];
                valueHex = QString("0x%1").arg((quint8)readResult[0], 2, 16, QChar('0'));
                qCDebug(log_host_hid) << "MS2130S read success (report ID 1) from address:" 
                                     << QString("0x%1").arg(u16_address, 4, 16, QChar('0')) 
                                     << "value:" << valueHex;
                success = true;
            }
        }
        #endif
    }
    
    // Strategy 3: Try with a standard 65-byte buffer as last resort
    if (!success) {
        QByteArray ctrlData(65, 0);
        QByteArray result(65, 0);
        
        ctrlData[0] = 0x00;  // Report ID
        ctrlData[1] = MS2130S_CMD_XDATA_READ;
        ctrlData[2] = static_cast<char>((u16_address >> 8) & 0xFF);
        ctrlData[3] = static_cast<char>(u16_address & 0xFF);
        
        if (this->sendFeatureReport((uint8_t*)ctrlData.data(), ctrlData.size())) {
            if (this->getFeatureReport((uint8_t*)result.data(), result.size())) {
                readResult[0] = result[4];
                valueHex = QString("0x%1").arg((quint8)readResult[0], 2, 16, QChar('0'));
                qCDebug(log_host_hid) << "MS2130S read success (large buffer) from address:" 
                                     << QString("0x%1").arg(u16_address, 4, 16, QChar('0')) 
                                     << "value:" << valueHex;
                success = true;
            }
        }
    }
    
    // End the transaction if we started it
    if (!wasInTransaction) {
        endTransaction();
    }
    
    if (!success) {
        qCWarning(log_host_hid) << "MS2130S all read attempts failed for address:" << QString("0x%1").arg(u16_address, 4, 16, QChar('0'));
        return qMakePair(QByteArray(4, 0), false);
    }
    
    // Create a 4-byte result for compatibility with existing code expecting that format
    QByteArray finalResult(4, 0);
    finalResult[0] = readResult[0];
    
    return qMakePair(finalResult, true);
}

QPair<QByteArray, bool> VideoHid::usbXdataRead4ByteMS2109(quint16 u16_address) {
    QByteArray ctrlData(9, 0); // Initialize with 9 bytes set to 0
    QByteArray result(9, 0);

    ctrlData[1] = CMD_XDATA_READ;
    ctrlData[2] = static_cast<char>((u16_address >> 8) & 0xFF);
    ctrlData[3] = static_cast<char>(u16_address & 0xFF);
    
    qCDebug(log_host_hid) << "MS2109 reading from address:" << QString("0x%1").arg(u16_address, 4, 16, QChar('0'));
    
    // 0: Some devices use report ID 0 to indicate that no specific report ID is used.
    if (this->sendFeatureReport((uint8_t*)ctrlData.data(), ctrlData.size())) {
        if (this->getFeatureReport((uint8_t*)result.data(), result.size())) {
            QByteArray readResult = result.mid(4, 1);
            qCDebug(log_host_hid) << "MS2109 read success from address:" << QString("0x%1").arg(u16_address, 4, 16, QChar('0')) 
                                 << "value:" << QString("0x%1").arg((quint8)readResult.at(0), 2, 16, QChar('0'));
            return qMakePair(readResult, true);
        }
    } else {
        // 1: Some devices use report ID 1 to indicate that no specific report ID is used.
        ctrlData[0] = 0x01;
        if (this->sendFeatureReport((uint8_t*)ctrlData.data(), ctrlData.size())) {
            bool getSuccess = this->getFeatureReport((uint8_t*)result.data(), result.size());

            if (getSuccess && !result.isEmpty()) {
                QByteArray readResult = result.mid(3, 4);
                qCDebug(log_host_hid) << "MS2109 read success (alt method) from address:" << QString("0x%1").arg(u16_address, 4, 16, QChar('0')) 
                                     << "value:" << readResult.toHex();
                return qMakePair(readResult, true);
            } else {
                qCWarning(log_host_hid) << "MS2109 failed to get feature report for address:" << QString("0x%1").arg(u16_address, 4, 16, QChar('0'));
            }
        } else {
            qCWarning(log_host_hid) << "Failed to send feature report (alt method) for address:" << QString("0x%1").arg(u16_address, 4, 16, QChar('0'));
        }
    }
    return qMakePair(QByteArray(4, 0), false); // Return 4 bytes set to 0 and false
}

bool VideoHid::usbXdataWrite4Byte(quint16 u16_address, QByteArray data) {
    // Different control data size for different chip types
    QByteArray ctrlData;
    
    // Select appropriate command and format based on chip type
    if (m_chipType == VideoChipType::MS2130S) {
        // MS2130S might need a different format or report ID
        // Try with standard 9-byte structure first
        ctrlData = QByteArray(9, 0); // Initialize with 9 bytes set to 0
        ctrlData[0] = 0x00; // Report ID - explicitly set to 0
        ctrlData[1] = MS2130S_CMD_XDATA_WRITE;
        ctrlData[2] = static_cast<char>((u16_address >> 8) & 0xFF);
        ctrlData[3] = static_cast<char>(u16_address & 0xFF);
        ctrlData.replace(4, 4, data);
    } else {
        // MS2109 standard format
        ctrlData = QByteArray(9, 0); // Initialize with 9 bytes set to 0
        ctrlData[1] = CMD_XDATA_WRITE;
        ctrlData[2] = static_cast<char>((u16_address >> 8) & 0xFF);
        ctrlData[3] = static_cast<char>(u16_address & 0xFF);
        ctrlData.replace(4, 4, data);
    }

    // Display debug information about the write operation
    QString hexData = data.toHex(' ').toUpper();
    qCDebug(log_host_hid) << "Writing to address:" << QString("0x%1").arg(u16_address, 4, 16, QChar('0'))
                         << "for chip type:" << (m_chipType == VideoChipType::MS2109 ? "MS2109" :
                                                m_chipType == VideoChipType::MS2130S ? "MS2130S" : "Unknown")
                         << "data:" << hexData
                         << "report buffer:" << ctrlData.toHex(' ').toUpper();

    bool result = this->sendFeatureReport((uint8_t*)ctrlData.data(), ctrlData.size());
    if (!result) {
        qCWarning(log_host_hid) << "Failed to write to address:" << QString("0x%1").arg(u16_address, 4, 16, QChar('0'));
        
        // For MS2130S, if the standard approach fails, try an alternative format
        if (m_chipType == VideoChipType::MS2130S && !result) {
            qCDebug(log_host_hid) << "Trying alternative format for MS2130S write operation";
            
            // Try an alternative format - some devices need a different structure
            QByteArray altCtrlData = QByteArray(65, 0); // Try with larger buffer
            altCtrlData[0] = 0x00; // Report ID
            altCtrlData[1] = MS2130S_CMD_XDATA_WRITE;
            altCtrlData[2] = static_cast<char>((u16_address >> 8) & 0xFF);
            altCtrlData[3] = static_cast<char>(u16_address & 0xFF);
            altCtrlData.replace(4, 4, data);
            
            qCDebug(log_host_hid) << "Trying alternative write with buffer size:" << altCtrlData.size();
            result = this->sendFeatureReport((uint8_t*)altCtrlData.data(), altCtrlData.size());
            
            if (result) {
                qCDebug(log_host_hid) << "Alternative format succeeded for MS2130S";
            } else {
                qCWarning(log_host_hid) << "Alternative format also failed for MS2130S";
            }
        }
    }
    return result;
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
    int retries = 2;
    while (retries-- > 0) {
        if (sendFeatureReportWindows(buffer, bufferLength)) {
            return true;
        }
        qCDebug(log_host_hid)  << "Retrying sendFeatureReportWindows...";
    }
    return false;
#elif __linux__
    int retries = 2;
    while (retries-- > 0) {
        if (sendFeatureReportLinux(buffer, bufferLength)) {
            return true;
        }
        qCDebug(log_host_hid)  << "Retrying sendFeatureReportLinux...";
    }
    return false;
#endif
}

void VideoHid::closeHIDDeviceHandle() {
    QMutexLocker locker(&m_deviceHandleMutex);
    
    #ifdef _WIN32
        if (deviceHandle != INVALID_HANDLE_VALUE) {
            qCDebug(log_host_hid) << "Closing HID device handle...";
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

bool VideoHid::beginTransaction() {
    if (m_inTransaction) {
        qCDebug(log_host_hid)  << "Transaction already in progress";
        return true;  // Already in a transaction
    }
    
    // We'll retry a few times in case of temporary device issues
    const int MAX_RETRIES = 3;
    bool success = false;
    
    for (int attempt = 0; attempt < MAX_RETRIES && !success; attempt++) {
        if (attempt > 0) {
            qCDebug(log_host_hid) << "Retry attempt" << attempt << "to open HID device";
            // Wait a short time between retries
            QThread::msleep(100); 
        }
        
        #ifdef _WIN32
            success = openHIDDeviceHandle();
        #elif __linux__
            success = openHIDDevice();
        #endif
    }

    if (success) {
        m_inTransaction = true;
        
        // Detect chip type if it's unknown
        if (m_chipType == VideoChipType::UNKNOWN) {
            detectChipType();
            qCDebug(log_host_hid) << "Detected chip type:" << (m_chipType == VideoChipType::MS2130S ? "MS2130S" : 
                                                              m_chipType == VideoChipType::MS2109 ? "MS2109" : "Unknown");
            
            // Add stabilization delay for both chip types to ensure device is ready
            if (m_chipType == VideoChipType::MS2130S) {
                qCDebug(log_host_hid) << "Performing MS2130S-specific initialization";
                QThread::msleep(100);
            } else if (m_chipType == VideoChipType::MS2109) {
                qCDebug(log_host_hid) << "Performing MS2109-specific initialization";
                QThread::msleep(100);
            }
        }
        
        qCDebug(log_host_hid)  << "HID transaction started";
        return true;
    } else {
        qCWarning(log_host_hid)  << "Failed to start HID transaction after" << MAX_RETRIES << "attempts";
        return false;
    }
}

void VideoHid::endTransaction() {
    if (m_inTransaction) {
        // For MS2130S, perform any cleanup or specific finalization
        if (m_chipType == VideoChipType::MS2130S) {
            qCDebug(log_host_hid) << "Performing MS2130S-specific cleanup before closing transaction";
            // Add a short delay to ensure any pending operations complete
            QThread::msleep(10);
        }
        
        closeHIDDeviceHandle();
        m_inTransaction = false;
        qCDebug(log_host_hid) << "HID transaction ended";
    }
}

bool VideoHid::isInTransaction() const {
    return m_inTransaction;
}

void VideoHid::connectToHotplugMonitor()
{
    qCDebug(log_host_hid) << "Connecting VideoHid to hotplug monitor";
    
    // Get the hotplug monitor from DeviceManager
    DeviceManager& deviceManager = DeviceManager::getInstance();
    HotplugMonitor* hotplugMonitor = deviceManager.getHotplugMonitor();
    
    if (!hotplugMonitor) {
        qCWarning(log_host_hid) << "Failed to get hotplug monitor from device manager";
        return;
    }
    
    // Connect to device unplugging signal
    connect(hotplugMonitor, &HotplugMonitor::deviceUnplugged,
            this, [this](const DeviceInfo& device) {
                qCDebug(log_host_hid) << "VideoHid: Attempting HID device deactivation for unplugged device port:" << device.portChain;
                
                // Only deactivate HID device if the device has a HID component
                if (!device.hasHidDevice()) {
                    qCDebug(log_host_hid) << "Device at port" << device.portChain << "has no HID component, skipping HID deactivation";
                    return;
                }
                
                // Check if the unplugged device matches the current HID device
                if (m_currentHIDPortChain == device.portChain) {
                    qCInfo(log_host_hid) << "Stopping HID device for unplugged device at port:" << device.portChain;
                    QString oldPath = m_currentHIDDevicePath;
                    // Defer stop() to avoid blocking the hotplug thread
                    QTimer::singleShot(0, this, [this, oldPath]() {
                        stop();
                        emit hidDeviceDisconnected(oldPath);
                    });
                    qCInfo(log_host_hid) << "✓ HID device stop scheduled for unplugged device at port:" << device.portChain;
                } else {
                    qCDebug(log_host_hid) << "HID device deactivation skipped - port chain mismatch. Current:" << m_currentHIDPortChain << "Unplugged:" << device.portChain;
                }
            });
            
    // Connect to new device plugged in signal
    connect(hotplugMonitor, &HotplugMonitor::newDevicePluggedIn,
            this, [this](const DeviceInfo& device) {
                qCDebug(log_host_hid) << "VideoHid: Attempting HID device auto-switch for new device port:" << device.portChain;
                
                // Only attempt auto-switch if the device has a HID component
                if (!device.hasHidDevice()) {
                    qCDebug(log_host_hid) << "Device at port" << device.portChain << "has no HID component, skipping HID auto-switch";
                    return;
                }
                
                // Check if there's currently an active HID device
                if (isInTransaction()) {
                    qCDebug(log_host_hid) << "HID device already active, skipping auto-switch to port:" << device.portChain;
                    return;
                }
                
                qCDebug(log_host_hid) << "No active HID device found, attempting to switch to new device";
                
                // Switch to the new HID device
                bool switchSuccess = switchToHIDDeviceByPortChain(device.portChain);
                if (switchSuccess) {
                    qCInfo(log_host_hid) << "✓ HID device auto-switched to new device at port:" << device.portChain;
                    
                    // Defer the start and stabilization to avoid blocking the hotplug thread
                    QTimer::singleShot(0, this, [this]() {
                        // Add longer delay to allow device to fully stabilize after plugging in
                        qCDebug(log_host_hid) << "Waiting for device stabilization after hotplug...";
                        QThread::msleep(500);
                        
                        // Explicitly detect chip type when new device is plugged in
                        // Note: This is also called in switchToHIDDeviceByPortChain, but we call it again here
                        // for redundancy and to ensure it's explicitly tied to the new device plugged in event
                        detectChipType();
                        qCInfo(log_host_hid) << "Verified chip type on new device: " << 
                            (m_chipType == VideoChipType::MS2130S ? "MS2130S" : 
                             m_chipType == VideoChipType::MS2109 ? "MS2109" : "Unknown");
                        
                        // Start the HID device
                        start();
                        emit hidDeviceConnected(m_currentHIDDevicePath);
                    });
                } else {
                    qCDebug(log_host_hid) << "HID device auto-switch failed for port:" << device.portChain;
                }
            });
            
    qCDebug(log_host_hid) << "VideoHid successfully connected to hotplug monitor";
}

void VideoHid::disconnectFromHotplugMonitor()
{
    qCDebug(log_host_hid) << "Disconnecting VideoHid from hotplug monitor";
    
    // Get the hotplug monitor from DeviceManager
    DeviceManager& deviceManager = DeviceManager::getInstance();
    HotplugMonitor* hotplugMonitor = deviceManager.getHotplugMonitor();
    
    if (hotplugMonitor) {
        disconnect(hotplugMonitor, nullptr, this, nullptr);
        qCDebug(log_host_hid) << "VideoHid disconnected from hotplug monitor";
    }
}

bool VideoHid::readChunk(quint16 address, QByteArray &data, int chunkSize) {
    const int REPORT_SIZE = 9;
    QByteArray ctrlData(REPORT_SIZE, 0);
    QByteArray result(REPORT_SIZE, 0);

    ctrlData[1] = CMD_EEPROM_READ;
    ctrlData[2] = static_cast<char>((address >> 8) & 0xFF);
    ctrlData[3] = static_cast<char>(address & 0xFF);

    if (sendFeatureReport((uint8_t*)ctrlData.data(), ctrlData.size())) {
        if (getFeatureReport((uint8_t*)result.data(), result.size())) {
            data.append(result.mid(4, chunkSize));
            read_size += chunkSize;
            emit firmwareReadChunkComplete(read_size);
            return true;
        }
    }
    qWarning() << "Failed to read chunk from address:" << QString("0x%1").arg(address, 4, 16, QChar('0'));
    return false;
}

QByteArray VideoHid::readEeprom(quint16 address, quint32 size) {
    const int MAX_CHUNK = 1;
    const int MAX_RETRIES = 3; // Number of retries for failed reads
    QByteArray firmwareData;
    read_size = 0;

    // Begin transaction for the entire operation
    if (!beginTransaction()) {
        qCDebug(log_host_hid) << "Failed to begin transaction for EEPROM read";
        emit firmwareReadError("Failed to begin transaction for EEPROM read");
        return QByteArray();
    }

    bool success = true;
    quint16 currentAddress = address;
    quint32 bytesRemaining = size;

    while (bytesRemaining > 0 && success) {
        int chunkSize = qMin(MAX_CHUNK, static_cast<int>(bytesRemaining));
        QByteArray chunk;
        bool chunkSuccess = false;
        int retries = MAX_RETRIES;

        // Retry reading the chunk up to MAX_RETRIES times
        while (retries > 0 && !chunkSuccess) {
            chunkSuccess = readChunk(currentAddress, chunk, chunkSize);
            if (!chunkSuccess) {
                retries--;
                qCDebug(log_host_hid) << "Retry" << (MAX_RETRIES - retries) << "of" << MAX_RETRIES
                                      << "for reading chunk at address:" << QString("0x%1").arg(currentAddress, 4, 16, QChar('0'));
                QThread::msleep(15); // Short delay before retrying
            }
        }

        if (chunkSuccess) {
            firmwareData.append(chunk);
            currentAddress += chunkSize;
            bytesRemaining -= chunkSize;
            emit firmwareReadProgress((read_size * 100) / size);
            if (read_size % 64 == 0) {
                qCDebug(log_host_hid) << "Read size:" << read_size;
            }
            QThread::msleep(5); // Add 5ms delay between successful reads
        } else {
            qCDebug(log_host_hid) << "Failed to read chunk from EEPROM at address:" << QString("0x%1").arg(currentAddress, 4, 16, QChar('0'))
                                  << "after" << MAX_RETRIES << "retries";
            success = false;
            break;
        }
    }

    // End transaction
    endTransaction();

    if (!success) {
        qCDebug(log_host_hid) << "EEPROM read failed";
        emit firmwareReadError("Failed to read firmware from EEPROM");
        return QByteArray();
    }

    return firmwareData;
}

quint32 VideoHid::readFirmwareSize(){
    QByteArray header = readEeprom(ADDR_EEPROM, 4);
    if (header.size() != 4) {
        qDebug() << "Can not read firemware header form eeprom:" << header.size();
        emit firmwareReadError("Can not read firemware header form eeprom");
        return 0;
    }

    quint16 sizeBytes = (static_cast<quint8>(header[2]) << 8) + static_cast<quint8>(header[3]);
    quint32 firmwareSize = sizeBytes + 52;
    qDebug() << "Caculate firmware size:" << firmwareSize << " bytes";
    
    return firmwareSize;
}

void VideoHid::loadEepromToFile(const QString &filePath) {
    quint32 firmwareSize = readFirmwareSize();

    QThread* thread = new QThread();
    FirmwareReader *worker = new FirmwareReader(this, ADDR_EEPROM, firmwareSize, filePath);
    worker->moveToThread(thread);

    connect(thread, &QThread::started, worker, &FirmwareReader::process);
    connect(worker, &FirmwareReader::finished, thread, &QThread::quit);
    connect(worker, &FirmwareReader::finished, worker, &FirmwareReader::deleteLater);
    connect(thread, &QThread::finished, thread, &QThread::deleteLater);


    connect(worker, &FirmwareReader::finished, this, [this](bool success) {
        if (success) {
            qCDebug(log_host_hid) << "Firmware read completed successfully";
            emit firmwareReadComplete(true);
        } else {
            qCDebug(log_host_hid) << "Firmware read failed - user should try again";
            emit firmwareReadComplete(false);

        }
    });
    
    thread->start();
}

QString VideoHid::findMatchingHIDDevice(const QString& portChain) const
{
    // Check if we have a cached path that's still valid
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - m_lastPathQuery).count();
    
#ifdef _WIN32
    if (!m_cachedDevicePath.empty() && elapsed < 10) {
        // Return cached path if it's less than 10 seconds old
        QString cachedPath = QString::fromStdWString(m_cachedDevicePath);
        qCDebug(log_host_hid) << "Using cached HID device path:" << cachedPath;
        return cachedPath;
    }
#elif __linux__
    if (!m_cachedDevicePath.isEmpty() && elapsed < 10) {
        // Return cached path if it's less than 10 seconds old
        qCDebug(log_host_hid) << "Using cached HID device path:" << m_cachedDevicePath;
        return m_cachedDevicePath;
    }
#endif

    // Update the last query time (need to cast away const since this is conceptually a const operation)
    const_cast<VideoHid*>(this)->m_lastPathQuery = now;

    if (portChain.isEmpty()) {
        qCDebug(log_host_hid) << "Empty port chain provided";
        return QString();
    }

    qCDebug(log_host_hid) << "Finding HID device matching port chain:" << portChain;

    // Use DeviceManager to look up device information by port chain
    DeviceManager& deviceManager = DeviceManager::getInstance();
    
    // Retry logic for HID device detection - HID devices may take time to enumerate after hotplug
    const int maxRetries = 3;
    const int retryDelayMs = 200; // 200ms delay between retries
    
    for (int retry = 0; retry < maxRetries; ++retry) {
        if (retry > 0) {
            qCDebug(log_host_hid) << "HID device not found, retrying in" << retryDelayMs << "ms (attempt" << (retry + 1) << "/" << maxRetries << ")";
            QThread::msleep(retryDelayMs);
            
            // Force fresh device discovery on retry
            deviceManager.discoverDevices();
        }
        
        QList<DeviceInfo> devices = deviceManager.getDevicesByPortChain(portChain);
        
        if (devices.isEmpty()) {
            qCWarning(log_host_hid) << "No devices found for port chain:" << portChain;
            continue; // Try again
        }

        qCDebug(log_host_hid) << "Found" << devices.size() << "device(s) for port chain:" << portChain;

        // Look for a device that has HID information
        DeviceInfo selectedDevice;
        for (const DeviceInfo& device : devices) {
            if (!device.hidDevicePath.isEmpty()) {
                selectedDevice = device;
                qCDebug(log_host_hid) << "Found device with HID info:" 
                         << "hidDevicePath:" << device.hidDevicePath;
                break;
            }
        }

        if (selectedDevice.isValid() && !selectedDevice.hidDevicePath.isEmpty()) {
            qCDebug(log_host_hid) << "Selected HID device path:" << selectedDevice.hidDevicePath;
            
            // Cache the found device path
#ifdef _WIN32
            const_cast<VideoHid*>(this)->m_cachedDevicePath = selectedDevice.hidDevicePath.toStdWString();
#elif __linux__
            const_cast<VideoHid*>(this)->m_cachedDevicePath = selectedDevice.hidDevicePath;
#endif
            
            return selectedDevice.hidDevicePath;
        }
        
        // If we get here, no HID device was found on this attempt
        if (retry < maxRetries - 1) {
            qCWarning(log_host_hid) << "No device with HID information found for port chain:" << portChain 
                                   << "(attempt" << (retry + 1) << "/" << maxRetries << ")";
        }
    }
    
    // All retries failed
    qCWarning(log_host_hid) << "No device with HID information found for port chain:" << portChain 
                           << "after" << maxRetries << "attempts";
    return QString();
}

QString VideoHid::getCurrentHIDDevicePath() const
{
    return m_currentHIDDevicePath;
}

QString VideoHid::getCurrentHIDPortChain() const
{
    return m_currentHIDPortChain;
}

bool VideoHid::switchToHIDDeviceByPortChain(const QString& portChain)
{
    if (portChain.isEmpty()) {
        qCWarning(log_host_hid) << "Cannot switch to HID device with empty port chain";
        return false;
    }

    qCDebug(log_host_hid) << "Attempting to switch to HID device by port chain:" << portChain;

    try {
        QString targetHIDPath = findMatchingHIDDevice(portChain);
        
        if (targetHIDPath.isEmpty()) {
            qCWarning(log_host_hid) << "No matching HID device found for port chain:" << portChain;
            return false;
        }

        qCDebug(log_host_hid) << "Found matching HID device path:" << targetHIDPath << "for port chain:" << portChain;

        // Check if we're already using this device - avoid unnecessary switching
        if (!m_currentHIDDevicePath.isEmpty() && m_currentHIDDevicePath == targetHIDPath) {
            qCDebug(log_host_hid) << "Already using HID device:" << targetHIDPath << "- skipping switch";
            return true;
        }

        QString previousDevicePath = m_currentHIDDevicePath;
        QString previousPortChain = m_currentHIDPortChain;
        
        qCDebug(log_host_hid) << "Switching HID device from" << previousDevicePath 
                             << "to" << targetHIDPath;

        // Close current HID device if open
        bool wasInTransaction = m_inTransaction;
        if (wasInTransaction) {
            qCDebug(log_host_hid) << "Closing current HID device before switch";
            endTransaction();
        }

        // Update current device tracking
        m_currentHIDDevicePath = targetHIDPath;
        m_currentHIDPortChain = portChain;
        
        // Clear cached device path to force re-discovery with new device
        clearDevicePathCache();
        
#ifdef _WIN32
        m_cachedDevicePath = targetHIDPath.toStdWString();
#elif __linux__
        m_cachedDevicePath = targetHIDPath;
#endif

        // Re-open HID device with new path if it was previously open
        bool switchSuccess = true;
        if (wasInTransaction) {
            qCDebug(log_host_hid) << "Re-opening HID device with new path";
            switchSuccess = beginTransaction();
            if (!switchSuccess) {
                qCWarning(log_host_hid) << "Failed to re-open HID device after switch";
                // Revert to previous device info on failure
                m_currentHIDDevicePath = previousDevicePath;
                m_currentHIDPortChain = previousPortChain;
            }
        }

        if (switchSuccess) {
            // Update global settings to remember the new device
            GlobalSetting::instance().setOpenterfacePortChain(portChain);
            
            // Emit signals for HID device switching
            emit hidDeviceChanged(previousDevicePath, targetHIDPath);
            emit hidDeviceSwitched(previousPortChain, portChain);
            emit hidDeviceConnected(targetHIDPath);
            
            if (!previousDevicePath.isEmpty()) {
                emit hidDeviceDisconnected(previousDevicePath);
            }
            
            qCDebug(log_host_hid) << "HID device switch successful to:" << targetHIDPath;
            
            // After successful switch, detect the chip type
            detectChipType();
            qCDebug(log_host_hid) << "Detected chip type:" << (m_chipType == VideoChipType::MS2130S ? "MS2130S" : 
                                                              m_chipType == VideoChipType::MS2109 ? "MS2109" : "Unknown");
        }
        
        return switchSuccess;

    } catch (const std::exception& e) {
        qCritical() << "Exception in switchToHIDDeviceByPortChain:" << e.what();
        return false;
    } catch (...) {
        qCritical() << "Unknown exception in switchToHIDDeviceByPortChain";
        return false;
    }
}

#ifdef _WIN32
std::wstring VideoHid::getHIDDevicePath() {
    QString portChain = GlobalSetting::instance().getOpenterfacePortChain();
    QString hidPath = findMatchingHIDDevice(portChain);
    
    if (!hidPath.isEmpty()) {
        // For MS2130S devices with VID_345F & PID_2132, we need to get the full device path
        // that Windows can use with CreateFileW, not just the device instance path
        if (hidPath.contains("VID_345F", Qt::CaseInsensitive) && 
            hidPath.contains("PID_2132", Qt::CaseInsensitive)) {
            // We need to get the proper path for this device using SetupDi functions
            std::wstring fullPath = getProperDevicePath(hidPath.toStdWString());
            if (!fullPath.empty()) {
                qCDebug(log_host_hid) << "Using proper device path for MS2130S:" 
                                     << QString::fromStdWString(fullPath);
                return fullPath;
            }
        }
        return hidPath.toStdWString();
    }
    
    // Fallback to original VID/PID enumeration method
    qCDebug(log_host_hid) << "Falling back to VID/PID enumeration for HID device discovery";
    
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

std::wstring VideoHid::getProperDevicePath(const std::wstring& deviceInstancePath) {
    GUID hidGuid;
    HidD_GetHidGuid(&hidGuid);
    
    // Convert wstring to QString for easier comparison and analysis
    QString instancePathStr = QString::fromStdWString(deviceInstancePath);
    qCDebug(log_host_hid) << "Looking for proper device path for:" << instancePathStr;
    
    // Check if path already starts with "\\?\hid" which indicates it's already a proper device path
    if (instancePathStr.startsWith("\\\\?\\hid", Qt::CaseInsensitive) || 
        instancePathStr.startsWith("\\\\.\\hid", Qt::CaseInsensitive)) {
        qCDebug(log_host_hid) << "Path already appears to be a proper device path";
        // Try to extract VID/PID information for logging
        if (instancePathStr.contains("VID_", Qt::CaseInsensitive) && instancePathStr.contains("PID_", Qt::CaseInsensitive)) {
            qCDebug(log_host_hid) << "Using existing device path with VID/PID in it";
            return deviceInstancePath;
        }
    }
    
    // Parse VID and PID from the original path if available
    uint16_t targetVid = 0x345F;  // Default for MS2130S
    uint16_t targetPid = 0x2132;  // Default for MS2130S
    QString mi;
    
    // Extract VID and PID using simple string search since QRegularExpression might not be available
    if (instancePathStr.contains("VID_", Qt::CaseInsensitive) && 
        instancePathStr.contains("PID_", Qt::CaseInsensitive)) {
        
        int vidIndex = instancePathStr.indexOf("VID_", 0, Qt::CaseInsensitive) + 4;
        int pidIndex = instancePathStr.indexOf("PID_", 0, Qt::CaseInsensitive) + 4;
        
        if (vidIndex > 4 && pidIndex > 4) {
            bool vidOk = false, pidOk = false;
            QString vidStr = instancePathStr.mid(vidIndex, 4);
            QString pidStr = instancePathStr.mid(pidIndex, 4);
            
            targetVid = vidStr.toUInt(&vidOk, 16);
            targetPid = pidStr.toUInt(&pidOk, 16);
            
            if (vidOk && pidOk) {
                qCDebug(log_host_hid) << "Extracted VID:" << QString("0x%1").arg(targetVid, 4, 16, QChar('0'))
                                      << "PID:" << QString("0x%1").arg(targetPid, 4, 16, QChar('0'));
            }
        }
    }
    
    // Extract MI (interface) value if present
    if (instancePathStr.contains("MI_", Qt::CaseInsensitive)) {
        int miIndex = instancePathStr.indexOf("MI_", 0, Qt::CaseInsensitive) + 3;
        if (miIndex > 3) {
            mi = instancePathStr.mid(miIndex, 2);
            qCDebug(log_host_hid) << "Extracted MI:" << mi;
        }
    }
    
    // Get a handle to a device information set for all present HID devices
    HDEVINFO deviceInfoSet = SetupDiGetClassDevs(&hidGuid, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    
    if (deviceInfoSet == INVALID_HANDLE_VALUE) {
        qCDebug(log_host_hid) << "Failed to get device info set";
        return L"";
    }
    
    SP_DEVICE_INTERFACE_DATA deviceInterfaceData;
    deviceInterfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);
    
    std::wstring properPath = L"";
    
    // Enumerate through all devices in the set
    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(deviceInfoSet, NULL, &hidGuid, i, &deviceInterfaceData); i++) {
        DWORD requiredSize = 0;
        
        // Get the size required for the device interface detail
        SetupDiGetDeviceInterfaceDetail(deviceInfoSet, &deviceInterfaceData, NULL, 0, &requiredSize, NULL);
        
        if (requiredSize == 0) {
            continue;
        }
        
        PSP_DEVICE_INTERFACE_DETAIL_DATA deviceInterfaceDetailData = 
            (PSP_DEVICE_INTERFACE_DETAIL_DATA)malloc(requiredSize);
        if (!deviceInterfaceDetailData) {
            continue;
        }
        
        deviceInterfaceDetailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);
        
        // Now retrieve the device interface detail which includes the device path
        if (SetupDiGetDeviceInterfaceDetail(deviceInfoSet, &deviceInterfaceData, 
                                          deviceInterfaceDetailData, requiredSize, NULL, NULL)) {
            
            // Convert device path to string for comparison
            QString currentDevicePath = QString::fromWCharArray(deviceInterfaceDetailData->DevicePath);
            
            // Get handle to the device
            HANDLE deviceHandle = CreateFile(deviceInterfaceDetailData->DevicePath, 
                                         GENERIC_READ | GENERIC_WRITE, 
                                         FILE_SHARE_READ | FILE_SHARE_WRITE, 
                                         NULL, OPEN_EXISTING, 0, NULL);
            
            if (deviceHandle != INVALID_HANDLE_VALUE) {
                HIDD_ATTRIBUTES attributes;
                attributes.Size = sizeof(attributes);
                
                if (HidD_GetAttributes(deviceHandle, &attributes)) {
                    // Check if this device matches our target VID and PID
                    if (attributes.VendorID == targetVid && attributes.ProductID == targetPid) {
                        // Check for MI if it was specified in the original path
                        bool miMatch = true;
                        if (!mi.isEmpty()) {
                            miMatch = currentDevicePath.contains("MI_" + mi, Qt::CaseInsensitive);
                        }
                        
                        if (miMatch) {
                            qCDebug(log_host_hid) << "Found matching device with path:" << currentDevicePath;
                            properPath = deviceInterfaceDetailData->DevicePath;
                            CloseHandle(deviceHandle);
                            free(deviceInterfaceDetailData);
                            break;  // Found a match
                        } else {
                            qCDebug(log_host_hid) << "Found device with matching VID/PID but different MI:" << currentDevicePath;
                        }
                    }
                }
                CloseHandle(deviceHandle);
            } else {
                qCDebug(log_host_hid) << "Failed to open device:" << currentDevicePath 
                                     << "Error:" << GetLastError();
            }
        }
        free(deviceInterfaceDetailData);
    }
    
    SetupDiDestroyDeviceInfoList(deviceInfoSet);
    
    if (properPath.empty()) {
        qCWarning(log_host_hid) << "Could not find proper device path for:" << instancePathStr;
    } else {
        qCDebug(log_host_hid) << "Successfully found proper device path:" 
                             << QString::fromStdWString(properPath);
    }
    
    return properPath;
}

bool VideoHid::sendFeatureReportWindows(BYTE* reportBuffer, DWORD bufferSize) {
    QMutexLocker locker(&m_deviceHandleMutex);
    
    bool openedForOperation = m_inTransaction || openHIDDeviceHandle();
    
    if (!openedForOperation) {
        qCDebug(log_host_hid) << "Failed to open device handle for sending feature report.";
        return false;
    }

    // Add debug info about the report being sent
    if (bufferSize > 0) {
        QString hexData;
        for (DWORD i = 0; i < bufferSize && i < 20; i++) { // Limit to first 20 bytes for readability
            hexData += QString("%1 ").arg(reportBuffer[i], 2, 16, QChar('0')).toUpper();
        }
        qCDebug(log_host_hid) << "Sending feature report with size:" << bufferSize 
                             << "data (hex):" << hexData;
    }
    
    // For MS2130S, make sure reportBuffer[0] is the report ID (usually 0)
    if (m_chipType == VideoChipType::MS2130S) {
        // In case the MS2130S needs a specific report ID
        // reportBuffer[0] = 0; // This should already be set
        qCDebug(log_host_hid) << "Using report ID" << (int)reportBuffer[0] << "for MS2130S device";
    }

    // Get device capabilities to better understand what's supported
    PHIDP_PREPARSED_DATA preparsedData = nullptr;
    HIDP_CAPS caps;
    memset(&caps, 0, sizeof(HIDP_CAPS)); // Properly initialize all fields to zero
    
    if (HidD_GetPreparsedData(deviceHandle, &preparsedData)) {
        if (HidP_GetCaps(preparsedData, &caps) == HIDP_STATUS_SUCCESS) {
            qCDebug(log_host_hid) << "Device capabilities - Feature Report Byte Length:" << caps.FeatureReportByteLength
                                 << "Input Report Byte Length:" << caps.InputReportByteLength
                                 << "Output Report Byte Length:" << caps.OutputReportByteLength;
            
            // MS2130S reports a very large feature report size, but we know it works with smaller sizes
            if (caps.FeatureReportByteLength > 1000 && m_chipType == VideoChipType::MS2130S) {
                qCDebug(log_host_hid) << "Detected very large feature report size for MS2130S, using standard size instead";
                // Don't try to adjust the buffer - we'll use our predefined size
            }
            // For normal cases, warn about size mismatch
            else if (bufferSize != caps.FeatureReportByteLength && caps.FeatureReportByteLength > 0) {
                qCDebug(log_host_hid) << "Warning: Feature report size mismatch. Using:" << bufferSize
                                     << "Device expects:" << caps.FeatureReportByteLength;
            }
        }
        HidD_FreePreparsedData(preparsedData);
    }

    // For MS2130S devices with large expected report sizes, use a direct method
    bool result = false;
    
    if (m_chipType == VideoChipType::MS2130S) {
        // For MS2130S, we need to use a special approach
        qCDebug(log_host_hid) << "Using MS2130S-specific feature report method";
        
        // Try method 1: Use direct IOCTL
        DWORD bytesReturned = 0;
        OVERLAPPED overlapped;
        memset(&overlapped, 0, sizeof(OVERLAPPED));
        
        // Create a standard HID report for MS2130S (9-bytes for specific commands)
        BYTE specialReport[9] = {0};
        specialReport[0] = reportBuffer[0]; // Report ID
        specialReport[1] = reportBuffer[1]; // Command
        specialReport[2] = reportBuffer[2]; // Address High
        specialReport[3] = reportBuffer[3]; // Address Low
        if (bufferSize > 4) {
            // Copy data bytes if available
            memcpy(&specialReport[4], &reportBuffer[4], std::min<DWORD>(5, bufferSize - 4));
        }
        
        // Try using the DeviceIoControl function directly
        result = DeviceIoControl(
            deviceHandle,
            IOCTL_HID_SET_FEATURE,
            specialReport,
            sizeof(specialReport),
            NULL,
            0,
            &bytesReturned,
            &overlapped
        );
        
        if (result) {
            qCDebug(log_host_hid) << "MS2130S-specific IOCTL method succeeded";
            return true;
        } else {
            DWORD error = GetLastError();
            qCWarning(log_host_hid) << "MS2130S-specific IOCTL method failed. Error:" << error;
        }
    }
    
    // Standard method as a fallback
    result = HidD_SetFeature(deviceHandle, reportBuffer, bufferSize);
    
    if (!result) {
        DWORD error = GetLastError();
        qCWarning(log_host_hid) << "Failed to send feature report. Windows error:" << error;
        
        // Try with a different approach for MS2130S
        if (m_chipType == VideoChipType::MS2130S) {
            // Some devices require a different approach - try WriteFile
            qCDebug(log_host_hid) << "Attempting alternative method (WriteFile) for MS2130S";
            
            // Try with a fixed small buffer
            BYTE smallBuffer[9] = {0};
            memcpy(smallBuffer, reportBuffer, std::min<DWORD>(9, bufferSize));
            
            DWORD bytesWritten = 0;
            OVERLAPPED ol;
            memset(&ol, 0, sizeof(OVERLAPPED));
            
            result = WriteFile(deviceHandle, smallBuffer, sizeof(smallBuffer), &bytesWritten, &ol);
            
            if (result) {
                qCDebug(log_host_hid) << "Alternative write method succeeded, wrote" << bytesWritten << "bytes";
            } else {
                error = GetLastError();
                qCWarning(log_host_hid) << "Alternative write method failed. Error:" << error;
            }
        }
        
        if (!m_inTransaction) {
            closeHIDDeviceHandle();
        }
        return result;
    }

    if (!m_inTransaction) {
        closeHIDDeviceHandle();
    }
    return true;
}

bool VideoHid::openHIDDeviceHandle() {
    QMutexLocker locker(&m_deviceHandleMutex);
    
    if (deviceHandle == INVALID_HANDLE_VALUE) {
        qCDebug(log_host_hid)  << "Opening HID device handle...";
        
        // Get the full device path
        std::wstring devicePath = getHIDDevicePath();
        if (devicePath.empty()) {
            qCWarning(log_host_hid) << "Failed to get valid HID device path";
            return false;
        }
        
        QString qDevicePath = QString::fromStdWString(devicePath);
        qCDebug(log_host_hid) << "Opening device with path:" << qDevicePath;
        
        // Format check: Windows requires paths that begin with \\?\ or \\.\ for device paths
        if (!qDevicePath.startsWith("\\\\") && !qDevicePath.startsWith("\\\\.\\") && !qDevicePath.startsWith("\\\\?\\")) {
            qCWarning(log_host_hid) << "Invalid device path format, must start with \\\\.\\, \\\\?\\ or \\\\";
            return false;
        }
        
        deviceHandle = CreateFileW(devicePath.c_str(),
                                 GENERIC_READ | GENERIC_WRITE,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE,
                                 NULL,
                                 OPEN_EXISTING,
                                 0,
                                 NULL);
        
        if (deviceHandle == INVALID_HANDLE_VALUE) {
            DWORD error = GetLastError();
            qCWarning(log_host_hid) << "Failed to open device handle. Error code:" << error;
            return false;
        }
    }
    qCDebug(log_host_hid) << "Successfully opened device handle";
    return true;
}

bool VideoHid::getFeatureReportWindows(BYTE* reportBuffer, DWORD bufferSize) {
    QMutexLocker locker(&m_deviceHandleMutex);
    
    bool openedForOperation = m_inTransaction || openHIDDeviceHandle();
    
    if (!openedForOperation) {
        qCDebug(log_host_hid) << "Failed to open device handle for getting feature report.";
        return false;
    }

    // Add debug info about the report being requested
    QString hexData;
    for (DWORD i = 0; i < 4 && i < bufferSize; i++) { // First few bytes for debugging
        hexData += QString("%1 ").arg(reportBuffer[i], 2, 16, QChar('0')).toUpper();
    }
    qCDebug(log_host_hid) << "Getting feature report with size:" << bufferSize 
                         << "report ID:" << (int)reportBuffer[0]
                         << "first bytes:" << hexData;
    
    // Get device capabilities to better understand what's supported
    PHIDP_PREPARSED_DATA preparsedData = nullptr;
    if (HidD_GetPreparsedData(deviceHandle, &preparsedData)) {
        HIDP_CAPS caps;
        if (HidP_GetCaps(preparsedData, &caps) == HIDP_STATUS_SUCCESS) {
            qCDebug(log_host_hid) << "Device capabilities - Feature Report Byte Length:" << caps.FeatureReportByteLength
                                 << "Input Report Byte Length:" << caps.InputReportByteLength
                                 << "Output Report Byte Length:" << caps.OutputReportByteLength;
        }
        HidD_FreePreparsedData(preparsedData);
    }

    // Send the Get Feature Report request
    // For MS2130S devices, use a specialized approach first
    bool result = false;
    
    if (m_chipType == VideoChipType::MS2130S) {
        qCDebug(log_host_hid) << "Using MS2130S-specific get feature report method";
        
        // Try with a fixed small buffer size that's known to work with MS2130S
        const DWORD MS2130S_BUFFER_SIZE = 64;
        BYTE* ms2130sBuffer = new BYTE[MS2130S_BUFFER_SIZE];
        memset(ms2130sBuffer, 0, MS2130S_BUFFER_SIZE);
        ms2130sBuffer[0] = reportBuffer[0]; // Copy the report ID
        
        // Try direct IOCTL for MS2130S
        DWORD bytesReturned = 0;
        OVERLAPPED overlapped;
        memset(&overlapped, 0, sizeof(OVERLAPPED));
        
        result = DeviceIoControl(
            deviceHandle,
            IOCTL_HID_GET_FEATURE,
            NULL,
            0,
            ms2130sBuffer,
            MS2130S_BUFFER_SIZE,
            &bytesReturned,
            &overlapped
        );
        
        if (result) {
            qCDebug(log_host_hid) << "MS2130S-specific IOCTL get method succeeded, got" << bytesReturned << "bytes";
            // Copy the data back to the original buffer
            memcpy(reportBuffer, ms2130sBuffer, std::min<DWORD>(bufferSize, bytesReturned));
            delete[] ms2130sBuffer;
            return true;
        } else {
            DWORD error = GetLastError();
            qCWarning(log_host_hid) << "MS2130S-specific IOCTL get method failed. Error:" << error;
        }
        
        delete[] ms2130sBuffer;
    }
    
    // Standard approach as fallback
    result = HidD_GetFeature(deviceHandle, reportBuffer, bufferSize);

    if (!result) {
        DWORD error = GetLastError();
        qCWarning(log_host_hid) << "Failed to get feature report. Windows error:" << error;
        
        // Try with a different approach for MS2130S
        if (m_chipType == VideoChipType::MS2130S) {
            // Some MS2130S devices might need a different approach
            qCDebug(log_host_hid) << "Attempting alternative method for MS2130S get feature report";
            
            // Try ReadFile approach with a fixed, smaller buffer
            const DWORD SAFE_BUFFER_SIZE = 64;
            DWORD bytesRead = 0;
            OVERLAPPED ol;
            memset(&ol, 0, sizeof(OVERLAPPED));
            
            // Prepare buffer for ReadFile with a reasonable size
            BYTE* readBuffer = new BYTE[SAFE_BUFFER_SIZE];
            memset(readBuffer, 0, SAFE_BUFFER_SIZE);
            readBuffer[0] = reportBuffer[0]; // Copy the report ID
            
            result = ReadFile(deviceHandle, readBuffer, SAFE_BUFFER_SIZE, &bytesRead, &ol);
            
            if (result) {
                qCDebug(log_host_hid) << "Alternative read method succeeded, read" << bytesRead << "bytes";
                // Copy the data back to the original buffer
                memcpy(reportBuffer, readBuffer, std::min<DWORD>(bufferSize, bytesRead));
            } else {
                error = GetLastError();
                qCWarning(log_host_hid) << "Alternative read method failed. Error:" << error;
            }
            
            delete[] readBuffer;
        }
    }

    if (!m_inTransaction) {
        closeHIDDeviceHandle();
    }

    return result;
}

#elif __linux__
QString VideoHid::getHIDDevicePath() {
    QString portChain = GlobalSetting::instance().getOpenterfacePortChain();
    QString hidPath = findMatchingHIDDevice(portChain);
    
    if (!hidPath.isEmpty()) {
        return hidPath;
    }
    
    // Fallback to original device name enumeration method
    qCDebug(log_host_hid) << "Falling back to device name enumeration for HID device discovery";
    
    QDir dir("/sys/class/hidraw");

    QStringList hidrawDevices = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);

    if (hidrawDevices.isEmpty()) {
        qCDebug(log_host_hid)  << "No Openterface device found.";
        
        return QString();
    }

    foreach (const QString &device, hidrawDevices) {\
        QString devicePath = "/sys/class/hidraw/" + device + "/device/uevent";

        QFile file(devicePath);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&file);
            
            while (!in.atEnd()) {
                QString line = in.readLine();
                qCDebug(log_host_hid)  << "Line: " << line;
                if (line.isEmpty()) {
                    break;
                }                    if (line.contains("HID_NAME")) {
                        // Check if this is the device you are looking for
                        if (line.contains("Openterface") || line.contains("MACROSILICON")) {
                            QString foundPath = "/dev/" + device;
                            
                            return foundPath;
                        }
                    }
            }
        } else {
            qCDebug(log_host_hid)  << "Failed to open device path: " << devicePath;
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
            qCDebug(log_host_hid)  << "Failed to open HID device (" << devicePath << "). Error:" << strerror(errno);
            return false;
        }
    }
    return true;
}

bool VideoHid::sendFeatureReportLinux(uint8_t* reportBuffer, int bufferSize) {
    bool openedForOperation = m_inTransaction || openHIDDevice();
    
    if (!openedForOperation) {
        return false;
    }

    std::vector<uint8_t> buffer(bufferSize);
    std::copy(reportBuffer, reportBuffer + bufferSize, buffer.begin());
    int res = ioctl(hidFd, HIDIOCSFEATURE(buffer.size()), buffer.data());

    if (res < 0) {
        qCDebug(log_host_hid)  << "Failed to send feature report. Error:" << strerror(errno);
        return false;
    }

    if (!m_inTransaction) {
        closeHIDDeviceHandle();
    }

    return true;
}

bool VideoHid::getFeatureReportLinux(uint8_t* reportBuffer, int bufferSize) {
    bool openedForOperation = m_inTransaction || openHIDDevice();
    
    if (!openedForOperation) {
        return false;
    }

    std::vector<uint8_t> buffer(bufferSize);
    int res = ioctl(hidFd, HIDIOCGFEATURE(buffer.size()), buffer.data());

    if (res < 0) {
        qCDebug(log_host_hid)  << "Failed to get feature report. Error:" << strerror(errno);
        return false;
    }

    std::copy(buffer.begin(), buffer.end(), reportBuffer);

    if (!m_inTransaction) {
        closeHIDDeviceHandle();
    }
    return true;
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
        qCDebug(log_host_hid)  << "Report:" << report.toHex(' ').toUpper();
        
        status = sendFeatureReport((uint8_t*)report.data(), report.size());
        
        if (!status) {
            qWarning() << "Failed to write chunk to address:" << QString("0x%1").arg(_address, 4, 16, QChar('0'));
            return false;
        }
        written_size += chunk_length;
        emit firmwareWriteChunkComplete(written_size);
        _address += chunkSize;
    }
    return true;
}

bool VideoHid::writeEeprom(quint16 address, const QByteArray &data) {
    const int MAX_CHUNK = 16;
    QByteArray remainingData = data;
    written_size = 0;
    
    // Begin transaction for the entire operation
    if (!beginTransaction()) {
        qCDebug(log_host_hid)  << "Failed to begin transaction for EEPROM write";
        return false;
    }
    
    bool success = true;
    while (!remainingData.isEmpty() && success) {
        QByteArray chunk = remainingData.left(MAX_CHUNK);
        success = writeChunk(address, chunk);
        
        if (success) {
            address += chunk.size();
            remainingData = remainingData.mid(MAX_CHUNK);
            if (written_size % 64 == 0) {
                qCDebug(log_host_hid)  << "Written size:" << written_size;
            }
            QThread::msleep(150); // Add 100ms delay between writes
        } else {
            qCDebug(log_host_hid)  << "Failed to write chunk to EEPROM";
            break;
        }
    }
    
    // End transaction
    endTransaction();
    
    return success;
}

void VideoHid::loadFirmwareToEeprom() {
    // Create firmware data
    if (networkFirmware.empty()){
        qCDebug(log_host_hid) << "No firmware data available to write";
        emit firmwareWriteComplete(false);
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
        qCDebug(log_host_hid) << "Firmware write progress: " << percent << "%";
        emit firmwareWriteProgress(percent);
    });
    
    connect(worker, &FirmwareWriter::finished, this, [this](bool success) {
        if (success) {
            qCDebug(log_host_hid) << "Firmware write completed successfully";
            emit firmwareWriteComplete(true);
        } else {
            qCDebug(log_host_hid) << "Firmware write failed - user should try again";
            emit firmwareWriteComplete(false);
            emit firmwareWriteError("Firmware update failed. Please try again.");
        }
    });
    
    // Start the thread
    thread->start();
}

FirmwareResult VideoHid::isLatestFirmware() {
    // Check if TLS/SSL is available (important for statically built Qt applications)
    if (!QSslSocket::supportsSsl()) {
        qCWarning(log_host_hid) << "TLS/SSL not available - skipping firmware check";
        fireware_result = FirmwareResult::CheckFailed;
        return FirmwareResult::CheckFailed;
    }

    qCDebug(log_host_hid) << "Checking for latest firmware...";
    QString firemwareFileName = getLatestFirmwareFilenName(firmwareURL);
    qCDebug(log_host_hid) << "Latest firmware file name:" << firemwareFileName;
    if (fireware_result == FirmwareResult::Timeout) {
        return FirmwareResult::Timeout;
    }
    qCDebug(log_host_hid) << "After timeout checking: " << firmwareURL;
    QString newURL = firmwareURL.replace("minikvm_latest_firmware.txt", firemwareFileName);
    fetchBinFileToString(newURL, 5000); // Add 5-second timeout
    m_currentfirmwareVersion = getFirmwareVersion();
    qCDebug(log_host_hid)  << "Firmware version:" << QString::fromStdString(m_currentfirmwareVersion);
    qCDebug(log_host_hid)  << "Latest firmware version:" << QString::fromStdString(m_firmwareVersion);
    if (getFirmwareVersion() == m_firmwareVersion) {
        fireware_result = FirmwareResult::Latest;
        return FirmwareResult::Latest;
    }
    if (safe_stoi(getFirmwareVersion()) <= safe_stoi(m_firmwareVersion)) {
        fireware_result = FirmwareResult::Upgradable;
        return FirmwareResult::Upgradable;
    }
    return fireware_result;
}

int safe_stoi(std::string str, int defaultValue) {
    try {
        return std::stoi(str);
    } catch (const std::invalid_argument&) {
        qCDebug(log_host_hid) << "Invalid argument for stoi, returning default value:" << defaultValue;
        return defaultValue;
    } catch (const std::out_of_range&) {
        qCDebug(log_host_hid) << "Out of range for stoi, returning default value:" << defaultValue;
        qCDebug(log_host_hid) << "String was:" << QString::fromStdString(str);
        return defaultValue;
    }
}