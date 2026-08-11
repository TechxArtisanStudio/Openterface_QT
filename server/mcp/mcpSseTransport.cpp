/*
* ========================================================================== *
*                                                                            *
*    This file is part of the Openterface Mini KVM App QT version            *
*                                                                            *
*    Copyright (C) 2024   <info@openterface.com>                             *
*                                                                            *
*    This program is free software: you can redistribute it and/or modify    *
*    it under the terms of the GNU General Public License as published by    *
*    the Free Software Foundation version 3.                                  *
*                                                                            *
*    This program is distributed in the hope that it will be useful, but     *
*    WITHOUT ANY WARRANTY; without even the implied warranty of              *
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU        *
*    General Public License for more details.                                *
*                                                                            *
*    You should have received a copy of the GNU General Public License       *
*    along with this program; if not, see <http://www.gnu.org/licenses/>.    *
*                                                                            *
* ========================================================================== *
*/

#include "mcpSseTransport.h"
#include "mcpProtocol.h"
#include "mcpToolHandler.h"

#include <QDebug>
#include <QJsonArray>
#include <QLoggingCategory>
#include <QUrlQuery>

#include <chrono>

Q_LOGGING_CATEGORY(log_server_mcp_sse, "opf.server.mcp.sse")

// ============================================================================
// Session
// ============================================================================

McpSseTransport::Session::~Session()
{
    if (socket) {
        socket->disconnectFromHost();
        socket->deleteLater();
        socket = nullptr;
    }
    if (timeoutTimer) {
        timeoutTimer->stop();
        delete timeoutTimer;
        timeoutTimer = nullptr;
    }
}

// ============================================================================
// Construction / Destruction
// ============================================================================

McpSseTransport::McpSseTransport(McpToolHandler* handler, QObject* parent)
    : QObject(parent)
    , m_toolHandler(handler)
{
}

McpSseTransport::~McpSseTransport()
{
    stop();
}

// ============================================================================
// Lifecycle
// ============================================================================

bool McpSseTransport::start(quint16 port, const QHostAddress& bindAddress)
{
    if (m_running) {
        qCWarning(log_server_mcp_sse) << "SSE transport already running on port" << m_port;
        return true;
    }

    // --- TCP server ---
    m_tcpServer = new QTcpServer(this);
    connect(m_tcpServer, &QTcpServer::newConnection,
            this, &McpSseTransport::onNewConnection);

    if (!m_tcpServer->listen(bindAddress, port)) {
        qCCritical(log_server_mcp_sse)
            << "Cannot bind to" << bindAddress << ":" << port
            << "—" << m_tcpServer->errorString();
        emit transportError(
            QString("SSE: cannot bind to port %1: %2")
                .arg(port).arg(m_tcpServer->errorString()));
        delete m_tcpServer;
        m_tcpServer = nullptr;
        return false;
    }

    m_port        = port;
    m_bindAddress = bindAddress;
    m_running     = true;

    // --- Keepalive timer — send SSE comment lines to prevent proxy/OS
    //     timeouts on long-lived connections. ---
    m_keepaliveTimer = new QTimer(this);
    connect(m_keepaliveTimer, &QTimer::timeout,
            this, &McpSseTransport::sendKeepalives);
    m_keepaliveTimer->start(MCP_SSE_KEEPALIVE_INTERVAL);

    // --- Cleanup timer — sweep sessions that have exceeded the idle
    //     timeout without receiving a POST /messages. ---
    m_cleanupTimer = new QTimer(this);
    connect(m_cleanupTimer, &QTimer::timeout,
            this, &McpSseTransport::sweepStaleSessions);
    m_cleanupTimer->start(MCP_SSE_CLEANUP_INTERVAL);

    qCInfo(log_server_mcp_sse)
        << "SSE transport listening on"
        << bindAddress.toString() << ":" << port;
    return true;
}

void McpSseTransport::stop()
{
    if (!m_running && !m_tcpServer)
        return;

    // Stop timers first so they don't fire during teardown.
    if (m_keepaliveTimer) { m_keepaliveTimer->stop(); delete m_keepaliveTimer; m_keepaliveTimer = nullptr; }
    if (m_cleanupTimer)   { m_cleanupTimer->stop();   delete m_cleanupTimer;   m_cleanupTimer   = nullptr; }

    // Close every active session.
    for (auto it = m_sessions.begin(); it != m_sessions.end(); ++it) {
        Session* s = it.value();
        const QString id = s->id;
        delete s;
        emit sessionDestroyed(id);
    }
    m_sessions.clear();
    m_readBuffers.clear();

    if (m_tcpServer) {
        m_tcpServer->close();
        delete m_tcpServer;
        m_tcpServer = nullptr;
    }

    m_running = false;
    m_port    = 0;
    qCInfo(log_server_mcp_sse) << "SSE transport stopped";
}

bool     McpSseTransport::isRunning()          const { return m_running; }
quint16  McpSseTransport::port()               const { return m_port; }
int      McpSseTransport::activeSessionCount() const { return m_sessions.size(); }

// ============================================================================
// TCP connection handling
// ============================================================================

void McpSseTransport::onNewConnection()
{
    while (m_tcpServer && m_tcpServer->hasPendingConnections()) {
        QTcpSocket* socket = m_tcpServer->nextPendingConnection();
        if (!socket) continue;

        // Initialize read buffer for this socket.
        m_readBuffers.insert(socket, QByteArray());

        connect(socket, &QTcpSocket::readyRead,
                this, &McpSseTransport::onReadyRead);
        connect(socket, &QTcpSocket::disconnected,
                this, &McpSseTransport::onSocketDisconnected);

        qCDebug(log_server_mcp_sse) << "New TCP connection from"
                                     << socket->peerAddress().toString();
    }
}

void McpSseTransport::onReadyRead()
{
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) return;

    // Accumulate data in the per-socket read buffer.
    auto bufIt = m_readBuffers.find(socket);
    if (bufIt == m_readBuffers.end()) return;

    bufIt.value().append(socket->readAll());

    QByteArray method, path, body;
    QMap<QByteArray, QByteArray> headers;

    if (parseHttpRequest(socket, bufIt.value(), method, path, headers, body)) {
        qCInfo(log_server_mcp_sse) << method << path
                                    << "from" << socket->peerAddress().toString();

        // Determine which endpoint was requested.
        // Extract path without query string for routing.
        int queryPos = path.indexOf('?');
        QByteArray routePath = (queryPos >= 0) ? path.left(queryPos) : path;

        if (method == "GET" && routePath == MCP_SSE_PATH_SSE) {
            handleSseRequest(socket);
        } else if (method == "POST" && routePath == MCP_SSE_PATH_MESSAGES) {
            // Extract sessionId from query string.
            QUrlQuery query;
            if (queryPos >= 0)
                query.setQuery(path.mid(queryPos + 1));
            QString sessionId = query.queryItemValue("sessionId");

            if (sessionId.isEmpty()) {
                qCWarning(log_server_mcp_sse) << "POST /messages without sessionId";
                sendHttpError(socket, 400, "Bad Request",
                              "Missing sessionId query parameter");
                socket->disconnectFromHost();
            } else {
                handleMessagesRequest(socket, sessionId, body);
            }
        } else {
            sendHttpError(socket, 404, "Not Found", "Not Found");
            socket->disconnectFromHost();
        }
    }
}

void McpSseTransport::onSocketDisconnected()
{
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) return;

    // Remove from read buffers.
    m_readBuffers.remove(socket);

    // Find and clean up the session that owns this socket.
    QString sessionIdToRemove;
    for (auto it = m_sessions.constBegin(); it != m_sessions.constEnd(); ++it) {
        if (it.value()->socket == socket) {
            sessionIdToRemove = it.key();
            break;
        }
    }

    if (!sessionIdToRemove.isEmpty()) {
        qCInfo(log_server_mcp_sse) << "Client disconnected, cleaning session:"
                                    << sessionIdToRemove;
        cleanupSession(sessionIdToRemove);
    }

    socket->deleteLater();
}

// ============================================================================
// HTTP request parser
// ============================================================================

bool McpSseTransport::parseHttpRequest(QTcpSocket* /*socket*/,
                                       QByteArray& buffer,
                                       QByteArray& method,
                                       QByteArray& path,
                                       QMap<QByteArray, QByteArray>& headers,
                                       QByteArray& body)
{
    // Look for end of headers (\r\n\r\n).
    int headerEnd = buffer.indexOf("\r\n\r\n");
    if (headerEnd < 0) return false;  // incomplete — wait for more data

    // Parse request line: "GET /path HTTP/1.1\r\n"
    int firstLineEnd = buffer.indexOf("\r\n");
    QByteArray requestLine = buffer.left(firstLineEnd);
    QList<QByteArray> parts = requestLine.split(' ');
    if (parts.size() < 2) return false;

    method = parts[0].toUpper();
    path   = parts[1];

    // Parse headers.
    headers.clear();
    int pos = firstLineEnd + 2;  // skip first \r\n
    while (pos < headerEnd) {
        int lineEnd = buffer.indexOf("\r\n", pos);
        if (lineEnd < 0 || lineEnd > headerEnd) break;

        QByteArray line = buffer.mid(pos, lineEnd - pos);
        int colonPos = line.indexOf(':');
        if (colonPos > 0) {
            QByteArray key   = line.left(colonPos).trimmed();
            QByteArray value = line.mid(colonPos + 1).trimmed();
            headers.insert(key.toLower(), value);
        }
        pos = lineEnd + 2;
    }

    int bodyStart = headerEnd + 4;  // skip \r\n\r\n

    // For POST requests, read Content-Length bytes of body.
    qint64 contentLength = 0;
    if (headers.contains("content-length")) {
        contentLength = headers["content-length"].toLongLong();
    }

    qint64 availableBody = buffer.size() - bodyStart;
    if (availableBody < contentLength) {
        return false;  // incomplete body — wait for more data
    }

    if (contentLength > 0) {
        body = buffer.mid(bodyStart, contentLength);
    } else {
        body.clear();
    }

    // Consume the parsed request from the buffer.
    buffer.remove(0, bodyStart + contentLength);

    return true;
}

// ============================================================================
// HTTP response helpers
// ============================================================================

void McpSseTransport::sendHttpResponse(QTcpSocket* socket,
                                       int statusCode,
                                       const QByteArray& statusText,
                                       const QList<QPair<QByteArray, QByteArray>>& extraHeaders,
                                       const QByteArray& body)
{
    if (!socket || socket->state() != QAbstractSocket::ConnectedState) return;

    QByteArray response;
    response.append("HTTP/1.1 ");
    response.append(QByteArray::number(statusCode));
    response.append(" ");
    response.append(statusText);
    response.append("\r\n");

    for (const auto& h : extraHeaders) {
        response.append(h.first);
        response.append(": ");
        response.append(h.second);
        response.append("\r\n");
    }

    response.append("Content-Length: ");
    response.append(QByteArray::number(body.size()));
    response.append("\r\n");
    response.append("\r\n");
    response.append(body);

    socket->write(response);
    socket->flush();
}

void McpSseTransport::sendHttpError(QTcpSocket* socket,
                                    int statusCode,
                                    const QByteArray& statusText,
                                    const QByteArray& message)
{
    QJsonObject bodyObj;
    bodyObj["error"] = QString::fromUtf8(message);
    QByteArray json = QJsonDocument(bodyObj).toJson(QJsonDocument::Compact);

    QList<QPair<QByteArray, QByteArray>> headers;
    headers.append(qMakePair(QByteArrayLiteral("Content-Type"),
                             QByteArrayLiteral("application/json")));
    sendHttpResponse(socket, statusCode, statusText, headers, json);
}

// ============================================================================
// GET /sse  —  establish SSE stream
// ============================================================================

void McpSseTransport::handleSseRequest(QTcpSocket* socket)
{
    if (m_sessions.size() >= MCP_SSE_MAX_SESSIONS) {
        qCWarning(log_server_mcp_sse)
            << "Max sessions (" << MCP_SSE_MAX_SESSIONS << ") reached — rejecting";
        sendHttpError(socket, 503, "Service Unavailable", "Too many SSE sessions");
        socket->disconnectFromHost();
        return;
    }

    // Write SSE response headers directly (no Content-Length — connection stays open).
    QByteArray responseHeaders;
    responseHeaders.append("HTTP/1.1 200 OK\r\n");
    responseHeaders.append("Content-Type: ");
    responseHeaders.append(MCP_SSE_CONTENT_TYPE);
    responseHeaders.append("\r\n");
    responseHeaders.append("Cache-Control: no-cache\r\n");
    responseHeaders.append("Connection: keep-alive\r\n");
    // Prevent reverse-proxy (nginx, etc.) from buffering the SSE stream.
    responseHeaders.append("X-Accel-Buffering: no\r\n");
    responseHeaders.append("\r\n");

    socket->write(responseHeaders);
    socket->flush();

    // Create session with this socket (takes ownership of streaming).
    Session* session = createSession(socket);
    if (!session) {
        qCCritical(log_server_mcp_sse) << "Failed to create session";
        socket->disconnectFromHost();
        return;
    }

    // Send the "endpoint" event — tells the client the URL to POST to.
    QJsonObject endpointData;
    endpointData["endpoint"] = QString("%1?sessionId=%2")
                                   .arg(QLatin1String(MCP_SSE_PATH_MESSAGES))
                                   .arg(session->id);
    sendSseEvent(session, QStringLiteral("endpoint"), endpointData);

    qCInfo(log_server_mcp_sse) << "SSE session created:" << session->id;
    emit sessionCreated(session->id);
}

// ============================================================================
// POST /messages  —  receive JSON-RPC, reply via SSE
// ============================================================================

void McpSseTransport::handleMessagesRequest(QTcpSocket* socket,
                                            const QString& sessionId,
                                            const QByteArray& body)
{
    Session* session = m_sessions.value(sessionId, nullptr);
    if (!session) {
        qCWarning(log_server_mcp_sse) << "Unknown sessionId:" << sessionId;
        sendHttpError(socket, 404, "Not Found", "Unknown or expired session");
        socket->disconnectFromHost();
        return;
    }

    // Bump the activity timestamp and reset the idle timer.
    refreshSession(sessionId);

    // Send HTTP 202 Accepted on the POST socket.
    QList<QPair<QByteArray, QByteArray>> ackHeaders;
    ackHeaders.append(qMakePair(QByteArrayLiteral("Content-Type"),
                                QByteArrayLiteral("application/json")));
    sendHttpResponse(socket, 202, "Accepted", ackHeaders,
                     QByteArrayLiteral("{\"accepted\":true}"));
    socket->disconnectFromHost();

    // Process the JSON-RPC request and push the response via SSE.
    processRequest(session, body);
}

// ============================================================================
// Session helpers
// ============================================================================

McpSseTransport::Session*
McpSseTransport::createSession(QTcpSocket* socket)
{
    QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);

    auto* session   = new Session;
    session->id     = id;
    session->socket = socket;
    session->lastActivityMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();

    // Per-session idle timeout — auto-removes the session if no POST
    // arrives within MCP_SSE_SESSION_TIMEOUT_MS.
    session->timeoutTimer = new QTimer(this);
    session->timeoutTimer->setSingleShot(true);
    session->timeoutTimer->setInterval(MCP_SSE_SESSION_TIMEOUT_MS);
    connect(session->timeoutTimer, &QTimer::timeout, this, [this, id]() {
        qCInfo(log_server_mcp_sse) << "Session timed out:" << id;
        cleanupSession(id);
    });
    session->timeoutTimer->start();

    m_sessions.insert(id, session);
    return session;
}

void McpSseTransport::cleanupSession(const QString& sessionId)
{
    Session* session = m_sessions.take(sessionId);
    if (!session) return;

    qCInfo(log_server_mcp_sse) << "Cleaning up session:" << sessionId;
    // Disconnect the SSE socket (but don't delete it — onSocketDisconnected
    // will handle cleanup via deleteLater).
    if (session->socket) {
        m_readBuffers.remove(session->socket);
        session->socket->disconnectFromHost();
        session->socket = nullptr;  // prevent double-close in destructor
    }
    delete session;
    emit sessionDestroyed(sessionId);
}

void McpSseTransport::refreshSession(const QString& sessionId)
{
    Session* session = m_sessions.value(sessionId, nullptr);
    if (!session) return;

    session->lastActivityMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();

    if (session->timeoutTimer)
        session->timeoutTimer->start();  // restart the single-shot timer
}

void McpSseTransport::sweepStaleSessions()
{
    const qint64 now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();

    QStringList expired;
    for (auto it = m_sessions.constBegin(); it != m_sessions.constEnd(); ++it) {
        Session* s = it.value();
        if ((now - s->lastActivityMs) > MCP_SSE_SESSION_TIMEOUT_MS)
            expired.append(s->id);
    }

    for (const QString& id : expired)
        cleanupSession(id);
}

void McpSseTransport::sendKeepalives()
{
    // SSE comment line — ignored by the client but keeps the TCP
    // connection alive through firewalls and reverse proxies.
    static const QByteArray comment(": keepalive\n\n");

    for (auto it = m_sessions.begin(); it != m_sessions.end(); ++it) {
        Session* s = it.value();
        if (s->socket && s->socket->state() == QAbstractSocket::ConnectedState)
            s->socket->write(comment);
    }
}

// ============================================================================
// JSON-RPC dispatch
// ============================================================================

void McpSseTransport::processRequest(Session* session, const QByteArray& body)
{
    if (!session || !session->socket) return;

    McpProtocol::Request req;
    if (!McpProtocol::parseRequest(body, req)) {
        QJsonObject err = McpProtocol::buildError(
            QVariant(), JSONRPC_ERROR_PARSE_ERROR,
            QStringLiteral("Parse error: invalid JSON"));
        sendSseEvent(session, QStringLiteral("message"), err);
        return;
    }

    // Notifications have no id — no response is expected.
    if (req.isNotification) {
        qCDebug(log_server_mcp_sse) << "Notification:" << req.method;
        if (req.method == QLatin1String(MCP_METHOD_INITIALIZED))
            qCInfo(log_server_mcp_sse) << "Client initialized (SSE)";
        return;
    }

    QJsonObject response;

    if (req.method == QLatin1String(MCP_METHOD_INITIALIZE)) {
        QJsonObject result = McpProtocol::buildInitializeResult();
        response = McpProtocol::buildResult(req.id, result);

    } else if (req.method == QLatin1String(MCP_METHOD_TOOLS_LIST)) {
        if (m_toolHandler) {
            QJsonObject result;
            result["tools"] = m_toolHandler->listTools();
            response = McpProtocol::buildResult(req.id, result);
        } else {
            response = McpProtocol::buildError(
                req.id, JSONRPC_ERROR_INTERNAL_ERROR,
                QStringLiteral("Tool handler not initialized"));
        }

    } else if (req.method == QLatin1String(MCP_METHOD_TOOLS_CALL)) {
        if (!m_toolHandler) {
            response = McpProtocol::buildError(
                req.id, JSONRPC_ERROR_INTERNAL_ERROR,
                QStringLiteral("Tool handler not initialized"));
        } else {
            QString toolName = req.params.value("name").toString();
            QJsonObject toolArgs = req.params.value("arguments").toObject();
            if (toolName.isEmpty()) {
                response = McpProtocol::buildError(
                    req.id, JSONRPC_ERROR_INVALID_PARAMS,
                    QStringLiteral("Missing tool name in 'name' field"));
            } else {
                QJsonObject toolResult = m_toolHandler->callTool(toolName, toolArgs);
                response = McpProtocol::buildResult(req.id, toolResult);
            }
        }

    } else if (req.method == QLatin1String(MCP_METHOD_PING)) {
        QJsonObject result;
        result["status"] = QStringLiteral("ok");
        response = McpProtocol::buildResult(req.id, result);

    } else {
        response = McpProtocol::buildError(
            req.id, JSONRPC_ERROR_METHOD_NOT_FOUND,
            QStringLiteral("Method not found: ") + req.method);
    }

    sendSseEvent(session, QStringLiteral("message"), response);
}

// ============================================================================
// SSE frame formatting
// ============================================================================

void McpSseTransport::sendSseEvent(Session* session,
                                   const QString& eventType,
                                   const QJsonObject& data)
{
    if (!session || !session->socket) return;
    if (session->socket->state() != QAbstractSocket::ConnectedState) return;

    // SSE wire format:
    //   event: <eventType>\n
    //   data: <json>\n
    //   \n
    QByteArray json = McpProtocol::serialize(data);
    QByteArray frame;
    frame.reserve(eventType.size() + json.size() + 32);
    frame.append("event: ");
    frame.append(eventType.toUtf8());
    frame.append('\n');
    frame.append("data: ");
    frame.append(json);
    frame.append('\n');
    frame.append('\n');

    session->socket->write(frame);
    session->socket->flush();
    qCDebug(log_server_mcp_sse) << "SSE →" << session->id
                                 << "event=" << eventType
                                 << "bytes=" << frame.size();
}
