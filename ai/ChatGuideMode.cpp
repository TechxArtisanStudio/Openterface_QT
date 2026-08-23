#include "ChatGuideMode.h"
#include "ChatInputRouter.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QThread>

Q_DECLARE_LOGGING_CATEGORY(log_ai_chat)

ChatGuideMode::ChatGuideMode(QObject *parent)
    : QObject(parent)
{
}

ChatGuideMode &ChatGuideMode::instance()
{
    static ChatGuideMode inst;
    return inst;
}

// ============================================================================
// Parse guide response
// ============================================================================

ChatGuideMode::GuideResponse ChatGuideMode::parseGuideResponse(const QString &responseText) const
{
    GuideResponse result;

    // Try to extract JSON
    QString trimmed = responseText.trimmed();
    int start = trimmed.indexOf('{');
    int end = trimmed.lastIndexOf('}');

    if (start >= 0 && end > start) {
        QString candidate = trimmed.mid(start, end - start + 1);
        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(candidate.toUtf8(), &parseError);

        if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
            QJsonObject obj = doc.object();
            result.nextStep = obj["next_step"].toString().trimmed();
            result.tool = obj["tool"].toString().trimmed();
            result.toolInput = obj["tool_input"].toString().trimmed();
            result.shortcut = obj["shortcut"].toString().trimmed();

            if (obj.contains("target_box")) {
                QJsonObject box = obj["target_box"].toObject();
                double x = box["x"].toDouble();
                double y = box["y"].toDouble();
                double w = box["width"].toDouble();
                double h = box["height"].toDouble();
                result.targetBox = QRectF(x, y, w, h);
            }

            // Detect completion
            QString status = obj["status"].toString().trimmed().toLower();
            if (status == "completed" || status == "done") {
                result.isComplete = true;
            }
        }
    }

    // Also check for completion in plain text
    if (result.nextStep.isEmpty()) {
        result.nextStep = responseText.trimmed();
    }
    result.isComplete = result.isComplete || isGuideCompletionText(result.nextStep);

    return result;
}

// ============================================================================
// Execute guide action
// ============================================================================

void ChatGuideMode::executeGuideAction(
    const GuideResponse &guide, const QString &messageContent, bool autoNext)
{
    QString actionDescription = "unknown";
    ChatInputRouter &router = ChatInputRouter::instance();

    if (!guide.shortcut.isEmpty()) {
        qCDebug(log_ai_chat) << "Guide Action Preparing: executing input sequence" << guide.shortcut;
        bool success = executeGuideInputSequence(guide.shortcut);
        actionDescription = QString("input sequence %1 (Success: %2)").arg(guide.shortcut).arg(success);
        qCDebug(log_ai_chat) << "Guide Action Executed:" << actionDescription;

    } else if (!guide.targetBox.isNull()) {
        double cx = guide.targetBox.center().x();
        double cy = guide.targetBox.center().y();

        QString normalizedTool = guide.tool.trimmed().toLower();
        QString contentLower = messageContent.toLower();
        bool isRightClick = normalizedTool == "right_click" || normalizedTool == "right-click"
            || contentLower.contains("right click") || contentLower.contains("right-click");
        bool isDoubleClick = !isRightClick && (normalizedTool == "double_click" || normalizedTool == "double-click"
            || contentLower.contains("double click") || contentLower.contains("double-click"));

        int button = isRightClick ? 0x02 : 0x01;
        QString actionName = isRightClick ? "right_click" : (isDoubleClick ? "double_click" : "left_click");

        int absX = clampCoord(static_cast<int>(cx * 4096.0));
        int absY = clampCoord(static_cast<int>(cy * 4096.0));

        router.setTrackedMousePos(absX, absY);
        qCDebug(log_ai_chat) << "Guide Action Preparing:" << actionName
                             << "at normalized(" << cx << "," << cy << ") -> clamped(" << absX << "," << absY << ")";
        router.animatedClick(button, absX, absY, isDoubleClick);

        actionDescription = QString("%1 at x=%2, y=%3").arg(actionName).arg(absX).arg(absY);
        qCDebug(log_ai_chat) << "Guide Action Executed:" << actionDescription;
    }

    emit guideOverlayCleared();
    emit guideActionExecuted(actionDescription);

    if (autoNext) {
        // Set auto-next thinking status
        setAutoNextStatus(GuideAutoNextStatus(GuideAutoNextStatus::Thinking,
            QString("Action executed: %1. Auto-guiding the next step...").arg(actionDescription)));

        // After a delay, request the next guide step
        QThread::msleep(2000);
        emit guideAutoNextMessageRequested("Guide me to the next action on the current screen.");
    }
}

// ============================================================================
// Guide completion detection
// ============================================================================

bool ChatGuideMode::isGuideCompletionText(const QString &text) const
{
    QString normalized = text.trimmed().toLower();
    if (normalized.startsWith("result:")) return true;

    static const QStringList completionPhrases = {
        "goal achieved",
        "task complete",
        "task completed",
        "already open and loaded",
        "already completed",
        "is already open",
        "is already loaded"
    };

    for (const auto &phrase : completionPhrases) {
        if (normalized.contains(phrase)) return true;
    }
    return false;
}

// ============================================================================
// Complete step and advance
// ============================================================================

void ChatGuideMode::completeGuideStepAndNext(const QString &stepDescription)
{
    QString firstLine = stepDescription.split('\n', Qt::SkipEmptyParts).value(0).trimmed();
    QString resultLine = firstLine.isEmpty()
        ? "Result: I completed this guide step."
        : QString("Result: I completed this step: %1").arg(firstLine);

    qCDebug(log_ai_chat) << "Guide Action User-Completed:" << resultLine;
    emit guideOverlayCleared();
    emit guideAutoNextMessageRequested(resultLine + "\nGuide me to the next action on the current screen.");
}

// ============================================================================
// Input sequence execution
// ============================================================================

bool ChatGuideMode::executeGuideInputSequence(const QString &inputSequence)
{
    QString normalized = inputSequence.trimmed();
    if (normalized.isEmpty()) return false;

    // Check for bracketed input like <ctrl>+<c>
    if (normalized.contains('<') && normalized.contains('>')) {
        return executeBracketedGuideInputSequence(normalized);
    }

    // Plain comma-separated steps
    QStringList steps = normalized.split(',', Qt::SkipEmptyParts);
    for (auto &s : steps) s = s.trimmed();
    steps.removeAll(QString());

    if (steps.isEmpty()) steps.append(normalized);

    bool executedAny = false;
    ChatInputRouter &router = ChatInputRouter::instance();

    for (int i = 0; i < steps.size(); ++i) {
        const QString &step = steps[i];
        if (executeShortcut(step)) {
            executedAny = true;
            double delay = guideDelayAfterStep({Shortcut, step},
                i + 1 < steps.size() ? nullptr : nullptr);
            QThread::msleep(static_cast<int>(delay * 1000));
            continue;
        }

        qCDebug(log_ai_chat) << "AI Executing Text Input:" << step;
        router.sendText(step);
        executedAny = true;
        QThread::msleep(160);
    }

    return executedAny;
}

bool ChatGuideMode::executeBracketedGuideInputSequence(const QString &input)
{
    QList<GuideInputStep> steps = parseBracketedGuideInputSteps(input);
    if (steps.isEmpty()) return false;

    bool executedAny = false;
    ChatInputRouter &router = ChatInputRouter::instance();

    for (int i = 0; i < steps.size(); ++i) {
        const auto &step = steps[i];
        if (step.type == Shortcut) {
            if (executeShortcut(step.value)) executedAny = true;
        } else {
            QString trimmed = step.value.trimmed();
            if (trimmed.isEmpty()) continue;
            qCDebug(log_ai_chat) << "AI Executing Text Input:" << trimmed;
            router.sendText(trimmed);
            executedAny = true;
        }

        double delay = guideDelayAfterStep(step, i + 1 < steps.size() ? &steps[i + 1] : nullptr);
        QThread::msleep(static_cast<int>(delay * 1000));
    }

    return executedAny;
}

// ============================================================================
// Shortcut dispatch
// ============================================================================

bool ChatGuideMode::executeShortcut(const QString &shortcut)
{
    ChatInputRouter &router = ChatInputRouter::instance();
    router.sendShortcut(shortcut);
    return true;
}

// ============================================================================
// Step delay heuristics
// ============================================================================

double ChatGuideMode::guideDelayAfterStep(const GuideInputStep &step, const GuideInputStep *nextStep) const
{
    if (step.type == Shortcut) {
        if (isGuideLauncherShortcut(step.value)) return 0.65;
        if (isGuideNavigationShortcut(step.value)) return 0.22;
        return 0.12;
    }

    // Text step
    QString trimmed = step.value.trimmed();
    if (trimmed.isEmpty()) return 0.12;
    if (nextStep && nextStep->type == Shortcut) {
        QString n = nextStep->value.trimmed().toLower();
        if (n == "enter" || n == "return" || n == "tab") return 0.3;
    }
    return 0.16;
}

// ============================================================================
// Shortcut classification
// ============================================================================

bool ChatGuideMode::isGuideLauncherShortcut(const QString &shortcut) const
{
    QString normalized = shortcut.trimmed().toLower();
    static const QSet<QString> launcherShortcuts = {
        "cmd+space", "cmd+h", "cmd+tab", "ctrl+alt+t", "win+r", "win+e"
    };
    return launcherShortcuts.contains(normalized);
}

bool ChatGuideMode::isGuideNavigationShortcut(const QString &shortcut) const
{
    QString normalized = shortcut.trimmed().toLower();
    static const QSet<QString> navigationShortcuts = {
        "enter", "return", "tab", "shift+tab",
        "up", "down", "left", "right",
        "esc", "escape"
    };
    return navigationShortcuts.contains(normalized);
}

bool ChatGuideMode::looksLikeGuideShortcut(const QString &step) const
{
    QString normalized = step.trimmed().toLower();
    if (normalized.isEmpty()) return false;
    if (normalized.contains('+')) return true;
    return isGuideNavigationShortcut(normalized) || isGuideLauncherShortcut(normalized);
}

// ============================================================================
// Bracketed input parsing
// ============================================================================

QList<ChatGuideMode::GuideInputStep> ChatGuideMode::parseBracketedGuideInputSteps(const QString &input) const
{
    QList<GuideInputStep> steps;
    QString textBuffer;
    QStringList pendingModifiers;

    auto flushTextBuffer = [&]() {
        if (!textBuffer.isEmpty()) {
            steps.append({Text, textBuffer});
        }
        textBuffer.clear();
    };

    auto appendShortcut = [&](const QString &keyToken) {
        QString key = keyToken.trimmed().toLower();
        if (key.isEmpty()) return;
        QStringList comboTokens = pendingModifiers;
        comboTokens.append(key);
        steps.append({Shortcut, comboTokens.join('+')});
        pendingModifiers.clear();
    };

    int i = 0;
    while (i < input.length()) {
        if (input[i] == '<') {
            int close = input.indexOf('>', i + 1);
            if (close < 0) {
                textBuffer.append(input[i]);
                ++i;
                continue;
            }

            QString rawTag = input.mid(i + 1, close - i - 1).trimmed();

            if (rawTag.startsWith('/')) {
                // Closing tag
                QString closingToken = normalizeBracketedKeyToken(rawTag.mid(1));
                if (isModifierToken(closingToken)) {
                    pendingModifiers.removeAll(closingToken);
                }
            } else {
                flushTextBuffer();
                QString normalizedTag = normalizeBracketedKeyToken(rawTag);
                if (!normalizedTag.isEmpty()) {
                    if (isModifierToken(normalizedTag)) {
                        pendingModifiers.append(normalizedTag);
                    } else {
                        appendShortcut(normalizedTag);
                    }
                }
            }

            i = close + 1;
        } else {
            QChar ch = input[i];
            if (!pendingModifiers.isEmpty() && !ch.isSpace() && ch.isLetterOrNumber()) {
                appendShortcut(QString(ch));
            } else {
                textBuffer.append(ch);
            }
            ++i;
        }
    }

    flushTextBuffer();
    return steps;
}

bool ChatGuideMode::isModifierToken(const QString &token) const
{
    return token == "ctrl" || token == "alt" || token == "shift" || token == "cmd"
        || token == "control" || token == "command" || token == "win" || token == "super";
}

QString ChatGuideMode::normalizeBracketedKeyToken(const QString &token) const
{
    QString normalized = token.trimmed().toLower();
    if (normalized == "del") return "delete";
    if (normalized == "control") return "ctrl";
    if (normalized == "command" || normalized == "meta" || normalized == "super"
        || normalized == "windows" || normalized == "win") return "cmd";
    if (normalized == "option") return "alt";
    if (normalized == "return") return "enter";
    return normalized;
}

// ============================================================================
// Auto-next status
// ============================================================================

void ChatGuideMode::setAutoNextStatus(const GuideAutoNextStatus &status)
{
    m_autoNextStatus = status;
    emit autoNextStatusChanged(status);
}

int ChatGuideMode::clampCoord(int value)
{
    return qBound(0, value, 4096);
}
