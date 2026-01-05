#ifndef MS2130S_H
#define MS2130S_H

#include <QtGlobal>
#include <cstdint>

// MS2130S Register Addresses
const uint16_t MS2130S_ADDR_INPUT_WIDTH_H = 0x1CFC;
const uint16_t MS2130S_ADDR_INPUT_WIDTH_L = 0x1CFD;
const uint16_t MS2130S_ADDR_INPUT_HEIGHT_H = 0x1CFE;
const uint16_t MS2130S_ADDR_INPUT_HEIGHT_L = 0x1CFF;

const uint16_t MS2130S_ADDR_INPUT_FPS_H = 0x1D02;
const uint16_t MS2130S_ADDR_INPUT_FPS_L = 0x1D03;

const uint16_t MS2130S_ADDR_INPUT_PIXELCLK_H = 0x1D00;
const uint16_t MS2130S_ADDR_INPUT_PIXELCLK_L = 0x1D01;

// Timing Registers
const uint16_t MS2130S_ADDR_INPUT_HTOTAL_H = 0x1CF8;
const uint16_t MS2130S_ADDR_INPUT_HTOTAL_L = 0x1CF9;

const uint16_t MS2130S_ADDR_INPUT_VTOTAL_H = 0x1CFA;
const uint16_t MS2130S_ADDR_INPUT_VTOTAL_L = 0x1CFB;

const uint16_t MS2130S_ADDR_INPUT_HACTIVE_H = 0x1CFC;
const uint16_t MS2130S_ADDR_INPUT_HACTIVE_L = 0x1CFD;

const uint16_t MS2130S_ADDR_INPUT_VACTIVE_H = 0x1CFE;
const uint16_t MS2130S_ADDR_INPUT_VACTIVE_L = 0x1CFF;

// Version Registers
const uint16_t MS2130S_ADDR_FIRMWARE_VERSION_0 = 0x1FDC;
const uint16_t MS2130S_ADDR_FIRMWARE_VERSION_1 = 0x1FDD;
const uint16_t MS2130S_ADDR_FIRMWARE_VERSION_2 = 0x1FDE;
const uint16_t MS2130S_ADDR_FIRMWARE_VERSION_3 = 0x1FDF;

// Status Registers
const uint16_t MS2130S_ADDR_HDMI_CONNECTION_STATUS = 0xFA8D;  // Different from MS2109

// Command codes - assuming same as MS2109, update if different
const quint8 MS2130S_CMD_XDATA_WRITE = 0xB6;
const quint8 MS2130S_CMD_XDATA_READ = 0xB5;

// MS2130S doesn't support EEPROM according to the info provided
// const quint8 MS2130S_CMD_EEPROM_WRITE = 0xE6;
// const quint8 MS2130S_CMD_EEPROM_READ = 0xE5;

// GPIO/SPDIFOUT - assuming same as MS2109, update if different
const uint16_t MS2130S_ADDR_GPIO0 = 0xDF00;
const uint16_t MS2130S_ADDR_SPDIFOUT = 0xDF01;

#endif // MS2130S_H