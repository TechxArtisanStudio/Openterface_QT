#ifndef MS2109_H
#define MS2109_H

#include <cstdint>

const uint16_t ADDR_HDMI_CONNECTION_STATUS = 0xFA8C;
// 0xDF00 bit0: GPIO0 reads the hard switch status, 1 means switchable usb connects to the target, 0 means switchable usb connects to the host
const uint16_t ADDR_GPIO0 = 0xDF00;
// 0xDF01 bit5: SPDIFOUT reads the soft switch status, 1 means switchable usb connects to the target, 0 means switchable usb connects to the host
const uint16_t ADDR_SPDIFOUT = 0xDF01;


#endif // MS2109_H