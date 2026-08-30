# Openterface MCP Server — Complete Reference

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
3. [Prerequisites & Setup](#3-prerequisites--setup)
4. [Installation & Build](#4-installation--build)
5. [Transport Modes](#5-transport-modes)
6. [Running Modes](#6-running-modes)
7. [Configuration](#7-configuration)
8. [MCP Protocol Reference](#8-mcp-protocol-reference)
9. [Tool Catalog](#9-tool-catalog)
10. [Keyboard & HID Internals](#10-keyboard--hid-internals)
11. [Screen Capture Pipeline](#11-screen-capture-pipeline)
12. [Client Configuration](#12-client-configuration)
13. [Skill: Visual Feedback Loop](#13-skill-visual-feedback-loop)
14. [Testing Examples](#14-testing-examples)
15. [Deployment & Operations](#15-deployment--operations)
16. [Logging & Debugging](#16-logging--debugging)
17. [Troubleshooting](#17-troubleshooting)
18. [Quick Reference Card](#18-quick-reference-card)
19. [File Structure](#19-file-structure)
20. [Security Considerations](#20-security-considerations)
21. [Thread Safety](#21-thread-safety)
22. [Performance Notes](#22-performance-notes)
23. [Future Extensions](#23-future-extensions)
24. [License & Support](#24-license--support)

---

## 1. Overview

### What is MCP?

The **Model Context Protocol** is an open standard (currently at protocol version
`2024-11-05`) that lets AI assistants discover and invoke tools on a host system.
Openterface implements an MCP server that advertises KVM-related tools — mouse control,
keyboard control, screen capture — which any MCP-compatible client can call.

The MCP server supports **three transport modes**:

| Transport | Description |
|-----------|-------------|
| **stdio** | Standard input/output transport (for CLI-based MCP clients like Claude Code) |
| **Named Pipe (QLocalServer)** | Unix domain socket / Windows named pipe (for local IPC) |
| **SSE (Server-Sent Events)** | HTTP-based transport (for remote/network-based MCP clients) |

All transports share the same `McpToolHandler` and expose identical tools, ensuring
consistent functionality regardless of the transport mode.

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
| Screen to Markdown  | `screen_to_markdown` (OCR-based text + UI element detection) |
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

## 3. Prerequisites & Setup

### 3.1 Hardware

- Openterface Mini-KVM device connected to the host via USB
- Target computer connected to the KVM's HDMI and USB ports

### 3.2 Software

| Dependency   | Required for         | Install (Fedora)                            | Install (Ubuntu/Debian)                   |
|--------------|----------------------|---------------------------------------------|-------------------------------------------|
| Qt 6         | Core framework       | `qt6-qtbase-devel`                          | `qt6-base-dev`                            |
| Qt HttpServer| SSE transport        | `qt6-qthttpserver-devel`                    | `libqt6httpserver6-dev`                   |
| Qt SerialPort| HID serial comms     | `qt6-qtserialport-devel`                    | `libqt6serialport6-dev`                   |
| FFmpeg       | Video capture        | `ffmpeg-devel`                              | `libavcodec-dev libavformat-dev`          |
| GStreamer    | Alt video backend    | `gstreamer1-devel`                          | `libgstreamer1.0-dev`                     |
| libusb       | USB device access    | `libusb1-devel`                             | `libusb-1.0-0-dev`                        |
| libudev      | Device enumeration   | `systemd-devel`                             | `libudev-dev`                             |
| turbojpeg    | JPEG encoding        | `libjpeg-turbo-devel`                       | `libturbojpeg0-dev`                       |
| Tesseract OCR| `screen_to_markdown` | `tesseract tesseract-devel leptonica-devel` | `tesseract-ocr libtesseract-dev libleptonica-dev` |

### 3.3 HID Device Permissions (Critical)

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

### 3.4 Serial Port Permissions

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

## 4. Installation & Build

### Prerequisites

- Windows 10/11 or Linux
- Qt 6.x development environment
- CMake 3.16 or higher
- C++17 compatible compiler

### Build the Project

```bash
# Clone the repository
git clone https://github.com/TechxArtisanStudio/Openterface_QT.git
cd Openterface_QT

# Build with CMake
mkdir build && cd build
cmake ..
cmake --build . --config Release
```

The executable will be located at:
- Windows: `build/openterfaceQT.exe`
- Linux: `build/openterfaceQT`

---

## 5. Transport Modes

The MCP server supports three transport mechanisms. All share the same `McpToolHandler`
and tool set — only the communication channel differs.

### 5.1 stdio (Standard Input/Output)

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

### 5.2 Named Pipe (QLocalServer)

| Property        | Value                                                        |
|-----------------|--------------------------------------------------------------|
| Direction       | Bidirectional on a local socket                              |
| Message format  | Newline-delimited JSON-RPC 2.0                               |
| Use case        | Local IPC when stdio is occupied by GUI output               |
| Linux path      | `/tmp/openterface-mcp` (Unix domain socket)                  |
| Windows path    | `\\.\pipe\openterface-mcp` (Named Pipe)                      |
| Platform        | Linux, macOS, Windows                                        |

Enabled in GUI mode via the MCP preferences page, or headless:

```bash
./openterfaceQT --mcp-pipe
```

**Example with socat (Linux):**

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

### 5.3 SSE Remote (HTTP)

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

Optional arguments:
- `--mcp-sse-bind <address>` — Bind to specific address (default: 127.0.0.1)
- `--mcp-sse-bind-any` — Bind to 0.0.0.0 (all interfaces, use with caution)

**Start (GUI):** Enable in Preferences → MCP → Transport: SSE HTTP.

### 5.4 Combined Transport Modes

You can run multiple transports simultaneously:

```bash
# stdio + SSE
./openterfaceQT --mcp-stdio --mcp-sse-port 8080 --skip-env-check

# All three transports
./openterfaceQT --mcp-stdio --mcp-pipe --mcp-sse-port 8080 --skip-env-check
```

---

## 6. Running Modes

### 6.1 GUI Mode (Desktop)

Launch normally — the full Qt window opens. The MCP server can be toggled from the
menu or preferences page. All transports and tools are available, including
`ScriptRunner`-dependent tools (`execute_script`, `keyboard_send_keys`).

```bash
./openterfaceQT
```

**Capabilities:** Full — mouse, keyboard, screen capture, scripts, HID, serial.

### 6.2 Headless SSE Mode (No GUI)

No window is created. A minimal Qt event loop runs with camera, HID, and MCP only.
This is the recommended mode for remote/automated operation.

```bash
./openterfaceQT --mcp-sse-port 8080
```

**Capabilities:** Mouse, keyboard, screen capture, system status.
**Not available:** `execute_script`, `keyboard_send_keys` (require `ScriptRunner`).

### 6.3 Headless stdio Mode (No GUI)

Same as headless SSE but uses stdin/stdout transport for local CLI clients.

```bash
./openterfaceQT --mcp-stdio
```

### 6.4 Comparison Matrix

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

## 7. Configuration

### 7.1 Command-Line Arguments

```
openterfaceQT [OPTIONS]
```

| Argument                    | Description                                                |
|-----------------------------|------------------------------------------------------------|
| `--backend <NAME>`          | Select media backend (`ffmpeg`, `mediafoundation`, `qt`, `gstreamer`, `qtmultimedia`). Persisted to settings. |
| `--list-backends`           | Print available media backends for the current platform and exit. |
| `--mcp-stdio`               | Enable stdio transport (headless mode)                     |
| `--mcp-pipe`                | Enable Named Pipe transport mode                           |
| `--mcp-sse-port <PORT>`     | Enable SSE transport on TCP port (headless mode)           |
| `--mcp-sse-bind <address>`  | Bind SSE to specific address (default: 127.0.0.1)          |
| `--mcp-sse-bind-any`        | Bind SSE to 0.0.0.0 (all interfaces)                       |
| `--mcp-start`               | Auto-start MCP server on launch (GUI mode)                 |
| `--skip-env-check`          | Skip environment sanity check                              |

**Media backend notes**:

- `--backend` overrides the stored preference **and** persists the choice for subsequent launches. For a one-shot override, set via Settings UI and restore afterwards, or edit the `video/mediaBackend` key in the registry/config directly.
- On Windows, available backends are `ffmpeg` (default, DirectShow), `mediafoundation` (native Windows Media Foundation), and `qt` (Qt Multimedia). On Linux, `ffmpeg` (V4L2) and `gstreamer`.
- `--backend` is ignored in headless MCP mode (`--mcp-stdio` / `--mcp-sse-port`): the headless path reads the stored backend directly. Set the backend first with a GUI launch or by editing the setting, then start in headless mode.
- Unknown backend names fall back to FFmpeg with a warning.

### 7.2 GUI Preferences Page

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

### 7.3 Persisted Settings

All GUI preferences are stored via `GlobalSetting` (QSettings-backed) and survive
restarts. The `mcpSettingsChanged()` signal triggers an MCP server restart with
the new configuration.

---

## 8. MCP Protocol Reference

### 8.1 SSE Protocol Flow

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

### 8.2 JSON-RPC 2.0 Methods

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

### 8.3 Error Codes

| Code      | Meaning              |
|-----------|----------------------|
| -32700    | Parse error          |
| -32600    | Invalid request      |
| -32601    | Method not found     |
| -32602    | Invalid params       |
| -32603    | Internal error       |

---

## 9. Tool Catalog

### 9.1 Mouse Tools

#### mouse_move_absolute

Move the cursor to an absolute position on the target screen.

| Parameter | Type    | Required | Range   | Description                |
|-----------|---------|----------|---------|----------------------------|
| `x`       | integer | Yes      | 0–4096  | Horizontal position        |
| `y`       | integer | Yes      | 0–4096  | Vertical position          |

> (0,0) = top-left corner. (4096,4096) = bottom-right corner. This maps to the
> full target screen resolution regardless of its actual pixel dimensions.
> Formula: `mcp_x = pixel_x / screen_width * 4096`

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

### 9.2 Keyboard Tools

#### keyboard_press_key

Press or release a single key. This is the primary keyboard tool — it handles
individual keys, modifier keys, and combo keys. Accepts either a Qt key code
(integer) or a key name (string).

| Parameter     | Type            | Required | Default | Description                                     |
|---------------|-----------------|----------|---------|-------------------------------------------------|
| `key`         | string/integer  | Yes      | —       | Key identifier (see below)                      |
| `modifiers`   | integer         | No       | 0       | Modifier bitmask (see below)                    |
| `isKeyDown`   | boolean         | No       | true    | true = press, false = release                   |
| `autoRelease` | boolean         | No       | true    | Auto-release after press                        |
| `side`        | string          | No       | —       | "left" or "right" for modifier keys             |

**`key` parameter** accepts:
- A key name string: `"Enter"`, `"Escape"`, `"Tab"`, `"Backspace"`, `"Delete"`, `"Space"`, `"Up"`, `"Down"`, `"Left"`, `"Right"`, `"Home"`, `"End"`, `"PageUp"`, `"PageDown"`, `"Insert"`, `"F1"` through `"F15"`, `"Shift"`, `"Control"` (or `"Ctrl"`), `"Alt"`, `"AltGr"`, `"Meta"` (or `"Super"`, `"Win"`)
- A single letter: `"A"` through `"Z"`
- A single digit: `"0"` through `"9"`
- A Qt key code integer (e.g., `16777220` for Enter, `65` for A)

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
{"key": "Escape"}
{"key": "Tab"}
{"key": "Enter"}
{"key": "Control", "autoRelease": false}
{"key": "F4", "modifiers": 4}
{"key": 16777220}
```

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

#### keyboard_send_keys

Send key combination using AutoHotKey syntax.

| Parameter | Type   | Required | Description                            |
|-----------|--------|----------|----------------------------------------|
| `keys`    | string | Yes      | Key combination in AHK syntax (e.g., "^c" for Ctrl+C) |

> ⚠️ **GUI mode only.** Returns error in headless mode.

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
| `layout`  | string | Yes      | Layout name (e.g., "en-us", "de-de", "us", "fr", "jp") |

```json
{"name": "keyboard_set_layout", "arguments": {"layout": "en-us"}}
```

### 9.3 Screen Capture Tools

#### capture_screen

Capture the current video frame from the target computer. Returns a JPEG image.

| Parameter | Type    | Required | Default | Description                 |
|-----------|---------|----------|---------|-----------------------------|
| `quality` | integer | No       | 80      | JPEG quality (1–100)        |

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

**Example:**
```json
{
  "jsonrpc": "2.0",
  "id": 3,
  "method": "tools/call",
  "params": {
    "name": "capture_screen",
    "arguments": {"quality": 80}
  }
}
```

#### capture_last_image

Retrieve the last saved screenshot from the local pictures folder.

No parameters. Returns base64-encoded image.

#### screen_to_markdown

Capture the target screen and convert it to a structured Markdown representation with OCR-detected text and UI element locations. This tool helps AI agents understand screen content and find clickable elements without needing vision capabilities.

**Prerequisites:** Tesseract OCR must be installed on the system (see §3.2).

**Parameters:**

| Parameter      | Type   | Required | Default      | Description                                      |
|----------------|--------|----------|--------------|--------------------------------------------------|
| `detail_level` | string | No       | `"detailed"` | `"basic"` (only interactive) or `"detailed"` (full) |

**Returns:** Markdown-formatted text containing:
- Screen dimensions
- Detected text elements with MCP coordinates (0-4096 range)
- Interactive elements (buttons, menus) with clickable coordinates
- Confidence scores for OCR detections

**Example (basic mode):**
```json
{
  "jsonrpc": "2.0",
  "id": 10,
  "method": "tools/call",
  "params": {
    "name": "screen_to_markdown",
    "arguments": {"detail_level": "basic"}
  }
}
```

**Example Response (basic mode):**
```markdown
# Screen Layout (1920x1080)

## Interactive Elements
- **[Restart]** at (2300, 2640) - BUTTON
- **[Cancel]** at (1800, 2640) - BUTTON
```

**Example Response (detailed mode):**
```markdown
# Screen Layout (1920x1080)

## Detected Text Elements
| Text | MCP Coordinates | Confidence |
|------|----------------|------------|
| Restart | (2300, 2640) | 0.95 |
| Cancel | (1800, 2640) | 0.93 |
| The system will restart in 60 seconds | (2048, 2200) | 0.88 |

## Interactive Elements
### BUTTON
- **[Restart]** at (2300, 2640)
- **[Cancel]** at (1800, 2640)

### TEXT
- **[The system will restart in 60 seconds]** at (2048, 2200)

## Quick Reference
Use these coordinates with `mouse_click` tool:
| Element | Coordinates (x, y) |
|---------|-------------------|
| Restart | (2300, 2640) |
| Cancel | (1800, 2640) |
```

**Use Case:** This tool is particularly useful for AI agents that need to:
- Find specific buttons or menu items without trial-and-error
- Understand the current screen state before taking actions
- Navigate complex UIs efficiently
- Work without vision capabilities

> **Note:** Coordinates are in the MCP range (0-4096). Use directly with `mouse_click` and other mouse tools.

### 9.4 Script Tools

#### execute_script

Run an AutoHotkey-like script. Supports commands: `Click`, `MouseMove`, `Send`,
`Sleep`, `Scroll`, `FullScreenCapture`, `AreaScreenCapture`, `SetCapsLockState`,
`SetNumLockState`, `SetScrollLockState`.

| Parameter | Type   | Required | Description                            |
|-----------|--------|----------|----------------------------------------|
| `script`  | string | Yes      | Script text in AHK-like syntax         |

```json
{
  "jsonrpc": "2.0",
  "id": 4,
  "method": "tools/call",
  "params": {
    "name": "execute_script",
    "arguments": {
      "script": "MouseMove, 100, 200\nClick\nSleep, 500\nSend, Hello"
    }
  }
}
```

> ⚠️ **GUI mode only.** Returns `"ScriptRunner not available"` in headless mode.

### 9.5 System Tools

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

## 10. Keyboard & HID Internals

### 10.1 USB HID Keyboard Report

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

### 10.2 Modifier Byte (Byte 5)

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

### 10.3 How Combo Keys Work

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

### 10.4 Scancode Mapping

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

### 10.5 Modifier Key Detection

When a modifier key is pressed directly (e.g., `keyboard_press_key` with
`key=16777249` which is Qt::Key_Control), the KeyboardManager enters a special
branch that:

1. Uses `nativeVirtualKey` (if non-zero) to distinguish left vs right modifier
2. Falls back to Qt key code detection for MCP/API calls
3. Updates `currentModifiers` state to track which modifiers are physically held

This is important for the "hold modifier, press multiple keys" pattern.

---

## 11. Screen Capture Pipeline

### 11.1 Architecture

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

### 11.2 Startup Sequence

When the app starts, it waits for the camera to produce its first frame:

```
Waiting for camera to produce first frame...
[DEBUG-FFMPEG] Device='/dev/video0', Resolution=1920x1080, Framerate=30
[DEBUG] Camera ready! First frame: 1920x1080
```

If no frame arrives within 5 seconds, a warning is logged and `capture_screen` will
return errors until a frame becomes available.

### 11.3 Backend Selection

| Backend  | Default on     | Configuration                          |
|----------|----------------|----------------------------------------|
| FFmpeg   | Linux, Windows | Auto-selected; uses libavformat        |
| GStreamer| Linux          | Alternative; requires GStreamer 1.x    |

Both backends are compiled with hardware acceleration support (VA-API, NVDEC) when
available.

---

## 12. Client Configuration

> **Note:** Replace placeholders with your actual values:
> - `<your-server-ip>` — The IP address of the machine running the Openterface server (e.g., `192.168.1.100`)
> - `/path/to/openterfaceQT` — The actual path where openterfaceQT is installed
> - `8080` — The port number configured in your Openterface MCP settings

### 12.1 Quick Start — Which Mode?

| Your Client          | Transport    | Server / URL                                              |
|----------------------|--------------|-----------------------------------------------------------|
| **Claude Code**      | **SSE** ✨   | `http://<host-ip>:8080/sse`                               |
| **Claude Desktop**   | **SSE** ✨   | `http://<host-ip>:8080/sse`                               |
| **Cursor**           | **SSE** ✨   | `http://<host-ip>:8080/sse`                               |
| **Windsurf**         | **SSE** ✨   | `http://<host-ip>:8080/sse`                               |
| **Cline**            | **SSE** ✨   | `http://<host-ip>:8080/sse`                               |
| **Continue**         | **SSE** ✨   | `http://<host-ip>:8080/sse`                               |
| Custom Python client | **SSE** ✨   | `http://<host-ip>:8080/sse`                               |
| Custom Node client   | **SSE** ✨   | `http://<host-ip>:8080/sse`                               |
| Claude Code (local)  | stdio        | `openterfaceQT --mcp-stdio`                               |

> ✨ = **Recommended** — SSE mode lets you run the server once on the KVM host
> and connect from any client on the LAN. All clients can use it simultaneously.

#### stdio vs SSE — Which Should I Use?

| | **SSE (Recommended)** | **stdio** |
|---|---|---|
| **Server lifecycle** | Runs once as a daemon | Launched per-client by the client |
| **Network** | LAN / remote access | Local machine only |
| **Multi-client** | ✅ Up to 16 simultaneous clients | ❌ One client at a time |
| **Claude Code** | ✅ Full support | ✅ Full support |
| **Claude Desktop** | ✅ Full support | ✅ Full support |
| **Cursor / Windsurf / Cline** | ✅ Full support | ⚠️ Varies by client |
| **AI Agent frameworks** | ✅ Any MCP SDK client | ❌ Must be on same machine |

### 12.2 Claude Code (CLI)

#### Option A — SSE (Recommended, for remote/LAN server)

First, start the server on the KVM host:

```bash
./openterfaceQT --mcp-sse-port 8080
```

Then configure Claude Code:

```bash
claude mcp add --transport sse openterface http://<your-server-ip>:8080/sse
```

Or manually edit `~/.claude/settings.json`:

```json
{
  "mcpServers": {
    "openterface": {
      "type": "sse",
      "url": "http://<your-server-ip>:8080/sse"
    }
  }
}
```

#### Option B — stdio (for local same-machine use)

```bash
claude mcp add openterface -- /path/to/openterfaceQT --mcp-stdio
```

Or manually edit `~/.claude/settings.json`:

```json
{
  "mcpServers": {
    "openterface": {
      "command": "/path/to/openterfaceQT",
      "args": ["--mcp-stdio"]
    }
  }
}
```

**Project config** (applies to current project only) — edit `.claude/settings.json`:

```json
{
  "mcpServers": {
    "openterface": {
      "command": "/path/to/openterfaceQT",
      "args": ["--mcp-stdio"]
    }
  }
}
```

**Environment Variables (Optional):**

```json
{
  "mcpServers": {
    "openterface": {
      "command": "/path/to/openterfaceQT",
      "args": ["--mcp-stdio"],
      "env": {
        "QT_QPA_PLATFORM": "xcb",
        "DISPLAY": ":0"
      }
    }
  }
}
```

**Verify:**

```bash
claude
# In the Claude Code prompt:
/status
# You should see "openterface" listed with tool count
```

**File Paths by OS:**

| OS      | Global Config Path                |
|---------|-----------------------------------|
| Linux   | `~/.claude/settings.json`         |
| macOS   | `~/.claude/settings.json`         |
| Windows | `%APPDATA%\Claude\settings.json`  |

### 12.3 Claude Desktop

#### Option A — SSE (Recommended)

Start the server on the KVM host:

```bash
./openterfaceQT --mcp-sse-port 8080
```

Edit the Claude Desktop config:

| OS      | Config File Path                                                  |
|---------|-------------------------------------------------------------------|
| macOS   | `~/Library/Application Support/Claude/claude_desktop_config.json` |
| Windows | `%APPDATA%\Claude\claude_desktop_config.json`                     |
| Linux   | `~/.config/Claude/claude_desktop_config.json`                     |

```json
{
  "mcpServers": {
    "openterface": {
      "type": "sse",
      "url": "http://<your-server-ip>:8080/sse"
    }
  }
}
```

#### Option B — stdio (local same-machine)

```json
{
  "mcpServers": {
    "openterface": {
      "command": "/path/to/openterfaceQT",
      "args": ["--mcp-stdio"]
    }
  }
}
```

> **Important (stdio only):** `command` must be an absolute path. Claude Desktop does not
> resolve `~` or `$HOME`.

**Windows Example:**

```json
{
  "mcpServers": {
    "openterface": {
      "command": "C:\\path\\to\\openterfaceQT.exe",
      "args": ["--mcp-stdio"]
    }
  }
}
```

**Restart Claude Desktop** after editing the config (fully quit, not just close window).
You should see a 🔌 (plug) icon indicating MCP servers are connected.

### 12.4 Cursor

**Prerequisites:** Openterface server running with SSE enabled.

1. Open Cursor Settings → **Cursor Settings** → **Features** → **MCP Servers**
2. Click **+ Add New MCP Server**
3. Fill in:

| Field           | Value                                           |
|-----------------|-------------------------------------------------|
| Type            | `sse`                                           |
| Name            | `openterface`                                   |
| Server URL      | `http://<your-server-ip>:8080/sse`               |

4. Click **Add**

**Config File Alternative:**

| OS      | Config File Path          |
|---------|---------------------------|
| macOS   | `~/.cursor/mcp.json`      |
| Windows | `%USERPROFILE%\.cursor\mcp.json` |
| Linux   | `~/.cursor/mcp.json`      |

```json
{
  "mcpServers": {
    "openterface": {
      "type": "sse",
      "url": "http://<your-server-ip>:8080/sse"
    }
  }
}
```

### 12.5 Windsurf (Codeium)

**Prerequisites:** Openterface server running with SSE.

1. Open Windsurf Settings → **Tools** → **MCP Servers**
2. Click **Add Server**
3. Select **SSE** as the transport type
4. Enter the URL: `http://<your-server-ip>:8080/sse`
5. Save

**Config File Alternative:**

| OS      | Config File Path                                  |
|---------|---------------------------------------------------|
| macOS   | `~/.codeium/windsurf/mcp_config.json`             |
| Windows | `%USERPROFILE%\.codeium\windsurf\mcp_config.json` |
| Linux   | `~/.codeium/windsurf/mcp_config.json`             |

```json
{
  "mcpServers": {
    "openterface": {
      "serverUrl": "http://<your-server-ip>:8080/sse"
    }
  }
}
```

### 12.6 Cline (VS Code)

1. Open VS Code → Cline sidebar
2. Click the **Settings** (gear) icon
3. Scroll to **MCP Servers**
4. Click **Edit MCP Settings**

```json
{
  "mcpServers": {
    "openterface": {
      "type": "sse",
      "url": "http://<your-server-ip>:8080/sse",
      "autoApprove": []
    }
  }
}
```

Or edit `~/.cline/mcp_settings.json` directly.

**stdio Mode:**

```json
{
  "mcpServers": {
    "openterface": {
      "command": "/path/to/openterfaceQT",
      "args": ["--mcp-stdio"],
      "autoApprove": []
    }
  }
}
```

**Auto-Approve Tools (Optional):**

```json
{
  "mcpServers": {
    "openterface": {
      "type": "sse",
      "url": "http://<your-server-ip>:8080/sse",
      "autoApprove": [
        "mouse_move_absolute",
        "mouse_click",
        "keyboard_press_key",
        "keyboard_type_text",
        "capture_screen"
      ]
    }
  }
}
```

> ⚠️ **Security note:** Auto-approving keyboard/mouse tools gives the AI full control
> over the target computer. Only enable this in trusted, isolated environments.

### 12.7 Continue (VS Code / JetBrains)

Edit the Continue config file:

| IDE        | Config Path               |
|------------|---------------------------|
| VS Code    | `~/.continue/config.json` |
| JetBrains  | `~/.continue/config.json` |

Add the MCP server under `experimental`:

```json
{
  "experimental": {
    "modelRoles": {
      "inlineEdit": "..."
    }
  },
  "tabAutocompleteModel": {
    "title": "...",
    "provider": "..."
  },
  "mcpServers": [
    {
      "name": "openterface",
      "type": "sse",
      "url": "http://<your-server-ip>:8080/sse"
    }
  ]
}
```

In the Continue chat, use `@openterface` to mention tools.

### 12.8 Generic MCP SDK (Python)

**Install:**

```bash
pip install mcp
```

**SSE Client:**

```python
import asyncio
from mcp.client.sse import sse_client
from mcp import ClientSession


async def main():
    async with sse_client("http://<your-server-ip>:8080/sse") as (read, write):
        async with ClientSession(read, write) as session:
            await session.initialize()

            tools = await session.list_tools()
            print("Available tools:")
            for tool in tools.tools:
                print(f"  - {tool.name}: {tool.description[:60]}...")

            # Capture screen
            result = await session.call_tool("capture_screen", {"quality": 75})
            for content in result.content:
                if hasattr(content, "data"):
                    import base64
                    img = base64.b64decode(content.data)
                    with open("/tmp/screenshot.jpg", "wb") as f:
                        f.write(img)
                    print(f"\nScreenshot saved ({len(img)} bytes)")

            # Move mouse
            result = await session.call_tool(
                "mouse_move_absolute", {"x": 2048, "y": 2048}
            )
            print(result.content[0].text)

            # Ctrl+C
            result = await session.call_tool(
                "keyboard_press_key", {"key": 67, "modifiers": 2}
            )
            print(result.content[0].text)


asyncio.run(main())
```

**stdio Client:**

```python
import asyncio
from mcp.client.stdio import stdio_client, StdioServerParameters
from mcp import ClientSession


async def main():
    server_params = StdioServerParameters(
        command="/path/to/openterfaceQT",
        args=["--mcp-stdio"],
    )

    async with stdio_client(server_params) as (read, write):
        async with ClientSession(read, write) as session:
            await session.initialize()
            tools = await session.list_tools()
            print(f"Found {len(tools.tools)} tools")

            result = await session.call_tool("system_status")
            print(result.content[0].text)


asyncio.run(main())
```

### 12.9 Generic MCP SDK (TypeScript)

**Install:**

```bash
npm install @modelcontextprotocol/sdk
```

**SSE Client:**

```typescript
import { Client } from "@modelcontextprotocol/sdk/client/index.js";
import { SSEClientTransport } from "@modelcontextprotocol/sdk/client/sse.js";

async function main() {
  const transport = new SSEClientTransport(new URL("http://<your-server-ip>:8080/sse"));
  const client = new Client({ name: "my-client", version: "1.0.0" });

  await client.connect(transport);

  const { tools } = await client.listTools();
  console.log("Available tools:");
  tools.forEach((t: any) => console.log(`  - ${t.name}`));

  await client.callTool({
    name: "capture_screen",
    arguments: { quality: 75 },
  });

  await client.callTool({
    name: "mouse_move_absolute",
    arguments: { x: 2048, y: 2048 },
  });

  await client.callTool({
    name: "keyboard_press_key",
    arguments: { key: 67, modifiers: 2 },
  });
}

main().catch(console.error);
```

### 12.10 Direct HTTP Client (curl / Python)

#### curl Helper Script

```bash
#!/bin/bash
# openterface-cli.sh — minimal CLI for Openterface MCP SSE server

SERVER="${OPENTERFACE_URL:-http://<your-server-ip>:8080}"
SSE_FILE="/tmp/openterface_sse_$$.txt"

cleanup() { kill $SSE_PID 2>/dev/null; rm -f "$SSE_FILE"; }
trap cleanup EXIT

# Start SSE listener
curl -s -N "$SERVER/sse" > "$SSE_FILE" 2>&1 &
SSE_PID=$!
sleep 2

# Extract session ID
SID=$(grep -o 'sessionId=[a-f0-9-]*' "$SSE_FILE" | head -1 | cut -d= -f2)
if [ -z "$SID" ]; then echo "Failed to connect"; exit 1; fi
echo "Session: $SID"

# Initialize
curl -s -X POST "$SERVER/messages?sessionId=$SID" \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":1,"method":"initialize",
       "params":{"protocolVersion":"2024-11-05",
               "capabilities":{"tools":{}},
               "clientInfo":{"name":"cli","version":"1.0"}}}' > /dev/null
sleep 1

# Call a tool
call_tool() {
  local name=$1; shift
  > "$SSE_FILE"
  curl -s -X POST "$SERVER/messages?sessionId=$SID" \
    -H "Content-Type: application/json" \
    -d "{\"jsonrpc\":\"2.0\",\"id\":$RANDOM,\"method\":\"tools/call\",
         \"params\":{\"name\":\"$name\",\"arguments\":{$@}}}" > /dev/null
  sleep 1
  grep '^data:' "$SSE_FILE" | sed 's/^data: //'
}

# Example usage
echo "=== System Status ==="
call_tool system_status ""

echo "=== Capture Screen ==="
call_tool capture_screen '"quality":75'

echo "=== Mouse Move ==="
call_tool mouse_move_absolute '"x":2048,"y":2048'

echo "=== Press A ==="
call_tool keyboard_press_key '"key":65'

echo "=== Ctrl+C ==="
call_tool keyboard_press_key '"key":67,"modifiers":2'
```

#### Python (No SDK)

```python
"""Minimal Openterface client — no MCP SDK dependency."""
import httpx
import json
import re
import time
import threading
import base64


SERVER = "http://<your-server-ip>:8080"


class SimpleOpenterfaceClient:
    def __init__(self, server=SERVER):
        self.server = server
        self.session_id = None
        self._buffer = []
        self._thread = None

    def connect(self):
        self._thread = threading.Thread(target=self._listen, daemon=True)
        self._thread.start()
        time.sleep(2)
        data = "\n".join(self._buffer)
        m = re.search(r"sessionId=([a-f0-9-]+)", data)
        if not m:
            raise RuntimeError("No session ID received")
        self.session_id = m.group(1)

        self._post("initialize", {
            "protocolVersion": "2024-11-05",
            "capabilities": {"tools": {}},
            "clientInfo": {"name": "simple-client", "version": "1.0"}
        })

    def _listen(self):
        with httpx.stream("GET", f"{self.server}/sse", timeout=None) as r:
            for line in r.iter_lines():
                self._buffer.append(line)

    def _post(self, method, params=None):
        payload = {"jsonrpc": "2.0", "id": int(time.time() * 1000), "method": method}
        if params is not None:
            payload["params"] = params
        with httpx.Client() as c:
            c.post(f"{self.server}/messages?sessionId={self.session_id}",
                   json=payload, timeout=10)

    def call(self, tool_name, arguments=None):
        before = len(self._buffer)
        self._post("tools/call", {"name": tool_name, "arguments": arguments or {}})
        time.sleep(2)
        for line in self._buffer[before:]:
            if line.startswith("data:"):
                obj = json.loads(line[5:].strip())
                return obj.get("result", obj)
        return None

    # Convenience methods
    def move_mouse(self, x, y):
        return self.call("mouse_move_absolute", {"x": x, "y": y})

    def click(self, x, y, button="left"):
        return self.call("mouse_click", {"x": x, "y": y, "button": button})

    def press_key(self, key, modifiers=0):
        return self.call("keyboard_press_key", {"key": key, "modifiers": modifiers})

    def type_text(self, text):
        return self.call("keyboard_type_text", {"text": text})

    def capture_screen(self, quality=90):
        result = self.call("capture_screen", {"quality": quality})
        for c in result.get("content", []):
            if c.get("type") == "image":
                return base64.b64decode(c["data"])
        return None

    def status(self):
        result = self.call("system_status")
        text = result.get("content", [{}])[0].get("text", "{}")
        return json.loads(text)


# Usage
if __name__ == "__main__":
    client = SimpleOpenterfaceClient()
    client.connect()
    print("Status:", client.status())
    client.move_mouse(2048, 2048)
    client.press_key(67, modifiers=2)  # Ctrl+C
    img = client.capture_screen()
    if img:
        with open("/tmp/screen.jpg", "wb") as f:
            f.write(img)
        print(f"Screenshot saved: {len(img)} bytes")
```

#### JavaScript / Node.js (SSE)

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

### 12.11 AI Agent Framework Integration

#### LangChain / LangGraph

```python
from langchain_mcp_adapters.client import MultiServerMCPClient

async with MultiServerMCPClient({
    "openterface": {
        "transport": "sse",
        "url": "http://<your-server-ip>:8080/sse"
    }
}) as client:
    tools = client.get_tools()

    from langgraph.prebuilt import create_react_agent
    from langchain_openai import ChatOpenAI

    agent = create_react_agent(ChatOpenAI(model="gpt-4o"), tools)
    result = await agent.ainvoke({
        "messages": [{"role": "user", "content": "Take a screenshot of the target machine"}]
    })
```

#### CrewAI

```python
from crewai import Agent, Task, Crew
from crewai.tools import tool
import httpx, json, re, threading, time, base64

class OpenterfaceTools:
    def __init__(self, server="http://<your-server-ip>:8080"):
        self.server = server
        self.session_id = None
        self._buffer = []
        self._connect()

    def _connect(self):
        t = threading.Thread(target=self._listen, daemon=True)
        t.start()
        time.sleep(2)
        data = "\n".join(self._buffer)
        m = re.search(r"sessionId=([a-f0-9-]+)", data)
        self.session_id = m.group(1)
        self._post("initialize", {
            "protocolVersion": "2024-11-05",
            "capabilities": {"tools": {}},
            "clientInfo": {"name": "crewai", "version": "1.0"}
        })

    def _listen(self):
        with httpx.stream("GET", f"{self.server}/sse", timeout=None) as r:
            for line in r.iter_lines():
                self._buffer.append(line)

    def _post(self, method, params=None):
        payload = {"jsonrpc": "2.0", "id": int(time.time()*1000), "method": method}
        if params: payload["params"] = params
        with httpx.Client() as c:
            c.post(f"{self.server}/messages?sessionId={self.session_id}",
                   json=payload, timeout=10)

    def call_tool(self, name, arguments=None):
        before = len(self._buffer)
        self._post("tools/call", {"name": name, "arguments": arguments or {}})
        time.sleep(2)
        for line in self._buffer[before:]:
            if line.startswith("data:"):
                return json.loads(line[5:].strip())
        return None

openterface = OpenterfaceTools()

@tool("Capture screenshot from target machine")
def capture_screen() -> str:
    result = openterface.call_tool("capture_screen", {"quality": 75})
    for c in result.get("result", {}).get("content", []):
        if c.get("type") == "image":
            img = base64.b64decode(c["data"])
            with open("/tmp/screenshot.jpg", "wb") as f:
                f.write(img)
            return f"Screenshot saved ({len(img)} bytes)"
    return "Failed"

@tool("Press key on target machine")
def press_key(key_code: int, modifiers: int = 0) -> str:
    result = openterface.call_tool("keyboard_press_key",
                                    {"key": key_code, "modifiers": modifiers})
    return result.get("result", {}).get("content", [{}])[0].get("text", "done")

@tool("Move mouse on target machine")
def move_mouse(x: int, y: int) -> str:
    result = openterface.call_tool("mouse_move_absolute", {"x": x, "y": y})
    return result.get("result", {}).get("content", [{}])[0].get("text", "done")

kvm_agent = Agent(
    role="Remote KVM Operator",
    goal="Control the target computer via Openterface Mini-KVM",
    backstory="You are an expert at remote computer management using KVM tools.",
    tools=[capture_screen, press_key, move_mouse],
    verbose=True
)
```

#### OpenAI Agents SDK

```python
from agents import Agent, Runner
from agents.mcp import MCPServerSse

agent = Agent(
    name="KVM Assistant",
    instructions="You control a remote computer via Openterface Mini-KVM.",
    mcp_servers=[
        MCPServerSse(
            name="openterface",
            url="http://<your-server-ip>:8080/sse"
        )
    ]
)

result = await Runner.run(agent, "Take a screenshot and tell me what you see")
```

#### AutoGPT / AgentGPT

Register the Openterface SSE endpoint as an MCP tool server in your agent's config:

```yaml
mcp_servers:
  - name: openterface
    transport: sse
    url: http://<your-server-ip>:8080/sse
```

#### Dify / Coze / n8n (Low-Code Platforms)

These platforms support MCP tool integration via HTTP. Configure:

| Setting          | Value                                      |
|------------------|--------------------------------------------|
| Transport        | SSE                                        |
| Server URL       | `http://<your-server-ip>:8080/sse`          |
| Auth             | None (LAN trusted network)                 |

The platform will automatically discover all tools and expose them as nodes
in your workflow.

#### Custom Agent (Any Language)

The SSE protocol is just HTTP — any language can integrate:

1. `GET /sse` → open SSE stream, extract `sessionId`
2. `POST /messages?sessionId=<id>` → send JSON-RPC tool calls
3. Read responses from the SSE stream

---

## 13. Skill: Visual Feedback Loop

> **Core Principle: Every action must be based on screenshot image analysis to decide the next step.**
> Therefore, agents using this skill **must have image analysis (vision) capabilities**.

### Methodology

```
Screenshot → Analyze Image → Determine Current State → Decide Next Action → Execute → Screenshot Again → ...
```

Agents cannot execute blind command sequences. After each action, they must call `capture_screen` again and use visual capabilities to determine:
- Did the screen content change?
- Does the change match expectations?
- What state is the screen in now (lock screen / password field / desktop / error message)?
- What should be done next?

### Why Visual Verification is Essential

| Situation | Without Vision | With Vision |
|-----------|---------------|-------------|
| Target not awake | Blindly type password, fails | Sees black screen → press Space first |
| Password field not active | Typing in wrong place | Sees no change → retry |
| Wrong password | Thinks it succeeded, but failed | Sees error message → retry |
| Task complete | Doesn't know when to stop | Sees desktop → task done |

### Agent Requirements

**Required:**
- ✅ Vision / image analysis capabilities (can understand screenshot content)
- ✅ Can identify GUI states (lock screen, password field, desktop, dialogs, error messages)
- ✅ Can compare two consecutive screenshots to detect changes

**Not Supported:**
- ❌ Text-only agents (cannot analyze screenshots, cannot complete tasks)
- ❌ Tools that only execute blind command sequences

### Workflow Template

```
1. capture_screen         → Get initial screen state
2. Analyze screenshot     → "I see ___ state"
3. Decide action          → "Need to press Space / type / click ___"
4. Execute action (keyboard_* / mouse_*)
5. Wait for response time
6. capture_screen         → Get new screen state
7. Analyze screenshot     → "Screen changed to ___, action succeeded/failed"
8. Repeat 2-7 until task is complete
```

### Key Parameters

- **Post-action delay**: Wait 1-2s after Space, 0.5-1s after typing, 2-3s after Enter
- **Screenshot method**: `capture_screen` returns base64 JPEG, pass directly to vision model
- **Success determination**: Don't rely on command return values, only on visual screenshot analysis

### Transport Modes (independent of operation, choose as needed)

```bash
# stdio (local agent, recommended)
<build_dir>/openterfaceQT --mcp-stdio --skip-env-check

# SSE (remote agent, specify port)
<build_dir>/openterfaceQT --mcp-sse-port 8080 --skip-env-check

# Both modes simultaneously
<build_dir>/openterfaceQT --mcp-stdio --mcp-sse-port 8080 --skip-env-check
```

All modes provide the same tools (mouse / keyboard / screenshot / script) with identical functionality.

---

## 14. Testing Examples

### Test 1: stdio Mode — Initialize

```bash
echo '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2024-11-05","capabilities":{},"clientInfo":{"name":"test","version":"1.0"}}}' | ./openterfaceQT --mcp-stdio
```

Expected response:
```json
{"jsonrpc":"2.0","id":1,"result":{"protocolVersion":"2024-11-05","capabilities":{"tools":{}},"serverInfo":{"name":"Openterface MCP Server","version":"1.0.0"}}}
```

### Test 2: stdio Mode — List Tools

```bash
# Send initialization first
echo '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2024-11-05","capabilities":{},"clientInfo":{"name":"test","version":"1.0"}}}' | ./openterfaceQT --mcp-stdio

# Then send tools/list
echo '{"jsonrpc":"2.0","id":2,"method":"tools/list"}' | ./openterfaceQT --mcp-stdio
```

### Test 3: SSE Mode — Full Flow

```bash
# Terminal 1: Start server
./openterfaceQT --mcp-sse-port 8080

# Terminal 2: Connect to SSE and get session ID
curl -N http://localhost:8080/sse

# Terminal 3: Send initialize request (replace <session-id> with actual ID)
curl -X POST "http://localhost:8080/messages?sessionId=<session-id>" \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2024-11-05","capabilities":{},"clientInfo":{"name":"test","version":"1.0"}}}'
```

### Test 4: Python MCP Client (stdio)

```python
import subprocess
import json

proc = subprocess.Popen(
    ['./openterfaceQT', '--mcp-stdio'],
    stdin=subprocess.PIPE,
    stdout=subprocess.PIPE,
    stderr=subprocess.PIPE,
    text=True
)

# Send initialization
init_msg = {
    "jsonrpc": "2.0",
    "id": 1,
    "method": "initialize",
    "params": {
        "protocolVersion": "2024-11-05",
        "capabilities": {},
        "clientInfo": {"name": "python-client", "version": "1.0"}
    }
}

proc.stdin.write(json.dumps(init_msg) + '\n')
proc.stdin.flush()

response = proc.stdout.readline()
print("Init response:", response)

# List tools
list_msg = {"jsonrpc": "2.0", "id": 2, "method": "tools/list"}
proc.stdin.write(json.dumps(list_msg) + '\n')
proc.stdin.flush()

response = proc.stdout.readline()
print("Tools list:", response)

proc.terminate()
```

### Test 5: Python MCP Client (SSE)

```python
import httpx
import json
import time
import threading

SSE_URL = "http://localhost:8080/sse"
BASE_URL = "http://localhost:8080"

def sse_client():
    session_id = None
    msg_endpoint = None

    def listener():
        nonlocal session_id, msg_endpoint
        with httpx.stream("GET", SSE_URL, timeout=60.0) as resp:
            for line in resp.iter_lines():
                if line.startswith("data:"):
                    data = json.loads(line[5:].strip())
                    if "endpoint" in data:
                        msg_endpoint = f"{BASE_URL}{data['endpoint']}"
                        import re
                        session_id = re.search(r'sessionId=([^&]+)',
                                     data['endpoint']).group(1)

    thread = threading.Thread(target=listener, daemon=True)
    thread.start()
    time.sleep(1)

    def send(method, args=None, req_id=100):
        msg = {"jsonrpc":"2.0", "method":method}
        if args: msg["params"] = args
        if req_id: msg["id"] = req_id
        httpx.post(msg_endpoint, json=msg, timeout=10.0)

    send("initialize", {
        "protocolVersion": "2024-11-05",
        "capabilities": {},
        "clientInfo": {"name": "client", "version": "1.0"}
    }, req_id=1)

    time.sleep(1)
    send("notifications/initialized")
    send("tools/list", req_id=2)
    send("tools/call", {"name":"system_status"}, req_id=10)

    return session_id

session = sse_client()
print(f"Connected with session: {session}")
```

---

## 15. Deployment & Operations

### 15.1 Running as a Background Service (systemd)

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

### 15.2 Running with nohup

```bash
cd /home/bot/project/Openterface_QT/build
nohup ./openterfaceQT --mcp-sse-port 8080 > /tmp/openterface_sse.log 2>&1 &
echo $! > /tmp/openterface.pid

# Stop later:
kill $(cat /tmp/openterface.pid)
```

### 15.3 Running with screen/tmux

```bash
screen -dmS openterface bash -c \
  'cd ~/project/Openterface_QT/build && ./openterfaceQT --mcp-sse-port 8080'

# Reattach: screen -r openterface
# Detach:   Ctrl-A, D
```

### 15.4 Firewall Configuration

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

### 15.5 Health Check

```bash
# Is the process running?
ps aux | grep openterfaceQT | grep -v grep

# Is the port listening?
ss -tlnp | grep 8080

# Can we connect?
curl -s http://localhost:8080/sse -N --max-time 3 | head -5
```

### 15.6 Log Rotation

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

## 16. Logging & Debugging

### 16.1 Log Output

| Source             | Destination            | Content                                       |
|--------------------|------------------------|-----------------------------------------------|
| qInfo/qWarning     | stdout/stderr          | General application messages                  |
| qCDebug(log_keyboard)| stderr (debug builds)| Detailed keyboard processing                  |
| `[MCP-DIAG]`       | stderr                 | MCP tool handler entry points                 |
| `[KB-DIAG]`        | stderr                 | HID report bytes sent to serial               |
| `[DEBUG-HID]`      | stderr                 | HID device open/close events                  |
| `[DEBUG-FFMPEG]`   | stderr                 | Video capture backend events                  |

### 16.2 Key Log Messages

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

### 16.3 Verifying HID Reports

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

## 17. Troubleshooting

### HID Permission Denied

**Symptom:**
```
[DEBUG-HID] open failed: /dev/hidraw4, error: Permission denied
```

**Fix:** Apply the udev rule (see §3.3), then restart the app.

### Combo Keys Don't Work (Ctrl+C, Alt+Tab)

**Symptom:** The target computer doesn't respond to Ctrl+C or other modifier combos.

**Fix:** Verify the `modifiers` parameter is being passed correctly. Check `[KB-DIAG]` logs — byte 5 should be non-zero for combo keys. Prior to 2025-06-26, the `KeyboardManager` ignored the `modifiers` parameter for non-modifier keys.

### Key Stays Pressed (No Release)

**Symptom:** After calling `keyboard_press_key`, the key appears held down on the
target computer (e.g., "aaaaaa..." in a text editor).

**Fix:** With `autoRelease` defaulting to `true`, keys are released automatically
after ~80ms. To hold keys manually, pass `autoRelease: false` and then release
explicitly with `isKeyDown: false`.

### No SSE Response

**Symptom:** POST returns `{"accepted":true}` but no `event: message` on the stream.

**Checks:**
1. Ensure the SSE connection is still open (don't close the listener)
2. Verify the `sessionId` matches the one from `GET /sse`
3. Sessions expire after 30 minutes of inactivity — open a new one
4. Check if `initialize` was called first

### capture_screen Returns Black Image

**Symptom:** Valid JPEG but all black.

**Cause:** Camera hasn't produced a frame yet, or target is not connected.

**Fix:** Wait 5–10 seconds after startup. Check logs for:
```
Camera ready! First frame: 1920x1080
```

### ScriptRunner Not Available

**Symptom:** `execute_script` or `keyboard_send_keys` returns error.

**Cause:** Headless mode doesn't inject `ScriptRunner`. This requires a GUI instance.

**Workaround:** Use `keyboard_press_key` for individual keys or `keyboard_type_text`
for typing. Both work in headless mode.

### Connection Refused from LAN

**Symptom:** `curl: (7) Failed to connect to 192.168.100.85:8080`

**Checks:**
1. Verify bind address is `0.0.0.0`: `ss -tlnp | grep 8080`
2. Check firewall rules (see §15.4)
3. Verify both machines are on the same subnet

### SSE Connection Timeout (Cursor / Windsurf)

**Cause:** Network unreachable or IP address wrong.

**Fix:**
```bash
# Verify the server IP from the client machine
ping <your-server-ip>

# Verify the port is reachable
curl -v http://<your-server-ip>:8080/sse --max-time 5
```

### MCP Server Failed to Start

**Cause:** The `command` path is wrong or the binary is not executable.

**Fix:**
```bash
ls -la /path/to/openterfaceQT
chmod +x /path/to/openterfaceQT
/path/to/openterfaceQT --mcp-stdio
```

### No Tools Available

**Cause:** The server started but failed to initialize camera/HID.

**Fix:** Check server logs for errors. Usually HID permission issue — apply the
udev rule (see §3.3).

### Claude Code Shows "Server disconnected"

**Cause:** The server process crashed or the stdio pipe broke.

**Fix:**
```bash
ps aux | grep openterfaceQT
ls -la /dev/hidraw4
# Should show: crw-rw-rw- (not crw-------)
```

### App Crashes on Startup

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

### stdio Mode Exits Immediately

**Solution:** Ensure the executable path is correct and you're running from the correct directory. Check that all required DLLs are present (on Windows).

### No Devices Found

**Solution:**
1. Verify the Openterface device is connected via USB
2. Check device permissions (especially on Linux: `sudo usermod -aG dialout $USER`)
3. Verify the serial port exists: `ls /dev/ttyACM*` (Linux) or check Device Manager (Windows)

### Named Pipe Connection Failed

**Solution:**
1. Verify the socket file exists: `ls -l /tmp/openterface-mcp.sock` (Linux)
2. Check permissions on the socket file
3. Ensure no other instance is using the same pipe name

---

## 18. Quick Reference Card

```
┌──────────────────────────────────────────────────────────────┐
│                  OPENTERFACE MCP CONNECTION                   │
├──────────────────────────────────────────────────────────────┤
│                                                              │
│  stdio (local):                                              │
│    command: /path/to/openterfaceQT                           │
│    args: ["--mcp-stdio"]                                     │
│                                                              │
│  SSE (remote):                                               │
│    url: http://<host-ip>:8080/sse                            │
│    (start server: ./openterfaceQT --mcp-sse-port 8080)       │
│                                                              │
├──────────────────────────────────────────────────────────────┤
│  CLIENT          │ TRANSPORT  │ CONFIG FILE                  │
├──────────────────┼────────────┼──────────────────────────────│
│  Claude Code     │ SSE ★      │ ~/.claude/settings.json      │
│  Claude Desktop  │ SSE ★      │ ~/Library/.../config.json    │
│  Cursor          │ SSE ★      │ ~/.cursor/mcp.json           │
│  Windsurf        │ SSE ★      │ ~/.codeium/.../mcp_config    │
│  Cline           │ SSE ★      │ ~/.cline/mcp_settings.json   │
│  Continue        │ SSE ★      │ ~/.continue/config.json      │
│  Python SDK      │ SSE        │ code                         │
│  TypeScript SDK  │ SSE        │ code                         │
│  curl            │ SSE        │ script                       │
│                  │            │                              │
│  Claude Code     │ stdio      │ ~/.claude/settings.json      │
│  (local only)    │            │                              │
└──────────────────────────────────────────────────────────────┘
  ★ = Recommended (remote / multi-client / agent framework ready)
```

---

## 19. File Structure

```
server/
├── tcpServer.h/cpp          # Existing TCP server (preserved)
├── tcpResponse.h/cpp        # Existing TCP response handler (preserved)
├── mcp/
│   ├── mcpConstants.h        — Protocol version, tool names, default port, limits
│   ├── mcpProtocol.h/cpp     — JSON-RPC 2.0 parsing and response building
│   ├── mcpToolHandler.h/cpp  — Tool registry: listTools() and callTool() dispatch
│   ├── mcpServer.h/cpp       — Top-level server: transport lifecycle, dependency injection
│   └── mcpSseTransport.h/cpp — SSE HTTP transport: QHttpServer, session mgmt, keepalives
```

---

## 20. Security Considerations

- The MCP server provides full control over the target computer's mouse and keyboard
- Run the server in a trusted environment only
- SSE transport does not include authentication — consider using a reverse proxy with auth for production use
- stdio transport is safer as it requires explicit client connection
- Named Pipe transport is limited to local connections only
- When using `--mcp-sse-bind-any`, the server is accessible from the network — use firewall rules to restrict access

---

## 21. Thread Safety

The MCP server is designed to work safely with the GUI and hardware components:

| Scenario | Handling |
|----------|----------|
| MCP thread calls MouseManager/KeyboardManager | Uses Qt Signal/Slot mechanism for thread-safe dispatch |
| MCP thread accesses CameraManager | CameraManager has internal mutex protection |
| Script execution | ScriptRunner uses worker thread + atomic flags |
| SSE multi-client | Each session is independent; hardware access is serialized via QMutex |

---

## 22. Performance Notes

- **Screen capture**: The `capture_screen` tool captures the current frame from the USB capture device. Quality settings affect JPEG compression and response size.
- **Mouse/Keyboard**: Commands are sent asynchronously via the serial port. Response confirms command was queued, not necessarily executed.
- **SSE sessions**: Sessions timeout after 30 minutes of inactivity. Keep-alive messages are sent every 15 seconds to maintain connections.
- **Multi-transport**: Running multiple transports simultaneously has minimal performance impact as they share the same tool handler.

---

## 23. Future Extensions

- **Streamable HTTP** (MCP 2025-03-26 spec) — single `POST /mcp` endpoint
- **WebSocket transport** — bidirectional, lower latency for high-frequency updates
- **TLS/HTTPS** — encrypted remote access via `QSslSocket` integration
- **Authentication** — Bearer token in `Authorization` header
- **Rate limiting** — per-session and per-IP request throttling
- **mDNS/Bonjour** — LAN auto-discovery via `QDNSServiceBrowser`

---

## 24. License & Support

**License:** GNU General Public License v3.0

**Issues and feature requests:**
https://github.com/TechxArtisanStudio/Openterface_QT/issues

---

*Last updated: 2026-08-24*
