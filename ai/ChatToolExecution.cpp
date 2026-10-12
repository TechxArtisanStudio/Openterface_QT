#include "ChatToolExecution.h"
#include "ChatInputRouter.h"
#include "ChatScreenCapture.h"
#include "SharedToolExecutor.h"
#include "ChatTaskScheduler.h"
#include "ChatTypes.h"
#include "WebSearchManager.h"
#include "ui/globalsetting.h"
#include "ui/recording/recordingcontroller.h"
#include "server/mcp/screenAnalyzer.h"
#include "host/cameramanager.h"
#include "host/HostManager.h"
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QLoggingCategory>
#include <QProcess>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QThread>
#include <QUrl>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QEventLoop>
#include <QTimer>

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

// ============================================================================
// BIOS/UEFI Screen Detection
// ============================================================================

/**
 * @brief Detect if the OCR text indicates a BIOS/UEFI screen.
 *
 * BIOS/UEFI screens have distinctive characteristics:
 * - Specific headers like "Aptio Setup Utility", "BIOS Setup", "UEFI Firmware Settings"
 * - Text-based menus with colored highlighting (though we can't detect colors in OCR)
 * - Menu items like "Main", "Advanced", "Boot", "Security", "Exit"
 * - No desktop environment indicators (no taskbar, no window manager)
 *
 * @param ocrText The OCR-extracted text from the screen
 * @return true if this appears to be a BIOS/UEFI screen
 */
static bool detectBiosScreen(const QString &ocrText)
{
    if (ocrText.isEmpty()) return false;

    QString text = ocrText.toLower();

    // Strong indicators - these are almost always BIOS/UEFI
    static const QStringList strongIndicators = {
        "aptio setup utility",
        "bios setup",
        "uefi firmware settings",
        "bios version",
        "bios revision",
        "setup utility",
        "american megatrends",
        "ami bios",
        "award bios",
        "phoenix bios",
        "insyde bios",
        "bios date",
        "system bios",
    };

    for (const QString &indicator : strongIndicators) {
        if (text.contains(indicator)) {
            qCDebug(log_ai_chat) << "BIOS detected: strong indicator found:" << indicator;
            return true;
        }
    }

    // Medium indicators - combination of these suggests BIOS
    static const QStringList mediumIndicators = {
        "main", "advanced", "boot", "security", "exit",
        "chipset", "peripherals", "power management",
        "save & exit", "discard changes",
        "load optimized defaults",
        "set supervisor password",
        "set user password",
    };

    int mediumCount = 0;
    for (const QString &indicator : mediumIndicators) {
        if (text.contains(indicator)) {
            mediumCount++;
        }
    }

    // If we see 3+ BIOS menu items, it's likely a BIOS screen
    if (mediumCount >= 3) {
        qCDebug(log_ai_chat) << "BIOS detected: found" << mediumCount << "BIOS menu indicators";
        return true;
    }

    return false;
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
                              toolName == "screenshot" || toolName == "screen_to_markdown" ||
                              toolName == "screen_diff");

        // Auto-convert capture_screen after keyboard actions
        // - In BIOS/TextUI modes: use screen_diff (change detection + highlight detection)
        // - In general OS/terminal: use screen_to_markdown (OCR text extraction)
        if ((toolName == "capture_screen" || toolName == "take_screenshot" || toolName == "screenshot")
            && prevWasKeyboardAction) {
            if (isTextBasedUI) {
                qCDebug(log_ai_chat) << "Auto-converting capture_screen to screen_diff (BIOS/TextUI)";
                toolName = "screen_diff";
            } else {
                qCDebug(log_ai_chat) << "Auto-converting capture_screen to screen_to_markdown (general OS/terminal)";
                toolName = "screen_to_markdown";
            }
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

            QJsonObject argsObj = QJsonObject::fromVariantMap(call.args);
            QJsonObject result = SharedToolExecutor::instance().screenToMarkdown(argsObj);

            if (result.contains("error")) {
                summaries.append(QString("screen_to_markdown: failed (%1)").arg(result["error"].toString()));
                qCWarning(log_ai_chat) << "AI Tool failed: screen_to_markdown -" << result["error"].toString();
            } else {
                QString markdown = result["markdown"].toString();
                QString modeStr = result["mode"].toString();

                // Get screenshot file path for saving the markdown file
                QString filePath = screenCapture.captureScreen();
                if (!filePath.isEmpty()) {
                    // Save the markdown output to a file for reference
                    QString markdownPath = filePath;
                    markdownPath.replace(".jpg", "_ocr.md");
                    QFile mdFile(markdownPath);
                    if (mdFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
                        QTextStream out(&mdFile);
                        out << markdown;
                        mdFile.close();
                    }
                }

                // Store the markdown text as the attachment content
                // This will be sent to the AI as text instead of an image
                ocrResultText = markdown;
                summaries.append(QString("screen_to_markdown: success (%1 chars OCR text, mode=%2)")
                    .arg(markdown.length())
                    .arg(modeStr));
                qCDebug(log_ai_chat) << "AI Tool executed: screen_to_markdown ->"
                                     << markdown.length() << "chars, mode:" << modeStr;

                // Auto-detect BIOS/UEFI screen and switch target system if needed
                // Only do this if not already in BIOS/TextUI mode
                GlobalSetting &settings = GlobalSetting::instance();
                QString currentTarget = settings.getChatTargetSystem();
                if (!currentTarget.isEmpty()) {
                    QString currentLower = currentTarget.toLower();
                    if (currentLower != "bios" && currentLower != "uefi") {
                        if (detectBiosScreen(markdown)) {
                            qCDebug(log_ai_chat) << "BIOS screen detected, auto-switching target system to BIOS";
                            settings.setChatTargetSystem("bios");
                            summaries.append("screen_to_markdown: BIOS detected — target system auto-switched to BIOS/UEFI");
                        }
                    }
                }
            }

        } else if (toolName == "screen_diff") {
            // Differential screen analysis: what CHANGED, not what IS.
            // Much cheaper and more accurate than a full screenshot for iterative
            // navigation tasks (BIOS menus, terminal output, etc.).
            hasNonKeyboardTool = true;

            QJsonObject argsObj = QJsonObject::fromVariantMap(call.args);
            QJsonObject result = SharedToolExecutor::instance().screenDiff(argsObj);

            if (result.contains("error")) {
                summaries.append(QString("screen_diff: failed (%1)").arg(result["error"].toString()));
                qCWarning(log_ai_chat) << "AI Tool failed: screen_diff -" << result["error"].toString();
            } else {
                QString report = result["report"].toString();
                QString outcome = result["outcome"].toString();
                ocrResultText = report;
                summaries.append(QString("screen_diff: %1 (change_ratio=%2)")
                    .arg(outcome)
                    .arg(result["change_ratio"].toDouble(), 0, 'f', 3));
                qCDebug(log_ai_chat) << "AI Tool executed: screen_diff ->"
                                     << outcome << "," << report.length() << "chars";
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

        } else if (toolName == "web_search" || toolName == "search" ||
                   toolName == "internet_search") {
            // Search the web for information using WebSearchManager
            hasNonKeyboardTool = true;
            qCDebug(log_ai_chat) << "web_search: call.args keys:" << call.args.keys();
            qCDebug(log_ai_chat) << "web_search: call.args:" << call.args;
            // Try multiple possible argument names the AI might use
            QString query = call.args.value("query").toString();
            if (query.isEmpty()) query = call.args.value("q").toString();
            if (query.isEmpty()) query = call.args.value("text").toString();
            if (query.isEmpty()) query = call.args.value("search").toString();
            qCDebug(log_ai_chat) << "web_search: extracted query:" << query;
            if (query.isEmpty()) {
                summaries.append("web_search: missing query argument");
                qCWarning(log_ai_chat) << "AI Tool failed: web_search missing query, args were:" << call.args;
            } else {
                // Use WebSearchManager for configurable provider fallback chain
                QString result = WebSearchManager::instance().search(query);
                summaries.append(QString("web_search: %1").arg(result));
                qCDebug(log_ai_chat) << "AI Tool executed: web_search query=" << query;
            }

        } else if (toolName == "web_fetch" || toolName == "fetch_url" || toolName == "get_url") {
            // Fetch the content of a specific URL
            hasNonKeyboardTool = true;
            QString url = call.args.value("url").toString();
            if (url.isEmpty()) {
                summaries.append("web_fetch: missing url argument");
                qCWarning(log_ai_chat) << "AI Tool failed: web_fetch missing url, args were:" << call.args;
            } else {
                int maxLength = 8000; // default
                QVariant maxLenVar = call.args.value("max_length");
                if (maxLenVar.isValid() && maxLenVar.canConvert<int>()) {
                    maxLength = maxLenVar.toInt();
                    if (maxLength <= 0) maxLength = 8000;
                }
                QString result = fetchUrlContent(url, maxLength);
                summaries.append(QString("web_fetch: %1").arg(result));
                qCDebug(log_ai_chat) << "AI Tool executed: web_fetch url=" << url;
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

                // Parse the key to get keyCode and modifiers
                // Use synchronous key presses instead of async sendShortcut
                int keyCode = 0;
                int modifiers = 0;

                // Normalize the key format
                QString normalizedKey = keys.trimmed();

                // Convert angle-bracket format to plus-separated format
                if (normalizedKey.contains('<') || normalizedKey.contains('>')) {
                    QString converted;
                    int i = 0;
                    while (i < normalizedKey.length()) {
                        if (normalizedKey[i] == '<') {
                            int end = normalizedKey.indexOf('>', i);
                            if (end > i) {
                                if (!converted.isEmpty()) converted += '+';
                                converted += normalizedKey.mid(i + 1, end - i - 1);
                                i = end + 1;
                            } else {
                                i++;
                            }
                        } else if (normalizedKey[i].isLetterOrNumber()) {
                            if (!converted.isEmpty() && !converted.endsWith('+')) {
                                converted += '+';
                            }
                            int start = i;
                            while (i < normalizedKey.length() && normalizedKey[i].isLetterOrNumber()) {
                                i++;
                            }
                            converted += normalizedKey.mid(start, i - start);
                        } else {
                            i++;
                        }
                    }
                    normalizedKey = converted;
                }

                // Parse the key like "ctrl+l", "alt+f4", "f2"
                QStringList parts = normalizedKey.toLower().split('+');
                for (auto &p : parts) p = p.trimmed();
                if (!parts.isEmpty()) {
                    QString keyToken = parts.last();
                    QStringList modifierList = parts.mid(0, parts.size() - 1);

                    // Build modifier flags
                    for (const auto &mod : modifierList) {
                        if (mod == "ctrl" || mod == "control") modifiers |= Qt::ControlModifier;
                        else if (mod == "alt" || mod == "option") modifiers |= Qt::AltModifier;
                        else if (mod == "shift") modifiers |= Qt::ShiftModifier;
                        else if (mod == "meta" || mod == "super" || mod == "win" || mod == "cmd") modifiers |= Qt::MetaModifier;
                    }

                    // Map the main key
                    static const QMap<QString, int> namedKeys = {
                        {"esc", Qt::Key_Escape}, {"escape", Qt::Key_Escape},
                        {"enter", Qt::Key_Return}, {"return", Qt::Key_Return},
                        {"tab", Qt::Key_Tab}, {"space", Qt::Key_Space},
                        {"backspace", Qt::Key_Backspace}, {"delete", Qt::Key_Delete},
                        {"home", Qt::Key_Home}, {"end", Qt::Key_End},
                        {"pageup", Qt::Key_PageUp}, {"pagedown", Qt::Key_PageDown},
                        {"up", Qt::Key_Up}, {"down", Qt::Key_Down},
                        {"left", Qt::Key_Left}, {"right", Qt::Key_Right},
                        {"f1", Qt::Key_F1}, {"f2", Qt::Key_F2}, {"f3", Qt::Key_F3},
                        {"f4", Qt::Key_F4}, {"f5", Qt::Key_F5}, {"f6", Qt::Key_F6},
                        {"f7", Qt::Key_F7}, {"f8", Qt::Key_F8}, {"f9", Qt::Key_F9},
                        {"f10", Qt::Key_F10}, {"f11", Qt::Key_F11}, {"f12", Qt::Key_F12}
                    };

                    if (namedKeys.contains(keyToken)) {
                        keyCode = namedKeys[keyToken];
                    } else if (keyToken.length() == 1) {
                        keyCode = keyToken.at(0).toUpper().unicode();
                    }
                }

                if (keyCode == 0) {
                    summaries.append(QString("repeat_key: unknown key '%1'").arg(keys));
                    qCWarning(log_ai_chat) << "AI Tool failed: repeat_key unknown key:" << keys;
                } else {
                    int successCount = 0;
                    HostManager &hostManager = HostManager::getInstance();

                    for (int i = 0; i < count; ++i) {
                        keyboardTokens.append(keys);

                        // Synchronous key press and release
                        hostManager.handleKeyboardAction(keyCode, modifiers, true);
                        QThread::msleep(50); // Hold key for 50ms
                        hostManager.handleKeyboardAction(keyCode, modifiers, false);

                        successCount++;

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
            }

        } else if (toolName == "navigate_to_menu_item" || toolName == "navigate_to" ||
                   toolName == "navigate_to_item" || toolName == "go_to_menu_item") {
            // Navigate through BIOS/TextUI menus by pressing an arrow key until
            // a specific menu item is highlighted.
            hasNonKeyboardTool = true;

            QJsonObject argsObj = QJsonObject::fromVariantMap(call.args);
            QJsonObject result = SharedToolExecutor::instance().navigateToMenuItem(argsObj);

            if (result.contains("error")) {
                summaries.append(QString("navigate_to_menu_item: failed (%1)").arg(result["error"].toString()));
                qCWarning(log_ai_chat) << "AI Tool failed: navigate_to_menu_item -" << result["error"].toString();
            } else {
                bool success = result["success"].toBool();
                int steps = result["steps_taken"].toInt();
                QString highlight = result["final_highlight"].toString();
                QString msg = result["message"].toString();
                QString target = argsObj.value("target").toString();

                if (success) {
                    summaries.append(QString("navigate_to_menu_item: SUCCESS — '%1' is now highlighted after %2 step(s)")
                        .arg(highlight).arg(steps));
                } else {
                    summaries.append(QString("navigate_to_menu_item: FAILED — '%1' not found after %2 step(s). Last highlight: '%3'")
                        .arg(target).arg(steps).arg(highlight));
                }
                ocrResultText = msg;
                qCDebug(log_ai_chat) << "AI Tool executed: navigate_to_menu_item target='" << target
                                     << "' success=" << success << " steps=" << steps
                                     << " highlight='" << highlight << "'";
            }

        } else if (toolName == "reboot_to_bios" || toolName == "boot_to_bios" ||
                   toolName == "restart_to_bios" || toolName == "enter_bios") {
            // Press BIOS key repeatedly after a delay (for entering BIOS after reboot)
            // Assumes reboot has already been initiated via other means
            hasNonKeyboardTool = true;

            // Get parameters with defaults
            QString biosKey = call.args.value("bios_key").toString().trimmed().toLower();
            if (biosKey.isEmpty()) biosKey = "del";

            bool delayOk;
            int delayBeforePressMs = intArg(call.args.value("delay_before_press_ms"), &delayOk);
            if (!delayOk || delayBeforePressMs < 0) {
                delayBeforePressMs = 2000; // Default 2 seconds
            }

            bool countOk;
            int pressCount = intArg(call.args.value("press_count"), &countOk);
            if (!countOk || pressCount <= 0) {
                pressCount = 20; // Default 20 presses
            }
            if (pressCount > 100) {
                summaries.append(QString("reboot_to_bios: press_count %1 exceeds maximum (100), limiting to 100").arg(pressCount));
                qCWarning(log_ai_chat) << "AI Tool: reboot_to_bios press_count" << pressCount << "exceeds max, limiting to 100";
                pressCount = 100;
            }

            bool intervalOk;
            int intervalMs = intArg(call.args.value("interval_ms"), &intervalOk);
            if (!intervalOk || intervalMs <= 0) {
                intervalMs = 1000; // Default 1 second
            }

            qCDebug(log_ai_chat) << "AI Tool: reboot_to_bios - bios_key=" << biosKey
                                 << "delay_before_press_ms=" << delayBeforePressMs
                                 << "press_count=" << pressCount
                                 << "interval_ms=" << intervalMs;

            // Validate the BIOS key
            int biosKeyCode = 0;
            int biosModifiers = 0;

            // Normalize the key format (handle angle brackets, plus separators, etc.)
            QString normalizedKey = biosKey;

            // Convert angle-bracket format to plus-separated format
            if (normalizedKey.contains('<') || normalizedKey.contains('>')) {
                QString converted;
                int i = 0;
                while (i < normalizedKey.length()) {
                    if (normalizedKey[i] == '<') {
                        int end = normalizedKey.indexOf('>', i);
                        if (end > i) {
                            if (!converted.isEmpty()) converted += '+';
                            converted += normalizedKey.mid(i + 1, end - i - 1);
                            i = end + 1;
                        } else {
                            i++;
                        }
                    } else if (normalizedKey[i].isLetterOrNumber()) {
                        if (!converted.isEmpty() && !converted.endsWith('+')) {
                            converted += '+';
                        }
                        int start = i;
                        while (i < normalizedKey.length() && normalizedKey[i].isLetterOrNumber()) {
                            i++;
                        }
                        converted += normalizedKey.mid(start, i - start);
                    } else {
                        i++;
                    }
                }
                normalizedKey = converted;
            }

            // Parse the key like "ctrl+l", "alt+f4", "del", "f2"
            QStringList parts = normalizedKey.toLower().split('+');
            for (auto &p : parts) p = p.trimmed();
            if (!parts.isEmpty()) {
                QString keyToken = parts.last();
                QStringList modifierList = parts.mid(0, parts.size() - 1);

                // Build modifier flags
                for (const auto &mod : modifierList) {
                    if (mod == "ctrl" || mod == "control") biosModifiers |= Qt::ControlModifier;
                    else if (mod == "alt" || mod == "option") biosModifiers |= Qt::AltModifier;
                    else if (mod == "shift") biosModifiers |= Qt::ShiftModifier;
                    else if (mod == "meta" || mod == "super" || mod == "win" || mod == "cmd") biosModifiers |= Qt::MetaModifier;
                }

                // Map the main key
                static const QMap<QString, int> namedKeys = {
                    {"esc", Qt::Key_Escape}, {"escape", Qt::Key_Escape},
                    {"enter", Qt::Key_Return}, {"return", Qt::Key_Return},
                    {"tab", Qt::Key_Tab}, {"space", Qt::Key_Space},
                    {"backspace", Qt::Key_Backspace}, {"delete", Qt::Key_Delete},
                    {"del", Qt::Key_Delete},
                    {"home", Qt::Key_Home}, {"end", Qt::Key_End},
                    {"pageup", Qt::Key_PageUp}, {"pagedown", Qt::Key_PageDown},
                    {"up", Qt::Key_Up}, {"down", Qt::Key_Down},
                    {"left", Qt::Key_Left}, {"right", Qt::Key_Right},
                    {"f1", Qt::Key_F1}, {"f2", Qt::Key_F2}, {"f3", Qt::Key_F3},
                    {"f4", Qt::Key_F4}, {"f5", Qt::Key_F5}, {"f6", Qt::Key_F6},
                    {"f7", Qt::Key_F7}, {"f8", Qt::Key_F8}, {"f9", Qt::Key_F9},
                    {"f10", Qt::Key_F10}, {"f11", Qt::Key_F11}, {"f12", Qt::Key_F12}
                };

                if (namedKeys.contains(keyToken)) {
                    biosKeyCode = namedKeys[keyToken];
                } else if (keyToken.length() == 1) {
                    biosKeyCode = keyToken.at(0).toUpper().unicode();
                }
            }

            if (biosKeyCode == 0) {
                summaries.append(QString("reboot_to_bios: unknown bios_key '%1'").arg(biosKey));
                qCWarning(log_ai_chat) << "AI Tool failed: reboot_to_bios unknown bios_key:" << biosKey;
            } else {
                // Step 1: Wait for the specified delay (to allow reboot to progress)
                if (delayBeforePressMs > 0) {
                    qCDebug(log_ai_chat) << "AI Tool: reboot_to_bios - waiting" << delayBeforePressMs << "ms before pressing" << biosKey;
                    QThread::msleep(delayBeforePressMs);
                }

                // Step 2: Press the BIOS key repeatedly
                int successCount = 0;
                HostManager &hostManager = HostManager::getInstance();

                for (int i = 0; i < pressCount; ++i) {
                    keyboardTokens.append(biosKey);

                    // Synchronous key press and release
                    hostManager.handleKeyboardAction(biosKeyCode, biosModifiers, true);
                    QThread::msleep(50); // Hold key for 50ms
                    hostManager.handleKeyboardAction(biosKeyCode, biosModifiers, false);

                    successCount++;

                    // Wait for the interval before the next press (except after the last one)
                    if (i < pressCount - 1) {
                        QThread::msleep(intervalMs);
                    }
                }

                summaries.append(QString("reboot_to_bios: success - bios_key='%1', pressed %2 times (delay=%3ms, interval=%4ms)")
                    .arg(biosKey).arg(successCount).arg(delayBeforePressMs).arg(intervalMs));
                qCDebug(log_ai_chat) << "AI Tool executed: reboot_to_bios - bios_key=" << biosKey
                                     << "pressed_count=" << successCount
                                     << "delay_before_press_ms=" << delayBeforePressMs
                                     << "interval_ms=" << intervalMs;
            }

        } else if (toolName == "detect_cursor" || toolName == "terminal_idle" ||
                   toolName == "check_terminal" || toolName == "wait_for_prompt") {
            // Detect whether the terminal is idle/waiting for input
            hasNonKeyboardTool = true;

            QJsonObject argsObj = QJsonObject::fromVariantMap(call.args);
            QJsonObject result = SharedToolExecutor::instance().detectCursor(argsObj);
            if (result.contains("error")) {
                summaries.append(QString("detect_cursor: failed (%1)").arg(result["error"].toString()));
                qCWarning(log_ai_chat) << "AI Tool failed: detect_cursor -" << result["error"].toString();
            } else {
                summaries.append(QString("detect_cursor: status=%1 confidence=%2 description=%3")
                    .arg(result["status"].toString())
                    .arg(result["confidence"].toDouble(), 0, 'f', 2)
                    .arg(result["description"].toString()));
                qCDebug(log_ai_chat) << "AI Tool executed: detect_cursor ->"
                                     << result["status"].toString() << "conf:" << result["confidence"].toDouble();
            }

        } else if (toolName == "run_command_and_wait" || toolName == "exec_and_wait" ||
                   toolName == "type_and_wait" || toolName == "command_and_wait") {
            // Type a command and wait for terminal to become idle
            hasNonKeyboardTool = true;

            QString command = call.args.value("command").toString().trimmed();
            if (command.isEmpty()) {
                summaries.append("run_command_and_wait: failed (missing 'command' argument)");
                qCWarning(log_ai_chat) << "AI Tool failed: run_command_and_wait - no command";
            } else {
                // Track typed command for AI context
                QString typedCommand = command;
                if (!typedCommand.endsWith('\n') && !typedCommand.endsWith('\r')) {
                    typedCommand += '\n';
                }
                keyboardTokens.append(typedCommand);

                QJsonObject argsObj = QJsonObject::fromVariantMap(call.args);
                QJsonObject result = SharedToolExecutor::instance().runCommandAndWait(argsObj);
                if (result.contains("error")) {
                    summaries.append(QString("run_command_and_wait: failed (%1)").arg(result["error"].toString()));
                    qCWarning(log_ai_chat) << "AI Tool failed: run_command_and_wait -" << result["error"].toString();
                } else {
                    bool success = result["success"].toBool();
                    summaries.append(QString("run_command_and_wait: %1 status=%2 wait=%3ms polls=%4 sawOutput=%5 conf=%6")
                        .arg(success ? "success" : "timeout")
                        .arg(result["status"].toString())
                        .arg(result["wait_time_ms"].toInt())
                        .arg(result["poll_count"].toInt())
                        .arg(result["saw_output"].toBool())
                        .arg(result.value("last_confidence").toDouble(), 0, 'f', 2));
                    qCDebug(log_ai_chat) << "AI Tool executed: run_command_and_wait ->"
                                         << (success ? "success" : "timeout")
                                         << "wait:" << result["wait_time_ms"].toInt() << "ms";
                }
            }

        } else if (toolName == "schedule_task" || toolName == "create_scheduled_task") {
            // Schedule a task for future execution
            hasNonKeyboardTool = true;

            QString prompt = call.args.value("prompt").toString().trimmed();
            if (prompt.isEmpty()) {
                summaries.append("schedule_task: failed (missing 'prompt' argument)");
                qCWarning(log_ai_chat) << "AI Tool failed: schedule_task - no prompt";
            } else {
                int delayMinutes = call.args.value("delay_minutes").toInt();
                QString scheduledTime = call.args.value("scheduled_time").toString();
                bool recurring = call.args.value("recurring").toBool();
                QString cronExpression = call.args.value("cron").toString();
                QString parentTaskId = call.args.value("parent_task_id").toString();
                QString context = call.args.value("context").toString();

                QString taskId = ChatTaskScheduler::instance().scheduleTask(
                    prompt, delayMinutes, scheduledTime, recurring, cronExpression,
                    parentTaskId, context);

                if (taskId.isEmpty()) {
                    summaries.append("schedule_task: failed (invalid parameters)");
                    qCWarning(log_ai_chat) << "AI Tool failed: schedule_task - invalid parameters";
                } else {
                    summaries.append(QString("schedule_task: success (taskId=%1)").arg(taskId));
                    qCDebug(log_ai_chat) << "AI Tool executed: schedule_task ->" << taskId;
                }
            }

        } else if (toolName == "list_scheduled_tasks" || toolName == "get_scheduled_tasks") {
            // List all scheduled tasks
            hasNonKeyboardTool = true;

            QString statusFilter = call.args.value("status").toString();
            QList<ScheduledTask> tasks = ChatTaskScheduler::instance().listTasks(statusFilter);

            if (tasks.isEmpty()) {
                summaries.append("list_scheduled_tasks: no tasks found");
                qCDebug(log_ai_chat) << "AI Tool executed: list_scheduled_tasks -> no tasks";
            } else {
                QStringList taskList;
                for (const ScheduledTask &task : tasks) {
                    taskList.append(QString("  - %1: %2 [%3] at %4")
                        .arg(task.taskId.left(8))
                        .arg(task.prompt.left(50))
                        .arg(task.status)
                        .arg(task.scheduledTime.toString("yyyy-MM-dd HH:mm")));
                }
                summaries.append(QString("list_scheduled_tasks: found %1 tasks\n%2")
                    .arg(tasks.size())
                    .arg(taskList.join("\n")));
                qCDebug(log_ai_chat) << "AI Tool executed: list_scheduled_tasks ->"
                                     << tasks.size() << "tasks";
            }

        } else if (toolName == "cancel_scheduled_task" || toolName == "remove_scheduled_task") {
            // Cancel a scheduled task
            hasNonKeyboardTool = true;

            QString taskId = call.args.value("task_id").toString().trimmed();
            if (taskId.isEmpty()) {
                summaries.append("cancel_scheduled_task: failed (missing 'task_id' argument)");
                qCWarning(log_ai_chat) << "AI Tool failed: cancel_scheduled_task - no task_id";
            } else {
                bool success = ChatTaskScheduler::instance().cancelTask(taskId);
                if (success) {
                    summaries.append(QString("cancel_scheduled_task: success (taskId=%1)").arg(taskId));
                    qCDebug(log_ai_chat) << "AI Tool executed: cancel_scheduled_task ->" << taskId;
                } else {
                    summaries.append(QString("cancel_scheduled_task: failed (task not found or cannot be cancelled)"));
                    qCWarning(log_ai_chat) << "AI Tool failed: cancel_scheduled_task - task not found";
                }
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
// Web URL fetch
// ============================================================================

QString ChatToolExecution::fetchUrlContent(const QString &url, int maxLength) const
{
    // Validate URL
    QUrl qurl(url);
    if (!qurl.isValid() || qurl.scheme().isEmpty()) {
        return "web_fetch: error: invalid URL";
    }

    // Ensure scheme is http or https
    QString scheme = qurl.scheme().toLower();
    if (scheme != "http" && scheme != "https") {
        return "web_fetch: error: only http/https URLs are supported";
    }

    // Create QNetworkAccessManager (thread-local to avoid cross-thread issues)
    QNetworkAccessManager manager;

    QNetworkRequest request(qurl);
    request.setHeader(QNetworkRequest::UserAgentHeader, "Openterface-QT/1.0 (web_fetch tool)");
    request.setRawHeader("Accept", "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    // Timeout: 15 seconds
    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);

    QNetworkReply *reply = manager.get(request);

    // Use QEventLoop to wait synchronously
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timeoutTimer, &QTimer::timeout, reply, &QNetworkReply::abort);
    timeoutTimer.start(15000);

    loop.exec();

    if (timeoutTimer.isActive()) {
        // Timeout didn't fire, request finished before timeout
        timeoutTimer.stop();
    } else {
        // Timeout fired
        reply->deleteLater();
        return "web_fetch: error: request timed out after 15s";
    }

    // Check for errors
    if (reply->error() != QNetworkReply::NoError) {
        QString errorString = reply->errorString();
        int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        reply->deleteLater();
        if (httpStatus > 0) {
            return QString("web_fetch: error: HTTP %1").arg(httpStatus);
        }
        return QString("web_fetch: error: %1").arg(errorString);
    }

    // Get HTTP status
    int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (httpStatus < 200 || httpStatus >= 300) {
        reply->deleteLater();
        return QString("web_fetch: error: HTTP %1").arg(httpStatus);
    }

    // Read response body
    QByteArray responseBody = reply->readAll();
    reply->deleteLater();

    // Determine content type
    QString contentType = QString::fromUtf8(reply->rawHeader("Content-Type"));

    // Convert to QString
    QString text = QString::fromUtf8(responseBody);

    // If it's HTML, strip tags
    if (contentType.contains("text/html", Qt::CaseInsensitive) || text.trimmed().startsWith("<!DOCTYPE html", Qt::CaseInsensitive) || text.trimmed().startsWith("<html", Qt::CaseInsensitive)) {
        // Remove <script> and <style> blocks entirely
        text.remove(QRegularExpression("<script[^>]*>.*?</script>", QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption));
        text.remove(QRegularExpression("<style[^>]*>.*?</style>", QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption));

        // Remove all HTML tags
        text.remove(QRegularExpression("<[^>]*>"));

        // Decode common HTML entities
        text.replace("&amp;", "&");
        text.replace("&lt;", "<");
        text.replace("&gt;", ">");
        text.replace("&quot;", "\"");
        text.replace("&apos;", "'");
        text.replace("&nbsp;", " ");
    }

    // Collapse whitespace: multiple spaces/newlines → single space
    text = text.simplified(); // This collapses all whitespace to single space and trims

    // Truncate if needed
    if (text.length() > maxLength) {
        text = text.left(maxLength) + "\n[content truncated at " + QString::number(maxLength) + " chars]";
    }

    if (text.isEmpty()) {
        return "web_fetch: [fetched] (empty content)";
    }

    return QString("web_fetch: [fetched] %1").arg(text);
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
