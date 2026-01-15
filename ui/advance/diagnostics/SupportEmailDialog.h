#ifndef SUPPORTEMAILDIALOG_H
#define SUPPORTEMAILDIALOG_H

#include <QDialog>
#include <QStringList>

class QTextEdit;
class QLineEdit;
class QPushButton;
class QLabel;

class SupportEmailDialog : public QDialog {
    Q_OBJECT

public:
    explicit SupportEmailDialog(const QStringList& failedTests, const QString& logFilePath, bool diagnosticsCompleted, QWidget* parent = nullptr);
    ~SupportEmailDialog();

private slots:
    void onApplyClicked();
    void onOrderIdApplyClicked();
    void onShowLogClicked();
    void onCopyEmailClicked();
    void onCopyDraftClicked();

private:
    void setupUI();
    QString generateEmailDraft(const QStringList& failedTests);

    QTextEdit* m_emailTextEdit;
    QLineEdit* m_nameLineEdit;
    QLineEdit* m_orderIdLineEdit;
    QPushButton* m_applyButton;
    QPushButton* m_showLogButton;
    QPushButton* m_copyEmailButton;
    QPushButton* m_copyDraftButton;
    QLabel* m_emailLabel;

    QString m_logFilePath;
};

#endif // SUPPORTEMAILDIALOG_H