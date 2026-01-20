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

#ifndef SERIALPROTOCOL_H
#define SERIALPROTOCOL_H

#include <QObject>
#include <QByteArray>
#include <QString>
#include <QLoggingCategory>
#include <functional>
#include <cstdint>

Q_DECLARE_LOGGING_CATEGORY(log_core_serial)

/**
 * @brief Protocol constants for CH9329/CH32V208 serial communication
 */
namespace SerialProtocolConstants {
    // Packet structure constants
    constexpr uint8_t HEADER_BYTE_1 = 0x57;
    constexpr uint8_t HEADER_BYTE_2 = 0xAB;
    constexpr int HEADER_SIZE = 2;
    constexpr int MIN_PACKET_SIZE = 6;  // header(2) + addr(1) + cmd(1) + len(1) + checksum(1)
    
    // Command codes (without response bit)
    constexpr uint8_t CMD_GET_INFO = 0x01;
    constexpr uint8_t CMD_SEND_KB_GENERAL = 0x02;
    constexpr uint8_t CMD_SEND_MOUSE_ABS = 0x04;
    constexpr uint8_t CMD_SEND_MOUSE_REL = 0x05;
    constexpr uint8_t CMD_GET_PARA_CFG = 0x08;
    constexpr uint8_t CMD_SET_PARA_CFG = 0x09;
    constexpr uint8_t CMD_SET_USB_STRING = 0x0B;
    constexpr uint8_t CMD_SET_DEFAULT_CFG = 0x0C;
    constexpr uint8_t CMD_RESET = 0x0F;
    constexpr uint8_t CMD_USB_SWITCH = 0x17;
    
    // Response bit mask
    constexpr uint8_t RESPONSE_BIT = 0x80;
    
    // Response codes
    constexpr uint8_t RESP_GET_INFO = CMD_GET_INFO | RESPONSE_BIT;           // 0x81
    constexpr uint8_t RESP_SEND_KB_GENERAL = CMD_SEND_KB_GENERAL | RESPONSE_BIT; // 0x82
    constexpr uint8_t RESP_SEND_MOUSE_ABS = CMD_SEND_MOUSE_ABS | RESPONSE_BIT;   // 0x84
    constexpr uint8_t RESP_SEND_MOUSE_REL = CMD_SEND_MOUSE_REL | RESPONSE_BIT;   // 0x85
    constexpr uint8_t RESP_GET_PARA_CFG = CMD_GET_PARA_CFG | RESPONSE_BIT;   // 0x88
    constexpr uint8_t RESP_SET_PARA_CFG = CMD_SET_PARA_CFG | RESPONSE_BIT;   // 0x89
    constexpr uint8_t RESP_RESET = CMD_RESET | RESPONSE_BIT;                 // 0x8F
    constexpr uint8_t RESP_USB_SWITCH = CMD_USB_SWITCH | RESPONSE_BIT;       // 0x97
    
    // Status codes
    constexpr uint8_t STATUS_SUCCESS = 0x00;
    constexpr uint8_t STATUS_ERR_TIMEOUT = 0xE1;
    constexpr uint8_t STATUS_ERR_HEADER = 0xE2;
    constexpr uint8_t STATUS_ERR_COMMAND = 0xE3;
    constexpr uint8_t STATUS_ERR_CHECKSUM = 0xE4;
    constexpr uint8_t STATUS_ERR_PARAMETER = 0xE5;
    constexpr uint8_t STATUS_ERR_EXECUTE = 0xE6;
}

/**
 * @brief Result of parsing a serial packet
 */
struct ParsedPacket {
    bool valid = false;
    uint8_t commandCode = 0;
    uint8_t responseCode = 0;
    uint8_t status = 0;
    uint8_t payloadLength = 0;
    QByteArray payload;
    QByteArray rawPacket;
    QString errorMessage;
};

/**
 * @brief Result of processing a response
 */
struct ResponseResult {
    bool success = false;
    uint8_t responseCode = 0;
    QString description;
    
    // Response-specific data
    bool targetUsbConnected = false;
    bool numLockState = false;
    bool capsLockState = false;
    bool scrollLockState = false;
    int baudrate = 0;
    uint8_t mode = 0;
    bool usbToTarget = false;  // For USB switch status
};

/**
 * @brief Callback interface for protocol response handling
 */
class IProtocolResponseHandler {
public:
    virtual ~IProtocolResponseHandler() = default;
    
    virtual void onGetInfoResponse(bool targetConnected, uint8_t indicators) = 0;
    virtual void onKeyboardResponse(uint8_t status) = 0;
    virtual void onMouseAbsResponse(uint8_t status) = 0;
    virtual void onMouseRelResponse(uint8_t status) = 0;
    virtual void onGetParamConfigResponse(int baudrate, uint8_t mode) = 0;
    virtual void onSetParamConfigResponse(uint8_t status) = 0;
    virtual void onResetResponse(uint8_t status) = 0;
    virtual void onUsbSwitchResponse(bool isToTarget) = 0;
    virtual void onUnknownResponse(const QByteArray& packet) = 0;
    virtual void onProtocolError(uint8_t status, const QByteArray& packet) = 0;
};

/**
 * @brief Serial protocol handler for CH9329/CH32V208 communication
 * 
 * This class handles:
 * - Packet building with header and checksum
 * - Packet parsing and validation
 * - Response code routing
 * - Status code interpretation
 */
class SerialProtocol : public QObject
{
    Q_OBJECT

public:
    explicit SerialProtocol(QObject *parent = nullptr);
    ~SerialProtocol() override = default;
    
    // ========== Packet Building ==========
    
    /**
     * @brief Build a complete packet with header and checksum
     * @param commandData The command data (without checksum)
     * @return Complete packet ready to send
     */
    static QByteArray buildPacket(const QByteArray& commandData);
    
    /**
     * @brief Calculate checksum for packet data
     * @param data The data to calculate checksum for
     * @return Checksum byte
     */
    static uint8_t calculateChecksum(const QByteArray& data);
    
    /**
     * @brief Verify packet checksum
     * @param packet Complete packet including checksum
     * @return true if checksum is valid
     */
    static bool verifyChecksum(const QByteArray& packet);
    
    // ========== Packet Parsing ==========
    
    /**
     * @brief Parse raw data into a structured packet
     * @param data Raw data from serial port
     * @return Parsed packet structure
     */
    static ParsedPacket parsePacket(const QByteArray& data);
    
    /**
     * @brief Validate packet header
     * @param data Raw packet data
     * @return true if header is valid (0x57 0xAB)
     */
    static bool validateHeader(const QByteArray& data);
    
    /**
     * @brief Extract packet size from raw data
     * @param data Raw data (must be at least 5 bytes)
     * @return Expected packet size, or -1 if invalid
     */
    static int extractPacketSize(const QByteArray& data);
    
    // ========== Response Processing ==========
    
    /**
     * @brief Process a parsed packet and extract response data
     * @param packet The parsed packet
     * @return Response result with extracted data
     */
    ResponseResult processResponse(const ParsedPacket& packet);
    
    /**
     * @brief Set response handler for callback-based processing
     * @param handler The handler to receive response callbacks
     */
    void setResponseHandler(IProtocolResponseHandler* handler);
    
    /**
     * @brief Process raw data and dispatch to handler
     * @param data Raw data from serial port
     * @return true if packet was processed successfully
     */
    bool processRawData(const QByteArray& data);
    
    // ========== Status Interpretation ==========
    
    /**
     * @brief Convert status code to human-readable string
     * @param status Status code byte
     * @return Human-readable status description
     */
    static QString statusToString(uint8_t status);
    
    /**
     * @brief Convert command code to human-readable name
     * @param code Command or response code
     * @return Human-readable command name
     */
    static QString commandToString(uint8_t code);
    
    /**
     * @brief Check if status indicates success
     * @param status Status code byte
     * @return true if status is success
     */
    static bool isSuccess(uint8_t status);
    
    /**
     * @brief Check if this is a response packet (has response bit set)
     * @param code Command/response code
     * @return true if this is a response
     */
    static bool isResponse(uint8_t code);
    
signals:
    // Response signals for signal-based processing
    void getInfoReceived(bool targetConnected, uint8_t indicators);
    void keyboardResponseReceived(uint8_t status);
    void mouseAbsResponseReceived(uint8_t status);
    void mouseRelResponseReceived(uint8_t status);
    void paramConfigReceived(int baudrate, uint8_t mode);
    void setParamConfigReceived(uint8_t status);
    void resetResponseReceived(uint8_t status);
    void usbSwitchStatusReceived(bool isToTarget);
    void unknownResponseReceived(const QByteArray& packet);
    void protocolError(uint8_t status, const QString& errorMessage);
    
private:
    IProtocolResponseHandler* m_handler = nullptr;
    
    // Internal response processing methods
    ResponseResult processGetInfoResponse(const ParsedPacket& packet);
    ResponseResult processKeyboardResponse(const ParsedPacket& packet);
    ResponseResult processMouseAbsResponse(const ParsedPacket& packet);
    ResponseResult processMouseRelResponse(const ParsedPacket& packet);
    ResponseResult processGetParamConfigResponse(const ParsedPacket& packet);
    ResponseResult processSetParamConfigResponse(const ParsedPacket& packet);
    ResponseResult processResetResponse(const ParsedPacket& packet);
    ResponseResult processUsbSwitchResponse(const ParsedPacket& packet);
};

#endif // SERIALPROTOCOL_H
