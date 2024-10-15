#ifndef MS2109_H
#define MS2109_H

#include <cstdint>

const uint16_t ADDR_HDMI_CONNECTION_STATUS = 0xFA8C;
// 0xDF00 bit0: GPIO0 reads the hard switch status, 1 means switchable usb connects to the target, 0 means switchable usb connects to the host
const uint16_t ADDR_GPIO0 = 0xDF00;
// 0xDF01 bit5: SPDIFOUT reads the soft switch status, 1 means switchable usb connects to the target, 0 means switchable usb connects to the host
const uint16_t ADDR_SPDIFOUT = 0xDF01;

const uint16_t ADDR_FIRMWARE_VERSION_0 = 0xCBDC;
const uint16_t ADDR_FIRMWARE_VERSION_1 = 0xCBDD;
const uint16_t ADDR_FIRMWARE_VERSION_2 = 0xCBDE;
const uint16_t ADDR_FIRMWARE_VERSION_3 = 0xCBDF;

const uint16_t ADDR_WIDTH_H = 0xC738;
const uint16_t ADDR_WIDTH_L = 0xC739;

const uint16_t ADDR_HEIGHT_H = 0xC73A;
const uint16_t ADDR_HEIGHT_L = 0xC73B;

const uint16_t ADDR_FPS_H = 0xC73E;
const uint16_t ADDR_FPS_L = 0xC73F;

#endif // MS2109_H