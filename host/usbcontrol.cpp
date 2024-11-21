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

// Add Windows system headers
#ifdef _WIN32
#include <windows.h>
#include <basetsd.h>
#endif

#include "usbcontrol.h"
#include <QDebug>

// Add this line to define the logging category
Q_LOGGING_CATEGORY(log_usb, "opf.usb")

USBControl::USBControl(QObject *parent) 
    : QObject(parent)
    , context(nullptr)
    , deviceHandle(nullptr)
{
}

USBControl::~USBControl()
{
    closeUSB();
}

bool USBControl::initializeUSB()
{
    int result = libusb_init(&context);
    if (result < 0) {
        emit error(QString("Failed to initialize libusb: %1").arg(libusb_error_name(result)));
        return false;
    }
    
    return true;
}

void USBControl::closeUSB()
{
    if (deviceHandle) {
        libusb_close(deviceHandle);
        deviceHandle = nullptr;
        emit deviceDisconnected();
    }
    
    if (context) {
        libusb_exit(context);
        context = nullptr;
    }
}

bool USBControl::findAndOpenUVCDevice()
{
    // open the device with the specified VID and PID
    deviceHandle = libusb_open_device_with_vid_pid(context, VENDOR_ID, PRODUCT_ID);
    if (!deviceHandle) {
        qCDebug(log_usb) << "Failed to open device with VID:" << QString("0x%1").arg(VENDOR_ID, 4, 16, QChar('0'))
                         << "PID:" << QString("0x%1").arg(PRODUCT_ID, 4, 16, QChar('0'));
        return false;
    }
    qCDebug(log_usb) << "Successfully opened device with VID:" << QString("0x%1").arg(VENDOR_ID, 4, 16, QChar('0'))
                     << "PID:" << QString("0x%1").arg(PRODUCT_ID, 4, 16, QChar('0'));

    device = libusb_get_device(deviceHandle);
    if (!device) {
        qCDebug(log_usb) << "Failed to get device";
        return false;
    }
    qCDebug(log_usb) << "Successfully opened and configured device";

    getConfigDescriptor();
    showConfigDescriptor();

    int result = libusb_claim_interface(deviceHandle, 0x00);
    if (result != 0) {
        qCDebug(log_usb) << "Failed to claim interface: " << libusb_error_name(result) << libusb_strerror(result);
        return false;
    }

    
    int brightness = getBrightness();
    qCDebug(log_usb) << "Brightness: " << brightness;
    emit deviceConnected();
    return true;
}

void USBControl::getConfigDescriptor()
{
    libusb_get_config_descriptor(device, 0 ,&config_descriptor);
    libusb_free_config_descriptor(config_descriptor);
}

void USBControl::showConfigDescriptor()
{
    qCDebug(log_usb) << "Config descriptor: ";
    qCDebug(log_usb) << "bLength: " << config_descriptor->bLength;
    qCDebug(log_usb) << "bDescriptorType: " << config_descriptor->bDescriptorType;
    qCDebug(log_usb) << "wTotalLength: " << config_descriptor->wTotalLength;
    qCDebug(log_usb) << "bNumInterfaces: " << config_descriptor->bNumInterfaces;
    qCDebug(log_usb) << "bConfigurationValue: " << config_descriptor->bConfigurationValue;
    qCDebug(log_usb) << "iConfiguration: " << config_descriptor->iConfiguration;
    qCDebug(log_usb) << "bmAttributes: " << config_descriptor->bmAttributes;
    qCDebug(log_usb) << "bMaxPower: " << config_descriptor->MaxPower;
    
    
}

int USBControl::getBrightness()
{
    unsigned char data[2];
    int result;
    result = libusb_control_transfer(
        deviceHandle,
        LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_CLASS | LIBUSB_RECIPIENT_INTERFACE,
        UVC_GET_CUR,
        (PU_BRIGHTNESS_CONTROL << 8) | INTERFACE_ID,
        (bDescriptorSubtype << 8) | INTERFACE_ID,
        data,
        sizeof(data),
        0
    );
    if (result != sizeof(data)) {
        qCDebug(log_usb) << "Failed to get brightness: " << libusb_strerror(result);
        return -1;
    }
    return (data[0] << 8) | data[1];
}
