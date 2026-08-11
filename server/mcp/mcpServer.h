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

#ifndef MCP_SERVER_H
#define MCP_SERVER_H

#include <QObject>
#include <QTimer>
#include <QFile>
#include <QString>
#include <QHostAddress>

class McpProtocol;
class McpToolHandler;
class McpSseTransport;
class CameraManager;
class ScriptRunner;
class ScriptExecutor;
class ASTNode;

class McpServer : public QObject {
    Q_OBJECT

public:
    explicit McpServer(QObject *parent = nullptr);
    ~McpServer() override;

    // --- Lifecycle ---

    /** Stop listening and disconnect any client. */
    void stop();

    /** Whether the server is currently listening. */
    bool isRunning() const;

    // --- Dependency Injection ---

    /** Set the CameraManager (used for screen capture tools). */
    void setCameraManager(CameraManager* cameraManager);

    /** Set the ScriptRunner (used for script execution tools). */
    void setScriptRunner(ScriptRunner* scriptRunner);

    /** Set the ScriptExecutor (used for script execution tools). */
    void setScriptExecutor(ScriptExecutor* scriptExecutor);

    /** Inject a pre-built tool handler (optional — creates one internally if null). */
    void setToolHandler(McpToolHandler* handler);

    /** Provide access to the internal tool handler for additional configuration. */
    McpToolHandler* toolHandler() const;

    /**
     * Start listening on stdin/stdout.
     * @return true if started successfully, false on failure.
     */
    bool startStdio();

    // --- SSE Remote Transport ---

    /**
     * Start the SSE HTTP transport on the given port.
     * @param port TCP port to listen on (default 8080).
     * @param bindAddress Network interface to bind to (default: any).
     * @return true if started successfully, false on failure.
     */
    bool startSse(quint16 port = 8080,
                  const QHostAddress& bindAddress = QHostAddress::Any);

    /** Stop the SSE transport and close all sessions. */
    void stopSse();

    /** Whether the SSE transport is currently running. */
    bool isSseRunning() const;

    /** Number of active SSE sessions. */
    int sseSessionCount() const;

signals:
    /** Emitted when the server starts listening successfully. */
    void started();

    /** Emitted when the server is stopped. */
    void stopped();

    /** Emitted for informational/log messages. */
    void logMessage(const QString& message);

    /** Emitted when stdio mode is ready to accept requests. */
    void stdioReady();

    /** Emitted when SSE transport starts. */
    void sseStarted(quint16 port);

    /** Emitted when SSE transport stops. */
    void sseStopped();

private slots:
    void onStdinReadyRead();

private:
    // --- Stdio transport ---
    bool m_stdioMode = false;
    QTimer* m_stdinPollTimer = nullptr;
    QByteArray m_stdinBuffer;
    QFile* m_stdinFile = nullptr;    // stdin wrapped as QFile
    QFile* m_stdoutFile = nullptr;   // stdout wrapped as QFile

    // --- SSE transport ---
    McpSseTransport* m_sseTransport = nullptr;

    // --- Shared ---
    McpToolHandler* m_toolHandler = nullptr;
    bool m_ownsToolHandler = false;     // True if we created m_toolHandler ourselves

    // Pending dependencies — stored until the tool handler is created
    CameraManager*  m_pendingCameraManager = nullptr;
    ScriptRunner*   m_pendingScriptRunner = nullptr;
    ScriptExecutor* m_pendingScriptExecutor = nullptr;

    void applyPendingDependencies();

    /**
     * Handle a single JSON-RPC request and send the response to the given device.
     * Dispatches: initialize, notifications/initialized, tools/list, tools/call, ping
     */
    void handleMessage(const QString& jsonLine, QIODevice* device);

    /** Serialize and send a JSON response, appending a newline. */
    void sendResponse(const QJsonObject& response, QIODevice* device);
};

#endif // MCP_SERVER_H
