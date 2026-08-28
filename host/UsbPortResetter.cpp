/*
* ========================================================================== *
*                                                                            *
*    This file is part of the Openterface Mini KVM App QT version            *
*                                                                            *
*    Copyright (C) 2024   <info@openterface.com>                             *
*                                                                            *
*    This program is free software: you can redistribute it and/or modify    *
*    it under the terms of the GNU General Public License as published by    *
*    the Free Software Foundation version 3.                                 *
*                                                                            *
*    This program is distributed in the hope that it will be useful, but     *
*    WITHOUT ANY WARRANTY; without even the implied warranty of              *
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU        *
*    General Public License for more details.                                *
*                                                                            *
*    You should have received a copy of the GNU General Public License       *
*    along with this program. If not, see <http://www.gnu.org/licenses/>.    *
*                                                                            *
* ========================================================================== *
*/

#include "UsbPortResetter.h"
#include <QDebug>
#include "log/opflogging.h"

Q_LOGGING_CATEGORY(log_usb_reset, "opf.usb.reset")

#ifdef __linux__
#include <libusb-1.0/libusb.h>

// USB Hub class request codes (USB 2.0 spec Table 11-16)
static constexpr uint8_t  USB_HUB_SET_FEATURE    = 0x03;
static constexpr uint16_t USB_PORT_FEAT_RESET    = 4;

// Known device VID:PID
static constexpr uint16_t COMPOSITE_VID = 0x345F;  // MACROSILICON MS2130S
static constexpr uint16_t COMPOSITE_PID = 0x2132;  // Openterface composite (HID+Camera+Audio)
static constexpr uint16_t SERIAL_VID    = 0x1A86;  // WCH CH32V208
static constexpr uint16_t SERIAL_PID    = 0xFE0C;  // CH32V208 serial

// Maximum number of ports to try resetting on a hub
static constexpr int MAX_HUB_PORTS = 8;

UsbPortResetter::UsbPortResetter(QObject *parent)
    : QObject(parent)
{
}

UsbPortResetter::~UsbPortResetter()
{
}

bool UsbPortResetter::resetHubPortsForSerialRecovery()
{
    emit resetStarted();

    // 1. Initialize libusb context
    libusb_context *ctx = nullptr;
    int ret = libusb_init(&ctx);
    if (ret < 0) {
        qCWarning(log_usb_reset) << "Failed to initialize libusb:" << libusb_error_name(ret);
        emit resetCompleted(false);
        return false;
    }

    // 2. Check if serial device is already present (no need to reset)
    bool serialPresent = false;
    bool compositePresent = false;
    libusb_device **devList = nullptr;
    ssize_t devCount = libusb_get_device_list(ctx, &devList);

    if (devCount < 0) {
        qCWarning(log_usb_reset) << "Failed to get device list:" << libusb_error_name(static_cast<int>(devCount));
        libusb_exit(ctx);
        emit resetCompleted(false);
        return false;
    }

    // Check if serial device is already present
    for (ssize_t i = 0; i < devCount; i++) {
        libusb_device *dev = devList[i];
        libusb_device_descriptor desc;
        if (libusb_get_device_descriptor(dev, &desc) < 0) continue;
        if (desc.idVendor == SERIAL_VID && desc.idProduct == SERIAL_PID) {
            serialPresent = true;
            break;
        }
        if (desc.idVendor == COMPOSITE_VID && desc.idProduct == COMPOSITE_PID) {
            compositePresent = true;
        }
    }

    if (serialPresent) {
        qCInfo(log_usb_reset) << "Serial device (CH32V208) is already present - no reset needed";
        libusb_free_device_list(devList, 1);
        libusb_exit(ctx);
        emit resetCompleted(true);
        return true;
    }

    if (!compositePresent) {
        qCWarning(log_usb_reset) << "Composite device (345F:2132) not found - cannot determine parent hub";
        libusb_free_device_list(devList, 1);
        libusb_exit(ctx);
        emit resetCompleted(false);
        return false;
    }

    qCInfo(log_usb_reset) << "Serial device missing, composite device present - attempting hub port reset";

    // 3. Find the composite device and its parent hub
    libusb_device *compositeDev = nullptr;
    for (ssize_t i = 0; i < devCount; i++) {
        libusb_device *dev = devList[i];
        libusb_device_descriptor desc;
        if (libusb_get_device_descriptor(dev, &desc) < 0) continue;
        if (desc.idVendor == COMPOSITE_VID && desc.idProduct == COMPOSITE_PID) {
            compositeDev = dev;
            break;
        }
    }

    if (!compositeDev) {
        qCWarning(log_usb_reset) << "Composite device not found in device list";
        libusb_free_device_list(devList, 1);
        libusb_exit(ctx);
        emit resetCompleted(false);
        return false;
    }

    uint8_t busNum = libusb_get_bus_number(compositeDev);
    uint8_t portNum = libusb_get_port_number(compositeDev);
    qCInfo(log_usb_reset) << "Composite device on bus" << busNum << "port" << portNum;

    // 4. Get the parent hub device
    libusb_device *parentHub = libusb_get_parent(compositeDev);
    if (!parentHub) {
        qCWarning(log_usb_reset) << "Cannot get parent hub of composite device";
        libusb_free_device_list(devList, 1);
        libusb_exit(ctx);
        emit resetCompleted(false);
        return false;
    }

    uint8_t hubBus = libusb_get_bus_number(parentHub);
    uint8_t hubPort = libusb_get_port_number(parentHub);
    qCInfo(log_usb_reset) << "Parent hub on bus" << hubBus << "port" << hubPort;

    // 5. Open the parent hub
    libusb_device_handle *hubHandle = nullptr;
    ret = libusb_open(parentHub, &hubHandle);
    if (ret < 0) {
        qCWarning(log_usb_reset) << "Cannot open parent hub:" << libusb_error_name(ret)
                                  << "- permission denied? Try running with appropriate USB permissions.";
        libusb_free_device_list(devList, 1);
        libusb_exit(ctx);
        emit resetCompleted(false);
        return false;
    }

    qCInfo(log_usb_reset) << "Successfully opened parent hub - resetting ports...";

    // 6. Send SET_FEATURE(PORT_RESET) to each port of the hub
    // This forces all downstream devices to re-enumerate
    bool anySuccess = false;
    for (int port = 1; port <= MAX_HUB_PORTS; port++) {
        uint8_t bmRequestType = static_cast<uint8_t>(LIBUSB_REQUEST_TYPE_CLASS)
                              | static_cast<uint8_t>(LIBUSB_RECIPIENT_OTHER);  // 0x23
        ret = libusb_control_transfer(
            hubHandle,
            bmRequestType,                                              // bmRequestType: 0x23
            USB_HUB_SET_FEATURE,                                  // bRequest: SET_FEATURE
            USB_PORT_FEAT_RESET,                                  // wValue: PORT_RESET (4)
            static_cast<uint16_t>(port),                          // wIndex: port number (1-based)
            nullptr, 0,                                           // no data
            1000                                                  // timeout: 1 second
        );

        if (ret >= 0) {
            qCInfo(log_usb_reset) << "Successfully reset port" << port << "of parent hub";
            anySuccess = true;
        } else {
            // Port may not exist or device not connected - this is normal
            qCDebug(log_usb_reset) << "Port" << port << "reset failed:" << libusb_error_name(ret)
                                    << "(port may not exist)";
        }
    }

    // 7. Clean up
    libusb_close(hubHandle);
    libusb_free_device_list(devList, 1);
    libusb_exit(ctx);

    if (anySuccess) {
        qCInfo(log_usb_reset) << "USB hub port reset completed successfully - waiting for serial device to re-enumerate";
    } else {
        qCWarning(log_usb_reset) << "Failed to reset any hub ports";
    }

    emit resetCompleted(anySuccess);
    return anySuccess;
}

#else  // Not Linux

UsbPortResetter::UsbPortResetter(QObject *parent)
    : QObject(parent)
{
}

UsbPortResetter::~UsbPortResetter()
{
}

bool UsbPortResetter::resetHubPortsForSerialRecovery()
{
    qCInfo(log_usb_reset) << "USB port reset is only supported on Linux";
    emit resetCompleted(false);
    return false;
}

#endif  // __linux__
