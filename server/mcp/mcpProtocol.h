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

#ifndef MCP_PROTOCOL_H
#define MCP_PROTOCOL_H

#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>
#include <QByteArray>
#include <QString>
#include <QVariant>
#include "mcpConstants.h"

/**
 * MCP Protocol handler — JSON-RPC 2.0 parsing and serialization
 * for the Model Context Protocol.
 */
class McpProtocol {
public:
    /** Represents a parsed JSON-RPC 2.0 request. */
    struct Request {
        QString jsonrpc;        // Must be "2.0"
        QString method;         // e.g. "initialize", "tools/list", "tools/call"
        QJsonObject params;     // Method parameters
        QVariant id;            // Request ID (int or string); invalid for notifications
        bool isNotification;    // true if id is absent (no response expected)
    };

    /** Parse a single JSON-RPC line into a Request struct. */
    static bool parseRequest(const QByteArray& line, Request& out);

    /** Build a JSON-RPC 2.0 success response. */
    static QJsonObject buildResult(const QVariant& id, const QJsonValue& result);

    /** Build a JSON-RPC 2.0 error response. */
    static QJsonObject buildError(const QVariant& id, int code, const QString& message, const QJsonValue& data = QJsonValue());

    /** Build the "initialize" result payload. */
    static QJsonObject buildInitializeResult();

    /** Serialize a QJsonObject to a newline-delimited QByteArray for sending. */
    static QByteArray serialize(const QJsonObject& obj);

    // --- Content helpers for tool results ---

    /** Build a text content item. */
    static QJsonObject textContent(const QString& text);

    /** Build an image content item (base64). */
    static QJsonObject imageContent(const QByteArray& base64Data, const QString& mimeType = "image/jpeg");

    /** Build a complete tool-call result envelope. */
    static QJsonObject toolResult(const QJsonArray& content);

    /** Build a tool-call error result envelope. */
    static QJsonObject toolError(const QString& message);
};

#endif // MCP_PROTOCOL_H
