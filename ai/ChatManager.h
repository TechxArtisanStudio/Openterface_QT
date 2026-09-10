/*
* ========================================================================== *
*                                                                            *
*    This file is part of the Openterface Mini KVM App QT version            *
*                                                                            *
*    This program is free software: you can redistribute it and/or modify    *
*    it under the terms of the GNU General Public License as published by    *
*    the Free Software Foundation version 3.                                 *
*                                                                            *
*    This program is distributed in the hope that it will be useful, but     *
*    WITHOUT ANY WARRANTY; without even the implied warranty of              *
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU        *
*    General Public License for more details.                                *
*                                                                            *
*    You should have received a copy of the GNU General Public License       *
*    along with this program. If not, see <http://www.gnu.org/licenses/>.    *
*                                                                            *
* ========================================================================== *
*/

#ifndef CHAT_MANAGER_H
#define CHAT_MANAGER_H

#include <QObject>
#include <QList>
#include <QString>
#include <QFuture>
#include "ChatTypes.h"
#include "ChatAgentTypes.h"

class ChatApiClient;

/**
 * @brief Main AI Chat orchestrator singleton.
 *
 * Ported from MacOS ChatManager.swift. Manages the chat message history,
 * coordinates API calls, handles agent/planner/guide modes, and persists state.
 */
class ChatManager : public QObject
{
    Q_OBJECT

public:
    static ChatManager &instance();

    // ========================================================================
    // State
    // ========================================================================
    QList<ChatMessage> messages() const { return m_messages; }
    bool isSending() const { return m_isSending; }
    QString lastError() const { return m_lastError; }

    ChatExecutionPlan currentPlan() const { return m_currentPlan; }
    bool hasPlan() const { return m_hasPlan; }

    QList<ChatTaskTraceEntry> plannerTraceEntries() const { return m_plannerTraceEntries; }

    /// Target system (BIOS/UEFI, TextUI, Linux, ...) pinned for the active agent task.
    /// A fresh (non-"continue") user message snapshots the persisted setting; subsequent
    /// "continue" runs reuse that pin so the task keeps operating in the same mode.
    /// This prevents a mid-task set_target_system correction (e.g. the model briefly
    /// calling set_target_system("linux") while the target was still booting toward BIOS)
    /// from leaking into the "continue" run and causing it to drop screen images /
    /// use OCR on a BIOS screen.
    QString effectiveAgentTargetSystem() const;

    // ========================================================================
    // Actions
    // ========================================================================

    /// Send a user message
    void sendMessage(const QString &text, const QString &attachmentFilePath = QString());

    /// Cancel any in-progress API call
    void cancelSending();

    /// Clear all chat history
    void clearHistory();

    /// Approve the current plan for execution
    void approveCurrentPlan();

    /// Clear the current plan
    void clearCurrentPlan();

    /// Send a quick reply
    void sendQuickReply(const ChatQuickReply &reply);

    /// Run a skill
    void runSkill(const ChatSkill &skill);

    /// Re-run the last user prompt
    void rerunLastPrompt(bool clearHistory = true);

    // ========================================================================
    // Configuration
    // ========================================================================

    /// Get the current API configuration (returns empty if invalid)
    ChatAPIConfiguration currentChatAPIConfiguration() const;

    // ========================================================================
    // Guide mode
    // ========================================================================

    /// Execute a guide action from a guide message
    void executeGuideAction(const ChatMessage &message, bool autoNext);

    /// Complete a guide step and ask for next
    void completeGuideStepAndNext(const QString &stepDescription);

    /// Guide auto-next status for a message
    GuideAutoNextStatus guideAutoNextStatus(const QUuid &messageID) const;

signals:
    // ========================================================================
    // UI signals
    // ========================================================================

    /// Messages list changed
    void messagesChanged();

    /// A new message was appended
    void messageAppended(const ChatMessage &message);

    /// A message was updated (by index)
    void messageUpdated(int index, const ChatMessage &message);

    /// isSending state changed
    void sendingStateChanged(bool sending);

    /// Last error changed
    void lastErrorChanged(const QString &error);

    /// Current plan changed
    void planChanged();

    /// Planner trace entries changed
    void plannerTracesChanged();

    /// Guide auto-next status changed for a message
    void guideAutoNextStatusChanged(const QUuid &messageID, const GuideAutoNextStatus &status);

    /// Agent request status changed for a message
    void agentRequestStatusChanged(const QUuid &messageID, const GuideAutoNextStatus &status);

    /// Guide overlay requested
    void guideOverlayRequested(const QRectF &normalizedRect, const QString &tool);

    /// Guide overlay cleared
    void guideOverlayCleared();

    /// Screenshot captured for preview
    void screenshotCaptured(const QString &filePath);

    /// Plan task updated
    void planTaskUpdated(int taskIndex, const ChatTask &task);

private:
    explicit ChatManager(QObject *parent = nullptr);

    // ========================================================================
    // Send paths
    // ========================================================================

    /// Main send dispatcher
    void performSend();

    /// Standard/agentic loop send
    void performStandardSend(const ChatAPIConfiguration &config, bool agenticEnabled);

    /// Multi-agent planner send
    void performPlannerSend(const ChatAPIConfiguration &config);

    /// Guide mode send
    void performGuideSend(const ChatAPIConfiguration &config);

    /// Execute an approved plan
    void executeApprovedPlan();

    // ========================================================================
    // Helpers
    // ========================================================================

    void persistHistory();
    void loadHistory();
    void presentAIError(const QString &error);
    void appendAssistantMessage(const QString &content, const QString &attachment = QString());
    void appendAssistantMessage(const QString &content, int processingTimeMs, int inputTokens, int outputTokens);
    void appendAssistantMessage(const QString &content, const QString &attachment, int processingTimeMs, int inputTokens, int outputTokens);
    void startAgentRequestStatus(const QUuid &messageID);
    void completeAgentRequestStatus(const QUuid &messageID);
    void failAgentRequestStatus(const QUuid &messageID, const QString &error);
    void cancelAgentRequestStatus(const QUuid &messageID);
    void clearGuideOverlay();
    void startGuideAutoNextStatus(const QUuid &messageID);
    void cancelGuideAutoNextStatus(const QUuid &messageID);

    // ========================================================================
    // Scheduled task handling
    // ========================================================================

    /// Handle scheduled task ready for execution
    void onScheduledTaskReady(const QString &taskId, const QString &prompt);

    // ========================================================================
    // State
    // ========================================================================

    QList<ChatMessage> m_messages;
    bool m_isSending = false;
    QString m_lastError;
    ChatExecutionPlan m_currentPlan;
    bool m_hasPlan = false;
    QList<ChatTaskTraceEntry> m_plannerTraceEntries;

    MainPlannerAgent m_plannerAgent;
    TaskAgentRegistry m_taskAgentRegistry;

    QHash<QUuid, GuideAutoNextStatus> m_guideAutoNextStatuses;
    QHash<QUuid, GuideAutoNextStatus> m_agentRequestStatuses;

    bool m_cancelRequested = false;

    // Track currently executing scheduled task
    QString m_executingScheduledTaskId;

    // Task-scoped target system pin. When a fresh (non-"continue") agentic
    // user message starts an agent run, m_taskTargetSystem is snapshotted
    // from GlobalSetting::getChatTargetSystem() and stays fixed for every
    // subsequent "continue" of the same task. This guarantees that e.g. a
    // "boot into BIOS and configure X" task keeps running with BIOS/TextUI
    // behavior (capture_screen vision, no OCR substitution) even if the model
    // briefly wrote a different value via set_target_system mid-task.
    QString m_taskTargetSystem;
    bool m_hasTaskTargetSystem = false;
};

#endif // CHAT_MANAGER_H
