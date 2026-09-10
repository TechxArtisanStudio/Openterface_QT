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
#include "ChatTaskScheduler.h"
#include "ui/globalsetting.h"
#include <QLoggingCategory>
#include <QtConcurrent>
#include <QEventLoop>
#include <QThread>
#include <QTimer>
#include <QRegularExpression>
#include <QImage>

Q_DECLARE_LOGGING_CATEGORY(log_ai_chat)

// Maximum number of retries when the AI returns empty content
static const int MAX_EMPTY_CONTENT_RETRIES = 3;

// ============================================================================
// Idle-loop watchdog helpers
// ============================================================================

namespace {

// How many consecutive "observation only" iterations on an unchanged screen
// (i.e. the model keeps calling capture_screen/screen_to_markdown but issues
// no key/mouse action and the screen content is static) before we nudge the
// model to conclude, and before we force-terminate the run.
const int IDLE_WATCHDOG_NUDGE_STREAK = 3;
const int IDLE_WATCHDOG_TERMINATE_STREAK = 5;
const int IDLE_WATCHDOG_MAX_NUDGES = 1;

// Tools that only "look" at the target without changing any state. When the
// model issues ONLY these for consecutive iterations, it is observing but not
// acting, which is what the watchdog detects.
bool isObservationOnlyTool(const QString &toolName)
{
    const QString t = toolName.toLower();
    return t == "capture_screen" || t == "screenshot" || t == "take_screenshot"
        || t == "screen_to_markdown" || t == "screen_diff" || t == "detect_cursor";
}

// Cheap perceptual comparison of two captured screen files. Loads both, scales
// them to a small grid and computes the mean per-channel absolute difference.
// Below the threshold the two frames are treated as "the same screen".
bool framesVisuallySame(const QString &pathA, const QString &pathB)
{
    if (pathA.isEmpty() || pathB.isEmpty()) return false;
    if (pathA == pathB) return true;

    QImage a(pathA);
    QImage b(pathB);
    if (a.isNull() || b.isNull() || a.size() != b.size()) {
        return false; // unknown or different-size frames -> treat as changed
    }

    QImage sa = a.scaled(160, 90, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                 .convertToFormat(QImage::Format_RGB888);
    QImage sb = b.scaled(160, 90, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                 .convertToFormat(QImage::Format_RGB888);

    const int w = sa.width();
    const int h = sa.height();
    long long sumAbs = 0;
    for (int y = 0; y < h; ++y) {
        const uchar *pa = sa.constScanLine(y);
        const uchar *pb = sb.constScanLine(y);
        for (int x = 0; x < w; ++x) {
            const uchar *ca = pa + x * 3;
            const uchar *cb = pb + x * 3;
            sumAbs += qAbs(int(ca[0]) - int(cb[0]))
                    + qAbs(int(ca[1]) - int(cb[1]))
                    + qAbs(int(ca[2]) - int(cb[2]));
        }
    }

    const long long samples = (long long)w * h * 3;
    const double meanAbsDiff = samples ? (double)sumAbs / samples : 0.0;
    return meanAbsDiff < 6.0;
}

// True if an API error string indicates HTTP 429 / rate limiting. Used to
// decide whether to back off (429) versus quick-retry (occasional empty
// content) when the model returns nothing useful.
bool isRateLimitError(const QString &err)
{
    const QString e = err.toLower();
    return e.contains("429")
        || e.contains("rate limit")
        || e.contains("rate_limit")
        || e.contains("ratelimit")
        || e.contains("too many requests");
}

} // namespace

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
//
// IMPORTANT: QtConcurrent pooled threads don't have a persistent event loop.
// A local QTimer may not fire correctly if started before loop.exec(). To work
// around this, we use QTimer::singleShot() with a zero-delay timer that's
// started AFTER the event loop is running, ensuring proper registration with
// the thread's event dispatcher.
static ChatCompletionResult sendCompletionSync(
    const QUrl &baseURL, const QString &model, const QString &apiKey,
    const QList<ChatApiMessage> &messages, QString &outError,
    const bool *cancelFlag = nullptr)
{
    // ------------------------------------------------------------------
    // Respect rate limiting: if the API recently returned HTTP 429, we are
    // inside a cooldown window (computed in ChatApiClient from Retry-After
    // or an exponential backoff). Sleep out the remainder before sending so
    // we don't hammer the upstream while it is throttling us. Sleeps in
    // one-second slices so a user cancel is honored promptly.
    // ------------------------------------------------------------------
    qint64 throttleMs = ChatApiClient::instance().rateLimitRemainingMs();
    if (throttleMs > 0) {
        qCWarning(log_ai_chat) << "AI Chat throttled by rate limit — waiting"
                               << (throttleMs / 1000.0) << "s before request (model="
                               << model << ")";
        const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + throttleMs;
        while (true) {
            if (cancelFlag && *cancelFlag) {
                outError = "Request cancelled while waiting out rate limit.";
                return ChatCompletionResult();
            }
            qint64 remaining = deadline - QDateTime::currentMSecsSinceEpoch();
            if (remaining <= 0) break;
            QThread::msleep(static_cast<unsigned long>(qMin(remaining, qint64(1000))));
        }
    }

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
    //
    // Use QTimer::singleShot() instead of a local QTimer. The singleShot timer
    // is managed by Qt's global timer system and doesn't require the thread to
    // have a pre-existing event loop. This is more reliable on QtConcurrent
    // pooled threads.
    QTimer::singleShot(120000, &loop, [&, completed, aborted]() {
        // If the callback already fired, the result is set — nothing to do.
        if (*completed) return;

        qCWarning(log_ai_chat) << "AI Chat API request timed out after 120s (model=" << model << ")";
        outError = "AI request timed out (120s). The API server may be overloaded.";
        *aborted = true;
        loop.quit();
    });

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

    // Start the task scheduler for scheduled task execution
    ChatTaskScheduler::instance().start();

    // Connect scheduled task ready signal
    connect(&ChatTaskScheduler::instance(), &ChatTaskScheduler::taskReadyForExecution,
            this, &ChatManager::onScheduledTaskReady, Qt::QueuedConnection);
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

QString ChatManager::effectiveAgentTargetSystem() const
{
    // Return the task-pinned target system if one has been set (i.e. an agent
    // task has been started in this conversation), otherwise fall back to the
    // currently persisted setting (which is also what the UI combobox shows).
    if (m_hasTaskTargetSystem && !m_taskTargetSystem.isEmpty())
        return m_taskTargetSystem;
    return GlobalSetting::instance().getChatTargetSystem();
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

    // Pin the target system for this agent task.
    // - Fresh (non-continue) user message: snapshot the currently persisted
    //   target system. The pin stays fixed for every subsequent "continue"
    //   of the same task, so a BIOS task stays in BIOS/TextUI mode even if
    //   the model or screen_to_markdown auto-detection flipped the global
    //   setting mid-task (e.g. model called set_target_system("linux")
    //   while the target was still booting toward BIOS).
    // - "continue" command: reuse the existing pin (don't re-read global).
    //   This is the key fix for "requests stop after some tries": the
    //   continuation reuses the task's original mode (e.g. bios=true) so
    //   screen images stay attached and the model doesn't go blind.
    if (isContinueCommand) {
        qCDebug(log_ai_chat) << "AI Chat: continue — reusing task target system"
                             << m_taskTargetSystem << "(pinned=" << m_hasTaskTargetSystem << ")";
    } else {
        m_taskTargetSystem = GlobalSetting::instance().getChatTargetSystem();
        m_hasTaskTargetSystem = !m_taskTargetSystem.isEmpty();
        qCDebug(log_ai_chat) << "AI Chat: new task — pinning target system"
                             << m_taskTargetSystem;
    }

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
                       "CRITICAL FIRST STEPS:\n"
                       "1. Call capture_screen IMMEDIATELY to see your current location\n"
                       "2. Identify where you are and what menu/screen is displayed\n"
                       "3. Review the conversation history to understand what you were working on\n"
                       "4. Continue issuing tool calls to make progress on the task\n\n"
                       "IMPORTANT:\n"
                       "- The current screenshot shows the latest state of the target screen\n"
                       "- If you're in BIOS/TextUI: verify which item is highlighted before pressing Enter\n"
                       "- If you're unsure what to do next, call capture_screen to re-examine the screen\n"
                       "- Do NOT assume you know where you are — always verify with capture_screen first";
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
    m_hasTaskTargetSystem = false;
    m_taskTargetSystem.clear();
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
    QString currentScreenPath;   // file path of the frame attached to this iteration's request
    for (int i = m_messages.size() - 1; i >= 0; --i) {
        if (m_messages[i].role == ChatRole::User && !m_messages[i].attachmentFilePath.isEmpty()) {
            imageDataURL = screenCapture.dataURLForImage(m_messages[i].attachmentFilePath);
            currentScreenPath = m_messages[i].attachmentFilePath;
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
    // BIOS/TextUI modes should NOT auto-convert because OCR cannot detect selection state.
    // IMPORTANT: read the task-pinned target system (effectiveAgentTargetSystem)
    // rather than the persisted setting directly. When a fresh user message
    // triggers an agent run, m_taskTargetSystem is snapshotted from the
    // persisted value and reused by subsequent "continue" runs, so e.g. a
    // "boot into BIOS and configure X" task keeps operating with BIOS/TextUI
    // behavior even if the model (or screen_to_markdown auto-detection)
    // flipped the persisted setting to "linux" mid-task.
    ChatTargetSystem targetSystem = chatTargetSystemFromString(effectiveAgentTargetSystem());
    bool isTextBasedUI = (targetSystem == ChatTargetSystem::BIOS || targetSystem == ChatTargetSystem::TextUI);
    qCDebug(log_ai_chat) << "Agent loop started: targetSystem=" << chatTargetSystemToString(targetSystem)
                         << "isTextBasedUI=" << isTextBasedUI
                         << "(taskPinned=" << m_hasTaskTargetSystem << ")";

    // Track how many times we've nudged the model about broken XML tool calls.
    // Cap at 2 nudges total — if the model can't produce valid JSON after two
    // tries, further identical nudges just confuse it and bloat the conversation.
    int xmlNudgeCount = 0;
    const int MAX_XML_NUDGES = GlobalSetting::instance().getNudgeMaxXmlNudges();

    // Index of the last nudge message we added, so we can remove it before
    // adding a replacement (keeps only one nudge visible in history at a time).
    int lastNudgeIndex = -1;

    // Track whether we reached max iterations (loop completed without break)
    bool reachedMaxIterations = false;

    // --------------------------------------------------------------------
    // Idle-loop watchdog state
    // --------------------------------------------------------------------
    // Counts consecutive iterations where the model only observed an unchanged
    // screen (observation-only tool calls) without any key/mouse action. Used
    // to detect "the OS finished booting but the model keeps polling
    // capture_screen" and to end the run instead of looping to max iterations.
    int idleObserveStreak = 0;
    int idleNudgeCount = 0;
    QString prevObserveFramePath;  // frame attached to the previous observe iteration
    bool watchdogEnded = false;

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
                currentScreenPath.clear(); // no fresh vision frame this iteration
            } else {
                QString freshPath = screenCapture.captureScreen();
                if (!freshPath.isEmpty()) {
                    imageDataURL = screenCapture.dataURLForImage(freshPath);
                    currentScreenPath = freshPath;
                    qCDebug(log_ai_chat) << "performStandardSend: iteration" << iteration
                                         << "auto-captured fresh screen:" << freshPath;
                } else {
                    currentScreenPath.clear();
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
        result = sendCompletionSync(config.baseURL, config.model, config.apiKey,
                                    conversation, apiError, &m_cancelRequested);

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

        // Handle empty AI response with auto-retry
        if (result.content.isEmpty()) {
            // Distinguish rate-limit (HTTP 429) from genuine empty-content.
            // A rate limit is not transient "model returned nothing" — the
            // upstream is explicitly asking us to back off. Back off with the
            // ChatApiClient's exponential cooldown (30s, 60s, 120s...) up to
            // a generous overall budget rather than the quick 3-strike loop,
            // which would burn through its budget almost immediately on a 429.
            const bool rateLimitActive = ChatApiClient::instance().isRateLimited()
                                      || isRateLimitError(apiError);
            bool exitOuterLoop = false;

            if (rateLimitActive) {
                // Generous overall budget for surviving stubborn upstream rate
                // limits (burst windows on LiteLLM/minimax-style gateways can
                // last a couple of minutes). The user's cancellation is always
                // honored inside the loop.
                static constexpr qint64 RATE_LIMIT_TOTAL_BUDGET_MS = 5 * 60 * 1000; // 5 min
                const qint64 rateLimitStart = QDateTime::currentMSecsSinceEpoch();
                int rateLimitAttempts = 0;
                bool rateLimitGaveUp = false;

                while (result.content.isEmpty() && !m_cancelRequested) {
                    const qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - rateLimitStart;
                    if (elapsed >= RATE_LIMIT_TOTAL_BUDGET_MS) {
                        qCWarning(log_ai_chat) << "Rate-limited (429) for" << (elapsed / 1000)
                                               << "s — giving up";
                        presentAIError(QString(
                            "AI is rate-limited (HTTP 429). Waited %1s, still throttled. "
                            "Please try again later.").arg(elapsed / 1000));
                        rateLimitGaveUp = true;
                        break;
                    }

                    // If the API stops reporting 429 (cooldown cleared and the
                    // last error is no longer a rate limit) but content is still
                    // empty, this wasn't really a rate limit after all — fall
                    // through to the normal empty-content error handling.
                    if (rateLimitAttempts > 0
                        && !ChatApiClient::instance().isRateLimited()
                        && !isRateLimitError(apiError)) {
                        break;
                    }

                    rateLimitAttempts++;
                    qint64 waitMs = ChatApiClient::instance().rateLimitRemainingMs();
                    if (waitMs < 1000) waitMs = 1000; // never spin faster than 1s

                    qCWarning(log_ai_chat) << "Rate-limited (429) — waiting"
                                           << (waitMs / 1000.0) << "s before retry"
                                           << "(attempt" << rateLimitAttempts << ")";

                    // Show a visible hint so the user isn't staring at a
                    // silent "thinking..." indicator for minutes.
                    ChatMessage hint(ChatRole::Assistant,
                        QString("⏸ Rate-limited (429) — waiting ~%1s before retrying...")
                            .arg(waitMs / 1000));
                    hint.isStatusHint = true;
                    m_messages.append(hint);
                    emit messageAppended(hint);

                    // Sleep in 1-second slices, honoring cancellation.
                    const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + waitMs;
                    while (true) {
                        if (m_cancelRequested) break;
                        qint64 remaining = deadline - QDateTime::currentMSecsSinceEpoch();
                        if (remaining <= 0) break;
                        QThread::msleep(static_cast<unsigned long>(qMin(remaining, qint64(1000))));
                    }
                    if (m_cancelRequested) break;

                    result = sendCompletionSync(config.baseURL, config.model,
                                                config.apiKey, conversation,
                                                apiError, &m_cancelRequested);
                }

                if (m_cancelRequested) {
                    exitOuterLoop = true;
                } else if (result.content.isEmpty() && !rateLimitGaveUp) {
                    // Fell out because the API stopped reporting 429 but still
                    // returned empty content — report like the normal path.
                    if (!apiError.isEmpty()) {
                        presentAIError(apiError);
                    } else {
                        presentAIError("Empty response from AI (after retries)");
                    }
                    exitOuterLoop = true;
                } else if (rateLimitGaveUp) {
                    exitOuterLoop = true; // presentAIError already called
                }
            } else {
                // Non-rate-limit empty content: keep the existing 3-strike
                // quick-retry behavior (a genuinely empty response is almost
                // always fixed by an immediate re-ask).
                int retryCount = 0;
                while (retryCount < MAX_EMPTY_CONTENT_RETRIES && result.content.isEmpty()) {
                    retryCount++;
                    qCWarning(log_ai_chat) << "AI returned empty content (attempt"
                                           << retryCount << "/" << MAX_EMPTY_CONTENT_RETRIES << "). Retrying...";
                    if (retryCount < MAX_EMPTY_CONTENT_RETRIES) {
                        QThread::msleep(500);
                    }
                    result = sendCompletionSync(config.baseURL, config.model,
                                                config.apiKey, conversation,
                                                apiError, &m_cancelRequested);
                }
                if (result.content.isEmpty()) {
                    if (!apiError.isEmpty()) {
                        presentAIError(apiError);
                    } else {
                        presentAIError("Empty response from AI (after retries)");
                    }
                    exitOuterLoop = true;
                }
            }

            if (exitOuterLoop) break;
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

                QString nudge = GlobalSetting::instance().getNudgeMessage("xmlBroken");
                if (nudge.isEmpty()) {
                    nudge = QStringLiteral(
                        "Your last response was empty or contained broken XML tags. "
                        "Do NOT use XML tags for tool calls. "
                        "You MUST use JSON format: "
                        "{\"tool_calls\": [{\"tool\": \"tool_name\", \"arg1\": value1}]} "
                        "Issue the tool calls NOW in JSON format, or explain what you see and stop.");
                }
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
            // Patterns are loaded from nudge config (ai/default_nudge.json or user override).
            bool wantsToContinue = false;
            const QStringList continuationPatterns = GlobalSetting::instance().getNudgeContinuationPatterns();
            for (const QString &pattern : continuationPatterns) {
                if (textLower.contains(pattern)) {
                    wantsToContinue = true;
                    break;
                }
            }

            if (wantsToContinue && iteration < maxIterations) {
                appendAssistantMessage(result.content);

                QString nudge = GlobalSetting::instance().getNudgeMessage("continuation");
                if (nudge.isEmpty()) {
                    nudge = QStringLiteral(
                        "You said you would continue but didn't issue any tool calls. "
                        "Don't just describe what you'll do — actually issue the tool calls NOW "
                        "using the JSON format: {\"tool_calls\": [{\"tool\": \"tool_name\", ...}]}");
                }
                ChatMessage nudgeMsg(ChatRole::System, nudge);
                m_messages.append(nudgeMsg);
                emit messageAppended(nudgeMsg);

                qCDebug(log_ai_chat) << "Agent loop: model said it would continue but didn't issue tool calls."
                                     << "Nudging at iteration" << iteration << "/" << maxIterations;
                persistHistory();
                continue;
            }

            // BIOS/TextUI fallback: in text-based UI modes the agent should
            // ALWAYS be taking actions (navigating, pressing keys, capturing
            // screen). If the model responds with text only and doesn't clearly
            // indicate the task is finished, nudge it to keep going rather
            // than ending the loop. This catches cases where the model
            // describes what it sees or plans next steps without issuing
            // tool calls — a common failure mode in agentic BIOS navigation.
            if (isTextBasedUI && iteration < maxIterations) {
                QString responseLower = result.content.toLower();
                bool looksComplete = false;
                const QStringList completionPatterns = GlobalSetting::instance().getNudgeCompletionPatterns();
                for (const QString &pattern : completionPatterns) {
                    if (responseLower.contains(pattern)) {
                        looksComplete = true;
                        break;
                    }
                }

                if (!looksComplete) {
                    appendAssistantMessage(result.content);

                    QString nudge = GlobalSetting::instance().getNudgeMessage("biosFallback");
                    if (nudge.isEmpty()) {
                        nudge = QStringLiteral(
                            "You are in BIOS/TextUI agentic mode. You MUST issue tool calls in every "
                            "response — do not stop to describe what you see or plan what to do next. "
                            "If you don't know what to do, call capture_screen to re-examine the screen. "
                            "If the task is truly complete, say 'Task complete.' explicitly. "
                            "Otherwise, issue the next tool call NOW using JSON format: "
                            "{\"tool_calls\": [{\"tool\": \"tool_name\", ...}]}");
                    }
                    ChatMessage nudgeMsg(ChatRole::System, nudge);
                    m_messages.append(nudgeMsg);
                    emit messageAppended(nudgeMsg);

                    qCDebug(log_ai_chat) << "Agent loop (BIOS/TextUI fallback): model returned text only"
                                         << "without clear completion. Nudging at iteration"
                                         << iteration << "/" << maxIterations;
                    persistHistory();
                    continue;
                }
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

        // ====================================================================
        // Idle-loop watchdog
        // ====================================================================
        // Detect when the model repeatedly issues ONLY observation tool calls
        // (capture_screen / screen_to_markdown / detect_cursor) on an UNCHANGED
        // screen without performing any state-changing action (no key/mouse).
        //
        // This matters for tasks like "boot back to the OS": after the reboot
        // completes and the desktop/login becomes visible and static, a model can
        // keep polling capture_screen forever instead of emitting a final
        // summary. The screen is no longer changing, so we nudge it to conclude,
        // and if it still loops we end the run here instead of wasting the rest
        // of the iteration budget.
        {
            bool thisIterObserveOnly = true;
            for (const auto &tc : toolCalls) {
                if (!isObservationOnlyTool(tc.tool)) {
                    thisIterObserveOnly = false;
                    break;
                }
            }

            if (thisIterObserveOnly) {
                bool frameSameAsPrev = false;
                if (!prevObserveFramePath.isEmpty() && !currentScreenPath.isEmpty()) {
                    frameSameAsPrev = framesVisuallySame(prevObserveFramePath, currentScreenPath);
                }
                idleObserveStreak = frameSameAsPrev ? idleObserveStreak + 1 : 1;
                prevObserveFramePath = currentScreenPath;
            } else {
                idleObserveStreak = 0;
                prevObserveFramePath.clear();
            }

            qCDebug(log_ai_chat) << "Idle watchdog: observeOnly=" << thisIterObserveOnly
                                 << "streak=" << idleObserveStreak
                                 << "nudges=" << idleNudgeCount;

            if (idleObserveStreak >= IDLE_WATCHDOG_TERMINATE_STREAK) {
                qCInfo(log_ai_chat) << "Idle watchdog: terminating after" << idleObserveStreak
                                    << "consecutive observe-only iterations on an unchanged screen";
                ChatMessage doneMsg(ChatRole::Assistant, QStringLiteral(
                    "The agent has been observing the same unchanged screen for several steps "
                    "without taking any further action, so I'm ending the run here. "
                    "If the target has finished booting into the OS, the task is complete — "
                    "use /continue to keep working if needed."));
                m_messages.append(doneMsg);
                emit messageAppended(doneMsg);
                watchdogEnded = true;
                break;
            }

            if (idleObserveStreak >= IDLE_WATCHDOG_NUDGE_STREAK
                && idleNudgeCount < IDLE_WATCHDOG_MAX_NUDGES) {
                idleNudgeCount++;
                qCInfo(log_ai_chat) << "Idle watchdog: nudging model to conclude (streak"
                                    << idleObserveStreak << ")";
                ChatMessage nudgeMsg(ChatRole::System, QStringLiteral(
                    "You have taken screenshots for several consecutive steps without changing "
                    "anything or issuing any key/mouse action, and the screen has not changed. "
                    "If the target has finished booting into an OS (a desktop, login screen, or "
                    "shell prompt is visible), your task is COMPLETE — stop now and reply with a "
                    "concise final summary (text only, no tool calls). "
                    "If you are legitimately waiting for something to finish, say so briefly and "
                    "stop polling; do NOT call capture_screen again unless you are about to take "
                    "a concrete action."));
                m_messages.append(nudgeMsg);
                emit messageAppended(nudgeMsg);
            }
        }

        persistHistory();
    }

    // If we reached max iterations, prompt the user to continue or stop.
    // Skip this if the idle-loop watchdog already ended the run with a message.
    if (reachedMaxIterations && !m_cancelRequested && !watchdogEnded) {
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
    result = sendCompletionSync(config.baseURL, config.model, config.apiKey, conversation, apiError, &m_cancelRequested);

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
        // Auto-retry on empty content
        int retryCount = 0;

        while (retryCount < MAX_EMPTY_CONTENT_RETRIES && result.content.isEmpty()) {
            retryCount++;
            qCWarning(log_ai_chat) << "Planner returned empty content (attempt"
                                   << retryCount << "/" << MAX_EMPTY_CONTENT_RETRIES << "). Retrying...";

            // Small delay before retry to avoid overwhelming the API
            if (retryCount < MAX_EMPTY_CONTENT_RETRIES) {
                QThread::msleep(500); // 500ms delay between retries
            }

            // Retry the API request
            result = sendCompletionSync(config.baseURL, config.model, config.apiKey, conversation, apiError, &m_cancelRequested);

            // Check for API error on retry
            if (!apiError.isEmpty()) {
                qCWarning(log_ai_chat) << "performPlannerSend: API error during retry:" << apiError;
                break; // Exit retry loop if API error occurs
            }
        }

        if (result.content.isEmpty()) {
            if (!apiError.isEmpty()) {
                qCWarning(log_ai_chat) << "performPlannerSend: API error:" << apiError;
                presentAIError(QString("Planner API error: %1").arg(apiError));
            } else {
                qCWarning(log_ai_chat) << "performPlannerSend: empty response from planner (after retries)";
                presentAIError("Empty response from planner (after retries)");
            }
            m_isSending = false;
            emit sendingStateChanged(false);
            return;
        }
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
    result = sendCompletionSync(config.baseURL, config.model, config.apiKey, conversation, apiError, &m_cancelRequested);

    if (m_cancelRequested) {
        m_isSending = false;
        emit sendingStateChanged(false);
        return;
    }

    if (result.content.isEmpty()) {
        // Auto-retry on empty content
        int retryCount = 0;

        while (retryCount < MAX_EMPTY_CONTENT_RETRIES && result.content.isEmpty()) {
            retryCount++;
            qCWarning(log_ai_chat) << "Guide returned empty content (attempt"
                                   << retryCount << "/" << MAX_EMPTY_CONTENT_RETRIES << "). Retrying...";

            // Small delay before retry to avoid overwhelming the API
            if (retryCount < MAX_EMPTY_CONTENT_RETRIES) {
                QThread::msleep(500); // 500ms delay between retries
            }

            // Retry the API request
            result = sendCompletionSync(config.baseURL, config.model, config.apiKey, conversation, apiError, &m_cancelRequested);

            // If API error occurred during retry, break out
            if (!apiError.isEmpty()) {
                qCWarning(log_ai_chat) << "Guide API error during retry:" << apiError;
                break;
            }
        }

        if (result.content.isEmpty()) {
            qCWarning(log_ai_chat) << "Guide empty response (after retries)";
            presentAIError("Empty response from guide (after retries)");
            m_isSending = false;
            emit sendingStateChanged(false);
            return;
        }
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
        result = sendCompletionSync(config.baseURL, config.model, config.apiKey, conversation, apiError, &m_cancelRequested);
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
    summaryResult = sendCompletionSync(config.baseURL, config.model, config.apiKey, summaryConversation, summaryError, &m_cancelRequested);

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

    // If a scheduled task was executing, mark it as completed
    if (!m_executingScheduledTaskId.isEmpty()) {
        ChatTaskScheduler::instance().markTaskCompleted(m_executingScheduledTaskId, true);
        m_executingScheduledTaskId.clear();
    }
}

void ChatManager::failAgentRequestStatus(const QUuid &messageID, const QString &error)
{
    m_agentRequestStatuses[messageID] = GuideAutoNextStatus(GuideAutoNextStatus::Failed, error);
    emit agentRequestStatusChanged(messageID, m_agentRequestStatuses[messageID]);

    // If a scheduled task was executing, mark it as failed
    if (!m_executingScheduledTaskId.isEmpty()) {
        ChatTaskScheduler::instance().markTaskCompleted(m_executingScheduledTaskId, false);
        m_executingScheduledTaskId.clear();
    }
}

void ChatManager::cancelAgentRequestStatus(const QUuid &messageID)
{
    m_agentRequestStatuses[messageID] = GuideAutoNextStatus(GuideAutoNextStatus::Cancelled, "Cancelled");
    emit agentRequestStatusChanged(messageID, m_agentRequestStatuses[messageID]);

    // If a scheduled task was executing, mark it as failed (cancelled)
    if (!m_executingScheduledTaskId.isEmpty()) {
        ChatTaskScheduler::instance().markTaskCompleted(m_executingScheduledTaskId, false);
        m_executingScheduledTaskId.clear();
    }
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

// Handle scheduled task ready for execution
void ChatManager::onScheduledTaskReady(const QString &taskId, const QString &prompt)
{
    qCDebug(log_ai_chat) << "Executing scheduled task:" << taskId << "with prompt:" << prompt;

    // Clear any previous executing task ID (shouldn't happen, but just in case)
    m_executingScheduledTaskId.clear();

    // Store the task ID so we can mark it as completed later
    m_executingScheduledTaskId = taskId;

    // Execute the task by sending it as a user message
    // The scheduler will mark it as completed/failed based on the outcome
    sendMessage(prompt);
}
