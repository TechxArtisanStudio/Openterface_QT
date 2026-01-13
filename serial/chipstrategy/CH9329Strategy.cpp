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

#include "CH9329Strategy.h"
#include "../ch9329.h"
#include <QSerialPort>
#include <QSettings>
#include <QDebug>

Q_LOGGING_CATEGORY(log_chip_ch9329, "opf.chip.ch9329")

bool CH9329Strategy::supportsBaudrate(int baudrate) const
{
    return baudrate == BAUDRATE_HIGH || baudrate == BAUDRATE_LOW;
}

QList<int> CH9329Strategy::supportedBaudrates() const
{
    return { BAUDRATE_LOW, BAUDRATE_HIGH };
}

int CH9329Strategy::determineInitialBaudrate(int storedBaudrate) const
{
    // Use stored baudrate if valid, otherwise default to 9600
    if (storedBaudrate > 0 && supportsBaudrate(storedBaudrate)) {
        return storedBaudrate;
    }
    return BAUDRATE_LOW;
}

int CH9329Strategy::validateBaudrate(int requestedBaudrate) const
{
    if (supportsBaudrate(requestedBaudrate)) {
        return requestedBaudrate;
    }
    qCWarning(log_chip_ch9329) << "CH9329: Unsupported baudrate" << requestedBaudrate 
                               << ", falling back to" << BAUDRATE_LOW;
    return BAUDRATE_LOW;
}

int CH9329Strategy::getAlternateBaudrate(int currentBaudrate) const
{
    return currentBaudrate == BAUDRATE_HIGH ? BAUDRATE_LOW : BAUDRATE_HIGH;
}

QByteArray CH9329Strategy::buildReconfigurationCommand(int targetBaudrate, uint8_t mode) const
{
    QByteArray command;
    
    if (targetBaudrate == BAUDRATE_LOW) {
        command = CMD_SET_PARA_CFG_PREFIX_9600;
        qCDebug(log_chip_ch9329) << "CH9329: Building 9600 baudrate configuration command";
    } else {
        command = CMD_SET_PARA_CFG_PREFIX_115200;
        qCDebug(log_chip_ch9329) << "CH9329: Building 115200 baudrate configuration command";
    }
    
    // Set mode byte at index 5 (6th byte)
    command[5] = mode;
    
    // Append the mid portion of the command
    command.append(CMD_SET_PARA_CFG_MID);
    
    qCDebug(log_chip_ch9329) << "CH9329: Configuration command built:" << command.toHex(' ');
    return command;
}

bool CH9329Strategy::performReset(
    QSerialPort* serialPort,
    int targetBaudrate,
    std::function<QByteArray(const QByteArray&, bool)> sendSyncCommand,
    std::function<bool(int)> setBaudRate,
    std::function<void()> closePort,
    std::function<bool(const QString&, int)> openPort,
    std::function<bool()> restartPort)
{
    Q_UNUSED(closePort)
    Q_UNUSED(openPort)
    
    if (!serialPort) {
        qCWarning(log_chip_ch9329) << "CH9329: Serial port is null, cannot reset";
        return false;
    }
    
    qCInfo(log_chip_ch9329) << "CH9329: Performing command-based reset to baudrate" << targetBaudrate;
    
    // Validate and adjust baudrate
    targetBaudrate = validateBaudrate(targetBaudrate);
    
    // Get the mode from settings
    QSettings settings("Techxartisan", "Openterface");
    uint8_t mode = settings.value("hardware/operatingMode", 0x02).toUInt();
    
    // Build and send reconfiguration command
    QByteArray configCommand = buildReconfigurationCommand(targetBaudrate, mode);
    QByteArray response = sendSyncCommand(configCommand, true);
    
    if (response.isEmpty()) {
        qCWarning(log_chip_ch9329) << "CH9329: No response to reconfiguration command";
        return false;
    }
    
    // Check response status
    if (response.size() >= 6 && response[5] == DEF_CMD_SUCCESS) {
        qCDebug(log_chip_ch9329) << "CH9329: Reconfiguration command successful";
        
        // Send reset command
        QByteArray resetResponse = sendSyncCommand(CMD_RESET, true);
        if (resetResponse.isEmpty()) {
            qCWarning(log_chip_ch9329) << "CH9329: Reset command failed";
            return false;
        }
        
        qCDebug(log_chip_ch9329) << "CH9329: Reset command successful, changing baudrate and restarting";
        
        // Change baudrate and restart port
        setBaudRate(targetBaudrate);
        restartPort();
        
        qCInfo(log_chip_ch9329) << "CH9329: Reset completed successfully at baudrate" << targetBaudrate;
        return true;
    }
    
    qCWarning(log_chip_ch9329) << "CH9329: Reconfiguration command returned error:" 
                               << QString::number(static_cast<uint8_t>(response[5]), 16);
    return false;
}

bool CH9329Strategy::performFactoryReset(
    QSerialPort* serialPort,
    std::function<QByteArray(const QByteArray&, bool)> sendSyncCommand,
    std::function<bool(int)> setBaudRate,
    std::function<int()> getAlternateBaudrate)
{
    if (!serialPort) {
        qCWarning(log_chip_ch9329) << "CH9329: Serial port is null, cannot factory reset";
        return false;
    }
    
    qCInfo(log_chip_ch9329) << "CH9329: Performing factory reset";
    
    // Try current baudrate first
    QByteArray response = sendSyncCommand(CMD_SET_DEFAULT_CFG, true);
    if (response.size() > 0) {
        qCInfo(log_chip_ch9329) << "CH9329: Factory reset successful at current baudrate";
        return true;
    }
    
    // Try alternate baudrate
    qCDebug(log_chip_ch9329) << "CH9329: Factory reset failed at current baudrate, trying alternate";
    
    int altBaudrate = getAlternateBaudrate();
    serialPort->close();
    setBaudRate(altBaudrate);
    
    if (serialPort->open(QIODevice::ReadWrite)) {
        response = sendSyncCommand(CMD_SET_DEFAULT_CFG, true);
        if (response.size() > 0) {
            qCInfo(log_chip_ch9329) << "CH9329: Factory reset successful at" << altBaudrate;
            return true;
        }
    }
    
    qCWarning(log_chip_ch9329) << "CH9329: Factory reset failed at all baudrates";
    return false;
}

ChipConfigResult CH9329Strategy::attemptBaudrateDetection(
    QSerialPort* serialPort,
    std::function<QByteArray(const QByteArray&, bool)> sendSyncCommand,
    std::function<void()> closePort,
    std::function<bool(const QString&, int)> openPort,
    std::function<bool(int)> setBaudRate,
    std::function<bool(int)> reconfigureChip,
    std::function<bool()> sendResetCommand,
    uint8_t expectedMode)
{
    ChipConfigResult result;
    
    if (!serialPort) {
        qCWarning(log_chip_ch9329) << "CH9329: Serial port is null";
        return result;
    }
    
    QString portName = serialPort->portName();
    int currentBaudrate = serialPort->baudRate();
    int altBaudrate = getAlternateBaudrate(currentBaudrate);
    
    qCDebug(log_chip_ch9329) << "CH9329: Attempting baudrate detection, trying" << altBaudrate;
    
    closePort();
    openPort(portName, altBaudrate);
    
    QByteArray response = sendSyncCommand(CMD_GET_PARA_CFG, true);
    if (response.size() > 0) {
        CmdDataParamConfig config = CmdDataParamConfig::fromByteArray(response);
        qCDebug(log_chip_ch9329) << "CH9329: Connected at baudrate" << altBaudrate 
                                 << ", mode:" << QString::number(config.mode, 16);
        
        if (config.mode == expectedMode) {
            qCDebug(log_chip_ch9329) << "CH9329: Mode is correct";
            result.success = true;
            result.workingBaudrate = altBaudrate;
            result.mode = config.mode;
            setBaudRate(altBaudrate);
        } else {
            qCWarning(log_chip_ch9329) << "CH9329: Mode mismatch, attempting reconfiguration";
            
            if (reconfigureChip(altBaudrate)) {
                qCDebug(log_chip_ch9329) << "CH9329: Reconfiguration successful, sending reset";
                if (sendResetCommand()) {
                    result.success = true;
                    result.workingBaudrate = altBaudrate;
                    result.mode = expectedMode;
                }
            } else {
                qCWarning(log_chip_ch9329) << "CH9329: Reconfiguration failed";
            }
        }
    } else {
        qCWarning(log_chip_ch9329) << "CH9329: No response at alternate baudrate" << altBaudrate;
    }
    
    return result;
}
