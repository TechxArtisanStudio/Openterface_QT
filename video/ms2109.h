#ifndef MS2109_H
#define MS2109_H

#include <QtGlobal>
#include <cstdint>

const uint16_t ADDR_HDMI_CONNECTION_STATUS = 0xFA8C;
// 0xDF00 bit0: GPIO0 reads the hard switch status, 1 means switchable usb connects to the target, 0 means switchable usb connects to the host
const uint16_t ADDR_GPIO0 = 0xDF00;
// 0xDF01 bit5: SPDIFOUT reads the soft switch status, 1 means switchable usb connects to the target, 0 means switchable usb connects to the host
const uint16_t ADDR_SPDIFOUT = 0xDF01;

const uint16_t ADDR_EEPROM = 0x0000;

const uint16_t ADDR_FIRMWARE_VERSION_0 = 0xCBDC;
const uint16_t ADDR_FIRMWARE_VERSION_1 = 0xCBDD;
const uint16_t ADDR_FIRMWARE_VERSION_2 = 0xCBDE;
const uint16_t ADDR_FIRMWARE_VERSION_3 = 0xCBDF;

// The patch variable address for input width and height is not correct, so we need to use the correct address
// const uint16_t ADDR_INPUT_WIDTH_H = 0xC738;      //old address
// const uint16_t ADDR_INPUT_WIDTH_L = 0xC739;      //old address

const uint16_t ADDR_INPUT_WIDTH_H = 0xC6AF;
const uint16_t ADDR_INPUT_WIDTH_L = 0xC6B0;

// const uint16_t ADDR_INPUT_HEIGHT_H = 0xC73A;     //old address
// const uint16_t ADDR_INPUT_HEIGHT_L = 0xC73B;     //old address
const uint16_t ADDR_INPUT_HEIGHT_H = 0xC6B1;
const uint16_t ADDR_INPUT_HEIGHT_L = 0xC6B2;

// const uint16_t ADDR_FPS_H = 0xC73E;
// const uint16_t ADDR_FPS_L = 0xC73F;

const uint16_t ADDR_INPUT_FPS_H = 0xC6B5;
const uint16_t ADDR_INPUT_FPS_L = 0xC6B6;

const uint16_t ADDR_INPUT_PIXELCLK_H = 0xC73C;
const uint16_t ADDR_INPUT_PIXELCLK_L = 0xC73D;

// const uint16_t ADDR_INPUT_PIXELCLK_H = 0xC6B3;
// const uint16_t ADDR_INPUT_PIXELCLK_L = 0xC6B4;

const quint8 CMD_XDATA_WRITE = 0xB6;
const quint8 CMD_XDATA_READ = 0xB5;

const quint8 CMD_EEPROM_WRITE = 0xE6;
const quint8 CMD_EEPROM_READ = 0xE5;

#endif // MS2109_H
