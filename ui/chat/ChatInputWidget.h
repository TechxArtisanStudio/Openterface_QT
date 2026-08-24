#ifndef CHAT_INPUT_WIDGET_H
#define CHAT_INPUT_WIDGET_H

#include <QWidget>
#include <QTextEdit>
#include <QPushButton>
#include <QLabel>
#include <QHBoxLayout>
#include <QKeyEvent>

/**
 * @brief QTextEdit subclass that emits sendRequested on Shift+Enter / Ctrl+Enter
 * instead of inserting a newline. Plain Enter inserts a newline as usual.
 */
class ChatTextEdit : public QTextEdit
{
    Q_OBJECT
public:
    explicit ChatTextEdit(QWidget *parent = nullptr) : QTextEdit(parent) {}

signals:
    void sendRequested();

protected:
    void keyPressEvent(QKeyEvent *event) override
    {
        // Shift+Enter or Ctrl+Enter → send the message
        if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
            && (event->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier))) {
            emit sendRequested();
            return;
        }
        QTextEdit::keyPressEvent(event);
    }
};

/**
 * @brief Text input area with send/stop buttons and attachment preview.
 */
class ChatInputWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ChatInputWidget(QWidget *parent = nullptr);

    QString text() const;
    QString attachmentPath() const;
    void clear();
    void setSending(bool sending);
    void setPlaceholder(const QString &text);
    void setAttachment(const QString &filePath);

signals:
    void sendRequested();
    void stopRequested();
    void attachmentRemoved();

private slots:
    void onSendClicked();
    void onRemoveAttachment();

private:
    void setupUI();
    void updateButtonState();

    ChatTextEdit *m_textEdit;
    QPushButton *m_sendBtn;
    QPushButton *m_stopBtn;
    QLabel *m_attachmentPreview;
    QPushButton *m_removeAttachmentBtn;
    bool m_isSending = false;
    QString m_attachmentPath;
};

#endif // CHAT_INPUT_WIDGET_H
