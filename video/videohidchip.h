#ifndef VIDEOHIDCHIP_H
#define VIDEOHIDCHIP_H

#include "videohid.h"
#include "ms2109.h"
#include "ms2109s.h"
#include "ms2130s.h"

class VideoHid;

// Abstract interface for chip-specific behavior
class VideoChip {
public:
    explicit VideoChip(VideoHid* owner) : m_owner(owner) {}
    virtual ~VideoChip() = default;

    virtual VideoChipType type() const = 0;
    virtual QString name() const = 0;

    // Address helpers
    virtual quint16 addrSpdifout() const = 0;
    virtual quint16 addrGpio0() const = 0;

    // Firmware version addresses
    virtual quint16 addrFirmwareVersion0() const { return ADDR_FIRMWARE_VERSION_0; }
    virtual quint16 addrFirmwareVersion1() const { return ADDR_FIRMWARE_VERSION_1; }
    virtual quint16 addrFirmwareVersion2() const { return ADDR_FIRMWARE_VERSION_2; }
    virtual quint16 addrFirmwareVersion3() const { return ADDR_FIRMWARE_VERSION_3; }

    // Register set for width/height/fps/clk
    virtual VideoHidRegisterSet getRegisterSet() const = 0;

    // Raw read/write operations use VideoHid implementations internally
    virtual QPair<QByteArray, bool> read4Byte(quint16 address) = 0;
    virtual bool write4Byte(quint16 address, const QByteArray &data) = 0;

protected:
    VideoHid* m_owner{nullptr};
};

// MS2109 implementation
class Ms2109Chip : public VideoChip {
public:
    explicit Ms2109Chip(VideoHid* owner) : VideoChip(owner) {}
    VideoChipType type() const override { return VideoChipType::MS2109; }
    QString name() const override { return "MS2109"; }
    quint16 addrSpdifout() const override { return ADDR_SPDIFOUT; }
    quint16 addrGpio0() const override { return ADDR_GPIO0; }
    VideoHidRegisterSet getRegisterSet() const override {
        VideoHidRegisterSet s;
        s.width_h = ADDR_INPUT_WIDTH_H; s.width_l = ADDR_INPUT_WIDTH_L;
        s.height_h = ADDR_INPUT_HEIGHT_H; s.height_l = ADDR_INPUT_HEIGHT_L;
        s.fps_h = ADDR_INPUT_FPS_H; s.fps_l = ADDR_INPUT_FPS_L;
        s.clk_h = ADDR_INPUT_PIXELCLK_H; s.clk_l = ADDR_INPUT_PIXELCLK_L;
        return s;
    }
    QPair<QByteArray, bool> read4Byte(quint16 address) override;
    bool write4Byte(quint16 address, const QByteArray &data) override;
};

// MS2109S implementation
class Ms2109sChip : public VideoChip {
public:
    explicit Ms2109sChip(VideoHid* owner) : VideoChip(owner) {}
    VideoChipType type() const override { return VideoChipType::MS2109S; }
    QString name() const override { return "MS2109S"; }
    quint16 addrSpdifout() const override { return MS2109S_ADDR_SPDIFOUT; }
    quint16 addrGpio0() const override { return MS2109S_ADDR_GPIO0; }
    VideoHidRegisterSet getRegisterSet() const override {
        VideoHidRegisterSet s;
        s.width_h = MS2109S_ADDR_INPUT_WIDTH_H; s.width_l = MS2109S_ADDR_INPUT_WIDTH_L;
        s.height_h = MS2109S_ADDR_INPUT_HEIGHT_H; s.height_l = MS2109S_ADDR_INPUT_HEIGHT_L;
        s.fps_h = MS2109S_ADDR_INPUT_FPS_H; s.fps_l = MS2109S_ADDR_INPUT_FPS_L;
        s.clk_h = MS2109S_ADDR_INPUT_PIXELCLK_H; s.clk_l = MS2109S_ADDR_INPUT_PIXELCLK_L;
        return s;
    }
    QPair<QByteArray, bool> read4Byte(quint16 address) override;
    bool write4Byte(quint16 address, const QByteArray &data) override;
};

// MS2130S implementation
class Ms2130sChip : public VideoChip {
public:
    explicit Ms2130sChip(VideoHid* owner) : VideoChip(owner) {}
    VideoChipType type() const override { return VideoChipType::MS2130S; }
    QString name() const override { return "MS2130S"; }
    quint16 addrSpdifout() const override { return MS2130S_ADDR_SPDIFOUT; }
    quint16 addrGpio0() const override { return MS2130S_ADDR_GPIO0; }
    VideoHidRegisterSet getRegisterSet() const override {
        VideoHidRegisterSet s;
        s.width_h = MS2130S_ADDR_INPUT_WIDTH_H; s.width_l = MS2130S_ADDR_INPUT_WIDTH_L;
        s.height_h = MS2130S_ADDR_INPUT_HEIGHT_H; s.height_l = MS2130S_ADDR_INPUT_HEIGHT_L;
        s.fps_h = MS2130S_ADDR_INPUT_FPS_H; s.fps_l = MS2130S_ADDR_INPUT_FPS_L;
        s.clk_h = MS2130S_ADDR_INPUT_PIXELCLK_H; s.clk_l = MS2130S_ADDR_INPUT_PIXELCLK_L;
        return s;
    }
    quint16 addrFirmwareVersion0() const override { return MS2130S_ADDR_FIRMWARE_VERSION_0; }
    quint16 addrFirmwareVersion1() const override { return MS2130S_ADDR_FIRMWARE_VERSION_1; }
    quint16 addrFirmwareVersion2() const override { return MS2130S_ADDR_FIRMWARE_VERSION_2; }
    quint16 addrFirmwareVersion3() const override { return MS2130S_ADDR_FIRMWARE_VERSION_3; }

    QPair<QByteArray, bool> read4Byte(quint16 address) override;
    bool write4Byte(quint16 address, const QByteArray &data) override;
};

#endif // VIDEOHIDCHIP_H
