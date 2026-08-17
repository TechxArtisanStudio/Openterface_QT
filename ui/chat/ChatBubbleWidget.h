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

#ifndef CHAT_BUBBLE_WIDGET_H
#define CHAT_BUBBLE_WIDGET_H

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>
#include "ai/ChatTypes.h"

class QuickReplyWidget;

/**
 * @brief Displays a single chat message bubble.
 *
 * Ported from MacOS ChatBubbleView.swift.
 * Supports user/assistant/system roles, attachments, quick replies, and guide actions.
 */
class ChatBubbleWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ChatBubbleWidget(QWidget *parent = nullptr);

    /// Set the message and its index in the message list
    void setMessage(const ChatMessage &message, int index);

signals:
    void quickReplyClicked(const QString &text);
    void guideExecuteClicked(int messageIndex);
    void guideExecuteNextClicked(int messageIndex);
    void guideCompleteClicked(int messageIndex);
    void copyClicked(const QString &text);

private:
    void setupUI();
    void updateContent();
    void clearQuickReplies();

    QVBoxLayout *m_layout;
    QLabel *m_roleLabel;
    QLabel *m_contentLabel;
    QLabel *m_attachmentLabel;
    QHBoxLayout *m_actionLayout;
    QHBoxLayout *m_quickReplyLayout;
    QWidget *m_quickReplyContainer;

    ChatMessage m_message;
    int m_messageIndex = -1;
};

#endif // CHAT_BUBBLE_WIDGET_H
