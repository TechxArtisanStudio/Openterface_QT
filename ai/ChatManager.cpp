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

Q_DECLARE_LOGGING_CATEGORY(log_ai_chat)
Q_LOGGING_CATEGORY(log_ai_chat, "openterface.ai.chat")

// Helper: synchronous wrapper around the callback-based API client
static ChatCompletionResult sendCompletionSync(
    const QUrl &baseURL, const QString &model, const QString &apiKey,
    const QList<ChatApiMessage> &messages, QString &outError)
{
    ChatCompletionResult result;
    QEventLoop loop;
    bool completed = false;

    ChatApiClient::instance().sendCompletion(baseURL, model, apiKey, messages, std::nullopt,
        [&](bool success, const ChatCompletionResult &r, const QString &error) {
            if (success) {
                result = r;
            } else {
                outError = error;
            }
            completed = true;
            loop.quit();
        });

    loop.exec();
    return result;
}

ChatManager::ChatManager(QObject *parent)
    : QObject(parent)
    , m_plannerAgent(GlobalSetting::instance().getChatAgentMaxIterations())
{
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

    m_lastError.clear();
    emit lastErrorChanged(m_lastError);

    QString storedContent = trimmed.isEmpty() ? "Attached screenshot" : trimmed;

    ChatMessage msg(ChatRole::User, storedContent, attachmentFilePath);
    m_messages.append(msg);

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

    // Get image data URL if last user message has attachment
    QString imageDataURL;
    for (int i = m_messages.size() - 1; i >= 0; --i) {
        if (m_messages[i].role == ChatRole::User && !m_messages[i].attachmentFilePath.isEmpty()) {
            imageDataURL = screenCapture.dataURLForImage(m_messages[i].attachmentFilePath);
            break;
        }
    }

    for (int iteration = 1; iteration <= maxIterations; ++iteration) {
        if (m_cancelRequested) break;

        QList<ChatApiMessage> conversation = builder.buildConversation(
            systemPrompt, m_messages, agenticEnabled, imageDataURL);

        // Trace the request
        tracing.appendAITrace(
            QString("REQUEST iteration=%1").arg(iteration),
            tracing.readableTraceParts(conversation));

        // Send API request
        ChatCompletionResult result;
        QString apiError;
        result = sendCompletionSync(config.baseURL, config.model, config.apiKey, conversation, apiError);

        if (m_cancelRequested) break;

        if (result.content.isEmpty()) {
            presentAIError("Empty response from AI");
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
            // No tool calls, append as regular response
            appendAssistantMessage(result.content);
            break;
        }

        // Execute tool calls
        appendAssistantMessage(result.content);

        AgentToolExecutionResult toolResult = toolExec.executeToolCalls(toolCalls);

        // Build tool result message
        QString toolResultContent = QString("TOOL_RESULT:\n%1").arg(toolResult.summary);
        ChatMessage toolMsg(ChatRole::User, toolResultContent, toolResult.attachmentFilePath);
        m_messages.append(toolMsg);
        emit messageAppended(toolMsg);

        // Update image data URL if we got a new screenshot
        if (!toolResult.attachmentFilePath.isEmpty()) {
            imageDataURL = screenCapture.dataURLForImage(toolResult.attachmentFilePath);
        }

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
    GlobalSetting &gs = GlobalSetting::instance();
    QString systemPrompt = gs.getChatSystemPrompt().trimmed();
    QString plannerPrompt = gs.getChatPlannerPrompt().trimmed();

    ChatScreenCapture &screenCapture = ChatScreenCapture::instance();
    ChatTracing &tracing = ChatTracing::instance();

    // Get the last user message
    QString userRequest;
    QString imageDataURL;
    for (int i = m_messages.size() - 1; i >= 0; --i) {
        if (m_messages[i].role == ChatRole::User) {
            userRequest = m_messages[i].content;
            if (!m_messages[i].attachmentFilePath.isEmpty()) {
                imageDataURL = screenCapture.dataURLForImage(m_messages[i].attachmentFilePath);
            }
            break;
        }
    }

    QList<ChatApiMessage> conversation = m_plannerAgent.buildPlanningConversation(
        systemPrompt, plannerPrompt, userRequest, imageDataURL);

    tracing.appendAITrace("PLANNER REQUEST", tracing.readableTraceParts(conversation));

    ChatCompletionResult result;
    QString apiError;
    result = sendCompletionSync(config.baseURL, config.model, config.apiKey, conversation, apiError);

    if (m_cancelRequested) {
        m_isSending = false;
        emit sendingStateChanged(false);
        return;
    }

    if (result.content.isEmpty()) {
        presentAIError("Empty response from planner");
        m_isSending = false;
        emit sendingStateChanged(false);
        return;
    }

    tracing.appendAITrace("PLANNER RESPONSE", result.content.left(500));

    ChatExecutionPlan plan;
    QString parseError;
    if (!m_plannerAgent.parsePlan(result.content, userRequest, plan, parseError)) {
        presentAIError(QString("Planner failed: %1").arg(parseError));
        m_isSending = false;
        emit sendingStateChanged(false);
        return;
    }

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

    for (int i = 0; i < m_currentPlan.tasks.size(); ++i) {
        if (m_cancelRequested) break;

        ChatTask &task = m_currentPlan.tasks[i];
        if (task.status != ChatTaskStatus::Approved) continue;

        task.status = ChatTaskStatus::Running;
        emit planTaskUpdated(i, task);
        persistHistory();

        // Resolve agent
        TaskAgentExecutor *agent = m_taskAgentRegistry.resolve(task);
        if (!agent) {
            task.status = ChatTaskStatus::Failed;
            task.resultSummary = QString("No agent found for %1/%2").arg(task.agentName, task.toolName);
            emit planTaskUpdated(i, task);
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

        // Send API request
        ChatCompletionResult result;
        QString apiError;
        result = sendCompletionSync(config.baseURL, config.model, config.apiKey, conversation, apiError);

        if (m_cancelRequested) break;

        // Apply response
        agent->applyResponse(result.content, task);
        task.inputTokenCount = result.inputTokenCount;
        task.outputTokenCount = result.outputTokenCount;

        emit planTaskUpdated(i, task);

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
    persistHistory();

    m_isSending = false;
    emit sendingStateChanged(false);
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
    ChatMessage msg(ChatRole::Assistant, content, attachment);
    m_messages.append(msg);
    emit messageAppended(msg);
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
