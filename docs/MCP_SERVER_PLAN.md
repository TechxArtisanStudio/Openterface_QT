# Openterface MCP Server Implementation Plan

## 1. Overview

The Openterface Mini KVM QT application currently exposes remote control capabilities (mouse, keyboard, screen capture) through a TCP Server. This plan introduces a **Model Context Protocol (MCP) Server** implementation that allows AI models like Claude to directly control the target computer.

### Core Requirements

- **GUI + MCP Coexistence**: The GUI runs normally while MCP operates in the background
- **Independent Toggle**: MCP Server can be started/stopped independently, similar to the TCP Server
- **Script Engine Preservation**: The existing Lexer/Parser/SemanticAnalyzer engine remains unchanged
- **Screen Capture as Tool**: Expose camera capture as an MCP Tool (not Resource)
- **Transport Layer**: Named Pipe via QLocalServer to avoid stdio conflicts with GUI

## 2. Architecture Design

```
┌─────────────────────────────────────────────────────────────┐
│                    Openterface QT App                        │
│                                                              │
│  ┌──────────┐    ┌──────────────────────────────────┐       │
│  │  GUI     │    │          MCP Server               │       │
│  │(MainWindow)──▶│  (McpServer.h/cpp)               │       │
│  │          │    │    ├─ McpProtocol.h/cpp          │       │
│  │  Toggle  │    │    └─ QLocalServer (Named Pipe)  │       │
│  └──────────┘    └────────┬─────────────────────────┘       │
│       │                   │                                  │
│       │          ┌────────▼────────┐                         │
│       │          │ McpToolHandler  │                         │
│       │          │ (Tool Registry) │                         │
│       │          └────────┬────────┘                         │
│       │                   │                                  │
│  ┌────▼───────────────────▼──────────────┐                  │
│  │     Existing Components (Reused)      │                  │
│  │  HostManager (Singleton)              │                  │
│  │    ├─ MouseManager                    │                  │
│  │    └─ KeyboardManager                 │                  │
│  │  CameraManager (via MainWindow ptr)   │                  │
│  │  ScriptRunner (Lexer/Parser/Executor) │                  │
│  │  SerialPortManager                    │                  │
│  └────────────────────────────────────────┘                  │
└──────────────────────────────────────────────────────────────┘
           ▲
           │ JSON-RPC 2.0 over Named Pipe
           │
    ┌──────┴──────┐
    │  MCP Client  │
    │ (Claude Code)│
    └──────────────┘
```

### Transport Layer: Named Pipe (QLocalServer)

- **Why not stdio**: stdio transport conflicts with GUI stdout
- **Implementation**: QLocalServer creates Unix Domain Socket on Linux (`/tmp/openterface-mcp.sock`) and Named Pipe on Windows (`\\.\pipe\openterface-mcp`)
- **Message Format**: JSON-RPC 2.0 messages delimited by newlines, identical to stdio transport format
- **Lifecycle**: Can be enabled/disabled dynamically during GUI runtime, same level as TCP Server

## 3. MCP Tools Specification

| Tool Name | Parameters | Return | Implementation |
|-----------|------------|--------|----------------|
| `mouse_move_absolute` | `{x: int (0-4096), y: int (0-4096)}` | success/fail | `MouseManager::handleAbsoluteMouseAction(x,y,0,0)` |
| `mouse_click` | `{x: int, y: int, button?: "left"/"right"/"middle", count?: int}` | success/fail | Two calls to `handleAbsoluteMouseAction` (press+release) |
| `mouse_move_relative` | `{dx: int, dy: int}` | success/fail | `MouseManager::handleRelativeMouseAction(dx,dy,0,0)` |
| `mouse_scroll` | `{direction: "up"/"down", lines?: int}` | success/fail | `MouseManager::scrollWheel(direction, lines)` |
| `keyboard_press_key` | `{key: int (Qt keyCode), modifiers?: int, isKeyDown: bool}` | success/fail | `HostManager::handleKeyboardAction()` |
| `keyboard_type_text` | `{text: string}` | success/fail | `KeyboardManager::pasteTextToTarget(text)` |
| `keyboard_send_keys` | `{keys: string (AHK syntax)}` | success/fail | Script `Send` command → `SemanticAnalyzer::analyzeSendStatement()` |
| `keyboard_function_key` | `{key: "F1"-"F12"}` | success/fail | `KeyboardManager::sendFunctionKey()` |
| `keyboard_ctrl_alt_del` | `{}` | success/fail | `KeyboardManager::sendCtrlAltDel()` |
| `keyboard_set_layout` | `{layout: string}` | success/fail | `KeyboardManager::setKeyboardLayout()` |
| `capture_screen` | `{quality?: int (1-100)}` | base64 JPEG | `CameraManager::getLatestOriginalFrame()` → encode JPEG → base64 |
| `capture_last_image` | `{}` | base64 image | TcpServer `lastimage` logic |
| `execute_script` | `{script: string (AHK syntax)}` | execution result | Lexer → Parser → ScriptRunner → SemanticAnalyzer |

## 4. File Structure

```
server/
├── tcpServer.h/cpp          # Existing, preserved
├── tcpResponse.h/cpp        # Existing, preserved
├── mcp/
│   ├── mcpServer.h          # MCP Server main class (QLocalServer)
│   ├── mcpServer.cpp        #   - Connection lifecycle management
│   ├── mcpProtocol.h        # JSON-RPC 2.0 protocol parser/serializer
│   ├── mcpProtocol.cpp      #   - parseRequest(), buildResult(), buildError()
│   ├── mcpToolHandler.h     # Tool registry and dispatch
│   ├── mcpToolHandler.cpp   #   - Each tool calls existing components
│   └── mcpConstants.h       # MCP protocol constants (version, methods, etc.)
```

**Total: 6 new files, no modifications to existing files**

## 5. Detailed Design

### 5.1 McpServer (`server/mcp/mcpServer.h/cpp`)

```cpp
class McpServer : public QObject {
    Q_OBJECT
public:
    explicit McpServer(QObject *parent = nullptr);
    ~McpServer();

    // Toggle control - same as TcpServer
    bool start(const QString &pipeName = "openterface-mcp");
    void stop();
    bool isRunning() const;

    // Dependency injection (same pattern as TcpServer::setCameraManager)
    void setCameraManager(CameraManager* cameraManager);
    void setHostManager(HostManager* hostManager);  // Or use HostManager::getInstance()
    void setScriptRunner(ScriptRunner* scriptRunner);

signals:
    void mcpStarted();
    void mcpStopped();
    void mcpLog(const QString& message);

private slots:
    void onNewConnection();
    void onReadyRead();
    void onClientDisconnected();

private:
    QLocalServer* m_server = nullptr;
    QLocalSocket* m_client = nullptr;  // MCP supports single client only
    McpProtocol* m_protocol = nullptr;
    McpToolHandler* m_toolHandler = nullptr;
    bool m_initialized = false;
    QString m_pipeName;

    void handleMessage(const QJsonObject& request, QLocalSocket* client);
    void sendResponse(const QJsonObject& response, QLocalSocket* client);
};
```

**Key Design Points:**
- Uses `QLocalServer` instead of TCP, default pipe name: `openterface-mcp`
- Accepts only one client connection (MCP standard behavior)
- Each message is **one line of JSON** (newline-delimited), identical to MCP stdio transport format
- `start()`/`stop()` provides toggle control, symmetric with TCP Server's `startServer()`

### 5.2 McpProtocol (`server/mcp/mcpProtocol.h/cpp`)

```cpp
class McpProtocol {
public:
    // Parse client requests
    struct Request {
        QString jsonrpc;   // Must be "2.0"
        QString method;    // "initialize", "tools/list", "tools/call"
        QJsonObject params;
        QVariant id;       // request ID (int or string)
    };

    static bool parseRequest(const QByteArray& line, Request& out);

    // Build responses
    static QJsonObject buildResult(const QVariant& id, const QJsonValue& result);
    static QJsonObject buildError(const QVariant& id, int code, const QString& message);

    // MCP method constants
    static constexpr const char* METHOD_INITIALIZE = "initialize";
    static constexpr const char* METHOD_TOOLS_LIST = "tools/list";
    static constexpr const char* METHOD_TOOLS_CALL = "tools/call";
    static constexpr const char* METHOD_NOTIFICATION_INITIALIZED = "notifications/initialized";
};
```

**Protocol Flow:**

```
Client (Claude)                    Server (McpServer)
    │                                   │
    │── initialize ──────────────────▶  │  Returns serverInfo + capabilities
    │◀── {"result": {...}} ─────────── │
    │                                   │
    │── notifications/initialized ───▶  │  (notification, no reply)
    │                                   │
    │── tools/list ──────────────────▶  │  Returns all tool definitions (name, description, inputSchema)
    │◀── {"result": {"tools": [...]}} ─│
    │                                   │
    │── tools/call {name, arguments} ▶  │  Calls corresponding tool handler
    │◀── {"result": {"content": [...]}} │  Returns text or image
    │                                   │
    │── (multiple tools/call) ...      │  Repeated calls
```

### 5.3 McpToolHandler (`server/mcp/mcpToolHandler.h/cpp`)

```cpp
class McpToolHandler : public QObject {
    Q_OBJECT
public:
    explicit McpToolHandler(QObject *parent = nullptr);

    void setCameraManager(CameraManager* cam);

    // Return all tool definitions (for tools/list)
    QJsonArray listTools() const;

    // Execute tool call (for tools/call)
    // Returns {"content": [{"type": "text"/"image", ...}]}
    QJsonObject callTool(const QString& name, const QJsonObject& arguments);

private:
    CameraManager* m_cameraManager = nullptr;

    // Tool implementations - directly call existing components
    QJsonObject toolMouseMoveAbsolute(const QJsonObject& args);
    QJsonObject toolMouseClick(const QJsonObject& args);
    QJsonObject toolMouseMoveRelative(const QJsonObject& args);
    QJsonObject toolMouseScroll(const QJsonObject& args);
    QJsonObject toolKeyboardPressKey(const QJsonObject& args);
    QJsonObject toolKeyboardTypeText(const QJsonObject& args);
    QJsonObject toolKeyboardSendKeys(const QJsonObject& args);
    QJsonObject toolKeyboardFunctionKey(const QJsonObject& args);
    QJsonObject toolKeyboardCtrlAltDel(const QJsonObject& args);
    QJsonObject toolKeyboardSetLayout(const QJsonObject& args);
    QJsonObject toolCaptureScreen(const QJsonObject& args);
    QJsonObject toolCaptureLastImage(const QJsonObject& args);
    QJsonObject toolExecuteScript(const QJsonObject& args);

    // Helper methods
    QJsonObject textResult(const QString& text);
    QJsonObject errorResult(const QString& message);
    QJsonObject imageResult(const QByteArray& base64Data, const QString& mimeType);
};
```

**Tool Definition Example (returned by tools/list):**

```json
{
  "name": "mouse_click",
  "description": "Click mouse on target computer. Coordinate range: 0-4096.",
  "inputSchema": {
    "type": "object",
    "properties": {
      "x": {"type": "integer", "description": "X coordinate (0-4096)", "minimum": 0, "maximum": 4096},
      "y": {"type": "integer", "description": "Y coordinate (0-4096)", "minimum": 0, "maximum": 4096},
      "button": {"type": "string", "enum": ["left","right","middle"], "default": "left"},
      "count": {"type": "integer", "description": "Click count", "default": 1}
    },
    "required": ["x", "y"]
  }
}
```

### 5.4 Toggle Control — GUI Integration

Add MCP Server toggle control in `MainWindow`, symmetric with TCP Server:

**Modified Files:** `ui/mainwindow.h`, `ui/mainwindow.cpp` (minimal changes)

```cpp
// mainwindow.h additions
#include "server/mcp/mcpServer.h"

class MainWindow {
    // ...
    McpServer* m_mcpServer = nullptr;  // New member

    void initMcpServer();   // Called near existing initTcpServer()
    void toggleMcpServer(bool enabled);  // Slot function
};
```

**Add toggle button in GUI menu/settings:** Add MCP Server toggle button next to existing TCP Server settings.

### 5.5 Command-Line `--mcp-start` Support

Parse new argument in `main.cpp`:

```cpp
// main.cpp argument parsing
for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--skip-env-check") == 0) {
        skipEnvironmentCheck = true;
    } else if (strcmp(argv[i], "--mcp-start") == 0) {
        // Auto-start MCP Server after GUI launches
        autoStartMcp = true;
    }
}
```

## 6. Thread Safety Design

| Scenario | Handling |
|----------|----------|
| MCP thread calls MouseManager/KeyboardManager | Use `QMetaObject::invokeMethod` to dispatch to main thread, or Signal/Slot mechanism |
| MCP thread accesses CameraManager | CameraManager already has mutex protection (`QMutex` in `getLatestOriginalFrame()`) |
| Script execution | Existing `ScriptRunner` already has worker QThread + atomic flag; MCP emits signal to trigger |
| Serialize MCP requests | `QLocalServer` accepts only one client; requests are processed sequentially |

**Core Principle:** McpToolHandler's tool methods do not directly operate hardware. They call existing components in the main thread safely via Qt Signal/Slot cross-thread mechanism:

```cpp
// Example: mouse_click tool implementation
QJsonObject McpToolHandler::toolMouseClick(const QJsonObject& args) {
    int x = args["x"].toInt();
    int y = args["y"].toInt();
    int button = parseButton(args.value("button").toString("left"));

    // Safe call via signal/slot (MCP thread → main thread)
    HostManager::getInstance().handleMousePress(...);
    QThread::msleep(50);
    HostManager::getInstance().handleMouseRelease(...);

    return textResult("Clicked at " + QString::number(x) + "," + QString::number(y));
}
```

**Note:** MouseManager's `handleAbsoluteMouseAction` is synchronous, internally only assembles QByteArray then calls `SerialPortManager::sendCommandAsync()` (non-blocking async send), so calling directly from MCP thread is safe.

## 7. CMakeLists.txt Changes

```cmake
# Add to SERVER_SOURCES in cmake/SourceFiles.cmake:
set(SERVER_SOURCES
    server/tcpServer.cpp server/tcpServer.h
    server/tcpResponse.cpp server/tcpResponse.h
    # === New MCP Files ===
    server/mcp/mcpServer.cpp server/mcp/mcpServer.h
    server/mcp/mcpProtocol.cpp server/mcp/mcpProtocol.h
    server/mcp/mcpToolHandler.cpp server/mcp/mcpToolHandler.h
    server/mcp/mcpConstants.h
)
```

**No new Qt module dependencies needed** — `QLocalServer`/`QLocalSocket` belong to `Qt6::Network`, already imported at line 65 and 136 of CMakeLists.txt.

## 8. Implementation Phases

### Phase 1: Core Framework (4 files)
1. **`mcpConstants.h`** — MCP protocol constant definitions
2. **`mcpProtocol.h/cpp`** — JSON-RPC 2.0 parsing/serialization
3. **`mcpServer.h/cpp`** — QLocalServer management, connection handling
4. **`mcpToolHandler.h/cpp`** — Tool registration framework (implement 2-3 tools first to validate framework)

### Phase 2: Integrate Existing Components
5. Implement all 13 tools in McpToolHandler
6. Add MCP Server toggle in MainWindow
7. Add `--mcp-start` command-line argument in main.cpp

### Phase 3: Verification
8. Write Python MCP client test script
9. Configure Claude Code MCP connection (`.claude/settings.json`)
10. End-to-end testing: Claude → MCP → mouse move/keyboard input/screen capture

## 9. Claude Code Configuration Example

```json
{
  "mcpServers": {
    "openterface-kvm": {
      "command": "socat",
      "args": [
        "STDIN,raw,echo=0",
        "UNIX-CONNECT:/tmp/openterface-mcp.sock"
      ]
    }
  }
}
```

Or use a simple Python bridge script (stdin/stdout ↔ Named Pipe).

## 10. Risks and Considerations

1. **QLocalServer Path Limitation**: Unix socket file path has length limit (108 characters); `/tmp/openterface-mcp.sock` is short enough
2. **Windows Named Pipe**: QLocalServer automatically uses Named Pipe on Windows with identical API; no platform-specific handling needed
3. **CameraManager Thread Safety**: `getLatestOriginalFrame()` has internal mutex protection; MCP thread can call safely
4. **Script Execution is Asynchronous**: `execute_script` tool needs to wait for completion before returning result. Use existing `ScriptRunner::scriptFinished` signal + QEventLoop to wait
5. **GUI Coexistence Performance**: MCP calls should not block GUI event loop. All hardware operations (serial send, screen capture) are non-blocking or executed via worker threads

## 11. File Dependencies

```
mcpConstants.h          ← No dependencies
mcpProtocol.h/cpp       ← QJsonObject, QJsonDocument, QByteArray
mcpToolHandler.h/cpp    ← mcpProtocol.h, CameraManager, HostManager, ScriptRunner
mcpServer.h/cpp         ← mcpProtocol.h, mcpToolHandler.h, QLocalServer, QLocalSocket
```

## 12. API Compatibility

**Backward Compatibility:** All existing TCP Server functionality remains unchanged. MCP Server is an independent module that reuses existing components without modifying them.

**Forward Compatibility:** MCP Server follows MCP specification v1.0. Future additions (Resources, Prompts, etc.) can be added to McpToolHandler without affecting existing tools.
