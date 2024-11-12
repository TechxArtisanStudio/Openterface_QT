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

#include "usbcontrol.h"
#include <QDebug>

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
    libusb_device **deviceList;
    ssize_t deviceCount = libusb_get_device_list(context, &deviceList);
    
    bool found = false;
    
    for (ssize_t i = 0; i < deviceCount; i++) {
        libusb_device *device = deviceList[i];
        
        if (matchUVCDevice(device)) {
            qDebug() << "Found UVC device";
            int result = libusb_open(device, &deviceHandle);
            if (result >= 0) {
                // Detach kernel driver if necessary
                if (libusb_kernel_driver_active(deviceHandle, 0) == 1) {
                    qDebug() << "Kernel driver active, detaching...";
                    if (libusb_detach_kernel_driver(deviceHandle, 0) == 0) {
                        qDebug() << "Kernel driver detached";
                    }
                }

                // Claim interface 0
                result = libusb_claim_interface(deviceHandle, 0);
                if (result < 0) {
                    qDebug() << "Failed to claim interface:" << libusb_error_name(result);
                    libusb_close(deviceHandle);
                    deviceHandle = nullptr;
                    continue;
                }

                found = true;
                qDebug() << "Successfully opened UVC device and claimed interface";
                debugControlRanges();
                break;
            } else {
                qDebug() << "Failed to open UVC device:" << libusb_error_name(result);
            }
        }
    }
    
    if (!found) {
        qDebug() << "No matching UVC device found";
    }
    
    libusb_free_device_list(deviceList, 1);
    return found;
}

bool USBControl::matchUVCDevice(libusb_device *device)
{
    libusb_device_descriptor desc;
    int result = libusb_get_device_descriptor(device, &desc);
    if (result < 0) return false;
    
    qDebug() << "Device descriptor:" << QString::number(desc.idVendor, 16) << QString::number(desc.idProduct, 16);

    // Match the specific vendor and product IDs
    return (desc.idVendor == VENDOR_ID && desc.idProduct == PRODUCT_ID);
}

QString USBControl::getUSBDeviceString(libusb_device_handle *handle, uint8_t desc_index)
{
    if (desc_index == 0) return QString();
    
    unsigned char buffer[256];
    int result = libusb_get_string_descriptor_ascii(handle, desc_index, buffer, sizeof(buffer));
    
    if (result < 0) return QString();
    
    return QString::fromLatin1(reinterpret_cast<const char*>(buffer), result);
}

int USBControl::getUVCControl(uint8_t selector, uint8_t unit, uint8_t cs)
{
    if (!deviceHandle) return -1;
    
    uint8_t data[2] = {0};
    int requestType = LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_CLASS | LIBUSB_RECIPIENT_INTERFACE;
    int wValue = (cs << 8) | 0x00;  // Control selector in high byte
    int wIndex = (unit << 8) | 0x00; // Unit ID in high byte, Interface 0
    int timeout = 1000;
    
    qDebug() << "UVC Control Request:";
    qDebug() << "  Request type:" << QString("0x%1").arg(requestType, 2, 16, QChar('0'));
    qDebug() << "  Request:" << QString("0x%1").arg(selector, 2, 16, QChar('0'));
    qDebug() << "  wValue:" << QString("0x%1").arg(wValue, 4, 16, QChar('0'));
    qDebug() << "  wIndex:" << QString("0x%1").arg(wIndex, 4, 16, QChar('0'));
    
    int result = libusb_control_transfer(
        deviceHandle,
        requestType,
        selector,        // bRequest
        wValue,          // wValue
        wIndex,          // wIndex
        data,           // data buffer
        sizeof(data),   // wLength
        timeout
    );
    
    if (result < 0) {
        qDebug() << "Failed to get UVC control:" << libusb_error_name(result);
        return -1;
    }
    
    int value = (data[1] << 8) | data[0];
    qDebug() << "  Response value:" << value;
    return value;
}

int USBControl::setUVCControl(uint8_t selector, uint8_t unit, uint8_t cs, int value)
{
    if (!deviceHandle) return -1;
    
    uint8_t data[2];
    data[0] = value & 0xFF;
    data[1] = (value >> 8) & 0xFF;
    
    int requestType = LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_CLASS | LIBUSB_RECIPIENT_INTERFACE;
    int wValue = (cs << 8) | 0x00;  // Control selector in high byte
    int wIndex = (unit << 8) | 0x00; // Unit ID in high byte, Interface 0
    int timeout = 1000;
    
    qDebug() << "UVC Control Set:";
    qDebug() << "  Request type:" << QString("0x%1").arg(requestType, 2, 16, QChar('0'));
    qDebug() << "  Request:" << QString("0x%1").arg(selector, 2, 16, QChar('0'));
    qDebug() << "  wValue:" << QString("0x%1").arg(wValue, 4, 16, QChar('0'));
    qDebug() << "  wIndex:" << QString("0x%1").arg(wIndex, 4, 16, QChar('0'));
    qDebug() << "  Setting value:" << value;
    
    int result = libusb_control_transfer(
        deviceHandle,
        requestType,
        selector,        // bRequest
        wValue,          // wValue
        wIndex,          // wIndex
        data,           // data buffer
        sizeof(data),   // wLength
        timeout
    );
    
    if (result < 0) {
        qDebug() << "Failed to set UVC control:" << libusb_error_name(result);
        return -1;
    }
    
    return 0;
}

int USBControl::getBrightness()
{
    if (!isControlSupported(0)) {
        qDebug() << "Brightness control is not supported";
        return -1;
    }
    return getUVCControl(UVC_GET_CUR, 2, PU_BRIGHTNESS_CONTROL);
}

bool USBControl::setBrightness(int value)
{
    if (!isControlSupported(0)) {
        qDebug() << "Brightness control is not supported";
        return false;
    }
    return setUVCControl(UVC_SET_CUR, 2, PU_BRIGHTNESS_CONTROL, value) >= 0;
}

int USBControl::getContrast()
{
    if (!isControlSupported(1)) {
        qDebug() << "Contrast control is not supported";
        return -1;
    }
    return getUVCControl(UVC_GET_CUR, 2, PU_CONTRAST_CONTROL);
}

bool USBControl::setContrast(int value)
{
    if (!isControlSupported(1)) {
        qDebug() << "Contrast control is not supported";
        return false;
    }
    return setUVCControl(UVC_SET_CUR, 2, PU_CONTRAST_CONTROL, value) >= 0;
}

bool USBControl::isControlSupported(uint8_t control)
{
    static const uint16_t bmControls = 0x000F;
    
    return (bmControls & (1 << control)) != 0;
}

int USBControl::getBrightnessMin()
{
    return getUVCControl(UVC_GET_MIN, 2, PU_BRIGHTNESS_CONTROL);
}

int USBControl::getBrightnessMax()
{
    return getUVCControl(UVC_GET_MAX, 2, PU_BRIGHTNESS_CONTROL);
}

int USBControl::getBrightnessDef()
{
    return getUVCControl(UVC_GET_DEF, 2, PU_BRIGHTNESS_CONTROL);
}

int USBControl::getContrastMin()
{
    return getUVCControl(UVC_GET_MIN, 2, PU_CONTRAST_CONTROL);
}

int USBControl::getContrastMax()
{
    return getUVCControl(UVC_GET_MAX, 2, PU_CONTRAST_CONTROL);
}

int USBControl::getContrastDef()
{
    return getUVCControl(UVC_GET_DEF, 2, PU_CONTRAST_CONTROL);
}

void USBControl::debugControlRanges()
{
    qDebug() << "UVC Control Ranges:";
    qDebug() << "Brightness:";
    qDebug() << "  Current:" << getBrightness();
    qDebug() << "  Min:" << getBrightnessMin();
    qDebug() << "  Max:" << getBrightnessMax();
    qDebug() << "  Default:" << getBrightnessDef();
    
    qDebug() << "Contrast:";
    qDebug() << "  Current:" << getContrast();
    qDebug() << "  Min:" << getContrastMin();
    qDebug() << "  Max:" << getContrastMax();
    qDebug() << "  Default:" << getContrastDef();
}

