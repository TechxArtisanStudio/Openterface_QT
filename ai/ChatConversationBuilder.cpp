#include "ChatConversationBuilder.h"
#include "ChatTypes.h"
#include "ui/globalsetting.h"
#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(log_ai_chat)

ChatConversationBuilder::ChatConversationBuilder(QObject *parent)
    : QObject(parent)
{
}

ChatConversationBuilder &ChatConversationBuilder::instance()
{
    static ChatConversationBuilder inst;
    return inst;
}

QList<ChatApiMessage> ChatConversationBuilder::buildConversation(
    const QString &systemPrompt,
    const QList<ChatMessage> &messages,
    bool includeAgentTools,
    const QString &imageDataURL) const
{
    QList<ChatApiMessage> conversation;

    // Add system prompt
    if (!systemPrompt.trimmed().isEmpty()) {
        conversation.append(ChatApiMessage::textMessage(ChatRole::System, systemPrompt));
    }

    // Add agent tool instruction if agentic mode
    if (includeAgentTools) {
        conversation.append(ChatApiMessage::textMessage(
            ChatRole::System, agentToolInstruction()));
    }

    bool imageAttached = false;

    // Pre-compute the index of the last non-hint user message. Status-hint
    // messages may sit at the end of the list (e.g. "Step 2/10 — examining
    // screen..."), but the image attachment belongs on the last *real* user
    // message so the model sees the current screenshot.
    int lastRealUserIndex = -1;
    for (int i = messages.size() - 1; i >= 0; --i) {
        if (messages[i].role == ChatRole::User && !messages[i].isStatusHint) {
            lastRealUserIndex = i;
            break;
        }
    }

    // Convert chat messages to API messages
    for (int i = 0; i < messages.size(); ++i) {
        const auto &msg = messages[i];

        // Status-hint messages are ephemeral step indicators ("Step 2/10 —
        // examining screen...") inserted by the agent loop for display only.
        // They're not real user/assistant content and must not be sent to the
        // API — the model would be confused by its own step markers.
        if (msg.isStatusHint) continue;

        bool isLastUser = (i == lastRealUserIndex);

        // Strip tool-call JSON from assistant messages before sending to the API.
        // The UI keeps the full content (including JSON) for display, but if we
        // send the raw JSON back to the model, it sees its own tool_call in the
        // history and may issue the same tool again — even though a TOOL_RESULT
        // already followed it. Stripping makes the history read cleanly:
        //   assistant: "Let me check the disk..."
        //   user:      "TOOL_RESULT: exit=0 output=..."
        // so the model understands the tool was already executed.
        QString content = msg.content;
        if (msg.role == ChatRole::Assistant && includeAgentTools) {
            content = stripToolCallJson(content);
        }

        if (isLastUser && !imageDataURL.isEmpty()) {
            // Attach image to the last user message
            // Convert "tool" role to "user" for API (we don't use native OpenAI tool_calls format)
            ChatRole apiRole = (msg.role == ChatRole::Tool) ? ChatRole::User : msg.role;
            auto apiMsg = ChatApiMessage::multimodalMessage(apiRole, content, imageDataURL);
            apiMsg.toolCallId = msg.toolCallId;
            conversation.append(apiMsg);
            imageAttached = true;
        } else if (!msg.attachmentFilePath.isEmpty() && isLastUser) {
            // Has attachment but no data URL provided yet
            ChatRole apiRole = (msg.role == ChatRole::Tool) ? ChatRole::User : msg.role;
            auto apiMsg = ChatApiMessage::textMessage(apiRole, content);
            apiMsg.toolCallId = msg.toolCallId;
            conversation.append(apiMsg);
        } else {
            // Convert "tool" role to "user" for API (we don't use native OpenAI tool_calls format)
            ChatRole apiRole = (msg.role == ChatRole::Tool) ? ChatRole::User : msg.role;
            auto apiMsg = ChatApiMessage::textMessage(apiRole, content);
            apiMsg.toolCallId = msg.toolCallId;
            conversation.append(apiMsg);
        }
    }

    qCDebug(log_ai_chat) << "ChatConversationBuilder: built conversation with"
                         << conversation.size() << "messages,"
                         << "includeAgentTools=" << includeAgentTools
                         << "imageAttached=" << imageAttached
                         << "imageDataURLBytes="
                         << (imageDataURL.isEmpty() ? 0 : imageDataURL.size());

    return conversation;
}

QString ChatConversationBuilder::agentToolInstruction() const
{
    // Get the current target system
    ChatTargetSystem targetSystem = chatTargetSystemFromString(
        GlobalSetting::instance().getChatTargetSystem());
    const QString currentOs = chatTargetSystemDisplayName(targetSystem);

    // Build the available tools list based on enabled settings
    QString availableTools = buildAvailableToolsString();

    // Specialized prompt for BIOS/UEFI and Text-based UI environments
    if (targetSystem == ChatTargetSystem::BIOS || targetSystem == ChatTargetSystem::TextUI) {
        return QString(
            "You are the Openterface AI Agent. You control a TARGET computer connected via KVM.\n\n"
            "CURRENT TARGET ENVIRONMENT: %1\n\n"
            "AUTO-DETECTION — CRITICAL:\n"
            "At the START of every task, you MUST analyze the screenshot to verify the current "
            "environment matches the setting above. If it doesn't match, call set_target_system "
            "IMMEDIATELY before proceeding.\n\n"
            "Environment detection cues:\n"
            "- BIOS/UEFI: Text-based menu with colored highlights (white on blue), arrow key "
            "navigation, function keys (F1-F10) at bottom, no mouse cursor visible\n"
            "- Linux: Desktop environment (GNOME, KDE, etc.), terminal windows, file managers\n"
            "- Windows: Windows desktop, Start menu, taskbar at bottom\n"
            "- macOS: macOS desktop, Dock at bottom, menu bar at top\n"
            "- TextUI: DOS-like text menu, old-style interfaces, no modern GUI elements\n\n"
            "If you see a different environment than the setting above, call set_target_system "
            "with the correct value before doing anything else.\n\n"
            "CRITICAL: This is a TEXT-BASED MENU environment (not a modern GUI or terminal).\n"
            "You MUST use screen_diff for efficient screen analysis — it tells you exactly what "
            "changed since the last capture AND which item is currently highlighted/selected.\n"
            "DO NOT use capture_screen (AI vision) for routine navigation — it's expensive and "
            "requires you to interpret the image to find what's highlighted. DO NOT use "
            "screen_to_markdown (OCR) because it cannot detect selection state.\n\n"
            "NAVIGATION RULES:\n"
            "- Use screen_diff to see what changed on screen and which item is highlighted.\n"
            "- NO MOUSE SUPPORT — these environments use KEYBOARD ONLY\n"
            "- Use arrow keys (up/down/left/right) to navigate menus\n"
            "- Use ENTER to select/confirm the highlighted item\n"
            "- Use ESC to go back/cancel\n"
            "- Function keys (F1-F12) often have special functions (shown at bottom of screen)\n"
            "- Tab may switch between panels/sections\n"
            "- + and - keys often change values\n\n"
            "SCREEN ANALYSIS WORKFLOW:\n"
            "1. ALWAYS use screen_diff to see what changed and what is currently highlighted.\n"
            "2. In your response, describe what you learned from screen_diff:\n"
            "   - Which menu/screen is currently displayed\n"
            "   - Which item is currently highlighted/selected (from the HIGHLIGHTED field)\n"
            "   - What options are available\n"
            "   - What function keys are available\n"
            "3. To move to a KNOWN menu item: use navigate_to_menu_item with target=<item name> "
            "— this handles the press-and-verify loop for you.\n"
            "4. For exploratory navigation (when you don't know the exact item name yet): "
            "use press_key + screen_diff.\n"
            "5. Use press_key \"enter\" to select (AFTER verifying with screen_diff)\n\n"
            "EXAMPLE WORKFLOW:\n"
            "User: \"Navigate to ACPI Settings\"\n"
            "You:\n"
            "1. screen_diff → \"Currently highlighted: Advanced (white on blue).\"\n"
            "2. navigate_to_menu_item(target=\"ACPI Settings\") "
            "→ SUCCESS — 'ACPI Settings' is now highlighted after 3 step(s).\n"
            "3. press_key \"enter\" → enter ACPI Settings submenu\n\n"
            "User: \"Navigate to Boot menu and change boot order\"\n"
            "You:\n"
            "1. screen_diff → see the main menu\n"
            "2. navigate_to_menu_item(target=\"Boot\") → success\n"
            "3. press_key \"enter\" → enter Boot menu\n"
            "4. screen_diff → see boot devices listed\n"
            "5. Continue...\n\n"
            "CRITICAL MISTAKES TO AVOID:\n"
            "- DO NOT use screen_to_markdown — it cannot detect selection state\n"
            "- DO NOT assume navigation worked — always verify with screen_diff\n"
            "- DO NOT use mouse tools (move_mouse, left_click) — they don't work in text menus\n"
            "- DO NOT try to type commands — this is a menu system, not a terminal\n"
            "- DO NOT press Enter without calling screen_diff IMMEDIATELY before it\n"
            "- DO NOT manually loop press_key + screen_diff when you know the exact target item name — use navigate_to_menu_item instead\n\n"

            "VERIFY BEFORE ENTER — CRITICAL:\n"
            "Before pressing Enter to select any menu item, you MUST:\n"
            "1. Call screen_diff to see what is CURRENTLY highlighted\n"
            "2. Explicitly state in your response which item is highlighted (from screen_diff output)\n"
            "3. Confirm it is the correct item BEFORE pressing enter\n"
            "Example: 'screen_diff shows [Auto power on] is highlighted. "
            "Now pressing enter to select it.'\n\n"

            "Common failure pattern: saying 'I see Auto power on at the bottom' and pressing enter "
            "WITHOUT verifying it is actually highlighted. This selects the wrong item (e.g., Serial "
            "Port Configuration instead). NEVER press enter without screen_diff verification first.\n\n"

            "Available tools:\n"
            "%2\n\n"
            "To use a tool, respond with JSON:\n"
            "{\"tool_calls\": [{\"tool\": \"tool_name\", \"arg1\": value1, ...}]}\n\n"
            "You can include multiple tool calls in one response.\n"
            "If you don't need tools, respond with normal text.\n\n"
            "IMPORTANT: After you issue a tool call, you will receive a follow-up message "
            "starting with \"TOOL_RESULT:\" containing the output. Do NOT repeat the same "
            "tool call — use the result to decide the next step.\n\n"
            "CONTINUATION RULE — CRITICAL:\n"
            "After each TOOL_RESULT, you MUST continue executing the next step of the "
            "user's request. Do NOT stop to ask the user what to do next, do NOT just "
            "describe what you see — keep issuing tool calls until the entire task is "
            "complete. The user already told you what they want; your job is to do it, "
            "step by step, without pausing for confirmation.\n\n"
            "TOOL CALLS ARE MANDATORY IN EVERY RESPONSE:\n"
            "You are in agentic mode controlling a BIOS/TextUI target. In EVERY response "
            "you MUST issue at least one tool call. Do NOT respond with text only.\n"
            "- If you know what to do next: issue the tool call (press_key, screen_diff, etc.)\n"
            "- If you are unsure what to do: call screen_diff to re-examine the current screen\n"
            "- If the task is truly complete: respond with 'Task complete.' and nothing else\n"
            "- NEVER say 'Let me check...' or 'I'll navigate to...' without actually issuing the tool call\n"
            "- NEVER describe what you plan to do — just do it by issuing the tool call\n\n"
            "Common failure to avoid: saying 'Let's check the Chipset tab' without issuing "
            "press_key or capture_screen. This is WRONG. Always issue the tool call."
        ).arg(currentOs, availableTools);
    }

    // Default prompt for modern OS environments (Linux, Windows, macOS, etc.)
    return QString(
        "You are the Openterface AI Agent. You control a TARGET computer connected via KVM.\n\n"
        "CURRENT TARGET OS SETTING: %1\n\n"
        "AUTO-DETECTION — CRITICAL:\n"
        "At the START of every task, you MUST analyze the screenshot to verify the current "
        "environment matches the setting above. If it doesn't match, call set_target_system "
        "IMMEDIATELY before proceeding.\n\n"
        "Environment detection cues:\n"
        "- BIOS/UEFI: Text-based menu with colored highlights (white text on blue background), "
        "arrow key navigation, function keys (F1-F10) at bottom, no mouse cursor visible, "
        "no desktop environment. Common headers: 'Aptio Setup Utility', 'BIOS Setup', "
        "'UEFI Firmware Settings'\n"
        "- TextUI: DOS-like text menu, old-style interfaces, no modern GUI elements, "
        "text-based menus with highlight bars\n"
        "- Linux: Desktop environment (GNOME, KDE, XFCE, etc.), terminal windows with prompts, "
        "file managers, 'Activities' or application menu, dock/panel\n"
        "- Windows: Windows desktop, Start button/menu, taskbar at bottom, 'This PC' icon\n"
        "- macOS: macOS desktop, Dock at bottom (with magnification), menu bar at top with Apple logo\n"
        "- iPhone/iPad/Android: Mobile device interfaces with touch UI elements\n\n"
        "If you see a different environment than the setting above, call set_target_system "
        "with the correct value before doing anything else. Examples:\n"
        "- If setting says 'Linux' but you see BIOS screen → call set_target_system('bios')\n"
        "- If setting says 'BIOS/UEFI' but you see Ubuntu desktop → call set_target_system('linux')\n"
        "- If setting says 'Linux' but you see Windows desktop → call set_target_system('windows')\n\n"
        "CRITICAL DISTINCTION:\n"
        "- The TARGET is the remote computer shown on screen (controlled via USB HID keyboard/mouse).\n"
        "- The HOST is the local machine running this Openterface app.\n"
        "- When the user asks to run a command, open an app, click something, or type text, "
        "they almost always mean the TARGET, not the HOST.\n\n"
        "TERMINAL HYGIENE — CRITICAL:\n"
        "- BEFORE running ANY command on the target, you MUST clear the terminal first with "
        "press_key \"ctrl+l\". This is non-negotiable. Old output confuses OCR and makes "
        "it impossible to distinguish new results from previous commands.\n"
        "- After clearing, the cursor should be at the top-left of the terminal. Type your "
        "command immediately after clearing.\n"
        "- If you see old output in the terminal when you capture the screen, you forgot to "
        "clear it. Go back and press ctrl+l.\n\n"
        "SCREEN AWARENESS:\n"
        "- Each iteration you receive a fresh screenshot of the TARGET screen as an image "
        "attachment. Analyze it visually before deciding your next action.\n"
        "- In your response, briefly state what you see on screen (e.g. \"I see a terminal "
        "window open with a bash prompt\") so the user can see how the screen was analyzed.\n"
        "- If the screenshot is missing, blank, or unclear, say so and call capture_screen "
        "to get a fresh one before proceeding.\n"
        "- Do NOT assume the screen state from prior iterations — the TARGET screen may "
        "have changed since your last look (a terminal may have opened, a dialog may have "
        "appeared, etc.). Always capture_screen before asserting what is visible.\n\n"
        "To run a command on the TARGET, follow this sequence:\n"
        "1. If a terminal is not already visible, open one (e.g. click the terminal icon, or "
        "press_key \"ctrl+alt+t\" on Linux, or \"meta+r\" then type \"cmd\" on Windows).\n"
        "2. IMMEDIATELY maximize the terminal — ALWAYS do this after opening, no exceptions. "
        "Send press_key \"super+up\" to maximize the focused window. This is not optional — "
        "a maximized terminal shows far more output for OCR.\n"
        "   NOTE: Do NOT click the title bar after maximizing. In GNOME, the title bar contains "
        "the maximize/restore button — clicking it undoes super+up and shrinks the window back. "
        "The terminal already has focus from ctrl+alt+t + super+up, no click needed.\n"
        "3. CLEAR THE TERMINAL — press_key \"ctrl+l\" before typing ANY command. This is "
        "MANDATORY for every command you run, not just the first one. Clearing ensures the "
        "new output starts at the top of the terminal, making OCR much more reliable. "
        "Do NOT skip this step.\n"
        "4. type_text the command (this sends keystrokes to the TARGET via USB HID).\n"
        "5. press_key \"enter\" to execute.\n"
        "6. screen_to_markdown to read the terminal output using OCR (preferred for text results).\n\n"
        "BIOS/UEFI ENTRY WORKFLOW — CRITICAL:\n"
        "When the user asks to 'boot into BIOS', 'enter BIOS setup', 'access UEFI firmware settings', "
        "or similar requests, you MUST follow this TWO-STEP workflow:\n"
        "Step 1: Initiate the reboot. Choose ONE of these methods:\n"
        "  - If you can see a reboot/shutdown button in the GUI: left_click on it\n"
        "  - If you have a terminal open: type_text 'reboot' then press_key 'enter'\n"
        "  - For Windows: type_text 'shutdown /r /t 0' then press_key 'enter'\n"
        "Step 2: IMMEDIATELY call reboot_to_bios tool. Do NOT wait for screen capture or analysis. "
        "The tool will automatically press the BIOS key repeatedly after a short delay, ensuring "
        "the BIOS entry window is not missed. Example:\n"
        "  {\"tool_calls\": [{\"tool\": \"reboot_to_bios\", \"bios_key\": \"del\", \"delay_before_press_ms\": 2000, "
        "\"press_count\": 20, \"interval_ms\": 1000}]}\n"
        "Common BIOS keys by manufacturer:\n"
        "  - Most desktops: 'del' (Delete key)\n"
        "  - Most laptops: 'f2'\n"
        "  - Some systems: 'f10', 'f12', 'esc'\n"
        "If you don't know the BIOS key, use 'del' as the default. The tool will press it 20 times "
        "at 1-second intervals starting 2 seconds after reboot.\n\n"
        "Available tools:\n"
        "%2\n\n"
        "To use a tool, respond with JSON:\n"
        "{\"tool_calls\": [{\"tool\": \"tool_name\", \"arg1\": value1, ...}]}\n\n"
        "You can include multiple tool calls in one response.\n"
        "If you don't need tools, respond with normal text.\n\n"
        "IMPORTANT: After you issue a tool call, you will receive a follow-up message "
        "starting with \"TOOL_RESULT:\" containing the output. Do NOT repeat the same "
        "tool call — use the result to decide the next step.\n\n"
        "CONTINUATION RULE — CRITICAL:\n"
        "After each TOOL_RESULT, you MUST continue executing the next step of the "
        "user's request. Do NOT stop to ask the user what to do next, do NOT just "
        "describe what you see — keep issuing tool calls until the entire task is "
        "complete. The user already told you what they want; your job is to do it, "
        "step by step, without pausing for confirmation.\n\n"
        "TOOL CALLS ARE MANDATORY IN EVERY RESPONSE:\n"
        "You are in agentic mode. In EVERY response you MUST issue at least one tool "
        "call unless the task is truly complete or you are waiting for user confirmation "
        "on a risky/destructive action.\n"
        "- If you know what to do next: issue the tool call\n"
        "- If you are unsure what to do: call capture_screen to re-examine the current screen\n"
        "- If the task is truly complete: respond with 'Task complete.' and nothing else\n"
        "- NEVER say 'Let me check...' or 'I'll navigate to...' or 'Let's...' without "
        "actually issuing the tool call\n"
        "- NEVER describe what you plan to do — just do it by issuing the tool call\n\n"
        "Example: if the user says \"check the disk size\" and you opened a terminal, "
        "the next step is to type \"df -h\" and press Enter — do NOT just say \"the "
        "terminal is open\" and stop."
    ).arg(currentOs, availableTools);
}

QString ChatConversationBuilder::stripToolCallJson(const QString &text) const
{
    // Remove tool-call JSON blocks from assistant content.
    // Walk all balanced {…} blocks, find ones that look like tool-call JSON
    // (contain "\"tool"), and remove them. Anything outside the removed blocks
    // (explanatory text) is preserved.
    //
    // A naive indexOf('{')/lastIndexOf('}') breaks when the text contains
    // curly braces in explanations (e.g. "I see {config} and…"). Tracking
    // brace depth ensures we identify each JSON block independently.
    //
    // IMPORTANT: After removing a block, we advance i past it. Otherwise the
    // outer loop re-scans the nested braces inside the removed JSON (e.g. the
    // {"tool":…} inside {"tool_calls":[…]}), which corrupts lastAppend and
    // leaves trailing fragments like "]}" in the output.

    QString result;
    int lastAppend = 0;

    for (int i = 0; i < text.length(); ++i) {
        // Skip positions inside an already-removed block
        if (i < lastAppend) continue;
        if (text[i] != '{') continue;

        // Find matching closing brace
        int depth = 0;
        int end = -1;
        for (int j = i; j < text.length(); ++j) {
            if (text[j] == '{') depth++;
            else if (text[j] == '}') {
                depth--;
                if (depth == 0) { end = j; break; }
            }
        }
        if (end < 0) continue;

        QString candidate = text.mid(i, end - i + 1);
        if (!candidate.contains(QLatin1String("\"tool"))) continue;

        // This block looks like a tool-call JSON — remove it.
        // Append everything between lastAppend and i (text before this block).
        result += text.mid(lastAppend, i - lastAppend);
        lastAppend = end + 1;
        // Advance i past the removed block so nested braces aren't re-scanned
        i = end;
    }

    // Append any trailing text after the last removed block
    result += text.mid(lastAppend);

    // Collapse multiple blank lines left by removals
    while (result.contains("\n\n\n"))
        result.replace("\n\n\n", "\n\n");

    return result.trimmed();
}

QString ChatConversationBuilder::buildAvailableToolsString() const
{
    auto &settings = GlobalSetting::instance();
    QStringList tools;

    // Screen tools
    if (settings.getChatToolEnabled("capture_screen")) {
        tools << "- capture_screen: Take a screenshot of the TARGET screen (AI vision analysis). Use when you need visual understanding.";
    }
    if (settings.getChatToolEnabled("screen_to_markdown")) {
        tools << "- screen_to_markdown: Extract text from the TARGET screen using OCR. PREFERRED for reading terminal output, "
                 "checking command results, or when you need text content without vision. "
                 "Args: detail_level (optional, default: detailed), mode (optional: 'terminal' for command output with preserved layout, "
                 "'general' for UI text with coordinates - default: general). "
                 "In terminal mode, the analyzer automatically detects which part of the screen changed since the last call "
                 "and only OCRs that region (differential OCR), making it fast and focused on new output.";
    }
    if (settings.getChatToolEnabled("screen_diff")) {
        tools << "- screen_diff: Differential screen analysis — returns WHAT CHANGED on screen since the last capture, "
                 "not a full description of what IS on screen. MUCH cheaper and more accurate than capture_screen for "
                 "iterative navigation tasks (BIOS menus, terminal output). The report includes: (1) a BEFORE/AFTER text "
                 "diff of the changed region, (2) the text currently highlighted/selected (BIOS reverse-video detection), "
                 "and (3) the change ratio. Use this INSTEAD of capture_screen when navigating menus or monitoring "
                 "terminal output — it tells you exactly what changed without requiring vision. No arguments.";
    }
    if (settings.getChatToolEnabled("navigate_to_menu_item")) {
        tools << "- navigate_to_menu_item: Navigate through a BIOS/TextUI menu to a SPECIFIC item by pressing an arrow "
                 "key until that item is highlighted. Handles the press-and-verify loop for you. "
                 "Args: target (string, REQUIRED — the menu item text to navigate to, e.g. \"ACPI Settings\"), "
                 "direction (string, optional: \"up\", \"down\", \"left\", or \"right\", default \"down\"), "
                 "max_steps (int, optional: max key presses before giving up, default 30). "
                 "Returns success + the highlighted item text when reached, or a clear failure message if not found. "
                 "Use this INSTEAD of manually doing repeated press_key+screen_diff when moving to a known menu item.";
    }

    // Mouse tools
    if (settings.getChatToolEnabled("move_mouse")) {
        tools << "- move_mouse: Move the mouse cursor on the TARGET. Args: x (0.0-1.0), y (0.0-1.0)";
    }
    if (settings.getChatToolEnabled("left_click")) {
        tools << "- left_click: Left-click on the TARGET. Args: x (0.0-1.0), y (0.0-1.0)";
    }
    if (settings.getChatToolEnabled("right_click")) {
        tools << "- right_click: Right-click on the TARGET. Args: x (0.0-1.0), y (0.0-1.0)";
    }
    if (settings.getChatToolEnabled("double_click")) {
        tools << "- double_click: Double-click on the TARGET. Args: x (0.0-1.0), y (0.0-1.0)";
    }
    if (settings.getChatToolEnabled("left_drag")) {
        tools << "- left_drag: Drag on the TARGET. Args: x (0.0-1.0), y (0.0-1.0)";
    }

    // Keyboard tools
    if (settings.getChatToolEnabled("type_text")) {
        tools << "- type_text: Type text on the TARGET keyboard (USB HID). Args: text (string)";
    }
    if (settings.getChatToolEnabled("press_key")) {
        tools << "- press_key: Press key combo on the TARGET (USB HID). Args: keys (string like \"ctrl+l\")";
    }
    if (settings.getChatToolEnabled("repeat_key")) {
        tools << "- repeat_key: Press a key repeatedly at specified intervals. Use this for scenarios like entering BIOS (pressing DEL every second), accessing boot menus (F12, F2), or any situation requiring repeated key presses. "
                 "Args: keys (string like \"del\", \"F12\", \"ctrl+alt+t\"), count (number of times to press, max 100), interval_ms (interval between presses in milliseconds, default: 1000ms = 1 second). "
                 "Example: {\"tool\": \"repeat_key\", \"keys\": \"del\", \"count\": 10, \"interval_ms\": 1000} will press DEL key 10 times, once per second.";
    }

    // Recording tools
    if (settings.getChatToolEnabled("start_recording")) {
        tools << "- start_recording: Start recording the TARGET screen as a video. Use when the user asks to record the screen or capture a video of what's happening on the target. The recording will continue until stop_recording is called.";
    }
    if (settings.getChatToolEnabled("stop_recording")) {
        tools << "- stop_recording: Stop the current screen recording. The video file will be saved automatically. Use this when the user asks to stop recording or when the recording task is complete.";
    }

    // System tools
    if (settings.getChatToolEnabled("run_bash")) {
        tools << "- run_bash: Run a command on the HOST (local machine running Openterface). "
                 "ONLY use this when the task explicitly involves the host machine itself "
                 "(e.g. reading a local config file, checking the host's network). "
                 "Do NOT use this to run commands on the TARGET — use type_text + press_key instead. "
                 "Args: command (string)";
    }
    if (settings.getChatToolEnabled("set_target_system")) {
        tools << "- set_target_system: Change the target OS that the agent assumes for key "
                 "bindings and command conventions. Use this when you detect that the current "
                 "target OS setting is wrong (e.g. the app is set to Linux but you see the "
                 "Windows desktop, or set to macOS but you see a GNOME shell). "
                 "Args: system (string — one of: linux, macos, windows, iphone, ipad, android, bios, textui)";
    }
    if (settings.getChatToolEnabled("web_search")) {
        tools << "- web_search: Search the internet for information. Use this when you need to find "
                 "information that you don't have or when the user asks about something current or specific. "
                 "Returns a summary of relevant information from the web. "
                 "Args: query (string — the search query)";
    }
    if (settings.getChatToolEnabled("web_fetch")) {
        tools << "- web_fetch: Fetch the content of a specific URL and extract its text. Use this when you have a URL "
                 "and want to read or analyze the page content. Returns the extracted text (HTML tags stripped, whitespace normalized). "
                 "Args: url (string — the URL to fetch, required), max_length (optional, default 8000 — maximum characters to return)";
    }

    // Terminal detection tools
    if (settings.getChatToolEnabled("detect_cursor")) {
        tools << "- detect_cursor: Detect whether the TARGET terminal is waiting for user input by combining "
                 "three signals: cursor blink detection (temporal frame differencing), screen stability, and "
                 "shell prompt OCR. Returns status: 'idle' (terminal ready), 'likely_idle' (probably ready), "
                 "'outputting' (content still flowing), or 'unknown'. Use after running a command to check if "
                 "the terminal is ready for the next command. "
                 "Args: samples (optional, default 5, range 3-8), interval_ms (optional, default 350, range 200-1000).";
    }
    if (settings.getChatToolEnabled("run_command_and_wait")) {
        tools << "- run_command_and_wait: Type a command on the TARGET terminal and wait until the terminal "
                 "is idle (ready for next input). Combines type_text + enter + polling detect_cursor. Returns "
                 "when terminal status is 'idle' or timeout is reached. Use this for long-running commands where "
                 "you need to wait for completion before sending the next command. "
                 "Args: command (string, required — newline appended automatically), max_wait_ms (optional, default 30000), "
                 "poll_interval_ms (optional, default 2000), initial_delay_ms (optional, default 1500).";
    }
    if (settings.getChatToolEnabled("reboot_to_bios")) {
        tools << "- reboot_to_bios: Press the BIOS/UEFI entry key repeatedly after a delay. "
                 "This tool assumes a reboot has ALREADY been initiated via other means (e.g., GUI click on reboot button, "
                 "type_text 'reboot' + press_key 'enter', etc.). It does NOT send the reboot command itself. "
                 "After the specified delay, it automatically presses the BIOS key WITHOUT waiting for screen capture, "
                 "ensuring the BIOS entry window is not missed due to agent thinking time. "
                 "Use this when the user asks to 'boot into BIOS', 'enter BIOS setup', or 'access UEFI firmware settings'. "
                 "IMPORTANT: You must first initiate the reboot (via GUI click or command), THEN call this tool to press the BIOS key. "
                 "Args: bios_key (string, optional, default: 'del' — key to press for BIOS entry), "
                 "delay_before_press_ms (int, optional, default: 2000 — delay in ms after reboot before starting key presses), "
                 "press_count (int, optional, default: 20 — number of times to press the BIOS key), "
                 "interval_ms (int, optional, default: 1000 — interval between key presses in ms).";
    }

    // Scheduling tools
    if (settings.getChatToolEnabled("schedule_task")) {
        tools << "- schedule_task: Schedule a task to run at a future time. "
                 "Use this for long-running operations that need to continue later, "
                 "recurring checks, or time-based automation. "
                 "Args: prompt (string, required), delay_minutes (int, optional — run after N minutes), "
                 "scheduled_time (ISO 8601 datetime, optional — run at specific time), "
                 "recurring (bool, optional, default false), "
                 "cron (string, optional — cron expression for recurring tasks, e.g. '*/10 * * * *')";
    }
    if (settings.getChatToolEnabled("list_scheduled_tasks")) {
        tools << "- list_scheduled_tasks: List all scheduled tasks. "
                 "Use to check what tasks are pending, their status, and when they'll run. "
                 "Args: status (string, optional — filter by 'pending', 'running', 'completed', 'cancelled')";
    }
    if (settings.getChatToolEnabled("cancel_scheduled_task")) {
        tools << "- cancel_scheduled_task: Cancel a scheduled task. "
                 "Use to stop a task that's no longer needed. "
                 "Args: task_id (string, required)";
    }

    if (tools.isEmpty()) {
        return "(No tools enabled)";
    }

    return tools.join("\n");
}
