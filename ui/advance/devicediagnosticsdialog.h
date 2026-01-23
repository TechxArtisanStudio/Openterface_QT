#ifndef DEVICEDIAGNOSTICSDIALOG_H
#define DEVICEDIAGNOSTICSDIALOG_H

#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QStackedWidget>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QProgressBar>
#include <QTimer>
#include <QGroupBox>
#include <QTextEdit>
#include <QSplitter>
#include <QIcon>
#include <QFrame>
#include <QDateTime>
#include <QSvgWidget>
#include "diagnostics/diagnosticstypes.h"

class TestItem : public QListWidgetItem
{
public:
    explicit TestItem(const QString &title, int testIndex, QListWidget *parent = nullptr);
    
    void setTestStatus(TestStatus status);
    TestStatus getTestStatus() const { return m_status; }
    int getTestIndex() const { return m_testIndex; }

private:
    void updateIcon();
    
    TestStatus m_status;
    int m_testIndex;
    QString m_title;
};

class DiagnosticsManager;

class DeviceDiagnosticsDialog : public QDialog
{
    Q_OBJECT
    
public:
    explicit DeviceDiagnosticsDialog(QWidget *parent = nullptr);
    ~DeviceDiagnosticsDialog();
    
public slots:
    void onRestartClicked();
    void onPreviousClicked();
    void onNextClicked();
    void onCheckNowClicked();
    void onTestItemClicked(QListWidgetItem* item);
    void onOpenLogFileClicked();
    void onSupportEmailClicked();
    
private slots:
    void onLogAppended(const QString &entry);
    void onDiagnosticsCompleted(bool allSuccessful);
    
signals:
    void testStarted(int testIndex);
    void testCompleted(int testIndex, bool success);
    void diagnosticsCompleted();

private:
    void setupUI();
    void setupLeftPanel();
    void setupRightPanel();
    void showTestPage(int index);
    void updateNavigationButtons();
    void updateConnectionSvg();
    void startSvgAnimation();
    void stopSvgAnimation();
    
    // UI Components
    QHBoxLayout* m_mainLayout;
    QSplitter* m_splitter;
    
    // Left panel - Test list
    QGroupBox* m_testListGroup;
    QListWidget* m_testList;
    
    // Right panel - Test details
    QWidget* m_rightPanel;
    QVBoxLayout* m_rightLayout;
    QLabel* m_testTitleLabel;
    QLabel* m_statusIconLabel;
    QLabel* m_reminderLabel;
    QPushButton* m_logFileButton;
    QTextEdit* m_logDisplayText;
    QHBoxLayout* m_buttonLayout;
    QPushButton* m_restartButton;
    QPushButton* m_previousButton;
    QPushButton* m_nextButton;
    QPushButton* m_checkNowButton;
    QPushButton* m_supportEmailButton;
    
    // Test management
    int m_currentTestIndex;
    
    // Test data
    QStringList m_testTitles;

    // Diagnostics backend
    DiagnosticsManager* m_manager;
    
    // SVG display for connection status
    QSvgWidget* m_connectionSvg;
    QTimer* m_svgAnimationTimer;
    bool m_svgAnimationState;  // Toggle between two SVG states
    
    // Diagnostics completion status
    bool m_diagnosticsCompleted;
};

#endif // DEVICEDIAGNOSTICSDIALOG_H