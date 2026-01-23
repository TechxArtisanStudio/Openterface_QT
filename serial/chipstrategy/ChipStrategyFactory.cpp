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

#include "ChipStrategyFactory.h"
#include <QDebug>

// Declare the unified serial logging category (defined in SerialPortManager.cpp)
Q_DECLARE_LOGGING_CATEGORY(log_core_serial)

ChipTypeId ChipStrategyFactory::detectChipType(const QString& portName)
{
    QList<QSerialPortInfo> availablePorts = QSerialPortInfo::availablePorts();
    
    for (const QSerialPortInfo& portInfo : availablePorts) {
        if (portName.indexOf(portInfo.portName()) >= 0) {
            uint16_t vid = portInfo.vendorIdentifier();
            uint16_t pid = portInfo.productIdentifier();
            
            QString vidStr = QString("%1").arg(vid, 4, 16, QChar('0')).toUpper();
            QString pidStr = QString("%1").arg(pid, 4, 16, QChar('0')).toUpper();
            
            qCDebug(log_core_serial) << "Detected VID:PID =" << vidStr << ":" << pidStr 
                                      << "for port" << portName;
            
            uint32_t detectedVidPid = (static_cast<uint32_t>(vid) << 16) | pid;
            
            if (detectedVidPid == static_cast<uint32_t>(ChipTypeId::CH9329)) {
                qCInfo(log_core_serial) << "Detected CH9329 chip for port" << portName;
                return ChipTypeId::CH9329;
            } else if (detectedVidPid == static_cast<uint32_t>(ChipTypeId::CH32V208)) {
                qCInfo(log_core_serial) << "Detected CH32V208 chip for port" << portName;
                return ChipTypeId::CH32V208;
            }
            
            break;
        }
    }
    
    qCWarning(log_core_serial) << "Unknown chip type for port" << portName;
    return ChipTypeId::Unknown;
}

std::unique_ptr<IChipStrategy> ChipStrategyFactory::createStrategy(ChipTypeId chipType)
{
    switch (chipType) {
        case ChipTypeId::CH9329:
            qCDebug(log_core_serial) << "Creating CH9329 strategy";
            return std::make_unique<CH9329Strategy>();
            
        case ChipTypeId::CH32V208:
            qCDebug(log_core_serial) << "Creating CH32V208 strategy";
            return std::make_unique<CH32V208Strategy>();
            
        case ChipTypeId::Unknown:
        default:
            // Default to CH9329 strategy for unknown chips (backward compatible)
            qCWarning(log_core_serial) << "Unknown chip type, using CH9329 strategy as fallback";
            return std::make_unique<CH9329Strategy>();
    }
}

std::unique_ptr<IChipStrategy> ChipStrategyFactory::createStrategyForPort(const QString& portName)
{
    ChipTypeId chipType = detectChipType(portName);
    return createStrategy(chipType);
}

QString ChipStrategyFactory::chipTypeName(ChipTypeId chipType)
{
    switch (chipType) {
        case ChipTypeId::CH9329:
            return QStringLiteral("CH9329");
        case ChipTypeId::CH32V208:
            return QStringLiteral("CH32V208");
        case ChipTypeId::Unknown:
        default:
            return QStringLiteral("Unknown");
    }
}

bool ChipStrategyFactory::supportsCommands(ChipTypeId chipType)
{
    switch (chipType) {
        case ChipTypeId::CH9329:
            return true;
        case ChipTypeId::CH32V208:
            return false;
        case ChipTypeId::Unknown:
        default:
            return true; // Assume yes for unknown chips
    }
}

bool ChipStrategyFactory::supportsUsbSwitch(ChipTypeId chipType)
{
    switch (chipType) {
        case ChipTypeId::CH9329:
            return false;
        case ChipTypeId::CH32V208:
            return true;
        case ChipTypeId::Unknown:
        default:
            return false;
    }
}
