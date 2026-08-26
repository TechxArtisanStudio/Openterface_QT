/* generated — contains angle-bracket literals via unicode escapes */
#ifndef CHAT_EMPTY_STATE_WIDGET_H
#define CHAT_EMPTY_STATE_WIDGET_H

#include <QWidget>
#include <QVBoxLayout>
#include <QPushButton>
#include <QLabel>
#include "ai/ChatTypes.h"

/**
 * @brief Centered "quick links" shown in the chat background when no messages
 * exist. Each skill becomes a clickable button; clicking it auto-submits the
 * skill's prompt to the agent.
 */
class ChatEmptyStateWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ChatEmptyStateWidget(QWidget *parent = nullptr);

    void setSkills(const QList<ChatSkill> &skills);

signals:
    void skillClicked(const QString &skillId);

private:
    void rebuildButtons();

    QVBoxLayout *m_layout;
    QWidget *m_buttonContainer;
    QVBoxLayout *m_buttonLayout;
    QList<ChatSkill> m_skills;
};

#endif // CHAT_EMPTY_STATE_WIDGET_H
