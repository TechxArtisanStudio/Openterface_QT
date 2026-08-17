#include "ChatToolExecution.h"
#include "ChatInputRouter.h"
#include "ChatScreenCapture.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QLoggingCategory>
#include <QProcess>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

Q_DECLARE_LOGGING_CATEGORY(log_ai_chat)
Q_LOGGING_CATEGORY(log_ai_chat, "openterface.ai.chat")

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

    // Extract JSON object from the text
    int start = trimmed.indexOf('{');
    int end = trimmed.lastIndexOf('}');
    if (start < 0 || end < 0 || end <= start) return {};

    QString candidate = trimmed.mid(start, end - start + 1);

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(candidate.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        return {};
    }

    QJsonObject root = doc.object();
    QList<AgentToolCall> calls;

    // Try "tool_calls" array first
    if (root.contains("tool_calls") && root["tool_calls"].isArray()) {
        QJsonArray arr = root["tool_calls"].toArray();
        for (const auto &val : arr) {
            QJsonObject callObj = val.toObject();
            QString tool = callObj["tool"].toString();
            if (tool.isEmpty()) continue;

            AgentToolCall call;
            call.tool = tool;
            // All other keys are args
            for (auto it = callObj.begin(); it != callObj.end(); ++it) {
                if (it.key() != "tool") {
                    call.args[it.key()] = it.value().toVariant();
                }
            }
            calls.append(call);
        }
        return calls;
    }

    // Try single "tool" object
    if (root.contains("tool")) {
        QString tool = root["tool"].toString();
        if (!tool.isEmpty()) {
            AgentToolCall call;
            call.tool = tool;
            for (auto it = root.begin(); it != root.end(); ++it) {
                if (it.key() != "tool") {
                    call.args[it.key()] = it.value().toVariant();
                }
            }
            calls.append(call);
            return calls;
        }
    }

    return {};
}

// ============================================================================
// Tool dispatch
// ============================================================================

AgentToolExecutionResult ChatToolExecution::executeToolCalls(const QList<AgentToolCall> &calls)
{
    QStringList summaries;
    QString attachmentPath;
    QStringList keyboardTokens;
    bool hasNonKeyboardTool = false;

    ChatInputRouter &router = ChatInputRouter::instance();
    ChatScreenCapture &screenCapture = ChatScreenCapture::instance();

    for (const auto &call : calls) {
        QString toolName = call.tool.toLower();

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
                // Check if it looks like a key sequence (contains < and >)
                bool looksLikeKeySequence = text.contains('<') && text.contains('>');
                if (looksLikeKeySequence) {
                    // Redirect to press_key
                    router.sendShortcut(text);
                    summaries.append(QString("type_text(redirected to press_key): success (keys=\"%1\")").arg(text));
                    qCDebug(log_ai_chat) << "AI Tool type_text redirected to press_key: keys=" << text;
                } else {
                    router.sendText(text);
                    summaries.append(QString("type_text: success (chars=%1, text=\"%2\")")
                        .arg(text.length()).arg(text));
                    qCDebug(log_ai_chat) << "AI Tool executed: type_text chars=" << text.length();
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

        } else {
            hasNonKeyboardTool = true;
            summaries.append(QString("%1: unsupported").arg(toolName));
            qCWarning(log_ai_chat) << "AI Tool unsupported:" << toolName;
        }
    }

    QString macroData;
    if (!hasNonKeyboardTool && !keyboardTokens.isEmpty()) {
        macroData = keyboardTokens.join(QString());
    }

    return AgentToolExecutionResult(summaries.join("\n"), attachmentPath, macroData);
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
