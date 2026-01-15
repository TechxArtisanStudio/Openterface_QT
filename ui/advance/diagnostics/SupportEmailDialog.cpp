#include "SupportEmailDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QTextEdit>
#include <QLineEdit>
#include <QPushButton>
#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QUrl>
#include <QFileInfo>
#include <QMessageBox>
#include <QStandardPaths>

SupportEmailDialog::SupportEmailDialog(const QStringList& failedTests, const QString& logFilePath, bool diagnosticsCompleted, QWidget* parent)
    : QDialog(parent), m_logFilePath(logFilePath) {
    QString title = tr("Support Email Draft");
    if (!diagnosticsCompleted) {
        title += tr(" - Please complete the diagnostics tests first");
    }
    setWindowTitle(title);
    setMinimumSize(600, 400);
    setAttribute(Qt::WA_DeleteOnClose);

    setupUI();

    // Set initial email draft
    m_emailTextEdit->setPlainText(generateEmailDraft(failedTests));
}

SupportEmailDialog::~SupportEmailDialog() {}

void SupportEmailDialog::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(15, 15, 15, 15);
    mainLayout->setSpacing(10);

    // Order ID section (moved to top)
    QHBoxLayout* orderLayout = new QHBoxLayout();
    QLabel* orderLabel = new QLabel(tr("Order ID (optional):"), this);
    m_orderIdLineEdit = new QLineEdit(this);
    m_orderIdLineEdit->setPlaceholderText(tr("Enter your order ID if applicable"));
    QPushButton* orderApplyButton = new QPushButton(tr("Apply"), this);
    connect(orderApplyButton, &QPushButton::clicked, this, &SupportEmailDialog::onOrderIdApplyClicked);

    orderLayout->addWidget(orderLabel);
    orderLayout->addWidget(m_orderIdLineEdit);
    orderLayout->addWidget(orderApplyButton);
    mainLayout->addLayout(orderLayout);

    // Name input section
    QHBoxLayout* nameLayout = new QHBoxLayout();
    QLabel* nameLabel = new QLabel(tr("Your Name:"), this);
    m_nameLineEdit = new QLineEdit(this);
    m_applyButton = new QPushButton(tr("Apply"), this);
    connect(m_applyButton, &QPushButton::clicked, this, &SupportEmailDialog::onApplyClicked);

    nameLayout->addWidget(nameLabel);
    nameLayout->addWidget(m_nameLineEdit);
    nameLayout->addWidget(m_applyButton);
    mainLayout->addLayout(nameLayout);

    // Send to email section
    QHBoxLayout* emailLayout = new QHBoxLayout();
    QLabel* sendToLabel = new QLabel(tr("Send to email:"), this);
    m_emailLabel = new QLabel("support@openterface.com", this);
    m_emailLabel->setStyleSheet("font-weight: bold;");
    m_copyEmailButton = new QPushButton(tr("Copy Email"), this);
    connect(m_copyEmailButton, &QPushButton::clicked, this, &SupportEmailDialog::onCopyEmailClicked);

    emailLayout->addWidget(sendToLabel);
    emailLayout->addWidget(m_emailLabel);
    emailLayout->addStretch();
    emailLayout->addWidget(m_copyEmailButton);
    mainLayout->addLayout(emailLayout);

    // Email draft section
    QLabel* draftLabel = new QLabel(tr("Email Draft:"), this);
    mainLayout->addWidget(draftLabel);

    m_emailTextEdit = new QTextEdit(this);
    m_emailTextEdit->setMinimumHeight(200);
    mainLayout->addWidget(m_emailTextEdit);

    // Buttons section - top row
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(10);

    buttonLayout->addStretch();

    m_copyDraftButton = new QPushButton(tr("Copy Draft"), this);
    connect(m_copyDraftButton, &QPushButton::clicked, this, &SupportEmailDialog::onCopyDraftClicked);
    buttonLayout->addWidget(m_copyDraftButton);

    m_showLogButton = new QPushButton(tr("Open File Folder"), this);
    m_showLogButton->setMinimumWidth(120); // Make it larger
    connect(m_showLogButton, &QPushButton::clicked, this, &SupportEmailDialog::onShowLogClicked);
    buttonLayout->addWidget(m_showLogButton);

    mainLayout->addLayout(buttonLayout);

    // Done button - centered at bottom
    QHBoxLayout* doneLayout = new QHBoxLayout();
    doneLayout->addStretch();
    QPushButton* doneButton = new QPushButton(tr("Done"), this);
    connect(doneButton, &QPushButton::clicked, this, &QDialog::accept);
    doneLayout->addWidget(doneButton);
    doneLayout->addStretch();
    mainLayout->addLayout(doneLayout);
}

QString SupportEmailDialog::generateEmailDraft(const QStringList& failedTests) {
    QString draft = tr("Subject: Openterface Diagnostics Report - Issues Found\n\n");
    draft += tr("Dear Openterface Support Team,\n\n");
    draft += tr("Order ID: [Please enter your order ID if you have one]\n\n");
    draft += tr("I have run the diagnostics tool and encountered the following issues:\n\n");

    for (const QString& test : failedTests) {
        draft += QString("- %1\n").arg(test);
    }

    draft += tr("\nPlease find attached the diagnostics log file for your reference.\n\n");
    draft += tr("Best regards,\n");
    draft += tr("[Your Name]\n");

    return draft;
}

void SupportEmailDialog::onApplyClicked() {
    QString name = m_nameLineEdit->text().trimmed();
    if (name.isEmpty()) {
        QMessageBox::warning(this, tr("Warning"), tr("Please enter your name."));
        return;
    }

    QString text = m_emailTextEdit->toPlainText();
    text.replace("[Your Name]", name);
    m_emailTextEdit->setPlainText(text);
}

void SupportEmailDialog::onOrderIdApplyClicked() {
    QString orderId = m_orderIdLineEdit->text().trimmed();
    if (orderId.isEmpty()) {
        QMessageBox::warning(this, tr("Warning"), tr("Please enter your order ID."));
        return;
    }

    QString text = m_emailTextEdit->toPlainText();
    text.replace("[Please enter your order ID if you have one]", orderId);
    m_emailTextEdit->setPlainText(text);
}

void SupportEmailDialog::onShowLogClicked() {
    QFileInfo fileInfo(m_logFilePath);
    if (!fileInfo.exists()) {
        QMessageBox::warning(this, tr("Warning"), tr("Log file does not exist."));
        return;
    }

    // Open the directory containing the log file
    QString dirPath = fileInfo.absolutePath();
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(dirPath))) {
        QMessageBox::warning(this, tr("Error"), tr("Could not open log file directory."));
    } else {
        QMessageBox::information(this, tr("Log File"), tr("Please attach the diagnostics_log.txt file to your email."));
    }
}

void SupportEmailDialog::onCopyEmailClicked() {
    QApplication::clipboard()->setText("support@openterface.com");
    QMessageBox::information(this, tr("Copied"), tr("Email address copied to clipboard."));
}

void SupportEmailDialog::onCopyDraftClicked() {
    QString draftText = m_emailTextEdit->toPlainText();
    QApplication::clipboard()->setText(draftText);
    QMessageBox::information(this, tr("Copied"), tr("Email draft copied to clipboard."));
}