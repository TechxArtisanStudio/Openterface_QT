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
    
    bool success = findAndOpenUVCDevice();
    if (success) {
        emit deviceConnected();
    } else {
        emit error("No UVC device found");
    }
    return success;
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
        libusb_device_descriptor desc;
        
        int result = libusb_get_device_descriptor(device, &desc);
        if (result < 0) continue;

        // Check for UVC devices
        // USB Video Class code is 0x0E
        // Interface class for video is 0x0E
        // Some devices use class 0xEF (Miscellaneous Device Class) with subclass 2
        if (desc.bDeviceClass == 0x0E || // USB Video Class
            desc.bDeviceClass == LIBUSB_CLASS_VIDEO || // Standard Video Class
            (desc.bDeviceClass == 0xEF && desc.bDeviceSubClass == 0x02)) { // Miscellaneous Device Class
            
            result = libusb_open(device, &deviceHandle);
            if (result < 0) continue;

            // Get configuration descriptor to verify it's a UVC device
            libusb_config_descriptor *config;
            result = libusb_get_config_descriptor(device, 0, &config);
            if (result < 0) {
                libusb_close(deviceHandle);
                continue;
            }

            // Look for Video interface
            bool isUVCDevice = false;
            for (int i = 0; i < config->bNumInterfaces; i++) {
                const libusb_interface *interface = &config->interface[i];
                for (int j = 0; j < interface->num_altsetting; j++) {
                    const libusb_interface_descriptor *altsetting = &interface->altsetting[j];
                    if (altsetting->bInterfaceClass == LIBUSB_CLASS_VIDEO) {
                        isUVCDevice = true;
                        break;
                    }
                }
                if (isUVCDevice) break;
            }

            libusb_free_config_descriptor(config);

            if (isUVCDevice) {
                // Successfully found and opened a UVC device
                found = true;
                break;
            } else {
                libusb_close(deviceHandle);
                deviceHandle = nullptr;
            }
        }
    }
    
    libusb_free_device_list(deviceList, 1);
    return found;
}

int USBControl::getUVCControl(uint8_t selector, uint8_t unit, uint8_t cs)
{
    if (!deviceHandle) return -1;
    
    uint8_t data[2] = {0};
    int requestType = LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_CLASS | LIBUSB_RECIPIENT_INTERFACE;
    int timeout = 1000;
    
    int result = libusb_control_transfer(
        deviceHandle,
        requestType,
        selector,
        cs << 8,
        unit << 8,
        data,
        sizeof(data),
        timeout
    );
    
    if (result < 0) {
        qDebug() << "Failed to get UVC control:" << libusb_error_name(result);
        return -1;
    }
    
    return (data[1] << 8) | data[0];
}

int USBControl::getBrightness()
{
    return getUVCControl(UVC_GET_CUR, 2, PU_BRIGHTNESS_CONTROL);
}

int USBControl::getContrast()
{
    return getUVCControl(UVC_GET_CUR, 2, PU_CONTRAST_CONTROL);
}

int USBControl::getSharpness()
{
    return getUVCControl(UVC_GET_CUR, 2, PU_SHARPNESS_CONTROL);
}

int USBControl::getSaturation()
{
    return getUVCControl(UVC_GET_CUR, 2, PU_SATURATION_CONTROL);
}

int USBControl::getGamma()
{
    return getUVCControl(UVC_GET_CUR, 2, PU_GAMMA_CONTROL);
}

int USBControl::getBacklightCompensation()
{
    return getUVCControl(UVC_GET_CUR, 2, PU_BACKLIGHT_COMPENSATION_CONTROL);
}

