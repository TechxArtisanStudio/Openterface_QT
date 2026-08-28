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

#ifndef USBPORTRESETTER_H
#define USBPORTRESETTER_H

#include <QObject>
#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(log_usb_reset)

/**
 * @brief Resets USB hub ports to recover from enumeration failures.
 *
 * On Linux, when the CH32V208 serial chip fails to enumerate after target restart
 * (USB error -71 EPROTO), the parent hub port can be reset to force re-enumeration.
 * This uses libusb to send SET_FEATURE(PORT_RESET) control transfers to the hub.
 *
 * This class is only effective on Linux. On Windows and macOS, the methods are
 * no-ops that return false.
 */
class UsbPortResetter : public QObject {
    Q_OBJECT

public:
    explicit UsbPortResetter(QObject *parent = nullptr);
    ~UsbPortResetter() override;

    /**
     * @brief Attempt to reset USB hub ports to recover the serial device.
     *
     * This finds the composite device (345F:2132), navigates to its parent hub,
     * and sends port reset commands to all downstream ports. The serial device
     * (CH32V208, 1A86:FE0C) should then re-enumerate successfully.
     *
     * @return true if at least one port was successfully reset
     */
    bool resetHubPortsForSerialRecovery();

signals:
    void resetStarted();
    void resetCompleted(bool success);
};

#endif // USBPORTRESETTER_H
