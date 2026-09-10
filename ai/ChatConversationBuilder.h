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

#ifndef CHAT_CONVERSATION_BUILDER_H
#define CHAT_CONVERSATION_BUILDER_H

#include <QObject>
#include <QList>
#include <QString>
#include "ChatTypes.h"

/**
 * @brief Builds conversation message arrays for the AI API.
 *
 * Converts ChatMessage history into ChatApiMessage arrays suitable for
 * the OpenAI-compatible chat completions API.
 */
class ChatConversationBuilder : public QObject
{
    Q_OBJECT

public:
    static ChatConversationBuilder &instance();

    /**
     * @brief Build a conversation from chat history for standard/agentic mode.
     * @param systemPrompt The system prompt
     * @param messages The chat message history
     * @param includeAgentTools Whether to include agent tool definitions
     * @param imageDataURL Optional base64 data URL for image attachment
     * @return List of API messages ready for the API call
     */
    QList<ChatApiMessage> buildConversation(
        const QString &systemPrompt,
        const QList<ChatMessage> &messages,
        bool includeAgentTools,
        const QString &imageDataURL = QString()
    ) const;

    /**
     * @brief Build the agent tools instruction text.
     */
    QString agentToolInstruction() const;

private:
    explicit ChatConversationBuilder(QObject *parent = nullptr);

    /**
     * @brief Remove tool-call JSON from assistant message content.
     *
     * The UI keeps the full content for display, but when the message is sent
     * back to the API, the raw JSON would cause the model to see its own
     * tool_call and potentially repeat it. This returns just the human-readable
     * portion, preserving any text before or after the JSON block.
     */
    QString stripToolCallJson(const QString &text) const;

    /**
     * @brief Build the available tools string based on enabled settings.
     * @return Formatted string listing available tools with descriptions
     */
    QString buildAvailableToolsString() const;
};

#endif // CHAT_CONVERSATION_BUILDER_H
