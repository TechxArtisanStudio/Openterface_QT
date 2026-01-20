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

#include "SerialProtocol.h"
#include "../SerialPortManager.h"
#include <QDebug>
#include <QLoggingCategory>

// Declare the unified serial logging category (defined in SerialPortManager.cpp)
Q_DECLARE_LOGGING_CATEGORY(log_core_serial)


using namespace SerialProtocolConstants;

SerialProtocol::SerialProtocol(QObject *parent)
    : QObject(parent)
{
}

// ========== Packet Building ==========

QByteArray SerialProtocol::buildPacket(const QByteArray& commandData)
{
    QByteArray packet = commandData;
    packet.append(static_cast<char>(calculateChecksum(commandData)));
    return packet;
}

uint8_t SerialProtocol::calculateChecksum(const QByteArray& data)
{
    uint32_t sum = 0;
    for (char byte : data) {
        sum += static_cast<unsigned char>(byte);
    }
    return static_cast<uint8_t>(sum % 256);
}

bool SerialProtocol::verifyChecksum(const QByteArray& packet)
{
    if (packet.size() < MIN_PACKET_SIZE) {
        return false;
    }
    
    // Calculate checksum of all bytes except the last one
    QByteArray dataWithoutChecksum = packet.left(packet.size() - 1);
    uint8_t calculatedChecksum = calculateChecksum(dataWithoutChecksum);
    uint8_t receivedChecksum = static_cast<uint8_t>(packet.at(packet.size() - 1));
    
    return calculatedChecksum == receivedChecksum;
}

// ========== Packet Parsing ==========

bool SerialProtocol::validateHeader(const QByteArray& data)
{
    if (data.size() < HEADER_SIZE) {
        return false;
    }
    return static_cast<uint8_t>(data[0]) == HEADER_BYTE_1 && 
           static_cast<uint8_t>(data[1]) == HEADER_BYTE_2;
}

int SerialProtocol::extractPacketSize(const QByteArray& data)
{
    if (data.size() < 5) {
        return -1;
    }
    
    // Packet structure: header(2) + addr(1) + cmd(1) + len(1) + payload(len) + checksum(1)
    int payloadLength = static_cast<unsigned char>(data[4]);
    return MIN_PACKET_SIZE + payloadLength;
}

ParsedPacket SerialProtocol::parsePacket(const QByteArray& data)
{
    ParsedPacket result;
    result.rawPacket = data;
    
    // Validate minimum size
    if (data.size() < MIN_PACKET_SIZE) {
        result.errorMessage = QString("Packet too small: %1 bytes (minimum %2)")
                              .arg(data.size()).arg(MIN_PACKET_SIZE);
        qCWarning(log_core_serial) << result.errorMessage;
        return result;
    }
    
    // Validate header
    if (!validateHeader(data)) {
        result.errorMessage = QString("Invalid header: expected 0x57AB, got 0x%1%2")
                              .arg(static_cast<uint8_t>(data[0]), 2, 16, QChar('0'))
                              .arg(static_cast<uint8_t>(data[1]), 2, 16, QChar('0'));
        qCWarning(log_core_serial) << result.errorMessage;
        return result;
    }
    
    // Extract packet fields
    result.commandCode = static_cast<uint8_t>(data[3]);
    result.responseCode = result.commandCode | RESPONSE_BIT;
    result.payloadLength = static_cast<uint8_t>(data[4]);
    
    // Calculate expected packet size
    int expectedSize = MIN_PACKET_SIZE + result.payloadLength;
    if (data.size() < expectedSize) {
        result.errorMessage = QString("Packet incomplete: expected %1 bytes, got %2")
                              .arg(expectedSize).arg(data.size());
        qCWarning(log_core_serial) << result.errorMessage;
        return result;
    }
    
    // Extract payload (bytes between length and checksum)
    if (result.payloadLength > 0) {
        result.payload = data.mid(5, result.payloadLength);
        if (result.payload.size() > 0) {
            result.status = static_cast<uint8_t>(result.payload[0]);
        }
    }
    
    // Verify checksum
    QByteArray packetToVerify = data.left(expectedSize);
    if (!verifyChecksum(packetToVerify)) {
        result.errorMessage = "Checksum verification failed";
        qCWarning(log_core_serial) << result.errorMessage << "Data:" << data.toHex(' ');
        // Still mark as valid for now, as some responses may have checksum issues
    }
    
    result.valid = true;
    result.rawPacket = packetToVerify;
    
    qCDebug(log_core_serial) << "Parsed packet: cmd=0x" << QString::number(result.commandCode, 16)
                                 << "len=" << result.payloadLength
                                 << "status=0x" << QString::number(result.status, 16);
    
    // Also explicitly log protocol parsing to file during diagnostics
    if (SerialPortManager* manager = &SerialPortManager::getInstance()) {
        if (manager->getSerialLogFilePath().contains("serial_log_diagnostics")) {
            manager->log(QString("PROTOCOL PARSE: cmd=0x%1, len=%2, status=0x%3")
                        .arg(result.commandCode, 2, 16, QChar('0'))
                        .arg(result.payloadLength)
                        .arg(result.status, 2, 16, QChar('0')));
        }
    }
    
    return result;
}

// ========== Response Processing ==========

void SerialProtocol::setResponseHandler(IProtocolResponseHandler* handler)
{
    m_handler = handler;
}

bool SerialProtocol::processRawData(const QByteArray& data)
{
    ParsedPacket packet = parsePacket(data);
    if (!packet.valid) {
        if (m_handler) {
            m_handler->onProtocolError(packet.status, packet.rawPacket);
        }
        emit protocolError(packet.status, packet.errorMessage);
        return false;
    }
    
    ResponseResult result = processResponse(packet);
    return result.success;
}

ResponseResult SerialProtocol::processResponse(const ParsedPacket& packet)
{
    ResponseResult result;
    result.responseCode = packet.commandCode | RESPONSE_BIT;
    
    if (!packet.valid) {
        result.description = "Invalid packet";
        return result;
    }
    
    // Check for error status in certain command ranges
    if (packet.status != STATUS_SUCCESS && 
        (packet.commandCode >= 0xC0 && packet.commandCode <= 0xCF)) {
        result.description = statusToString(packet.status);
        if (m_handler) {
            m_handler->onProtocolError(packet.status, packet.rawPacket);
        }
        emit protocolError(packet.status, result.description);
        return result;
    }
    
    // Route to appropriate handler based on response code
    uint8_t respCode = packet.commandCode | RESPONSE_BIT;
    
    switch (respCode) {
        case RESP_GET_INFO:
            return processGetInfoResponse(packet);
            
        case RESP_SEND_KB_GENERAL:
            return processKeyboardResponse(packet);
            
        case RESP_SEND_MOUSE_ABS:
            return processMouseAbsResponse(packet);
            
        case RESP_SEND_MOUSE_REL:
            return processMouseRelResponse(packet);
            
        case RESP_GET_PARA_CFG:
            return processGetParamConfigResponse(packet);
            
        case RESP_SET_PARA_CFG:
            return processSetParamConfigResponse(packet);
            
        case RESP_RESET:
            return processResetResponse(packet);
            
        case RESP_USB_SWITCH:
            return processUsbSwitchResponse(packet);
            
        default:
            result.description = QString("Unknown response code: 0x%1").arg(respCode, 2, 16, QChar('0'));
            qCDebug(log_core_serial) << result.description << "Packet:" << packet.rawPacket.toHex(' ');
            if (m_handler) {
                m_handler->onUnknownResponse(packet.rawPacket);
            }
            emit unknownResponseReceived(packet.rawPacket);
            return result;
    }
}

ResponseResult SerialProtocol::processGetInfoResponse(const ParsedPacket& packet)
{
    ResponseResult result;
    result.responseCode = RESP_GET_INFO;
    result.success = true;
    result.description = "Get info response";
    
    if (packet.rawPacket.size() >= 8) {
        result.targetUsbConnected = (static_cast<uint8_t>(packet.rawPacket[6]) == 0x01);
        uint8_t indicators = static_cast<uint8_t>(packet.rawPacket[7]);
        
        result.numLockState = (indicators & 0x01) != 0;
        result.capsLockState = (indicators & 0x02) != 0;
        result.scrollLockState = (indicators & 0x04) != 0;
        
        if (m_handler) {
            m_handler->onGetInfoResponse(result.targetUsbConnected, indicators);
        }
        emit getInfoReceived(result.targetUsbConnected, indicators);
    }
    
    return result;
}

ResponseResult SerialProtocol::processKeyboardResponse(const ParsedPacket& packet)
{
    ResponseResult result;
    result.responseCode = RESP_SEND_KB_GENERAL;
    result.success = isSuccess(packet.status);
    result.description = QString("Keyboard response: %1").arg(statusToString(packet.status));
    
    if (m_handler) {
        m_handler->onKeyboardResponse(packet.status);
    }
    emit keyboardResponseReceived(packet.status);
    
    return result;
}

ResponseResult SerialProtocol::processMouseAbsResponse(const ParsedPacket& packet)
{
    ResponseResult result;
    result.responseCode = RESP_SEND_MOUSE_ABS;
    result.success = isSuccess(packet.status);
    result.description = QString("Absolute mouse response: %1").arg(statusToString(packet.status));
    
    if (m_handler) {
        m_handler->onMouseAbsResponse(packet.status);
    }
    emit mouseAbsResponseReceived(packet.status);
    
    return result;
}

ResponseResult SerialProtocol::processMouseRelResponse(const ParsedPacket& packet)
{
    ResponseResult result;
    result.responseCode = RESP_SEND_MOUSE_REL;
    result.success = isSuccess(packet.status);
    result.description = QString("Relative mouse response: %1").arg(statusToString(packet.status));
    
    if (m_handler) {
        m_handler->onMouseRelResponse(packet.status);
    }
    emit mouseRelResponseReceived(packet.status);
    
    return result;
}

ResponseResult SerialProtocol::processGetParamConfigResponse(const ParsedPacket& packet)
{
    ResponseResult result;
    result.responseCode = RESP_GET_PARA_CFG;
    result.success = true;
    result.description = "Parameter configuration response";
    
    if (packet.rawPacket.size() >= 12) {
        // Extract baudrate from bytes 8-11 (big endian)
        result.baudrate = (static_cast<uint8_t>(packet.rawPacket[8]) << 24) |
                          (static_cast<uint8_t>(packet.rawPacket[9]) << 16) |
                          (static_cast<uint8_t>(packet.rawPacket[10]) << 8) |
                          static_cast<uint8_t>(packet.rawPacket[11]);
        
        // Mode is at byte 5
        if (packet.rawPacket.size() >= 6) {
            result.mode = static_cast<uint8_t>(packet.rawPacket[5]);
        }
        
        qCDebug(log_core_serial) << "Param config: baudrate=" << result.baudrate 
                                     << "mode=0x" << QString::number(result.mode, 16);
        
        if (m_handler) {
            m_handler->onGetParamConfigResponse(result.baudrate, result.mode);
        }
        emit paramConfigReceived(result.baudrate, result.mode);
    } else {
        result.success = false;
        result.description = QString("Incomplete param config response: %1 bytes").arg(packet.rawPacket.size());
        qCWarning(log_core_serial) << result.description;
    }
    
    return result;
}

ResponseResult SerialProtocol::processSetParamConfigResponse(const ParsedPacket& packet)
{
    ResponseResult result;
    result.responseCode = RESP_SET_PARA_CFG;
    result.success = isSuccess(packet.status);
    result.description = QString("Set param config response: %1").arg(statusToString(packet.status));
    
    if (m_handler) {
        m_handler->onSetParamConfigResponse(packet.status);
    }
    emit setParamConfigReceived(packet.status);
    
    return result;
}

ResponseResult SerialProtocol::processResetResponse(const ParsedPacket& packet)
{
    ResponseResult result;
    result.responseCode = RESP_RESET;
    result.success = isSuccess(packet.status);
    result.description = QString("Reset response: %1").arg(statusToString(packet.status));
    
    if (m_handler) {
        m_handler->onResetResponse(packet.status);
    }
    emit resetResponseReceived(packet.status);
    
    return result;
}

ResponseResult SerialProtocol::processUsbSwitchResponse(const ParsedPacket& packet)
{
    ResponseResult result;
    result.responseCode = RESP_USB_SWITCH;
    result.description = "USB switch status response";
    
    // Validate USB switch response format
    if (packet.rawPacket.size() >= 7 && 
        packet.rawPacket[0] == 0x57 && 
        static_cast<uint8_t>(packet.rawPacket[1]) == 0xAB &&
        packet.rawPacket[2] == 0x00 && 
        packet.rawPacket[4] == 0x01) {
        
        uint8_t usbStatus = static_cast<uint8_t>(packet.rawPacket[5]);
        if (usbStatus == 0x00) {
            result.success = true;
            result.usbToTarget = false;
            result.description = "USB pointing to HOST";
            qCInfo(log_core_serial) << result.description;
        } else if (usbStatus == 0x01) {
            result.success = true;
            result.usbToTarget = true;
            result.description = "USB pointing to TARGET";
            qCInfo(log_core_serial) << result.description;
        } else {
            result.description = QString("Unknown USB status: 0x%1").arg(usbStatus, 2, 16, QChar('0'));
            qCWarning(log_core_serial) << result.description;
        }
        
        if (result.success) {
            if (m_handler) {
                m_handler->onUsbSwitchResponse(result.usbToTarget);
            }
            emit usbSwitchStatusReceived(result.usbToTarget);
        }
    } else {
        result.description = QString("Invalid USB status response format");
        qCWarning(log_core_serial) << result.description << "Packet:" << packet.rawPacket.toHex(' ');
    }
    
    return result;
}

// ========== Status Interpretation ==========

QString SerialProtocol::statusToString(uint8_t status)
{
    switch (status) {
        case STATUS_SUCCESS:
            return "Success";
        case STATUS_ERR_TIMEOUT:
            return "Serial response timeout";
        case STATUS_ERR_HEADER:
            return "Packet header error";
        case STATUS_ERR_COMMAND:
            return "Command error";
        case STATUS_ERR_CHECKSUM:
            return "Checksum error";
        case STATUS_ERR_PARAMETER:
            return "Parameter error";
        case STATUS_ERR_EXECUTE:
            return "Execution error";
        default:
            return QString("Unknown status (0x%1)").arg(status, 2, 16, QChar('0'));
    }
}

QString SerialProtocol::commandToString(uint8_t code)
{
    // Remove response bit if present
    uint8_t baseCode = code & ~RESPONSE_BIT;
    QString suffix = (code & RESPONSE_BIT) ? " (Response)" : "";
    
    switch (baseCode) {
        case SerialProtocolConstants::CMD_GET_INFO:
            return "GET_INFO" + suffix;
        case SerialProtocolConstants::CMD_SEND_KB_GENERAL:
            return "SEND_KB_GENERAL" + suffix;
        case SerialProtocolConstants::CMD_SEND_MOUSE_ABS:
            return "SEND_MOUSE_ABS" + suffix;
        case SerialProtocolConstants::CMD_SEND_MOUSE_REL:
            return "SEND_MOUSE_REL" + suffix;
        case SerialProtocolConstants::CMD_GET_PARA_CFG:
            return "GET_PARA_CFG" + suffix;
        case SerialProtocolConstants::CMD_SET_PARA_CFG:
            return "SET_PARA_CFG" + suffix;
        case SerialProtocolConstants::CMD_SET_USB_STRING:
            return "SET_USB_STRING" + suffix;
        case SerialProtocolConstants::CMD_SET_DEFAULT_CFG:
            return "SET_DEFAULT_CFG" + suffix;
        case SerialProtocolConstants::CMD_RESET:
            return "RESET" + suffix;
        case SerialProtocolConstants::CMD_USB_SWITCH:
            return "USB_SWITCH" + suffix;
        default:
            return QString("UNKNOWN_CMD(0x%1)%2").arg(code, 2, 16, QChar('0')).arg(suffix);
    }
}

bool SerialProtocol::isSuccess(uint8_t status)
{
    return status == STATUS_SUCCESS;
}

bool SerialProtocol::isResponse(uint8_t code)
{
    return (code & RESPONSE_BIT) != 0;
}
