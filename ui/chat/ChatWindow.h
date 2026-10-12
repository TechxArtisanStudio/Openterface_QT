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

#ifndef CHAT_WINDOW_H
#define CHAT_WINDOW_H

#include <QWidget>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QStackedWidget>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>

class ChatBubbleWidget;
class ChatInputWidget;
class ChatPlanCardWidget;
class ChatEmptyStateWidget;

/**
 * @brief AI Chat companion window docked beside the main window.
 *
 * Ported from MacOS ChatWindowRootView.swift.
 * Contains mode selector, skill bar, plan card, message scroll, and input area.
 */
class ChatWindow : public QWidget
{
    Q_OBJECT

public:
    explicit ChatWindow(QWidget *parent = nullptr);
    ~ChatWindow() override;

    /// Clear all messages and reset UI
    void clearAll();

    /// Scroll to bottom of messages
    void scrollToBottom();

signals:
    /// Mode changed by user
    void modeChanged(int modeIndex);

    /// New session requested
    void newSessionRequested();

    /// Trace dialog requested
    void traceRequested();

    /// Plan approved
    void planApproved();

    /// Plan cleared
    void planCleared();

    /// Skill selected
    void skillSelected(const QString &skillId);

    /// Message send requested
    void messageSendRequested(const QString &text, const QString &attachmentPath);

    /// Stop sending requested
    void stopRequested();

    /// Guide action execute requested
    void guideActionRequested(int messageIndex, bool autoNext);

    /// Guide step completed by user
    void guideStepCompleted(const QString &description);

private slots:
    void onSendClicked();
    void onStopClicked();
    void onNewSessionClicked();
    void onModeChanged(int index);
    void onTraceClicked();
    void onPlanApproved();
    void onPlanClearClicked();
    void onSkillClicked(const QString &skillId);
    void onQuickReplyClicked(const QString &text);
    void onGuideExecuteClicked(int messageIndex);
    void onGuideExecuteNextClicked(int messageIndex);
    void onGuideCompleteClicked(int messageIndex);

    void onTargetOsSelected(const QString &system);

private:
    void setupUI();
    void updateModeUI();
    void refreshBubbles();
    void updatePlanCard();
    void updateEmptyState();
    void updateTargetOsButton();
    ChatBubbleWidget *createBubbleWidget(int index);

    // Layout
    QVBoxLayout *m_mainLayout;
    QHBoxLayout *m_topBar;
    QComboBox *m_modeCombo;
    QPushButton *m_targetOsBtn;
    QPushButton *m_newSessionBtn;
    QPushButton *m_traceBtn;
    ChatEmptyStateWidget *m_emptyState;
    ChatPlanCardWidget *m_planCard;
    QStackedWidget *m_stackedArea;
    QScrollArea *m_scrollArea;
    QWidget *m_messageContainer;
    QVBoxLayout *m_messageLayout;
    QLabel *m_errorLabel;
    QPushButton *m_errorCopyBtn;
    QWidget *m_errorRow;
    QLabel *m_statusLabel;
    ChatInputWidget *m_inputWidget;

    // Bubble widgets (parallel to ChatManager messages)
    QList<ChatBubbleWidget *> m_bubbleWidgets;

    // Guide overlay rect
    QRectF m_guideOverlayRect;
    QString m_guideOverlayTool;
};

#endif // CHAT_WINDOW_H
