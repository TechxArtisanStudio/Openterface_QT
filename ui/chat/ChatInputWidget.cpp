#include "ChatInputWidget.h"
#include <QVBoxLayout>
#include <QPixmap>

ChatInputWidget::ChatInputWidget(QWidget *parent)
    : QWidget(parent)
{
    setupUI();
}

void ChatInputWidget::setupUI()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    // Attachment preview
    auto *attachLayout = new QHBoxLayout();
    m_attachmentPreview = new QLabel();
    m_attachmentPreview->setVisible(false);
    m_removeAttachmentBtn = new QPushButton("X");
    m_removeAttachmentBtn->setFixedSize(20, 20);
    m_removeAttachmentBtn->setVisible(false);
    attachLayout->addWidget(m_attachmentPreview);
    attachLayout->addWidget(m_removeAttachmentBtn);
    attachLayout->addStretch();
    layout->addLayout(attachLayout);

    // Text edit + buttons
    auto *inputLayout = new QHBoxLayout();
    m_textEdit = new ChatTextEdit();
    m_textEdit->setMaximumHeight(120);
    m_textEdit->setPlaceholderText("Type a message... (Shift+Enter to send)");

    auto *btnLayout = new QVBoxLayout();
    m_sendBtn = new QPushButton("Send");
    m_stopBtn = new QPushButton("Stop");
    m_stopBtn->setVisible(false);
    m_stopBtn->setStyleSheet("background-color: #dc3545; color: white;");
    btnLayout->addWidget(m_sendBtn);
    btnLayout->addWidget(m_stopBtn);

    inputLayout->addWidget(m_textEdit, 1);
    inputLayout->addLayout(btnLayout);
    layout->addLayout(inputLayout);

    // Send via Shift+Enter / Ctrl+Enter — handled by ChatTextEdit::keyPressEvent
    connect(m_textEdit, &ChatTextEdit::sendRequested, this, &ChatInputWidget::onSendClicked);

    connect(m_sendBtn, &QPushButton::clicked, this, &ChatInputWidget::onSendClicked);
    connect(m_stopBtn, &QPushButton::clicked, this, [this]() { emit stopRequested(); });
    connect(m_removeAttachmentBtn, &QPushButton::clicked, this, &ChatInputWidget::onRemoveAttachment);
}

QString ChatInputWidget::text() const
{
    return m_textEdit->toPlainText().trimmed();
}

QString ChatInputWidget::attachmentPath() const
{
    return m_attachmentPath;
}

void ChatInputWidget::clear()
{
    m_textEdit->clear();
    onRemoveAttachment();
}

void ChatInputWidget::setSending(bool sending)
{
    m_isSending = sending;
    updateButtonState();
}

void ChatInputWidget::setPlaceholder(const QString &text)
{
    m_textEdit->setPlaceholderText(text);
}

void ChatInputWidget::setAttachment(const QString &filePath)
{
    m_attachmentPath = filePath;
    QPixmap pix(filePath);
    if (!pix.isNull()) {
        m_attachmentPreview->setPixmap(pix.scaled(60, 60, Qt::KeepAspectRatio));
    }
    m_attachmentPreview->setVisible(true);
    m_removeAttachmentBtn->setVisible(true);
}

void ChatInputWidget::onSendClicked()
{
    if (!text().isEmpty() || !m_attachmentPath.isEmpty()) {
        emit sendRequested();
    }
}

void ChatInputWidget::onRemoveAttachment()
{
    m_attachmentPath.clear();
    m_attachmentPreview->setVisible(false);
    m_removeAttachmentBtn->setVisible(false);
    emit attachmentRemoved();
}

void ChatInputWidget::updateButtonState()
{
    m_sendBtn->setVisible(!m_isSending);
    m_stopBtn->setVisible(m_isSending);
    m_textEdit->setEnabled(!m_isSending);
}
