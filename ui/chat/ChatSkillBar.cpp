#include "ChatSkillBar.h"

ChatSkillBar::ChatSkillBar(QWidget *parent)
    : QWidget(parent)
{
    m_layout = new QHBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);

    m_scrollArea = new QScrollArea();
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setFixedHeight(36);

    m_buttonContainer = new QWidget();
    m_buttonLayout = new QHBoxLayout(m_buttonContainer);
    m_buttonLayout->setContentsMargins(0, 0, 0, 0);
    m_buttonLayout->setSpacing(4);

    m_scrollArea->setWidget(m_buttonContainer);
    m_layout->addWidget(m_scrollArea);
}

void ChatSkillBar::setSkills(const QList<ChatSkill> &skills)
{
    m_skills = skills;
    rebuildButtons();
}

void ChatSkillBar::rebuildButtons()
{
    // Clear existing
    QLayoutItem *item;
    while ((item = m_buttonLayout->takeAt(0)) != nullptr) {
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }

    for (const auto &skill : m_skills) {
        auto *btn = new QPushButton(skill.displayLabel());
        btn->setStyleSheet("QPushButton { padding: 4px 12px; border-radius: 4px; "
                           "background-color: #e0e0e0; border: 1px solid #ccc; }"
                           "QPushButton:hover { background-color: #d0d0d0; }");
        btn->setCursor(Qt::PointingHandCursor);
        connect(btn, &QPushButton::clicked, this, [this, skill]() {
            emit skillClicked(skill.id);
        });
        m_buttonLayout->addWidget(btn);
    }
    m_buttonLayout->addStretch();
}
