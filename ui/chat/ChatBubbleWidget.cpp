#include "ChatBubbleWidget.h"
#include "QuickReplyWidget.h"
#include <QVBoxLayout>
#include <QPixmap>
#include <QClipboard>
#include <QApplication>
#include <QFileInfo>

ChatBubbleWidget::ChatBubbleWidget(QWidget *parent)
    : QWidget(parent)
{
    setupUI();
}

void ChatBubbleWidget::setupUI()
{
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(8, 4, 8, 4);
    m_layout->setSpacing(2);

    m_roleLabel = new QLabel();
    m_roleLabel->setStyleSheet("font-weight: bold; font-size: 11px; color: #666;");
    m_layout->addWidget(m_roleLabel);

    m_contentLabel = new QLabel();
    m_contentLabel->setWordWrap(true);
    m_contentLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_contentLabel->setStyleSheet("padding: 8px; border-radius: 8px;");
    m_layout->addWidget(m_contentLabel);

    m_attachmentLabel = new QLabel();
    m_attachmentLabel->setVisible(false);
    m_layout->addWidget(m_attachmentLabel);

    // Quick reply container
    m_quickReplyContainer = new QWidget();
    m_quickReplyLayout = new QHBoxLayout(m_quickReplyContainer);
    m_quickReplyLayout->setContentsMargins(0, 0, 0, 0);
    m_quickReplyLayout->setSpacing(4);
    m_quickReplyContainer->setVisible(false);
    m_layout->addWidget(m_quickReplyContainer);

    // Action buttons
    m_actionLayout = new QHBoxLayout();
    m_actionLayout->setContentsMargins(0, 0, 0, 0);
    m_actionLayout->setSpacing(4);
    m_layout->addLayout(m_actionLayout);
}

void ChatBubbleWidget::setMessage(const ChatMessage &message, int index)
{
    m_message = message;
    m_messageIndex = index;
    updateContent();
}

void ChatBubbleWidget::updateContent()
{
    // Role label
    switch (m_message.role) {
    case ChatRole::User:
        m_roleLabel->setText("You");
        m_contentLabel->setStyleSheet(
            "padding: 8px; border-radius: 8px; "
            "background-color: #d4e6ff; color: #000;");
        break;
    case ChatRole::Assistant:
        m_roleLabel->setText("AI Assistant");
        m_contentLabel->setStyleSheet(
            "padding: 8px; border-radius: 8px; "
            "background-color: #e8e8e8; color: #000;");
        break;
    case ChatRole::System:
        m_roleLabel->setText("System");
        m_contentLabel->setStyleSheet(
            "padding: 8px; border-radius: 8px; "
            "background-color: #fff3cd; color: #856404;");
        break;
    }

    // Content
    m_contentLabel->setText(m_message.content);

    // Attachment
    if (!m_message.attachmentFilePath.isEmpty()) {
        QPixmap pix(m_message.attachmentFilePath);
        if (!pix.isNull()) {
            int maxDim = 200;
            QPixmap scaled = pix.scaled(maxDim, maxDim, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            m_attachmentLabel->setPixmap(scaled);
            m_attachmentLabel->setVisible(true);
        } else {
            m_attachmentLabel->setText(QString("[Attachment: %1]")
                .arg(QFileInfo(m_message.attachmentFilePath).fileName()));
            m_attachmentLabel->setVisible(true);
        }
    } else {
        m_attachmentLabel->setVisible(false);
    }

    // Quick replies
    clearQuickReplies();
    if (!m_message.quickReplies.isEmpty() && m_message.role == ChatRole::Assistant) {
        m_quickReplyContainer->setVisible(true);
        for (const auto &qr : m_message.quickReplies) {
            auto *chip = new QuickReplyWidget(qr.label, this);
            connect(chip, &QuickReplyWidget::clicked, this, [this, qr]() {
                emit quickReplyClicked(qr.sendText);
            });
            m_quickReplyLayout->addWidget(chip);
        }
    }

    // Guide action buttons
    // Clear previous action buttons
    QLayoutItem *item;
    while ((item = m_actionLayout->takeAt(0)) != nullptr) {
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }

    if (!m_message.guideActionRect.isNull() || !m_message.guideShortcut.isEmpty()) {
        auto *execBtn = new QPushButton("Execute");
        auto *execNextBtn = new QPushButton("Execute & Next");
        auto *completeBtn = new QPushButton("I Did This");

        int idx = m_messageIndex;
        connect(execBtn, &QPushButton::clicked, this, [this, idx]() {
            emit guideExecuteClicked(idx);
        });
        connect(execNextBtn, &QPushButton::clicked, this, [this, idx]() {
            emit guideExecuteNextClicked(idx);
        });
        connect(completeBtn, &QPushButton::clicked, this, [this, idx]() {
            emit guideCompleteClicked(idx);
        });

        m_actionLayout->addWidget(execBtn);
        m_actionLayout->addWidget(execNextBtn);
        m_actionLayout->addWidget(completeBtn);
        m_actionLayout->addStretch();
    }

    // Copy button for assistant messages
    if (m_message.role == ChatRole::Assistant && !m_message.content.isEmpty()) {
        auto *copyBtn = new QPushButton("Copy");
        connect(copyBtn, &QPushButton::clicked, this, [this]() {
            QApplication::clipboard()->setText(m_message.content);
        });
        m_actionLayout->addWidget(copyBtn);
    }
}

void ChatBubbleWidget::clearQuickReplies()
{
    QLayoutItem *item;
    while ((item = m_quickReplyLayout->takeAt(0)) != nullptr) {
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }
    m_quickReplyContainer->setVisible(false);
}
