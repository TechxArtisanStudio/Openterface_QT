/* generated — contains angle-bracket literals via unicode escapes */
#include "ChatEmptyStateWidget.h"

ChatEmptyStateWidget::ChatEmptyStateWidget(QWidget *parent)
    : QWidget(parent)
{
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(20, 20, 20, 20);

    // Push buttons to vertical center
    m_layout->addStretch(1);

    // Title
    auto *titleLabel = new QLabel(QStringLiteral("AI Assistant"));
    titleLabel->setAlignment(Qt::AlignCenter);
    titleLabel->setStyleSheet(
        "font-size: 18px; font-weight: bold; color: #555; margin-bottom: 4px;");
    m_layout->addWidget(titleLabel);

    // Subtitle
    auto *subtitleLabel = new QLabel(
        QStringLiteral("Click a quick action below, or type a message"));
    subtitleLabel->setAlignment(Qt::AlignCenter);
    subtitleLabel->setStyleSheet(
        "font-size: 12px; color: #888; margin-bottom: 16px;");
    m_layout->addWidget(subtitleLabel);

    // Button container — buttons laid out vertically, centered
    m_buttonContainer = new QWidget();
    m_buttonLayout = new QVBoxLayout(m_buttonContainer);
    m_buttonLayout->setContentsMargins(0, 0, 0, 0);
    m_buttonLayout->setSpacing(8);
    m_buttonLayout->setAlignment(Qt::AlignCenter);
    m_layout->addWidget(m_buttonContainer, 0, Qt::AlignCenter);

    // Push buttons to vertical center
    m_layout->addStretch(1);
}

void ChatEmptyStateWidget::setSkills(const QList<ChatSkill> &skills)
{
    m_skills = skills;
    rebuildButtons();
}

void ChatEmptyStateWidget::rebuildButtons()
{
    // Clear existing buttons
    QLayoutItem *item;
    while ((item = m_buttonLayout->takeAt(0)) != nullptr) {
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }

    for (const auto &skill : m_skills) {
        auto *btn = new QPushButton(skill.displayLabel());
        btn->setCursor(Qt::PointingHandCursor);
        btn->setMinimumWidth(180);
        btn->setStyleSheet(
            "QPushButton {"
            "  padding: 10px 20px;"
            "  border-radius: 8px;"
            "  background-color: #f0f0f0;"
            "  border: 1px solid #ccc;"
            "  font-size: 13px;"
            "  color: #333;"
            "}"
            "QPushButton:hover {"
            "  background-color: #e0e0e0;"
            "  border: 1px solid #999;"
            "}"
            "QPushButton:pressed {"
            "  background-color: #d0d0d0;"
            "}");
        connect(btn, &QPushButton::clicked, this, [this, skill]() {
            emit skillClicked(skill.id);
        });
        m_buttonLayout->addWidget(btn, 0, Qt::AlignCenter);
    }
}
