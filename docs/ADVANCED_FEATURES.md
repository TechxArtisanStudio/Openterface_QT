# Advanced AI Chat & MCP Features

This document covers advanced features and architectural improvements in the Openterface AI Chat and MCP systems.

## Table of Contents

- [Overview](#overview)
- [OpenAI Native Function Calling](#openai-native-function-calling)
- [Shared Tool Executor Architecture](#shared-tool-executor-architecture)
- [Web Search Integration](#web-search-integration)
- [Tools Configuration Tree](#tools-configuration-tree)
- [Cursor Blink Detection](#cursor-blink-detection)
- [Advanced MCP Integration](#advanced-mcp-integration)
- [Performance Optimization](#performance-optimization)
- [Troubleshooting](#troubleshooting)

---

## Overview

The Openterface AI Chat and MCP systems have undergone significant architectural improvements to provide:

- **Native OpenAI Function Calling Support**: Seamless integration with models using structured tool calls
- **Shared Execution Layer**: Consistent behavior between MCP and AI Chat
- **Web Search Capabilities**: Multi-provider search integration
- **Advanced Terminal Detection**: Cursor blink analysis for intelligent automation
- **Improved Configuration**: Hierarchical tool management UI

These features work together to provide a robust, flexible platform for remote computer control and automation.

---

## OpenAI Native Function Calling

### Background

OpenAI's function calling API returns tool calls in a structured format separate from the message content:

```json
{
  "choices": [{
    "message": {
      "role": "assistant",
      "content": null,
      "tool_calls": [
        {
          "id": "call_abc123",
          "type": "function",
          "function": {
            "name": "capture_screen",
            "arguments": "{\"quality\": 80}"
          }
        }
      ]
    }
  }]
}
```

Previously, the system only parsed tool calls embedded in the `content` text, causing failures when models used the native format (where `content` is null).

### Implementation

#### 1. Data Structure Enhancement

**File:** `ai/ChatTypes.h`

```cpp
struct ChatCompletionResult {
    QString content;
    int inputTokenCount = -1;
    int outputTokenCount = -1;
    QList<QJsonObject> toolCalls;  // Native OpenAI tool calls
};

struct AgentToolCall {
    QString tool;
    QVariantMap args;
    QString toolCallId;  // Preserves OpenAI call ID for proper API responses
};
```

#### 2. API Response Parsing

**File:** `ai/ChatApiClient.cpp`

The client now extracts tool calls from the `message.tool_calls` array:

```cpp
// Extract tool_calls from OpenAI function calling format
QList<QJsonObject> toolCalls;
if (message.contains("tool_calls") && message["tool_calls"].isArray()) {
    QJsonArray toolCallsArray = message["tool_calls"].toArray();
    for (const auto &toolCallVal : toolCallsArray) {
        QJsonObject toolCallObj = toolCallVal.toObject();
        if (toolCallObj.contains("function") && toolCallObj["function"].isObject()) {
            QJsonObject functionObj = toolCallObj["function"].toObject();
            QJsonObject toolCall;
            toolCall["id"] = toolCallObj["id"].toString();
            toolCall["name"] = functionObj["name"].toString();
            toolCall["arguments"] = functionObj["arguments"].toString();
            toolCalls.append(toolCall);
        }
    }
}
```

#### 3. Tool Call Execution

**File:** `ai/ChatManager.cpp`

The manager checks for native tool calls first, then falls back to text parsing:

```cpp
// First check for tool_calls from OpenAI function calling format
QList<AgentToolCall> toolCalls;
if (!result.toolCalls.isEmpty()) {
    // Convert OpenAI format to internal AgentToolCall format
    for (const auto &toolCallObj : result.toolCalls) {
        AgentToolCall call;
        call.tool = toolCallObj["name"].toString();
        // Parse arguments JSON string into QVariantMap
        QString argsStr = toolCallObj["arguments"].toString();
        QJsonDocument argsDoc = QJsonDocument::fromJson(argsStr.toUtf8());
        if (argsDoc.isObject()) {
            QJsonObject argsObj = argsDoc.object();
            for (auto it = argsObj.begin(); it != argsObj.end(); ++it) {
                call.args[it.key()] = it.value().toVariant();
            }
        }
        call.toolCallId = toolCallObj["id"].toString();
        toolCalls.append(call);
    }
} else {
    // Fallback: parse tool calls from content text (legacy format)
    toolCalls = toolExec.parseToolCalls(result.content);
}
```

### Benefits

- **Compatibility**: Works with all OpenAI-compatible models (GPT-4, GPT-3.5, Claude, etc.)
- **Reliability**: No more "empty tool_calls" errors
- **Proper API Integration**: Tool call IDs are preserved for correct conversation flow
- **Backward Compatibility**: Legacy text-based tool calls still work
- **Better Error Handling**: Empty content is only an error if no tool calls exist

### Testing

The implementation has been verified with:
- MCP server tool execution (system_status, capture_screen)
- Multiple simultaneous tool calls
- Tool call ID preservation across conversation turns
- Backward compatibility with text-based format

---

## Shared Tool Executor Architecture

### Overview

`SharedToolExecutor` is a singleton that provides core logic for terminal detection, command execution, and screen analysis. Both the MCP tool handler and AI chat system delegate to this class, ensuring consistent behavior.

### Architecture

```
┌─────────────────────────────────────────────────────┐
│              SharedToolExecutor (Singleton)          │
│                                                     │
│  Core Methods:                                      │
│  - detectCursor()          Terminal idle detection  │
│  - runCommandAndWait()     Execute + poll for idle  │
│  - screenToMarkdown()      OCR-based screen text    │
│  - differentialAnalysis()  Change detection         │
│  - captureScreen()         Screenshot capture       │
│  - typeText()              Keyboard input           │
│  - pressKey()              Key combinations         │
│  - mouseClick()            Mouse actions            │
└─────────────────────────────────────────────────────┘
         ▲                    ▲
         │                    │
    ┌────┴────┐          ┌────┴────┐
    │   MCP   │          │AI Chat  │
    │ Handler │          │ System  │
    └─────────┘          └─────────┘
```

### Key Features

#### 1. Terminal Idle Detection

Detects when a terminal is waiting for input by analyzing:
- Cursor blink patterns (typically 1-2 Hz)
- Screen stability over multiple frames
- Shell prompt presence via OCR

```cpp
QJsonObject detectCursor(const QJsonObject &args);
// Returns: {detected: true, confidence: 0.95, status: "idle", ...}
```

#### 2. Smart Command Execution

Executes commands and waits for completion using two-phase polling:
- Phase 1: Wait for "outputting" (command running)
- Phase 2: Wait for "idle" (command finished)

```cpp
QJsonObject runCommandAndWait(const QJsonObject &args);
// Returns: {success: true, wait_time_ms: 1234, status: "idle", ...}
```

#### 3. Differential Screen Analysis

Compares current frame against previous frame to detect changes:
- Returns text diff of what changed
- Highlights BIOS-relevant changes
- More efficient than full OCR for iterative tasks

```cpp
QJsonObject differentialAnalysis(const QJsonObject &args);
// Returns: {changes: "...", bios_highlights: [...], ...}
```

### Usage

**MCP Tool Handler:**

```cpp
// In mcpToolHandler.cpp
QJsonObject result = SharedToolExecutor::instance().detectCursor(args);
// Format result for MCP protocol
```

**AI Chat System:**

```cpp
// In ChatToolExecution.cpp
QJsonObject result = SharedToolExecutor::instance().runCommandAndWait(args);
// Format result for AI conversation
```

### Benefits

- **Code Reuse**: Single implementation for both systems
- **Consistency**: Same behavior across MCP and AI Chat
- **Maintainability**: Bug fixes apply to both systems
- **Performance**: Shared resources (camera, screen analyzer)

---

## Web Search Integration

### Overview

The AI Chat system includes multi-provider web search capabilities, allowing the AI to search the internet for information when needed.

### Provider Architecture

The system uses a fallback chain of search providers:

1. **Exa AI** (Primary) - AI-optimized semantic search via MCP
   - Endpoint: `https://mcp.exa.ai/mcp`
   - Works anonymously (no API key required)
   - Best for technical queries

2. **Parallel AI** (Secondary) - AI-optimized search via MCP
   - Endpoint: `https://search.parallel.ai/mcp`
   - Works anonymously
   - Good alternative when Exa fails

3. **DuckDuckGo** (Fallback) - Instant Answer API
   - Free, no API key required
   - Limited to instant answers

4. **Wikipedia** (Fallback) - Encyclopedia search
   - Free, no API key required
   - Good for general knowledge

### Configuration

Configure providers in **Settings → AI Chat → Web Search**:

- Enable/disable web search
- Set provider priority order
- Configure API keys (optional, for enhanced access)

### Usage

The AI can invoke web search via tool calls:

```json
{
  "tool_calls": [{
    "tool": "web_search",
    "query": "Linux kernel 6.10 new features"
  }]
}
```

The system automatically:
1. Tries providers in order
2. Returns first successful result
3. Propagates SSL errors immediately
4. Formats results for AI consumption

### Implementation

**Core Files:**
- `ai/WebSearchManager.h/cpp` - Provider orchestration
- `ai/WebSearchProviders.h/cpp` - Concrete provider implementations
- `ai/WebSearchProvider.h` - Base provider interface

**Provider Interface:**

```cpp
class WebSearchProvider {
    virtual QString name() const = 0;
    virtual QString id() const = 0;
    virtual bool requiresApiKey() const = 0;
    virtual bool isConfigured() const = 0;
    virtual QString search(const QString &query) const = 0;
};
```

---

## Tools Configuration Tree

### Overview

The AI Chat tools are now organized in a hierarchical tree structure in the settings UI, providing better organization and easier management.

### Tree Structure

```
Tools Configuration
├── [Select All checkbox]
└── Tree View
    ├── Screen Tools (expandable)
    │   ├── Screen Capture (capture_screen)
    │   └── Screen to Markdown (screen_to_markdown)
    ├── Mouse Tools (expandable)
    │   ├── Move Mouse (move_mouse)
    │   ├── Left Click (left_click)
    │   ├── Right Click (right_click)
    │   ├── Double Click (double_click)
    │   └── Left Drag (left_drag)
    ├── Keyboard Tools (expandable)
    │   ├── Type Text (type_text)
    │   ├── Press Key (press_key)
    │   └── Repeat Key (repeat_key)
    ├── Recording Tools (expandable)
    │   ├── Start Recording (start_recording)
    │   └── Stop Recording (stop_recording)
    └── System/Host Tools (expandable)
        ├── Set Target System (set_target_system)
        └── Run Bash (run_bash)
```

### Features

**Tree View Capabilities:**
- Expandable/collapsible groups
- Two-column layout (name + tool ID)
- Checkable items at all levels
- Group propagation (check group → check all children)
- Select All synchronization
- Bold group headers for visual distinction

**State Management:**
- Automatic group state updates based on children
- Dirty state tracking for Apply/Revert
- Snapshot/revert preserves tree state
- Persistent storage via GlobalSetting

### Implementation

**Key Components:**

```cpp
// Tree model with checkable items
QTreeView *m_toolsTreeView;
QStandardItemModel *m_toolsModel;

// Group propagation
connect(m_toolsModel, &QStandardItemModel::itemChanged, this, 
    [this](QStandardItem* item){
        if (item && item->rowCount() > 0 && item->isCheckable()) {
            Qt::CheckState groupState = item->checkState();
            for (int c = 0; c < item->rowCount(); ++c) {
                QStandardItem* child = item->child(c, 0);
                if (child && child->isCheckable()) {
                    child->setCheckState(groupState);
                }
            }
        }
    });
```

### Benefits

- **Better Organization**: Hierarchical structure reduces visual clutter
- **Consistent Design**: Matches logging page UI pattern
- **Improved UX**: Expandable groups make navigation easier
- **Scalable**: Easy to add new tool groups
- **Professional Appearance**: Two-column layout provides better documentation

---

## Cursor Blink Detection

### Overview

Advanced terminal state detection using cursor blink analysis, enabling the AI to determine when a terminal is idle and ready for input.

### How It Works

The system captures multiple frames and analyzes:

1. **Cursor Blink Pattern**: Terminals typically blink cursors at 1-2 Hz
2. **Screen Stability**: Idle terminals have minimal pixel changes
3. **Shell Prompt Presence**: OCR detects common prompt patterns ($, #, >)

### Detection Algorithm

```cpp
QJsonObject SharedToolExecutor::detectCursor(const QJsonObject &args) {
    int samples = args.value("samples").toInt(5);
    int intervalMs = args.value("interval_ms").toInt(350);
    
    // Capture multiple frames
    QList<QImage> frames;
    for (int i = 0; i < samples; ++i) {
        frames.append(captureFrame());
        QThread::msleep(intervalMs);
    }
    
    // Analyze cursor blink
    bool cursorBlinking = analyzeCursorBlink(frames);
    
    // Check screen stability
    double stability = calculateScreenStability(frames);
    
    // Detect shell prompt via OCR
    bool hasPrompt = detectShellPrompt(frames.last());
    
    // Calculate confidence
    double confidence = calculateConfidence(cursorBlinking, stability, hasPrompt);
    
    return {
        {"detected", cursorBlinking && stability > 0.9 && hasPrompt},
        {"confidence", confidence},
        {"status", cursorBlinking ? "idle" : "busy"},
        {"cursor_position", cursorPosition},
        {"frames_analyzed", samples}
    };
}
```

### Use Cases

#### 1. Command Completion Detection

```cpp
// Execute command and wait for completion
QJsonObject result = runCommandAndWait({
    {"command", "ls -la"},
    {"max_wait_ms", 10000}
});

// Two-phase polling:
// Phase 1: Wait for "outputting" (command running)
// Phase 2: Wait for "idle" (command finished)
```

#### 2. BIOS Navigation

Detect when BIOS menu is ready for input:

```cpp
QJsonObject detection = detectCursor({
    {"samples", 8},
    {"interval_ms", 200}
});

if (detection["detected"].toBool()) {
    // BIOS menu is idle, safe to send keystrokes
    pressKey({{"keys", "down"}});
}
```

#### 3. Automated Testing

Verify terminal state before/after operations:

```cpp
// Before: Terminal should be idle
QJsonObject before = detectCursor({});
Q_ASSERT(before["detected"].toBool());

// Execute test command
runCommandAndWait({{"command", "make test"}});

// After: Terminal should be idle again
QJsonObject after = detectCursor({});
Q_ASSERT(after["detected"].toBool());
```

### Configuration

Adjust detection parameters in **Settings → AI Chat → Advanced**:

- **Detection Samples**: Number of frames to capture (3-8, default 5)
- **Detection Interval**: Time between frames in ms (200-1000, default 350)
- **Confidence Threshold**: Minimum confidence for "idle" detection (0.0-1.0, default 0.8)

### Performance

- **Detection Time**: ~1.5-3 seconds (5 samples × 350ms)
- **CPU Usage**: Low (image comparison is optimized)
- **Memory**: Minimal (frames are discarded after analysis)

---

## Advanced MCP Integration

### Overview

The AI Chat system integrates deeply with the MCP (Model Context Protocol) server, enabling advanced automation scenarios.

### MCP Tool Exposure

All AI Chat tools are also available via MCP:

| AI Chat Tool | MCP Tool | Description |
|--------------|----------|-------------|
| `capture_screen` | `capture_screen` | Screenshot capture |
| `type_text` | `keyboard_type_text` | Keyboard input |
| `press_key` | `keyboard_press_key` | Key combinations |
| `move_mouse` | `mouse_move_absolute` | Mouse positioning |
| `left_click` | `mouse_click` | Mouse clicks |
| `run_bash` | `execute_script` | Command execution |
| `web_search` | N/A | AI Chat only |

### Shared Execution

Both systems use `SharedToolExecutor` for consistent behavior:

```cpp
// MCP tool handler
QJsonObject McpToolHandler::toolCaptureScreen(const QJsonObject &args) {
    return SharedToolExecutor::instance().captureScreen(args);
}

// AI Chat tool execution
void ChatToolExecution::executeTool(const AgentToolCall &call) {
    QJsonObject args = variantMapToJsonObject(call.args);
    QJsonObject result = SharedToolExecutor::instance().captureScreen(args);
    // Format for AI conversation
}
```

### Advanced Scenarios

#### 1. Remote AI Control

Use MCP to control the AI Chat system remotely:

```python
# Python client
import requests

# Connect to MCP server
session = requests.get("http://localhost:8080/sse").json()
session_id = session["sessionId"]

# Execute AI Chat tool via MCP
requests.post(f"http://localhost:8080/messages?sessionId={session_id}", json={
    "jsonrpc": "2.0",
    "id": 1,
    "method": "tools/call",
    "params": {
        "name": "capture_screen",
        "arguments": {"quality": 80}
    }
})
```

#### 2. Multi-Client Coordination

Multiple MCP clients can coordinate via shared state:

```python
# Client 1: Capture screen
screen = mcp_call("capture_screen", {})

# Client 2: Analyze screen
analysis = mcp_call("screen_to_markdown", {"detail_level": "detailed"})

# Client 3: Execute action based on analysis
if "Error" in analysis:
    mcp_call("keyboard_press_key", {"key": "Escape"})
```

#### 3. Automated Workflows

Combine MCP tools for complex automation:

```python
# Workflow: Update system and verify
mcp_call("keyboard_type_text", {"text": "sudo apt update"})
mcp_call("keyboard_press_key", {"key": "Enter"})

# Wait for completion
time.sleep(5)

# Verify completion via screen analysis
screen = mcp_call("screen_to_markdown", {})
if "$" in screen:  # Shell prompt visible
    mcp_call("keyboard_type_text", {"text": "sudo apt upgrade -y"})
    mcp_call("keyboard_press_key", {"key": "Enter"})
```

---

## Performance Optimization

### Screen Capture Caching

The system caches the latest frame to avoid redundant captures:

```cpp
// SharedToolExecutor maintains frame cache
QImage latestFrame;

QJsonObject captureScreen(const QJsonObject &args) {
    if (!latestFrame.isNull() && useCache) {
        return encodeImage(latestFrame);
    }
    latestFrame = CameraManager::instance().getLatestFrame();
    return encodeImage(latestFrame);
}
```

### Differential Analysis

For iterative tasks, use differential analysis instead of full OCR:

```cpp
// Instead of full screen_to_markdown every iteration
QJsonObject diff = SharedToolExecutor::instance().differentialAnalysis({});
// Returns only what changed, much faster
```

### Batch Operations

Group multiple tool calls to reduce overhead:

```json
{
  "tool_calls": [
    {"tool": "type_text", "text": "ls -la"},
    {"tool": "press_key", "keys": "enter"},
    {"tool": "capture_screen"}
  ]
}
```

---

## Troubleshooting

### OpenAI Function Calling Issues

**Problem**: "Empty tool_calls" error

**Solution**: Ensure your model supports native function calling. The system now automatically detects and handles both formats:
- Native format: `message.tool_calls` array
- Legacy format: JSON embedded in `message.content`

### Shared Tool Executor Issues

**Problem**: Tools behave differently in MCP vs AI Chat

**Solution**: Both systems now use `SharedToolExecutor`. If you see inconsistencies:
1. Check that `SharedToolExecutor::instance()` is initialized
2. Verify `CameraManager` is injected: `setCameraManager(cam)`
3. Check logs for `SharedToolExecutor` messages

### Web Search Issues

**Problem**: Web search returns no results

**Solution**:
1. Check provider configuration in Settings
2. Verify network connectivity
3. Try fallback providers (DuckDuckGo, Wikipedia)
4. Check SSL/TLS configuration if using Exa/Parallel

### Cursor Detection Issues

**Problem**: Terminal detection fails

**Solution**:
1. Increase detection samples (5 → 8)
2. Increase detection interval (350ms → 500ms)
3. Lower confidence threshold (0.8 → 0.6)
4. Ensure terminal cursor is visible and blinking

---

## Related Documentation

- [AI Chat Documentation](ai_chat.md) - Complete AI Chat system documentation
- [MCP Server Documentation](MCP_SERVER.md) - MCP server reference
- [Web Search Tool](ai_chat_web_search_tool.md) - Web search implementation details
- [Tools Configuration](ai_chat_tools_tree_refactoring.md) - Tools tree UI implementation
- [Cursor Blink Detection](cursor-blink-detection.md) - Terminal detection algorithm

---

## Summary

The advanced features described in this document provide a robust foundation for:

- **Reliable Tool Execution**: Native OpenAI function calling support
- **Consistent Behavior**: Shared execution layer for MCP and AI Chat
- **Intelligent Automation**: Web search and cursor detection
- **Better UX**: Hierarchical tool configuration
- **Performance**: Optimized screen capture and analysis

These features work together to make Openterface a powerful platform for remote computer control and automation.
