#ifndef CHAT_SKILL_BAR_H
#define CHAT_SKILL_BAR_H

#include <QWidget>
#include <QHBoxLayout>
#include <QPushButton>
#include <QScrollArea>
#include "ai/ChatTypes.h"

/**
 * @brief Horizontal scrollable bar of skill quick-action buttons.
 */
class ChatSkillBar : public QWidget
{
    Q_OBJECT

public:
    explicit ChatSkillBar(QWidget *parent = nullptr);

    void setSkills(const QList<ChatSkill> &skills);

signals:
    void skillClicked(const QString &skillId);

private:
    void rebuildButtons();

    QHBoxLayout *m_layout;
    QScrollArea *m_scrollArea;
    QWidget *m_buttonContainer;
    QHBoxLayout *m_buttonLayout;
    QList<ChatSkill> m_skills;
};

#endif // CHAT_SKILL_BAR_H
