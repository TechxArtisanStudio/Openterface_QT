# Openterface MCP Server Documentation

## Overview

The Openterface Mini-KVM project includes an MCP (Model Context Protocol) server implementation that allows AI models like Claude to control the target computer through mouse, keyboard, and screen capture operations.

The MCP server supports **two transport modes**:
1. **stdio** - Standard input/output transport (for CLI-based MCP clients)
2. **SSE** - Server-Sent Events over HTTP (for remote/network-based MCP clients)

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

The stdio transport mode runs the MCP server in headless mode, reading JSON-RPC messages from stdin and writing responses to stdout.

#### Command-Line Usage

```bash
./openterfaceQT.exe --mcp-stdio
```

#### Example with Claude Code

Create a `.claude/settings.json` file in your project root:

```json
{
  "mcpServers": {
    "openterface-kvm": {
      "command": "<build_dir>/openterfaceQT.exe",
      "args": ["--mcp-stdio"]
    }
  }
}
```

#### Testing stdio Mode Manually

You can test the stdio mode by sending JSON-RPC messages:

```bash
# Start the server
./openterfaceQT.exe --mcp-stdio

# In another terminal, send initialization message
echo '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2024-11-05","capabilities":{},"clientInfo":{"name":"test-client","version":"1.0"}}}' | ./openterfaceQT.exe --mcp-stdio
```

Expected response:
```json
{"jsonrpc":"2.0","id":1,"result":{"protocolVersion":"2024-11-05","capabilities":{"tools":{}},"serverInfo":{"name":"Openterface MCP Server","version":"1.0.0"}}}
```

### Mode 2: SSE Transport

The SSE transport mode runs the MCP server as an HTTP server, allowing remote clients to connect.

#### Command-Line Usage

```bash
./openterfaceQT.exe --mcp-sse-port 8080
```

This starts the SSE server on port 8080.

#### Testing SSE Mode

1. **Start the server:**
   ```bash
   ./openterfaceQT.exe --mcp-sse-port 8080
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

## Available MCP Tools

The MCP server exposes the following tools for controlling the target computer:

### Mouse Control

#### `mouse_move_absolute`
Move the mouse to absolute coordinates on the target screen.

**Parameters:**
- `x` (integer, 0-4096): X coordinate
- `y` (integer, 0-4096): Y coordinate

#### `mouse_click`
Click the mouse at specified coordinates.

**Parameters:**
- `x` (integer, 0-4096): X coordinate
- `y` (integer, 0-4096): Y coordinate
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
Press or release a specific key.

**Parameters:**
- `key` (integer): Qt key code
- `modifiers` (integer, optional): Modifier keys (Shift, Ctrl, Alt)
- `isKeyDown` (boolean): true for key press, false for release

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

## Architecture

```
+------------------------------------------------------------+
|                    Openterface QT App                        |
|                                                              |
|  +----------+    +----------------------------------+       |
|  |  GUI     |    |          MCP Server               |       |
|  |(MainWindow)->|  (McpServer.h/cpp)               |       |
|  |          |    |    +- McpProtocol.h/cpp          |       |
|  |  Toggle  |    |    +- McpToolHandler.h/cpp       |       |
|  |          |    |    +- mcpSseTransport.h/cpp      |       |
|  |          |    |    +- stdio/SSE transport        |       |
|  +----------+    +--------+-------------------------+       |
|       |                   |                                  |
|  +----v-------------------v--------------+                  |
|  |     Existing Components (Reused)      |                  |
|  |  HostManager (Singleton)              |                  |
|  |    +- MouseManager                    |                  |
|  |    +- KeyboardManager                 |                  |
|  |  CameraManager                        |                  |
|  |  ScriptRunner (Lexer/Parser/Executor) |                  |
|  |  SerialPortManager                    |                  |
|  +---------------------------------------+                  |
+--------------------------------------------------------------+
           ^
           | JSON-RPC 2.0
           |
    +------+------+
    |  MCP Client  |
    | (Claude/AI)  |
    +-------------+
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

## File Structure

```
server/
+- tcpServer.h/cpp          # Existing TCP server (preserved)
+- tcpResponse.h/cpp        # Existing TCP response handler (preserved)
+- mcp/
|   +- mcpServer.h/cpp          # MCP Server main class
|   +- mcpProtocol.h/cpp        # JSON-RPC 2.0 protocol handler
|   +- mcpToolHandler.h/cpp     # Tool registry and dispatch
|   +- mcpSseTransport.h/cpp    # SSE transport implementation
|   +- mcpConstants.h           # MCP protocol constants
```

## Command-Line Arguments

| Argument | Description |
|----------|-------------|
| `--mcp-stdio` | Run MCP server in stdio transport mode (headless) |
| `--mcp-sse-port <port>` | Run MCP server in SSE mode on specified port |
| `--mcp-start` | Auto-start MCP server after GUI launches (GUI mode only) |
| `--skip-env-check` | Skip environment check on startup |

## Testing Examples

### Test 1: stdio Mode - Initialize

```bash
echo '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2024-11-05","capabilities":{},"clientInfo":{"name":"test","version":"1.0"}}}' | ./openterfaceQT.exe --mcp-stdio
```

### Test 2: stdio Mode - List Tools

```bash
# Send initialization first
echo '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2024-11-05","capabilities":{},"clientInfo":{"name":"test","version":"1.0"}}}' | ./openterfaceQT.exe --mcp-stdio

# Then send tools/list
echo '{"jsonrpc":"2.0","id":2,"method":"tools/list"}' | ./openterfaceQT.exe --mcp-stdio
```

### Test 3: SSE Mode - Full Flow

```bash
# Terminal 1: Start server
./openterfaceQT.exe --mcp-sse-port 8080

# Terminal 2: Connect to SSE and get session ID
curl -N http://localhost:8080/sse

# Terminal 3: Send initialize request (replace <session-id> with actual ID)
curl -X POST "http://localhost:8080/messages?sessionId=<session-id>" \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2024-11-05","capabilities":{},"clientInfo":{"name":"test","version":"1.0"}}}'
```

### Test 4: Python MCP Client

```python
import subprocess
import json

# Start MCP server in stdio mode
proc = subprocess.Popen(
    ['./openterfaceQT.exe', '--mcp-stdio'],
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

## Performance Notes

- **Screen capture**: The `capture_screen` tool captures the current frame from the USB capture device. Quality settings affect JPEG compression and response size.
- **Mouse/Keyboard**: Commands are sent asynchronously via the serial port. Response confirms command was queued, not necessarily executed.
- **SSE sessions**: Sessions timeout after 5 minutes of inactivity. Keep-alive messages are sent every 30 seconds to maintain connections.

## Security Considerations

- The MCP server provides full control over the target computer's mouse and keyboard
- Run the server in a trusted environment only
- SSE transport does not include authentication - consider using a reverse proxy with auth for production use
- stdio transport is safer as it requires explicit client connection

## License

GNU General Public License v3.0

## Support

For issues and feature requests, please visit:
https://github.com/TechxArtisanStudio/Openterface_QT/issues

---



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
<build_dir>/openterfaceQT.exe --mcp-stdio --skip-env-check

# SSE (remote agent, specify port)
<build_dir>/openterfaceQT.exe --mcp-sse-port 8080 --skip-env-check

# Both modes simultaneously
<build_dir>/openterfaceQT.exe --mcp-stdio --mcp-sse-port 8080 --skip-env-check
```

Both modes provide the same 15 tools (mouse / keyboard / screenshot / script) with identical functionality.


## SSE Transport Test Results

**Test Date:** 2026-07-01  
**Status:** ✅ PASSED

### Test Execution

#### Server Configuration
```bash
<build_dir>/openterfaceQT.exe --mcp-sse-port 8080 --skip-env-check
```

#### Connection Details
- **SSE Endpoint:** http://localhost:8080/sse
- **Session ID:** 7bbbfeed-5134-46b2-8784-4447b7e602a6
- **Message Endpoint:** http://localhost:8080/messages?sessionId=7bbbfeed-5134-46b2-8784-4447b7e602a6

#### Workflow Test Results

| Step | Operation | Request ID | Response | Status |
|------|-----------|------------|----------|--------|
| 1 | Initialize | 1 | Server info received | ✅ |
| 2 | notifications/initialized | - | Accepted (202) | ✅ |
| 3 | tools/list | 2 | 15 tools listed | ✅ |
| 4 | capture_screen | 10 | JPEG image data | ✅ |
| 5 | keyboard_press_key (Space) | 11 | Key pressed+released | ✅ |
| 6 | keyboard_type_text ("bbot") | 12 | Typed 4 chars | ✅ |
| 7 | keyboard_press_key (Enter) | 13 | Key pressed+released | ✅ |
| 8 | capture_screen | 14 | JPEG image data | ✅ |

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

### Key Findings

1. **Session Management:** SSE sessions are properly tracked with unique UUIDs
2. **Asynchronous Communication:** POST requests return 202 Accepted immediately
3. **Response Delivery:** Responses are delivered via SSE event stream
4. **Binary Data:** Screen captures work correctly (base64-encoded JPEG)
5. **Error Handling:** All requests processed without errors

### Python Client Example (SSE Mode)

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
    
    # Execute unlock workflow
    send("tools/call", {"name":"capture_screen"}, req_id=10)
    time.sleep(1)
    send("tools/call", {"name":"keyboard_press_key",
                        "arguments":{"key":32,"modifiers":0,"autoRelease":True}}, req_id=11)
    time.sleep(1.5)
    send("tools/call", {"name":"keyboard_type_text",
                        "arguments":{"text":"bbot"}}, req_id=12)
    time.sleep(1)
    send("tools/call", {"name":"keyboard_press_key",
                        "arguments":{"key":16777220,"modifiers":0,"autoRelease":True}}, req_id=13)
    
    return session_id

session = sse_client()
print(f"Connected with session: {session}")
```

### Conclusion

The SSE transport is fully functional and provides:
- Remote/network-based MCP client support
- Asynchronous request/response handling
- Session-based connection management
- Real-time event streaming
- Full tool compatibility with stdio mode

