#include "ChatManager.h"
#include "ChatApiClient.h"
#include "ChatScreenCapture.h"
#include "ChatInputRouter.h"
#include "ChatConversationBuilder.h"
#include "ChatToolExecution.h"
#include "ChatPersistence.h"
#include "ChatTracing.h"
#include "ChatGuideMode.h"
#include "ChatSkillManager.h"
#include "ui/globalsetting.h"
#include <QLoggingCategory>
#include <QtConcurrent>
#include <QEventLoop>
#include <QThread>
#include <QTimer>
#include <QRegularExpression>

Q_DECLARE_LOGGING_CATEGORY(log_ai_chat)

// Synchronous wrapper around the callback-based API client.
// Uses a QTimer to quit the event loop after a timeout so the UI never hangs
// when the API server is slow or unresponsive.
//
// LIFETIME SAFETY: The callback fires on the main thread (QNetworkReply's
// thread), but sendCompletionSync runs on the worker thread. If the 120s
// timeout fires first, the worker thread's stack is destroyed when
// sendCompletionSync returns — but the callback can still fire later when
// the reply finishes. Without protection, the callback would write to dead
// stack variables (use-after-free → crash/hang).
//
// Fix: two shared flags coordinate the two handlers:
//   - completed: set by the callback when it fires first (success/error path)
//   - aborted:   set by the timeout when IT fires first (timeout path)
// If the callback sees aborted==true, it knows the stack is gone and skips
// all work. If the timeout sees completed==true, it skips (already handled).
static ChatCompletionResult sendCompletionSync(
    const QUrl &baseURL, const QString &model, const QString &apiKey,
    const QList<ChatApiMessage> &messages, QString &outError)
{
    ChatCompletionResult result;
    QEventLoop loop;
    // Shared between the callback (main thread) and timeout (worker thread).
    // QSharedPointer keeps the bools alive even after sendCompletionSync returns.
    auto completed = QSharedPointer<bool>::create(false);
    auto aborted   = QSharedPointer<bool>::create(false);

    ChatApiClient::instance().sendCompletion(baseURL, model, apiKey, messages, std::nullopt,
        [&, completed, aborted](bool success, const ChatCompletionResult &r, const QString &error) {
            // If the timeout already fired and sendCompletionSync has returned,
            // our stack variables (result, outError, loop) are gone — do nothing.
            if (*aborted) return;

            if (success) {
                result = r;
            } else {
                outError = error;
            }
            *completed = true;
            loop.quit();
        });

    // 120s timeout: generous for LLM inference, but prevents the UI from
    // hanging indefinitely if the API server stalls or the network drops.
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, [&, completed, aborted]() {
        // If the callback already fired, the result is set — nothing to do.
        if (*completed) return;

        qCWarning(log_ai_chat) << "AI Chat API request timed out after 120s (model=" << model << ")";
        outError = "AI request timed out (120s). The API server may be overloaded.";
        *aborted = true;
        loop.quit();
    });
    timeout.start(120000);

    loop.exec();
    return result;
}

ChatManager::ChatManager(QObject *parent)
    : QObject(parent)
    , m_plannerAgent(GlobalSetting::instance().getChatAgentMaxIterations())
{
    // IMPORTANT: Ensure ChatApiClient singleton is initialized on the main thread.
    // If it's first accessed from a worker thread (via QtConcurrent::run in
    // performStandardSend), the QNetworkAccessManager inside it gets created on
    // that thread, causing "Cannot create children for a parent that is in a
    // different thread" errors on subsequent accesses. By referencing it here
    // in the constructor (which runs on the main thread), we guarantee the
    // network manager lives on the main thread.
    (void)ChatApiClient::instance();

    loadHistory();

    // Connect guide mode signals
    ChatGuideMode *guideMode = &ChatGuideMode::instance();
    connect(guideMode, &ChatGuideMode::guideOverlayRequested,
            this, &ChatManager::guideOverlayRequested);
    connect(guideMode, &ChatGuideMode::guideOverlayCleared,
            this, &ChatManager::guideOverlayCleared);
    connect(guideMode, &ChatGuideMode::guideAutoNextMessageRequested,
            this, [this](const QString &msg) {
        sendMessage(msg);
    });
}

ChatManager &ChatManager::instance()
{
    static ChatManager inst;
    return inst;
}

// ============================================================================
// Configuration
// ============================================================================

ChatAPIConfiguration ChatManager::currentChatAPIConfiguration() const
{
    GlobalSetting &gs = GlobalSetting::instance();
    QString baseURLString = gs.getChatApiBaseURL().trimmed();
    QString model = gs.getChatModel().trimmed();
    QString apiKey = gs.getChatApiKey().trimmed();

    if (baseURLString.isEmpty() || model.isEmpty() || apiKey.isEmpty()) {
        return ChatAPIConfiguration();
    }

    QUrl baseURL(baseURLString);
    if (!baseURL.isValid()) return ChatAPIConfiguration();

    return ChatAPIConfiguration{baseURL, model, apiKey};
}

// ============================================================================
// Actions
// ============================================================================

void ChatManager::sendMessage(const QString &text, const QString &attachmentFilePath)
{
    QString trimmed = text.trimmed();
    if (trimmed.isEmpty() && attachmentFilePath.isEmpty()) return;
    if (m_isSending) return;

    // Handle special "stop" command from max iterations prompt
    if (trimmed.toLower() == "stop") {
        ChatMessage userMsg(ChatRole::User, trimmed);
        userMsg.isStatusHint = true;  // Make it subtle
        m_messages.append(userMsg);
        emit messageAppended(userMsg);

        // Show a status hint confirming the stop
        ChatMessage stopMsg(ChatRole::Assistant, "⏹ Agent stopped by user.");
        stopMsg.isStatusHint = true;
        m_messages.append(stopMsg);
        emit messageAppended(stopMsg);
        persistHistory();
        return;
    }

    // Handle special "continue" command from max iterations prompt
    // This triggers a new agent run with a nudge to continue
    bool isContinueCommand = (trimmed.toLower() == "continue");

    m_lastError.clear();
    emit lastErrorChanged(m_lastError);

    QString storedContent = trimmed.isEmpty() ? "Attached screenshot" : trimmed;

    ChatMessage msg(ChatRole::User, storedContent, attachmentFilePath);
    // Note: We do NOT mark this as statusHint - we want the agent to see
    // the user's explicit "continue" request in the conversation history.
    m_messages.append(msg);
    int userMessageIndex = m_messages.size() - 1;  // Remember the user message index

    // If this is a continue command, add a system message to nudge the agent
    if (isContinueCommand) {
        // Show a status hint confirming the continue (UI only)
        ChatMessage continueMsg(ChatRole::Assistant, "▶ Continuing agent execution...");
        continueMsg.isStatusHint = true;
        m_messages.append(continueMsg);
        emit messageAppended(continueMsg);

        // Build a comprehensive context summary for the agent
        QString nudge = "The user has asked you to continue. You previously reached the maximum iteration limit. "
                       "Please continue working on the original task.\n\n"
                       "IMPORTANT CONTEXT:\n"
                       "- Review the conversation history above to understand what you were working on\n"
                       "- The current screenshot shows the latest state of the target screen\n"
                       "- Continue issuing tool calls to make progress on the task\n"
                       "- If you're unsure what to do next, summarize what you've accomplished so far and ask for clarification";
        ChatMessage nudgeMsg(ChatRole::System, nudge);
        m_messages.append(nudgeMsg);
        emit messageAppended(nudgeMsg);
    }

    // IMPORTANT: Capture screen on the MAIN thread before spawning the worker.
    // The GStreamer backend's getLatestOriginalFrame() is NOT thread-safe
    // (it touches GStreamer pipeline objects that must live on the main thread).
    // Running captureScreen() from QtConcurrent::run causes a segfault.
    // Guide mode captures inside performGuideSend which is also called on the
    // worker, so we also need to handle that — but guide mode is typically
    // short-lived and the crash was observed in agent/planner modes.
    GlobalSetting &gs = GlobalSetting::instance();
    bool needsAutoCapture =
        attachmentFilePath.isEmpty() &&
        (gs.getChatAgenticModeEnabled() ||
         gs.getChatPlannerModeEnabled() ||
         gs.getChatGuideModeEnabled());

    if (needsAutoCapture) {
        ChatScreenCapture &sc = ChatScreenCapture::instance();
        QString capturedPath = sc.captureScreen();
        if (!capturedPath.isEmpty()) {
            // Attach to the user message (not m_messages.last() which might be a nudge)
            m_messages[userMessageIndex].attachmentFilePath = capturedPath;
            qCDebug(log_ai_chat) << "sendMessage: auto-captured screen on main thread:"
                                 << capturedPath;
        } else {
            qCWarning(log_ai_chat) << "sendMessage: auto-capture returned empty";
        }
    }

    if (GlobalSetting::instance().getChatAgenticModeEnabled()) {
        startAgentRequestStatus(msg.id);
    }

    persistHistory();
    m_isSending = true;
    emit sendingStateChanged(true);
    emit messageAppended(msg);

    // Run send in background thread
    (void)QtConcurrent::run([this]() {
        performSend();
    });
}

void ChatManager::cancelSending()
{
    m_cancelRequested = true;
    ChatApiClient::instance().cancelAll();
    m_isSending = false;
    emit sendingStateChanged(false);
}

void ChatManager::clearHistory()
{
    cancelSending();
    m_messages.clear();
    m_hasPlan = false;
    m_currentPlan = ChatExecutionPlan();
    m_plannerTraceEntries.clear();
    m_guideAutoNextStatuses.clear();
    m_agentRequestStatuses.clear();
    clearGuideOverlay();
    persistHistory();
    emit messagesChanged();
    emit planChanged();
    emit plannerTracesChanged();
}

void ChatManager::approveCurrentPlan()
{
    if (!m_hasPlan || m_currentPlan.status != ChatPlanStatus::AwaitingApproval) return;

    m_currentPlan.status = ChatPlanStatus::Approved;
    for (int i = 0; i < m_currentPlan.tasks.size(); ++i) {
        m_currentPlan.tasks[i].status = ChatTaskStatus::Approved;
    }
    m_lastError.clear();
    emit lastErrorChanged(m_lastError);
    m_isSending = true;
    emit sendingStateChanged(true);
    persistHistory();
    emit planChanged();

    (void)QtConcurrent::run([this]() {
        executeApprovedPlan();
    });
}

void ChatManager::clearCurrentPlan()
{
    cancelSending();
    m_hasPlan = false;
    m_currentPlan = ChatExecutionPlan();
    m_plannerTraceEntries.clear();
    persistHistory();
    emit planChanged();
    emit plannerTracesChanged();
}

void ChatManager::sendQuickReply(const ChatQuickReply &reply)
{
    sendMessage(reply.sendText);
}

void ChatManager::runSkill(const ChatSkill &skill)
{
    if (m_isSending) return;

    if (skill.captureScreen) {
        QString screenshotPath = ChatScreenCapture::instance().captureScreen();
        if (screenshotPath.isEmpty()) {
            presentAIError("Could not capture screenshot from the target device.");
            return;
        }
        sendMessage(skill.prompt, screenshotPath);
    } else {
        sendMessage(skill.prompt);
    }
}

void ChatManager::rerunLastPrompt(bool clearSequenceHistory)
{
    if (m_isSending) return;

    // Find last user message (not a tool result)
    ChatMessage lastPrompt;
    for (int i = m_messages.size() - 1; i >= 0; --i) {
        if (m_messages[i].role == ChatRole::User &&
            !m_messages[i].content.startsWith("TOOL_RESULT:")) {
            lastPrompt = m_messages[i];
            break;
        }
    }

    QString promptContent = lastPrompt.content.trimmed();
    QString replayText;
    if (!promptContent.isEmpty() && promptContent != "Attached screenshot") {
        replayText = promptContent;
    } else if (m_hasPlan && !m_currentPlan.goal.trimmed().isEmpty()) {
        replayText = m_currentPlan.goal.trimmed();
    }

    if (replayText.isEmpty() && lastPrompt.attachmentFilePath.isEmpty()) return;

    if (clearSequenceHistory) {
        m_messages.clear();
        m_hasPlan = false;
        m_currentPlan = ChatExecutionPlan();
        m_lastError.clear();
        m_plannerTraceEntries.clear();
        m_guideAutoNextStatuses.clear();
        m_agentRequestStatuses.clear();
        emit messagesChanged();
        emit planChanged();
    }

    sendMessage(replayText, lastPrompt.attachmentFilePath);
}

// ============================================================================
// Guide mode
// ============================================================================

void ChatManager::executeGuideAction(const ChatMessage &message, bool autoNext)
{
    ChatGuideMode::GuideResponse guide;
    guide.targetBox = message.guideActionRect;
    guide.shortcut = message.guideShortcut;
    guide.tool = message.guideTool;

    ChatGuideMode::instance().executeGuideAction(guide, message.content, autoNext);
}

void ChatManager::completeGuideStepAndNext(const QString &stepDescription)
{
    ChatGuideMode::instance().completeGuideStepAndNext(stepDescription);
}
GuideAutoNextStatus ChatManager::guideAutoNextStatus(const QUuid &messageID) const
{
    return m_guideAutoNextStatuses.value(messageID);
}

// ============================================================================
// Send paths
// ============================================================================

void ChatManager::performSend()
{
    m_cancelRequested = false;

    ChatAPIConfiguration config = currentChatAPIConfiguration();
    if (config.baseURL.isEmpty() || config.model.isEmpty() || config.apiKey.isEmpty()) {
        if (config.baseURL.isEmpty()) {
            presentAIError("Invalid Chat API base URL");
        } else if (config.model.isEmpty()) {
            presentAIError("Chat model is empty");
        } else {
            presentAIError("Missing AI API key in Settings");
        }
        m_isSending = false;
        emit sendingStateChanged(false);
        return;
    }

    GlobalSetting &gs = GlobalSetting::instance();
    bool guideModeEnabled = gs.getChatGuideModeEnabled();
    bool plannerModeEnabled = gs.getChatPlannerModeEnabled();

    if (guideModeEnabled) {
        performGuideSend(config);
    } else if (plannerModeEnabled) {
        performPlannerSend(config);
    } else {
        bool agenticEnabled = gs.getChatAgenticModeEnabled();
        performStandardSend(config, agenticEnabled);
    }
}

void ChatManager::performStandardSend(const ChatAPIConfiguration &config, bool agenticEnabled)
{
    GlobalSetting &gs = GlobalSetting::instance();
    QString systemPrompt = gs.getChatSystemPrompt().trimmed();
    int maxIterations = gs.getChatAgentMaxIterations();

    ChatConversationBuilder &builder = ChatConversationBuilder::instance();
    ChatToolExecution &toolExec = ChatToolExecution::instance();
    ChatScreenCapture &screenCapture = ChatScreenCapture::instance();
    ChatTracing &tracing = ChatTracing::instance();

    // Get image data URL if last user message has attachment.
    // Note: if we're in agentic mode and no attachment was provided,
    // sendMessage() already auto-captured on the main thread and stored
    // the path in m_messages.last().attachmentFilePath.
    QString imageDataURL;
    for (int i = m_messages.size() - 1; i >= 0; --i) {
        if (m_messages[i].role == ChatRole::User && !m_messages[i].attachmentFilePath.isEmpty()) {
            imageDataURL = screenCapture.dataURLForImage(m_messages[i].attachmentFilePath);
            qCDebug(log_ai_chat) << "performStandardSend: using attachment for image:"
                                 << m_messages[i].attachmentFilePath;
            break;
        }
    }

    // Track whether any keyboard action has occurred across all iterations.
    // Once the agent starts typing/pressing keys (e.g. running a terminal command),
    // all subsequent screen captures should use OCR (screen_to_markdown) instead
    // of AI vision — the model is reading terminal text output, not interpreting
    // visual layout. This persists across iterations so a capture_screen call in
    // iteration N+1 is auto-converted even though the keyboard action happened
    // in iteration N.
    //
    // HOWEVER: For BIOS/TextUI modes, we should NOT auto-convert because OCR
    // cannot detect which menu item is selected (selection is shown by color).
    // These modes must use AI vision (capture_screen) exclusively.
    bool anyKeyboardActionInLoop = false;

    // Get target system to determine if we should auto-convert to OCR
    ChatTargetSystem targetSystem = chatTargetSystemFromString(
        GlobalSetting::instance().getChatTargetSystem());
    bool isTextBasedUI = (targetSystem == ChatTargetSystem::BIOS || targetSystem == ChatTargetSystem::TextUI);
    qCDebug(log_ai_chat) << "Agent loop started: targetSystem=" << chatTargetSystemToString(targetSystem)
                         << "isTextBasedUI=" << isTextBasedUI;

    // Track how many times we've nudged the model about broken XML tool calls.
    // Cap at 2 nudges total — if the model can't produce valid JSON after two
    // tries, further identical nudges just confuse it and bloat the conversation.
    int xmlNudgeCount = 0;
    static const int MAX_XML_NUDGES = 2;

    // Index of the last nudge message we added, so we can remove it before
    // adding a replacement (keeps only one nudge visible in history at a time).
    int lastNudgeIndex = -1;

    // Track whether we reached max iterations (loop completed without break)
    bool reachedMaxIterations = false;

    for (int iteration = 1; iteration <= maxIterations; ++iteration) {
        // Track if this is the last iteration
        if (iteration == maxIterations) {
            reachedMaxIterations = true;
        }

        if (m_cancelRequested) break;

        // In agentic mode, refresh the screenshot at every iteration (after
        // the first — the initial auto-capture at sendMessage() time is still
        // current for iteration 1). Without this, the model keeps seeing the
        // same screenshot across iterations even though tools may have changed
        // the target screen (e.g. opened a terminal), so it can't reason about
        // the new state.
        if (agenticEnabled && iteration > 1) {
            // Wait for the target screen to settle after the previous tool
            // actions. The POST_KEYBOARD_SETTLE_MS in tool execution covers
            // the key transmission time, but the target OS may still need
            // time to launch an app (e.g. terminal emulator starting up
            // after Ctrl+Alt+T can take 500-1000ms). 600ms here on top of
            // the 400ms post-keyboard settle gives ~1s total, enough for
            // most targets to render a new window.
            QThread::msleep(600);

            // CRITICAL: After keyboard actions, don't send the image to the API.
            // The model should use OCR (screen_to_markdown) to read terminal output,
            // not vision. If we send the image, the model will analyze it visually
            // even though we also provide OCR text. By not sending the image, we
            // force the model to rely on OCR or call screen_to_markdown explicitly.
            //
            // EXCEPTION: For BIOS/TextUI modes, ALWAYS send the image because these
            // modes must use AI vision (OCR cannot detect selection state).
            if (anyKeyboardActionInLoop && !isTextBasedUI) {
                qCDebug(log_ai_chat) << "Iteration" << iteration << ": keyboard action occurred, NOT sending image to API (forcing OCR)";
                imageDataURL.clear();
            } else {
                QString freshPath = screenCapture.captureScreen();
                if (!freshPath.isEmpty()) {
                    imageDataURL = screenCapture.dataURLForImage(freshPath);
                    qCDebug(log_ai_chat) << "performStandardSend: iteration" << iteration
                                         << "auto-captured fresh screen:" << freshPath;
                }
            }
        }

        // Update the status label with the current step. When we have a
        // screenshot attached to this iteration's API call, say "Examining
        // screen" so the user sees the AI is actively looking at the target
        // rather than just thinking about prior text.
        {
            ChatMessage *owner = nullptr;
            for (int i = m_messages.size() - 1; i >= 0; --i) {
                if (m_messages[i].role == ChatRole::User
                    && m_agentRequestStatuses.contains(m_messages[i].id)) {
                    owner = &m_messages[i];
                    break;
                }
            }
            if (owner) {
                QString statusText;
                if (!imageDataURL.isEmpty()) {
                    statusText = QString("Examining screen (%1/%2)...")
                        .arg(iteration).arg(maxIterations);
                } else {
                    statusText = QString("Thinking (%1/%2)...")
                        .arg(iteration).arg(maxIterations);
                }
                m_agentRequestStatuses[owner->id] = GuideAutoNextStatus(
                    GuideAutoNextStatus::Thinking, statusText);
                emit agentRequestStatusChanged(owner->id, m_agentRequestStatuses[owner->id]);
            }
        }

        // Append a visible step indicator to the chat so the user can see
        // each iteration's progress. The model's own response follows as a
        // separate bubble, making it clear which response was based on what
        // screen state.
        if (agenticEnabled) {
            QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss");
            QString stepText;
            if (!imageDataURL.isEmpty()) {
                stepText = QString("🔍 Step %1/%2 [%3] — examining current screen (sending image to AI)...")
                    .arg(iteration).arg(maxIterations).arg(timestamp);
            } else {
                stepText = QString("💭 Step %1/%2 [%3] — thinking...")
                    .arg(iteration).arg(maxIterations).arg(timestamp);
            }
            ChatMessage stepMsg(ChatRole::Assistant, stepText);
            stepMsg.isStatusHint = true;
            m_messages.append(stepMsg);
            emit messageAppended(stepMsg);
        }

        QList<ChatApiMessage> conversation = builder.buildConversation(
            systemPrompt, m_messages, agenticEnabled, imageDataURL);

        // Trace the request
        tracing.appendAITrace(
            QString("REQUEST iteration=%1").arg(iteration),
            tracing.readableTraceParts(conversation));

        // Record when we started waiting for the API, so we can show duration
        // in the step hint after the response arrives.
        qint64 apiStartTime = QDateTime::currentMSecsSinceEpoch();

        // Send API request
        ChatCompletionResult result;
        QString apiError;
        result = sendCompletionSync(config.baseURL, config.model, config.apiKey, conversation, apiError);

        // Update the step hint to show how long the API call took, replacing
        // the "thinking..." text with a concrete "AI processed Xs" message.
        if (agenticEnabled && !m_messages.isEmpty()) {
            // Find the last status hint we added for this iteration
            for (int i = m_messages.size() - 1; i >= 0; --i) {
                if (m_messages[i].isStatusHint) {
                    qint64 elapsedMs = QDateTime::currentMSecsSinceEpoch() - apiStartTime;
                    double elapsedSec = elapsedMs / 1000.0;
                    QString durationText;
                    if (elapsedSec < 1.0) {
                        durationText = QString("⏱ Step %1 — AI processed %2ms")
                            .arg(iteration).arg(elapsedMs);
                    } else {
                        durationText = QString("⏱ Step %1 — AI processed %2s")
                            .arg(iteration).arg(elapsedSec, 0, 'f', 1);
                    }
                    m_messages[i].content = durationText;
                    emit messageUpdated(i, m_messages[i]);
                    break;
                }
            }
        }

        if (m_cancelRequested) break;

        if (result.content.isEmpty()) {
            if (!apiError.isEmpty()) {
                presentAIError(apiError);
            } else {
                presentAIError("Empty response from AI");
            }
            break;
        }

        // Trace the response
        tracing.appendAITrace(
            QString("RESPONSE iteration=%1").arg(iteration),
            result.content.left(500));

        if (!agenticEnabled) {
            // Standard mode: just append the response
            appendAssistantMessage(result.content);
            break;
        }

        // Agentic mode: check for tool calls
        QList<AgentToolCall> toolCalls = toolExec.parseToolCalls(result.content);
        if (toolCalls.isEmpty()) {
            QString textLower = result.content.toLower();

            // Detect broken/empty XML tool call attempts: the model sometimes emits
            // empty "" tags, or a response that is entirely XML tool-call
            // markup with no real content. Detect these and nudge the model to use
            // proper JSON format instead.
            static const QString tcOpen  = QString::fromUtf8("<tool_call>");
            static const QString tcClose = QString::fromUtf8("</tool_call>");
            static const QString fnOpen  = QString::fromUtf8("<function");
            bool hasBrokenXmlToolCall = result.content.contains(tcOpen)
                                     || result.content.contains(tcClose)
                                     || (result.content.contains(fnOpen)
                                         && !result.content.contains("\"tool_calls\"")
                                         && !result.content.contains("{\"tool\""));

            // Also detect responses that are mostly XML markup with no meaningful content
            bool isMostlyXmlMarkup = false;
            if (!hasBrokenXmlToolCall) {
                QString stripped = result.content;
                stripped.remove(QRegularExpression(QString::fromUtf8(
                    "<\\s*(tool_call|function|parameter|/tool_call|/function|/parameter)\\s*[^>]*>")));
                stripped = stripped.trimmed();
                if (stripped.isEmpty() && (result.content.contains(tcOpen)
                                         || result.content.contains(fnOpen))) {
                    isMostlyXmlMarkup = true;
                }
            }

            if ((hasBrokenXmlToolCall || isMostlyXmlMarkup) && iteration < maxIterations) {
                if (xmlNudgeCount >= MAX_XML_NUDGES) {
                    // Already nudged enough — give up with a clear message.
                    // Continuing to nudge just accumulates confusion in the history.
                    qCWarning(log_ai_chat) << "Agent loop: model returned broken XML"
                                           << xmlNudgeCount << "times — giving up after"
                                           << MAX_XML_NUDGES << "nudges at iteration" << iteration;
                    ChatMessage giveUpMsg(ChatRole::Assistant,
                        QStringLiteral("(The model kept returning broken tool call format. "
                                       "Try starting a new conversation, or rephrase your request.)"));
                    m_messages.append(giveUpMsg);
                    emit messageAppended(giveUpMsg);
                    break;
                }

                xmlNudgeCount++;

                // Remove the previous nudge message (if any) to avoid piling up
                // identical complaints in the conversation history. The model sees
                // ONE clear instruction, not 5 copies of it.
                if (lastNudgeIndex >= 0 && lastNudgeIndex < m_messages.size()) {
                    m_messages.removeAt(lastNudgeIndex);
                    // Also remove the status hint that preceded it
                    if (lastNudgeIndex > 0 && lastNudgeIndex - 1 < m_messages.size()
                        && m_messages[lastNudgeIndex - 1].isStatusHint) {
                        m_messages.removeAt(lastNudgeIndex - 1);
                    }
                    lastNudgeIndex = -1;
                }

                // Don't show the broken XML to the user — add a status hint instead
                QString stepText = QString::fromUtf8(
                    "\xe2\x9a\xa0 Step %1/%2 \xe2\x80\x94 model returned broken tool call format. Retrying (%3/%4)...")
                    .arg(iteration).arg(maxIterations).arg(xmlNudgeCount).arg(MAX_XML_NUDGES);
                ChatMessage stepMsg(ChatRole::Assistant, stepText);
                stepMsg.isStatusHint = true;
                m_messages.append(stepMsg);
                emit messageAppended(stepMsg);

                QString nudge = QStringLiteral(
                    "Your last response was empty or contained broken XML tags. "
                    "Do NOT use XML tags for tool calls. "
                    "You MUST use JSON format: "
                    "{\"tool_calls\": [{\"tool\": \"tool_name\", \"arg1\": value1}]} "
                    "Issue the tool calls NOW in JSON format, or explain what you see and stop.");
                ChatMessage nudgeMsg(ChatRole::System, nudge);
                m_messages.append(nudgeMsg);
                lastNudgeIndex = m_messages.size() - 1;
                emit messageAppended(nudgeMsg);

                qCDebug(log_ai_chat) << "Agent loop: nudging model about broken XML (attempt"
                                     << xmlNudgeCount << "/" << MAX_XML_NUDGES
                                     << ") at iteration" << iteration;
                persistHistory();
                continue;
            }
            // Case 2: Model said it would continue in prose but didn't emit tool calls
            // Only detect clear future-intent phrases, not completion statements.
            // "I've opened" or "Now you can see" are completions, not continuations.
            // Also catch "Let me..." patterns that indicate the model intends to take action.
            bool wantsToContinue = textLower.contains("let me try")
                                || textLower.contains("let me check")
                                || textLower.contains("let me verify")
                                || textLower.contains("i'll now")
                                || textLower.contains("i will now")
                                || textLower.contains("next, i'll")
                                || textLower.contains("next, i will")
                                || textLower.contains("now i'll")
                                || textLower.contains("now i will")
                                || textLower.contains("let me open")
                                || textLower.contains("i'll open")
                                || textLower.contains("try again")
                                // Add more patterns for "Let me [action]"
                                || textLower.contains("let me go")
                                || textLower.contains("let me look")
                                || textLower.contains("let me find")
                                || textLower.contains("let me navigate")
                                || textLower.contains("let me press")
                                || textLower.contains("let me click")
                                || textLower.contains("let me type")
                                || textLower.contains("let me scroll")
                                || textLower.contains("let me select")
                                || textLower.contains("let me choose")
                                || textLower.contains("let me enter")
                                || textLower.contains("let me move")
                                || textLower.contains("i'll go")
                                || textLower.contains("i will go")
                                || textLower.contains("i'll look")
                                || textLower.contains("i will look")
                                || textLower.contains("i'll find")
                                || textLower.contains("i will find")
                                || textLower.contains("i'll navigate")
                                || textLower.contains("i will navigate");

            if (wantsToContinue && iteration < maxIterations) {
                appendAssistantMessage(result.content);

                QString nudge = QStringLiteral(
                    "You said you would continue but didn't issue any tool calls. "
                    "Don't just describe what you'll do — actually issue the tool calls NOW "
                    "using the JSON format: {\"tool_calls\": [{\"tool\": \"tool_name\", ...}]}");
                ChatMessage nudgeMsg(ChatRole::System, nudge);
                m_messages.append(nudgeMsg);
                emit messageAppended(nudgeMsg);

                qCDebug(log_ai_chat) << "Agent loop: model said it would continue but didn't issue tool calls."
                                     << "Nudging at iteration" << iteration << "/" << maxIterations;
                persistHistory();
                continue;
            }

            // No tool calls and no continuation intent — agent is done.
            qCDebug(log_ai_chat) << "Agent loop ending at iteration" << iteration
                                 << "/" << maxIterations
                                 << "— model returned text only, no tool calls."
                                 << "Response preview:" << result.content.left(200);
            appendAssistantMessage(result.content);
            reachedMaxIterations = false;  // Agent finished naturally
            break;
        }

        qCDebug(log_ai_chat) << "Agent iteration" << iteration << "/" << maxIterations
                             << "— parsed" << toolCalls.size() << "tool call(s):";
        for (const auto &tc : toolCalls) {
            qCDebug(log_ai_chat) << "  tool:" << tc.tool << "args:" << tc.args;
        }

        // Cross-iteration auto-conversion: if any previous iteration performed
        // a keyboard action (type_text, press_key, etc.), convert capture_screen
        // calls to screen_to_markdown. Terminal output should be read via OCR,
        // not vision. This complements the within-batch conversion in
        // ChatToolExecution::executeToolCalls which only sees the current batch.
        //
        // DISABLED for BIOS/TextUI modes: OCR cannot detect selection state
        // (indicated by color/highlighting), so these modes must use AI vision.
        qCDebug(log_ai_chat) << "Auto-conversion check: anyKeyboardActionInLoop=" << anyKeyboardActionInLoop
                             << "isTextBasedUI=" << isTextBasedUI;
        if (anyKeyboardActionInLoop && !isTextBasedUI) {
            qCDebug(log_ai_chat) << "Auto-conversion enabled: checking for capture_screen calls to convert";
            for (auto &tc : toolCalls) {
                QString tool = tc.tool.toLower();
                qCDebug(log_ai_chat) << "  Checking tool:" << tool;
                if (tool == "capture_screen" || tool == "take_screenshot" || tool == "screenshot") {
                    qCDebug(log_ai_chat) << "Cross-iteration auto-converting" << tc.tool
                                         << "-> screen_to_markdown (keyboard action in prior iteration)";
                    tc.tool = "screen_to_markdown";
                }
            }
            qCDebug(log_ai_chat) << "Tool calls after conversion:";
            for (const auto &tc : toolCalls) {
                qCDebug(log_ai_chat) << "  tool:" << tc.tool;
            }
        } else if (anyKeyboardActionInLoop && isTextBasedUI) {
            qCDebug(log_ai_chat) << "Auto-conversion DISABLED for text-based UI (BIOS/TextUI mode)";
        }

        // Execute tool calls
        appendAssistantMessage(result.content);

        AgentToolExecutionResult toolResult = toolExec.executeToolCalls(toolCalls);

        qCDebug(log_ai_chat) << "Tool execution result: summary=" << toolResult.summary.left(200)
                             << "ocrText.length=" << toolResult.ocrText.length()
                             << "attachmentPath=" << toolResult.attachmentFilePath;

        // Update cross-iteration keyboard tracker: if any tool in this batch
        // was a keyboard action, all subsequent captures should use OCR.
        for (const auto &tc : toolCalls) {
            QString tool = tc.tool.toLower();
            if (tool == "type_text" || tool == "press_key" || tool == "key_press" ||
                tool == "send_key" || tool == "hotkey") {
                anyKeyboardActionInLoop = true;
                break;
            }
        }

        // Build tool result message. The "Tool Result" role label in the UI
        // already indicates what this is, so we just include the summary and
        // optional OCR text without a redundant "TOOL_RESULT:" prefix.
        QString toolResultContent = toolResult.summary;

        // If OCR was used, include the OCR text in the result
        if (!toolResult.ocrText.isEmpty()) {
            toolResultContent += QString("\n\n--- OCR Analysis Result ---\n%1").arg(toolResult.ocrText);
        }

        ChatMessage toolMsg(ChatRole::Tool, toolResultContent, toolResult.attachmentFilePath);
        // Generate a tool_call_id for the OpenAI API format. Tool messages
        // require this field to link back to the assistant's tool call.
        toolMsg.toolCallId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        m_messages.append(toolMsg);
        emit messageAppended(toolMsg);

        // Add a step indicator showing which analysis method was used
        if (!toolResult.ocrText.isEmpty()) {
            QString ocrStepText = QString("📝 Step %1/%2 — examined screen using OCR (text extraction)...")
                .arg(iteration).arg(maxIterations);
            ChatMessage ocrStepMsg(ChatRole::Assistant, ocrStepText);
            ocrStepMsg.isStatusHint = true;
            m_messages.append(ocrStepMsg);
            emit messageAppended(ocrStepMsg);
        } else if (!toolResult.attachmentFilePath.isEmpty()) {
            QString visionStepText = QString("🔍 Step %1/%2 — examined screen using AI vision analysis...")
                .arg(iteration).arg(maxIterations);
            ChatMessage visionStepMsg(ChatRole::Assistant, visionStepText);
            visionStepMsg.isStatusHint = true;
            m_messages.append(visionStepMsg);
            emit messageAppended(visionStepMsg);
        }

        // Update image data URL if we got a new screenshot (but not if OCR was used)
        if (!toolResult.attachmentFilePath.isEmpty() && toolResult.ocrText.isEmpty()) {
            imageDataURL = screenCapture.dataURLForImage(toolResult.attachmentFilePath);
        } else if (!toolResult.ocrText.isEmpty()) {
            // OCR was used - clear the image data URL since we're using text analysis
            imageDataURL.clear();
        }

        persistHistory();
    }

    // If we reached max iterations, prompt the user to continue or stop
    if (reachedMaxIterations && !m_cancelRequested) {
        QString maxIterMsg = QString("**Reached maximum steps (%1).** The agent has completed %1 iterations. "
                                     "Would you like to continue running?")
            .arg(maxIterations);

        ChatMessage msg(ChatRole::Assistant, maxIterMsg);
        msg.quickReplies.append(ChatQuickReply("Continue", "continue"));
        msg.quickReplies.append(ChatQuickReply("Stop", "stop"));
        m_messages.append(msg);
        emit messageAppended(msg);
        persistHistory();
    }

    // Complete agent request status
    ChatMessage *lastUser = nullptr;
    for (int i = m_messages.size() - 1; i >= 0; --i) {
        if (m_messages[i].role == ChatRole::User) {
            lastUser = &m_messages[i];
            break;
        }
    }
    if (lastUser && m_agentRequestStatuses.contains(lastUser->id)) {
        completeAgentRequestStatus(lastUser->id);
    }

    m_isSending = false;
    emit sendingStateChanged(false);
    persistHistory();
}

void ChatManager::performPlannerSend(const ChatAPIConfiguration &config)
{
    qCDebug(log_ai_chat) << "performPlannerSend: starting planner mode";
    GlobalSetting &gs = GlobalSetting::instance();
    QString systemPrompt = gs.getChatSystemPrompt().trimmed();
    QString plannerPrompt = gs.getChatPlannerPrompt().trimmed();

    qCDebug(log_ai_chat) << "performPlannerSend: systemPrompt length:" << systemPrompt.length()
                         << "plannerPrompt length:" << plannerPrompt.length();

    ChatScreenCapture &screenCapture = ChatScreenCapture::instance();
    ChatTracing &tracing = ChatTracing::instance();

    // Get the last user message.
    // Note: if no attachment was provided, sendMessage() already auto-captured
    // on the main thread and stored the path in m_messages.last().attachmentFilePath.
    QString userRequest;
    QString imageDataURL;
    for (int i = m_messages.size() - 1; i >= 0; --i) {
        if (m_messages[i].role == ChatRole::User) {
            userRequest = m_messages[i].content;
            if (!m_messages[i].attachmentFilePath.isEmpty()) {
                imageDataURL = screenCapture.dataURLForImage(m_messages[i].attachmentFilePath);
                qCDebug(log_ai_chat) << "performPlannerSend: using attachment:" << m_messages[i].attachmentFilePath;
            }
            break;
        }
    }

    qCDebug(log_ai_chat) << "performPlannerSend: userRequest:" << userRequest.left(200);

    QList<ChatApiMessage> conversation = m_plannerAgent.buildPlanningConversation(
        systemPrompt, plannerPrompt, userRequest, imageDataURL);

    qCDebug(log_ai_chat) << "performPlannerSend: built conversation with" << conversation.size() << "messages";
    tracing.appendAITrace("PLANNER REQUEST", tracing.readableTraceParts(conversation));

    ChatCompletionResult result;
    QString apiError;
    qCDebug(log_ai_chat) << "performPlannerSend: sending completion request";
    result = sendCompletionSync(config.baseURL, config.model, config.apiKey, conversation, apiError);

    if (m_cancelRequested) {
        qCDebug(log_ai_chat) << "performPlannerSend: cancelled";
        m_isSending = false;
        emit sendingStateChanged(false);
        return;
    }

    if (!apiError.isEmpty()) {
        qCWarning(log_ai_chat) << "performPlannerSend: API error:" << apiError;
        presentAIError(QString("Planner API error: %1").arg(apiError));
        m_isSending = false;
        emit sendingStateChanged(false);
        return;
    }

    if (result.content.isEmpty()) {
        qCWarning(log_ai_chat) << "performPlannerSend: empty response from planner";
        presentAIError("Empty response from planner");
        m_isSending = false;
        emit sendingStateChanged(false);
        return;
    }

    qCDebug(log_ai_chat) << "performPlannerSend: received response, length:" << result.content.length();
    tracing.appendAITrace("PLANNER RESPONSE", result.content.left(500));

    ChatExecutionPlan plan;
    QString parseError;
    if (!m_plannerAgent.parsePlan(result.content, userRequest, plan, parseError)) {
        qCWarning(log_ai_chat) << "performPlannerSend: failed to parse plan:" << parseError;
        qCDebug(log_ai_chat) << "performPlannerSend: raw response:" << result.content.left(500);
        presentAIError(QString("Planner failed: %1").arg(parseError));
        m_isSending = false;
        emit sendingStateChanged(false);
        return;
    }

    qCDebug(log_ai_chat) << "performPlannerSend: successfully parsed plan with" << plan.tasks.size() << "tasks";
    m_currentPlan = plan;
    m_hasPlan = true;
    persistHistory();
    emit planChanged();

    // Add assistant message describing the plan
    appendAssistantMessage(QString("I've created a plan:\n\n**%1**\n\n%2 tasks pending your approval.")
        .arg(plan.summary).arg(plan.tasks.size()));

    m_isSending = false;
    emit sendingStateChanged(false);
}

void ChatManager::performGuideSend(const ChatAPIConfiguration &config)
{
    GlobalSetting &gs = GlobalSetting::instance();
    QString systemPrompt = gs.getChatSystemPrompt().trimmed();
    QString guidePrompt = gs.getChatGuidePrompt().trimmed();

    ChatScreenCapture &screenCapture = ChatScreenCapture::instance();
    ChatConversationBuilder &builder = ChatConversationBuilder::instance();
    ChatTracing &tracing = ChatTracing::instance();

    // Capture screen for guide
    QString screenshotPath = screenCapture.captureScreen();
    QString imageDataURL;
    if (!screenshotPath.isEmpty()) {
        imageDataURL = screenCapture.dataURLForImage(screenshotPath);
    }

    QList<ChatApiMessage> conversation = builder.buildConversation(
        systemPrompt + "\n\n" + guidePrompt, m_messages, false, imageDataURL);

    tracing.appendAITrace("GUIDE REQUEST", tracing.readableTraceParts(conversation));

    ChatCompletionResult result;
    QString apiError;
    result = sendCompletionSync(config.baseURL, config.model, config.apiKey, conversation, apiError);

    if (m_cancelRequested) {
        m_isSending = false;
        emit sendingStateChanged(false);
        return;
    }

    if (result.content.isEmpty()) {
        presentAIError("Empty response from guide");
        m_isSending = false;
        emit sendingStateChanged(false);
        return;
    }

    tracing.appendAITrace("GUIDE RESPONSE", result.content.left(500));

    // Parse guide response
    ChatGuideMode::GuideResponse guide = ChatGuideMode::instance().parseGuideResponse(result.content);

    // Create message with guide data
    ChatMessage msg(ChatRole::Assistant, guide.nextStep);
    if (!guide.targetBox.isNull()) {
        msg.guideActionRect = guide.targetBox;
        msg.guideTool = guide.tool;
    }
    if (!guide.shortcut.isEmpty()) {
        msg.guideShortcut = guide.shortcut;
    }

    m_messages.append(msg);
    emit messageAppended(msg);

    // Show overlay if there's a target box
    if (!guide.targetBox.isNull()) {
        emit guideOverlayRequested(guide.targetBox, guide.tool);
    }

    persistHistory();
    m_isSending = false;
    emit sendingStateChanged(false);
}

void ChatManager::executeApprovedPlan()
{
    ChatAPIConfiguration config = currentChatAPIConfiguration();
    if (config.baseURL.isEmpty()) {
        presentAIError("Invalid API configuration");
        m_isSending = false;
        emit sendingStateChanged(false);
        return;
    }

    ChatScreenCapture &screenCapture = ChatScreenCapture::instance();
    ChatTracing &tracing = ChatTracing::instance();

    // Add a message showing the plan is starting
    QString planStartMsg = QString("**Executing plan:** %1\n\n%2 tasks to complete.")
        .arg(m_currentPlan.summary)
        .arg(m_currentPlan.tasks.size());
    QMetaObject::invokeMethod(this, [this, planStartMsg]() {
        appendAssistantMessage(planStartMsg);
    }, Qt::QueuedConnection);

    for (int i = 0; i < m_currentPlan.tasks.size(); ++i) {
        if (m_cancelRequested) break;

        ChatTask &task = m_currentPlan.tasks[i];
        if (task.status != ChatTaskStatus::Approved) continue;

        task.status = ChatTaskStatus::Running;
        emit planTaskUpdated(i, task);

        // Add step start message
        QString stepStartMsg = QString("**Step %1/%2:** %3\n\n*Running...*")
            .arg(i + 1).arg(m_currentPlan.tasks.size()).arg(task.title);
        QMetaObject::invokeMethod(this, [this, stepStartMsg]() {
            appendAssistantMessage(stepStartMsg);
        }, Qt::QueuedConnection);

        persistHistory();

        // Resolve agent
        TaskAgentExecutor *agent = m_taskAgentRegistry.resolve(task);
        if (!agent) {
            task.status = ChatTaskStatus::Failed;
            task.resultSummary = QString("No agent found for %1/%2").arg(task.agentName, task.toolName);
            emit planTaskUpdated(i, task);

            // Add error message
            QString errorMsg = QString("**Step %1/%2:** %3\n\n✗ **Failed:** %4")
                .arg(i + 1).arg(m_currentPlan.tasks.size()).arg(task.title, task.resultSummary);
            QMetaObject::invokeMethod(this, [this, errorMsg]() {
                appendAssistantMessage(errorMsg);
            }, Qt::QueuedConnection);
            continue;
        }

        // Capture screen for the task
        QString screenshotPath = screenCapture.captureScreen();
        QString imageDataURL;
        if (!screenshotPath.isEmpty()) {
            imageDataURL = screenCapture.dataURLForImage(screenshotPath);
        }

        // Build conversation
        QString systemPrompt = GlobalSetting::instance().getChatSystemPrompt().trimmed();
        QList<ChatApiMessage> conversation = agent->buildTaskConversation(
            systemPrompt, m_currentPlan, task, imageDataURL);

        tracing.appendTaskStepTrace(task.id, task.title,
            tracing.readableTraceParts(conversation), screenshotPath);

        // Send API request and track timing
        QElapsedTimer timer;
        timer.start();
        ChatCompletionResult result;
        QString apiError;
        result = sendCompletionSync(config.baseURL, config.model, config.apiKey, conversation, apiError);
        qint64 elapsedMs = timer.elapsed();

        if (m_cancelRequested) break;

        // Show the AI's response (tool call details) similar to agent mode.
        // The response contains JSON with tool arguments which gets formatted
        // as a readable table by ChatBubbleWidget::formatContentForDisplay.
        int inputTokens = result.inputTokenCount;
        int outputTokens = result.outputTokenCount;
        QString aiResponse = result.content;
        QMetaObject::invokeMethod(this, [this, aiResponse, elapsedMs, inputTokens, outputTokens]() {
            appendAssistantMessage(aiResponse, static_cast<int>(elapsedMs), inputTokens, outputTokens);
        }, Qt::QueuedConnection);

        // Apply response
        agent->applyResponse(result.content, task);
        task.inputTokenCount = result.inputTokenCount;
        task.outputTokenCount = result.outputTokenCount;

        emit planTaskUpdated(i, task);

        // Add step result message with timing and token info
        QString statusIcon = (task.status == ChatTaskStatus::Completed) ? "✓" : "✗";
        QString stepResultMsg = QString("**Step %1/%2:** %3\n\n%4 **Result:** %5")
            .arg(i + 1).arg(m_currentPlan.tasks.size()).arg(task.title)
            .arg(statusIcon, task.resultSummary);
        QString attachmentPath = screenshotPath;  // Capture for lambda
        QMetaObject::invokeMethod(this, [this, stepResultMsg, attachmentPath]() {
            appendAssistantMessage(stepResultMsg, attachmentPath);
        }, Qt::QueuedConnection);

        // Trace
        tracing.appendTaskStepTrace(task.id,
            QString("RESPONSE status=%1").arg(chatTaskStatusToString(task.status)),
            result.content.left(300), screenshotPath);

        persistHistory();
    }

    // Update plan status
    bool allCompleted = true;
    bool anyFailed = false;
    for (const auto &task : m_currentPlan.tasks) {
        if (task.status == ChatTaskStatus::Failed) anyFailed = true;
        if (task.status != ChatTaskStatus::Completed) allCompleted = false;
    }

    if (anyFailed) {
        m_currentPlan.status = ChatPlanStatus::Failed;
    } else if (allCompleted) {
        m_currentPlan.status = ChatPlanStatus::Completed;
    }

    emit planChanged();

    // Build a summary request to send to the AI
    QString summaryPrompt = QString(
        "You have just completed an execution plan. Please provide a brief summary of what was accomplished.\n\n"
        "Plan goal: %1\n\n"
        "Tasks executed:\n"
    ).arg(m_currentPlan.summary);

    for (const auto &task : m_currentPlan.tasks) {
        QString statusIcon;
        switch (task.status) {
        case ChatTaskStatus::Completed: statusIcon = "✓"; break;
        case ChatTaskStatus::Failed: statusIcon = "✗"; break;
        case ChatTaskStatus::Skipped: statusIcon = "-"; break;
        default: statusIcon = "?"; break;
        }
        summaryPrompt += QString("%1 %2: %3\n").arg(statusIcon, task.title, task.resultSummary);
    }

    if (m_cancelRequested) {
        summaryPrompt += "\nNote: The plan was cancelled by the user before completion.";
    }

    summaryPrompt += "\n\nProvide a concise summary of the results and any next steps if applicable.";

    // Build conversation for summary
    QList<ChatApiMessage> summaryConversation;
    QString systemPrompt = GlobalSetting::instance().getChatSystemPrompt().trimmed();
    if (!systemPrompt.isEmpty()) {
        summaryConversation.append(ChatApiMessage::textMessage(ChatRole::System, systemPrompt));
    }
    summaryConversation.append(ChatApiMessage::textMessage(ChatRole::User, summaryPrompt));

    // Send API request for summary
    ChatCompletionResult summaryResult;
    QString summaryError;
    summaryResult = sendCompletionSync(config.baseURL, config.model, config.apiKey, summaryConversation, summaryError);

    QString finalSummary;
    if (!summaryError.isEmpty()) {
        // If API call fails, use fallback summary
        if (m_cancelRequested) {
            finalSummary = "**Plan cancelled by user.**";
        } else if (anyFailed) {
            int failedCount = 0;
            for (const auto &task : m_currentPlan.tasks) {
                if (task.status == ChatTaskStatus::Failed) failedCount++;
            }
            finalSummary = QString("**Plan completed with errors.**\n\n%1 of %2 tasks failed.")
                .arg(failedCount).arg(m_currentPlan.tasks.size());
        } else {
            finalSummary = QString("**Plan completed successfully.**\n\nAll %1 tasks executed.")
                .arg(m_currentPlan.tasks.size());
        }
    } else {
        // Use AI-generated summary
        finalSummary = summaryResult.content;
    }

    qCDebug(log_ai_chat) << "=== FINAL SUMMARY DEBUG ===";
    qCDebug(log_ai_chat) << "Final summary text:" << finalSummary;

    // Use QMetaObject::invokeMethod to ensure the message is added on the main thread
    QMetaObject::invokeMethod(this, [this, finalSummary]() {
        qCDebug(log_ai_chat) << "Invoking appendAssistantMessage on main thread";
        appendAssistantMessage(finalSummary);
        qCDebug(log_ai_chat) << "Calling persistHistory...";
        persistHistory();

        qCDebug(log_ai_chat) << "Setting m_isSending to false and emitting sendingStateChanged";
        m_isSending = false;
        emit sendingStateChanged(false);

        qCDebug(log_ai_chat) << "=== END FINAL SUMMARY DEBUG ===";
    }, Qt::QueuedConnection);
}

// ============================================================================
// Helpers
// ============================================================================

void ChatManager::persistHistory()
{
    ChatPersistence::instance().saveHistory(m_messages, m_currentPlan, m_hasPlan, m_plannerTraceEntries);
}

void ChatManager::loadHistory()
{
    ChatPersistence::instance().loadHistory(m_messages, m_currentPlan, m_hasPlan, m_plannerTraceEntries);
    emit messagesChanged();
    if (m_hasPlan) emit planChanged();
}

void ChatManager::presentAIError(const QString &error)
{
    qCWarning(log_ai_chat) << "AI Error:" << error;
    m_lastError = error;
    emit lastErrorChanged(error);

    ChatMessage msg(ChatRole::System, QString("Error: %1").arg(error));
    m_messages.append(msg);
    emit messageAppended(msg);
    persistHistory();
}

void ChatManager::appendAssistantMessage(const QString &content, const QString &attachment)
{
    qCDebug(log_ai_chat) << "appendAssistantMessage called with content:" << content.left(100) << "...";
    ChatMessage msg(ChatRole::Assistant, content, attachment);
    qCDebug(log_ai_chat) << "Created message with id:" << msg.id;
    m_messages.append(msg);
    qCDebug(log_ai_chat) << "Message appended to m_messages, size:" << m_messages.size();
    emit messageAppended(msg);
    qCDebug(log_ai_chat) << "messageAppended signal emitted";
}

void ChatManager::appendAssistantMessage(const QString &content, int processingTimeMs, int inputTokens, int outputTokens)
{
    qCDebug(log_ai_chat) << "appendAssistantMessage (with metadata) called with content:" << content.left(100) << "...";
    ChatMessage msg(ChatRole::Assistant, content);
    msg.processingTimeMs = processingTimeMs;
    msg.inputTokens = inputTokens;
    msg.outputTokens = outputTokens;
    qCDebug(log_ai_chat) << "Created message with id:" << msg.id << "processingTimeMs:" << processingTimeMs << "tokens:" << inputTokens << "/" << outputTokens;
    m_messages.append(msg);
    qCDebug(log_ai_chat) << "Message appended to m_messages, size:" << m_messages.size();
    emit messageAppended(msg);
    qCDebug(log_ai_chat) << "messageAppended signal emitted";
}

void ChatManager::appendAssistantMessage(const QString &content, const QString &attachment, int processingTimeMs, int inputTokens, int outputTokens)
{
    qCDebug(log_ai_chat) << "appendAssistantMessage (with attachment and metadata) called with content:" << content.left(100) << "...";
    ChatMessage msg(ChatRole::Assistant, content, attachment);
    msg.processingTimeMs = processingTimeMs;
    msg.inputTokens = inputTokens;
    msg.outputTokens = outputTokens;
    qCDebug(log_ai_chat) << "Created message with id:" << msg.id << "attachment:" << attachment << "processingTimeMs:" << processingTimeMs << "tokens:" << inputTokens << "/" << outputTokens;
    m_messages.append(msg);
    qCDebug(log_ai_chat) << "Message appended to m_messages, size:" << m_messages.size();
    emit messageAppended(msg);
    qCDebug(log_ai_chat) << "messageAppended signal emitted";
}

void ChatManager::startAgentRequestStatus(const QUuid &messageID)
{
    m_agentRequestStatuses[messageID] = GuideAutoNextStatus(GuideAutoNextStatus::Thinking, "Thinking...");
    emit agentRequestStatusChanged(messageID, m_agentRequestStatuses[messageID]);
}

void ChatManager::completeAgentRequestStatus(const QUuid &messageID)
{
    m_agentRequestStatuses[messageID] = GuideAutoNextStatus(GuideAutoNextStatus::Completed, "Done");
    emit agentRequestStatusChanged(messageID, m_agentRequestStatuses[messageID]);
}

void ChatManager::failAgentRequestStatus(const QUuid &messageID, const QString &error)
{
    m_agentRequestStatuses[messageID] = GuideAutoNextStatus(GuideAutoNextStatus::Failed, error);
    emit agentRequestStatusChanged(messageID, m_agentRequestStatuses[messageID]);
}

void ChatManager::cancelAgentRequestStatus(const QUuid &messageID)
{
    m_agentRequestStatuses[messageID] = GuideAutoNextStatus(GuideAutoNextStatus::Cancelled, "Cancelled");
    emit agentRequestStatusChanged(messageID, m_agentRequestStatuses[messageID]);
}

void ChatManager::clearGuideOverlay()
{
    emit guideOverlayCleared();
}

void ChatManager::startGuideAutoNextStatus(const QUuid &messageID)
{
    m_guideAutoNextStatuses[messageID] = GuideAutoNextStatus(GuideAutoNextStatus::Thinking, "Auto-next...");
    emit guideAutoNextStatusChanged(messageID, m_guideAutoNextStatuses[messageID]);
}

void ChatManager::cancelGuideAutoNextStatus(const QUuid &messageID)
{
    m_guideAutoNextStatuses[messageID] = GuideAutoNextStatus(GuideAutoNextStatus::Cancelled, "Cancelled");
    emit guideAutoNextStatusChanged(messageID, m_guideAutoNextStatuses[messageID]);
}
