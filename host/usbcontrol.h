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

#include <libusb-1.0/libusb.h>
#include <QObject>
#include <QLoggingCategory>

// Add this line to declare the logging category
Q_DECLARE_LOGGING_CATEGORY(log_usb)

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
    bool findAndOpenUVCDevice();

private:
    static const uint16_t VENDOR_ID = 0x534D;   // 534D
    static const uint16_t PRODUCT_ID = 0x2109;  // 2109
    
    static const uint8_t UVC_GET_CUR = 0x81;
    static const uint8_t UVC_GET_MIN = 0x82;
    static const uint8_t UVC_GET_MAX = 0x83;
    static const uint8_t UVC_GET_DEF = 0x87;
    static const uint8_t UVC_SET_CUR = 0x01;
    static const uint8_t bLength = 0x0B;
    static const uint8_t bDescriptorType = 0x24;
    static const uint8_t bDescriptorSubtype = 0x05;
    static const uint8_t bUnitID = 0x02;
    static const uint8_t bSourceID = 0x01;
    static const uint8_t bControlSize = 0x02;
    static const uint8_t bmControls = 0x0F;
    static const uint8_t PU_BRIGHTNESS_CONTROL = 0x01;
    static const uint8_t PU_CONTRAST_CONTROL = 0x02;


    // Define the vendor and product IDs
    
    static const uint8_t INTERFACE_ID = 0x24;
    // UVC Control Constants
    
    libusb_context *context;
    libusb_device_handle *deviceHandle;
    libusb_device *device;
    libusb_config_descriptor *config_descriptor;
    void getConfigDescriptor();
    void showConfigDescriptor();
    int getBrightness();
};

#endif // USBCONTROL_H
