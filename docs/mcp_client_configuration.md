# MCP Client Configuration Guide

> **Note:** Before following this guide, you need to replace the following placeholders with your actual values:
> - `<your-server-ip>` — The IP address of the machine running the Openterface server (e.g., `192.168.1.100`)
> - `/path/to/openterfaceQT` — The actual path where openterfaceQT is installed on your system
> - `8080` — The port number configured in your Openterface MCP settings (default: 8080)
>
> To find your server IP, run `ip addr` or `ifconfig` on the server machine.

> **The Openterface MCP server supports standard SSE (Server-Sent Events) transport,
> which can be directly integrated into Claude Code, Claude Desktop, Cursor, Windsurf,
> Cline, Continue, and any other MCP-compatible AI agent.**
>
> SSE mode is the **recommended** integration method — the server runs as an independent
> daemon on the KVM host, and any client on the LAN can connect to it over HTTP.
> Multiple clients can share the same server simultaneously.

How to connect various AI clients to the Openterface MCP server — step by step,
with file paths, screenshots-equivalent instructions, and troubleshooting for
each client.

---

## Table of Contents

1. [Quick Start — Which Mode?](#1-quick-start--which-mode)
2. [Claude Code (CLI)](#2-claude-code-cli)
3. [Claude Desktop](#3-claude-desktop)
4. [Cursor](#4-cursor)
5. [Windsurf (Codeium)](#5-windsurf-codeium)
6. [Cline (VS Code)](#6-cline-vs-code)
7. [Continue (VS Code / JetBrains)](#7-continue-vs-code--jetbrains)
8. [Generic MCP SDK (Python)](#8-generic-mcp-sdk-python)
9. [Generic MCP SDK (TypeScript)](#9-generic-mcp-sdk-typescript)
10. [Direct HTTP Client (curl / Python)](#10-direct-http-client-curl--python)
11. [Verifying the Connection](#11-verifying-the-connection)
12. [Troubleshooting](#12-troubleshooting)

---

## 1. Quick Start — Which Mode?

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
> No need to start/stop the server per client.

### stdio vs SSE — Which Should I Use?

| | **SSE (Recommended)** | **stdio** |
|---|---|---|
| **Server lifecycle** | Runs once as a daemon | Launched per-client by the client |
| **Network** | LAN / remote access | Local machine only |
| **Multi-client** | ✅ Up to 16 simultaneous clients | ❌ One client at a time |
| **Claude Code** | ✅ Full support | ✅ Full support |
| **Claude Desktop** | ✅ Full support | ✅ Full support |
| **Cursor / Windsurf / Cline** | ✅ Full support | ⚠️ Varies by client |
| **AI Agent frameworks** | ✅ Any MCP SDK client | ❌ Must be on same machine |

> **Use SSE** when: the server and client are on different machines, you want
> multiple clients to share one server, or you're integrating with AI agent
> frameworks (LangChain, AutoGPT, CrewAI, etc.).
>
> **Use stdio** when: you want the simplest possible local setup and don't need
> remote access or multi-client support.

---

## 2. Claude Code (CLI)

### 2.1 Prerequisites

- Claude Code installed (`claude --version` should work)
- Openterface QT built on the same machine

### 2.2 Configuration

#### Option A — SSE (Recommended, for remote/LAN server)

First, start the server on the KVM host:

```bash
./openterfaceQT --mcp-sse-port 8080
```

Then configure Claude Code — run this command:

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

> This is the **recommended** way — the server runs as a daemon on the KVM host,
> and Claude Code connects over the network. Multiple Claude Code sessions (or
> other clients) can share the same server simultaneously.

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

**Project config** (applies to current project only):

Edit `.claude/settings.json` in your project root:

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

### 2.3 Environment Variables (Optional)

If the server needs specific environment variables (e.g., for display, library paths):

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

### 2.4 Verify

```bash
# Start Claude Code
claude

# In the Claude Code prompt, check MCP servers
/status

# You should see "openterface" listed with tool count
```

Or directly test:
```
> List the available MCP tools
```

Claude should list all 14 Openterface tools (mouse_move_absolute, keyboard_press_key, capture_screen, etc.).

### 2.5 File Paths by OS

| OS      | Global Config Path                          |
|---------|---------------------------------------------|
| Linux   | `~/.claude/settings.json`                   |
| macOS   | `~/.claude/settings.json`                   |
| Windows | `%APPDATA%\Claude\settings.json`            |

---

## 3. Claude Desktop

### 3.1 Prerequisites

- Claude Desktop app installed
- Openterface QT built on the same machine

### 3.2 Configuration

#### Option A — SSE (Recommended)

Start the server on the KVM host:

```bash
./openterfaceQT --mcp-sse-port 8080
```

Edit the Claude Desktop config:

| OS      | Config File Path                                                     |
|---------|----------------------------------------------------------------------|
| macOS   | `~/Library/Application Support/Claude/claude_desktop_config.json`    |
| Windows | `%APPDATA%\Claude\claude_desktop_config.json`                        |
| Linux   | `~/.config/Claude/claude_desktop_config.json`                        |

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

### 3.3 Windows Example

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

### 3.4 Restart Claude Desktop

After editing the config, fully quit and restart Claude Desktop:

1. Quit the app (not just close the window)
2. Reopen Claude Desktop
3. Start a new conversation
4. You should see a 🔌 (plug) icon indicating MCP servers are connected

### 3.5 Verify

In a new Claude Desktop conversation, type:

```
What tools do you have available?
```

Claude should list all Openterface tools. You can also check the Developer menu
for MCP server status.

---

## 4. Cursor

### 4.1 Prerequisites

- Openterface server running with SSE enabled:
  ```bash
  ./openterfaceQT --mcp-sse-port 8080
  ```
- The server must be reachable from the machine running Cursor (same LAN, firewall open)

### 4.2 Configuration

1. Open Cursor Settings → **Cursor Settings** → **Features** → **MCP Servers**
2. Click **+ Add New MCP Server**
3. Fill in:

| Field           | Value                                           |
|-----------------|-------------------------------------------------|
| Type            | `sse`                                           |
| Name            | `openterface`                                   |
| Server URL      | `http://<your-server-ip>:8080/sse`               |

4. Click **Add**

### 4.3 Alternative: Edit MCP Config File

Cursor also reads from a config file:

| OS      | Config File Path                                   |
|---------|----------------------------------------------------|
| macOS   | `~/.cursor/mcp.json`                               |
| Windows | `%USERPROFILE%\.cursor\mcp.json`                   |
| Linux   | `~/.cursor/mcp.json`                               |

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

### 4.4 Verify

In Cursor's AI chat, ask:

```
Can you see the Openterface MCP tools?
```

Cursor should detect and list the available tools. You can also check the MCP panel
in Cursor Settings for connection status.

### 4.5 stdio Mode (Local)

If Cursor and Openterface are on the same machine, you can use stdio:

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

---

## 5. Windsurf (Codeium)

### 5.1 Prerequisites

- Openterface server running with SSE:
  ```bash
  ./openterfaceQT --mcp-sse-port 8080
  ```

### 5.2 Configuration

1. Open Windsurf Settings → **Tools** → **MCP Servers**
2. Click **Add Server**
3. Select **SSE** as the transport type
4. Enter the URL: `http://<your-server-ip>:8080/sse`
5. Save

### 5.3 Config File Alternative

Windsurf reads from:

| OS      | Config File Path                                   |
|---------|----------------------------------------------------|
| macOS   | `~/.codeium/windsurf/mcp_config.json`              |
| Windows | `%USERPROFILE%\.codeium\windsurf\mcp_config.json`  |
| Linux   | `~/.codeium/windsurf/mcp_config.json`              |

```json
{
  "mcpServers": {
    "openterface": {
      "serverUrl": "http://<your-server-ip>:8080/sse"
    }
  }
}
```

### 5.4 Verify

In the Windsurf Cascade chat, ask:

```
List available MCP tools
```

---

## 6. Cline (VS Code)

### 6.1 Prerequisites

- VS Code with Cline extension installed
- Openterface server running with SSE

### 6.2 Configuration

1. Open VS Code → Cline sidebar
2. Click the **Settings** (gear) icon
3. Scroll to **MCP Servers**
4. Click **Edit MCP Settings**
5. Add the server:

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

### 6.3 stdio Mode

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

### 6.4 Auto-Approve Tools (Optional)

To let Cline use tools without asking for permission each time:

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

### 6.5 Verify

In the Cline chat panel, ask:

```
What MCP tools are available?
```

---

## 7. Continue (VS Code / JetBrains)

### 7.1 Prerequisites

- VS Code or JetBrains IDE with Continue extension installed
- Openterface server running with SSE

### 7.2 Configuration

Edit the Continue config file:

| IDE        | Config Path                                |
|------------|--------------------------------------------|
| VS Code    | `~/.continue/config.json`                  |
| JetBrains  | `~/.continue/config.json`                  |

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

### 7.3 Verify

In the Continue chat, use `@` to mention tools or ask:

```
@openterface what can you do?
```

---

## 8. Generic MCP SDK (Python)

For custom integrations using the official MCP Python SDK.

### 8.1 Install

```bash
pip install mcp
```

### 8.2 SSE Client

```python
import asyncio
from mcp.client.sse import sse_client
from mcp import ClientSession


async def main():
    # Connect to the Openterface SSE server
    async with sse_client("http://<your-server-ip>:8080/sse") as (read, write):
        async with ClientSession(read, write) as session:
            # Initialize
            await session.initialize()

            # List tools
            tools = await session.list_tools()
            print("Available tools:")
            for tool in tools.tools:
                print(f"  - {tool.name}: {tool.description[:60]}...")

            # Call a tool: capture screen
            result = await session.call_tool("capture_screen", {"quality": 75})
            for content in result.content:
                if hasattr(content, "data"):
                    import base64
                    img = base64.b64decode(content.data)
                    with open("/tmp/screenshot.jpg", "wb") as f:
                        f.write(img)
                    print(f"\nScreenshot saved ({len(img)} bytes)")

            # Call a tool: move mouse
            result = await session.call_tool(
                "mouse_move_absolute", {"x": 2048, "y": 2048}
            )
            print(result.content[0].text)

            # Call a tool: press Ctrl+C
            result = await session.call_tool(
                "keyboard_press_key", {"key": 67, "modifiers": 2}
            )
            print(result.content[0].text)


asyncio.run(main())
```

### 8.3 stdio Client

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

            # Use tools...
            result = await session.call_tool("system_status")
            print(result.content[0].text)


asyncio.run(main())
```

---

## 9. Generic MCP SDK (TypeScript)

### 9.1 Install

```bash
npm install @modelcontextprotocol/sdk
```

### 9.2 SSE Client

```typescript
import { Client } from "@modelcontextprotocol/sdk/client/index.js";
import { SSEClientTransport } from "@modelcontextprotocol/sdk/client/sse.js";

async function main() {
  const transport = new SSEClientTransport(new URL("http://<your-server-ip>:8080/sse"));
  const client = new Client({ name: "my-client", version: "1.0.0" });

  await client.connect(transport);

  // List tools
  const { tools } = await client.listTools();
  console.log("Available tools:");
  tools.forEach((t: any) => console.log(`  - ${t.name}`));

  // Capture screen
  const result = await client.callTool({
    name: "capture_screen",
    arguments: { quality: 75 },
  });
  console.log("Capture result:", result);

  // Move mouse
  await client.callTool({
    name: "mouse_move_absolute",
    arguments: { x: 2048, y: 2048 },
  });

  // Ctrl+C
  await client.callTool({
    name: "keyboard_press_key",
    arguments: { key: 67, modifiers: 2 },
  });
}

main().catch(console.error);
```

---

## 10. Direct HTTP Client (curl / Python)

For cases where you don't want to use the MCP SDK — just raw HTTP.

### 10.1 curl Helper Script

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

### 10.2 Python (No SDK)

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

---


## 10.5 AI Agent Framework Integration

The Openterface MCP SSE server can be used as a tool provider for any AI agent
framework that supports MCP or HTTP-based tool calling.

### LangChain / LangGraph

```python
from langchain_mcp_adapters.client import MultiServerMCPClient

async with MultiServerMCPClient({
    "openterface": {
        "transport": "sse",
        "url": "http://<your-server-ip>:8080/sse"
    }
}) as client:
    tools = client.get_tools()
    # tools now contains: mouse_move_absolute, keyboard_press_key, capture_screen, etc.

    # Use with an LLM
    from langgraph.prebuilt import create_react_agent
    from langchain_openai import ChatOpenAI

    agent = create_react_agent(ChatOpenAI(model="gpt-4o"), tools)
    result = await agent.ainvoke({
        "messages": [{"role": "user", "content": "Take a screenshot of the target machine"}]
    })
```

### CrewAI

```python
from crewai import Agent, Task, Crew
from crewai.tools import tool
import httpx, json, re, threading, time, base64

# Wrap Openterface MCP tools as CrewAI tools
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

# Create agent
kvm_agent = Agent(
    role="Remote KVM Operator",
    goal="Control the target computer via Openterface Mini-KVM",
    backstory="You are an expert at remote computer management using KVM tools.",
    tools=[capture_screen, press_key, move_mouse],
    verbose=True
)
```

### AutoGPT / AgentGPT

Register the Openterface SSE endpoint as an MCP tool server in your agent's config:

```yaml
mcp_servers:
  - name: openterface
    transport: sse
    url: http://<your-server-ip>:8080/sse
```

### OpenAI Agents SDK

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

### Dify / Coze / n8n (Low-Code Platforms)

These platforms support MCP tool integration via HTTP. Configure:

| Setting          | Value                                      |
|------------------|--------------------------------------------|
| Transport        | SSE                                        |
| Server URL       | `http://<your-server-ip>:8080/sse`          |
| Auth             | None (LAN trusted network)                 |

The platform will automatically discover all 14 tools and expose them as nodes
in your workflow.

### Custom Agent (Any Language)

The SSE protocol is just HTTP — any language can integrate:

1. `GET /sse` → open SSE stream, extract `sessionId`
2. `POST /messages?sessionId=<id>` → send JSON-RPC tool calls
3. Read responses from the SSE stream

See §10 (Direct HTTP Client) for curl and Python examples.

## 11. Verifying the Connection

After configuring any client, verify the connection:

### 11.1 Check Server is Running

```bash
# Is the process alive?
ps aux | grep openterfaceQT | grep -v grep

# Is the port open?
ss -tlnp | grep 8080

# Can we reach the SSE endpoint?
curl -s http://<your-server-ip>:8080/sse -N --max-time 3
```

### 11.2 Check Tool Discovery

In any MCP client, ask:

```
List all available tools
```

Expected response should include these 14 tools:

```
mouse_move_absolute    mouse_click           mouse_move_relative
mouse_scroll           keyboard_press_key    keyboard_type_text
keyboard_function_key  keyboard_ctrl_alt_del keyboard_set_layout
capture_screen         capture_last_image    execute_script
system_status
```

### 11.3 Quick Functional Test

```
Move the mouse to the center of the screen, then take a screenshot
```

If you see a screenshot returned, the full pipeline (SSE → MCP → mouse HID → video
capture → JPEG) is working end-to-end.

---

## 12. Troubleshooting

### "MCP server failed to start"

**Cause:** The `command` path is wrong or the binary is not executable.

**Fix:**
```bash
# Verify the binary exists and is executable
ls -la /path/to/openterfaceQT
chmod +x /path/to/openterfaceQT

# Test it manually
/path/to/openterfaceQT --mcp-stdio
```

### "Connection refused" (SSE mode)

**Cause:** Server not running, or firewall blocking the port.

**Fix:**
```bash
# Start the server
./openterfaceQT --mcp-sse-port 8080

# Open the firewall
sudo firewall-cmd --add-port=8080/tcp --permanent && sudo firewall-cmd --reload
```

### "No tools available"

**Cause:** The server started but failed to initialize camera/HID.

**Fix:** Check server logs for errors. Usually HID permission issue — apply the
udev rule (see main MCP guide §5.3).

### Claude Code shows "Server disconnected"

**Cause:** The server process crashed or the stdio pipe broke.

**Fix:**
```bash
# Check if the server is still running
ps aux | grep openterfaceQT

# Check if HID device is accessible
ls -la /dev/hidraw4
# Should show: crw-rw-rw- (not crw-------)
```

### Cursor / Windsurf: "SSE connection timeout"

**Cause:** Network unreachable or IP address wrong.

**Fix:**
```bash
# Verify the server IP from the client machine
ping <your-server-ip>

# Verify the port is reachable
curl -v http://<your-server-ip>:8080/sse --max-time 5
```

### "ScriptRunner not available"

**Cause:** Using headless mode. `execute_script` and `keyboard_send_keys` require
the GUI mode with full MainWindow initialization.

**Workaround:** Use `keyboard_press_key` or `keyboard_type_text` instead.

### Permission errors in server logs

```
[DEBUG-HID] open failed: /dev/hidraw4, error: 权限不够
```

**Fix:** See the main MCP guide §5.3 — apply the udev rule for HID device access.

---

## Quick Reference Card

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

*Last updated: 2025-06-26*
