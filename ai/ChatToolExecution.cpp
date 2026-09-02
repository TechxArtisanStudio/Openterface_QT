#include "ChatToolExecution.h"
#include "ChatInputRouter.h"
#include "ChatScreenCapture.h"
#include "ChatTypes.h"
#include "ui/globalsetting.h"
#include "ui/recording/recordingcontroller.h"
#include "server/mcp/screenAnalyzer.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QLoggingCategory>
#include <QProcess>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QThread>

Q_DECLARE_LOGGING_CATEGORY(log_ai_chat)

// Extract tool arguments from a JSON call object.
// The model sometimes puts args directly in the object:
//   {"tool": "type_text", "text": "hello"}
// And sometimes wraps them in an "arg"/"args"/"arguments" key:
//   {"tool": "type_text", "arg": {"text": "hello"}}
// This helper handles both formats, preferring the wrapped form when present.
static QVariantMap extractToolArgs(const QJsonObject &callObj)
{
    // Check for wrapped args first
    for (const QString &wrapperKey : {QStringLiteral("arg"), QStringLiteral("args"), QStringLiteral("arguments")}) {
        if (callObj.contains(wrapperKey) && callObj[wrapperKey].isObject()) {
            QJsonObject wrapped = callObj[wrapperKey].toObject();
            QVariantMap result;
            for (auto it = wrapped.begin(); it != wrapped.end(); ++it) {
                result[it.key()] = it.value().toVariant();
            }
            return result;
        }
    }

    // No wrapper — all keys except "tool" are args
    QVariantMap result;
    for (auto it = callObj.begin(); it != callObj.end(); ++it) {
        if (it.key() != "tool") {
            result[it.key()] = it.value().toVariant();
        }
    }
    return result;
}

ChatToolExecution::ChatToolExecution(QObject *parent)
    : QObject(parent)
{
}

ChatToolExecution &ChatToolExecution::instance()
{
    static ChatToolExecution inst;
    return inst;
}

// ============================================================================
// Tool-call parsing
// ============================================================================

QList<AgentToolCall> ChatToolExecution::parseToolCalls(const QString &text) const
{
    QString trimmed = text.trimmed();
    if (!trimmed.contains("tool")) return {};

    // Find all balanced {…} blocks in the response and try to parse each one.
    // A naive indexOf('{')/lastIndexOf('}') breaks when the model's explanation
    // contains curly braces (e.g. "I see {a terminal} and will type…"). Walking
    // the brace depth and trying each candidate ensures we find the real JSON
    // even when the text around it is messy.
    //
    // IMPORTANT: After processing each block (whether it succeeds or fails),
    // we advance i past it. Otherwise the outer loop re-scans nested braces
    // inside the block — e.g. the inner {"tool":…} inside {"tool_calls":[…]}
    // could be incorrectly parsed as a standalone single-tool call.
    QList<AgentToolCall> calls;

    for (int i = 0; i < trimmed.length(); ++i) {
        if (trimmed[i] != '{') continue;

        // Find the matching closing brace by tracking depth
        int depth = 0;
        int end = -1;
        for (int j = i; j < trimmed.length(); ++j) {
            if (trimmed[j] == '{') depth++;
            else if (trimmed[j] == '}') {
                depth--;
                if (depth == 0) { end = j; break; }
            }
        }
        if (end < 0) continue;

        // Always skip past this block after processing, even if it doesn't
        // contain tool calls. The nested braces inside are part of this block
        // and should not be scanned independently.
        struct ScopeGuard { int &ref; int val; ~ScopeGuard() { ref = val; } } guard{i, end};

        QString candidate = trimmed.mid(i, end - i + 1);

        // Quick filter: must mention "tool" to be a tool-call JSON
        if (!candidate.contains(QLatin1String("\"tool"))) continue;

        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(candidate.toUtf8(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            continue;
        }

        QJsonObject root = doc.object();

        // Try "tool_calls" array first
        if (root.contains("tool_calls") && root["tool_calls"].isArray()) {
            QJsonArray arr = root["tool_calls"].toArray();
            for (const auto &val : arr) {
                QJsonObject callObj = val.toObject();
                QString tool = callObj["tool"].toString();
                if (tool.isEmpty()) continue;

                AgentToolCall call;
                call.tool = tool;
                call.args = extractToolArgs(callObj);
                calls.append(call);
            }
            if (!calls.isEmpty()) return calls;
            continue;
        }

        // Try single "tool" object
        if (root.contains("tool")) {
            QString tool = root["tool"].toString();
            if (!tool.isEmpty()) {
                AgentToolCall call;
                call.tool = tool;
                call.args = extractToolArgs(root);
                calls.append(call);
                return calls;
            }
        }
    }

    // No JSON tool calls found - try XML format (Anthropic)
    return parseXmlToolCalls(trimmed);
}

// ============================================================================
// XML tool-call parser (fallback for Anthropic XML format)
// ============================================================================

QList<AgentToolCall> ChatToolExecution::parseXmlToolCalls(const QString &text) const
{
    QList<AgentToolCall> calls;

    // Match Anthropic XML tool call format using QRegularExpression
    // Format: <function name="tool_name"> ... </function>
    static const QRegularExpression funcRegex(
        QStringLiteral("<\\s*function\\s+name\\s*=\\s*\"([^\"]+)\"\\s*>(.*?)</function\\s*>"),
        QRegularExpression::DotMatchesEverythingOption);

    QRegularExpressionMatchIterator it = funcRegex.globalMatch(text);
    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        QString toolName = match.captured(1).trimmed();
        QString paramsXml = match.captured(2).trimmed();

        if (toolName.isEmpty()) continue;

        AgentToolCall call;
        call.tool = toolName;

        // Parse <parameter name="key">value</parameter>
        static const QRegularExpression paramRegex(
            QStringLiteral("<\\s*parameter\\s+name\\s*=\\s*\"([^\"]+)\"\\s*>(.*?)</parameter\\s*>"),
            QRegularExpression::DotMatchesEverythingOption);

        QRegularExpressionMatchIterator pit = paramRegex.globalMatch(paramsXml);
        while (pit.hasNext()) {
            QRegularExpressionMatch pm = pit.next();
            call.args[pm.captured(1).trimmed()] = pm.captured(2).trimmed();
        }

        calls.append(call);
    }

    if (!calls.isEmpty()) {
        qCDebug(log_ai_chat) << "XML tool-call parser found" << calls.size() << "calls";
    }
    return calls;
}


// ============================================================================
// Tool dispatch
// ============================================================================

AgentToolExecutionResult ChatToolExecution::executeToolCalls(const QList<AgentToolCall> &calls)
{
    QStringList summaries;
    QString attachmentPath;
    QString ocrResultText;  // For screen_to_markdown OCR results
    QStringList keyboardTokens;
    bool hasNonKeyboardTool = false;

    ChatInputRouter &router = ChatInputRouter::instance();
    ChatScreenCapture &screenCapture = ChatScreenCapture::instance();
    GlobalSetting &settings = GlobalSetting::instance();

    // Track whether the previous tool was a mouse action, so we can insert a
    // settle delay before the next keyboard action. The target machine needs
    // time to process the USB HID click and shift keyboard focus to the
    // clicked window before keystrokes arrive.
    bool prevWasMouseAction = false;

    // Configurable delays for USB HID synchronization
    const int MOUSE_TO_KEYBOARD_DELAY_MS = settings.getChatMouseToKeyboardDelayMs();
    const int POST_KEYBOARD_SETTLE_MS = settings.getChatPostKeyboardSettleMs();
    const int PRE_CAPTURE_DELAY_MS = settings.getChatPreCaptureDelayMs();

    // Track if previous action was keyboard (for auto-OCR after commands)
    bool prevWasKeyboardAction = false;

    // Get target system to determine if we should auto-convert to OCR
    // BIOS/TextUI modes should NOT auto-convert because OCR cannot detect selection state
    ChatTargetSystem targetSystem = chatTargetSystemFromString(settings.getChatTargetSystem());
    bool isTextBasedUI = (targetSystem == ChatTargetSystem::BIOS || targetSystem == ChatTargetSystem::TextUI);
    qCDebug(log_ai_chat) << "Tool execution: targetSystem=" << chatTargetSystemToString(targetSystem)
                         << "isTextBasedUI=" << isTextBasedUI;

    for (const auto &call : calls) {
        QString toolName = call.tool.toLower();

        // Check if tool is enabled in settings
        if (!settings.getChatToolEnabled(toolName)) {
            summaries.append(QString("%1: disabled in settings").arg(call.tool));
            qCWarning(log_ai_chat) << "AI Tool disabled in settings:" << call.tool;
            continue;
        }

        bool isMouseTool = (toolName == "move_mouse" || toolName == "left_click" ||
                            toolName == "right_click" || toolName == "double_click" ||
                            toolName == "left_drag" || toolName == "drag_mouse" ||
                            toolName == "mouse_drag" || toolName == "drag");
        bool isKeyboardTool = (toolName == "type_text" || toolName == "press_key" ||
                               toolName == "key_press" || toolName == "send_key" ||
                               toolName == "hotkey");
        bool isCaptureTool = (toolName == "capture_screen" || toolName == "take_screenshot" ||
                              toolName == "screenshot" || toolName == "screen_to_markdown");

        // Auto-convert capture_screen to screen_to_markdown after keyboard actions
        // This ensures terminal output is read using OCR instead of vision
        // BUT: Disable this for BIOS/TextUI modes because OCR cannot detect selection state
        if ((toolName == "capture_screen" || toolName == "take_screenshot" || toolName == "screenshot")
            && prevWasKeyboardAction && !isTextBasedUI) {
            qCDebug(log_ai_chat) << "Auto-converting capture_screen to screen_to_markdown (after keyboard action)";
            toolName = "screen_to_markdown";
        }

        // Insert a settle delay when transitioning mouse → keyboard, so the
        // target has time to process the click before keystrokes arrive.
        if (isKeyboardTool && prevWasMouseAction) {
            qCDebug(log_ai_chat) << "Tool delay: mouse→keyboard settle"
                                 << MOUSE_TO_KEYBOARD_DELAY_MS << "ms";
            QThread::msleep(MOUSE_TO_KEYBOARD_DELAY_MS);
        }

        // Insert a short delay before screen capture when preceded by any
        // action, so the screen has time to reflect the previous action.
        if (isCaptureTool && (prevWasMouseAction || !keyboardTokens.isEmpty())) {
            QThread::msleep(PRE_CAPTURE_DELAY_MS);
        }

        if (toolName == "capture_screen" || toolName == "take_screenshot" || toolName == "screenshot") {
            hasNonKeyboardTool = true;
            QString filePath = screenCapture.captureScreen();
            if (!filePath.isEmpty()) {
                attachmentPath = filePath;
                summaries.append("capture_screen: success");
                qCDebug(log_ai_chat) << "AI Tool executed: capture_screen ->" << filePath;
            } else {
                summaries.append("capture_screen: failed (no image captured)");
                qCWarning(log_ai_chat) << "AI Tool failed: capture_screen";
            }

        } else if (toolName == "screen_to_markdown") {
            // OCR-based screen analysis using Tesseract
            hasNonKeyboardTool = true;
            QString filePath = screenCapture.captureScreen();
            if (filePath.isEmpty()) {
                summaries.append("screen_to_markdown: failed (no image captured)");
                qCWarning(log_ai_chat) << "AI Tool failed: screen_to_markdown - no image captured";
            } else {
                QImage frame(filePath);
                if (frame.isNull()) {
                    summaries.append("screen_to_markdown: failed (could not load image)");
                    qCWarning(log_ai_chat) << "AI Tool failed: screen_to_markdown - could not load image";
                } else {
                    // Get detail level from args (default: detailed)
                    QString detailLevel = call.args.value("detail_level", "detailed").toString();

                    // Get mode from args (default: general, can be "terminal" for command output)
                    QString modeStr = call.args.value("mode", "general").toString().toLower();
                    AnalysisMode mode = (modeStr == "terminal") ? AnalysisMode::Terminal : AnalysisMode::General;

                    // Use ScreenAnalyzer to perform OCR
                    ScreenAnalyzer analyzer;
                    if (!analyzer.isAvailable()) {
                        summaries.append("screen_to_markdown: failed (Tesseract OCR not available)");
                        qCWarning(log_ai_chat) << "AI Tool failed: screen_to_markdown - Tesseract not initialized";
                    } else {
                        ScreenAnalysis analysis = analyzer.analyzeScreen(frame, detailLevel, mode);
                        // Save the markdown output to a file for reference
                        QString markdownPath = filePath;
                        markdownPath.replace(".jpg", "_ocr.md");
                        QFile mdFile(markdownPath);
                        if (mdFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
                            QTextStream out(&mdFile);
                            out << analysis.markdownOutput;
                            mdFile.close();
                        }

                        // Store the markdown text as the attachment content
                        // This will be sent to the AI as text instead of an image
                        ocrResultText = analysis.markdownOutput;
                        summaries.append(QString("screen_to_markdown: success (%1 chars OCR text, mode=%2)")
                            .arg(analysis.markdownOutput.length())
                            .arg(modeStr));
                        qCDebug(log_ai_chat) << "AI Tool executed: screen_to_markdown ->"
                                             << analysis.markdownOutput.length() << "chars, mode:" << modeStr;
                    }
                }
            }

        } else if (toolName == "move_mouse") {
            hasNonKeyboardTool = true;
            bool xOk, yOk;
            double nx = doubleArg(call.args.value("x"), &xOk);
            double ny = doubleArg(call.args.value("y"), &yOk);
            if (xOk && yOk) {
                int absX = normalizedToAbsolute(nx);
                int absY = normalizedToAbsolute(ny);
                router.sendMouseMove(absX, absY);
                router.setTrackedMousePos(absX, absY);
                summaries.append(QString("move_mouse: ok (x=%1, y=%2)")
                    .arg(nx, 0, 'f', 3).arg(ny, 0, 'f', 3));
                qCDebug(log_ai_chat) << "AI Tool executed: move_mouse normalized=("
                                     << nx << "," << ny << ") abs=(" << absX << "," << absY << ")";
            } else {
                summaries.append("move_mouse: invalid args");
                qCWarning(log_ai_chat) << "AI Tool failed: move_mouse invalid args";
            }

        } else if (toolName == "left_click") {
            hasNonKeyboardTool = true;
            ClickPoint pt = resolveClick(0x01, call.args, false);
            double lnx = absoluteToNormalized(pt.x);
            double lny = absoluteToNormalized(pt.y);
            QString annotated = screenCapture.captureAnnotatedClick(pt.x, pt.y, "left_click");
            if (!annotated.isEmpty()) {
                attachmentPath = annotated;
                summaries.append(QString("left_click: success (x=%1, y=%2, image=%3)")
                    .arg(lnx, 0, 'f', 3).arg(lny, 0, 'f', 3)
                    .arg(QFileInfo(annotated).fileName()));
            } else {
                summaries.append(QString("left_click: success (x=%1, y=%2, image=unavailable)")
                    .arg(lnx, 0, 'f', 3).arg(lny, 0, 'f', 3));
            }
            qCDebug(log_ai_chat) << "AI Tool executed: left_click normalized=("
                                 << lnx << "," << lny << ") abs=(" << pt.x << "," << pt.y << ")";

        } else if (toolName == "right_click") {
            hasNonKeyboardTool = true;
            ClickPoint pt = resolveClick(0x02, call.args, false);
            double rnx = absoluteToNormalized(pt.x);
            double rny = absoluteToNormalized(pt.y);
            QString annotated = screenCapture.captureAnnotatedClick(pt.x, pt.y, "right_click");
            if (!annotated.isEmpty()) {
                attachmentPath = annotated;
                summaries.append(QString("right_click: success (x=%1, y=%2, image=%3)")
                    .arg(rnx, 0, 'f', 3).arg(rny, 0, 'f', 3)
                    .arg(QFileInfo(annotated).fileName()));
            } else {
                summaries.append(QString("right_click: success (x=%1, y=%2, image=unavailable)")
                    .arg(rnx, 0, 'f', 3).arg(rny, 0, 'f', 3));
            }
            qCDebug(log_ai_chat) << "AI Tool executed: right_click normalized=("
                                 << rnx << "," << rny << ") abs=(" << pt.x << "," << pt.y << ")";

        } else if (toolName == "double_click") {
            hasNonKeyboardTool = true;
            ClickPoint pt = resolveClick(0x01, call.args, true);
            double dnx = absoluteToNormalized(pt.x);
            double dny = absoluteToNormalized(pt.y);
            QString annotated = screenCapture.captureAnnotatedClick(pt.x, pt.y, "double_click");
            if (!annotated.isEmpty()) {
                attachmentPath = annotated;
                summaries.append(QString("double_click: success (x=%1, y=%2, image=%3)")
                    .arg(dnx, 0, 'f', 3).arg(dny, 0, 'f', 3)
                    .arg(QFileInfo(annotated).fileName()));
            } else {
                summaries.append(QString("double_click: success (x=%1, y=%2, image=unavailable)")
                    .arg(dnx, 0, 'f', 3).arg(dny, 0, 'f', 3));
            }
            qCDebug(log_ai_chat) << "AI Tool executed: double_click normalized=("
                                 << dnx << "," << dny << ") abs=(" << pt.x << "," << pt.y << ")";

        } else if (toolName == "left_drag" || toolName == "drag_mouse" ||
                   toolName == "mouse_drag" || toolName == "drag") {
            hasNonKeyboardTool = true;
            bool ok;
            DragPoints dp = resolveDragPoints(call.args, &ok);
            if (!ok) {
                summaries.append("left_drag: invalid args");
                qCWarning(log_ai_chat) << "AI Tool failed: left_drag invalid args";
                continue;
            }
            router.animatedDrag(dp.startX, dp.startY, dp.endX, dp.endY);
            router.setTrackedMousePos(dp.endX, dp.endY);
            double startNX = absoluteToNormalized(dp.startX);
            double startNY = absoluteToNormalized(dp.startY);
            double endNX = absoluteToNormalized(dp.endX);
            double endNY = absoluteToNormalized(dp.endY);
            QString annotated = screenCapture.captureAnnotatedClick(dp.endX, dp.endY, "left_drag");
            if (!annotated.isEmpty()) {
                attachmentPath = annotated;
                summaries.append(QString("left_drag: success (start_x=%1, start_y=%2, x=%3, y=%4, image=%5)")
                    .arg(startNX, 0, 'f', 3).arg(startNY, 0, 'f', 3)
                    .arg(endNX, 0, 'f', 3).arg(endNY, 0, 'f', 3)
                    .arg(QFileInfo(annotated).fileName()));
            } else {
                summaries.append(QString("left_drag: success (start_x=%1, start_y=%2, x=%3, y=%4, image=unavailable)")
                    .arg(startNX, 0, 'f', 3).arg(startNY, 0, 'f', 3)
                    .arg(endNX, 0, 'f', 3).arg(endNY, 0, 'f', 3));
            }
            qCDebug(log_ai_chat) << "AI Tool executed: left_drag start=("
                                 << dp.startX << "," << dp.startY << ") end=("
                                 << dp.endX << "," << dp.endY << ")";

        } else if (toolName == "type_text") {
            QString text = call.args.value("text").toString();
            if (text.isEmpty()) {
                summaries.append("type_text: empty text");
                qCWarning(log_ai_chat) << "AI Tool failed: type_text empty";
            } else {
                keyboardTokens.append(text);
                // Check if it looks like a key sequence (contains < and >, or is a short
                // modifier combination without spaces). Only redirect if the ENTIRE text
                // is a key sequence, not if it's mixed with regular text like "sudo reboot<enter>".
                //
                // Key sequences typically:
                // - Don't contain spaces (they use + to join keys)
                // - Are relatively short (< 30 chars)
                // - May be wrapped in angle brackets like "<enter>" or "<ctrl+l>"
                // - Or are plain modifier combos like "ctrl+l", "alt+f4"
                //
                // If the text has spaces, it's almost certainly regular text that should
                // be typed as-is, not interpreted as a key sequence.
                bool hasSpaces = text.contains(' ');
                bool hasAngleBrackets = text.contains('<') && text.contains('>');
                bool isShort = text.length() < 30;

                // Only redirect if it's clearly a key sequence:
                // - Either wrapped in angle brackets with no spaces: "<ctrl+l>"
                // - Or a short modifier combo with no spaces: "ctrl+l", "alt+f4"
                bool looksLikeKeySequence = false;
                if (hasAngleBrackets && !hasSpaces) {
                    // Text like "<enter>" or "<ctrl+l>" - definitely a key sequence
                    looksLikeKeySequence = true;
                } else if (isShort && !hasSpaces && text.contains('+')) {
                    // Text like "ctrl+l" or "alt+shift+t" - likely a key sequence
                    looksLikeKeySequence = true;
                }

                if (looksLikeKeySequence) {
                    // Strip angle brackets if present before sending to sendShortcut
                    QString keySeq = text;
                    if (hasAngleBrackets) {
                        keySeq.remove('<');
                        keySeq.remove('>');
                    }
                    // Redirect to press_key
                    router.sendShortcut(keySeq);
                    summaries.append(QString("type_text(redirected to press_key): success (keys=\"%1\")").arg(keySeq));
                    qCDebug(log_ai_chat) << "AI Tool type_text redirected to press_key: keys=" << keySeq;
                    // sendShortcut schedules key press/release via QTimer on the main
                    // thread. Wait for the full key sequence to finish before the
                    // background thread proceeds to the next tool, otherwise a
                    // follow-up press_key (e.g. "enter") could fire before this one
                    // completes.
                    int steps = qMax(1, keySeq.split('+').size());
                    QThread::msleep(steps * 80 + 50);
                } else {
                    router.sendText(text);
                    summaries.append(QString("type_text: success (chars=%1, text=\"%2\")")
                        .arg(text.length()).arg(text));
                    int estimatedMs = estimateTypingDurationMs(text.length());
                    qCDebug(log_ai_chat) << "AI Tool executed: type_text chars=" << text.length()
                                         << "text=\"" << text << "\""
                                         << "estimatedDuration=" << estimatedMs << "ms";
                    // sendText queues character-by-character typing on the main thread
                    // via handlePastingCharacters (async, ~typingDelayMs per char in
                    // batches). Block the background thread until typing is expected
                    // to finish so a follow-up press_key (e.g. "enter") doesn't fire
                    // before all characters have been sent.
                    //
                    // Add a 200ms safety margin: the estimate doesn't account for the
                    // initial scheduling delay (QMetaObject::invokeMethod queues the
                    // first tick on the main thread; if the main thread is busy with
                    // the previous tool's screenshot capture etc., typing starts late).
                    // Without the margin, the background thread can wake up and issue
                    // the next tool before all characters are sent, causing the first
                    // or last characters to be lost or interleaved with the next tool's
                    // key events.
                    QThread::msleep(estimatedMs + 200);
                }
            }

        } else if (toolName == "press_key" || toolName == "key_press" ||
                   toolName == "send_key" || toolName == "hotkey") {
            QString keys = call.args.value("keys").toString();
            if (keys.isEmpty()) keys = call.args.value("key").toString();
            keys = keys.trimmed();
            if (keys.isEmpty()) {
                summaries.append("press_key: missing keys argument");
                qCWarning(log_ai_chat) << "AI Tool failed: press_key missing keys";
            } else {
                keyboardTokens.append(keys);
                router.sendShortcut(keys);
                summaries.append(QString("press_key: success (keys=\"%1\")").arg(keys));
                qCDebug(log_ai_chat) << "AI Tool executed: press_key keys=" << keys;
                // sendShortcut schedules key press/release via QTimer::singleShot on
                // the main thread. Wait for the sequence to complete before the
                // background thread proceeds, so a following tool (e.g. another
                // press_key or capture_screen) doesn't race with in-flight key events.
                int steps = qMax(1, keys.split('+').size());
                QThread::msleep(steps * 80 + 50);
            }

        } else if (toolName == "run_bash" || toolName == "bash" ||
                   toolName == "shell" || toolName == "exec_command") {
            hasNonKeyboardTool = true;
            QString command = call.args.value("command").toString();
            if (command.isEmpty()) {
                summaries.append("run_bash: missing command argument");
                qCWarning(log_ai_chat) << "AI Tool failed: run_bash missing command";
            } else {
                QString result = runBashCommand(command);
                summaries.append(QString("run_bash: %1").arg(result));
                qCDebug(log_ai_chat) << "AI Tool executed: run_bash command=" << command;
            }

        } else if (toolName == "set_target_system") {
            // Let the AI correct the target OS when it detects a mismatch
            // (e.g. UI shows macOS but the target is actually running Windows).
            hasNonKeyboardTool = true;
            const QString requested = call.args.value("system").toString().trimmed().toLower();
            if (requested.isEmpty()) {
                summaries.append("set_target_system: missing 'system' argument");
                qCWarning(log_ai_chat) << "AI Tool failed: set_target_system missing system arg";
            } else {
                const ChatTargetSystem ts = chatTargetSystemFromString(requested);
                const QString canonical = chatTargetSystemToString(ts);
                settings.setChatTargetSystem(canonical);
                const QString display = chatTargetSystemDisplayName(ts);
                summaries.append(QString("set_target_system: target OS set to %1")
                                     .arg(display));
                qCDebug(log_ai_chat) << "AI Tool executed: set_target_system ->" << display;
            }

        } else if (toolName == "start_recording" || toolName == "record_screen" ||
                   toolName == "start_video_recording") {
            // Start recording the target screen
            hasNonKeyboardTool = true;
            RecordingController &recorder = RecordingController::instance();
            if (recorder.isRecording()) {
                summaries.append("start_recording: already recording");
                qCDebug(log_ai_chat) << "AI Tool: start_recording - already recording";
            } else {
                recorder.startRecording();
                summaries.append("start_recording: success");
                qCDebug(log_ai_chat) << "AI Tool executed: start_recording";
            }

        } else if (toolName == "stop_recording" || toolName == "stop_video_recording") {
            // Stop recording the target screen
            hasNonKeyboardTool = true;
            RecordingController &recorder = RecordingController::instance();
            if (!recorder.isRecording()) {
                summaries.append("stop_recording: not currently recording");
                qCDebug(log_ai_chat) << "AI Tool: stop_recording - not recording";
            } else {
                recorder.stopRecording();
                summaries.append("stop_recording: success");
                qCDebug(log_ai_chat) << "AI Tool executed: stop_recording";
            }

        } else if (toolName == "repeat_key" || toolName == "repeat_keys" ||
                   toolName == "press_key_repeatedly" || toolName == "spam_key") {
            // Press a key repeatedly at specified intervals (e.g. for entering BIOS)
            hasNonKeyboardTool = true;
            QString keys = call.args.value("keys").toString();
            if (keys.isEmpty()) keys = call.args.value("key").toString();
            keys = keys.trimmed();

            // Get interval in milliseconds (default: 1000ms = 1 second)
            bool intervalOk;
            int intervalMs = intArg(call.args.value("interval_ms"), &intervalOk);
            if (!intervalOk || intervalMs <= 0) {
                intervalMs = 1000; // Default 1 second
            }

            // Get count (number of times to press)
            bool countOk;
            int count = intArg(call.args.value("count"), &countOk);
            if (!countOk || count <= 0) {
                summaries.append("repeat_key: missing or invalid 'count' argument");
                qCWarning(log_ai_chat) << "AI Tool failed: repeat_key missing count";
            } else if (keys.isEmpty()) {
                summaries.append("repeat_key: missing 'keys' argument");
                qCWarning(log_ai_chat) << "AI Tool failed: repeat_key missing keys";
            } else {
                // Validate count to prevent excessive key presses
                if (count > 100) {
                    summaries.append(QString("repeat_key: count %1 exceeds maximum (100), limiting to 100").arg(count));
                    qCWarning(log_ai_chat) << "AI Tool: repeat_key count" << count << "exceeds max, limiting to 100";
                    count = 100;
                }

                int successCount = 0;
                for (int i = 0; i < count; ++i) {
                    keyboardTokens.append(keys);
                    router.sendShortcut(keys);
                    successCount++;

                    // Wait for the key sequence to complete
                    int steps = qMax(1, keys.split('+').size());
                    QThread::msleep(steps * 80 + 50);

                    // Wait for the interval before the next press (except after the last one)
                    if (i < count - 1) {
                        QThread::msleep(intervalMs);
                    }
                }

                summaries.append(QString("repeat_key: success (key=\"%1\", count=%2, interval=%3ms)")
                    .arg(keys).arg(successCount).arg(intervalMs));
                qCDebug(log_ai_chat) << "AI Tool executed: repeat_key keys=" << keys
                                     << "count=" << successCount
                                     << "intervalMs=" << intervalMs;
            }

        } else {
            hasNonKeyboardTool = true;
            summaries.append(QString("%1: unsupported").arg(toolName));
            qCWarning(log_ai_chat) << "AI Tool unsupported:" << toolName;
        }

        // Update mouse-action tracker for inter-tool delay logic.
        // Mouse tools set the flag; keyboard and capture tools clear it
        // (a capture after a keyboard-only run doesn't need the delay on
        // the *next* iteration either, since capture itself doesn't need
        // a mouse settle).
        prevWasMouseAction = isMouseTool;

        // Update keyboard-action tracker for auto-OCR logic.
        // After keyboard actions, we prefer OCR for screen analysis.
        prevWasKeyboardAction = isKeyboardTool;

        // After a keyboard tool, give the target time to *process* the
        // received HID keystrokes and render the result (e.g. open a
        // terminal window for Ctrl+Alt+T, or draw the typed character).
        // The per-tool sleep earlier covers transmission; this covers the
        // target's reaction time.
        if (isKeyboardTool) {
            QThread::msleep(POST_KEYBOARD_SETTLE_MS);
        }
    }

    QString macroData;
    if (!hasNonKeyboardTool && !keyboardTokens.isEmpty()) {
        macroData = keyboardTokens.join(QString());
    }

    return AgentToolExecutionResult(summaries.join("\n"), attachmentPath, macroData, ocrResultText);
}

// ============================================================================
// Bash runner
// ============================================================================

QString ChatToolExecution::runBashCommand(const QString &command) const
{
    // Working directory: AppDataLocation/Openterface
    QString workDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(workDir);

    QProcess process;
    process.setWorkingDirectory(workDir);
    process.setProcessChannelMode(QProcess::MergedChannels);

#ifdef Q_OS_WIN
    process.start("cmd.exe", QStringList() << "/c" << command);
#else
    process.start("/bin/bash", QStringList() << "-c" << command);
#endif

    if (!process.waitForStarted(5000)) {
        return QString("launch_error: %1").arg(process.errorString());
    }

    if (!process.waitForFinished(30000)) {
        process.kill();
        return "error: process timed out after 30s";
    }

    QByteArray output = process.readAll();
    int exitCode = process.exitCode();

    QString combined = QString::fromUtf8(output).trimmed();
    if (combined.length() > 8192) {
        combined = combined.left(8192) + "\n[output truncated at 8192 chars]";
    }
    if (combined.isEmpty()) combined = "(empty)";

    return QString("exit=%1 output=%2").arg(exitCode).arg(combined);
}

// ============================================================================
// Typing-duration estimate
// ============================================================================

int ChatToolExecution::estimateTypingDurationMs(int charCount)
{
    if (charCount <= 0) return 0;

    const int batchSize = GlobalSetting::instance().getChatBatchSize();
    const int perCharDelay = GlobalSetting::instance().getChatTypingDelayMs();
    const int batchDelay = GlobalSetting::instance().getChatTypingDelayMs();
    const int initialDelay = GlobalSetting::instance().getChatInitialTypingDelayMs();

    // handlePastingCharacters processes `batchSize` chars per tick, sleeping
    // perCharDelay after each char, then yields via QTimer::singleShot(batchDelay)
    // before the next tick. Total time:
    //   initialDelay                      -- initial delay before first character
    //   charCount * perCharDelay          -- per-char sleeps across all batches
    //   + max(0, numBatches - 1) * batchDelay -- inter-batch yields (last batch exits)
    //
    // The initial delay (in handlePastingCharacters) gives the target OS
    // time to process a preceding mouse click or keyboard shortcut (like ctrl+alt+t)
    // and ensure the target window has focus before the first keystroke arrives.
    const int effectiveBatch = qMax(1, batchSize);
    const int numBatches = (charCount + effectiveBatch - 1) / effectiveBatch;
    return initialDelay + charCount * perCharDelay + qMax(0, numBatches - 1) * batchDelay;
}

// ============================================================================
// Coordinate helpers
// ============================================================================

int ChatToolExecution::normalizedToAbsolute(double value)
{
    return qBound(0, static_cast<int>(qBound(0.0, value, 1.0) * 4096.0 + 0.5), 4096);
}

double ChatToolExecution::absoluteToNormalized(int value)
{
    return qBound(0.0, static_cast<double>(value) / 4096.0, 1.0);
}

double ChatToolExecution::doubleArg(const QVariant &value, bool *ok)
{
    if (ok) *ok = false;
    if (!value.isValid()) return 0.0;

    bool success = false;
    double result = 0.0;

    switch (value.typeId()) {
    case QMetaType::Double:
    case QMetaType::Float:
        result = value.toDouble();
        success = true;
        break;
    case QMetaType::Int:
    case QMetaType::Long:
    case QMetaType::LongLong:
    case QMetaType::UInt:
    case QMetaType::ULong:
    case QMetaType::ULongLong:
        result = value.toDouble(&success);
        break;
    case QMetaType::QString:
        result = value.toString().toDouble(&success);
        break;
    default:
        result = value.toDouble(&success);
        break;
    }

    if (ok) *ok = success;
    return success ? result : 0.0;
}

int ChatToolExecution::intArg(const QVariant &value, bool *ok)
{
    if (ok) *ok = false;
    if (!value.isValid()) return 0;

    bool success = false;
    int result = 0;

    switch (value.typeId()) {
    case QMetaType::Int:
    case QMetaType::Long:
    case QMetaType::LongLong:
    case QMetaType::UInt:
    case QMetaType::ULong:
    case QMetaType::ULongLong:
        result = value.toInt();
        success = true;
        break;
    case QMetaType::Double:
    case QMetaType::Float:
        result = static_cast<int>(value.toDouble(&success));
        break;
    case QMetaType::QString:
        result = value.toString().toInt(&success);
        break;
    default:
        result = value.toInt(&success);
        break;
    }

    if (ok) *ok = success;
    return success ? result : 0;
}

// ============================================================================
// Click resolution
// ============================================================================

ChatToolExecution::ClickPoint ChatToolExecution::resolveClick(
    int button, const QVariantMap &args, bool isDoubleClick)
{
    ChatInputRouter &router = ChatInputRouter::instance();
    int x, y;

    bool xOk, yOk;
    double nx = doubleArg(args.value("x"), &xOk);
    double ny = doubleArg(args.value("y"), &yOk);

    if (xOk && yOk) {
        x = normalizedToAbsolute(nx);
        y = normalizedToAbsolute(ny);
    } else {
        // Use tracked position
        x = router.trackedMouseX();
        y = router.trackedMouseY();
    }

    router.setTrackedMousePos(x, y);
    router.animatedClick(button, x, y, isDoubleClick);

    return {x, y};
}

// ============================================================================
// Drag resolution
// ============================================================================

ChatToolExecution::DragPoints ChatToolExecution::resolveDragPoints(
    const QVariantMap &args, bool *ok)
{
    if (ok) *ok = false;
    ChatInputRouter &router = ChatInputRouter::instance();

    // Resolve end position
    int endX, endY;
    bool xOk, yOk;
    double nx = doubleArg(args.value("x"), &xOk);
    double ny = doubleArg(args.value("y"), &yOk);

    if (xOk && yOk) {
        endX = normalizedToAbsolute(nx);
        endY = normalizedToAbsolute(ny);
    } else {
        // Try end_x / end_y
        double enx = doubleArg(args.value("end_x"), &xOk);
        double eny = doubleArg(args.value("end_y"), &yOk);
        if (xOk && yOk) {
            endX = normalizedToAbsolute(enx);
            endY = normalizedToAbsolute(eny);
        } else {
            return {0, 0, 0, 0};
        }
    }

    // Resolve start position
    int startX, startY;
    double snx = doubleArg(args.value("start_x"), &xOk);
    double sny = doubleArg(args.value("start_y"), &yOk);

    if (xOk && yOk) {
        startX = normalizedToAbsolute(snx);
        startY = normalizedToAbsolute(sny);
    } else {
        // Use tracked position
        startX = router.trackedMouseX();
        startY = router.trackedMouseY();
    }

    if (ok) *ok = true;
    return {startX, startY, endX, endY};
}
