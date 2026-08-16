# Openterface MCP Server Documentation

## Overview

The Openterface_QT project includes an MCP (Model Context Protocol) server implementation that allows AI models like Claude to control the target computer through mouse, keyboard, and screen capture operations.

The MCP server supports **three transport modes**:
1. **stdio** - Standard input/output transport (for CLI-based MCP clients like Claude Code)
2. **Named Pipe (QLocalServer)** - Unix domain socket / Windows named pipe (for local IPC)
3. **SSE (Server-Sent Events)** - HTTP-based transport (for remote/network-based MCP clients)

All transports share the same `McpToolHandler` and expose identical tools, ensuring consistent functionality regardless of the transport mode.

## Installation

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

## Running MCP Server

### Mode 1: stdio Transport

The stdio transport mode runs the MCP server in headless mode, reading JSON-RPC messages from stdin and writing responses to stdout. This is the recommended mode for local CLI-based clients like Claude Code.

#### Command-Line Usage

```bash
./openterfaceQT --mcp-stdio
```

#### Example with Claude Code

Create a `.claude/settings.json` file in your project root:

```json
{
  "mcpServers": {
    "openterface-kvm": {
      "command": "<build_dir>/openterfaceQT",
      "args": ["--mcp-stdio"]
    }
  }
}
```

#### Testing stdio Mode Manually

You can test the stdio mode by sending JSON-RPC messages:

```bash
# Start the server
./openterfaceQT --mcp-stdio

# In another terminal, send initialization message
echo '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2024-11-05","capabilities":{},"clientInfo":{"name":"test-client","version":"1.0"}}}' | ./openterfaceQT --mcp-stdio
```

Expected response:
```json
{"jsonrpc":"2.0","id":1,"result":{"protocolVersion":"2024-11-05","capabilities":{"tools":{}},"serverInfo":{"name":"Openterface MCP Server","version":"1.0.0"}}}
```

### Mode 2: Named Pipe Transport

The Named Pipe transport uses QLocalServer to create a Unix domain socket on Linux (`/tmp/openterface-mcp.sock`) or a Windows named pipe (`\\.\pipe\openterface-mcp`). This transport is useful for local IPC when stdio is not suitable.

#### Command-Line Usage

Named Pipe transport is enabled by default when running with GUI. For standalone operation, use:

```bash
./openterfaceQT --mcp-pipe
```

#### Example with socat (Linux)

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

### Mode 3: SSE Transport

The SSE transport mode runs the MCP server as an HTTP server, allowing remote clients to connect. This is ideal for network-based access or when the client runs on a different machine.

#### Command-Line Usage

```bash
./openterfaceQT --mcp-sse-port 8080
```

Optional arguments:
- `--mcp-sse-bind <address>` - Bind to specific address (default: 127.0.0.1)
- `--mcp-sse-bind-any` - Bind to 0.0.0.0 (all interfaces, use with caution)

#### Testing SSE Mode

1. **Start the server:**
   ```bash
   ./openterfaceQT --mcp-sse-port 8080
   ```

2. **Connect to SSE endpoint:**
   
   Open a connection to `http://localhost:8080/sse`
   
   The server will respond with an SSE stream and send an `endpoint` event containing the URL to POST messages to:
   
   ```
   event: endpoint
   data: {"endpoint":"/messages?sessionId=<session-id>"}
   ```

3. **Send JSON-RPC messages:**
   
   POST to the endpoint URL with your JSON-RPC request:
   
   ```bash
   curl -X POST "http://localhost:8080/messages?sessionId=<session-id>" \
        -H "Content-Type: application/json" \
        -d '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2024-11-05","capabilities":{},"clientInfo":{"name":"test-client","version":"1.0"}}}'
   ```

4. **Receive responses via SSE stream:**
   
   The server will send responses as SSE events:
   
   ```
   event: message
   data: {"jsonrpc":"2.0","id":1,"result":{...}}
   ```

#### SSE Session Management

- Sessions are identified by UUID and timeout after 30 minutes of inactivity
- Maximum of 16 concurrent SSE sessions
- Sessions are automatically cleaned up when clients disconnect
- Keep-alive messages are sent every 15 seconds to maintain connections

## Available MCP Tools

The MCP server exposes the following tools for controlling the target computer:

### Mouse Control

#### `mouse_move_absolute`
Move the mouse to absolute coordinates on the target screen.

**Parameters:**
- `x` (integer, 0-4096): X coordinate. Formula: `pixel_x / screen_width * 4096`
- `y` (integer, 0-4096): Y coordinate. Formula: `pixel_y / screen_height * 4096`

**Example:** On a 1920x1080 screen, pixel (960, 540) = MCP (2048, 2048)

#### `mouse_click`
Click the mouse at specified coordinates.

**Parameters:**
- `x` (integer, 0-4096): X coordinate. Formula: `pixel_x / screen_width * 4096`
- `y` (integer, 0-4096): Y coordinate. Formula: `pixel_y / screen_height * 4096`
- `button` (string, optional): "left", "right", or "middle" (default: "left")
- `count` (integer, optional): Click count (default: 1)

#### `mouse_move_relative`
Move the mouse relative to current position.

**Parameters:**
- `dx` (integer): X offset
- `dy` (integer): Y offset

#### `mouse_scroll`
Scroll the mouse wheel.

**Parameters:**
- `direction` (string): "up" or "down"
- `lines` (integer, optional): Number of lines to scroll (default: 3)

### Keyboard Control

#### `keyboard_press_key`
Press or release a specific key. Accepts either a Qt key code (integer) or a key name (string).

**Parameters:**
- `key` (string or integer): Key identifier. Can be:
  - A key name string: `"Enter"`, `"Escape"`, `"Tab"`, `"Backspace"`, `"Delete"`, `"Space"`, `"Up"`, `"Down"`, `"Left"`, `"Right"`, `"Home"`, `"End"`, `"PageUp"`, `"PageDown"`, `"Insert"`, `"F1"` through `"F15"`, `"Shift"`, `"Control"` (or `"Ctrl"`), `"Alt"`, `"AltGr"`, `"Meta"` (or `"Super"`, `"Win"`)
  - A single letter: `"A"` through `"Z"`
  - A single digit: `"0"` through `"9"`
  - A Qt key code integer (e.g., `16777220` for Enter, `65` for A)
- `modifiers` (integer, optional): Modifier bitmask. `1` = Shift, `2` = Ctrl, `4` = Alt, `8` = Meta/Win. Can be combined (e.g., `6` for Ctrl+Alt). Default: `0`.
- `isKeyDown` (boolean, optional): `true` for key press, `false` for release. Default: `true`.
- `autoRelease` (boolean, optional): If `true` (default), the key is automatically released after press. Set to `false` to hold the key down (useful for modifier combos like Alt+Tab).
- `side` (string, optional): `"left"` or `"right"` — specifies which side of the keyboard for Shift, Ctrl, and Alt keys.

**Example:**
```json
{"key": "Escape"}
{"key": "Tab"}
{"key": "Enter"}
{"key": "Control", "autoRelease": false}
{"key": "F4", "modifiers": 4}
{"key": 16777220}
```

#### `keyboard_type_text`
Type text string to the target.

**Parameters:**
- `text` (string): Text to type

#### `keyboard_send_keys`
Send key combination using AutoHotKey syntax.

**Parameters:**
- `keys` (string): Key combination in AHK syntax (e.g., "^c" for Ctrl+C)

#### `keyboard_function_key`
Send function key (F1-F12).

**Parameters:**
- `key` (string): "F1" through "F12"

#### `keyboard_ctrl_alt_del`
Send Ctrl+Alt+Del sequence.

**Parameters:** None

#### `keyboard_set_layout`
Set keyboard layout.

**Parameters:**
- `layout` (string): Layout identifier (e.g., "en-us", "de-de")

### Screen Capture

#### `capture_screen`
Capture the current screen from the target.

**Parameters:**
- `quality` (integer, optional): JPEG quality 1-100 (default: 80)

**Returns:** Base64-encoded JPEG image

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

**Response:**
```json
{
  "jsonrpc": "2.0",
  "id": 3,
  "result": {
    "content": [
      {
        "type": "image",
        "data": "<base64-encoded-jpeg>",
        "mimeType": "image/jpeg"
      }
    ]
  }
}
```

#### `capture_last_image`
Capture the last received image from the camera.

**Parameters:** None

**Returns:** Base64-encoded image

#### `screen_to_markdown`
Capture the target screen and convert it to a structured Markdown representation with OCR-detected text and UI element locations. This tool helps AI agents understand screen content and find clickable elements without needing vision capabilities.

**Prerequisites:** Tesseract OCR must be installed on the system. Install with:
```bash
# Fedora/RHEL
sudo dnf install tesseract tesseract-devel leptonica-devel

# Ubuntu/Debian
sudo apt-get install tesseract-ocr libtesseract-dev libleptonica-dev
```

**Parameters:**
- `detail_level` (string, optional): Level of detail in output - `"basic"` (only interactive elements) or `"detailed"` (full breakdown with all text). Default: `"detailed"`

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

**Example (detailed mode):**
```json
{
  "jsonrpc": "2.0",
  "id": 11,
  "method": "tools/call",
  "params": {
    "name": "screen_to_markdown",
    "arguments": {"detail_level": "detailed"}
  }
}
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

```json
{
  "name": "mouse_click",
  "arguments": {
    "x": <MCP_X>,
    "y": <MCP_Y>
  }
}
```

### Clickable Elements
| Element | Coordinates (x, y) |
|---------|-------------------|
| Restart | (2300, 2640) |
| Cancel | (1800, 2640) |
```

**Use Case:**
This tool is particularly useful for AI agents that need to:
- Find specific buttons or menu items without trial-and-error
- Understand the current screen state before taking actions
- Navigate complex UIs efficiently
- Work without vision capabilities

**Note:** Coordinates are in the MCP range (0-4096). Use directly with `mouse_click` and other mouse tools.

### Script Execution

#### `execute_script`
Execute an AutoHotKey-style script.

**Parameters:**
- `script` (string): Script in AHK syntax

**Example:**
```json
{
  "jsonrpc": "2.0",
  "id": 4,
  "method": "tools/call",
  "params": {
    "name": "execute_script",
    "arguments": {
      "script": "Send, Hello World{Enter}"
    }
  }
}
```

### System Information

#### `system_status`
Get current system status including transport states and device information.

**Parameters:** None

**Returns:** JSON object with system status

## Architecture

```
┌──────────────────────────────────────────────────────────────────┐
│                       Openterface QT App                          │
│                                                                   │
│  ┌──────────────┐  ┌─────────────────────────────────────────┐  │
│  │  GUI         │  │          MCP Server                      │  │
│  │ (MainWindow) │  │  (McpServer.h/cpp)                      │  │
│  │              │  │    ├─ McpProtocol.h/cpp                 │  │
│  │  Toggle      │  │    ├─ McpToolHandler.h/cpp              │  │
│  │              │  │    ├─ mcpSseTransport.h/cpp             │  │
│  │              │  │    └─ stdio/Named Pipe/SSE transports   │  │
│  └──────────────┘  └────────────────┬────────────────────────┘  │
│         │                           │                             │
│         │              ┌────────────▼────────────┐               │
│         │              │  Transport Layer         │               │
│         │              │  ┌─────────┐ ┌────────┐ │               │
│         │              │  │  stdio  │ │  SSE   │ │               │
│         │              │  │(stdin/  │ │(HTTP/  │ │               │
│         │              │  │ stdout) │ │ Port)  │ │               │
│         │              │  └────┬────┘ └───┬────┘ │               │
│         │              │       │          │      │               │
│         │              │       └────┬─────┘      │               │
│         │              │            │            │               │
│         │              │  ┌─────────▼─────────┐  │               │
│         │              │  │  Named Pipe       │  │               │
│         │              │  │  (QLocalServer)   │  │               │
│         │              │  └─────────┬─────────┘  │               │
│         │              └────────────┼────────────┘               │
│         │                           │                             │
│  ┌──────▼───────────────────────────▼──────────────────────┐    │
│  │           McpToolHandler (Shared)                        │    │
│  │           (Tool Registry and Dispatch)                   │    │
│  └──────────────────────┬──────────────────────────────────┘    │
│                         │                                        │
│  ┌──────────────────────▼──────────────────────────────────┐    │
│  │     Existing Components (Reused)                         │    │
│  │  HostManager (Singleton)                                 │    │
│  │    ├─ MouseManager                                       │    │
│  │    └─ KeyboardManager                                    │    │
│  │  CameraManager (via MainWindow ptr)                      │    │
│  │  ScriptRunner (Lexer/Parser/Executor)                    │    │
│  │  SerialPortManager                                       │    │
│  └──────────────────────────────────────────────────────────┘    │
└──────────────────────────────────────────────────────────────────┘
           ▲
           │ JSON-RPC 2.0
           │
    ┌──────┴──────┐
    │  MCP Client  │
    │ (Claude/AI)  │
    └─────────────┘
```

## MCP Protocol Flow

### Initialization Sequence

```
Client                          Server
  |                                |
  |-- initialize --------------->  |  Returns serverInfo + capabilities
  |<-- result -------------------- |
  |                                |
  |-- notifications/initialized -> |  (notification, no reply)
  |                                |
  |-- tools/list --------------->  |  Returns tool definitions
  |<-- result -------------------- |
  |                                |
  |-- tools/call --------------->  |  Execute tool
  |<-- result -------------------- |
  |                                |
```

### SSE Protocol Flow

```
Client                              Server
  |                                    |
  |-- GET /sse ----------------------->|  Open SSE connection
  |<-- 200 OK (text/event-stream) -----|
  |<-- event: endpoint ----------------|  Discover message endpoint
  |<-- data: {"endpoint":"/messages?sessionId=..."}
  |                                    |
  |-- POST /messages?sessionId=... --->|  Send JSON-RPC
  |<-- 202 Accepted -------------------|
  |<-- event: message -----------------|  Receive response via SSE
  |<-- data: {"jsonrpc":"2.0",...} ----|
  |                                    |
```

## File Structure

```
server/
├── tcpServer.h/cpp          # Existing TCP server (preserved)
├── tcpResponse.h/cpp        # Existing TCP response handler (preserved)
├── mcp/
│   ├── mcpServer.h/cpp          # MCP Server main class
│   ├── mcpProtocol.h/cpp        # JSON-RPC 2.0 protocol handler
│   ├── mcpToolHandler.h/cpp     # Tool registry and dispatch
│   ├── mcpSseTransport.h/cpp    # SSE transport implementation
│   └── mcpConstants.h           # MCP protocol constants
```

## Command-Line Arguments

| Argument | Description |
|----------|-------------|
| `--mcp-stdio` | Run MCP server in stdio transport mode (headless) |
| `--mcp-pipe` | Run MCP server in Named Pipe transport mode |
| `--mcp-sse-port <port>` | Run MCP server in SSE mode on specified port |
| `--mcp-sse-bind <address>` | Bind SSE to specific address (default: 127.0.0.1) |
| `--mcp-sse-bind-any` | Bind SSE to 0.0.0.0 (all interfaces) |
| `--mcp-start` | Auto-start MCP server after GUI launches (GUI mode only) |
| `--skip-env-check` | Skip environment check on startup |

### Combined Transport Modes

You can run multiple transports simultaneously:

```bash
# stdio + SSE
./openterfaceQT --mcp-stdio --mcp-sse-port 8080 --skip-env-check

# All three transports
./openterfaceQT --mcp-stdio --mcp-pipe --mcp-sse-port 8080 --skip-env-check
```

## Testing Examples

### Test 1: stdio Mode - Initialize

```bash
echo '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2024-11-05","capabilities":{},"clientInfo":{"name":"test","version":"1.0"}}}' | ./openterfaceQT --mcp-stdio
```

### Test 2: stdio Mode - List Tools

```bash
# Send initialization first
echo '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2024-11-05","capabilities":{},"clientInfo":{"name":"test","version":"1.0"}}}' | ./openterfaceQT --mcp-stdio

# Then send tools/list
echo '{"jsonrpc":"2.0","id":2,"method":"tools/list"}' | ./openterfaceQT --mcp-stdio
```

### Test 3: SSE Mode - Full Flow

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

# Start MCP server in stdio mode
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

# Read response
response = proc.stdout.readline()
print("Init response:", response)

# List tools
list_msg = {
    "jsonrpc": "2.0",
    "id": 2,
    "method": "tools/list"
}

proc.stdin.write(json.dumps(list_msg) + '\n')
proc.stdin.flush()

response = proc.stdout.readline()
print("Tools list:", response)

# Clean up
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
    
    # Listen for SSE events
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
    
    # Send MCP requests
    def send(method, args=None, req_id=100):
        msg = {"jsonrpc":"2.0", "method":method}
        if args: msg["params"] = args
        if req_id: msg["id"] = req_id
        httpx.post(msg_endpoint, json=msg, timeout=10.0)
    
    # Initialize
    send("initialize", {
        "protocolVersion": "2024-11-05",
        "capabilities": {},
        "clientInfo": {"name": "client", "version": "1.0"}
    }, req_id=1)
    
    time.sleep(1)
    send("notifications/initialized")
    
    # List tools
    send("tools/list", req_id=2)
    
    # Call a tool
    send("tools/call", {"name":"system_status"}, req_id=10)
    
    return session_id

session = sse_client()
print(f"Connected with session: {session}")
```

## Skill: Visual Feedback Loop — Openterface MCP Automation

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

## Troubleshooting

### Issue: stdio mode exits immediately

**Solution:** Ensure the executable path is correct and you're running from the correct directory. Check that all required DLLs are present (on Windows).

### Issue: No devices found

**Solution:** 
1. Verify the Openterface device is connected via USB
2. Check device permissions (especially on Linux: `sudo usermod -aG dialout $USER`)
3. Verify the serial port exists: `ls /dev/ttyACM*` (Linux) or check Device Manager (Windows)

### Issue: Black screen on capture

**Solution:** The VideoHid initialization may have failed. Check logs for "VideoHid started" messages. Ensure the HDMI source is connected and active.

### Issue: SSE connection refused

**Solution:** 
1. Verify the port is not in use: `netstat -an | grep 8080`
2. Check firewall settings
3. Try a different port: `--mcp-sse-port 8081`

### Issue: Named Pipe connection failed

**Solution:**
1. Verify the socket file exists: `ls -l /tmp/openterface-mcp.sock` (Linux)
2. Check permissions on the socket file
3. Ensure no other instance is using the same pipe name

## Performance Notes

- **Screen capture**: The `capture_screen` tool captures the current frame from the USB capture device. Quality settings affect JPEG compression and response size.
- **Mouse/Keyboard**: Commands are sent asynchronously via the serial port. Response confirms command was queued, not necessarily executed.
- **SSE sessions**: Sessions timeout after 30 minutes of inactivity. Keep-alive messages are sent every 15 seconds to maintain connections.
- **Multi-transport**: Running multiple transports simultaneously has minimal performance impact as they share the same tool handler.

## Security Considerations

- The MCP server provides full control over the target computer's mouse and keyboard
- Run the server in a trusted environment only
- SSE transport does not include authentication - consider using a reverse proxy with auth for production use
- stdio transport is safer as it requires explicit client connection
- Named Pipe transport is limited to local connections only
- When using `--mcp-sse-bind-any`, the server is accessible from the network - use firewall rules to restrict access

## Thread Safety

The MCP server is designed to work safely with the GUI and hardware components:

| Scenario | Handling |
|----------|----------|
| MCP thread calls MouseManager/KeyboardManager | Uses Qt Signal/Slot mechanism for thread-safe dispatch |
| MCP thread accesses CameraManager | CameraManager has internal mutex protection |
| Script execution | ScriptRunner uses worker thread + atomic flags |
| SSE multi-client | Each session is independent; hardware access is serialized via QMutex |

## Future Extensions

- **Streamable HTTP** (MCP 2025-03-26 spec) - single `POST /mcp` endpoint
- **WebSocket transport** - bidirectional, lower latency for high-frequency updates
- **TLS/HTTPS** - encrypted remote access via `QSslSocket` integration
- **Authentication** - Bearer token in `Authorization` header
- **Rate limiting** - per-session and per-IP request throttling
- **mDNS/Bonjour** - LAN auto-discovery via `QDNSServiceBrowser`

## License

GNU General Public License v3.0

## Support

For issues and feature requests, please visit:
https://github.com/TechxArtisanStudio/Openterface_QT/issues
