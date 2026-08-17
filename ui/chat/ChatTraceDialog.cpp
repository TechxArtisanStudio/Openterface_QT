#include "ChatTraceDialog.h"
#include "ai/ChatTracing.h"
#include <QVBoxLayout>
#include <QPushButton>
#include <QDialogButtonBox>

ChatTraceDialog::ChatTraceDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("AI Trace Log");
    resize(600, 400);

    auto *layout = new QVBoxLayout(this);

    m_browser = new QTextBrowser();
    m_browser->setFont(QFont("monospace", 10));
    layout->addWidget(m_browser);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    loadTrace();
}

void ChatTraceDialog::loadTrace()
{
    QString content = ChatTracing::instance().readTraceLog();
    m_browser->setPlainText(content);
}
