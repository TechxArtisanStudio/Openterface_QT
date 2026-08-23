#include "ChatTraceDialog.h"
#include "ai/ChatTracing.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QMessageBox>

ChatTraceDialog::ChatTraceDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("AI Trace Log");
    resize(600, 400);

    auto *layout = new QVBoxLayout(this);

    m_browser = new QTextBrowser();
    m_browser->setFont(QFont("monospace", 10));
    layout->addWidget(m_browser, 1);

    auto *buttonRow = new QHBoxLayout();

    auto *clearBtn = new QPushButton("Clear");
    clearBtn->setToolTip("Delete the trace log file");
    connect(clearBtn, &QPushButton::clicked, this, &ChatTraceDialog::onClearClicked);
    buttonRow->addWidget(clearBtn);

    buttonRow->addStretch();

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    buttonRow->addWidget(buttons);

    layout->addLayout(buttonRow);

    loadTrace();
}

void ChatTraceDialog::loadTrace()
{
    QString content = ChatTracing::instance().readTraceLog();
    m_browser->setPlainText(content);
}

void ChatTraceDialog::onClearClicked()
{
    auto result = QMessageBox::question(
        this,
        "Clear Trace Log",
        "Delete the AI trace log? This cannot be undone.",
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (result != QMessageBox::Yes) return;

    ChatTracing::instance().clearTraceLog();
    loadTrace();
}
