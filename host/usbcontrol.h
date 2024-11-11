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
    
    // UVC Control Methods
    int getBrightness();
    int getContrast();
    int getSharpness();
    int getSaturation();
    int getGamma();
    int getBacklightCompensation();
    
private:
    // UVC Control Constants
    static const uint8_t UVC_GET_CUR = 0x81;
    static const uint8_t UVC_GET_MIN = 0x82;
    static const uint8_t UVC_GET_MAX = 0x83;
    static const uint8_t UVC_GET_RES = 0x84;
    static const uint8_t UVC_GET_DEF = 0x87;

    libusb_context *context;
    libusb_device_handle *deviceHandle;
    
    // Helper methods
    int getUVCControl(uint8_t selector, uint8_t unit, uint8_t cs);
    bool findAndOpenUVCDevice();
    
    // UVC Processing Unit controls
    static const uint8_t PU_BRIGHTNESS_CONTROL = 0x02;
    static const uint8_t PU_CONTRAST_CONTROL = 0x03;
    static const uint8_t PU_SHARPNESS_CONTROL = 0x05;
    static const uint8_t PU_SATURATION_CONTROL = 0x07;
    static const uint8_t PU_GAMMA_CONTROL = 0x09;
    static const uint8_t PU_BACKLIGHT_COMPENSATION_CONTROL = 0x08;
};

#endif // USBCONTROL_H
