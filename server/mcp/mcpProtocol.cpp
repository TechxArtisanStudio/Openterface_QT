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

#include "mcpProtocol.h"
#include <QDebug>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(log_server_mcp_protocol, "opf.server.mcp.protocol")

bool McpProtocol::parseRequest(const QByteArray& line, Request& out) {
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(line, &parseError);

    if (parseError.error != QJsonParseError::NoError) {
        qCWarning(log_server_mcp_protocol) << "JSON parse error:" << parseError.errorString();
        return false;
    }

    if (!doc.isObject()) {
        qCWarning(log_server_mcp_protocol) << "JSON document is not an object";
        return false;
    }

    QJsonObject obj = doc.object();

    // Validate jsonrpc field
    out.jsonrpc = obj.value("jsonrpc").toString();
    if (out.jsonrpc != JSONRPC_VERSION) {
        qCWarning(log_server_mcp_protocol) << "Invalid jsonrpc version:" << out.jsonrpc;
        return false;
    }

    // Extract method
    out.method = obj.value("method").toString();
    if (out.method.isEmpty()) {
        qCWarning(log_server_mcp_protocol) << "Missing method field";
        return false;
    }

    // Extract params (optional)
    out.params = obj.value("params").toObject();

    // Extract id — present for requests, absent for notifications
    out.isNotification = !obj.contains("id");
    if (!out.isNotification) {
        out.id = obj.value("id").toVariant();
    } else {
        out.id = QVariant();
    }

    return true;
}

QJsonObject McpProtocol::buildResult(const QVariant& id, const QJsonValue& result) {
    QJsonObject obj;
    obj["jsonrpc"] = JSONRPC_VERSION;
    obj["id"] = QJsonValue::fromVariant(id);
    obj["result"] = result;
    return obj;
}

QJsonObject McpProtocol::buildError(const QVariant& id, int code, const QString& message, const QJsonValue& data) {
    QJsonObject errorObj;
    errorObj["code"] = code;
    errorObj["message"] = message;
    if (!data.isNull()) {
        errorObj["data"] = data;
    }

    QJsonObject obj;
    obj["jsonrpc"] = JSONRPC_VERSION;
    obj["id"] = QJsonValue::fromVariant(id);
    obj["error"] = errorObj;
    return obj;
}

QJsonObject McpProtocol::buildInitializeResult() {
    QJsonObject serverInfo;
    serverInfo["name"] = MCP_SERVER_NAME;
    serverInfo["version"] = MCP_SERVER_VERSION;

    QJsonObject capabilities;
    capabilities["tools"] = QJsonObject();  // Empty object indicates basic tool support

    QJsonObject result;
    result["protocolVersion"] = MCP_PROTOCOL_VERSION;
    result["serverInfo"] = serverInfo;
    result["capabilities"] = capabilities;

    return result;
}

QByteArray McpProtocol::serialize(const QJsonObject& obj) {
    QJsonDocument doc(obj);
    return doc.toJson(QJsonDocument::Compact) + "\n";
}

QJsonObject McpProtocol::textContent(const QString& text) {
    QJsonObject content;
    content["type"] = "text";
    content["text"] = text;
    return content;
}

QJsonObject McpProtocol::imageContent(const QByteArray& base64Data, const QString& mimeType) {
    QJsonObject content;
    content["type"] = "image";
    content["data"] = QString::fromLatin1(base64Data);
    content["mimeType"] = mimeType;
    return content;
}

QJsonObject McpProtocol::toolResult(const QJsonArray& content) {
    QJsonObject result;
    result["content"] = content;
    return result;
}

QJsonObject McpProtocol::toolError(const QString& message) {
    QJsonObject content;
    content["type"] = "text";
    content["text"] = message;

    QJsonObject result;
    result["content"] = QJsonArray{ content };
    result["isError"] = true;
    return result;
}
