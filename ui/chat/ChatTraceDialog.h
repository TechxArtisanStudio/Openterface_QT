#ifndef CHAT_TRACE_DIALOG_H
#define CHAT_TRACE_DIALOG_H

#include <QDialog>
#include <QTextBrowser>

/**
 * @brief Dialog showing AI request/response trace log.
 */
class ChatTraceDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ChatTraceDialog(QWidget *parent = nullptr);

private:
    void loadTrace();

    QTextBrowser *m_browser;
};

#endif // CHAT_TRACE_DIALOG_H
