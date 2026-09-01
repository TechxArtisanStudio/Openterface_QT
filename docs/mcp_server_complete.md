# MCP Server — Complete Reference

The Openterface Mini-KVM QT application includes a **Model Context Protocol (MCP) Server**
that exposes keyboard, mouse, and screen capture capabilities as standardized tools. AI
models (Claude, GPT, etc.) and automation clients can control a target computer remotely
through this interface.

This document covers every aspect of the MCP server: architecture, transport modes,
running modes, configuration, the full tool catalog, keyboard/HID internals, screen
capture pipeline, client integration examples, deployment, and troubleshooting.

---

## Table of Contents

1. [Overview](#1-overview)
2. [Architecture](#2-architecture)
3. [Transport Modes](#3-transport-modes)
4. [Running Modes](#4-running-modes)
5. [Prerequisites & Setup](#5-prerequisites--setup)
6. [Configuration](#6-configuration)
7. [MCP Protocol Reference](#7-mcp-protocol-reference)
8. [Tool Catalog](#8-tool-catalog)
9. [Keyboard & HID Internals](#9-keyboard--hid-internals)
10. [Screen Capture Pipeline](#10-screen-capture-pipeline)
11. [Client Integration Examples](#11-client-integration-examples)
12. [Deployment & Operations](#12-deployment--operations)
13. [Logging & Debugging](#13-logging--debugging)
14. [Troubleshooting](#14-troubleshooting)

---

## 1. Overview

### What is MCP?

The **Model Context Protocol** is an open standard (currently at protocol version
`2024-11-05`) that lets AI assistants discover and invoke tools on a host system.
Openterface implements an MCP server that advertises KVM-related tools — mouse control,
keyboard control, screen capture — which any MCP-compatible client can call.

### What Can You Do?

| Capability          | Tools                                              |
|---------------------|----------------------------------------------------|
| Move the cursor     | `mouse_move_absolute`, `mouse_move_relative`       |
| Click               | `mouse_click` (left/right/middle, single/double)   |
| Scroll              | `mouse_scroll` (up/down)                           |
| Type text           | `keyboard_type_text`, `keyboard_press_key`         |
| Send hotkeys        | `keyboard_press_key` with modifiers (Ctrl+C, etc.) |
| Function keys       | `keyboard_function_key` (F1–F12)                   |
| Secure attention    | `keyboard_ctrl_alt_del`                            |
| Switch layout       | `keyboard_set_layout`                              |
| Capture screen      | `capture_screen` (live video, JPEG base64)         |
| Retrieve screenshot | `capture_last_image` (saved file)                  |
| Run scripts         | `execute_script` (AutoHotkey-like syntax)          |
| Query status        | `system_status`                                    |

### Use Cases

- **Remote server management** — control headless machines via KVM over LAN
- **AI-driven automation** — let Claude or other models perform GUI tasks
- **Testing pipelines** — scripted mouse/keyboard input with screen verification
- **Helpdesk / support** — remote troubleshooting without installing agents on the target
- **Embedded / IoT** — interact with devices that only have HDMI + USB

---

## 2. Architecture

### Component Diagram

```
┌─────────────────────────────────────────────────────────────────────┐
│                        Openterface QT App                            │
│                                                                     │
│  ┌──────────────────────────────────────────────────────────────┐  │
│  │                        MCP Server                             │  │
│  │                       (McpServer)                             │  │
│  │                                                               │  │
│  │   ┌───────────┐   ┌──────────────────┐   ┌────────────────┐ │  │
│  │   │  stdio    │   │ McpSseTransport  │   │ Named Pipe     │ │  │
│  │   │ (stdin/   │   │ (QHttpServer)    │   │ (QLocalServer) │ │  │
│  │   │  stdout)  │   │ GET  /sse        │   │ Unix socket /  │ │  │
│  │   │           │   │ POST /messages    │   │ Windows pipe   │ │  │
│  │   └─────┬─────┘   └────────┬─────────┘   └───────┬────────┘ │  │
│  │         │                   │                      │          │  │
│  │         └───────────────────┼──────────────────────┘          │  │
│  │                             │                                  │  │
│  │                   ┌─────────▼──────────┐                       │  │
│  │                   │  McpToolHandler    │                       │  │
│  │                   │  (Tool Registry)   │                       │  │
│  │                   └─────────┬──────────┘                       │  │
│  └─────────────────────────────┼──────────────────────────────────┘  │
│                                │                                     │
│  ┌─────────────────────────────▼──────────────────────────────────┐  │
│  │               Existing Components (Reused)                     │  │
│  │                                                                │  │
│  │  HostManager (Singleton)                                       │  │
│  │    ├─ MouseManager     → HID mouse reports via serial          │  │
│  │    └─ KeyboardManager  → HID keyboard reports via serial       │  │
│  │                                                                │  │
│  │  CameraManager                                                 │  │
│  │    ├─ FFmpegBackendHandler   (Linux/Windows)                   │  │
│  │    └─ GStreamerBackendHandler (Linux alternative)              │  │
│  │                                                                │  │
│  │  ScriptRunner → Lexer → Parser → ScriptExecutor                │  │
│  │  SerialPortManager (CH9329 / CH32V208 protocols)               │  │
│  │  VideoHid (device discovery, firmware, EDID)                   │  │
│  └────────────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────────┘
              ▲                    ▲                     ▲
              │                    │                     │
     ┌────────┴──────┐    ┌───────┴────────┐    ┌──────┴──────┐
     │  Claude Code  │    │  Python/curl   │    │  MCP Client │
     │  (stdio)      │    │  (SSE / HTTP)  │    │  (Named Pipe│
     │               │    │                │    │   / socket) │
     └───────────────┘    └────────────────┘    └─────────────┘
```

### Source File Layout

```
server/mcp/
├── mcpConstants.h        — Protocol version, tool names, default port, limits
├── mcpProtocol.h/cpp     — JSON-RPC 2.0 parsing and response building
├── mcpToolHandler.h/cpp  — Tool registry: listTools() and callTool() dispatch
├── mcpServer.h/cpp       — Top-level server: transport lifecycle, dependency injection
└── mcpSseTransport.h/cpp — SSE HTTP transport: QHttpServer, session mgmt, keepalives
```

### Data Flow (Keyboard Example)

```
MCP Client
    │  POST /messages  {"method":"tools/call","params":{"name":"keyboard_press_key",
    │                                                     "arguments":{"key":65,"modifiers":2}}}
    ▼
McpSseTransport.handleMessages()
    │  parse JSON-RPC, forward to McpServer.handleMessage()
    ▼
McpServer → McpToolHandler.callTool("keyboard_press_key", args)
    │
    ▼
McpToolHandler.toolKeyboardPressKey()
    │  extract key=65 (A), modifiers=2 (Ctrl), autoRelease=true
    │  call HostManager::getInstance().handleKeyboardAction(65, 2, true, 0)
    ▼
HostManager::handleKeyboardAction()
    │  forward to KeyboardManager
    ▼
KeyboardManager::handleKeyboardAction()
    │  map keyCode → HID scancode (A → 0x04)
    │  merge modifiers into combinedModifiers (currentModifiers | 2)
    │  build HID report: keyData[5]=0x04(Ctrl), keyData[7]=0x04(A)
    │  emit SerialPortManager::sendCommandAsync(keyData)
    ▼
SerialPortManager → USB serial → CH9329/CH32V208 chip → target computer
```

---

## 3. Transport Modes

The MCP server supports three transport mechanisms. All share the same `McpToolHandler`
and tool set — only the communication channel differs.

### 3.1 stdio (Standard Input/Output)

| Property        | Value                                              |
|-----------------|----------------------------------------------------|
| Direction       | Bidirectional on stdin/stdout                      |
| Message format  | Newline-delimited JSON-RPC 2.0                     |
| Use case        | Local CLI clients (Claude Code, custom scripts)    |
| Binding         | Process stdin/stdout — no network port             |
| Platform        | Linux, macOS, Windows                              |

**Start:**

```bash
./openterfaceQT --mcp-stdio
```

Or in GUI mode, toggle via Menu → Tools → MCP Server.

**Client config (Claude Code `settings.json`):**

```json
{
  "mcpServers": {
    "openterface": {
      "command": "/home/bot/project/Openterface_QT/build/openterfaceQT",
      "args": ["--mcp-stdio"]
    }
  }
}
```

### 3.2 Named Pipe (QLocalServer)

| Property        | Value                                                        |
|-----------------|--------------------------------------------------------------|
| Direction       | Bidirectional on a local socket                              |
| Message format  | Newline-delimited JSON-RPC 2.0                               |
| Use case        | Local IPC when stdio is occupied by GUI output               |
| Linux path      | `/tmp/openterface-mcp` (Unix domain socket)                  |
| Windows path    | `\\.\pipe\openterface-mcp` (Named Pipe)                      |
| Platform        | Linux, macOS, Windows                                        |

Enabled in GUI mode via the MCP preferences page.

### 3.3 SSE Remote (HTTP)

| Property             | Value                                              |
|----------------------|----------------------------------------------------|
| Direction            | POST for requests, SSE stream for responses        |
| Message format       | JSON-RPC 2.0 over HTTP + Server-Sent Events        |
| Use case             | Remote LAN access from any HTTP client             |
| Default port         | 8080                                               |
| Bind address         | Configurable: `0.0.0.0` (all), `127.0.0.1`, custom |
| Max sessions         | 16 concurrent                                      |
| Keepalive interval   | 15 seconds                                         |
| Session timeout      | 30 minutes of inactivity                           |
| Cleanup interval     | 60 seconds                                         |

**Endpoints:**

| Endpoint            | Method | Description                                        |
|---------------------|--------|----------------------------------------------------|
| `/sse`              | GET    | Open persistent SSE stream; returns `sessionId`    |
| `/messages`         | POST   | Send JSON-RPC request; append `?sessionId=<uuid>`  |

**Start (headless):**

```bash
./openterfaceQT --mcp-sse-port 8080
```

**Start (GUI):** Enable in Preferences → MCP → Transport: SSE HTTP.

---

## 4. Running Modes

### 4.1 GUI Mode (Desktop)

Launch normally — the full Qt window opens. The MCP server can be toggled from the
menu or preferences page. All transports and tools are available, including
`ScriptRunner`-dependent tools (`execute_script`, `keyboard_send_keys`).

```bash
./openterfaceQT
```

**Capabilities:** Full — mouse, keyboard, screen capture, scripts, HID, serial.

### 4.2 Headless SSE Mode (No GUI)

No window is created. A minimal Qt event loop runs with camera, HID, and MCP only.
This is the recommended mode for remote/automated operation.

```bash
./openterfaceQT --mcp-sse-port 8080
```

**Capabilities:** Mouse, keyboard, screen capture, system status.
**Not available:** `execute_script`, `keyboard_send_keys` (require `ScriptRunner`).

### 4.3 Headless stdio Mode (No GUI)

Same as headless SSE but uses stdin/stdout transport for local CLI clients.

```bash
./openterfaceQT --mcp-stdio
```

### 4.4 Comparison Matrix

| Feature                    | GUI Mode    | Headless SSE       | Headless stdio     |
|----------------------------|-------------|--------------------|--------------------|
| Window                     | Yes         | No                 | No                 |
| Mouse tools                | ✅          | ✅                 | ✅                 |
| Keyboard tools             | ✅          | ✅                 | ✅                 |
| Screen capture             | ✅          | ✅                 | ✅                 |
| `execute_script`           | ✅          | ❌                 | ❌                 |
| `keyboard_send_keys`       | ✅          | ❌                 | ❌                 |
| LAN remote access          | ✅ (SSE)    | ✅                 | ❌ (local only)    |
| Claude Code integration    | ❌          | ❌                 | ✅                 |
| Multiple sessions          | ✅ (SSE)    | ✅ (up to 16)      | ❌ (single)        |

---

### 4.5 One Instance Per Device (Parallel, Instead of Switching)

With several Openterface units attached, the usual approach is to run one
instance and switch it between units. A switch is not a pointer update: it tears
down and rebuilds the serial port, the HID handle, the camera and the audio
device. That is real work, and it is where most multi-device trouble lives —
each switch re-enumerates the USB interfaces on the GUI thread, and while that
runs the instance cannot accept a new SSE session, so a client may time out on a
request the server never received.

An alternative is to **run one instance per unit, each bound to its own device
and its own MCP port**, and switch by choosing which port to talk to:

```bash
openterfaceQT --backend ffmpeg --mcp-sse-port 8091 --device 1-3 &
openterfaceQT --backend ffmpeg --mcp-sse-port 8092 --device 1-6 &
openterfaceQT --backend ffmpeg --mcp-sse-port 8093 --device 1-9 &
openterfaceQT --backend ffmpeg --mcp-sse-port 8094 --device 1-12 &
```

Each unit exposes its own device nodes (`/dev/ttyACM*`, `/dev/hidraw*`,
`/dev/video*`), so the single-consumer constraint is per node, not global, and
the instances do not contend.

What this buys:

- **No device switching at all.** No serial close/reopen, no camera teardown, no
  re-enumeration storm — so the failure modes that belong to switching simply do
  not arise.
- **Near-instantaneous unit selection.** Choosing a unit is choosing a TCP port.
- **Simultaneous video from every unit.** All instances stream at once, so you
  can record or watch several targets in parallel rather than one at a time.

Measured on four units on one hub, 20 minutes per configuration, driving all
four consoles continuously (type, capture, verify against the target's own
console): switching produced 7–11 failed operations per ~120–160, while one
instance per unit produced **0 failures in 300 operations** at roughly 2.5x the
throughput. Every switching failure was a refused MCP session during
post-switch re-enumeration.

Costs and caveats:

- **Memory.** Roughly 180–360 MB RSS per instance.
- **Bring the devices up first.** Start the instances only once every unit is
  attached and the bus has settled. A binding survives only while nothing else
  re-enumerates: powering another unit afterwards can renumber device nodes
  underneath a running instance, which then drives a different unit than
  intended, silently.
- **A port chain is not a stable identity.** It can change across
  re-enumeration, so do not treat `--device 1-3` as a durable name for a
  particular physical unit. Verify which unit an instance actually drives — for
  example by typing a marker through it and reading it back on that target —
  rather than trusting the argument.

Note: `--device <port chain>` is proposed in PR #598 (`device_list` /
`device_select` MCP tools and headless `--device`). Without it an instance binds
the first unit it discovers, which is sufficient only when one unit is attached.

## 5. Prerequisites & Setup

### 5.1 Hardware

- Openterface Mini-KVM device connected to the host via USB
- Target computer connected to the KVM's HDMI and USB ports

### 5.2 Software

| Dependency   | Required for         | Install (Fedora)                            |
|--------------|----------------------|---------------------------------------------|
| Qt 6         | Core framework       | `qt6-qtbase-devel`                          |
| Qt HttpServer| SSE transport        | `qt6-qthttpserver-devel`                    |
| Qt SerialPort| HID serial comms     | `qt6-qtserialport-devel`                    |
| FFmpeg       | Video capture        | `ffmpeg-devel`                              |
| GStreamer    | Alt video backend    | `gstreamer1-devel`                          |
| libusb       | USB device access    | `libusb1-devel`                             |
| libudev      | Device enumeration   | `systemd-devel`                             |
| turbojpeg    | JPEG encoding        | `libjpeg-turbo-devel`                       |

### 5.3 HID Device Permissions (Critical)

The USB HID device (`/dev/hidraw*`) is root-only by default. Without fixing this,
**all keyboard and mouse commands silently fail** with `Permission denied`.

**Permanent fix (udev rule — survives reboot):**

```bash
sudo tee /etc/udev/rules.d/99-openterface-hid.rules > /dev/null << 'EOF'
# Openterface HID device — allow all users read/write access
SUBSYSTEM=="hidraw", ATTRS{idVendor}=="1a86", ATTRS{idProduct}=="5573", MODE="0666"
SUBSYSTEM=="hidraw", KERNEL=="hidraw*", MODE="0666"
EOF

sudo udevadm control --reload-rules
sudo udevadm trigger
```

**Temporary fix (lost on reboot):**

```bash
sudo chmod 666 /dev/hidraw*
```

**Verify:**

```bash
ls -la /dev/hidraw4
# Expected: crw-rw-rw- root root ... /dev/hidraw4
```

**Identify the correct device:**

```bash
# Find which hidraw corresponds to the Openterface
udevadm info -a -n /dev/hidraw* | grep -B5 '1a86'
```

### 5.4 Serial Port Permissions

The companion serial port (`/dev/ttyACM0`) also needs access:

```bash
sudo usermod -aG dialout $USER
# Log out and back in for this to take effect
```

Or temporarily:

```bash
sudo chmod 666 /dev/ttyACM0
```

---

## 6. Configuration

### 6.1 Command-Line Arguments

```
openterfaceQT [OPTIONS]
```

| Argument                    | Description                                                |
|-----------------------------|------------------------------------------------------------|
| `--mcp-sse-port <PORT>`     | Enable SSE transport on TCP port (headless mode)           |
| `--mcp-stdio`               | Enable stdio transport (headless mode)                     |
| `--mcp-start`               | Auto-start MCP server on launch                            |
| `--skip-env-check`          | Skip environment sanity check                              |

### 6.2 GUI Preferences Page

In GUI mode, navigate to **Preferences → MCP** to configure:

| Setting                 | Default      | Description                                  |
|-------------------------|--------------|----------------------------------------------|
| Enable MCP Server       | Off          | Master on/off toggle                         |
| Transport Mode          | Stdio        | `Stdio` or `SSE HTTP`                        |
| SSE Port                | 8080         | TCP port for SSE transport                   |
| SSE Bind Address        | `0.0.0.0`    | Network interface (Any, Loopback, Custom)    |
| SSE Path (/sse)         | `/sse`       | SSE endpoint path                            |
| SSE Path (/messages)    | `/messages`  | Messages endpoint path                       |
| Keepalive Interval      | 15s          | SSE keepalive comment interval               |
| Session Timeout         | 1800s        | Idle session timeout before cleanup          |
| Cleanup Interval        | 60s          | Stale session sweep interval                 |
| Max Sessions            | 16           | Maximum concurrent SSE sessions              |

### 6.3 Persisted Settings

All GUI preferences are stored via `GlobalSetting` (QSettings-backed) and survive
restarts. The `mcpSettingsChanged()` signal triggers an MCP server restart with
the new configuration.

---

## 7. MCP Protocol Reference

### 7.1 Transport: SSE

```
Client                                         Openterface SSE Server
  │                                                   │
  │───── GET /sse ─────────────────────────────────►│
  │                                                   │
  │◄──── HTTP 200, Content-Type: text/event-stream ──│
  │                                                   │
  │◄──── event: endpoint ───────────────────────────│
  │      data: {"endpoint":"/messages?sessionId=UUID}│
  │                                                   │
  │      (SSE connection stays open)                  │
  │                                                   │
  │───── POST /messages?sessionId=UUID ────────────►│
  │      Content-Type: application/json               │
  │      {"jsonrpc":"2.0", "id":1, "method":"...",   │
  │       "params":{...}}                             │
  │                                                   │
  │◄──── HTTP 200 {"accepted":true} ────────────────│  (immediate ack)
  │                                                   │
  │◄──── event: message ───────────────────────────│  (async response)
  │      data: {"id":1,"jsonrpc":"2.0","result":...} │
  │                                                   │
  │◄──── :keepalive ────────────────────────────────│  (every 15s)
```

### 7.2 JSON-RPC 2.0 Methods

#### initialize

Handshake. Must be called first.

**Request:**
```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "initialize",
  "params": {
    "protocolVersion": "2024-11-05",
    "capabilities": {"tools": {}},
    "clientInfo": {"name": "my-client", "version": "1.0"}
  }
}
```

**Response:**
```json
{
  "id": 1,
  "jsonrpc": "2.0",
  "result": {
    "capabilities": {"tools": {}},
    "protocolVersion": "2024-11-05",
    "serverInfo": {"name": "openterface-kvm", "version": "1.0.0"}
  }
}
```

#### notifications/initialized

Optional confirmation that the client received the initialize result.

```json
{"jsonrpc": "2.0", "method": "notifications/initialized"}
```

#### tools/list

Return all available tool definitions.

```json
{"jsonrpc": "2.0", "id": 2, "method": "tools/list", "params": {}}
```

Returns an array of tool objects, each with `name`, `description`, and `inputSchema`.

#### tools/call

Invoke a tool by name.

```json
{
  "jsonrpc": "2.0",
  "id": 3,
  "method": "tools/call",
  "params": {
    "name": "mouse_move_absolute",
    "arguments": {"x": 2048, "y": 2048}
  }
}
```

**Success response:**
```json
{
  "id": 3,
  "jsonrpc": "2.0",
  "result": {
    "content": [{"type": "text", "text": "Mouse moved to absolute position (2048, 2048)"}]
  }
}
```

**Error response:**
```json
{
  "id": 3,
  "jsonrpc": "2.0",
  "result": {
    "content": [{"type": "text", "text": "ERROR: <message>"}],
    "isError": true
  }
}
```

#### ping

Keepalive check.

```json
{"jsonrpc": "2.0", "id": 99, "method": "ping"}
```

### 7.3 Error Codes

| Code      | Meaning              |
|-----------|----------------------|
| -32700    | Parse error          |
| -32600    | Invalid request      |
| -32601    | Method not found     |
| -32602    | Invalid params       |
| -32603    | Internal error       |

---

## 8. Tool Catalog

### 8.1 Mouse Tools

#### mouse_move_absolute

Move the cursor to an absolute position on the target screen.

| Parameter | Type    | Required | Range   | Description                |
|-----------|---------|----------|---------|----------------------------|
| `x`       | integer | Yes      | 0–4096  | Horizontal position        |
| `y`       | integer | Yes      | 0–4096  | Vertical position          |

> (0,0) = top-left corner. (4096,4096) = bottom-right corner. This maps to the
> full target screen resolution regardless of its actual pixel dimensions.

```json
{"name": "mouse_move_absolute", "arguments": {"x": 1024, "y": 768}}
```

#### mouse_click

Click the mouse at a given position.

| Parameter | Type    | Required | Default | Description                          |
|-----------|---------|----------|---------|--------------------------------------|
| `x`       | integer | Yes      | —       | X coordinate (0–4096)                |
| `y`       | integer | Yes      | —       | Y coordinate (0–4096)                |
| `button`  | string  | No       | "left"  | "left", "right", or "middle"         |
| `count`   | integer | No       | 1       | Number of clicks (1–10, 2 = double)  |

```json
{"name": "mouse_click", "arguments": {"x": 2048, "y": 2048, "button": "right", "count": 2}}
```

#### mouse_move_relative

Move the cursor relative to its current position.

| Parameter | Type    | Required | Description                            |
|-----------|---------|----------|----------------------------------------|
| `dx`      | integer | Yes      | Horizontal delta (positive = right)    |
| `dy`      | integer | Yes      | Vertical delta (positive = down)       |

```json
{"name": "mouse_move_relative", "arguments": {"dx": 100, "dy": -50}}
```

#### mouse_scroll

Scroll the mouse wheel.

| Parameter   | Type    | Required | Default | Description               |
|-------------|---------|----------|---------|---------------------------|
| `direction` | string  | Yes      | —       | "up" or "down"            |
| `lines`     | integer | No       | 1       | Number of scroll lines    |

```json
{"name": "mouse_scroll", "arguments": {"direction": "down", "lines": 5}}
```

### 8.2 Keyboard Tools

#### keyboard_press_key

Press or release a single key. This is the primary keyboard tool — it handles
individual keys, modifier keys, and combo keys.

| Parameter     | Type    | Required | Default | Description                                     |
|---------------|---------|----------|---------|-------------------------------------------------|
| `key`         | integer | Yes      | —       | Qt key code (e.g., 65 for A, 16777220 for Enter)|
| `modifiers`   | integer | No       | 0       | Modifier bitmask (see below)                    |
| `isKeyDown`   | boolean | No       | true    | true = press, false = release                   |
| `autoRelease` | boolean | No       | true    | Auto-release after press                        |
| `side`        | string  | No       | —       | "left" or "right" for modifier keys             |

**autoRelease behavior:**

- **true (default):** The key is pressed, held for ~80ms, then automatically released.
  Response: `"Key pressed+released"`. Use for normal typing and hotkeys.
- **false:** The key stays physically held down. Response: `"Key pressed"`. Use for
  modifier keys that must persist across multiple key events.

**Modifier bitmask** (values match HID byte directly):

| Bit | Value | Key     |
|-----|-------|---------|
| 0   | 1     | Shift   |
| 1   | 2     | Ctrl    |
| 2   | 4     | Alt     |
| 3   | 8     | Meta/Win|

Combine with bitwise OR:

| Combo         | Value |
|---------------|-------|
| Ctrl          | 2     |
| Shift         | 1     |
| Alt           | 4     |
| Win           | 8     |
| Ctrl+Shift    | 3     |
| Ctrl+Alt      | 6     |
| Ctrl+Alt+Shift| 7     |
| Ctrl+Alt+Win  | 14    |

**Common Qt key codes:**

| Key          | Code     | Notes                                |
|--------------|----------|--------------------------------------|
| A–Z          | 65–90    | Standard ASCII                       |
| 0–9          | 48–57    | Standard ASCII                       |
| Space        | 32       |                                      |
| Enter        | 16777220 | Qt::Key_Return                       |
| Tab          | 16777217 | Qt::Key_Tab                          |
| Backspace    | 16777219 | Qt::Key_Backspace                    |
| Escape       | 16777216 | Qt::Key_Escape                       |
| Delete       | 16777223 | Qt::Key_Delete                       |
| Insert       | 16777222 | Qt::Key_Insert                       |
| Home         | 16777232 | Qt::Key_Home                         |
| End          | 16777233 | Qt::Key_End                          |
| PageUp       | 16777234 | Qt::Key_PageUp                       |
| PageDown     | 16777235 | Qt::Key_PageDown                     |
| Left         | 16777236 | Qt::Key_Left                         |
| Up           | 16777237 | Qt::Key_Up                           |
| Right        | 16777238 | Qt::Key_Right                        |
| Down         | 16777239 | Qt::Key_Down                         |
| CapsLock     | 16777203 | Qt::Key_CapsLock                     |
| NumLock      | 16777215 | Qt::Key_NumLock                      |
| ScrollLock   | 16777214 | Qt::Key_ScrollLock                   |
| PrintScreen  | 16777221 | Qt::Key_Print                        |
| Pause        | 16777224 | Qt::Key_Pause                        |
| Shift        | 16777248 | Qt::Key_Shift                        |
| Ctrl         | 16777249 | Qt::Key_Control                      |
| Alt          | 16777250 | Qt::Key_Alt                          |
| Meta/Win     | 16777251 | Qt::Key_Meta                         |
| F1–F12       | —        | Use `keyboard_function_key` instead  |

**Examples:**

```json
// Type letter A (auto press+release)
{"name": "keyboard_press_key", "arguments": {"key": 65}}

// Ctrl+C (copy)
{"name": "keyboard_press_key", "arguments": {"key": 67, "modifiers": 2}}

// Alt+Tab (switch window)
{"name": "keyboard_press_key", "arguments": {"key": 16777217, "modifiers": 4}}

// Win+R (Run dialog)
{"name": "keyboard_press_key", "arguments": {"key": 82, "modifiers": 8}}

// Ctrl+Shift+T (reopen tab)
{"name": "keyboard_press_key", "arguments": {"key": 84, "modifiers": 3}}

// Hold Shift, type A, B, C, then release Shift
{"name": "keyboard_press_key", "arguments": {"key": 16777248, "autoRelease": false}}
{"name": "keyboard_press_key", "arguments": {"key": 65, "modifiers": 1}}
{"name": "keyboard_press_key", "arguments": {"key": 66, "modifiers": 1}}
{"name": "keyboard_press_key", "arguments": {"key": 67, "modifiers": 1}}
{"name": "keyboard_press_key", "arguments": {"key": 16777248, "isKeyDown": false}}
```

#### keyboard_type_text

Type a string of text on the target computer. The text is sent via clipboard paste.

| Parameter | Type   | Required | Description                       |
|-----------|--------|----------|-----------------------------------|
| `text`    | string | Yes      | Text to type                      |

```json
{"name": "keyboard_type_text", "arguments": {"text": "Hello, World!"}}
```

> This tool works by setting the clipboard content and then sending Ctrl+V. It does
> not work on systems where clipboard paste is disabled or unavailable.

#### keyboard_function_key

Send a function key (F1–F12).

| Parameter | Type   | Required | Description                    |
|-----------|--------|----------|--------------------------------|
| `key`     | string | Yes      | "F1" through "F12"             |

```json
{"name": "keyboard_function_key", "arguments": {"key": "F5"}}
```

#### keyboard_ctrl_alt_del

Send the Ctrl+Alt+Del secure attention sequence. No parameters.

```json
{"name": "keyboard_ctrl_alt_del", "arguments": {}}
```

> This uses the dedicated `sendCtrlAltDel()` function in KeyboardManager which sends
> a properly sequenced multi-report HID command (press Ctrl+Alt, press Del, release all).

#### keyboard_set_layout

Switch the keyboard layout used for scancode mapping.

| Parameter | Type   | Required | Description                                  |
|-----------|--------|----------|----------------------------------------------|
| `layout`  | string | Yes      | Layout name (e.g., "us", "de", "fr", "jp")  |

```json
{"name": "keyboard_set_layout", "arguments": {"layout": "us"}}
```

### 8.3 Screen Capture Tools

#### capture_screen

Capture the current video frame from the target computer. Returns a JPEG image.

| Parameter | Type    | Required | Default | Description                 |
|-----------|---------|----------|---------|-----------------------------|
| `quality` | integer | No       | 90      | JPEG quality (1–100)        |

**Response:**
```json
{
  "content": [
    {
      "type": "image",
      "mimeType": "image/jpeg",
      "data": "/9j/4AAQSkZJRgABAQEAYABgAAD..."
    }
  ]
}
```

> The `data` field contains base64-encoded JPEG. Typical size: 100–400 KB depending
> on screen content and quality setting.

#### capture_last_image

Retrieve the last saved screenshot from the local pictures folder.

No parameters.

### 8.4 Script Tools

#### execute_script

Run an AutoHotkey-like script. Supports commands: `Click`, `MouseMove`, `Send`,
`Sleep`, `Scroll`, `FullScreenCapture`, `AreaScreenCapture`, `SetCapsLockState`,
`SetNumLockState`, `SetScrollLockState`.

| Parameter | Type   | Required | Description                            |
|-----------|--------|----------|----------------------------------------|
| `script`  | string | Yes      | Script text in AHK-like syntax         |

```json
{"name": "execute_script", "arguments": {"script": "MouseMove, 100, 200\nClick\nSleep, 500\nSend, Hello"}}
```

> ⚠️ **GUI mode only.** Returns `"ScriptRunner not available"` in headless mode.

### 8.5 System Tools

#### system_status

Return the current system and device status as JSON.

No parameters.

**Response (text content):**
```json
{
  "all_ready": true,
  "camera": {
    "active": true,
    "backend": "ffmpeg",
    "frame_available": true,
    "frame_width": 1920,
    "frame_height": 1080
  },
  "host": {
    "keyboard_ready": true,
    "mouse_ready": true
  },
  "mcp_server": "running",
  "serial_port": {
    "open": true,
    "ready": true,
    "path": "/dev/ttyACM0"
  }
}
```

---

## 9. Keyboard & HID Internals

### 9.1 USB HID Keyboard Report

The Openterface sends keyboard data over USB serial using the CH9329 protocol.
Each report is 13 bytes:

```
Byte:  0   1   2   3   4   5   6   7   8   9  10  11  12
       ─────────────  ─── ─── ─────────────────────────────
       Header         Mod Res Key[0] Key[1] ... Key[5]

Header = 57 AB 00 02 08  (fixed command prefix)
Mod    = Modifier byte (see below)
Res    = Reserved (always 00)
Key    = Up to 6 simultaneous key scancodes (USB HID usage table)
```

### 9.2 Modifier Byte (Byte 5)

| Bit | Key           |
|-----|---------------|
| 0   | Left Ctrl     |
| 1   | Left Shift    |
| 2   | Left Alt      |
| 3   | Left GUI/Win  |
| 4   | Right Ctrl    |
| 5   | Right Shift   |
| 6   | Right Alt     |
| 7   | Right GUI/Win |

### 9.3 How Combo Keys Work

When you call `keyboard_press_key` with `key=65 (A)` and `modifiers=2 (Ctrl)`:

1. `McpToolHandler.toolKeyboardPressKey()` receives the request
2. It calls `HostManager::handleKeyboardAction(65, 2, true, 0)`
3. `KeyboardManager::handleKeyboardAction()` maps key 65 → HID scancode `0x04` (A)
4. For non-modifier keys, it merges: `combinedModifiers = currentModifiers | modifiers`
   → `combinedModifiers = 0x00 | 0x02 = 0x02` (Left Ctrl)
5. The HID report is built:
   - `keyData[5] = 0x02` (Ctrl modifier)
   - `keyData[7] = 0x04` (A key)
6. On auto-release (80ms later), a second report is sent:
   - `keyData[5] = 0x00` (no modifiers)
   - `keyData[7] = 0x00` (no key)

**Before the fix** (prior to 2025-06-26): step 4 used only `currentModifiers`
which was 0, so the Ctrl was never in the report.

### 9.4 Scancode Mapping

The KeyboardManager uses a **layout-aware keymap** to translate Qt key codes to USB
HID scancodes. The default US layout maps directly:

```
Qt::Key_A (65) → 0x04    Qt::Key_Z (90) → 0x1D
Qt::Key_1 (49) → 0x1E    Qt::Key_0 (48) → 0x27
Qt::Key_Return → 0x28    Qt::Key_Escape → 0x29
Qt::Key_Tab    → 0x2B    Qt::Key_Space  → 0x2C
```

For non-US layouts, the `KeyboardLayoutManager` provides alternative mappings and
a Unicode fallback map for characters not in the base keymap.

### 9.5 Modifier Key Detection

When a modifier key is pressed directly (e.g., `keyboard_press_key` with
`key=16777249` which is Qt::Key_Control), the KeyboardManager enters a special
branch that:

1. Uses `nativeVirtualKey` (if non-zero) to distinguish left vs right modifier
2. Falls back to Qt key code detection for MCP/API calls
3. Updates `currentModifiers` state to track which modifiers are physically held

This is important for the "hold modifier, press multiple keys" pattern.

---

## 10. Screen Capture Pipeline

### 10.1 Architecture

```
Target Computer
    │
    │ HDMI video signal
    ▼
Openterface Hardware
    │
    │ USB Video Class (UVC)
    ▼
/dev/video0 (V4L2)
    │
    ├─ FFmpegBackendHandler (default)
    │    └─ libavformat / libavcodec → MJPEG decode → QImage
    │
    └─ GStreamerBackendHandler (alternative)
         └─ v4l2src → jpegdec → appsink → QImage
    │
    ▼
CameraManager (thread-safe frame buffer)
    │
    ▼
McpToolHandler.toolCaptureScreen()
    │  QImage → JPEG encode (quality parameter) → base64
    ▼
MCP Response (image/jpeg, base64)
```

### 10.2 Startup Sequence

When the app starts, it waits for the camera to produce its first frame:

```
Waiting for camera to produce first frame...
[DEBUG-FFMPEG] Device='/dev/video0', Resolution=1920x1080, Framerate=30
[DEBUG] Camera ready! First frame: 1920x1080
```

If no frame arrives within 5 seconds, a warning is logged and `capture_screen` will
return errors until a frame becomes available.

### 10.3 Backend Selection

| Backend  | Default on     | Configuration                          |
|----------|----------------|----------------------------------------|
| FFmpeg   | Linux, Windows | Auto-selected; uses libavformat        |
| GStreamer| Linux          | Alternative; requires GStreamer 1.x    |

Both backends are compiled with hardware acceleration support (VA-API, NVDEC) when
available.

---

## 11. Client Integration Examples

### 11.1 Claude Code (stdio)

Add to Claude Code's MCP settings:

```json
{
  "mcpServers": {
    "openterface": {
      "command": "/home/bot/project/Openterface_QT/build/openterfaceQT",
      "args": ["--mcp-stdio"]
    }
  }
}
```

Claude will automatically discover all tools and can call them directly.

### 11.2 Claude Desktop (stdio)

Add to `claude_desktop_config.json`:

```json
{
  "mcpServers": {
    "openterface-kvm": {
      "command": "C:\\path\\to\\openterfaceQT.exe",
      "args": ["--mcp-stdio"]
    }
  }
}
```

### 11.3 curl (SSE)

See the [Quick curl Test](#quick-curl-test) section below.

### 11.4 Python Client (SSE)

See the [Python Client](#python-client) section below.

### 11.5 JavaScript / Node.js (SSE)

```javascript
import { EventSource } from "eventsource";
import fetch from "node-fetch";

const SERVER = "http://192.168.100.85:8080";

// 1. Open SSE stream
const es = new EventSource(`${SERVER}/sse`);
let sessionId;

es.addEventListener("endpoint", (e) => {
  const data = JSON.parse(e.data);
  const match = data.endpoint.match(/sessionId=([a-f0-9-]+)/);
  sessionId = match[1];
  console.log("Session:", sessionId);

  // 2. Initialize
  rpc("initialize", {
    protocolVersion: "2024-11-05",
    capabilities: { tools: {} },
    clientInfo: { name: "node-client", version: "1.0" },
  });
});

es.addEventListener("message", (e) => {
  console.log("Response:", JSON.parse(e.data));
});

async function rpc(method, params, id = 1) {
  await fetch(`${SERVER}/messages?sessionId=${sessionId}`, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ jsonrpc: "2.0", id, method, params }),
  });
}

// 3. Use tools after initialization
setTimeout(() => {
  rpc("tools/call", { name: "capture_screen", arguments: { quality: 75 } }, 2);
}, 3000);
```

---

## 12. Deployment & Operations

### 12.1 Running as a Background Service (systemd)

```bash
sudo tee /etc/systemd/system/openterface-sse.service > /dev/null << 'EOF'
[Unit]
Description=Openterface Mini-KVM MCP SSE Server
After=network.target
Wants=network.target

[Service]
Type=simple
User=bot
Group=bot
ExecStart=/home/bot/project/Openterface_QT/build/openterfaceQT --mcp-sse-port 8080
Restart=on-failure
RestartSec=5
StandardOutput=journal
StandardError=journal
SyslogIdentifier=openterface

# Security hardening
NoNewPrivileges=true
ProtectSystem=strict
ReadWritePaths=/tmp /home/bot

[Install]
WantedBy=multi-user.target
EOF

sudo systemctl daemon-reload
sudo systemctl enable --now openterface-sse

# Check status
sudo systemctl status openterface-sse
journalctl -u openterface-sse -f
```

### 12.2 Running with nohup

```bash
cd /home/bot/project/Openterface_QT/build
nohup ./openterfaceQT --mcp-sse-port 8080 > /tmp/openterface_sse.log 2>&1 &
echo $! > /tmp/openterface.pid

# Stop later:
kill $(cat /tmp/openterface.pid)
```

### 12.3 Running with screen/tmux

```bash
screen -dmS openterface bash -c \
  'cd ~/project/Openterface_QT/build && ./openterfaceQT --mcp-sse-port 8080'

# Reattach: screen -r openterface
# Detach:   Ctrl-A, D
```

### 12.4 Firewall Configuration

If the server is behind a firewall, allow TCP 8080:

```bash
# firewalld (Fedora/RHEL)
sudo firewall-cmd --permanent --add-port=8080/tcp
sudo firewall-cmd --reload

# iptables
sudo iptables -A INPUT -p tcp --dport 8080 -j ACCEPT

# ufw (Ubuntu)
sudo ufw allow 8080/tcp
```

### 12.5 Health Check

```bash
# Is the process running?
ps aux | grep openterfaceQT | grep -v grep

# Is the port listening?
ss -tlnp | grep 8080

# Can we connect?
curl -s http://localhost:8080/sse -N --max-time 3 | head -5

# Is the camera working?
curl -s http://localhost:8080/sse -N --max-time 3 | grep 'sessionId' | \
  xargs -I{} curl -s -X POST "http://localhost:8080/messages?{}" \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":1,"method":"tools/call",
       "params":{"name":"system_status","arguments":{}}}'
```

### 12.6 Log Rotation

For nohup mode, add a cron job:

```bash
# /etc/cron.daily/openterface-log
#!/bin/bash
LOG=/tmp/openterface_sse.log
if [ -f "$LOG" ] && [ $(stat -c%s "$LOG") -gt 10485760 ]; then  # > 10MB
  mv "$LOG" "${LOG}.1"
  touch "$LOG"
fi
```

---

## 13. Logging & Debugging

### 13.1 Log Output

| Source             | Destination            | Content                                       |
|--------------------|------------------------|-----------------------------------------------|
| qInfo/qWarning     | stdout/stderr          | General application messages                  |
| qCDebug(log_keyboard)| stderr (debug builds)| Detailed keyboard processing                  |
| `[MCP-DIAG]`       | stderr                 | MCP tool handler entry points                 |
| `[KB-DIAG]`        | stderr                 | HID report bytes sent to serial               |
| `[DEBUG-HID]`      | stderr                 | HID device open/close events                  |
| `[DEBUG-FFMPEG]`   | stderr                 | Video capture backend events                  |

### 13.2 Key Log Messages

```
# Successful startup
MCP SSE transport on port 8080
Camera ready! First frame: 1920x1080

# HID device issue
[DEBUG-HID] open failed: /dev/hidraw4, error: Permission denied

# HID device working
[DEBUG-HID] Device opened successfully, fd=29

# MCP tool called
[MCP-DIAG] toolKeyboardPressKey: keyCode=65 (0x41) modifiers=2 (0x2) ...

# HID report sent
[KB-DIAG] Sending HID report: [57 ab 00 02 08 02 00 04 00 00 00 00 00]
          combinedModifiers=0x2 mappedKeyCode=0x4 isKeyDown=1
```

### 13.3 Verifying HID Reports

To confirm the correct data is being sent, check the `[KB-DIAG]` lines:

```bash
# Watch live HID reports
tail -f /tmp/openterface_sse.log | grep 'KB-DIAG'
```

Example for Ctrl+A:
```
[KB-DIAG] Sending HID report: [57 ab 00 02 08 02 00 04 00 00 00 00 00]
          combinedModifiers=0x2 mappedKeyCode=0x4 isKeyDown=1
          └─ Byte 5 = 0x02 → Left Ctrl ✓
          └─ Byte 7 = 0x04 → Key A ✓
```

---

## 14. Troubleshooting

### Problem: HID Permission Denied

**Symptom:**
```
[DEBUG-HID] open failed: /dev/hidraw4, error: Permission denied
```

**Fix:** Apply the udev rule (see §5.3), then restart the app.

---

### Problem: Combo Keys Don't Work (Ctrl+C, Alt+Tab)

**Symptom:** The target computer doesn't respond to Ctrl+C or other modifier combos.

**Fix:** Update to the latest code. Prior to 2025-06-26, the `KeyboardManager` ignored
the `modifiers` parameter for non-modifier keys, so the HID report's modifier byte
was always 0. Verify the fix is working by checking `[KB-DIAG]` logs — byte 5 should
be non-zero for combo keys.

---

### Problem: Key Stays Pressed (No Release)

**Symptom:** After calling `keyboard_press_key`, the key appears held down on the
target computer (e.g., "aaaaaa..." in a text editor).

**Fix:** This was the original behavior before the `autoRelease` parameter was added.
With the fix, `autoRelease` defaults to `true`, so keys are released automatically
after ~80ms. To keep the old behavior (manual control), pass `autoRelease: false`.

---

### Problem: No SSE Response

**Symptom:** POST returns `{"accepted":true}` but no `event: message` on the stream.

**Checks:**
1. Ensure the SSE connection is still open (don't close the listener)
2. Verify the `sessionId` matches the one from `GET /sse`
3. Sessions expire after 30 minutes of inactivity — open a new one
4. Check if `initialize` was called first

---

### Problem: capture_screen Returns Black Image

**Symptom:** Valid JPEG but all black.

**Cause:** Camera hasn't produced a frame yet, or target is not connected.

**Fix:** Wait 5–10 seconds after startup. Check logs for:
```
Camera ready! First frame: 1920x1080
```

---

### Problem: ScriptRunner Not Available

**Symptom:** `execute_script` or `keyboard_send_keys` returns error.

**Cause:** Headless mode doesn't inject `ScriptRunner`. This requires a GUI instance.

**Workaround:** Use `keyboard_press_key` for individual keys or `keyboard_type_text`
for typing. Both work in headless mode.

---

### Problem: Connection Refused from LAN

**Symptom:** `curl: (7) Failed to connect to 192.168.100.85:8080`

**Checks:**
1. Verify bind address is `0.0.0.0`: `ss -tlnp | grep 8080`
2. Check firewall rules (see §12.4)
3. Verify both machines are on the same subnet

---

### Problem: App Crashes on Startup

**Symptom:** Process exits immediately.

**Checks:**
1. Ensure Qt 6 runtime libraries are installed
2. Run manually (without nohup) to see the error:
   ```bash
   ./openterfaceQT --mcp-sse-port 8080
   ```
3. Check if the Openterface USB device is connected:
   ```bash
   lsusb | grep 1a86
   ```

---

*Last updated: 2025-06-26*
