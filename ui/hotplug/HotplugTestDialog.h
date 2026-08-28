#ifndef HOTPLUGTESTDIALOG_H
#define HOTPLUGTESTDIALOG_H

#include <QDialog>
#include <QLabel>
#include <QProgressBar>
#include <QTextEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QListWidget>
#include "HotplugTestWizard.h"

class HotplugTestDialog : public QDialog {
    Q_OBJECT

public:
    explicit HotplugTestDialog(QWidget* parent = nullptr);
    ~HotplugTestDialog();

private slots:
    void onStartTestClicked();
    void onStopTestClicked();
    void onSkipStepClicked();
    void onRetryStepClicked();
    void onManualVerifyClicked();

    void onStepChanged(int index, const HotplugTestStep& step);
    void onVerificationUpdated(const QString& name, bool passed, bool timedOut, qint64 elapsedMs);
    void onStepCompleted(int index, bool passed);
    void onTestCompleted(const TestReport& report);

private:
    void setupUI();
    void updateStepDisplay();
    void appendLog(const QString& message);

    // UI elements
    QListWidget* m_stepList;
    QLabel* m_instructionLabel;
    QLabel* m_statusLabel;
    QProgressBar* m_progressBar;
    QTextEdit* m_logText;
    QPushButton* m_startButton;
    QPushButton* m_stopButton;
    QPushButton* m_skipButton;
    QPushButton* m_retryButton;
    QPushButton* m_verifyButton;

    // Test wizard instance
    HotplugTestWizard& m_wizard;
};

#endif // HOTPLUGTESTDIALOG_H
