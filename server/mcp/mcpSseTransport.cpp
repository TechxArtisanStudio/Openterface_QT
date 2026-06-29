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

#include "mcpSseTransport.h"
#include "mcpProtocol.h"
#include "mcpToolHandler.h"

#include <QDebug>
#include <QHttpServerResponse>
#include <QJsonArray>
#include <QLoggingCategory>
#include <QTcpSocket>
#include <QUrlQuery>

#include <chrono>

Q_LOGGING_CATEGORY(log_server_mcp_sse, "opf.server.mcp.sse")

// ============================================================================
// Session
// ============================================================================

McpSseTransport::Session::~Session()
{
    // Resetting the unique_ptr closes the chunked stream (sends the
    // terminating zero-length chunk) before the session struct is destroyed.
    responder.reset();
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

    // --- HTTP server ---
    m_httpServer = new QHttpServer(this);

    // Register routes.  The `route()` template deduces ViewHandler from the
    // member-function pointer; `this` is passed as the context object.
    m_httpServer->route(QLatin1String(MCP_SSE_PATH_SSE),
                        QHttpServerRequest::Method::Get,
                        this,
                        &McpSseTransport::handleSse);

    m_httpServer->route(QLatin1String(MCP_SSE_PATH_MESSAGES),
                        QHttpServerRequest::Method::Post,
                        this,
                        &McpSseTransport::handleMessages);

    // Any path that does not match the two routes above gets a 404.
    m_httpServer->setMissingHandler(this,
        [this](const QHttpServerRequest& request, QHttpServerResponder& responder) {
            Q_UNUSED(request);
            sendHttpError(responder,
                          QHttpServerResponder::StatusCode::NotFound,
                          "Not Found");
        });

    // --- TCP server ---
    m_tcpServer = new QTcpServer(this);
    if (!m_tcpServer->listen(bindAddress, port)) {
        qCCritical(log_server_mcp_sse)
            << "Cannot bind to" << bindAddress << ":" << port
            << "—" << m_tcpServer->errorString();
        emit transportError(
            QString("SSE: cannot bind to port %1: %2")
                .arg(port).arg(m_tcpServer->errorString()));
        delete m_httpServer;  m_httpServer = nullptr;
        delete m_tcpServer;   m_tcpServer  = nullptr;
        return false;
    }

    if (!m_httpServer->bind(m_tcpServer)) {
        qCCritical(log_server_mcp_sse) << "QHttpServer::bind() failed";
        emit transportError("SSE: QHttpServer::bind() failed");
        m_tcpServer->close();
        delete m_httpServer;  m_httpServer = nullptr;
        delete m_tcpServer;   m_tcpServer  = nullptr;
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
    if (!m_running && !m_httpServer && !m_tcpServer)
        return;

    // Stop timers first so they don't fire during teardown.
    if (m_keepaliveTimer) { m_keepaliveTimer->stop(); delete m_keepaliveTimer; m_keepaliveTimer = nullptr; }
    if (m_cleanupTimer)   { m_cleanupTimer->stop();   delete m_cleanupTimer;   m_cleanupTimer   = nullptr; }

    // Close every active session (resets the unique_ptr → ends chunked stream).
    for (auto it = m_sessions.begin(); it != m_sessions.end(); ++it) {
        Session* s = it.value();
        const QString id = s->id;
        delete s;
        emit sessionDestroyed(id);
    }
    m_sessions.clear();

    // QTcpServer is owned by QHttpServer after bind(); deleting the HTTP
    // server also cleans up the TCP server.
    if (m_httpServer) {
        delete m_httpServer;   // also deletes the bound QTcpServer
        m_httpServer = nullptr;
        m_tcpServer  = nullptr;
    } else if (m_tcpServer) {
        m_tcpServer->close();
        delete m_tcpServer;
        m_tcpServer = nullptr;
    }

    m_running = false;
    m_port    = 0;
    qCInfo(log_server_mcp_sse) << "SSE transport stopped";
}

bool     McpSseTransport::isRunning()         const { return m_running; }
quint16  McpSseTransport::port()              const { return m_port; }
int      McpSseTransport::activeSessionCount() const { return m_sessions.size(); }

// ============================================================================
// GET /sse  —  establish SSE stream
// ============================================================================

void McpSseTransport::handleSse(const QHttpServerRequest& request,
                                QHttpServerResponder& responder)
{
    Q_UNUSED(request);
    qCInfo(log_server_mcp_sse) << "GET /sse from"
                                << request.remoteAddress().toString();

    if (m_sessions.size() >= MCP_SSE_MAX_SESSIONS) {
        qCWarning(log_server_mcp_sse)
            << "Max sessions (" << MCP_SSE_MAX_SESSIONS << ") reached — rejecting";
        sendHttpError(responder,
                      QHttpServerResponder::StatusCode::ServiceUnavailable,
                      "Too many SSE sessions");
        return;
    }

    // Build HTTP response headers.
    QHttpHeaders headers;
    headers.append(QHttpHeaders::WellKnownHeader::ContentType,
                   QLatin1String(MCP_SSE_CONTENT_TYPE));
    headers.append(QHttpHeaders::WellKnownHeader::CacheControl,  "no-cache");
    headers.append(QHttpHeaders::WellKnownHeader::Connection,    "keep-alive");
    // Prevent reverse-proxy (nginx, etc.) from buffering the SSE stream.
    headers.append(QByteArrayLiteral("X-Accel-Buffering"),       "no");

    // Begin chunked transfer — this flushes the HTTP 200 + headers
    // immediately.  The connection stays open until writeEndChunked()
    // is called or the responder is destroyed.
    responder.writeBeginChunked(headers, QHttpServerResponder::StatusCode::Ok);

    // Move the responder into a new session BEFORE the local (moved-from)
    // copy goes out of scope when this function returns.
    Session* session = createSession(std::move(responder));
    if (!session) {
        qCCritical(log_server_mcp_sse) << "Failed to create session";
        return;  // connection will be closed when responder is destroyed
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

void McpSseTransport::handleMessages(const QHttpServerRequest& request,
                                     QHttpServerResponder& responder)
{
    // --- Extract sessionId from query string ---
    QUrlQuery query(request.url());
    QString sessionId = query.queryItemValue("sessionId");
    if (sessionId.isEmpty()) {
        qCWarning(log_server_mcp_sse) << "POST /messages without sessionId";
        sendHttpError(responder,
                      QHttpServerResponder::StatusCode::BadRequest,
                      "Missing sessionId query parameter");
        return;
    }

    Session* session = m_sessions.value(sessionId, nullptr);
    if (!session) {
        qCWarning(log_server_mcp_sse) << "Unknown sessionId:" << sessionId;
        sendHttpError(responder,
                      QHttpServerResponder::StatusCode::NotFound,
                      "Unknown or expired session");
        return;
    }

    // Bump the activity timestamp and reset the idle timer.
    refreshSession(sessionId);

    // --- Send an immediate HTTP 202 Accepted so the client knows we
    //     received the request.  The actual JSON-RPC response is sent
    //     asynchronously via the SSE stream. ---
    QHttpHeaders ackHeaders;
    ackHeaders.append(QHttpHeaders::WellKnownHeader::ContentType, "application/json");
    responder.write(QByteArrayLiteral("{\"accepted\":true}"),
                    ackHeaders,
                    QHttpServerResponder::StatusCode::Accepted);

    // --- Process the JSON-RPC request and push the response via SSE. ---
    processRequest(session, request.body());
}

// ============================================================================
// Session helpers
// ============================================================================

McpSseTransport::Session*
McpSseTransport::createSession(QHttpServerResponder&& responder)
{
    // Pre-generate the session ID so we can include it in the first SSE
    // event (the "endpoint" event is sent by the caller after this returns).
    QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);

    auto* session   = new Session;
    session->id     = id;
    session->responder = std::make_unique<QHttpServerResponder>(std::move(responder));
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
    // Resetting the unique_ptr sends the final chunk and closes the stream.
    session->responder.reset();
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
        if (s->responder)
            s->responder->writeChunk(comment);
    }
}

// ============================================================================
// JSON-RPC dispatch
// ============================================================================

void McpSseTransport::processRequest(Session* session, const QByteArray& body)
{
    if (!session || !session->responder) return;

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
    if (!session || !session->responder) return;

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

    session->responder->writeChunk(frame);
    qCDebug(log_server_mcp_sse) << "SSE →" << session->id
                                 << "event=" << eventType
                                 << "bytes=" << frame.size();
}

// ============================================================================
// HTTP error response helper
// ============================================================================

void McpSseTransport::sendHttpError(QHttpServerResponder& responder,
                                    QHttpServerResponder::StatusCode status,
                                    const QByteArray& message)
{
    QJsonObject body;
    body["error"] = QString::fromUtf8(message);
    QByteArray json = QJsonDocument(body).toJson(QJsonDocument::Compact);

    QHttpHeaders headers;
    headers.append(QHttpHeaders::WellKnownHeader::ContentType, "application/json");
    responder.write(json, headers, status);
}
