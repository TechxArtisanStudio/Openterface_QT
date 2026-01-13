#ifndef DIAGNOSTICSMANAGER_H
#define DIAGNOSTICSMANAGER_H

#include <QObject>
#include <QTimer>
#include <QStringList>
#include <QVector>
#include "diagnosticstypes.h" // for TestStatus

Q_DECLARE_LOGGING_CATEGORY(log_device_diagnostics)

class DiagnosticsManager : public QObject
{
    Q_OBJECT
public:
    explicit DiagnosticsManager(QObject *parent = nullptr);

    QStringList testTitles() const { return m_testTitles; }
    TestStatus testStatus(int index) const;
    QString getTestTitle(int index) const;
    QString getLogFilePath() const;
    bool isTestingInProgress() const { return m_isTestingInProgress; }

public slots:
    void startTest(int index);
    void resetAllTests();

signals:
    void testStarted(int index);
    void testCompleted(int index, bool success);
    void diagnosticsCompleted(bool allSuccessful);
    void logAppended(const QString &entry);
    void statusChanged(int index, TestStatus status);

private slots:
    void onTimerTimeout();

private:
    void appendToLog(const QString &message);
    void startTargetPlugPlayTest();
    void onTargetStatusCheckTimeout();
    bool checkTargetConnectionStatus();
    void startHostPlugPlayTest();
    void onHostStatusCheckTimeout();
    bool checkHostConnectionStatus();
    void startSerialConnectionTest();
    bool performSerialConnectionTest();
    void startFactoryResetTest();
    bool performFactoryResetTest();
    void startHighBaudrateTest();
    bool performHighBaudrateTest();
    void startStressTest();
    void onStressTestTimeout();
    void finishStressTest();
    bool sendStressMouseCommand();
    bool sendStressKeyboardCommand();
    void checkAllTestsCompletion();

    QStringList m_testTitles;
    QVector<TestStatus> m_statuses;
    QTimer *m_testTimer;
    QTimer *m_targetCheckTimer;
    QTimer *m_hostCheckTimer;
    int m_runningTestIndex;
    bool m_isTestingInProgress;
    
    // Target Plug & Play test state
    bool m_targetPreviouslyConnected;
    bool m_targetCurrentlyConnected;
    bool m_targetUnplugDetected;
    bool m_targetReplugDetected;
    int m_targetTestElapsedTime;
    int m_targetPlugCount;
    
    // Host Plug & Play test state
    bool m_hostPreviouslyConnected;
    bool m_hostCurrentlyConnected;
    bool m_hostUnplugDetected;
    bool m_hostReplugDetected;
    int m_hostTestElapsedTime;
    
    // Stress Test state
    int m_stressTotalCommands;
    int m_stressSuccessfulCommands;
    QTimer *m_stressTestTimer;
};

#endif // DIAGNOSTICSMANAGER_H
