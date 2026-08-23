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

---

## File Reference

### Backend (`ai/`)

| File | Description |
|------|-------------|
| `ChatTypes.h` | Core data types: `ChatMessage`, `ChatRole`, `ChatExecutionPlan`, `ChatTask`, `ChatSkill`, `ChatApiMessage`, etc. |
| `ChatAgentTypes.h/cpp` | Agent abstractions: `TaskAgentExecutor`, `MainPlannerAgent`, `ScreenTaskAgent`, `TypeTextTaskAgent`, `MouseTaskAgent`, `TaskAgentRegistry` |
| `ChatManager.h/cpp` | Main orchestrator singleton |
| `ChatApiClient.h/cpp` | OpenAI-compatible HTTP client |
| `ChatConversationBuilder.h/cpp` | Builds API message arrays, strips tool JSON, injects agent instructions |
| `ChatToolExecution.h/cpp` | Parses and executes tool calls |
| `ChatScreenCapture.h/cpp` | Captures target screen frames |
| `ChatInputRouter.h/cpp` | Routes mouse/keyboard to target via HID |
| `ChatGuideMode.h/cpp` | Guide mode logic |
| `ChatSkillManager.h/cpp` | Skill loading and management |
| `ChatPersistence.h/cpp` | JSON history save/load |
| `ChatTracing.h/cpp` | Debug trace logging |
| `ChatInputRouter.h/cpp` | Mouse/keyboard routing to target |

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
