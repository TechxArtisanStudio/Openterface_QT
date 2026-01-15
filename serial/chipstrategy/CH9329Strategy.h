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

#ifndef CH9329STRATEGY_H
#define CH9329STRATEGY_H

#include "IChipStrategy.h"
#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(log_chip_ch9329)

/**
 * @brief Strategy implementation for CH9329 HID controller chip
 * 
 * CH9329 Characteristics:
 * - VID:PID = 1A86:7523
 * - Supports both 9600 and 115200 baudrates
 * - Requires command-based configuration for baudrate switching
 * - Supports reset command (CMD_RESET)
 * - Supports factory reset command (CMD_SET_DEFAULT_CFG)
 * - Does NOT support USB switch via serial command
 */
class CH9329Strategy : public IChipStrategy
{
public:
    // Baudrate constants
    static constexpr int BAUDRATE_HIGH = 115200;
    static constexpr int BAUDRATE_LOW = 9600;
    
    CH9329Strategy() = default;
    ~CH9329Strategy() override = default;
    
    // ========== Chip Information ==========
    QString chipName() const override { return QStringLiteral("CH9329"); }
    int defaultBaudrate() const override { return BAUDRATE_LOW; }
    bool supportsBaudrate(int baudrate) const override;
    QList<int> supportedBaudrates() const override;
    
    // ========== Configuration ==========
    int determineInitialBaudrate(int storedBaudrate) const override;
    bool supportsCommandBasedConfiguration() const override { return true; }
    bool supportsUsbSwitchCommand() const override { return false; }
    
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
    
private:
    int getAlternateBaudrate(int currentBaudrate) const;
};

#endif // CH9329STRATEGY_H
