/* generated — contains angle-bracket literals via unicode escapes */
#include "ChatEmptyStateWidget.h"
#include <QEvent>
#include <QMouseEvent>
#include <QGridLayout>
#include <QResizeEvent>

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

    // Button container — buttons laid out in 2 columns
    m_buttonContainer = new QWidget();
    m_buttonContainer->setMaximumWidth(600);
    m_buttonLayout = new QGridLayout(m_buttonContainer);
    m_buttonLayout->setContentsMargins(0, 0, 0, 0);
    m_buttonLayout->setSpacing(12);
    // Make both columns expand equally
    m_buttonLayout->setColumnStretch(0, 1);
    m_buttonLayout->setColumnStretch(1, 1);
    m_layout->addWidget(m_buttonContainer, 0, Qt::AlignHCenter);

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

    int row = 0;
    int col = 0;
    for (const auto &skill : m_skills) {
        // Create a card-style widget for each skill
        auto *card = new QWidget();
        card->setCursor(Qt::PointingHandCursor);
        card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
        card->setStyleSheet(
            "QWidget {"
            "  padding: 12px 16px;"
            "  border-radius: 10px;"
            "  background-color: #f8f9fa;"
            "  border: 1px solid #e0e0e0;"
            "}"
            "QWidget:hover {"
            "  background-color: #eef1f5;"
            "  border: 1px solid #b0b8c4;"
            "}");

        auto *cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(12, 12, 12, 12);
        cardLayout->setSpacing(4);

        // Title
        auto *titleLabel = new QLabel(skill.displayLabel());
        titleLabel->setWordWrap(true);
        titleLabel->setStyleSheet(
            "font-size: 14px; font-weight: 600; color: #2c3e50; "
            "background: transparent; border: none;");
        cardLayout->addWidget(titleLabel);

        // Description (if available)
        if (!skill.description.isEmpty()) {
            auto *descLabel = new QLabel(skill.description);
            descLabel->setWordWrap(true);
            descLabel->setStyleSheet(
                "font-size: 12px; color: #6c757d; "
                "background: transparent; border: none;");
            cardLayout->addWidget(descLabel);
        }

        // Make the card clickable
        card->installEventFilter(this);
        card->setProperty("skillId", skill.id);

        // Add to grid layout (2 columns)
        m_buttonLayout->addWidget(card, row, col);
        col++;
        if (col >= 2) {
            col = 0;
            row++;
        }
    }
}

bool ChatEmptyStateWidget::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonPress) {
        QWidget *card = qobject_cast<QWidget*>(obj);
        if (card) {
            QString skillId = card->property("skillId").toString();
            if (!skillId.isEmpty()) {
                emit skillClicked(skillId);
                return true;
            }
        }
    }
    return QWidget::eventFilter(obj, event);
}
