#include "ChatPlanCardWidget.h"
#include <QHBoxLayout>

ChatPlanCardWidget::ChatPlanCardWidget(QWidget *parent)
    : QWidget(parent)
{
    setStyleSheet("background-color: #f0f0f0; border: 1px solid #ccc; border-radius: 8px; padding: 8px;");

    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(8, 8, 8, 8);
    m_layout->setSpacing(4);

    m_titleLabel = new QLabel("Execution Plan");
    m_titleLabel->setStyleSheet("font-weight: bold; font-size: 13px;");
    m_layout->addWidget(m_titleLabel);

    m_summaryLabel = new QLabel();
    m_summaryLabel->setWordWrap(true);
    m_layout->addWidget(m_summaryLabel);

    m_tasksLabel = new QLabel();
    m_tasksLabel->setWordWrap(true);
    m_tasksLabel->setStyleSheet("font-size: 11px; color: #555;");
    m_layout->addWidget(m_tasksLabel);

    auto *btnLayout = new QHBoxLayout();
    m_approveBtn = new QPushButton("Approve & Run");
    m_approveBtn->setStyleSheet("background-color: #28a745; color: white; padding: 6px;");
    m_clearBtn = new QPushButton("Clear");
    btnLayout->addWidget(m_approveBtn);
    btnLayout->addWidget(m_clearBtn);
    btnLayout->addStretch();
    m_layout->addLayout(btnLayout);

    connect(m_approveBtn, &QPushButton::clicked, this, &ChatPlanCardWidget::approveClicked);
    connect(m_clearBtn, &QPushButton::clicked, this, &ChatPlanCardWidget::clearClicked);
}

void ChatPlanCardWidget::setPlan(const ChatExecutionPlan &plan)
{
    m_plan = plan;
    updateContent();
}

void ChatPlanCardWidget::updateContent()
{
    m_summaryLabel->setText(m_plan.summary);

    QStringList taskLines;
    for (int i = 0; i < m_plan.tasks.size(); ++i) {
        const auto &task = m_plan.tasks[i];
        QString status;
        switch (task.status) {
        case ChatTaskStatus::Pending:   status = "○"; break;
        case ChatTaskStatus::Approved:  status = "◉"; break;
        case ChatTaskStatus::Running:   status = "▶"; break;
        case ChatTaskStatus::Completed: status = "✓"; break;
        case ChatTaskStatus::Failed:    status = "✗"; break;
        case ChatTaskStatus::Skipped:   status = "-"; break;
        }
        taskLines.append(QString("%1 %2. %3").arg(status).arg(i + 1).arg(task.title));
    }
    m_tasksLabel->setText(taskLines.join("\n"));

    bool canApprove = (m_plan.status == ChatPlanStatus::AwaitingApproval);
    m_approveBtn->setEnabled(canApprove);

    if (canApprove) {
        // Green button when clickable
        m_approveBtn->setText("Approve & Run");
        m_approveBtn->setStyleSheet("background-color: #28a745; color: white; padding: 6px;");
    } else {
        QString statusText = chatPlanStatusToString(m_plan.status);
        // Capitalize first letter
        if (!statusText.isEmpty()) {
            statusText[0] = statusText[0].toUpper();
        }
        m_approveBtn->setText(statusText);

        // Normal grey button with green text when completed
        if (m_plan.status == ChatPlanStatus::Completed) {
            m_approveBtn->setStyleSheet("background-color: #f0f0f0; color: #28a745; padding: 6px; border: 1px solid #ccc;");
        } else {
            // Normal grey button for other non-clickable states
            m_approveBtn->setStyleSheet("background-color: #f0f0f0; color: #333; padding: 6px; border: 1px solid #ccc;");
        }
    }
}
