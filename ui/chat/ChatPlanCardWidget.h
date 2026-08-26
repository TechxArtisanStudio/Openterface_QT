#ifndef CHAT_PLAN_CARD_WIDGET_H
#define CHAT_PLAN_CARD_WIDGET_H

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include "ai/ChatTypes.h"

/**
 * @brief Displays the current execution plan with approve/clear buttons.
 */
class ChatPlanCardWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ChatPlanCardWidget(QWidget *parent = nullptr);

    void setPlan(const ChatExecutionPlan &plan);

signals:
    void approveClicked();
    void clearClicked();

private:
    void updateContent();

    QVBoxLayout *m_layout;
    QLabel *m_titleLabel;
    QLabel *m_summaryLabel;
    QLabel *m_tasksLabel;
    QPushButton *m_approveBtn;
    QPushButton *m_clearBtn;

    ChatExecutionPlan m_plan;
};

#endif // CHAT_PLAN_CARD_WIDGET_H
