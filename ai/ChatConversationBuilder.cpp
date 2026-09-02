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
    auto &settings = GlobalSetting::instance();

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
            "You MUST use capture_screen (AI vision) for ALL screen analysis. DO NOT use "
            "screen_to_markdown (OCR) because:\n"
            "- OCR cannot detect which menu item is currently selected\n"
            "- Selection state is indicated by COLOR/HIGHLIGHTING (e.g., white text on blue background)\n"
            "- OCR treats all text equally and misses the visual distinction\n"
            "- AI vision can see the color differences and identify the selected item\n\n"
            "NAVIGATION RULES:\n"
            "- NO MOUSE SUPPORT — these environments use KEYBOARD ONLY\n"
            "- Use arrow keys (up/down/left/right) to navigate menus\n"
            "- Use ENTER to select/confirm the highlighted item\n"
            "- Use ESC to go back/cancel\n"
            "- Function keys (F1-F12) often have special functions (shown at bottom of screen)\n"
            "- Tab may switch between panels/sections\n"
            "- + and - keys often change values\n\n"
            "SCREEN ANALYSIS WORKFLOW:\n"
            "1. ALWAYS use capture_screen to see the current state\n"
            "2. In your response, describe what you see:\n"
            "   - Which menu/screen is currently displayed\n"
            "   - Which item is currently highlighted/selected (look for color difference)\n"
            "   - What options are available\n"
            "   - What function keys are available (usually shown at bottom)\n"
            "3. Use press_key with arrow keys to navigate to the desired option\n"
            "4. Use capture_screen AGAIN to verify you've navigated correctly\n"
            "5. Use press_key \"enter\" to select\n\n"
            "EXAMPLE WORKFLOW:\n"
            "User: \"Navigate to Boot menu and change boot order\"\n"
            "You:\n"
            "1. capture_screen → \"I see the BIOS main menu. Items: Main, Advanced, Boot, Security. "
            "The 'Main' item is currently highlighted (shown in white on blue).\"\n"
            "2. press_key \"down\" → move to 'Advanced'\n"
            "3. press_key \"down\" → move to 'Boot'\n"
            "4. capture_screen → \"I see 'Boot' is now highlighted. Pressing Enter to enter Boot menu.\"\n"
            "5. press_key \"enter\" → enter Boot menu\n"
            "6. capture_screen → \"I see the Boot menu with boot devices listed...\"\n"
            "7. Continue navigating...\n\n"
            "CRITICAL MISTAKES TO AVOID:\n"
            "- DO NOT use screen_to_markdown — it cannot detect selection state\n"
            "- DO NOT assume navigation worked — always verify with capture_screen\n"
            "- DO NOT use mouse tools (move_mouse, left_click) — they don't work in text menus\n"
            "- DO NOT try to type commands — this is a menu system, not a terminal\n\n"
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
            "step by step, without pausing for confirmation."
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

    if (tools.isEmpty()) {
        return "(No tools enabled)";
    }

    return tools.join("\n");
}
