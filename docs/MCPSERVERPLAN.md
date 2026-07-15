# MCP Server SSE Remote Transport Implementation Plan

## Overview

This document outlines the implementation plan for adding SSE (Server-Sent Events) remote transport to the existing MCP server. The server currently supports **stdio** and **Named Pipe** transports. This plan adds HTTP-based SSE remote access while maintaining compatibility with existing transports.

**Supported Transport Protocols:**
- **stdio** — Standard input/output, for local CLI integration (Claude Code, etc.)
- **Named Pipe (QLocalServer)** — Unix domain socket / Windows named pipe, for local IPC
- **SSE Remote (HTTP)** — Server-Sent Events over HTTP, for remote network access

## Architecture

The implementation follows a modular architecture where all transports share the same `McpToolHandler`:

```
┌──────────────────────────────────────────────────────────────────┐
│                       McpServer (Extended)                        │
│                                                                   │
│  ┌──────────────┐  ┌─────────────────┐  ┌─────────────────────┐ │
│  │   stdio      │  │  QLocalServer   │  │     QHttpServer     │ │
│  │  (stdin/out) │  │  (Named Pipe)   │  │  (SSE Remote)       │ │
│  │              │  │                 │  │                     │ │
│  │  CLI clients │  │  /tmp/          │  │  GET  /sse          │ │
│  │  (Claude     │  │  openterface-   │  │  POST /messages     │ │
│  │   Code, etc) │  │  mcp            │  │  (Port 8080)        │ │
│  └──────┬───────┘  └────────┬────────┘  └──────────┬──────────┘ │
│         │                   │                       │            │
│         └───────────────────┼───────────────────────┘            │
│                             │                                    │
│                   ┌─────────▼──────────┐                         │
│                   │   McpToolHandler   │                         │
│                   │   (Shared)         │                         │
│                   └────────────────────┘                         │
└──────────────────────────────────────────────────────────────────┘
```

## File Changes

### New Files

| File | Purpose |
|------|---------|
| `server/mcp/mcpSseTransport.h` | SSE transport class definition |
| `server/mcp/mcpSseTransport.cpp` | HTTP routing, session management, and SSE streaming |

### Modified Files

| File | Changes |
|------|---------|
| `CMakeLists.txt` | Add Qt6::HttpServer dependency |
| `cmake/SourceFiles.cmake` | Add new SSE transport files |
| `server/mcp/mcpServer.h` | Add SSE transport control methods |
| `server/mcp/mcpServer.cpp` | Implement SSE transport integration |
| `server/mcp/mcpConstants.h` | Add SSE-related constants (port, paths) |

---

## Technical Implementation Details

### 1. McpSseTransport Class — Full Definition

```cpp
#ifndef MCPSSETRANSPORT_H
#define MCPSSETRANSPORT_H

#include <QObject>
#include <QHttpServer>
#include <QHttpServerResponse>
#include <QMap>
#include <QTimer>
#include <QUuid>
#include <QJsonObject>
#include <QJsonDocument>

class McpToolHandler;

class McpSseTransport : public QObject {
    Q_OBJECT

public:
    explicit McpSseTransport(McpToolHandler* handler, QObject* parent = nullptr);
    ~McpSseTransport() override;

    /// Start listening on the given port. Returns false if port is busy.
    bool start(quint16 port = 8080, const QHostAddress& bindAddress = QHostAddress::LocalHost);
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
    // ── Session Model ──────────────────────────────────────────────
    //
    // Each SSE client holds a persistent HTTP connection (the SSE stream).
    // JSON-RPC requests arrive as POST /messages, and responses are pushed
    // back through the SSE stream. The session bridges these two channels.
    //
    // Key design decisions:
    //   - sseStream is a QHttpServerResponse* kept alive as long as the
    //     client stays connected. We write SSE frames to it incrementally
    //     via QHttpServerResponse::write() (available since Qt 6.4).
    //   - We detect client disconnect by connecting to
    //     QHttpServerResponse::destroyed() — when the TCP connection drops,
    //     Qt destroys the response object, and we clean up the session.
    //   - A per-session QTimer handles inactivity timeout.

    struct Session {
        QString id;
        bool initialized = false;          // set after successful "initialize" handshake
        QHttpServerResponse* sseStream = nullptr;  // persistent SSE connection
        QTimer* timeoutTimer = nullptr;    // fires on inactivity
        qint64 lastActivityMs = 0;         // epoch ms of last request
    };

    // ── HTTP Route Handlers ────────────────────────────────────────
    //
    // QHttpServer routing:
    //   m_httpServer->route("/sse", &McpSseTransport::handleSse);
    //   m_httpServer->route("/messages", &McpSseTransport::handleMessages);
    //
    // handleSse returns QHttpServerResponse with Content-Type: text/event-stream.
    // The response object is kept alive (not returned by value to be destroyed)
    // by storing its pointer in the Session struct.

    QHttpServerResponse handleSse(const QHttpServerRequest& request);
    QHttpServerResponse handleMessages(const QHttpServerRequest& request);

    // ── SSE Frame Construction ─────────────────────────────────────
    //
    // SSE wire format (each frame ends with \n\n):
    //   event: <event-type>\n
    //   id: <optional-id>\n
    //   data: <json-payload>\n
    //   \n
    //
    // We use two event types:
    //   "endpoint" — sent once on connection, tells client the POST URL
    //   "message"  — sent for each JSON-RPC response
    //
    // Multi-line data handling: if the JSON payload contains newlines
    // (it shouldn't since QJsonDocument::Compact is used, but defensively),
    // each line gets its own "data: " prefix per SSE spec.

    QByteArray formatSseFrame(const QString& eventType, const QByteArray& data) const;

    // ── JSON-RPC Processing ────────────────────────────────────────
    //
    // processRequest() reuses the same dispatch logic as stdio/pipe:
    //   1. Parse the JSON body
    //   2. Extract "method" field
    //   3. Route to initialize / tools/list / tools/call / ping
    //   4. Build the JSON-RPC response
    //   5. Return the response QJsonObject
    //
    // IMPORTANT: McpToolHandler::callTool() is synchronous and may block
    // (e.g., keyboard sequences with delays). For multi-client SSE, we
    // must NOT block the Qt event loop. Solution: wrap tool calls in
    // QtConcurrent::run() and deliver the result via QMetaObject::invokeMethod
    // back to the main thread when ready. This keeps the HTTP server
    // responsive while tools execute in worker threads.
    //
    // For Phase 2, we start with synchronous execution (single client works
    // fine). Phase 3 adds the async wrapper for multi-client safety.

    QJsonObject processRequest(const QJsonObject& request, Session* session);

    // ── Session Lifecycle ──────────────────────────────────────────
    //
    // Create:   called from handleSse() on new GET /sse connection
    // Refresh:  called on every POST /messages (reset timeout timer)
    // Destroy:  called when:
    //           a) QHttpServerResponse::destroyed fires (client disconnected)
    //           b) Inactivity timeout fires
    //           c) stop() is called (server shutdown)
    //
    // Memory management:
    //   - Session is heap-allocated, owned by m_sessions map
    //   - Cleanup always goes through cleanupSession() which:
    //     1. Removes from m_sessions map
    //     2. Stops and deletes timeoutTimer
    //     3. Emits sessionDestroyed signal
    //     4. Deletes the Session struct
    //   - We do NOT delete sseStream — Qt owns it and will destroy it
    //     when the HTTP response completes

    Session* createSession();
    void refreshSession(Session* session);
    void cleanupSession(const QString& sessionId);

    // ── Member Variables ───────────────────────────────────────────
    QMap<QString, Session*> m_sessions;
    McpToolHandler* m_toolHandler;
    QHttpServer* m_httpServer = nullptr;
    QTimer* m_cleanupTimer = nullptr;     // periodic sweep for stale sessions
    quint16 m_port = 0;
    QHostAddress m_bindAddress;
    bool m_running = false;

    static constexpr int MAX_SESSIONS = 16;           // hard cap
    static constexpr int SESSION_TIMEOUT_MS = 30 * 60 * 1000;  // 30 min
    static constexpr int CLEANUP_INTERVAL_MS = 60 * 1000;       // sweep every 60s
};

#endif // MCPSSETRANSPORT_H
```

### 2. SSE Streaming — Qt6 HttpServer Internals

#### How QHttpServerResponse Streaming Works

```cpp
// Qt6 HttpServer supports streaming responses via QHttpServerResponse::write().
// The response object remains alive and connected to the client until you
// stop writing or the client disconnects.
//
// Key API:
//   QHttpServerResponse resp(statusCode);
//   resp.write(data);         // append chunk to response body
//   resp.close();             // finalize and close the response
//
// For SSE, we:
//   1. Create a response with Content-Type: text/event-stream
//   2. Write the initial "endpoint" event
//   3. Keep the response pointer in the Session
//   4. On each POST /messages, process the request, then write() the
//      response back through the stored SSE stream pointer
//   5. NEVER call close() — the stream stays open indefinitely

// Pseudo-code for handleSse():
QHttpServerResponse McpSseTransport::handleSse(const QHttpServerRequest& request)
{
    Q_UNUSED(request);

    Session* session = createSession();
    if (!session) {
        return QHttpServerResponse(QHttpServerResponse::StatusCode::ServiceUnavailable);
    }

    // Build the SSE response with correct headers
    QHttpServerResponse response(QHttpServerResponse::StatusCode::Ok);
    response.setHeader("Content-Type", "text/event-stream");
    response.setHeader("Cache-Control", "no-cache");
    response.setHeader("Connection", "keep-alive");
    response.setHeader("Mcp-Session-Id", session->id.toUtf8());

    // Write the initial endpoint event
    QByteArray endpointEvent = formatSseFrame("endpoint",
        QString("/messages?sessionId=%1").arg(session->id).toUtf8());
    response.write(endpointEvent);

    // Store the response pointer for later writes
    // IMPORTANT: We need to keep this response alive.
    // QHttpServerResponse is returned by value from the route handler,
    // but we need a persistent pointer. Solution: use QHttpServerResponse
    // with a custom body or store the data channel.
    session->sseStream = /* ... see note below ... */;

    // Connect to destruction signal for cleanup
    connect(session->sseStream, &QObject::destroyed, this, [this, sessionId = session->id]() {
        cleanupSession(sessionId);
    });

    return response;
}
```

#### Critical Design Note: Persistent SSE Stream

`QHttpServer` route handlers return `QHttpServerResponse` by value. For SSE we need the response to stay alive and be writable after the handler returns. There are two approaches:

**Approach A — `QHttpServerResponse` with `QIODevice` body (Recommended):**
Create a `QBuffer` (or custom `QIODevice`) that we keep a pointer to. Write SSE frames to this buffer. `QHttpServer` reads from it and streams to the client. The buffer stays alive as long as we hold a reference.

```cpp
// Create a persistent buffer for this session
QBuffer* sseBuffer = new QBuffer(session);  // parented to session for cleanup
sseBuffer->open(QIODevice::WriteOnly);

// Build response that reads from this buffer
QHttpServerResponse response(sseBuffer, "text/event-stream");

// Store buffer pointer in session for later writes
session->sseBuffer = sseBuffer;

// To send an SSE event later:
session->sseBuffer->write(formatSseFrame("message", jsonData));
```

**Approach B — Store response and use `write()`:**
If `QHttpServerResponse` supports incremental `write()` after returning from the handler (verify at implementation time), store the response pointer directly.

**Approach C — Use `QHttpServerResponder`:**
Some Qt versions provide `QHttpServerResponder` for low-level response writing. Check availability.

> **Implementation note:** The exact approach depends on Qt 6.9.3's `QHttpServer` API. Verify during Phase 1 by writing a minimal SSE echo server test. The recommended path is Approach A (`QBuffer`-backed streaming).

#### SSE Frame Format

```cpp
QByteArray McpSseTransport::formatSseFrame(const QString& eventType, const QByteArray& data) const
{
    QByteArray frame;
    frame.append("event: ").append(eventType.toUtf8()).append('\n');

    // SSE spec: each line of multi-line data gets its own "data: " prefix
    const QList<QByteArray> lines = data.split('\n');
    for (const QByteArray& line : lines) {
        frame.append("data: ").append(line).append('\n');
    }

    frame.append('\n');  // blank line = end of frame
    return frame;
}
```

#### Client Disconnect Detection

```cpp
// Strategy: detect when the client closes the TCP connection.
//
// Option 1 (Preferred): QHttpServerResponse emits a signal or the underlying
//   QTcpSocket disconnects. We connect to the response's destroyed() signal
//   or monitor the socket's disconnected() signal.
//
// Option 2 (Fallback): Periodic keepalive. Write SSE comment lines
//   (": keepalive\n\n") every 15 seconds. If write() fails, the client
//   is gone — trigger cleanup.
//
// Option 3: Rely on timeout timer. If no POST /messages arrives within
//   30 minutes, assume disconnected and clean up. This is the simplest
//   approach and serves as a safety net regardless of disconnect detection.

// Implementation: use Option 1 as primary + Option 3 as safety net.
```

### 3. Async Tool Execution for Multi-Client

```cpp
// Problem: McpToolHandler::callTool() is synchronous. If client A triggers
// a long-running tool (e.g., send_keys with 500ms delays), client B's
// requests are blocked.
//
// Solution: wrap tool calls in QtConcurrent::run() for SSE transport.
//
// Flow:
//   1. POST /messages arrives with tools/call request
//   2. Look up session, validate
//   3. Launch QtConcurrent::run([=]() {
//        return m_toolHandler->callTool(name, args);
//      })
//   4. When the QFuture completes, use QMetaObject::invokeMethod()
//      to call back on the main thread and write the SSE response
//
// This keeps the Qt event loop free for other HTTP requests.
//
// IMPORTANT: McpToolHandler must be thread-safe for this to work.
// Currently it accesses HostManager singleton for keyboard/mouse.
// We need to add a QMutex around hardware access.
//
// Phase 2 (single client): synchronous is fine, skip this.
// Phase 3 (multi-client): add async wrapper + mutex.

// Code sketch for Phase 3:
#include <QtConcurrent>

void McpSseTransport::handleToolCallAsync(Session* session, const QJsonObject& request)
{
    QString toolName = request["params"].toObject()["name"].toString();
    QJsonObject toolArgs = request["params"].toObject()["arguments"].toObject();
    qint64 requestId = request["id"].toInteger();

    // Capture what we need; DO NOT capture session pointer directly
    // because it might be destroyed while the future is running.
    QString sessionId = session->id;

    QFuture<QJsonObject> future = QtConcurrent::run([this, toolName, toolArgs]() {
        return m_toolHandler->callTool(toolName, toolArgs);
    });

    // Use QFutureWatcher to get completion callback on main thread
    auto* watcher = new QFutureWatcher<QJsonObject>(this);
    connect(watcher, &QFutureWatcher<QJsonObject>::finished, this, [this, watcher, sessionId, requestId]() {
        QJsonObject result = watcher->result();

        // Build JSON-RPC response
        QJsonObject response;
        response["jsonrpc"] = "2.0";
        response["id"] = requestId;
        response["result"] = result;

        // Send via SSE — session might be gone by now, check first
        if (m_sessions.contains(sessionId)) {
            Session* s = m_sessions[sessionId];
            QByteArray data = QJsonDocument(response).toJson(QJsonDocument::Compact);
            sendSseEvent(s, "message", data);
        }

        watcher->deleteLater();
    });
}
```

### 4. McpServer Integration

```cpp
// ── mcpServer.h additions ──

public:
    // SSE transport control
    bool startSse(quint16 port = 8080);
    void stopSse();
    bool isSseRunning() const;
    quint16 ssePort() const;
    int sseSessionCount() const;

    // Unified lifecycle
    bool startAll();    // starts whichever transports are configured
    void stopAll();     // stops all running transports

signals:
    void sseTransportStarted(quint16 port);
    void sseTransportStopped();
    void sseSessionCreated(const QString& sessionId);
    void sseSessionDestroyed(const QString& sessionId);

private:
    McpSseTransport* m_sseTransport = nullptr;
    quint16 m_ssePort = 8080;
    bool m_sseEnabled = false;

// ── mcpServer.cpp key integration points ──

// In constructor or init:
m_sseTransport = new McpSseTransport(m_toolHandler, this);
connect(m_sseTransport, &McpSseTransport::sessionCreated,
        this, &McpServer::sseSessionCreated);
connect(m_sseTransport, &McpSseTransport::sessionDestroyed,
        this, &McpServer::sseSessionDestroyed);
connect(m_sseTransport, &McpSseTransport::transportError,
        this, [this](const QString& msg) {
            qWarning() << "[MCP] SSE transport error:" << msg;
        });

// startSse():
bool McpServer::startSse(quint16 port)
{
    if (m_sseTransport->isRunning()) {
        qWarning() << "[MCP] SSE transport already running on port" << m_sseTransport->port();
        return false;
    }
    if (!m_sseTransport->start(port)) {
        emit transportError(QString("Failed to start SSE on port %1").arg(port));
        return false;
    }
    m_ssePort = port;
    m_sseEnabled = true;
    qInfo() << "[MCP] SSE transport started on port" << port;
    emit sseTransportStarted(port);
    return true;
}
```

### 5. Thread Safety Strategy

```
┌─────────────────────────────────────────────────────────────────┐
│                    Thread Safety Model                           │
│                                                                  │
│  Main Thread (Qt Event Loop)                                    │
│  ┌─────────────────────────────────────────────────────────┐    │
│  │  QHttpServer (accepts connections, reads requests)       │    │
│  │  McpSseTransport (session map, SSE frame writing)        │    │
│  │  McpServer (transport lifecycle)                         │    │
│  └─────────────────────────┬───────────────────────────────┘    │
│                            │ QtConcurrent::run()                 │
│  ┌─────────────────────────▼───────────────────────────────┐    │
│  │  Worker Thread Pool (Qt-managed)                         │    │
│  │  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐   │    │
│  │  │ tools/call A │  │ tools/call B │  │ tools/call C │   │    │
│  │  └──────┬───────┘  └──────┬───────┘  └──────┬───────┘   │    │
│  │         │                  │                  │            │    │
│  │         └──────────────────┼──────────────────┘            │    │
│  │                            │ QMutex                         │    │
│  │                   ┌────────▼────────┐                       │    │
│  │                   │  HostManager    │                       │    │
│  │                   │  (hardware I/O) │                       │    │
│  │                   └─────────────────┘                       │    │
│  └─────────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────┘

Rules:
  - Session map (m_sessions) is ONLY accessed from main thread
  - McpToolHandler::callTool() runs in worker threads (Phase 3)
  - HostManager access is serialized via QMutex
  - SSE write operations happen ONLY on main thread (via invokeMethod callback)
  - No shared mutable state between sessions except HostManager (mutex-protected)
```

### 6. Constants

```cpp
// ── Add to mcpConstants.h ──

// SSE Transport
#define MCP_SSE_DEFAULT_PORT        8080
#define MCP_SSE_PATH_SSE            "/sse"
#define MCP_SSE_PATH_MESSAGES       "/messages"
#define MCP_SSE_CONTENT_TYPE        "text/event-stream"
#define MCP_SSE_KEEPALIVE_INTERVAL  15000       // 15 seconds
#define MCP_SSE_SESSION_TIMEOUT_MS  1800000     // 30 minutes
#define MCP_SSE_CLEANUP_INTERVAL    60000       // 60 seconds
#define MCP_SSE_MAX_SESSIONS        16
```

### 7. Command-Line Arguments

Extend existing argument parsing to support SSE:

```cpp
// New CLI arguments:
//   --sse-port <port>      Enable SSE transport on specified port (default: 8080)
//   --sse-bind <address>   Bind SSE to specific address (default: 127.0.0.1)
//   --sse-bind-any         Bind SSE to 0.0.0.0 (all interfaces, use with caution)
//
// Examples:
//   ./app --stdio                      # stdio only (existing)
//   ./app --stdio --sse-port 8080      # stdio + SSE on 8080
//   ./app --stdio --sse-port 9090 --sse-bind-any  # stdio + SSE on all interfaces
```

---

## Implementation Steps (Detailed)

### Phase 1: Infrastructure & API Verification (Day 1)

**Goal:** Confirm Qt6 HttpServer SSE streaming works, set up build system, create skeleton files.

| # | Task | Deliverable | Acceptance Criteria |
|---|------|-------------|---------------------|
| 1.1 | Write a minimal SSE echo server test (standalone `main.cpp` using `QHttpServer`) | `tests/sse_echo_test.cpp` | `curl -N http://localhost:8080/sse` receives `event: test\ndata: hello\n\n` and stays connected |
| 1.2 | Verify `QHttpServerResponse` streaming behavior — can we write incrementally after handler returns? | Notes in code comments | Determine whether Approach A (QBuffer) or B (direct write) works on Qt 6.9.3 |
| 1.3 | Verify client disconnect detection — does destroying the response fire a signal? | Notes in code comments | Confirm disconnect detection mechanism |
| 1.4 | Add `Qt6::HttpServer` to `CMakeLists.txt` | Updated CMakeLists.txt | `cmake` configures without error, build succeeds |
| 1.5 | Create `mcpSseTransport.h` skeleton with full class definition (above) | `server/mcp/mcpSseTransport.h` | File compiles, all includes resolve |
| 1.6 | Create `mcpSseTransport.cpp` skeleton with stub implementations | `server/mcp/mcpSseTransport.cpp` | File compiles, `start()` returns false (not yet implemented) |
| 1.7 | Add source files to `cmake/SourceFiles.cmake` | Updated cmake file | New files appear in build |
| 1.8 | Add SSE constants to `mcpConstants.h` | Updated constants file | Constants are `#define`d, no conflicts |

**Phase 1 Exit Criteria:** Project builds cleanly. Minimal SSE echo test proves streaming works on Qt 6.9.3. We know the exact API for persistent SSE streams.

---

### Phase 2: SSE Core — Single Client (Day 2–3)

**Goal:** One SSE client can connect, send JSON-RPC requests, and receive responses.

| # | Task | Deliverable | Acceptance Criteria |
|---|------|-------------|---------------------|
| 2.1 | Implement `formatSseFrame()` | Working SSE frame serializer | Output matches SSE spec: `event: X\ndata: Y\n\n` |
| 2.2 | Implement `createSession()` | Session creation with UUID, timeout timer | Session gets unique ID, timer starts |
| 2.3 | Implement `cleanupSession()` | Session removal, timer stop, signal emit | No memory leaks, signal fires |
| 2.4 | Implement `handleSse()` — `GET /sse` | Route handler that creates session, returns SSE stream | `curl -N` receives `endpoint` event with session URL |
| 2.5 | Implement client disconnect detection | Auto-cleanup when client drops | Killing `curl` triggers `cleanupSession()` |
| 2.6 | Implement `handleMessages()` — `POST /messages` | Route handler that parses JSON-RPC, calls `processRequest()` | Returns 202 Accepted for valid requests, 400/404 for invalid |
| 2.7 | Implement `processRequest()` — reuse existing dispatch logic | JSON-RPC processing | `initialize`, `tools/list`, `tools/call`, `ping` all work |
| 2.8 | Implement SSE response delivery — write JSON-RPC response to SSE stream | Responses appear in SSE stream | Client sees `event: message` with correct JSON-RPC response |
| 2.9 | Implement `refreshSession()` — reset timeout on each request | Timeout timer resets | Session doesn't expire while client is active |
| 2.10 | Register routes on `QHttpServer` in `start()` | Routes `/sse` and `/messages` are live | Both endpoints respond correctly |
| 2.11 | Test with `curl` — full flow: connect → initialize → tools/list → tools/call | Test log | All 4 JSON-RPC methods succeed via SSE |
| 2.12 | Test with Python MCP SDK (`mcp.client.sse`) | Python test script | `ClientSession.initialize()` + `list_tools()` succeed |

**Phase 2 Exit Criteria:** A single SSE client can complete a full MCP session (initialize → list tools → call tool → disconnect). Verified with both curl and Python SDK.

---

### Phase 3: Session Lifecycle & Multi-Client (Day 4–5)

**Goal:** Multiple concurrent SSE clients, async tool execution, robust session management.

| # | Task | Deliverable | Acceptance Criteria |
|---|------|-------------|---------------------|
| 3.1 | Implement periodic session sweep timer (`m_cleanupTimer`) | Timer fires every 60s, evicts stale sessions | Sessions idle >30 min are cleaned up |
| 3.2 | Implement `MAX_SESSIONS` cap | Reject new SSE connections when full | 17th client gets `503 Service Unavailable` |
| 3.3 | Integrate `McpSseTransport` into `McpServer` | `startSse()`, `stopSse()`, `isSseRunning()` | McpServer can start/stop SSE transport |
| 3.4 | Add `startAll()` / `stopAll()` unified lifecycle | All transports start/stop together | stdio + SSE both work simultaneously |
| 3.5 | Add CLI arguments `--sse-port`, `--sse-bind`, `--sse-bind-any` | Argument parsing in `main.cpp` | Arguments correctly configure SSE transport |
| 3.6 | Add `QMutex` to `HostManager` for thread-safe hardware access | Mutex-protected keyboard/mouse/serial calls | No crashes when 2 clients call tools simultaneously |
| 3.7 | Implement async tool execution via `QtConcurrent::run()` | Non-blocking tool calls in SSE transport | Client B's requests work while Client A's tool is running |
| 3.8 | Implement `QFutureWatcher` callback → SSE response delivery | Async results appear in correct client's SSE stream | Response goes to the right session, not mixed up |
| 3.9 | Handle edge case: session destroyed while tool is running | Watcher checks session existence before writing | No crash, no dangling pointer access |
| 3.10 | Handle edge case: `tools/call` with invalid tool name via SSE | Error response via SSE stream | JSON-RPC error returned, session stays alive |
| 3.11 | Handle edge case: POST to non-existent session ID | HTTP 404 response | Clear error message, no crash |
| 3.12 | Handle edge case: malformed JSON in POST body | HTTP 400 response | JSON-RPC parse error via SSE (if session valid) |
| 3.13 | Test 3 concurrent SSE clients | Test script with 3 parallel Python clients | All 3 can initialize and call tools independently |
| 3.14 | Test stdio + SSE simultaneously | Test log | Both transports work without interference |

**Phase 3 Exit Criteria:** Multiple concurrent SSE clients work correctly. Tool calls don't block other clients. stdio and SSE coexist. All edge cases handled gracefully.

---

### Phase 4: Integration, Hardening & Testing (Day 6–7)

**Goal:** Production-ready SSE transport with comprehensive testing.

| # | Task | Deliverable | Acceptance Criteria |
|---|------|-------------|---------------------|
| 4.1 | Add SSE status to `system_status` tool output | Updated tool response | Status shows SSE running state, port, session count |
| 4.2 | Add logging for SSE lifecycle events | Log output | Connect, disconnect, timeout, error all logged with `[MCP-SSE]` prefix |
| 4.3 | Implement graceful shutdown — close all SSE sessions cleanly | Clean shutdown | All sessions receive connection close, no resource leaks |
| 4.4 | Port conflict handling — clear error message if port is busy | Error handling | `startSse()` returns false with descriptive message, app continues |
| 4.5 | Add CORS headers (optional, for browser clients) | Response headers | `Access-Control-Allow-Origin: *` when configured |
| 4.6 | Write automated test script | `tests/test_sse_transport.py` | Script tests: connect, initialize, list tools, call each tool category, disconnect, multi-client |
| 4.7 | Test all 14 MCP tools via SSE transport | Test report | Every tool returns correct result via SSE |
| 4.8 | Stress test: 10 concurrent clients, rapid tool calls | Test report | No crashes, no memory leaks (verify with valgrind or AddressSanitizer) |
| 4.9 | Test connection drop scenarios: kill client mid-tool-call | Test report | Session cleaned up, no dangling resources |
| 4.10 | Test server shutdown while clients connected | Test report | Clients see connection close, server exits cleanly |
| 4.11 | Memory leak check — run 100 connect/disconnect cycles | valgrind/ASan report | Zero leaks |
| 4.12 | Update README with SSE transport documentation | Updated README | Usage instructions, CLI args, example client configs |

**Phase 4 Exit Criteria:** All tests pass. No memory leaks. Documentation updated. SSE transport is production-ready.

---

## Testing Strategy

### Manual Tests (curl)

```bash
# 1. SSE stream establishment
curl -N http://localhost:8080/sse
# Expected:
#   event: endpoint
#   data: /messages?sessionId=<uuid>

# 2. Initialize (in another terminal, using the sessionId from above)
SESSION_ID="<uuid>"
curl -X POST "http://localhost:8080/messages?sessionId=$SESSION_ID" \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2024-11-05","capabilities":{},"clientInfo":{"name":"curl-test","version":"1.0"}}}'
# Expected: SSE stream shows event: message with initialize result

# 3. List tools
curl -X POST "http://localhost:8080/messages?sessionId=$SESSION_ID" \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}}'

# 4. Call a tool
curl -X POST "http://localhost:8080/messages?sessionId=$SESSION_ID" \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"system_status","arguments":{}}}'

# 5. Error: invalid session
curl -X POST "http://localhost:8080/messages?sessionId=invalid-id" \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":1,"method":"ping"}'
# Expected: 404 Not Found

# 6. Error: malformed JSON
curl -X POST "http://localhost:8080/messages?sessionId=$SESSION_ID" \
  -H "Content-Type: application/json" \
  -d 'not json'
# Expected: 400 Bad Request
```

### Python SDK Test

```python
"""test_sse_transport.py — Automated SSE transport test."""
import asyncio
from mcp import ClientSession
from mcp.client.sse import sse_client

async def test_single_client():
    """Test basic MCP session via SSE."""
    async with sse_client("http://localhost:8080/sse") as (read, write):
        async with ClientSession(read, write) as session:
            # Initialize
            result = await session.initialize()
            print(f"Server: {result.serverInfo.name} v{result.serverInfo.version}")

            # List tools
            tools = await session.list_tools()
            print(f"Tools ({len(tools.tools)}): {[t.name for t in tools.tools]}")

            # Call system_status
            result = await session.call_tool("system_status", {})
            print(f"Status: {result.content[0].text}")

async def test_multi_client():
    """Test 3 concurrent clients."""
    async def client_task(client_id: int):
        async with sse_client("http://localhost:8080/sse") as (read, write):
            async with ClientSession(read, write) as session:
                await session.initialize()
                tools = await session.list_tools()
                print(f"Client {client_id}: {len(tools.tools)} tools")

    await asyncio.gather(
        client_task(1), client_task(2), client_task(3)
    )

async def main():
    print("=== Single Client Test ===")
    await test_single_client()
    print("\n=== Multi Client Test ===")
    await test_multi_client()
    print("\nAll tests passed!")

if __name__ == "__main__":
    asyncio.run(main())
```

### Integration Test Checklist

- [ ] Single SSE client: full MCP lifecycle
- [ ] Multiple concurrent SSE clients (3+)
- [ ] All 14 tools callable via SSE
- [ ] stdio + SSE coexistence
- [ ] Named Pipe + SSE coexistence (if Named Pipe still enabled)
- [ ] Client disconnect → session cleanup
- [ ] Server shutdown → clean session close
- [ ] Invalid session ID → 404
- [ ] Malformed JSON → 400
- [ ] Session timeout after 30 min inactivity
- [ ] Max sessions cap (16) → 503
- [ ] No memory leaks after 100 connect/disconnect cycles
- [ ] Async tool execution: client B not blocked by client A's long tool call

---

## Risks and Mitigations

| # | Risk | Impact | Mitigation |
|---|------|--------|------------|
| 1 | Qt6 HttpServer SSE API differs from expected | Phase 1 blocked | Phase 1.1–1.3 verifies API before committing to design |
| 2 | `McpToolHandler` not thread-safe for concurrent tool calls | Crashes with multi-client | Phase 3.6 adds QMutex; Phase 2 works with single client |
| 3 | SSE connection leak (sessions not cleaned up) | Memory grows unbounded | Phase 3.1 periodic sweep timer + Phase 4.11 valgrind check |
| 4 | Port 8080 conflicts with other services | SSE can't start | CLI `--sse-port` for custom port; clear error message |
| 5 | Binding to 0.0.0.0 exposes server to network | Security risk | Default bind to 127.0.0.1; `--sse-bind-any` requires explicit opt-in |
| 6 | Large tool responses exceed SSE frame size | Client can't parse | Use `QJsonDocument::Compact` (no pretty-print); SSE has no hard size limit |
| 7 | Client sends requests faster than tools can execute | Request queue grows | Phase 3: async execution + optional per-session request queue cap |

## Future Extensions

- **Streamable HTTP** (MCP 2025-03-26 spec) — single `POST /mcp` endpoint, supports both JSON and SSE responses, no separate SSE stream needed
- **WebSocket transport** — bidirectional, lower latency for high-frequency updates
- **TLS/HTTPS** — encrypted remote access via `QSslSocket` integration with `QHttpServer`
- **Authentication** — Bearer token in `Authorization` header, validated in `handleSse()` and `handleMessages()`
- **Rate limiting** — per-session and per-IP request throttling via `QElapsedTimer` sliding window
- **mDNS/Bonjour** — LAN auto-discovery via `QDNSServiceBrowser` (register `_mcp._tcp` service)
- **Session persistence** — save/restore session state across server restarts
