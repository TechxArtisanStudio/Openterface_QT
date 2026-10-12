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

#ifndef CHAT_AGENT_TYPES_H
#define CHAT_AGENT_TYPES_H

#include <QObject>
#include <QString>
#include <QList>
#include <QHash>
#include "ChatTypes.h"

/**
 * @brief Abstract base for task agents used in multi-agent planner mode.
 *
 * Ported from MacOS TaskAgentExecutor protocol.
 * Each agent handles one kind of task (screen, typing, mouse, macro).
 */
class TaskAgentExecutor
{
public:
    virtual ~TaskAgentExecutor() = default;

    virtual QString agentName() const = 0;
    virtual QString toolName() const = 0;

    /// Get the task-specific prompt from settings
    virtual QString prompt() const = 0;

    /// Build conversation messages for this task
    virtual QList<ChatApiMessage> buildTaskConversation(
        const QString &systemPrompt,
        const ChatExecutionPlan &plan,
        const ChatTask &task,
        const QString &imageDataURL = QString()
    ) const = 0;

    /// Apply the AI response to update the task status/result
    virtual void applyResponse(const QString &response, ChatTask &task) = 0;
};

// ============================================================================
// MainPlannerAgent - Plans multi-step tasks
// ============================================================================
class MainPlannerAgent : public QObject
{
    Q_OBJECT

public:
    explicit MainPlannerAgent(int maxPlannerTasks = 8, QObject *parent = nullptr);

    /// Build the conversation for plan generation
    QList<ChatApiMessage> buildPlanningConversation(
        const QString &systemPrompt,
        const QString &plannerPrompt,
        const QString &userRequest,
        const QString &imageDataURL = QString()
    ) const;

    /// Parse the plan JSON from AI response
    bool parsePlan(const QString &responseText, const QString &goal,
                   ChatExecutionPlan &outPlan, QString &error) const;

    int maxPlannerTasks() const { return m_maxPlannerTasks; }

private:
    int m_maxPlannerTasks;
};

// ============================================================================
// ScreenTaskAgent - Verifies screen state via capture_screen
// ============================================================================
class ScreenTaskAgent : public TaskAgentExecutor
{
public:
    QString agentName() const override { return "screen"; }
    QString toolName() const override { return "capture_screen"; }
    QString prompt() const override;

    QList<ChatApiMessage> buildTaskConversation(
        const QString &systemPrompt,
        const ChatExecutionPlan &plan,
        const ChatTask &task,
        const QString &imageDataURL = QString()
    ) const override;

    void applyResponse(const QString &response, ChatTask &task) override;
};

// ============================================================================
// TypeTextTaskAgent - Determines text/shortcut to type
// ============================================================================
class TypeTextTaskAgent : public TaskAgentExecutor
{
public:
    QString agentName() const override { return "typing"; }
    QString toolName() const override { return "type_text"; }
    QString prompt() const override;

    QList<ChatApiMessage> buildTaskConversation(
        const QString &systemPrompt,
        const ChatExecutionPlan &plan,
        const ChatTask &task,
        const QString &imageDataURL = QString()
    ) const override;

    void applyResponse(const QString &response, ChatTask &task) override;
};

// ============================================================================
// PressKeyTaskAgent - Determines keyboard shortcut to press
// ============================================================================
class PressKeyTaskAgent : public TaskAgentExecutor
{
public:
    QString agentName() const override { return "typing"; }
    QString toolName() const override { return "press_key"; }
    QString prompt() const override;

    QList<ChatApiMessage> buildTaskConversation(
        const QString &systemPrompt,
        const ChatExecutionPlan &plan,
        const ChatTask &task,
        const QString &imageDataURL = QString()
    ) const override;

    void applyResponse(const QString &response, ChatTask &task) override;
};

// ============================================================================
// ScreenToMarkdownTaskAgent - Extracts text using OCR
// ============================================================================
class ScreenToMarkdownTaskAgent : public TaskAgentExecutor
{
public:
    QString agentName() const override { return "screen"; }
    QString toolName() const override { return "screen_to_markdown"; }
    QString prompt() const override;

    QList<ChatApiMessage> buildTaskConversation(
        const QString &systemPrompt,
        const ChatExecutionPlan &plan,
        const ChatTask &task,
        const QString &imageDataURL = QString()
    ) const override;

    void applyResponse(const QString &response, ChatTask &task) override;
};

// ============================================================================
// RunBashTaskAgent - Executes bash command on host
// ============================================================================
class RunBashTaskAgent : public TaskAgentExecutor
{
public:
    QString agentName() const override { return "system"; }
    QString toolName() const override { return "run_bash"; }
    QString prompt() const override;

    QList<ChatApiMessage> buildTaskConversation(
        const QString &systemPrompt,
        const ChatExecutionPlan &plan,
        const ChatTask &task,
        const QString &imageDataURL = QString()
    ) const override;

    void applyResponse(const QString &response, ChatTask &task) override;
};

// ============================================================================
// WebSearchTaskAgent - Searches the internet
// ============================================================================
class WebSearchTaskAgent : public TaskAgentExecutor
{
public:
    QString agentName() const override { return "system"; }
    QString toolName() const override { return "web_search"; }
    QString prompt() const override;

    QList<ChatApiMessage> buildTaskConversation(
        const QString &systemPrompt,
        const ChatExecutionPlan &plan,
        const ChatTask &task,
        const QString &imageDataURL = QString()
    ) const override;

    void applyResponse(const QString &response, ChatTask &task) override;
};

// ============================================================================
// MouseTaskAgent - Determines click/move coordinates
// ============================================================================
class MouseTaskAgent : public TaskAgentExecutor
{
public:
    explicit MouseTaskAgent(const QString &tool = "left_click");

    QString agentName() const override { return "mouse"; }
    QString toolName() const override { return m_toolName; }
    QString prompt() const override;

    QList<ChatApiMessage> buildTaskConversation(
        const QString &systemPrompt,
        const ChatExecutionPlan &plan,
        const ChatTask &task,
        const QString &imageDataURL = QString()
    ) const override;

    void applyResponse(const QString &response, ChatTask &task) override;

private:
    QString m_toolName;
};

// ============================================================================
// TaskAgentRegistry - Resolves the right agent for a given task
// ============================================================================
class TaskAgentRegistry
{
public:
    TaskAgentRegistry();

    /// Resolve the agent for a given task. Returns nullptr if not found.
    TaskAgentExecutor *resolve(const ChatTask &task) const;

private:
    QHash<QString, TaskAgentExecutor *> m_exactMappings;
    QHash<QString, TaskAgentExecutor *> m_toolMappings;

    static QString makeExactKey(const QString &agentName, const QString &toolName);
};

#endif // CHAT_AGENT_TYPES_H
