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

#ifndef ICHIPSTRATEGY_H
#define ICHIPSTRATEGY_H

#include <QString>
#include <QByteArray>
#include <functional>

class QSerialPort;

/**
 * @brief Configuration result from chip initialization
 */
struct ChipConfigResult {
    bool success = false;
    int workingBaudrate = 9600;
    uint8_t mode = 0;
};

/**
 * @brief Interface for chip-specific serial communication strategies
 * 
 * This interface abstracts chip-specific behaviors for different HID controller chips
 * (CH9329, CH32V208, etc.) used in the Openterface Mini KVM.
 * 
 * Each chip type has different capabilities:
 * - CH9329: Supports 9600 and 115200 baudrates, requires command-based configuration
 * - CH32V208: Only supports 115200 baudrate, uses simple close/reopen for reset
 */
class IChipStrategy
{
public:
    virtual ~IChipStrategy() = default;
    
    // ========== Chip Information ==========
    
    /**
     * @brief Get the chip type name for logging
     */
    virtual QString chipName() const = 0;
    
    /**
     * @brief Get the default baudrate for this chip
     */
    virtual int defaultBaudrate() const = 0;
    
    /**
     * @brief Check if this chip supports the given baudrate
     */
    virtual bool supportsBaudrate(int baudrate) const = 0;
    
    /**
     * @brief Get the list of supported baudrates
     */
    virtual QList<int> supportedBaudrates() const = 0;
    
    // ========== Configuration ==========
    
    /**
     * @brief Determine the initial baudrate to use when connecting
     * @param storedBaudrate The baudrate stored in settings (0 if none)
     * @return The baudrate to use for initial connection
     */
    virtual int determineInitialBaudrate(int storedBaudrate) const = 0;
    
    /**
     * @brief Check if this chip supports command-based configuration
     * CH32V208 does not support commands for configuration changes
     */
    virtual bool supportsCommandBasedConfiguration() const = 0;
    
    /**
     * @brief Check if this chip supports USB switching via serial commands
     */
    virtual bool supportsUsbSwitchCommand() const = 0;
    
    // ========== Reset Operations ==========
    
    /**
     * @brief Perform chip reset operation
     * @param serialPort Pointer to the serial port
     * @param targetBaudrate The desired baudrate after reset
     * @param sendSyncCommand Function to send sync command and get response
     * @return true if reset was successful
     * 
     * For CH32V208: Simple close/reopen at 115200
     * For CH9329: Send reconfiguration command + reset command
     */
    virtual bool performReset(
        QSerialPort* serialPort,
        int targetBaudrate,
        std::function<QByteArray(const QByteArray&, bool)> sendSyncCommand,
        std::function<bool(int)> setBaudRate,
        std::function<void()> closePort,
        std::function<bool(const QString&, int)> openPort,
        std::function<bool()> restartPort
    ) = 0;
    
    /**
     * @brief Perform factory reset operation
     * @param serialPort Pointer to the serial port
     * @param sendSyncCommand Function to send sync command and get response
     * @return true if factory reset was successful
     */
    virtual bool performFactoryReset(
        QSerialPort* serialPort,
        std::function<QByteArray(const QByteArray&, bool)> sendSyncCommand,
        std::function<bool(int)> setBaudRate,
        std::function<int()> getAlternateBaudrate
    ) = 0;
    
    // ========== Baudrate Detection ==========
    
    /**
     * @brief Attempt to detect and configure the correct baudrate
     * @param serialPort Pointer to the serial port
     * @param sendSyncCommand Function to send sync command and get response
     * @param expectedMode The expected operating mode from settings
     * @return Configuration result with success status and working baudrate
     */
    virtual ChipConfigResult attemptBaudrateDetection(
        QSerialPort* serialPort,
        std::function<QByteArray(const QByteArray&, bool)> sendSyncCommand,
        std::function<void()> closePort,
        std::function<bool(const QString&, int)> openPort,
        std::function<bool(int)> setBaudRate,
        std::function<bool(int)> reconfigureChip,
        std::function<bool()> sendResetCommand,
        uint8_t expectedMode
    ) = 0;
    
    /**
     * @brief Build the reconfiguration command for the target baudrate
     * @param targetBaudrate The desired baudrate
     * @param mode The operating mode
     * @return The command bytes, or empty if not supported
     */
    virtual QByteArray buildReconfigurationCommand(int targetBaudrate, uint8_t mode) const = 0;
    
    /**
     * @brief Validate and adjust baudrate for this chip
     * @param requestedBaudrate The requested baudrate
     * @return The actual baudrate to use (may be different if chip doesn't support requested)
     */
    virtual int validateBaudrate(int requestedBaudrate) const = 0;
};

#endif // ICHIPSTRATEGY_H
