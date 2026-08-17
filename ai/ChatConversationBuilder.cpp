#include "ChatConversationBuilder.h"
#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(log_ai_chat)
Q_LOGGING_CATEGORY(log_ai_chat, "openterface.ai.chat")

ChatConversationBuilder::ChatConversationBuilder(QObject *parent)
    : QObject(parent)
{
}

ChatConversationBuilder &ChatConversationBuilder::instance()
{
    static ChatConversationBuilder inst;
    return inst;
}

QList<ChatApiMessage> ChatConversationBuilder::buildConversation(
    const QString &systemPrompt,
    const QList<ChatMessage> &messages,
    bool includeAgentTools,
    const QString &imageDataURL) const
{
    QList<ChatApiMessage> conversation;

    // Add system prompt
    if (!systemPrompt.trimmed().isEmpty()) {
        conversation.append(ChatApiMessage::textMessage(ChatRole::System, systemPrompt));
    }

    // Add agent tool instruction if agentic mode
    if (includeAgentTools) {
        conversation.append(ChatApiMessage::textMessage(
            ChatRole::System, agentToolInstruction()));
    }

    // Convert chat messages to API messages
    for (int i = 0; i < messages.size(); ++i) {
        const auto &msg = messages[i];
        bool isLastUser = (msg.role == ChatRole::User) && (i == messages.size() - 1);

        if (isLastUser && !imageDataURL.isEmpty()) {
            // Attach image to the last user message
            conversation.append(ChatApiMessage::multimodalMessage(
                msg.role, msg.content, imageDataURL));
        } else if (!msg.attachmentFilePath.isEmpty() && isLastUser) {
            // Has attachment but no data URL provided yet
            conversation.append(ChatApiMessage::textMessage(msg.role, msg.content));
        } else {
            conversation.append(ChatApiMessage::textMessage(msg.role, msg.content));
        }
    }

    return conversation;
}

QString ChatConversationBuilder::agentToolInstruction() const
{
    return QStringLiteral(
        "You are the Openterface AI Agent.\n\n"
        "When you need to interact with the target computer, return a JSON object with tool_calls.\n\n"
        "Available tools:\n"
        "- capture_screen: Take a screenshot of the target screen. No arguments needed.\n"
        "- move_mouse: Move the mouse cursor. Args: x (0.0-1.0), y (0.0-1.0)\n"
        "- left_click: Left-click at position. Args: x (0.0-1.0), y (0.0-1.0)\n"
        "- right_click: Right-click at position. Args: x (0.0-1.0), y (0.0-1.0)\n"
        "- double_click: Double-click at position. Args: x (0.0-1.0), y (0.0-1.0)\n"
        "- left_drag: Drag from current position. Args: x (0.0-1.0), y (0.0-1.0)\n"
        "- type_text: Type text on target keyboard. Args: text (string)\n"
        "- press_key: Press key combination. Args: keys (string like \"ctrl+l\")\n\n"
        "To use a tool, respond with JSON:\n"
        "{\"tool_calls\": [{\"tool\": \"tool_name\", \"arg1\": value1, ...}]}\n\n"
        "You can include multiple tool calls in one response.\n"
        "If you don't need tools, respond with normal text."
    );
}
