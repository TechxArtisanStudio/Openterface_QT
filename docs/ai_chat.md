# AI Chat

The AI Chat feature lets you control the **target computer** (the machine connected via the Openterface KVM) using natural language. An AI model views the target's screen, decides what to do, and sends keyboard/mouse commands through the USB HID interface — as if you were sitting at the keyboard.

All chat logic lives in two directories:

| Directory | Purpose |
|-----------|---------|
| `ai/` | Backend: API calls, agent loop, tool execution, persistence |
| `ui/chat/` | Frontend: chat window, message bubbles, settings page, trace viewer |

---

## Table of Contents

- [Architecture Overview](#architecture-overview)
- [Chat Modes](#chat-modes)
- [Agent Tool Reference](#agent-tool-reference)
- [Agent Loop Flow](#agent-loop-flow)
- [Skills System](#skills-system)
- [Guide Mode](#guide-mode)
- [Planner Mode](#planner-mode)
- [Configuration](#configuration)
- [User Interface](#user-interface)
- [Persistence & Tracing](#persistence--tracing)
- [Timing & Synchronization](#timing--synchronization)
- [Advanced Features](#advanced-features)
  - [OpenAI Native Function Calling Support](#openai-native-function-calling-support)
  - [Shared Tool Executor Architecture](#shared-tool-executor-architecture)
  - [Web Search Integration](#web-search-integration)
  - [Tools Configuration Tree](#tools-configuration-tree)
  - [Cursor Blink Detection](#cursor-blink-detection)
  - [Advanced MCP Integration](#advanced-mcp-integration)

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────┐
│  ChatWindow (ui/chat/ChatWindow)                    │
│  ┌───────────┐ ┌────────────┐ ┌───────────────────┐│
│  │ SkillBar  │ │ PlanCard   │ │ ScrollArea        ││
│  │           │ │            │ │  ┌──────────────┐ ││
│  │           │ │            │ │  │ BubbleWidgets│ ││
│  │           │ │            │ │  └──────────────┘ ││
│  └───────────┘ └────────────┘ │  ┌──────────────┐ ││
│                                │  │ InputWidget  │ ││
│                                │  └──────────────┘ ││
│                                └───────────────────┘│
└────────────────────────┬────────────────────────────┘
                         │ signals/slots
┌────────────────────────▼────────────────────────────┐
│  ChatManager (singleton)                            │
│  - Message history                                  │
│  - Mode dispatch (Agent / Planner / Guide)            │
│  - Agent loop orchestration                         │
└──┬──────────┬──────────┬──────────┬────────────────┘
   │          │          │          │
   ▼          ▼          ▼          ▼
ChatApi    ChatConver-  ChatTool   ChatScreen
Client     sationBldr   Execution  Capture
   │          │          │          │
   ▼          ▼          ▼          ▼
OpenAI     Builds API   Parses &   Grabs frame
-compatible  messages,   executes   from camera,
HTTP POST  strips      tools via    encodes as
             JSON,       HID,       base64
             attaches    mouse,
             images      keyboard
```

### Key Singletons

| Singleton | File | Responsibility |
|-----------|------|----------------|
| `ChatManager` | `ai/ChatManager.cpp` | Main orchestrator. Holds message history, dispatches to the right mode, runs the agent loop. |
| `ChatApiClient` | `ai/ChatApiClient.cpp` | Sends POST to `{baseURL}/chat/completions`. OpenAI-compatible. Uses `QNetworkAccessManager`. |
| `ChatConversationBuilder` | `ai/ChatConversationBuilder.cpp` | Builds the message array sent to the API. Strips tool-call JSON from history, attaches images, injects agent instructions. |
| `ChatToolExecution` | `ai/ChatToolExecution.cpp` | Parses tool-call JSON from AI responses. Executes tools (click, type, capture, bash). |
| `ChatScreenCapture` | `ai/ChatScreenCapture.cpp` | Grabs the current video frame from `CameraManager`, saves as JPEG, converts to base64 data URL. |
| `ChatInputRouter` | `ai/ChatInputRouter.cpp` | Routes mouse/keyboard commands to the target via `HostManager` (USB HID). Animated clicks. |
| `ChatGuideMode` | `ai/ChatGuideMode.cpp` | Parses guide responses, executes overlay + input sequences, handles auto-next. |
| `ChatSkillManager` | `ai/ChatSkillManager.cpp` | Loads skill JSON files from the skills folder. |
| `ChatPersistence` | `ai/ChatPersistence.cpp` | Saves/loads chat history as JSON in `QStandardPaths::AppDataLocation`. |
| `ChatTracing` | `ai/ChatTracing.cpp` | Appends request/response traces to a log file for debugging. |

---

## Chat Modes

The system supports three modes, selectable in Settings → AI Chat:

### Agent Mode
The AI can **directly execute actions** on the target. It sees the screen, decides what to do, issues tool calls (click, type, capture, etc.), sees the result, and continues — all in a loop up to the configured max iterations. This is the primary mode for remote control.

The agent instruction (injected as a system message) teaches the model:
- The distinction between HOST (local machine) and TARGET (remote machine via KVM)
- How to analyze the screenshot each iteration
- Step-by-step recipes for common operations (opening a terminal, typing a command)
- When to use `run_bash` (HOST only) vs `type_text` + `press_key` (TARGET)

### Planner Mode
The AI first **creates a multi-step execution plan** (a list of tasks) and presents it for approval. Each task is assigned to a specialized agent:

| Agent | Tool | Purpose |
|-------|------|---------|
| `ScreenTaskAgent` | `capture_screen` | Verify screen state |
| `TypeTextTaskAgent` | `type_text` | Determine what text/shortcut to type |
| `MouseTaskAgent` | `left_click` / `right_click` | Determine click coordinates |

Once you approve the plan, tasks are executed sequentially. Each task's result feeds into the next.

### Guide Mode
Turn-by-turn guidance. The AI gives you one step at a time, draws an overlay rectangle on the video pane showing where to click, and optionally auto-advances to the next step. You can:
- **Execute** — perform the highlighted action
- **Execute & Next** — perform the action and immediately get the next step
- **I Did This** — mark the step complete and get the next one

---

## Agent Tool Reference

When the AI responds with a tool-call JSON block, `ChatToolExecution` parses and executes it. Available tools:

| Tool | Arguments | Description |
|------|-----------|-------------|
| `capture_screen` | _(none)_ | Capture the current target screen. Returns a screenshot attached to the next API call. |
| `move_mouse` | `x` (0.0–1.0), `y` (0.0–1.0) | Move the mouse cursor to a normalized position on the target. |
| `left_click` | `x` (0.0–1.0), `y` (0.0–1.0) | Left-click at a normalized position. |
| `right_click` | `x` (0.0–1.0), `y` (0.0–1.0) | Right-click at a normalized position. |
| `double_click` | `x` (0.0–1.0), `y` (0.0–1.0) | Double-click at a normalized position. |
| `left_drag` | `x` (0.0–1.0), `y` (0.0–1.0) | Drag from current position to the given coordinates. |
| `type_text` | `text` (string) | Type text on the target keyboard via USB HID. Uses batched keystroke simulation. |
| `press_key` | `keys` (string, e.g. `"ctrl+l"`, `"enter"`) | Press a key combination on the target via USB HID. |
| `repeat_key` | `keys` (string), `count` (int), `interval_ms` (int, default 1000) | Press a key repeatedly at specified intervals. Useful for entering BIOS (pressing DEL), accessing boot menus (F12, F2), or any situation requiring repeated key presses. |
| `reboot_to_bios` | `bios_key` (string, default "del"), `delay_before_press_ms` (int, default 2000), `press_count` (int, default 20), `interval_ms` (int, default 1000) | Press the BIOS/UEFI entry key repeatedly after a delay. This tool assumes a reboot has ALREADY been initiated via other means (e.g., GUI click on reboot button, type_text 'reboot' + press_key 'enter', etc.). It does NOT send the reboot command itself. After the specified delay, it automatically presses the BIOS key WITHOUT waiting for screen capture, ensuring the BIOS entry window is not missed due to agent thinking time. Use when user asks to "boot into BIOS", "enter BIOS setup", or "access UEFI firmware settings". |
| `run_bash` | `command` (string) | Run a shell command on the **HOST** machine (the one running Openterface). Not the target. |

### Tool-Call JSON Format

The AI emits tool calls as JSON in its response:

```json
{"tool_calls": [{"tool": "type_text", "text": "ls -la"}, {"tool": "press_key", "keys": "enter"}]}
```

`ChatConversationBuilder::stripToolCallJson()` removes these JSON blocks from assistant messages before re-sending history to the API, so the model doesn't see its own stale tool calls and re-execute them.

---

## Agent Loop Flow

This is the sequence for a single user request in Agent mode:

```
User sends message
        │
        ▼
┌─ sendMessage() ─────────────────────────────────────┐
│  1. Append user message to history                  │
│  2. Auto-capture screenshot on main thread          │
│     (CameraManager is NOT thread-safe)              │
│  3. Start agent request status indicator            │
│  4. Persist history                                 │
│  5. Spawn QtConcurrent worker → performSend()       │
└─────────────────────────────────────────────────────┘
        │
        ▼  (background thread)
┌─ performStandardSend() ─────────────────────────────┐
│  Loop (iteration 1..maxIterations):                 │
│                                                     │
│  1. If iteration > 1: re-capture screen             │
│     (tools may have changed it)                     │
│  2. Update status label: "Examining screen (X/Y)..."│
│  3. Insert step indicator bubble (isStatusHint)     │
│  4. Build conversation via ChatConversationBuilder  │
│     - System prompt                                 │
│     - Agent tool instructions                       │
│     - History (tool JSON stripped from assistant)   │
│     - Image attached to last user message           │
│  5. Trace the request                               │
│  6. POST to API (sendCompletionSync)                │
│  7. Trace the response                              │
│  8. Parse tool calls from response                  │
│     ├─ No tools → append response, break            │
│     └─ Has tools:                                   │
│        a. Append assistant response to history      │
│        b. Execute tools (ChatToolExecution)         │
│        c. Append TOOL_RESULT as user message        │
│        d. Update imageDataURL if new screenshot     │
│        e. Persist history                           │
│        f. Continue to next iteration                │
│                                                     │
│  After loop: complete agent request status          │
└─────────────────────────────────────────────────────┘
```

### Status Indicators

During the agent loop, two types of ephemeral feedback are shown:

1. **Status label** (grey italic text above the input area): `"Examining screen (2/10)..."` or `"Thinking (3/10)..."`
2. **Step indicator bubbles** in the chat: `"🔍 Step 2/10 — examining current screen..."` styled as centered, grey, italic text (not a real message bubble)

These are `isStatusHint = true` messages — they appear in the UI but are **not persisted** and **not sent to the API**.

---

## Skills System

Skills are pre-defined prompts that can be triggered with a single click from the skill bar.

### Storage

Skills are JSON files stored in the skills folder:

```
QStandardPaths::AppDataLocation/skills/
```

Each file:

```json
{
  "id": "my-skill",
  "name": "Check Disk Usage",
  "icon": "drive-harddisk",
  "prompt": "Run 'df -h' to check disk usage and summarize the results.",
  "captureScreen": true,
  "userLabel": "Disk Usage"
}
```

| Field | Description |
|-------|-------------|
| `id` | Unique identifier |
| `name` | Internal name |
| `icon` | Theme icon name or resource path |
| `prompt` | The message sent to the AI |
| `captureScreen` | Whether to auto-capture a screenshot before sending |
| `userLabel` | Optional display label (overrides `name` in the UI) |

### Lifecycle

- `ChatSkillManager::instance()` loads skills on startup via `seedAndLoad()`
- `loadFromFolder()` reads all `.json` files from the skills folder
- Deprecated skill IDs are filtered out and their files auto-deleted
- Skills appear as clickable buttons in the `ChatSkillBar`
- When clicked, `ChatManager::runSkill()` is called — optionally capturing the screen first, then calling `sendMessage()` with the skill's prompt

---

## Guide Mode

Guide mode provides turn-by-turn instructions overlaid on the target screen.

### How It Works

1. The AI response is parsed by `ChatGuideMode::parseGuideResponse()` which extracts:
   - `next_step` — text describing what to do
   - `target_box` — normalized (0–1) rectangle for the overlay
   - `tool` — which action to perform (`left_click`, `right_click`, etc.)
   - `tool_input` — parameters for the action
   - `shortcut` — keyboard shortcut (e.g. `"ctrl+alt+t"`)
   - `isComplete` — whether the overall task is done

2. A highlight rectangle is drawn on the video pane via `guideOverlayRequested` signal

3. The user clicks one of three buttons on the guide message bubble:
   - **Execute** — perform the action
   - **Execute & Next** — perform the action, then auto-request the next step
   - **I Did This** — mark complete, request next step without executing

4. Input sequences can include shortcuts and text, parsed by `parseBracketedGuideInputSteps()`:
   ```
   {ctrl+alt+t}type:ls -la{enter}
   ```

5. After each step, a configurable delay (`guideDelayAfterStep`) is applied before the next action.

---

## Planner Mode

Planner mode creates a structured execution plan before taking action.

### Plan Structure

```
ChatExecutionPlan
├── goal: "Check disk usage and list large files"
├── summary: "Multi-step plan to analyze disk usage"
├── status: Draft → AwaitingApproval → Approved → Running → Completed/Failed
├── tasks[]:
│   ├── ChatTask { title, detail, agentName, toolName, status, resultSummary }
│   ├── ChatTask { ... }
│   └── ...
```

### Plan Lifecycle

1. User sends a request → `performPlannerSend()` is called
2. `MainPlannerAgent` builds a planning conversation and sends it to the API
3. The response is parsed as a plan JSON (up to `maxPlannerTasks` tasks)
4. Plan is presented in the `ChatPlanCardWidget` with status `AwaitingApproval`
5. User clicks **Approve** → `approveCurrentPlan()` → `executeApprovedPlan()`
6. Tasks are executed sequentially, each dispatched to the appropriate `TaskAgentExecutor`
7. Each task's result feeds into the next task's conversation

### Task Agents

| Agent | Class | Handles |
|-------|-------|---------|
| Screen | `ScreenTaskAgent` | Verifying screen state via `capture_screen` |
| Typing | `TypeTextTaskAgent` | Determining what text/shortcut to type |
| Mouse | `MouseTaskAgent` | Determining click coordinates for `left_click` / `right_click` |

`TaskAgentRegistry::resolve()` maps a task to the correct agent based on `agentName` and `toolName`.

---

## Configuration

All settings are in **Settings → AI Chat** (`ChatSettingsPage`):

### API Configuration

| Setting | Description | Default |
|---------|-------------|---------|
| Base URL | OpenAI-compatible API endpoint | `https://api.openai.com/v1` |
| API Key | Authentication key (also reads `OPENAI_API_KEY` env var) | _(empty)_ |
| Model | Model name | `gpt-4o-mini` |

### Target & Mode

| Setting | Description | Range |
|---------|-------------|-------|
| Target System | OS context for the AI (affects key bindings, paths) | Linux, macOS, Windows, iPhone, iPad, Android |
| Agent Max Iterations | Maximum loop iterations per request | 1–30 |
| Typing Delay | Delay between keystrokes (ms) | 0–1000 |
| Batch Size | Characters typed per batch before a pause | 1–50 |
| Mode | Agent / Planner / Guide | — |

### Prompts

Five configurable prompts (editable in tabs):

| Prompt | Used By |
|--------|---------|
| **System** | Base instruction for all modes |
| **Planner** | Plan generation in Planner mode |
| **Screen Task** | Screen verification agent in Planner mode |
| **Typing Task** | Typing agent in Planner mode |
| **Guide** | Step-by-step guidance in Guide mode |

### Prompt Construction by Mode

Each mode constructs the conversation sent to the AI model differently:

#### Agent Mode
The conversation is built by `ChatConversationBuilder::buildConversation()`:

1. **System prompt** (from Settings) — base instruction defining the assistant's role and capabilities
2. **Agent tool instruction** (hardcoded in `agentToolInstruction()`) — appended automatically when `includeAgentTools=true`. Contains:
   - Critical distinction between HOST (local) and TARGET (remote via KVM)
   - Screen awareness instructions (analyze screenshots, capture before asserting)
   - Step-by-step recipes for common operations (opening terminal, typing commands)
   - Available tools reference with descriptions
   - Tool-call JSON format specification

```
[System message 1] → Settings → System prompt
[System message 2] → Hardcoded agent tool instruction
[User/Assistant...] → Conversation history
[User message + image] → Last user message with screenshot
```

#### Planner Mode
The conversation is built by `MainPlannerAgent::buildPlanningConversation()`:

1. **System prompt** (from Settings) — base instruction
2. **Available task agents/tools** (hardcoded) — lists the agent/tool pairs the planner can use
3. **Planner prompt** (from Settings) — specific instructions for plan generation, including JSON schema
4. **User request** — the user's message with screenshot attached

```
[System message 1] → Settings → System prompt
[System message 2] → Hardcoded list of available task agents/tools
[System message 3] → Settings → Planner prompt (with JSON schema)
[User message + image] → User request with screenshot
```

#### Guide Mode
The conversation is built by concatenating prompts before calling `buildConversation()`:

1. **Combined system prompt** = `System prompt + "\n\n" + Guide prompt`
2. No agent tool instruction is added (`includeAgentTools=false`)
3. Conversation history and user message with screenshot

```
[System message] → Settings → (System prompt + "\n\n" + Guide prompt)
[User/Assistant...] → Conversation history
[User message + image] → Last user message with screenshot
```

#### Task Agents (Planner Mode Execution)
When executing a plan, each task agent builds its own conversation:

1. **System prompt** (from Settings) — base instruction
2. **Task-specific prompt** (from Settings) — e.g., Screen Task prompt or Typing Task prompt
3. **Task instruction** — includes plan summary, task title, task detail, and tool name

```
[System message 1] → Settings → System prompt
[System message 2] → Settings → Task-specific prompt (Screen/Typing/Mouse)
[User message + image] → Task instruction with screenshot
```

### Key Differences

| Mode | System Prompt | Additional Instructions | Agent Tools | Multi-turn |
|------|---------------|------------------------|-------------|------------|
| **Agent** | ✓ | Hardcoded tool instruction | ✓ | ✓ |
| **Planner** | ✓ | Hardcoded task list + Planner prompt | ✗ | ✗ (single-shot) |
| **Guide** | ✓ + Guide prompt (concatenated) | ✗ | ✗ | ✓ |
| **Task Agents** | ✓ | Task-specific prompt | ✗ | ✗ (single-shot) |

---

## User Interface

### ChatWindow Layout

```
┌──────────────────────────────────┐
│ [Mode ▾]  [New Session]  [Trace]│  ← Top bar
├──────────────────────────────────┤
│ [Skill1] [Skill2] [Skill3] ...  │  ← Skill bar
├──────────────────────────────────┤
│ ┌──────────────────────────────┐ │
│ │ Plan Card (when active)      │ │  ← Plan approval UI
│ │ [Approve] [Clear]            │ │
│ └──────────────────────────────┘ │
├──────────────────────────────────┤
│                                  │
│  You                    10:32 AM │  ← User bubble
│  ┌──────────────────────────┐   │
│  │ Check disk usage          │   │
│  └──────────────────────────┘   │
│                                  │
│  ╌ Step 1/10 — examining... ╌   │  ← Status hint
│                                  │
│  AI Assistant          10:32 AM │  ← Assistant bubble
│  ┌──────────────────────────┐   │
│  │ I see a terminal open... │   │
│  │ {"tool_calls": [...]}    │   │  ← JSON hidden in API, shown in UI
│  └──────────────────────────┘   │
│  [Copy]                         │
│                                  │
│  Examining screen (2/10)...      │  ← Status label
├──────────────────────────────────┤
│  [Type a message...        ] [>] │  ← Input (Shift+Enter to send)
└──────────────────────────────────┘
```

### Keyboard Shortcuts

| Shortcut | Action |
|----------|--------|
| `Shift+Enter` | Send message |
| `Ctrl+Enter` | Send message |

### Message Bubble Types

| Type | Style | Description |
|------|-------|-------------|
| User | Blue background | Your messages |
| Assistant | Grey background | AI responses (includes tool-call JSON for display) |
| System | Yellow background | System messages |
| Status Hint | Centered italic, no background | Ephemeral step indicators ("Step 2/10 — ...") |

### Action Buttons

- **Copy** — Copy assistant message text to clipboard
- **Execute** / **Execute & Next** / **I Did This** — Guide mode actions

---

## Persistence & Tracing

### Chat History

- Stored as JSON in `QStandardPaths::AppDataLocation/chat_history.json`
- Saved after every agent loop iteration and after every message
- Includes: messages, current plan (if any), planner trace entries
- **Excludes**: `isStatusHint` messages (ephemeral step indicators)
- Restored on application startup via `ChatManager` constructor

### Trace Log

- Stored in `QStandardPaths::AppDataLocation/ai_trace.log`
- Records every API request (full conversation) and response (first 500 chars)
- Viewable via the **Trace** button in the chat window (`ChatTraceDialog`)
- Can be cleared via the **Clear** button in the trace dialog (with confirmation)

---

## Timing & Synchronization

The USB HID interface is asynchronous — keystrokes take time to arrive at the target. The system uses several delays to prevent race conditions:

| Constant | Value | Purpose |
|----------|-------|---------|
| `MOUSE_TO_KEYBOARD_DELAY_MS` | 800 ms | Delay after mouse action before keyboard action (click → type). Gives the target OS time to process the click and shift keyboard focus. |
| `POST_KEYBOARD_SETTLE_MS` | 400 ms | Delay after keyboard action before next tool (type → capture). Lets the target OS render the result. |
| `PRE_CAPTURE_DELAY_MS` | 400 ms | Delay before capture_screen to let the screen update |

### Priming Key Event (USB HID Reset)

**Problem:** After sending a modifier key sequence (like `ctrl+alt+t` to open a terminal), the first character of subsequent `type_text` commands could be lost. For example, typing "top" would result in "op" — the 't' was missing.

**Root Cause:** The CH9329 USB HID chip retained residual state from the modifier key sequence. Even though the first character was transmitted correctly over serial, the chip/target OS didn't process it because the HID channel wasn't in a clean state.

**Solution:** Before typing starts, a **priming null key event** is sent — `CMD_SEND_KB_GENERAL_DATA` with all zeros (modifier byte = 0x00, all 6 keycode slots = 0x00). This resets the USB HID channel to a clean state, ensuring the first real character is processed correctly.

The priming event is sent in `KeyboardManager::handlePastingCharacters()` via `QTimer::singleShot()` after a 500ms initial delay:

```
500ms delay → priming null event → 50ms wait → start typing
```

This approach was discovered through extensive testing with serial TX logging, which showed the 't' character WAS being transmitted correctly but not appearing on the target screen.

### Initial Typing Delay

Before the first character is typed, there's a **500ms delay** (via `QTimer::singleShot`). This gives the target OS time to:
- Process a preceding mouse click and shift focus to the clicked window
- Open a new terminal window after a shortcut like `ctrl+alt+t`
- Render the window and be ready to receive keystrokes

Combined with `MOUSE_TO_KEYBOARD_DELAY_MS` (800ms), a click→type sequence has ~1300ms total delay, ensuring reliable character delivery.

### Typing Duration Estimation

`ChatToolExecution::estimateTypingDurationMs(charCount)` calculates how long `type_text` will take based on:
- 500ms initial delay (before first character)
- `getChatTypingDelayMs()` — per-character delay
- `getChatBatchSize()` — characters per batch
- Batch delay between groups

This is used to block the background thread until typing on the main thread is expected to have finished, so subsequent tools (e.g. `press_key "enter"`) don't race with in-flight keystrokes.

### Screen Capture Thread Safety

`CameraManager::getLatestOriginalFrame()` is **not thread-safe** — it touches GStreamer pipeline objects that must live on the main thread. Therefore:
- The initial screenshot is captured on the **main thread** in `sendMessage()` before spawning the worker
- Subsequent iterations capture on the **main thread** via `QMetaObject::invokeMethod` (queued connection) from the background worker

### Screen Awareness

In Agent mode, the screen is re-captured at every iteration (except the first, which uses the auto-capture from `sendMessage()` time). The agent instruction tells the model to:
- State what it sees on screen in its response
- Not assume screen state from prior iterations
- Call `capture_screen` if the screenshot is missing or unclear

### BIOS Boot Timing Challenge

**Problem:** When a user asks to "boot into BIOS", the normal agent loop faces a critical timing issue:
1. Agent sends reboot command to target
2. Agent captures screen (blank during reboot)
3. Agent analyzes blank screen and "thinks" (API call takes 1-3 seconds)
4. Agent decides to press Del key
5. By this time, the BIOS entry window (typically 2-5 seconds after power-on) has already passed

The agent's thinking time makes it impossible to catch the BIOS entry window reliably.

**Solution:** The `reboot_to_bios` tool bypasses the normal agent loop timing by:
1. Waiting for a configurable delay (default 2000ms) after reboot has been initiated
2. Automatically pressing the BIOS key repeatedly WITHOUT waiting for screen capture or AI response
3. Completing the entire key-pressing sequence in a single tool execution

**Example Usage:**
```json
// First: Initiate reboot via GUI click or command
{
  "tool_calls": [
    {"tool": "type_text", "text": "reboot"},
    {"tool": "press_key", "keys": "enter"}
  ]
}

// Then: Press BIOS key repeatedly after delay
{
  "tool_calls": [
    {
      "tool": "reboot_to_bios",
      "bios_key": "del",
      "delay_before_press_ms": 2000,
      "press_count": 20,
      "interval_ms": 1000
    }
  ]
}
```

This workflow:
1. First sends the reboot command to the target
2. Then immediately calls reboot_to_bios, which waits 2 seconds and presses the Del key 20 times (once per second)
3. Ensures the BIOS setup is entered even if the agent is still processing

**Supported BIOS Keys:** The `bios_key` parameter supports any key that `repeat_key` supports, including:
- `del` / `delete` (most common for desktop BIOS)
- `f2` (common for laptops)
- `f10`, `f12` (boot menu keys)
- `esc` (some systems)
- Any modifier+key combination like `ctrl+alt+del`

---

## Advanced Features

This section covers advanced capabilities and architectural improvements in the AI Chat system.

### OpenAI Native Function Calling Support

The AI Chat system now supports OpenAI's native function calling format, providing seamless integration with models that use structured tool calls instead of JSON-embedded-in-text.

#### Background

OpenAI's function calling API returns tool calls in a structured format:

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

#### Implementation

The fix adds native support for the `tool_calls` array:

**1. Data Structure Enhancement** (`ai/ChatTypes.h`)

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

**2. API Response Parsing** (`ai/ChatApiClient.cpp`)

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

**3. Tool Call Execution** (`ai/ChatManager.cpp`)

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

#### Benefits

- **Compatibility**: Works with all OpenAI-compatible models (GPT-4, GPT-3.5, Claude, etc.)
- **Reliability**: No more "empty tool_calls" errors
- **Proper API Integration**: Tool call IDs are preserved for correct conversation flow
- **Backward Compatibility**: Legacy text-based tool calls still work
- **Better Error Handling**: Empty content is only an error if no tool calls exist

#### Testing

The implementation has been verified with:
- MCP server tool execution (system_status, capture_screen)
- Multiple simultaneous tool calls
- Tool call ID preservation across conversation turns
- Backward compatibility with text-based format

### Shared Tool Executor Architecture

The AI Chat and MCP systems now share a common tool execution layer, eliminating code duplication and ensuring consistent behavior.

#### Overview

`SharedToolExecutor` is a singleton that provides core logic for:
- Terminal detection (cursor blink analysis)
- Command execution with intelligent waiting
- Screen analysis (OCR-based markdown conversion)
- Differential screen analysis (change detection)

Both the MCP tool handler and AI chat system delegate to this class.

#### Architecture

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

#### Key Features

**1. Terminal Idle Detection**

Detects when a terminal is waiting for input by analyzing:
- Cursor blink patterns
- Screen stability over multiple frames
- Shell prompt presence

```cpp
QJsonObject detectCursor(const QJsonObject &args);
// Returns: {detected: true, confidence: 0.95, status: "idle", ...}
```

**2. Smart Command Execution**

Executes commands and waits for completion using two-phase polling:
- Phase 1: Wait for "outputting" (command running)
- Phase 2: Wait for "idle" (command finished)

```cpp
QJsonObject runCommandAndWait(const QJsonObject &args);
// Returns: {success: true, wait_time_ms: 1234, status: "idle", ...}
```

**3. Differential Screen Analysis**

Compares current frame against previous frame to detect changes:
- Returns text diff of what changed
- Highlights BIOS-relevant changes
- More efficient than full OCR for iterative tasks

```cpp
QJsonObject differentialAnalysis(const QJsonObject &args);
// Returns: {changes: "...", bios_highlights: [...], ...}
```

#### Usage

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

#### Benefits

- **Code Reuse**: Single implementation for both systems
- **Consistency**: Same behavior across MCP and AI Chat
- **Maintainability**: Bug fixes apply to both systems
- **Performance**: Shared resources (camera, screen analyzer)

### Web Search Integration

The AI Chat system includes multi-provider web search capabilities, allowing the AI to search the internet for information when needed.

#### Provider Architecture

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

#### Configuration

Configure providers in **Settings → AI Chat → Web Search**:

- Enable/disable web search
- Set provider priority order
- Configure API keys (optional, for enhanced access)

#### Usage

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

#### Implementation

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

### Tools Configuration Tree

The AI Chat tools are now organized in a hierarchical tree structure in the settings UI, providing better organization and easier management.

#### Tree Structure

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

#### Features

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

#### Implementation

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

#### Benefits

- **Better Organization**: Hierarchical structure reduces visual clutter
- **Consistent Design**: Matches logging page UI pattern
- **Improved UX**: Expandable groups make navigation easier
- **Scalable**: Easy to add new tool groups
- **Professional Appearance**: Two-column layout provides better documentation

### Cursor Blink Detection

Advanced terminal state detection using cursor blink analysis, enabling the AI to determine when a terminal is idle and ready for input.

#### How It Works

The system captures multiple frames and analyzes:

1. **Cursor Blink Pattern**: Terminals typically blink cursors at 1-2 Hz
2. **Screen Stability**: Idle terminals have minimal pixel changes
3. **Shell Prompt Presence**: OCR detects common prompt patterns ($, #, >)

#### Detection Algorithm

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

#### Use Cases

**1. Command Completion Detection**

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

**2. BIOS Navigation**

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

**3. Automated Testing**

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

#### Configuration

Adjust detection parameters in **Settings → AI Chat → Advanced**:

- **Detection Samples**: Number of frames to capture (3-8, default 5)
- **Detection Interval**: Time between frames in ms (200-1000, default 350)
- **Confidence Threshold**: Minimum confidence for "idle" detection (0.0-1.0, default 0.8)

#### Performance

- **Detection Time**: ~1.5-3 seconds (5 samples × 350ms)
- **CPU Usage**: Low (image comparison is optimized)
- **Memory**: Minimal (frames are discarded after analysis)

### Advanced MCP Integration

The AI Chat system integrates deeply with the MCP (Model Context Protocol) server, enabling advanced automation scenarios.

#### MCP Tool Exposure

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

#### Shared Execution

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

#### Advanced Scenarios

**1. Remote AI Control**

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

**2. Multi-Client Coordination**

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

**3. Automated Workflows**

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

#### Performance Optimization

**1. Screen Capture Caching**

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

**2. Differential Analysis**

For iterative tasks, use differential analysis instead of full OCR:

```cpp
// Instead of full screen_to_markdown every iteration
QJsonObject diff = SharedToolExecutor::instance().differentialAnalysis({});
// Returns only what changed, much faster
```

**3. Batch Operations**

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

### Troubleshooting Advanced Features

#### OpenAI Function Calling Issues

**Problem**: "Empty tool_calls" error

**Solution**: Ensure your model supports native function calling. The system now automatically detects and handles both formats:
- Native format: `message.tool_calls` array
- Legacy format: JSON embedded in `message.content`

#### Shared Tool Executor Issues

**Problem**: Tools behave differently in MCP vs AI Chat

**Solution**: Both systems now use `SharedToolExecutor`. If you see inconsistencies:
1. Check that `SharedToolExecutor::instance()` is initialized
2. Verify `CameraManager` is injected: `setCameraManager(cam)`
3. Check logs for `SharedToolExecutor` messages

#### Web Search Issues

**Problem**: Web search returns no results

**Solution**:
1. Check provider configuration in Settings
2. Verify network connectivity
3. Try fallback providers (DuckDuckGo, Wikipedia)
4. Check SSL/TLS configuration if using Exa/Parallel

#### Cursor Detection Issues

**Problem**: Terminal detection fails

**Solution**:
1. Increase detection samples (5 → 8)
2. Increase detection interval (350ms → 500ms)
3. Lower confidence threshold (0.8 → 0.6)
4. Ensure terminal cursor is visible and blinking

---

## File Reference

### Backend (`ai/`)

| File | Description |
|------|-------------|
| `ChatTypes.h` | Core data types: `ChatMessage`, `ChatRole`, `ChatExecutionPlan`, `ChatTask`, `ChatSkill`, `ChatApiMessage`, `ChatCompletionResult`, `AgentToolCall`, etc. |
| `ChatAgentTypes.h/cpp` | Agent abstractions: `TaskAgentExecutor`, `MainPlannerAgent`, `ScreenTaskAgent`, `TypeTextTaskAgent`, `MouseTaskAgent`, `TaskAgentRegistry` |
| `ChatManager.h/cpp` | Main orchestrator singleton |
| `ChatApiClient.h/cpp` | OpenAI-compatible HTTP client with native function calling support |
| `ChatConversationBuilder.h/cpp` | Builds API message arrays, strips tool JSON, injects agent instructions |
| `ChatToolExecution.h/cpp` | Parses and executes tool calls (both native and legacy formats) |
| `ChatScreenCapture.h/cpp` | Captures target screen frames |
| `ChatInputRouter.h/cpp` | Routes mouse/keyboard to target via HID |
| `ChatGuideMode.h/cpp` | Guide mode logic |
| `ChatSkillManager.h/cpp` | Skill loading and management |
| `ChatPersistence.h/cpp` | JSON history save/load |
| `ChatTracing.h/cpp` | Debug trace logging |
| `SharedToolExecutor.h/cpp` | Shared tool execution layer for MCP and AI Chat |
| `WebSearchManager.h/cpp` | Multi-provider web search orchestration |
| `WebSearchProviders.h/cpp` | Concrete search provider implementations (Exa, Parallel, DuckDuckGo, Wikipedia) |
| `WebSearchProvider.h` | Base search provider interface |

### Frontend (`ui/chat/`)

| File | Description |
|------|-------------|
| `ChatWindow.h/cpp` | Main chat window widget |
| `ChatBubbleWidget.h/cpp` | Individual message bubble |
| `ChatInputWidget.h/cpp` | Text input with send button |
| `ChatPlanCardWidget.h/cpp` | Plan approval card |
| `ChatSkillBar.h/cpp` | Horizontal skill button bar |
| `ChatSettingsPage.h/cpp` | Settings → AI Chat preferences page |
| `ChatTraceDialog.h/cpp` | Trace log viewer dialog |
| `QuickReplyWidget.h` | Clickable quick-reply chip |
