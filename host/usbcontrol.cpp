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
    deviceHandle = libusb_open_device_with_vid_pid(context, VENDOR_ID, PRODUCT_ID);
    if (!deviceHandle) {
        qDebug(log_usb) << "Failed to open device with VID:" << QString("0x%1").arg(VENDOR_ID, 4, 16, QChar('0'))
                         << "PID:" << QString("0x%1").arg(PRODUCT_ID, 4, 16, QChar('0'));
        return false;
    }
    
    qDebug(log_usb) << "Successfully opened device with VID:" << QString("0x%1").arg(VENDOR_ID, 4, 16, QChar('0'))
                     << "PID:" << QString("0x%1").arg(PRODUCT_ID, 4, 16, QChar('0'));

    libusb_device *device = libusb_get_device(deviceHandle);
    if (!device) {
        qDebug(log_usb) << "Failed to get device from handle";
        libusb_close(deviceHandle);
        deviceHandle = nullptr;
        return false;
    }

    qDebug(log_usb) << "Successfully opened and configured device";
    debugControlRanges();
    emit deviceConnected();
    return true;
}

QString USBControl::getUSBDeviceString(libusb_device_handle *handle, uint8_t desc_index)
{
    if (desc_index == 0) return QString();
    
    unsigned char buffer[256];
    int result = libusb_get_string_descriptor_ascii(handle, desc_index, buffer, sizeof(buffer));
    
    if (result < 0) return QString();
    
    return QString::fromLatin1(reinterpret_cast<const char*>(buffer), result);
}

int USBControl::testUSBControl()
{
    uint8_t data[256] = {};
    int result = libusb_control_transfer(
        deviceHandle,
        LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_STANDARD | LIBUSB_RECIPIENT_DEVICE,
        LIBUSB_REQUEST_GET_DESCRIPTOR,
        (LIBUSB_DT_DEVICE << 8) | 0,
        0,
        data,
        sizeof(data),
        1000
    );
    if (result < 0) {
        qDebug(log_usb) << "Test USB Control Result:" << libusb_error_name(result);
        return -1;
    }
    qDebug(log_usb) << "Test USB Control Result:" << result;
    for(int i = 0; i < result; i++) {
        qDebug(log_usb) << "Data[" << i << "]:" << QString("0x%1").arg(data[i], 2, 16, QChar('0'));
    }
}

int USBControl::testUVCControl()
{
    uint8_t data[2] = {0x0F, 0x0F};
    uint8_t request_type = LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_CLASS | LIBUSB_RECIPIENT_INTERFACE;
    uint8_t bRequest = 0x01;
    uint16_t wValue = 0x0002;
    uint16_t wIndex = 0x01;
    uint16_t wLength = 2;

    int result = libusb_control_transfer(deviceHandle, request_type, bRequest, wValue, wIndex, data, wLength, 0);
    if (result < 0) {
        qDebug(log_usb) << "Test UVC Control Result:" << libusb_error_name(result);
        return -1;  
    }
    qDebug(log_usb) << "Test UVC Control Result:" << result;

    return 0;
}

int USBControl::getUVCControl(uint8_t selector, uint8_t unit, uint8_t cs)
{
    if (!deviceHandle) return -1;
    
    uint8_t data[2] = {0};
    int requestType = LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_CLASS | LIBUSB_RECIPIENT_INTERFACE;
    int wValue = (cs << 8) | 0x00;  // Control selector in high byte
    int wIndex = (unit << 8) | 0x00; // Unit ID in high byte, Interface 0
    int timeout = 1000;
    
    qDebug(log_usb) << "UVC Control Request:";
    qDebug(log_usb) << "  Request type:" << QString("0x%1").arg(requestType, 2, 16, QChar('0'));
    qDebug(log_usb) << "  Request:" << QString("0x%1").arg(selector, 2, 16, QChar('0'));
    qDebug(log_usb) << "  wValue:" << QString("0x%1").arg(wValue, 4, 16, QChar('0'));
    qDebug(log_usb) << "  wIndex:" << QString("0x%1").arg(wIndex, 4, 16, QChar('0'));
    
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
        qDebug(log_usb) << "Failed to get UVC control:" << libusb_error_name(result);
        return -1;
    }
    
    int value = (data[1] << 8) | data[0];
    qDebug(log_usb) << "  Response value:" << value;
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
    
    qDebug(log_usb) << "UVC Control Set:";
    qDebug(log_usb) << "  Request type:" << QString("0x%1").arg(requestType, 2, 16, QChar('0'));
    qDebug(log_usb) << "  Request:" << QString("0x%1").arg(selector, 2, 16, QChar('0'));
    qDebug(log_usb) << "  wValue:" << QString("0x%1").arg(wValue, 4, 16, QChar('0'));
    qDebug(log_usb) << "  wIndex:" << QString("0x%1").arg(wIndex, 4, 16, QChar('0'));
    qDebug(log_usb) << "  Setting value:" << value;
    
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
        qDebug(log_usb) << "Failed to set UVC control:" << libusb_error_name(result);
        return -1;
    }
    
    return 0;
}

int USBControl::getBrightness()
{
    if (!isControlSupported(0)) {
        qDebug(log_usb) << "Brightness control is not supported";
        return -1;
    }
    return getUVCControl(UVC_GET_CUR, 2, PU_BRIGHTNESS_CONTROL);
}

bool USBControl::setBrightness(int value)
{
    if (!isControlSupported(0)) {
        qDebug(log_usb) << "Brightness control is not supported";
        return false;
    }
    return setUVCControl(UVC_SET_CUR, 2, PU_BRIGHTNESS_CONTROL, value) >= 0;
}

int USBControl::getContrast()
{
    if (!isControlSupported(1)) {
        qDebug(log_usb) << "Contrast control is not supported";
        return -1;
    }
    return getUVCControl(UVC_GET_CUR, 2, PU_CONTRAST_CONTROL);
}

bool USBControl::setContrast(int value)
{
    if (!isControlSupported(1)) {
        qDebug(log_usb) << "Contrast control is not supported";
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
    qDebug(log_usb) << "UVC Control Ranges:";
    qDebug(log_usb) << "Brightness:";
    qDebug(log_usb) << "  Current:" << getBrightness();
    qDebug(log_usb) << "  Min:" << getBrightnessMin();
    qDebug(log_usb) << "  Max:" << getBrightnessMax();
    qDebug(log_usb) << "  Default:" << getBrightnessDef();
    
    qDebug(log_usb) << "Contrast:";
    qDebug(log_usb) << "  Current:" << getContrast();
    qDebug(log_usb) << "  Min:" << getContrastMin();
    qDebug(log_usb) << "  Max:" << getContrastMax();
    qDebug(log_usb) << "  Default:" << getContrastDef();
    
    // testUVCControl();
}


