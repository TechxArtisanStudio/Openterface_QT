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

#include "CH32V208Strategy.h"
#include "../ch9329.h"
#include <QSerialPort>
#include <QDebug>

Q_LOGGING_CATEGORY(log_chip_ch32v208, "opf.chip.ch32v208")

bool CH32V208Strategy::supportsBaudrate(int baudrate) const
{
    return baudrate == BAUDRATE_FIXED;
}

QList<int> CH32V208Strategy::supportedBaudrates() const
{
    return { BAUDRATE_FIXED };
}

int CH32V208Strategy::determineInitialBaudrate(int storedBaudrate) const
{
    Q_UNUSED(storedBaudrate)
    // CH32V208 always uses 115200, ignore stored baudrate
    return BAUDRATE_FIXED;
}

int CH32V208Strategy::validateBaudrate(int requestedBaudrate) const
{
    if (requestedBaudrate != BAUDRATE_FIXED) {
        qCWarning(log_chip_ch32v208) << "CH32V208: Only supports 115200 baudrate, ignoring requested:" 
                                     << requestedBaudrate;
    }
    return BAUDRATE_FIXED;
}

QByteArray CH32V208Strategy::buildReconfigurationCommand(int targetBaudrate, uint8_t mode) const
{
    Q_UNUSED(targetBaudrate)
    Q_UNUSED(mode)
    
    // CH32V208 does not support command-based configuration
    qCDebug(log_chip_ch32v208) << "CH32V208: Command-based reconfiguration not supported";
    return QByteArray();
}

bool CH32V208Strategy::performReset(
    QSerialPort* serialPort,
    int targetBaudrate,
    std::function<QByteArray(const QByteArray&, bool)> sendSyncCommand,
    std::function<bool(int)> setBaudRate,
    std::function<void()> closePort,
    std::function<bool(const QString&, int)> openPort,
    std::function<bool()> restartPort)
{
    Q_UNUSED(sendSyncCommand)
    Q_UNUSED(setBaudRate)
    Q_UNUSED(restartPort)
    
    if (!serialPort) {
        qCWarning(log_chip_ch32v208) << "CH32V208: Serial port is null, cannot reset";
        return false;
    }
    
    // CH32V208 only supports 115200
    if (targetBaudrate != BAUDRATE_FIXED) {
        qCWarning(log_chip_ch32v208) << "CH32V208: Ignoring requested baudrate" << targetBaudrate 
                                     << ", using 115200";
        targetBaudrate = BAUDRATE_FIXED;
    }
    
    qCInfo(log_chip_ch32v208) << "CH32V208: Performing simple close/reopen reset";
    
    QString portName = serialPort->portName();
    
    // Simple close and reopen - no commands needed
    closePort();
    
    // Brief delay before reopening
    // Note: The caller should handle this with QTimer::singleShot for non-blocking
    
    bool success = openPort(portName, BAUDRATE_FIXED);
    
    if (success) {
        qCInfo(log_chip_ch32v208) << "CH32V208: Reset completed successfully";
    } else {
        qCWarning(log_chip_ch32v208) << "CH32V208: Failed to reopen port after reset";
    }
    
    return success;
}

bool CH32V208Strategy::performFactoryReset(
    QSerialPort* serialPort,
    std::function<QByteArray(const QByteArray&, bool)> sendSyncCommand,
    std::function<bool(int)> setBaudRate,
    std::function<int()> getAlternateBaudrate)
{
    Q_UNUSED(setBaudRate)
    Q_UNUSED(getAlternateBaudrate)
    
    if (!serialPort) {
        qCWarning(log_chip_ch32v208) << "CH32V208: Serial port is null, cannot factory reset";
        return false;
    }
    
    qCInfo(log_chip_ch32v208) << "CH32V208: Attempting factory reset at 115200";
    
    // Try to send factory reset command - CH32V208 may or may not support this
    QByteArray response = sendSyncCommand(CMD_SET_DEFAULT_CFG, true);
    
    if (response.size() > 0) {
        qCInfo(log_chip_ch32v208) << "CH32V208: Factory reset command successful";
        return true;
    }
    
    // CH32V208 may not support factory reset command
    qCWarning(log_chip_ch32v208) << "CH32V208: Factory reset command not supported or failed";
    return false;
}

ChipConfigResult CH32V208Strategy::attemptBaudrateDetection(
    QSerialPort* serialPort,
    std::function<QByteArray(const QByteArray&, bool)> sendSyncCommand,
    std::function<void()> closePort,
    std::function<bool(const QString&, int)> openPort,
    std::function<bool(int)> setBaudRate,
    std::function<bool(int)> reconfigureChip,
    std::function<bool()> sendResetCommand,
    uint8_t expectedMode)
{
    Q_UNUSED(reconfigureChip)
    Q_UNUSED(sendResetCommand)
    Q_UNUSED(expectedMode)
    
    ChipConfigResult result;
    
    if (!serialPort) {
        qCWarning(log_chip_ch32v208) << "CH32V208: Serial port is null";
        return result;
    }
    
    QString portName = serialPort->portName();
    
    qCInfo(log_chip_ch32v208) << "CH32V208: Only supports 115200, retrying at fixed baudrate";
    
    closePort();
    openPort(portName, BAUDRATE_FIXED);
    
    QByteArray response = sendSyncCommand(CMD_GET_PARA_CFG, true);
    qCDebug(log_chip_ch32v208) << "CH32V208: Response at 115200:" << response.toHex(' ');
    
    if (response.size() > 0) {
        CmdDataParamConfig config = CmdDataParamConfig::fromByteArray(response);
        qCDebug(log_chip_ch32v208) << "CH32V208: Connected at 115200, mode:" 
                                   << QString::number(config.mode, 16);
        
        // CH32V208 mode cannot be changed, just accept whatever mode it has
        qCInfo(log_chip_ch32v208) << "CH32V208: Connection successful (mode cannot be changed on CH32V208)";
        
        result.success = true;
        result.workingBaudrate = BAUDRATE_FIXED;
        result.mode = config.mode;
        setBaudRate(BAUDRATE_FIXED);
    } else {
        qCWarning(log_chip_ch32v208) << "CH32V208: No response at 115200 baudrate";
    }
    
    return result;
}
