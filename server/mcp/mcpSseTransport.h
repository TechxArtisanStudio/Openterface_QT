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

#ifndef MCP_SSE_TRANSPORT_H
#define MCP_SSE_TRANSPORT_H

#include <QObject>
#include <QMap>
#include <QTimer>
#include <QUuid>
#include <QJsonObject>
#include <QJsonDocument>
#include <QHostAddress>
#include <QHttpServer>
#include <QHttpServerRequest>
#include <QHttpServerResponder>
#include <QHttpHeaders>
#include <QTcpServer>

#include <memory>

#include "mcpConstants.h"

class McpToolHandler;

/**
 * SSE (Server-Sent Events) transport for MCP server.
 *
 * Implements the MCP SSE transport protocol (2024-11-05):
 *   GET  /sse       — establish a persistent SSE stream; returns an
 *                     "endpoint" event with "?sessionId=<uuid>" so the
 *                     client can POST JSON-RPC requests.
 *   POST /messages  — accept a JSON-RPC request; the response is pushed
 *                     back through the corresponding SSE stream as an
 *                     "event: message" frame.
 *
 * Uses QHttpServer with chunked transfer encoding (writeBeginChunked /
 * writeChunk) to keep the SSE connection alive indefinitely.
 */
class McpSseTransport : public QObject {
    Q_OBJECT

public:
    explicit McpSseTransport(McpToolHandler* handler, QObject* parent = nullptr);
    ~McpSseTransport() override;

    /// Start listening on the given port. Returns true on success.
    bool start(quint16 port = MCP_SSE_DEFAULT_PORT,
               const QHostAddress& bindAddress = QHostAddress::Any);
    /// Stop the HTTP server and close all sessions.
    void stop();
    bool isRunning() const;
    quint16 port() const;
    int activeSessionCount() const;

signals:
    void sessionCreated(const QString& sessionId);
    void sessionDestroyed(const QString& sessionId);
    void transportError(const QString& errorMsg);

private:
    // -----------------------------------------------------------------------
    // Per-SSE-session state.  The responder is heap-allocated because it is
    // move-only and must outlive the route-handler call frame.
    // -----------------------------------------------------------------------
    struct Session {
        QString id;
        std::unique_ptr<QHttpServerResponder> responder;
        QTimer* timeoutTimer = nullptr;
        qint64 lastActivityMs = 0;

        ~Session();
    };

    // -----------------------------------------------------------------------
    // HTTP route handlers (called by QHttpServer routing)
    // The MVC router expects responder as a non-const lvalue reference; we
    // std::move() into heap storage from inside the handler.
    // -----------------------------------------------------------------------
    void handleSse(const QHttpServerRequest& request,
                   QHttpServerResponder& responder);
    void handleMessages(const QHttpServerRequest& request,
                        QHttpServerResponder& responder);

    // -----------------------------------------------------------------------
    // Internal helpers
    // -----------------------------------------------------------------------
    Session* createSession(QHttpServerResponder&& responder);
    void cleanupSession(const QString& sessionId);
    void refreshSession(const QString& sessionId);
    void sweepStaleSessions();
    void sendKeepalives();

    void processRequest(Session* session, const QByteArray& body);
    void sendSseEvent(Session* session,
                      const QString& eventType,
                      const QJsonObject& data);

    static void sendHttpError(QHttpServerResponder& responder,
                              QHttpServerResponder::StatusCode status,
                              const QByteArray& message);

    // -----------------------------------------------------------------------
    // Data members
    // -----------------------------------------------------------------------
    McpToolHandler* m_toolHandler;

    QHttpServer*    m_httpServer  = nullptr;
    QTcpServer*     m_tcpServer   = nullptr;

    QMap<QString, Session*> m_sessions;

    QTimer* m_keepaliveTimer = nullptr;
    QTimer* m_cleanupTimer   = nullptr;

    quint16      m_port        = 0;
    QHostAddress m_bindAddress;
    bool         m_running     = false;
};

#endif // MCP_SSE_TRANSPORT_H
