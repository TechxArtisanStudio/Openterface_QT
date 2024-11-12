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

#ifndef USBCONTROL_H
#define USBCONTROL_H

#include <libusb.h>
#include <QObject>

class USBControl : public QObject
{
    Q_OBJECT

public:
    explicit USBControl(QObject *parent = nullptr);
    virtual ~USBControl() override;

signals:
    void deviceConnected();
    void deviceDisconnected();
    void error(const QString &message);

public slots:
    bool initializeUSB();
    void closeUSB();
    
    // Only keep brightness and contrast controls
    int getBrightness();
    int getContrast();
    bool setBrightness(int value);
    bool setContrast(int value);
    
    bool findAndOpenUVCDevice();

    // Add these methods
    int getBrightnessMin();
    int getBrightnessMax();
    int getBrightnessDef();
    int getContrastMin();
    int getContrastMax();
    int getContrastDef();
    void debugControlRanges();  // New method to print all ranges

private:
    // UVC Control Constants
    static const uint8_t UVC_GET_CUR = 0x81;
    static const uint8_t UVC_GET_MIN = 0x82;
    static const uint8_t UVC_GET_MAX = 0x83;
    static const uint8_t UVC_GET_DEF = 0x87;
    static const uint8_t UVC_SET_CUR = 0x01;

    // Define the vendor and product IDs
    static const uint16_t VENDOR_ID = 0x534D;   // 534D
    static const uint16_t PRODUCT_ID = 0x2109;  // 2109

    libusb_context *context;
    libusb_device_handle *deviceHandle;
    
    // Helper methods
    int getUVCControl(uint8_t selector, uint8_t unit, uint8_t cs);
    int setUVCControl(uint8_t selector, uint8_t unit, uint8_t cs, int value);
    bool matchUVCDevice(libusb_device *device);
    QString getUSBDeviceString(libusb_device_handle *handle, uint8_t desc_index);
    
    // Only keep brightness and contrast control codes
    static const uint8_t PU_BRIGHTNESS_CONTROL = 0xD0;  // D0
    static const uint8_t PU_CONTRAST_CONTROL = 0xD1;    // D1

    bool isControlSupported(uint8_t control);
};

#endif // USBCONTROL_H
