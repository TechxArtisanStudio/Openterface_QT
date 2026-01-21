#ifndef DIAGNOSTICSMANAGER_H
#define DIAGNOSTICSMANAGER_H

#include <QObject>
#include <QTimer>
#include <QStringList>
#include <QVector>
#include <QThread>
#include <functional>
#include "diagnosticstypes.h" // for TestStatus
#include "LogWriter.h"

Q_DECLARE_LOGGING_CATEGORY(log_device_diagnostics)

class DiagnosticsManager : public QObject
{
    Q_OBJECT
public:
    explicit DiagnosticsManager(QObject *parent = nullptr);
    ~DiagnosticsManager();

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
    void logMessage(const QString& msg);

private slots:
    void onTimerTimeout();
    void onTargetUsbStatusChanged(bool connected);

private:
    void appendToLog(const QString &message);
    void startTargetPlugPlayTest();
    void testTargetConnectionStatus();
    void testTargetAtBaudrate(int baudrate, std::function<void(bool)> callback);
    void startPlugPlayDetection();
    void failTargetPlugPlayTest(const QString& reason);
    void startHostPlugPlayTest();
    void onHostStatusCheckTimeout();
    bool checkHostConnectionStatus();
    void startSerialConnectionTest();
    bool performSerialConnectionTest();
    bool testSerialConnectionAtBaudrate(int baudrate);
    void startFactoryResetTest();
    bool performFactoryResetTest();
    void startHighBaudrateTest();
    bool performHighBaudrateTest();
    void startLowBaudrateTest();
    bool performLowBaudrateTest();
    void startStressTest();
    void onStressTestTimeout();
    void finishStressTest();
    bool sendStressMouseCommand();
    bool sendStressKeyboardCommand();
    void checkAllTestsCompletion();

    QStringList m_testTitles;
    QVector<TestStatus> m_statuses;
    QTimer *m_testTimer;
    QTimer *m_hostCheckTimer;
    QTimer *m_targetCheckTimer;
    int m_runningTestIndex;
    bool m_isTestingInProgress;

    // Connection to SerialPortManager targetUSBStatus signal
    QMetaObject::Connection m_targetStatusConnection;
    
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

    // Logging
    QThread* m_logThread;
    LogWriter* m_logWriter;

    // Diagnostics-specific serial log file created for the diagnostics session
    QString m_serialLogFilePath;
public:
    QString getSerialLogFilePath() const { return m_serialLogFilePath; }
};

#endif // DIAGNOSTICSMANAGER_H
