/*
* ========================================================================== *
*                                                                            *
*    This file is part of the Openterface Mini KVM App QT version            *
*                                                                            *
*    Copyright (C) 2024   <info@openterface.com>                             *
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

#ifndef CHAT_TOOL_EXECUTION_H
#define CHAT_TOOL_EXECUTION_H

#include <QObject>
#include <QString>
#include <QList>
#include "ChatTypes.h"

/**
 * @brief Parses and dispatches agentic tool calls from AI responses.
 *
 * Ported from MacOS ChatToolExecutionService.swift.
 * Supports: capture_screen, move_mouse, left_click, right_click,
 * double_click, left_drag, type_text, press_key, run_bash.
 */
class ChatToolExecution : public QObject
{
    Q_OBJECT

public:
    static ChatToolExecution &instance();

    /**
     * @brief Parse tool calls from assistant response text.
     *
     * Looks for JSON with "tool_calls" array or single "tool" object.
     * Returns empty list if no tool calls found.
     */
    QList<AgentToolCall> parseToolCalls(const QString &text) const;

    /**
     * @brief Execute a list of tool calls and return aggregated result.
     */
    AgentToolExecutionResult executeToolCalls(const QList<AgentToolCall> &calls);

    /**
     * @brief Run a bash command and return the result string.
     */
    QString runBashCommand(const QString &command) const;

signals:
    /// Emitted when a tool produces a log message
    void toolLogMessage(const QString &message);

private:
    explicit ChatToolExecution(QObject *parent = nullptr);

    // Coordinate helpers
    static int normalizedToAbsolute(double value);
    static double absoluteToNormalized(int value);
    static double doubleArg(const QVariant &value, bool *ok = nullptr);
    static int intArg(const QVariant &value, bool *ok = nullptr);

    // Click with coordinate resolution
    struct ClickPoint { int x; int y; };
    ClickPoint resolveClick(int button, const QVariantMap &args, bool isDoubleClick);

    // Drag point resolution
    struct DragPoints { int startX; int startY; int endX; int endY; };
    DragPoints resolveDragPoints(const QVariantMap &args, bool *ok);
};

#endif // CHAT_TOOL_EXECUTION_H
