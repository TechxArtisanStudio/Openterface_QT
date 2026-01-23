#ifndef MS2109S_H
#define MS2109S_H

#include <QtGlobal>
#include <cstdint>

// Input resolution registers
const uint16_t MS2109S_ADDR_INPUT_WIDTH_H = 0xC703;
const uint16_t MS2109S_ADDR_INPUT_WIDTH_L = 0xC704;
const uint16_t MS2109S_ADDR_INPUT_HEIGHT_H = 0xC705;
const uint16_t MS2109S_ADDR_INPUT_HEIGHT_L = 0xC706;

// Frame rate registers
const uint16_t MS2109S_ADDR_INPUT_FPS_H = 0xC617;
const uint16_t MS2109S_ADDR_INPUT_FPS_L = 0xC618;

// Pixel clock registers
const uint16_t MS2109S_ADDR_INPUT_PIXELCLK_H = 0xC8C5;
const uint16_t MS2109S_ADDR_INPUT_PIXELCLK_L = 0xC8C6;

// Timing registers
const uint16_t MS2109S_ADDR_INPUT_HTOTAL_H = 0xC8BD;
const uint16_t MS2109S_ADDR_INPUT_HTOTAL_L = 0xC8BE;
const uint16_t MS2109S_ADDR_INPUT_VTOTAL_H = 0xC8BF;
const uint16_t MS2109S_ADDR_INPUT_VTOTAL_L = 0xC8C0;
const uint16_t MS2109S_ADDR_INPUT_HST_H = 0xC8C9;
const uint16_t MS2109S_ADDR_INPUT_HST_L = 0xC8CA;
const uint16_t MS2109S_ADDR_INPUT_VST_H = 0xC8CB;
const uint16_t MS2109S_ADDR_INPUT_VST_L = 0xC8CC;
const uint16_t MS2109S_ADDR_INPUT_HW_H = 0xC8CD;
const uint16_t MS2109S_ADDR_INPUT_HW_L = 0xC8CE;
const uint16_t MS2109S_ADDR_INPUT_VW_H = 0xC8CF;
const uint16_t MS2109S_ADDR_INPUT_VW_L = 0xC8D0;

// Version registers (same region as other chips)
const uint16_t MS2109S_ADDR_FIRMWARE_VERSION_0 = 0xCBDC;
const uint16_t MS2109S_ADDR_FIRMWARE_VERSION_1 = 0xCBDD;
const uint16_t MS2109S_ADDR_FIRMWARE_VERSION_2 = 0xCBDE;
const uint16_t MS2109S_ADDR_FIRMWARE_VERSION_3 = 0xCBDF;

// HDMI connection/status
const uint16_t MS2109S_ADDR_HDMI_CONNECTION_STATUS = 0xFD9C;

// This chip doesn't expose the hardware/software switch registers in a useful way
const uint16_t MS2109S_ADDR_GPIO0 = 0x0000; // not applicable
const uint16_t MS2109S_ADDR_SPDIFOUT = 0x0000; // not applicable

// Commands (assumed same as other chips)
const quint8 MS2109S_CMD_XDATA_WRITE = 0xB6;
const quint8 MS2109S_CMD_XDATA_READ = 0xB5;

#endif // MS2109S_H
