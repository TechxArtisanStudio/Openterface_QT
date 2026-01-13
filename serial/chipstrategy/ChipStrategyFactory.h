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

#ifndef CHIPSTRATEGYFACTORY_H
#define CHIPSTRATEGYFACTORY_H

#include "IChipStrategy.h"
#include "CH9329Strategy.h"
#include "CH32V208Strategy.h"
#include <memory>
#include <QSerialPortInfo>
#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(log_chip_factory)

/**
 * @brief Chip type enumeration matching VID:PID combinations
 */
enum class ChipTypeId : uint32_t {
    Unknown = 0,
    CH9329 = 0x1A867523,     // VID:PID = 1A86:7523
    CH32V208 = 0x1A86FE0C    // VID:PID = 1A86:FE0C
};

/**
 * @brief Factory class for creating chip-specific strategy instances
 * 
 * This factory detects the chip type based on VID/PID and creates
 * the appropriate strategy implementation.
 */
class ChipStrategyFactory
{
public:
    /**
     * @brief Detect chip type from serial port name
     * @param portName The serial port name (e.g., "COM3" or "/dev/ttyUSB0")
     * @return The detected chip type ID
     */
    static ChipTypeId detectChipType(const QString& portName);
    
    /**
     * @brief Create a strategy instance for the given chip type
     * @param chipType The chip type ID
     * @return Unique pointer to the strategy instance
     */
    static std::unique_ptr<IChipStrategy> createStrategy(ChipTypeId chipType);
    
    /**
     * @brief Create a strategy instance by detecting chip type from port name
     * @param portName The serial port name
     * @return Unique pointer to the strategy instance (or default strategy for unknown chips)
     */
    static std::unique_ptr<IChipStrategy> createStrategyForPort(const QString& portName);
    
    /**
     * @brief Get chip type name for logging
     * @param chipType The chip type ID
     * @return Human-readable chip type name
     */
    static QString chipTypeName(ChipTypeId chipType);
    
    /**
     * @brief Check if a chip type supports command-based configuration
     * @param chipType The chip type ID
     * @return true if commands are supported
     */
    static bool supportsCommands(ChipTypeId chipType);
    
    /**
     * @brief Check if a chip type supports USB switch via serial
     * @param chipType The chip type ID
     * @return true if USB switch commands are supported
     */
    static bool supportsUsbSwitch(ChipTypeId chipType);

private:
    ChipStrategyFactory() = default; // Static factory, no instantiation
};

#endif // CHIPSTRATEGYFACTORY_H
