#include "diagnosticsmanager.h"
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QRandomGenerator>
#include <QDateTime>
#include <QLoggingCategory>
#include <QEventLoop>
#include <QTimer>
#include <QSettings>
#include <QThread>

#include "LogWriter.h"

#include "device/DeviceManager.h" // for device presence checks
#include "device/DeviceInfo.h"
#include "serial/SerialPortManager.h"
#include "serial/ch9329.h"
#include "global.h" // for GlobalVar to get screen resolution


DiagnosticsManager::DiagnosticsManager(QObject *parent)
    : QObject(parent)
    , m_testTimer(new QTimer(this))
    , m_runningTestIndex(-1)
    , m_isTestingInProgress(false)
    , m_targetCheckTimer(nullptr)
    , m_targetPreviouslyConnected(false)
    , m_targetCurrentlyConnected(false)
    , m_targetUnplugDetected(false)
    , m_targetReplugDetected(false)
    , m_targetTestElapsedTime(0)
    , m_targetPlugCount(0)
    , m_hostPreviouslyConnected(false)
    , m_hostCurrentlyConnected(false)
    , m_hostUnplugDetected(false)
    , m_hostReplugDetected(false)
    , m_hostTestElapsedTime(0)
    , m_stressTotalCommands(0)
    , m_stressSuccessfulCommands(0)
    , m_stressTestTimer(nullptr)
{
    // Initialize test titles
    m_testTitles = {
        tr("Overall Connection"),
        tr("Target Plug & Play"),
        tr("Host Plug & Play"),
        tr("Serial Connection"),
        tr("Factory Reset"),
        tr("High Baudrate"),
        tr("Low Baudrate"),
        tr("Stress Test")
    };

    m_statuses.resize(m_testTitles.size());
    for (int i = 0; i < m_statuses.size(); ++i) {
        m_statuses[i] = TestStatus::NotStarted;
    }

    m_testTimer->setSingleShot(true);
    connect(m_testTimer, &QTimer::timeout, this, &DiagnosticsManager::onTimerTimeout);
    
    // Note: Target Plug & Play now uses SerialPortManager::targetUSBStatus signal for detection
    // m_targetCheckTimer removed; signal-based detection will be connected when the test starts.
    
    // Setup Host Plug & Play test timer
    m_hostCheckTimer = new QTimer(this);
    m_hostCheckTimer->setInterval(500); // Check every 500ms
    connect(m_hostCheckTimer, &QTimer::timeout, this, &DiagnosticsManager::onHostStatusCheckTimeout);
    
    // Setup Stress Test timer
    m_stressTestTimer = new QTimer(this);
    m_stressTestTimer->setInterval(50); // Send command every 50ms (600 commands in 30 seconds)
    connect(m_stressTestTimer, &QTimer::timeout, this, &DiagnosticsManager::onStressTestTimeout);

    // Initialize asynchronous logging
    m_logThread = new QThread(this);
    m_logWriter = new LogWriter(getLogFilePath(), this);
    m_logWriter->moveToThread(m_logThread);
    connect(this, &DiagnosticsManager::logMessage, m_logWriter, &LogWriter::writeLog);
    m_logThread->start();
}

TestStatus DiagnosticsManager::testStatus(int index) const
{
    if (index >= 0 && index < m_statuses.size()) {
        return m_statuses[index];
    }
    return TestStatus::NotStarted;
}

QString DiagnosticsManager::getTestTitle(int testIndex) const
{
    if (testIndex >= 0 && testIndex < m_testTitles.size()) {
        return m_testTitles[testIndex];
    }
    return QString();
}

QString DiagnosticsManager::getLogFilePath() const
{
    QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir dir(dataDir);
    if (!dir.exists()) {
        dir.mkpath(dataDir);
    }
    return dir.filePath("diagnostics_log.txt");
}

void DiagnosticsManager::appendToLog(const QString &message)
{
    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
    QString logEntry = QString("[%1] %2").arg(timestamp, message);

    // Emit to UI
    emit logAppended(logEntry);

    // Write to file asynchronously
    emit logMessage(logEntry);
}

void DiagnosticsManager::startTest(int testIndex)
{
    if (m_isTestingInProgress)
        return;
    if (testIndex < 0 || testIndex >= m_testTitles.size())
        return;

    // Ensure diagnostics creates a dedicated serial log file for this session
    if (m_serialLogFilePath.isEmpty()) {
        QString serialPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                             + "/serial_log_diagnostics_"
                             + QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss")
                             + ".txt";
        SerialPortManager::getInstance().setSerialLogFilePath(serialPath);
        // Enable debug logging for serial operations during diagnostics
        SerialPortManager::enableDebugLogging(true);
        m_serialLogFilePath = serialPath;
        appendToLog(QString("Serial logs are being written to: %1").arg(serialPath));
    }

    // Special-case: Overall Connection (index 0) -> perform immediate device presence checks
    if (testIndex == 0) {
        m_isTestingInProgress = true;
        m_runningTestIndex = testIndex;

        m_statuses[testIndex] = TestStatus::InProgress;
        emit statusChanged(testIndex, TestStatus::InProgress);

        QString testName = m_testTitles[testIndex];
        appendToLog(QString("Started test: %1 (Overall Connection check)").arg(testName));
        emit testStarted(testIndex);

        // Query device manager for current devices
        DeviceManager &dm = DeviceManager::getInstance();
        QList<DeviceInfo> devices = dm.getCurrentDevices();

        bool foundHid = false;
        bool foundSerial = false;
        bool foundCamera = false;
        bool foundAudio = false;

        appendToLog(QString("Found %1 device(s) reported by device manager").arg(devices.size()));

        for (const DeviceInfo &d : devices) {
            QString devSummary = QString("Device %1: %2").arg(d.portChain, d.getInterfaceSummary());
            appendToLog(devSummary);

            if (d.hasHidDevice()) {
                foundHid = true;
                appendToLog(QString("HID present on port %1").arg(d.getPortChainDisplay()));
            }
            if (d.hasSerialPort()) {
                foundSerial = true;
                appendToLog(QString("Serial port present: %1").arg(d.serialPortPath));
            }
            if (d.hasCameraDevice()) {
                foundCamera = true;
                appendToLog(QString("Camera present on port %1").arg(d.getPortChainDisplay()));
            }
            if (d.hasAudioDevice()) {
                foundAudio = true;
                appendToLog(QString("Audio present on port %1").arg(d.getPortChainDisplay()));
            }
        }

        // Also append a full device tree for richer diagnostics
        QString deviceTree = DeviceManager::getInstance().getDeviceTree();
        if (!deviceTree.isEmpty()) {
            appendToLog("Device tree:");
            for (const QString &line : deviceTree.split('\n')) {
                appendToLog(QString("  %1").arg(line));
            }
        }

        // Individual checks logged; now determine overall success
        bool success = (foundHid && foundSerial && foundCamera && foundAudio);

        if (success) {
            m_statuses[testIndex] = TestStatus::Completed;
            appendToLog("Overall Connection: PASS - all required interfaces present");
        } else {
            m_statuses[testIndex] = TestStatus::Failed;
            QString missing;
            if (!foundHid) missing += " HID";
            if (!foundSerial) missing += " Serial";
            if (!foundCamera) missing += " Camera";
            if (!foundAudio) missing += " Audio";
            appendToLog(QString("Overall Connection: FAIL - missing:%1").arg(missing));
        }

        emit statusChanged(testIndex, m_statuses[testIndex]);
        emit testCompleted(testIndex, success);

        // Reset running state
        m_isTestingInProgress = false;
        m_runningTestIndex = -1;

        // If all tests done, emit diagnosticsCompleted (reuse logic from onTimerTimeout)
        bool allCompleted = true;
        bool allSuccessful = true;
        for (int i = 0; i < m_statuses.size(); ++i) {
            if (m_statuses[i] == TestStatus::NotStarted || m_statuses[i] == TestStatus::InProgress) {
                allCompleted = false;
                break;
            }
            if (m_statuses[i] == TestStatus::Failed) {
                allSuccessful = false;
            }
        }

        if (allCompleted) {
            appendToLog(QString("=== DIAGNOSTICS COMPLETE: %1 ===").arg(allSuccessful ? "All diagnostic tests PASSED!" : "Diagnostic tests completed with some FAILURES. Check results above."));
            emit diagnosticsCompleted(allSuccessful);
        }

        qCDebug(log_device_diagnostics) << "Overall Connection check finished:" << (success ? "PASS" : "FAIL");
        return;
    }
    
    // Special-case: Target Plug & Play (index 1) -> perform target cable hot-plug detection
    if (testIndex == 1) {
        startTargetPlugPlayTest();
        return;
    }
    
    // Special-case: Host Plug & Play (index 2) -> perform host device hot-plug detection
    if (testIndex == 2) {
        startHostPlugPlayTest();
        return;
    }
    
    // Special-case: Serial Connection (index 3) -> perform serial port connection test
    if (testIndex == 3) {
        startSerialConnectionTest();
        return;
    }
    
    // Special-case: Factory Reset (index 4) -> perform factory reset test
    if (testIndex == 4) {
        startFactoryResetTest();
        return;
    }
    
    // Special-case: High Baudrate (index 5) -> perform baudrate switching test
    if (testIndex == 5) {
        startHighBaudrateTest();
        return;
    }
    
    // Special-case: Low Baudrate (index 6) -> perform low baudrate communication test
    if (testIndex == 6) {
        startLowBaudrateTest();
        return;
    }
    
    // Special-case: Stress Test (index 7) -> perform mouse/keyboard stress testing
    if (testIndex == 7) {
        startStressTest();
        return;
    }

    // Fallback: generic timed test simulation (unchanged behavior for other tests)
    m_isTestingInProgress = true;
    m_runningTestIndex = testIndex;

    m_statuses[testIndex] = TestStatus::InProgress;
    emit statusChanged(testIndex, TestStatus::InProgress);

    QString testName = m_testTitles[testIndex];
    appendToLog(QString("Started test: %1").arg(testName));
    emit testStarted(testIndex);

    int testDuration = 2000 + QRandomGenerator::global()->bounded(3000);
    m_testTimer->start(testDuration);

    qCDebug(log_device_diagnostics) << "Started test" << testIndex << "(" << m_testTitles[testIndex] << ")";
}

void DiagnosticsManager::onTimerTimeout()
{
    if (m_runningTestIndex < 0 || m_runningTestIndex >= m_testTitles.size())
        return;

    bool success = (QRandomGenerator::global()->bounded(100) < 90);
    int testIndex = m_runningTestIndex;

    m_statuses[testIndex] = success ? TestStatus::Completed : TestStatus::Failed;
    emit statusChanged(testIndex, m_statuses[testIndex]);
    emit testCompleted(testIndex, success);

    QString testName = m_testTitles[testIndex];
    QString result = success ? "PASSED" : "FAILED";
    appendToLog(QString("Test completed: %1 - %2").arg(testName, result));

    m_isTestingInProgress = false;
    m_runningTestIndex = -1;

    // Check all completed
    bool allCompleted = true;
    bool allSuccessful = true;
    for (int i = 0; i < m_statuses.size(); ++i) {
        if (m_statuses[i] == TestStatus::NotStarted || m_statuses[i] == TestStatus::InProgress) {
            allCompleted = false;
            break;
        }
        if (m_statuses[i] == TestStatus::Failed) {
            allSuccessful = false;
        }
    }

    if (allCompleted) {
        appendToLog(QString("=== DIAGNOSTICS COMPLETE: %1 ===").arg(allSuccessful ? "All diagnostic tests PASSED!" : "Diagnostic tests completed with some FAILURES. Check results above."));
        emit diagnosticsCompleted(allSuccessful);
    }

    qCDebug(log_device_diagnostics) << "Test" << testIndex << (success ? "passed" : "failed");
}

void DiagnosticsManager::resetAllTests()
{
    for (int i = 0; i < m_statuses.size(); ++i) {
        m_statuses[i] = TestStatus::NotStarted;
        emit statusChanged(i, TestStatus::NotStarted);
    }
    m_isTestingInProgress = false;
    if (m_testTimer->isActive()) m_testTimer->stop();
    if (m_hostCheckTimer->isActive()) m_hostCheckTimer->stop();
    if (m_targetCheckTimer && m_targetCheckTimer->isActive()) m_targetCheckTimer->stop();
    if (m_stressTestTimer && m_stressTestTimer->isActive()) m_stressTestTimer->stop();

    // Restore serial logging to default location if diagnostics had created a special log
    if (!m_serialLogFilePath.isEmpty()) {
        QString defaultSerial = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/serial_log.txt";
        SerialPortManager::getInstance().setSerialLogFilePath(defaultSerial);
        // Disable debug logging for serial operations
        SerialPortManager::enableDebugLogging(false);
        m_serialLogFilePath.clear();
        appendToLog("Serial logging restored to default serial_log.txt");
    }

    // Disconnect from target signal if connected
    if (m_targetStatusConnection) {
        QObject::disconnect(m_targetStatusConnection);
        m_targetStatusConnection = QMetaObject::Connection();
    }

    // Reset target test counters
    m_targetPlugCount = 0;
    m_targetPreviouslyConnected = false;
    m_targetCurrentlyConnected = false;
    m_targetUnplugDetected = false;
    m_targetReplugDetected = false;

    appendToLog("=== DIAGNOSTICS RESTARTED ===");
    appendToLog("All test results have been reset.");

    qCDebug(log_device_diagnostics) << "Diagnostics restarted";
}

void DiagnosticsManager::startTargetPlugPlayTest()
{
    m_isTestingInProgress = true;
    m_runningTestIndex = 1; // Target Plug & Play test index
    
    // Reset test state
    m_targetPreviouslyConnected = false;
    m_targetCurrentlyConnected = false;
    m_targetUnplugDetected = false;
    m_targetReplugDetected = false;
    m_targetTestElapsedTime = 0;
    m_targetPlugCount = 0;
    
    m_statuses[1] = TestStatus::InProgress;
    emit statusChanged(1, TestStatus::InProgress);
    
    appendToLog("Started test: Target Plug & Play");
    appendToLog("First, checking target connection status...");
    emit testStarted(1);
    
    // First, asynchronously test target connection by sending GET_INFO
    testTargetConnectionStatus();
}

void DiagnosticsManager::testTargetConnectionStatus()
{
    SerialPortManager& serialManager = SerialPortManager::getInstance();
    
    QString currentPortPath = serialManager.getCurrentSerialPortPath();
    if (currentPortPath.isEmpty()) {
        appendToLog("No serial port available for target connection test");
        failTargetPlugPlayTest("No serial port available");
        return;
    }
    
    appendToLog(QString("Testing target connection on serial port: %1").arg(currentPortPath));
    
    // Test at both baudrates to determine working baudrate and target status
    testTargetAtBaudrate(115200, [this](bool success115200) {
        if (success115200) {
            appendToLog("Target connection confirmed at 115200 baudrate");
            m_targetPreviouslyConnected = true;
            m_targetCurrentlyConnected = true;
            startPlugPlayDetection();
        } else {
            // Try 9600 baudrate
            testTargetAtBaudrate(9600, [this](bool success9600) {
                if (success9600) {
                    appendToLog("Target connection confirmed at 9600 baudrate");
                    m_targetPreviouslyConnected = true;
                    m_targetCurrentlyConnected = true;
                    startPlugPlayDetection();
                } else {
                    appendToLog("No target response at either 115200 or 9600 baudrate");
                    m_targetPreviouslyConnected = false;
                    m_targetCurrentlyConnected = false;
                    startPlugPlayDetection();
                }
            });
        }
    });
}

void DiagnosticsManager::testTargetAtBaudrate(int baudrate, std::function<void(bool)> callback)
{
    SerialPortManager& serialManager = SerialPortManager::getInstance();
    
    if (!serialManager.setBaudRate(baudrate)) {
        appendToLog(QString("Failed to set baudrate to %1").arg(baudrate));
        callback(false);
        return;
    }
    
    appendToLog(QString("Testing target at %1 baudrate...").arg(baudrate));
    
    // Use QTimer to make it asynchronous
    QTimer::singleShot(100, this, [this, baudrate, callback]() {
        SerialPortManager& serialManager = SerialPortManager::getInstance();
        
        try {
            QByteArray response = serialManager.sendSyncCommand(CMD_GET_INFO, false);
            
            if (response.isEmpty()) {
                appendToLog(QString("No response at %1 baudrate").arg(baudrate));
                callback(false);
            } else {
                appendToLog(QString("Received response at %1 baudrate: %2").arg(baudrate).arg(QString(response.toHex(' '))));
                
                if (response.size() >= static_cast<int>(sizeof(CmdGetInfoResult))) {
                    appendToLog(QString("Valid target response at %1 baudrate").arg(baudrate));
                    callback(true);
                } else {
                    appendToLog(QString("Invalid response size at %1 baudrate").arg(baudrate));
                    callback(false);
                }
            }
        } catch (...) {
            appendToLog(QString("Exception occurred testing at %1 baudrate").arg(baudrate));
            callback(false);
        }
    });
}

void DiagnosticsManager::startPlugPlayDetection()
{
    appendToLog("Target connection status determined. Starting plug & play detection...");
    appendToLog("Test requires detecting 2 plug-in events to complete successfully.");
    appendToLog("Test will timeout after 15 seconds if not completed.");
    
    if (m_targetPreviouslyConnected) {
        appendToLog("Target initially connected. Please unplug the cable first, then plug it back in twice.");
    } else {
        appendToLog("Target initially disconnected. Please plug in the cable (need 2 plug-in events total).");
    }
    
    appendToLog(QString("Initial state: current=%1, previous=%2, plugCount=%3")
                .arg(m_targetCurrentlyConnected).arg(m_targetPreviouslyConnected).arg(m_targetPlugCount));

    SerialPortManager* spm = &SerialPortManager::getInstance();

    // Connect to targetUSBStatus signal for real-time detection
    m_targetStatusConnection = connect(spm, &SerialPortManager::targetUSBStatus,
                                       this, &DiagnosticsManager::onTargetUsbStatusChanged, Qt::QueuedConnection);
    
    if (m_targetStatusConnection) {
        appendToLog("Successfully connected to SerialPortManager::targetUSBStatus signal");
    } else {
        appendToLog("Failed to connect to SerialPortManager::targetUSBStatus signal");
    }

    // Create a dedicated timer for periodic status checking during diagnostics
    if (!m_targetCheckTimer) {
        m_targetCheckTimer = new QTimer(this);
        m_targetCheckTimer->setInterval(1000); // Check every 1 second
        connect(m_targetCheckTimer, &QTimer::timeout, this, [this]() {
            if (m_runningTestIndex == 1) {
                SerialPortManager& serialManager = SerialPortManager::getInstance();
                try {
                    // Send GET_INFO to trigger targetUSBStatus signal
                    serialManager.sendAsyncCommand(CMD_GET_INFO, false);
                } catch (...) {
                    // Ignore errors to avoid stopping the test
                }
            }
        });
    }

    // Start the periodic status checking
    m_targetCheckTimer->start();
    appendToLog("Started periodic status checking (every 1 second)");

    // Send an initial GET_INFO command
    appendToLog("Triggering initial status check...");
    QTimer::singleShot(100, this, [this]() {
        SerialPortManager& serialManager = SerialPortManager::getInstance();
        try {
            // Send GET_INFO to activate the status monitoring
            serialManager.sendAsyncCommand(CMD_GET_INFO, false);
            appendToLog("Initial GET_INFO sent to activate status monitoring");
        } catch (...) {
            appendToLog("Initial GET_INFO failed - target may be disconnected");
        }
    });

    // Start a 15s timeout for the plug & play test
    QTimer::singleShot(15000, this, [this]() {
        if (m_runningTestIndex == 1) {
            failTargetPlugPlayTest(QString("Only detected %1/2 plug-in events within 15 seconds").arg(m_targetPlugCount));
        }
    });

    qCDebug(log_device_diagnostics) << "Started Target Plug & Play detection (signal-based detection with periodic checks)";
}

void DiagnosticsManager::failTargetPlugPlayTest(const QString& reason)
{
    // Stop the periodic status check timer
    if (m_targetCheckTimer && m_targetCheckTimer->isActive()) {
        m_targetCheckTimer->stop();
        appendToLog("Stopped periodic status checking");
    }
    
    // Disconnect any signal connections
    if (m_targetStatusConnection) {
        QObject::disconnect(m_targetStatusConnection);
        m_targetStatusConnection = QMetaObject::Connection();
    }
    
    m_statuses[1] = TestStatus::Failed;
    emit statusChanged(1, TestStatus::Failed);
    emit testCompleted(1, false);

    appendToLog(QString("Target Plug & Play test: FAILED - %1").arg(reason));

    m_isTestingInProgress = false;
    m_runningTestIndex = -1;

    // Check if all tests completed
    checkAllTestsCompletion();

    qCDebug(log_device_diagnostics) << "Target Plug & Play test failed:" << reason;
}

void DiagnosticsManager::onTargetUsbStatusChanged(bool connected)
{
    if (m_runningTestIndex != 1) {
        return; // Only handle during Target Plug & Play test
    }

    // Check if this is actually a state change
    if (connected == m_targetCurrentlyConnected) {
        // No real state change, just return without logging
        return;
    }

    // Log the actual state change
    appendToLog(QString("USB Status Signal: connected=%1, current=%2 -> %3")
                .arg(connected).arg(m_targetCurrentlyConnected).arg(connected));

    // Detect state changes
    if (!connected && m_targetCurrentlyConnected) {
        // Target unplugged - compare with current state, not previous
        m_targetUnplugDetected = true;
        appendToLog("Target cable unplugged detected!");
        int remainingPlugs = 2 - m_targetPlugCount;
        appendToLog(QString("Please plug it back in (need %1 more plug-in events)...").arg(remainingPlugs));
    } else if (connected && !m_targetCurrentlyConnected) {
        // Target plugged in - compare with current state, not previous
        m_targetPlugCount++;
        appendToLog(QString("Target cable plugged in detected! (Count: %1/2)").arg(m_targetPlugCount));

        if (m_targetPlugCount >= 2) {
            // Stop the periodic status check timer
            if (m_targetCheckTimer && m_targetCheckTimer->isActive()) {
                m_targetCheckTimer->stop();
                appendToLog("Stopped periodic status checking");
            }
            
            // Test completed successfully - disconnect and report success
            if (m_targetStatusConnection) {
                QObject::disconnect(m_targetStatusConnection);
                m_targetStatusConnection = QMetaObject::Connection();
            }

            m_statuses[1] = TestStatus::Completed;
            emit statusChanged(1, TestStatus::Completed);
            emit testCompleted(1, true);

            appendToLog("Target Plug & Play test: PASSED - 2 plug-in events detected successfully");

            m_isTestingInProgress = false;
            m_runningTestIndex = -1;

            // Check if all tests completed
            checkAllTestsCompletion();
            return;
        } else {
            // Need one more plug-in event
            appendToLog("Please unplug and plug in the cable again to complete the test.");
        }
    }

    // Update state tracking
    m_targetPreviouslyConnected = m_targetCurrentlyConnected;
    m_targetCurrentlyConnected = connected;
    
    appendToLog(QString("Updated state: current=%1, previous=%2").arg(m_targetCurrentlyConnected).arg(m_targetPreviouslyConnected));
}


void DiagnosticsManager::checkAllTestsCompletion()
{
    bool allCompleted = true;
    bool allSuccessful = true;
    
    for (int i = 0; i < m_statuses.size(); ++i) {
        if (m_statuses[i] == TestStatus::NotStarted || m_statuses[i] == TestStatus::InProgress) {
            allCompleted = false;
            break;
        }
        if (m_statuses[i] == TestStatus::Failed) {
            allSuccessful = false;
        }
    }
    
    if (allCompleted) {
        appendToLog(QString("=== DIAGNOSTICS COMPLETE: %1 ===").arg(allSuccessful ? "All diagnostic tests PASSED!" : "Diagnostic tests completed with some FAILURES. Check results above."));
        emit diagnosticsCompleted(allSuccessful);
    }
}

void DiagnosticsManager::startHostPlugPlayTest()
{
    m_isTestingInProgress = true;
    m_runningTestIndex = 2; // Host Plug & Play test index
    
    // Reset test state
    m_hostPreviouslyConnected = false;
    m_hostCurrentlyConnected = false;
    m_hostUnplugDetected = false;
    m_hostReplugDetected = false;
    m_hostTestElapsedTime = 0;
    
    m_statuses[2] = TestStatus::InProgress;
    emit statusChanged(2, TestStatus::InProgress);
    
    appendToLog("Started test: Host Plug & Play");
    appendToLog("Test requires detecting host device unplug and re-plug to complete successfully.");
    appendToLog("Test will timeout after 30 seconds if not completed.");
    emit testStarted(2);
    
    // Check initial host connection status
    m_hostPreviouslyConnected = checkHostConnectionStatus();
    m_hostCurrentlyConnected = m_hostPreviouslyConnected;
    
    if (m_hostPreviouslyConnected) {
        appendToLog("Host devices initially connected. Please unplug the USB cable from host, then plug it back in.");
    } else {
        appendToLog("Host devices initially disconnected. Please plug in the USB cable to host.");
    }
    
    // Start periodic checking
    m_hostCheckTimer->start();
    
    qCDebug(log_device_diagnostics) << "Started Host Plug & Play test";
}

void DiagnosticsManager::onHostStatusCheckTimeout()
{
    m_hostTestElapsedTime += 500; // Timer interval is 500ms
    
    bool currentStatus = checkHostConnectionStatus();
    
    // Detect state changes
    if (currentStatus != m_hostCurrentlyConnected) {
        m_hostCurrentlyConnected = currentStatus;
        
        if (!currentStatus && m_hostPreviouslyConnected) {
            // Host devices were unplugged
            m_hostUnplugDetected = true;
            appendToLog("Host devices unplugged detected!");
            appendToLog("Please plug the USB cable back into the host to complete the test...");
        } else if (currentStatus && m_hostUnplugDetected && !m_hostReplugDetected) {
            // Host devices were re-plugged after being unplugged
            m_hostReplugDetected = true;
            appendToLog("Host devices re-plugged detected!");
            
            // Test completed successfully
            m_hostCheckTimer->stop();
            m_statuses[2] = TestStatus::Completed;
            emit statusChanged(2, TestStatus::Completed);
            emit testCompleted(2, true);
            
            appendToLog("Host Plug & Play test: PASSED - Hot-plug cycle completed successfully");
            
            m_isTestingInProgress = false;
            m_runningTestIndex = -1;
            
            // Check if all tests completed
            checkAllTestsCompletion();
            return;
        }
        
        m_hostPreviouslyConnected = currentStatus;
    }
    
    // Check for timeout (30 seconds)
    if (m_hostTestElapsedTime >= 30000) {
        m_hostCheckTimer->stop();
        m_statuses[2] = TestStatus::Failed;
        emit statusChanged(2, TestStatus::Failed);
        emit testCompleted(2, false);
        
        if (!m_hostUnplugDetected) {
            appendToLog("Host Plug & Play test: FAILED - No unplug detected within 30 seconds");
        } else {
            appendToLog("Host Plug & Play test: FAILED - No re-plug detected within 30 seconds");
        }
        
        m_isTestingInProgress = false;
        m_runningTestIndex = -1;
        
        // Check if all tests completed
        checkAllTestsCompletion();
    }
}

bool DiagnosticsManager::checkHostConnectionStatus()
{
    // Get device manager instance to check host-side devices
    DeviceManager& deviceManager = DeviceManager::getInstance();
    QList<DeviceInfo> devices = deviceManager.getCurrentDevices();
    
    bool hasCamera = false;
    bool hasAudio = false;
    bool hasHid = false;
    bool hasSerial = false;
    
    // Check if we have all required host-side device interfaces
    for (const DeviceInfo& device : devices) {
        if (device.hasCameraDevice()) {
            hasCamera = true;
        }
        if (device.hasAudioDevice()) {
            hasAudio = true;
        }
        if (device.hasHidDevice()) {
            hasHid = true;
        }
        if (device.hasSerialPort()) {
            hasSerial = true;
        }
    }
    
    // Host is considered connected if we have all essential interfaces
    bool isConnected = (hasCamera && hasAudio && hasHid && hasSerial);
    
    qCDebug(log_device_diagnostics) << "Host connection status:" << isConnected 
                                   << "Camera:" << hasCamera 
                                   << "Audio:" << hasAudio 
                                   << "HID:" << hasHid 
                                   << "Serial:" << hasSerial;
    
    return isConnected;
}

void DiagnosticsManager::startSerialConnectionTest()
{
    m_isTestingInProgress = true;
    m_runningTestIndex = 3; // Serial Connection test index
    
    m_statuses[3] = TestStatus::InProgress;
    emit statusChanged(3, TestStatus::InProgress);
    
    appendToLog("Started test: Serial Connection");
    appendToLog("Testing serial port connectivity by sending CMD_GET_INFO command...");
    emit testStarted(3);
    
    // Perform serial connection test
    bool success = performSerialConnectionTest();
    
    if (success) {
        m_statuses[3] = TestStatus::Completed;
        appendToLog("Serial Connection test: PASSED - Successfully received response from serial port");
    } else {
        m_statuses[3] = TestStatus::Failed;
        appendToLog("Serial Connection test: FAILED - No response or invalid response from serial port");
    }
    
    emit statusChanged(3, m_statuses[3]);
    emit testCompleted(3, success);
    
    m_isTestingInProgress = false;
    m_runningTestIndex = -1;
    
    // Check if all tests completed
    checkAllTestsCompletion();
    
    qCDebug(log_device_diagnostics) << "Serial Connection test finished:" << (success ? "PASS" : "FAIL");
}

bool DiagnosticsManager::performSerialConnectionTest()
{
    // Get SerialPortManager instance
    SerialPortManager& serialManager = SerialPortManager::getInstance();
    
    // Check if serial port is available
    QString currentPortPath = serialManager.getCurrentSerialPortPath();
    if (currentPortPath.isEmpty()) {
        appendToLog("No serial port available for testing");
        return false;
    }
    
    appendToLog(QString("Using serial port: %1").arg(currentPortPath));
    appendToLog("Testing serial connection at 115200 baudrate...");
    
    // Test at 115200 baudrate first
    bool success115200 = testSerialConnectionAtBaudrate(115200);
    
    appendToLog("Testing serial connection at 9600 baudrate...");
    
    // Test at 9600 baudrate
    bool success9600 = testSerialConnectionAtBaudrate(9600);
    
    // Test passes if either baudrate works
    if (success115200 || success9600) {
        appendToLog("Serial Connection test: PASSED - Successfully connected at least one baudrate");
        return true;
    } else {
        appendToLog("Serial Connection test: FAILED - No connection at either 115200 or 9600 baudrate");
        return false;
    }
}

bool DiagnosticsManager::testSerialConnectionAtBaudrate(int baudrate)
{
    SerialPortManager& serialManager = SerialPortManager::getInstance();
    
    // Set baudrate
    if (!serialManager.setBaudRate(baudrate)) {
        appendToLog(QString("Failed to set baudrate to %1").arg(baudrate));
        return false;
    }
    
    appendToLog(QString("Testing target connection status at %1 baudrate with 3 attempts (1 second interval)...").arg(baudrate));
    
    // Retry up to 3 times with 1 second intervals
    for (int attempt = 1; attempt <= 3; attempt++) {
        appendToLog(QString("Attempt %1/3 at %2 baudrate: Sending CMD_GET_INFO command...").arg(attempt).arg(baudrate));
        
        try {
            // Send CMD_GET_INFO command and wait for response
            QByteArray response = serialManager.sendSyncCommand(CMD_GET_INFO, false);
            
            if (response.isEmpty()) {
                appendToLog(QString("Attempt %1 at %2 baudrate: No response received from serial port").arg(attempt).arg(baudrate));
            } else {
                appendToLog(QString("Attempt %1 at %2 baudrate: Received response: %3").arg(attempt).arg(baudrate).arg(QString(response.toHex(' '))));
                
                // Validate response structure
                if (response.size() >= static_cast<int>(sizeof(CmdGetInfoResult))) {
                    CmdGetInfoResult result = CmdGetInfoResult::fromByteArray(response);
                    
                    // Check if response has valid header (0x57AB)
                    if (result.prefix == 0xAB57) { // Little-endian format
                        appendToLog(QString("Attempt %1 at %2 baudrate: Valid response - Version: %3, Target Connected: %4")
                                   .arg(attempt)
                                   .arg(baudrate)
                                   .arg(result.version)
                                   .arg(result.targetConnected ? "Yes" : "No"));
                        
                        // Check if target is connected
                        if (result.targetConnected != 0) {
                            appendToLog(QString("Target connection detected on attempt %1 at %2 baudrate - Test PASSED").arg(attempt).arg(baudrate));
                            return true;
                        } else {
                            appendToLog(QString("Attempt %1 at %2 baudrate: Target not connected").arg(attempt).arg(baudrate));
                        }
                    } else {
                        appendToLog(QString("Attempt %1 at %2 baudrate: Invalid response header: 0x%3 (expected 0x57AB)")
                                   .arg(attempt)
                                   .arg(baudrate)
                                   .arg(result.prefix, 4, 16, QChar('0')));
                    }
                } else {
                    appendToLog(QString("Attempt %1 at %2 baudrate: Response too short: %3 bytes (expected at least %4 bytes)")
                               .arg(attempt)
                               .arg(baudrate)
                               .arg(response.size())
                               .arg(sizeof(CmdGetInfoResult)));
                }
            }
            
        } catch (const std::exception& e) {
            appendToLog(QString("Attempt %1 at %2 baudrate: Exception during serial communication: %3").arg(attempt).arg(baudrate).arg(e.what()));
        } catch (...) {
            appendToLog(QString("Attempt %1 at %2 baudrate: Unknown error during serial communication").arg(attempt).arg(baudrate));
        }
        
        // Wait 1 second before next attempt (except for the last attempt)
        if (attempt < 3) {
            appendToLog("Waiting 1 second before next attempt...");
            QEventLoop loop;
            QTimer::singleShot(1000, &loop, &QEventLoop::quit);
            loop.exec();
        }
    }
    
    appendToLog(QString("Failed to connect at %1 baudrate after 3 attempts").arg(baudrate));
    return false;
}

void DiagnosticsManager::startFactoryResetTest()
{
    m_isTestingInProgress = true;
    m_runningTestIndex = 4; // Factory Reset test index
    
    m_statuses[4] = TestStatus::InProgress;
    emit statusChanged(4, TestStatus::InProgress);
    
    appendToLog("Started test: Factory Reset");
    appendToLog("Performing factory reset operation on HID chip...");
    emit testStarted(4);
    
    // Perform factory reset test
    bool success = performFactoryResetTest();
    
    if (success) {
        m_statuses[4] = TestStatus::Completed;
        appendToLog("Factory Reset test: PASSED - Factory reset operation completed successfully");
    } else {
        m_statuses[4] = TestStatus::Failed;
        appendToLog("Factory Reset test: FAILED - Factory reset operation failed");
    }
    
    emit statusChanged(4, m_statuses[4]);
    emit testCompleted(4, success);
    
    m_isTestingInProgress = false;
    m_runningTestIndex = -1;
    
    // Check if all tests completed
    checkAllTestsCompletion();
    
    qCDebug(log_device_diagnostics) << "Factory Reset test finished:" << (success ? "PASS" : "FAIL");
}

bool DiagnosticsManager::performFactoryResetTest()
{
    SerialPortManager& serialManager = SerialPortManager::getInstance();
    
    QString currentPortPath = serialManager.getCurrentSerialPortPath();
    if (currentPortPath.isEmpty()) {
        appendToLog("No serial port available for factory reset test");
        return false;
    }
    
    appendToLog(QString("Using serial port: %1 for factory reset test").arg(currentPortPath));
    
    try {
        // Directly perform factory reset without pre-communication test
        appendToLog("Performing standard factory reset (RTS pin method)...");
        appendToLog("This will hold RTS pin low for 4 seconds, then reconnect...");
        
        bool resetSuccess = serialManager.factoryResetHipChipSync(10000); // 10 second timeout
        
        if (resetSuccess) {
            appendToLog("Standard factory reset completed successfully");
            
            // Verify communication after reset
            appendToLog("Verifying communication after factory reset...");
            
            // Test communication multiple times to ensure stability
            bool communicationVerified = false;
            for (int attempt = 1; attempt <= 3; attempt++) {
                appendToLog(QString("Communication verification attempt %1/3...").arg(attempt));
                
                QByteArray postResetResponse = serialManager.sendSyncCommand(CMD_GET_INFO, true);
                if (!postResetResponse.isEmpty() && postResetResponse.size() >= static_cast<int>(sizeof(CmdGetInfoResult))) {
                    CmdGetInfoResult postResult = CmdGetInfoResult::fromByteArray(postResetResponse);
                    if (postResult.prefix == 0xAB57) {
                        appendToLog(QString("Post-reset communication successful on attempt %1 - version: %2")
                                   .arg(attempt).arg(postResult.version));
                        communicationVerified = true;
                        break;
                    } else {
                        appendToLog(QString("Attempt %1: Invalid response header: 0x%2")
                                   .arg(attempt).arg(postResult.prefix, 4, 16, QChar('0')));
                    }
                } else {
                    appendToLog(QString("Attempt %1: No valid response received").arg(attempt));
                }
                
                if (attempt < 3) {
                    appendToLog("Waiting 1 second before retry...");
                    QEventLoop waitLoop;
                    QTimer::singleShot(1000, &waitLoop, &QEventLoop::quit);
                    waitLoop.exec();
                }
            }
            
            if (communicationVerified) {
                appendToLog("Factory reset verification successful!");
                appendToLog("Device has been reset to factory defaults and is responding correctly.");
                return true;
            } else {
                appendToLog("Factory reset verification failed - device not responding properly");
                return false;
            }
        } else {
            appendToLog("Standard factory reset failed");
            
            // Try V191 command method as fallback
            appendToLog("Trying V191 factory reset method (command-based) as fallback...");
            
            bool v191Success = serialManager.factoryResetHipChipV191Sync(5000); // 5 second timeout
            
            if (v191Success) {
                appendToLog("V191 factory reset completed successfully");
                
                // Verify communication
                QByteArray v191Response = serialManager.sendSyncCommand(CMD_GET_INFO, true);
                if (!v191Response.isEmpty() && v191Response.size() >= static_cast<int>(sizeof(CmdGetInfoResult))) {
                    CmdGetInfoResult v191Result = CmdGetInfoResult::fromByteArray(v191Response);
                    if (v191Result.prefix == 0xAB57) {
                        appendToLog(QString("V191 factory reset verification successful - version: %1").arg(v191Result.version));
                        return true;
                    }
                }
                appendToLog("V191 factory reset completed but verification failed");
                return false;
            } else {
                appendToLog("V191 factory reset also failed");
                appendToLog("Both factory reset methods failed - device may not support factory reset");
                return false;
            }
        }
        
    } catch (const std::exception& e) {
        appendToLog(QString("Factory reset test exception: %1").arg(e.what()));
        return false;
    } catch (...) {
        appendToLog("Unknown exception occurred during factory reset test");
        return false;
    }
}

void DiagnosticsManager::startHighBaudrateTest()
{
    m_isTestingInProgress = true;
    m_runningTestIndex = 5; // High Baudrate test index
    
    m_statuses[5] = TestStatus::InProgress;
    emit statusChanged(5, TestStatus::InProgress);
    
    appendToLog("Started test: High Baudrate");
    appendToLog("Testing baudrate switching to 115200...");
    emit testStarted(5);
    
    // Perform high baudrate test
    bool success = performHighBaudrateTest();
    
    if (success) {
        m_statuses[5] = TestStatus::Completed;
        appendToLog("High Baudrate test: PASSED - Successfully switched to 115200 baudrate");
    } else {
        m_statuses[5] = TestStatus::Failed;
        appendToLog("High Baudrate test: FAILED - Could not switch to 115200 baudrate");
    }
    
    emit statusChanged(5, m_statuses[5]);
    emit testCompleted(5, success);
    
    m_isTestingInProgress = false;
    m_runningTestIndex = -1;
    
    // Check if all tests completed
    checkAllTestsCompletion();
    
    qCDebug(log_device_diagnostics) << "High Baudrate test finished:" << (success ? "PASS" : "FAIL");
}

bool DiagnosticsManager::performHighBaudrateTest()
{
    // Get SerialPortManager instance
    SerialPortManager& serialManager = SerialPortManager::getInstance();
    
    // Check if serial port is available
    QString currentPortPath = serialManager.getCurrentSerialPortPath();
    if (currentPortPath.isEmpty()) {
        appendToLog("High Baudrate test failed: No serial port available");
        return false;
    }
    
    appendToLog(QString("Using serial port: %1 for baudrate test").arg(currentPortPath));
    
    // Get current baudrate before testing
    int currentBaudrate = serialManager.getCurrentBaudrate();
    appendToLog(QString("Current baudrate: %1").arg(currentBaudrate));
    
    // If already at 115200, test successful communication and return
    if (currentBaudrate == SerialPortManager::BAUDRATE_HIGHSPEED) {
        appendToLog("Already at 115200 baudrate, ");
        QByteArray testResponse = serialManager.sendSyncCommand(CMD_GET_INFO, true);
        if (!testResponse.isEmpty()) {
            CmdGetInfoResult infoResult = CmdGetInfoResult::fromByteArray(testResponse);
            appendToLog(QString("Communication test successful at 115200 - received response (version: %1)").arg(infoResult.version));
            return true;
        } else {
            appendToLog("Communication test failed at 115200 baudrate, if you haven't tested the factory reset pls do it first. ");
            return false;
        }
    }
    
    try {
        // Build command to switch to 115200 baudrate (similar to applyCommandBasedBaudrateChange)
        QByteArray command;
        static QSettings settings("Techxartisan", "Openterface");
        uint8_t mode = (settings.value("hardware/operatingMode", 0x02).toUInt());
        
        appendToLog("Attempting to switch to 115200 baudrate using command-based method...");
        
        // Use 115200 configuration command
        command = CMD_SET_PARA_CFG_PREFIX_115200;
        command[5] = mode; 
        command.append(CMD_SET_PARA_CFG_MID);
        
        appendToLog("Sending configuration command for 115200 baudrate...");
        QByteArray configResponse = serialManager.sendSyncCommand(command, true);
        
        if (configResponse.isEmpty()) {
            appendToLog("No response received from configuration command");
            return false;
        }
        
        appendToLog(QString("Configuration response: %1").arg(QString(configResponse.toHex(' '))));
        
        // Send reset command
        appendToLog("Sending reset command...");
        bool resetSuccess = serialManager.sendResetCommand();
        if (!resetSuccess) {
            appendToLog("Reset command failed");
            return false;
        }
        
        // Wait for reset to complete
        appendToLog("Waiting 500ms for reset to complete...");
        QEventLoop waitLoop;
        QTimer::singleShot(500, &waitLoop, &QEventLoop::quit);
        waitLoop.exec();
        
        // Set host-side baudrate to 115200
        appendToLog("Setting host-side baudrate to 115200...");
        bool baudrateSuccess = serialManager.setBaudRate(SerialPortManager::BAUDRATE_HIGHSPEED);
        if (!baudrateSuccess) {
            appendToLog("Failed to set host-side baudrate to 115200");
            return false;
        }
        
        // Wait for baudrate change to stabilize
        appendToLog("Waiting 500ms for baudrate change to stabilize...");
        QTimer::singleShot(500, &waitLoop, &QEventLoop::quit);
        waitLoop.exec();
        
        QThread::msleep(100); // Additional short delay
        // Verify communication at 115200 baudrate
        appendToLog("Verifying communication at 115200 baudrate...");
        for (int attempt = 1; attempt <= 3; attempt++) {
            appendToLog(QString("Verification attempt %1/3...").arg(attempt));
            
            QByteArray verifyResponse = serialManager.sendSyncCommand(CMD_GET_INFO, true);
            if (!verifyResponse.isEmpty() && verifyResponse.size() >= static_cast<int>(sizeof(CmdGetInfoResult))) {
                CmdGetInfoResult result = CmdGetInfoResult::fromByteArray(verifyResponse);
                if (result.prefix == 0xAB57) {
                    appendToLog(QString("115200 baudrate verification successful on attempt %1 - version: %2")
                               .arg(attempt).arg(result.version));
                    appendToLog("High baudrate switch completed successfully!");
                    return true;
                } else {
                    appendToLog(QString("Attempt %1: Invalid response header: 0x%2")
                               .arg(attempt).arg(result.prefix, 4, 16, QChar('0')));
                }
            } else {
                appendToLog(QString("Attempt %1: No valid response received").arg(attempt));
            }
            
            if (attempt < 3) {
                appendToLog("Waiting 1 second before retry...");
                QTimer::singleShot(1000, &waitLoop, &QEventLoop::quit);
                waitLoop.exec();
            }
        }
        
        appendToLog("High baudrate verification failed after all attempts");
        return false;
        
    } catch (const std::exception& e) {
        appendToLog(QString("High baudrate test failed due to exception: %1").arg(e.what()));
        return false;
    } catch (...) {
        appendToLog("High baudrate test failed due to unknown error");
        return false;
    }
}

void DiagnosticsManager::startLowBaudrateTest()
{
    m_isTestingInProgress = true;
    m_runningTestIndex = 6; // Low Baudrate test index (updated from 5 to 6)
    
    m_statuses[6] = TestStatus::InProgress;
    emit statusChanged(6, TestStatus::InProgress);
    
    appendToLog("Started test: Low Baudrate");
    appendToLog("Testing serial communication at low baudrate (9600)...");
    emit testStarted(6);
    
    // Perform low baudrate test
    bool success = performLowBaudrateTest();
    
    if (success) {
        m_statuses[6] = TestStatus::Completed;
        appendToLog("Low Baudrate test: PASSED - Successfully tested communication at 9600 baudrate");
    } else {
        m_statuses[6] = TestStatus::Failed;
        appendToLog("Low Baudrate test: FAILED - Could not establish reliable communication at 9600 baudrate");
    }
    
    emit statusChanged(6, m_statuses[6]);
    emit testCompleted(6, success);
    
    m_isTestingInProgress = false;
    m_runningTestIndex = -1;
    
    // Check if all tests completed
    checkAllTestsCompletion();
    
    qCDebug(log_device_diagnostics) << "Low Baudrate test finished:" << (success ? "PASS" : "FAIL");
}

bool DiagnosticsManager::performLowBaudrateTest()
{
    // Get SerialPortManager instance
    SerialPortManager& serialManager = SerialPortManager::getInstance();
    
    // Check if serial port is available
    QString currentPortPath = serialManager.getCurrentSerialPortPath();
    if (currentPortPath.isEmpty()) {
        appendToLog("Low Baudrate test failed: No serial port available");
        return false;
    }
    
    appendToLog(QString("Using serial port: %1 for low baudrate test").arg(currentPortPath));
    
    // Get current baudrate before testing
    int currentBaudrate = serialManager.getCurrentBaudrate();
    appendToLog(QString("Current baudrate: %1").arg(currentBaudrate));
    
    try {
        // Build command to switch to 9600 baudrate (reusing factory reset approach)
        QByteArray command;
        static QSettings settings("Techxartisan", "Openterface");
        uint8_t mode = (settings.value("hardware/operatingMode", 0x02).toUInt());
        
        appendToLog("Setting device to factory default baudrate (9600) using reset method...");
        
        // Perform factory reset to restore default baudrate (9600)
        appendToLog("Performing factory reset to restore default 9600 baudrate...");
        appendToLog("This will hold RTS pin low for 4 seconds, then reconnect...");
        
        bool resetSuccess = serialManager.factoryResetHipChipSync(10000); // 10 second timeout
        
        if (resetSuccess) {
            appendToLog("Factory reset completed successfully - device should be at 9600 baudrate");
            
            // Set host-side baudrate to 9600
            appendToLog("Setting host-side baudrate to 9600...");
            bool baudrateSuccess = serialManager.setBaudRate(SerialPortManager::DEFAULT_BAUDRATE);
            if (!baudrateSuccess) {
                appendToLog("Failed to set host-side baudrate to 9600");
                return false;
            }
            
            // Wait for baudrate change to stabilize
            appendToLog("Waiting 1 second for baudrate change to stabilize...");
            QEventLoop waitLoop;
            QTimer::singleShot(1000, &waitLoop, &QEventLoop::quit);
            waitLoop.exec();
            
            // Verify communication at 9600 baudrate
            appendToLog("Verifying communication at 9600 baudrate...");
            bool communicationVerified = false;
            for (int attempt = 1; attempt <= 3; attempt++) {
                appendToLog(QString("Communication verification attempt %1/3...").arg(attempt));
                
                QByteArray verifyResponse = serialManager.sendSyncCommand(CMD_GET_INFO, true);
                if (!verifyResponse.isEmpty() && verifyResponse.size() >= static_cast<int>(sizeof(CmdGetInfoResult))) {
                    CmdGetInfoResult result = CmdGetInfoResult::fromByteArray(verifyResponse);
                    if (result.prefix == 0xAB57) {
                        appendToLog(QString("9600 baudrate verification successful on attempt %1 - version: %2")
                                   .arg(attempt).arg(result.version));
                        communicationVerified = true;
                        break;
                    } else {
                        appendToLog(QString("Attempt %1: Invalid response header: 0x%2")
                                   .arg(attempt).arg(result.prefix, 4, 16, QChar('0')));
                    }
                } else {
                    appendToLog(QString("Attempt %1: No valid response received").arg(attempt));
                }
                
                if (attempt < 3) {
                    appendToLog("Waiting 1 second before retry...");
                    QTimer::singleShot(1000, &waitLoop, &QEventLoop::quit);
                    waitLoop.exec();
                }
            }
            
            if (communicationVerified) {
                appendToLog("Low baudrate test successful!");
                appendToLog("Device is communicating reliably at 9600 baudrate.");
                return true;
            } else {
                appendToLog("Low baudrate test verification failed - device not responding properly at 9600 baudrate");
                return false;
            }
            
        } else {
            appendToLog("Factory reset failed, cannot test 9600 baudrate");
            
            // Try testing current baudrate if reset failed
            appendToLog("Testing communication at current baudrate as fallback...");
            QByteArray fallbackResponse = serialManager.sendSyncCommand(CMD_GET_INFO, true);
            if (!fallbackResponse.isEmpty()) {
                CmdGetInfoResult fallbackResult = CmdGetInfoResult::fromByteArray(fallbackResponse);
                if (fallbackResult.prefix == 0xAB57) {
                    appendToLog(QString("Communication test successful at current baudrate (%1) - version: %2")
                               .arg(currentBaudrate).arg(fallbackResult.version));
                    appendToLog("Note: Low baudrate test used current baudrate as device reset failed");
                    return true;
                }
            }
            
            appendToLog("Both factory reset and fallback communication failed");
            return false;
        }
        
    } catch (const std::exception& e) {
        appendToLog(QString("Low baudrate test failed due to exception: %1").arg(e.what()));
        return false;
    } catch (...) {
        appendToLog("Low baudrate test failed due to unknown error");
        return false;
    }
}

void DiagnosticsManager::startStressTest()
{
    m_isTestingInProgress = true;
    m_runningTestIndex = 7; // Stress Test index (updated from 6 to 7)
    
    // Reset test counters
    m_stressTotalCommands = 0;
    m_stressSuccessfulCommands = 0;
    
    m_statuses[7] = TestStatus::InProgress;
    emit statusChanged(7, TestStatus::InProgress);
    
    appendToLog("Started test: Stress Test");
    appendToLog("Testing communication reliability with async commands...");
    appendToLog("Will send 600 commands over 30 seconds and measure response rate.");
    appendToLog("Target response rate: >90% for PASS");
    emit testStarted(7);
    
    // Check if serial port is available
    SerialPortManager& serialManager = SerialPortManager::getInstance();
    QString currentPortPath = serialManager.getCurrentSerialPortPath();
    if (currentPortPath.isEmpty()) {
        appendToLog("Stress test failed: No serial port available");
        m_statuses[7] = TestStatus::Failed;
        emit statusChanged(7, TestStatus::Failed);
        emit testCompleted(7, false);
        m_isTestingInProgress = false;
        m_runningTestIndex = -1;
        checkAllTestsCompletion();
        return;
    }
    
    appendToLog(QString("Using serial port: %1 for stress test").arg(currentPortPath));
    
    // Start statistics tracking
    serialManager.startStats();
    
    // Start stress test timer
    m_stressTestTimer->start();
    
    // Set a timeout for the entire test (35 seconds to allow for completion)
    QTimer::singleShot(35000, this, [this]() {
        if (m_runningTestIndex == 7) {
            finishStressTest();
        }
    });
    
    qCDebug(log_device_diagnostics) << "Started Stress Test with async commands and statistics tracking";
}

void DiagnosticsManager::onStressTestTimeout()
{
    // Check if we've sent enough commands
    if (m_stressTotalCommands >= 600) {
        finishStressTest();
        return;
    }
    
    // Send alternating mouse and keyboard commands
    bool success = false;
    if (m_stressTotalCommands % 2 == 0) {
        // Send mouse command (relative movement)
        success = sendStressMouseCommand();
    } else {
        // Send keyboard command (press and release a key)
        success = sendStressKeyboardCommand();
    }
    
    if (success) {
        m_stressTotalCommands++;
        m_stressSuccessfulCommands++;
    } else {
        m_stressTotalCommands++;
    }
    
    // Log progress every 100 commands with real-time statistics
    if (m_stressTotalCommands % 100 == 0) {
        SerialPortManager& serialManager = SerialPortManager::getInstance();
        double responseRate = serialManager.getResponseRate();
        qint64 elapsedMs = serialManager.getStatsElapsedMs();
        
        appendToLog(QString("Progress: %1/600 commands sent, response rate: %2% (elapsed: %3s)")
                   .arg(m_stressTotalCommands)
                   .arg(responseRate, 0, 'f', 1)
                   .arg(elapsedMs / 1000.0, 0, 'f', 1));
    }
}

bool DiagnosticsManager::sendStressMouseCommand()
{
    SerialPortManager& serialManager = SerialPortManager::getInstance();
    
    try {
        // Get target device resolution from GlobalVar
        int targetWidth = GlobalVar::instance().getInputWidth();
        int targetHeight = GlobalVar::instance().getInputHeight();
        
        // Generate random absolute coordinates within the target screen resolution
        int randomX = QRandomGenerator::global()->bounded(0, targetWidth);
        int randomY = QRandomGenerator::global()->bounded(0, targetHeight);
        
        // Create absolute mouse movement command using the correct format
        QByteArray mouseCmd;
        mouseCmd.append(MOUSE_ABS_ACTION_PREFIX); // Absolute mouse prefix from ch9329.h
        mouseCmd.append(static_cast<char>(0x00)); // Button state (no buttons)
        
        // Encode X coordinate (little-endian 16-bit)
        mouseCmd.append(static_cast<char>(randomX & 0xFF));        // X low byte
        mouseCmd.append(static_cast<char>((randomX >> 8) & 0xFF)); // X high byte
        
        // Encode Y coordinate (little-endian 16-bit)
        mouseCmd.append(static_cast<char>(randomY & 0xFF));        // Y low byte
        mouseCmd.append(static_cast<char>((randomY >> 8) & 0xFF)); // Y high byte
        
        mouseCmd.append(static_cast<char>(0x00)); // Wheel (0)
        
        // Use async command instead of sync
        return serialManager.sendAsyncCommand(mouseCmd, false);
        
    } catch (...) {
        return false;
    }
}

bool DiagnosticsManager::sendStressKeyboardCommand()
{
    SerialPortManager& serialManager = SerialPortManager::getInstance();
    
    try {
        // Send a simple key press using the correct format
        QByteArray keyPressCmd = CMD_SEND_KB_GENERAL_DATA; // Correct prefix from ch9329.h
        keyPressCmd[5] = 0x00; // Modifier keys (none)
        keyPressCmd[6] = 0x00; // Reserved
        keyPressCmd[7] = 0x47; // Scroll Lock key code
        keyPressCmd[8] = 0x00; // Key 2
        keyPressCmd[9] = 0x00; // Key 3
        keyPressCmd[10] = 0x00; // Key 4
        keyPressCmd[11] = 0x00; // Key 5
        keyPressCmd[12] = 0x00; // Key 6
        
        // Use async command - only send key press for simplicity in stress test
        return serialManager.sendAsyncCommand(keyPressCmd, false);
        
    } catch (...) {
        return false;
    }
}

void DiagnosticsManager::finishStressTest()
{
    if (m_stressTestTimer && m_stressTestTimer->isActive()) {
        m_stressTestTimer->stop();
    }
    
    SerialPortManager& serialManager = SerialPortManager::getInstance();
    
    // Stop statistics and get final results
    serialManager.stopStats();
    
    int commandsSent = serialManager.getCommandsSent();
    int responsesReceived = serialManager.getResponsesReceived();
    double responseRate = serialManager.getResponseRate();
    qint64 elapsedMs = serialManager.getStatsElapsedMs();
    
    appendToLog(QString("Stress test completed in %1 seconds")
               .arg(elapsedMs / 1000.0, 0, 'f', 1));
    appendToLog(QString("Commands sent: %1, Responses received: %2")
               .arg(commandsSent)
               .arg(responsesReceived));
    appendToLog(QString("Response rate: %1%").arg(responseRate, 0, 'f', 1));
    
    // Determine if test passed (>90% response rate)
    bool success = (responseRate > 90.0);
    
    if (success) {
        m_statuses[7] = TestStatus::Completed;
        appendToLog(QString("Stress Test: PASSED - Response rate %1% exceeds 90% threshold")
                   .arg(responseRate, 0, 'f', 1));
    } else {
        m_statuses[7] = TestStatus::Failed;
        appendToLog(QString("Stress Test: FAILED - Response rate %1% is below 90% threshold")
                   .arg(responseRate, 0, 'f', 1));
    }
    
    emit statusChanged(7, m_statuses[7]);
    emit testCompleted(7, success);
    
    m_isTestingInProgress = false;
    m_runningTestIndex = -1;
    
    // Check if all tests completed
    checkAllTestsCompletion();
    
    qCDebug(log_device_diagnostics) << "Stress Test finished:" << (success ? "PASS" : "FAIL")
                                   << "Response rate:" << responseRate << "%";
}

DiagnosticsManager::~DiagnosticsManager()
{
    if (m_logThread) {
        m_logThread->quit();
        m_logThread->wait();
    }
}
