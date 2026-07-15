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

#ifndef MCP_SSE_TRANSPORT_H
#define MCP_SSE_TRANSPORT_H

#include <QObject>
#include <QMap>
#include <QTimer>
#include <QUuid>
#include <QJsonObject>
#include <QJsonDocument>
#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>

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
 * Uses raw QTcpServer + manual HTTP handling for Qt 6.5.3 compatibility.
 * This avoids QHttpServer's chunked-transfer APIs (writeBeginChunked /
 * writeChunk) which were only added in Qt 6.7.
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

private slots:
    void onNewConnection();
    void onReadyRead();
    void onSocketDisconnected();

private:
    // -----------------------------------------------------------------------
    // Per-SSE-session state.  The socket is owned by the session and closed
    // when the session is destroyed.
    // -----------------------------------------------------------------------
    struct Session {
        QString id;
        QTcpSocket* socket = nullptr;
        QTimer* timeoutTimer = nullptr;
        qint64 lastActivityMs = 0;

        ~Session();
    };

    // -----------------------------------------------------------------------
    // Internal helpers
    // -----------------------------------------------------------------------

    /// Parse a raw HTTP request from the buffer. Returns true when a full
    /// request (headers + body) has been received.
    bool parseHttpRequest(QTcpSocket* socket, QByteArray& buffer,
                          QByteArray& method, QByteArray& path,
                          QMap<QByteArray, QByteArray>& headers,
                          QByteArray& body);

    /// Send a complete HTTP response on the socket.
    void sendHttpResponse(QTcpSocket* socket,
                          int statusCode,
                          const QByteArray& statusText,
                          const QList<QPair<QByteArray, QByteArray>>& extraHeaders,
                          const QByteArray& body);

    /// Send an HTTP error response.
    void sendHttpError(QTcpSocket* socket,
                       int statusCode,
                       const QByteArray& statusText,
                       const QByteArray& message);

    /// Route a parsed HTTP request to the appropriate handler.
    void handleSseRequest(QTcpSocket* socket);
    void handleMessagesRequest(QTcpSocket* socket,
                               const QString& sessionId,
                               const QByteArray& body);

    Session* createSession(QTcpSocket* socket);
    void cleanupSession(const QString& sessionId);
    void refreshSession(const QString& sessionId);
    void sweepStaleSessions();
    void sendKeepalives();

    void processRequest(Session* session, const QByteArray& body);
    void sendSseEvent(Session* session,
                      const QString& eventType,
                      const QJsonObject& data);

    // -----------------------------------------------------------------------
    // Data members
    // -----------------------------------------------------------------------
    McpToolHandler* m_toolHandler;

    QTcpServer*     m_tcpServer   = nullptr;

    QMap<QString, Session*> m_sessions;

    /// Per-socket read buffer for accumulating partial HTTP requests.
    QMap<QTcpSocket*, QByteArray> m_readBuffers;

    QTimer* m_keepaliveTimer = nullptr;
    QTimer* m_cleanupTimer   = nullptr;

    quint16      m_port        = 0;
    QHostAddress m_bindAddress;
    bool         m_running     = false;
};

#endif // MCP_SSE_TRANSPORT_H
