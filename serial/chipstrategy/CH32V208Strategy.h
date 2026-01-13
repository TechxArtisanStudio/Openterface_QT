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

#ifndef CH32V208STRATEGY_H
#define CH32V208STRATEGY_H

#include "IChipStrategy.h"
#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(log_chip_ch32v208)

/**
 * @brief Strategy implementation for CH32V208 HID controller chip
 * 
 * CH32V208 Characteristics:
 * - VID:PID = 1A86:FE0C
 * - Only supports 115200 baudrate
 * - Does NOT support command-based configuration
 * - Reset is done by simple close/reopen
 * - Supports USB switch via serial command (new protocol)
 */
class CH32V208Strategy : public IChipStrategy
{
public:
    // Baudrate constant - CH32V208 only supports 115200
    static constexpr int BAUDRATE_FIXED = 115200;
    
    CH32V208Strategy() = default;
    ~CH32V208Strategy() override = default;
    
    // ========== Chip Information ==========
    QString chipName() const override { return QStringLiteral("CH32V208"); }
    int defaultBaudrate() const override { return BAUDRATE_FIXED; }
    bool supportsBaudrate(int baudrate) const override;
    QList<int> supportedBaudrates() const override;
    
    // ========== Configuration ==========
    int determineInitialBaudrate(int storedBaudrate) const override;
    bool supportsCommandBasedConfiguration() const override { return false; }
    bool supportsUsbSwitchCommand() const override { return true; }
    
    // ========== Reset Operations ==========
    bool performReset(
        QSerialPort* serialPort,
        int targetBaudrate,
        std::function<QByteArray(const QByteArray&, bool)> sendSyncCommand,
        std::function<bool(int)> setBaudRate,
        std::function<void()> closePort,
        std::function<bool(const QString&, int)> openPort,
        std::function<bool()> restartPort
    ) override;
    
    bool performFactoryReset(
        QSerialPort* serialPort,
        std::function<QByteArray(const QByteArray&, bool)> sendSyncCommand,
        std::function<bool(int)> setBaudRate,
        std::function<int()> getAlternateBaudrate
    ) override;
    
    // ========== Baudrate Detection ==========
    ChipConfigResult attemptBaudrateDetection(
        QSerialPort* serialPort,
        std::function<QByteArray(const QByteArray&, bool)> sendSyncCommand,
        std::function<void()> closePort,
        std::function<bool(const QString&, int)> openPort,
        std::function<bool(int)> setBaudRate,
        std::function<bool(int)> reconfigureChip,
        std::function<bool()> sendResetCommand,
        uint8_t expectedMode
    ) override;
    
    QByteArray buildReconfigurationCommand(int targetBaudrate, uint8_t mode) const override;
    int validateBaudrate(int requestedBaudrate) const override;
};

#endif // CH32V208STRATEGY_H
