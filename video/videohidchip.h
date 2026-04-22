#ifndef VIDEOHIDCHIP_H
#define VIDEOHIDCHIP_H

#include <QtGlobal>
#include <QString>
#include <QByteArray>
#include <QPair>
#include <atomic>
#include <functional>

#include "transport/IHIDTransport.h"
#include "ms2109.h"
#include "ms2109s.h"
#include "ms2130s.h"

// Chipset enumeration is declared here so chip classes and VideoHid can share it
// without a circular include (videohid.h still re-exports it via its own header).
enum class VideoChipType {
    MS2109,
    MS2109S,
    MS2130S,
    UNKNOWN
};

// Register set for width/height/fps/pixel-clock addresses
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

// ──────────────────────────────────────────────────
//  Abstract base — depends only on IHIDTransport
// ──────────────────────────────────────────────────
class VideoChip {
public:
    explicit VideoChip(IHIDTransport* transport) : m_transport(transport) {}
    virtual ~VideoChip() = default;

    virtual VideoChipType type() const = 0;
    virtual QString       name() const = 0;

    // Address helpers
    virtual quint16 addrSpdifout() const = 0;
    virtual quint16 addrGpio0()    const = 0;

    // Firmware version register addresses
    virtual quint16 addrFirmwareVersion0() const { return ADDR_FIRMWARE_VERSION_0; }
    virtual quint16 addrFirmwareVersion1() const { return ADDR_FIRMWARE_VERSION_1; }
    virtual quint16 addrFirmwareVersion2() const { return ADDR_FIRMWARE_VERSION_2; }
    virtual quint16 addrFirmwareVersion3() const { return ADDR_FIRMWARE_VERSION_3; }

    // Register set for resolution/FPS/pixel-clock reads
    virtual VideoHidRegisterSet getRegisterSet() const = 0;

    // Protocol-level read/write — fully self-contained in each subclass
    virtual QPair<QByteArray, bool> read4Byte(quint16 address) = 0;
    virtual bool                    write4Byte(quint16 address, const QByteArray& data) = 0;

protected:
    IHIDTransport* m_transport{nullptr};
};

// ──────────────────────────────────────────────────
//  MS2109
// ──────────────────────────────────────────────────
class Ms2109Chip : public VideoChip {
public:
    explicit Ms2109Chip(IHIDTransport* transport) : VideoChip(transport) {}

    VideoChipType       type() const override { return VideoChipType::MS2109; }
    QString             name() const override { return QStringLiteral("MS2109"); }
    quint16 addrSpdifout() const override { return ADDR_SPDIFOUT; }
    quint16 addrGpio0()    const override { return ADDR_GPIO0; }

    VideoHidRegisterSet getRegisterSet() const override {
        VideoHidRegisterSet s;
        s.width_h  = ADDR_INPUT_WIDTH_H;    s.width_l  = ADDR_INPUT_WIDTH_L;
        s.height_h = ADDR_INPUT_HEIGHT_H;   s.height_l = ADDR_INPUT_HEIGHT_L;
        s.fps_h    = ADDR_INPUT_FPS_H;       s.fps_l    = ADDR_INPUT_FPS_L;
        s.clk_h    = ADDR_INPUT_PIXELCLK_H; s.clk_l    = ADDR_INPUT_PIXELCLK_L;
        return s;
    }

    QPair<QByteArray, bool> read4Byte(quint16 address) override;
    bool                    write4Byte(quint16 address, const QByteArray& data) override;
};

// ──────────────────────────────────────────────────
//  MS2109S
// ──────────────────────────────────────────────────
class Ms2109sChip : public VideoChip {
public:
    explicit Ms2109sChip(IHIDTransport* transport) : VideoChip(transport) {}

    VideoChipType       type() const override { return VideoChipType::MS2109S; }
    QString             name() const override { return QStringLiteral("MS2109S"); }
    quint16 addrSpdifout() const override { return MS2109S_ADDR_SPDIFOUT; }
    quint16 addrGpio0()    const override { return MS2109S_ADDR_GPIO0; }

    VideoHidRegisterSet getRegisterSet() const override {
        VideoHidRegisterSet s;
        s.width_h  = MS2109S_ADDR_INPUT_WIDTH_H;    s.width_l  = MS2109S_ADDR_INPUT_WIDTH_L;
        s.height_h = MS2109S_ADDR_INPUT_HEIGHT_H;   s.height_l = MS2109S_ADDR_INPUT_HEIGHT_L;
        s.fps_h    = MS2109S_ADDR_INPUT_FPS_H;       s.fps_l    = MS2109S_ADDR_INPUT_FPS_L;
        s.clk_h    = MS2109S_ADDR_INPUT_PIXELCLK_H; s.clk_l    = MS2109S_ADDR_INPUT_PIXELCLK_L;
        return s;
    }

    QPair<QByteArray, bool> read4Byte(quint16 address) override;
    bool                    write4Byte(quint16 address, const QByteArray& data) override;
};

// ──────────────────────────────────────────────────
//  MS2130S  (+ SPI flash driver)
// ──────────────────────────────────────────────────
class Ms2130sChip : public VideoChip {
public:
    explicit Ms2130sChip(IHIDTransport* transport)
        : VideoChip(transport) {}

    VideoChipType       type() const override { return VideoChipType::MS2130S; }
    QString             name() const override { return QStringLiteral("MS2130S"); }
    quint16 addrSpdifout() const override { return MS2130S_ADDR_SPDIFOUT; }
    quint16 addrGpio0()    const override { return MS2130S_ADDR_GPIO0; }

    quint16 addrFirmwareVersion0() const override { return MS2130S_ADDR_FIRMWARE_VERSION_0; }
    quint16 addrFirmwareVersion1() const override { return MS2130S_ADDR_FIRMWARE_VERSION_1; }
    quint16 addrFirmwareVersion2() const override { return MS2130S_ADDR_FIRMWARE_VERSION_2; }
    quint16 addrFirmwareVersion3() const override { return MS2130S_ADDR_FIRMWARE_VERSION_3; }

    VideoHidRegisterSet getRegisterSet() const override {
        VideoHidRegisterSet s;
        s.width_h  = MS2130S_ADDR_INPUT_WIDTH_H;    s.width_l  = MS2130S_ADDR_INPUT_WIDTH_L;
        s.height_h = MS2130S_ADDR_INPUT_HEIGHT_H;   s.height_l = MS2130S_ADDR_INPUT_HEIGHT_L;
        s.fps_h    = MS2130S_ADDR_INPUT_FPS_H;       s.fps_l    = MS2130S_ADDR_INPUT_FPS_L;
        s.clk_h    = MS2130S_ADDR_INPUT_PIXELCLK_H; s.clk_l    = MS2130S_ADDR_INPUT_PIXELCLK_L;
        return s;
    }

    QPair<QByteArray, bool> read4Byte(quint16 address) override;
    bool                    write4Byte(quint16 address, const QByteArray& data) override;

    // SPI flash operations (moved from VideoHid)
    bool eraseSector(quint32 startAddress);
    bool flashEraseDone(bool& done);
    bool flashGetSize(quint32& flashSize);
    bool flashBurstWrite(quint32 address, const QByteArray& data);
    bool flashBurstRead(quint32 address, quint32 length, QByteArray& outData);
    bool writeFirmware(quint16 address, const QByteArray& data);
    bool initializeGPIO();
    void restoreGPIO();
    int  detectConnectMode();

    // Callback invoked after each chunk write with the total written byte count.
    // Set by VideoHid so it can emit firmwareWriteChunkComplete without the chip
    // holding a back-pointer to VideoHid.
    std::function<void(quint32)> onChunkWritten;

    // Flash guard flag — set true during flash to suppress background register reads
    std::atomic_bool flashInProgress{false};

    // Connect mode and saved GPIO state (owned here, not in VideoHid)
    int   connectMode{0};
    bool  gpioSaved{false};
    quint8 gpio_saved_b0{0};
    quint8 gpio_saved_a0{0};
    quint8 gpio_saved_c7{0};
    quint8 gpio_saved_c8{0};
    quint8 gpio_saved_ca{0};
    quint8 gpio_saved_f01f{0};
};

#endif // VIDEOHIDCHIP_H
