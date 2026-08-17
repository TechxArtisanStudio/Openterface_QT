#ifndef CHAT_INPUT_WIDGET_H
#define CHAT_INPUT_WIDGET_H

#include <QWidget>
#include <QTextEdit>
#include <QPushButton>
#include <QLabel>
#include <QHBoxLayout>

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

    QTextEdit *m_textEdit;
    QPushButton *m_sendBtn;
    QPushButton *m_stopBtn;
    QLabel *m_attachmentPreview;
    QPushButton *m_removeAttachmentBtn;
    bool m_isSending = false;
    QString m_attachmentPath;
};

#endif // CHAT_INPUT_WIDGET_H
