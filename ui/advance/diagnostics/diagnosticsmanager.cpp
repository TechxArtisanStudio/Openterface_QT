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

#include "device/DeviceManager.h" // for device presence checks
#include "device/DeviceInfo.h"
#include "serial/SerialPortManager.h"
#include "serial/ch9329.h"
#include "global.h" // for GlobalVar to get screen resolution


DiagnosticsManager::DiagnosticsManager(QObject *parent)
    : QObject(parent)
    , m_testTimer(new QTimer(this))
    , m_targetCheckTimer(new QTimer(this))
    , m_runningTestIndex(-1)
    , m_isTestingInProgress(false)
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
        tr("Stress Test")
    };

    m_statuses.resize(m_testTitles.size());
    for (int i = 0; i < m_statuses.size(); ++i) {
        m_statuses[i] = TestStatus::NotStarted;
    }

    m_testTimer->setSingleShot(true);
    connect(m_testTimer, &QTimer::timeout, this, &DiagnosticsManager::onTimerTimeout);
    
    // Setup Target Plug & Play test timer
    m_targetCheckTimer->setInterval(500); // Check every 500ms
    connect(m_targetCheckTimer, &QTimer::timeout, this, &DiagnosticsManager::onTargetStatusCheckTimeout);
    
    // Setup Host Plug & Play test timer
    m_hostCheckTimer = new QTimer(this);
    m_hostCheckTimer->setInterval(500); // Check every 500ms
    connect(m_hostCheckTimer, &QTimer::timeout, this, &DiagnosticsManager::onHostStatusCheckTimeout);
    
    // Setup Stress Test timer
    m_stressTestTimer = new QTimer(this);
    m_stressTestTimer->setInterval(50); // Send command every 50ms (600 commands in 30 seconds)
    connect(m_stressTestTimer, &QTimer::timeout, this, &DiagnosticsManager::onStressTestTimeout);
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

    // Also write to file
    QString logPath = getLogFilePath();
    QFile logFile(logPath);
    if (logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        QTextStream out(&logFile);
        out << logEntry << "\n";
        logFile.close();
    }
}

void DiagnosticsManager::startTest(int testIndex)
{
    if (m_isTestingInProgress)
        return;
    if (testIndex < 0 || testIndex >= m_testTitles.size())
        return;

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
    
    // Special-case: Stress Test (index 6) -> perform mouse/keyboard stress testing
    if (testIndex == 6) {
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
    if (m_targetCheckTimer->isActive()) m_targetCheckTimer->stop();
    if (m_hostCheckTimer->isActive()) m_hostCheckTimer->stop();
    if (m_stressTestTimer && m_stressTestTimer->isActive()) m_stressTestTimer->stop();

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
    appendToLog("Test requires detecting 2 plug-in events to complete successfully.");
    appendToLog("Test will timeout after 10 seconds if not completed.");
    emit testStarted(1);
    
    // Check initial target connection status
    m_targetPreviouslyConnected = checkTargetConnectionStatus();
    m_targetCurrentlyConnected = m_targetPreviouslyConnected;
    
    if (m_targetPreviouslyConnected) {
        appendToLog("Target initially connected. Please unplug the cable first, then plug it back in twice.");
    } else {
        appendToLog("Target initially disconnected. Please plug in the cable (need 2 plug-in events total).");
    }
    
    // Start periodic checking
    m_targetCheckTimer->start();
    
    qCDebug(log_device_diagnostics) << "Started Target Plug & Play test";
}

void DiagnosticsManager::onTargetStatusCheckTimeout()
{
    m_targetTestElapsedTime += 500; // Timer interval is 500ms
    
    bool currentStatus = checkTargetConnectionStatus();
    
    // Detect state changes
    if (currentStatus != m_targetCurrentlyConnected) {
        m_targetCurrentlyConnected = currentStatus;
        
        if (!currentStatus && m_targetPreviouslyConnected) {
            // Target was unplugged
            m_targetUnplugDetected = true;
            appendToLog("Target cable unplugged detected!");
            int remainingPlugs = 2 - m_targetPlugCount;
            appendToLog(QString("Please plug it back in (need %1 more plug-in events)...").arg(remainingPlugs));
        } else if (currentStatus && !m_targetPreviouslyConnected) {
            // Target was plugged in
            m_targetPlugCount++;
            appendToLog(QString("Target cable plugged in detected! (Count: %1/2)").arg(m_targetPlugCount));
            
            if (m_targetPlugCount >= 2) {
                // Test completed successfully - we've detected 2 plug-in events
                m_targetCheckTimer->stop();
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
        
        m_targetPreviouslyConnected = currentStatus;
    }
    
    // Check for timeout (10 seconds)
    if (m_targetTestElapsedTime >= 10000) {
        m_targetCheckTimer->stop();
        m_statuses[1] = TestStatus::Failed;
        emit statusChanged(1, TestStatus::Failed);
        emit testCompleted(1, false);
        
        appendToLog(QString("Target Plug & Play test: FAILED - Only detected %1/2 plug-in events within 10 seconds").arg(m_targetPlugCount));
        
        m_isTestingInProgress = false;
        m_runningTestIndex = -1;
        
        // Check if all tests completed
        checkAllTestsCompletion();
    }
}

bool DiagnosticsManager::checkTargetConnectionStatus()
{
    // Get serial port manager instance
    DeviceManager& deviceManager = DeviceManager::getInstance();
    QList<DeviceInfo> devices = deviceManager.getCurrentDevices();
    
    // Find a device with serial port to send command
    for (const DeviceInfo& device : devices) {
        if (!device.serialPortPath.isEmpty()) {
            // Try to get SerialPortManager instance and send CMD_GET_INFO
            SerialPortManager& serialManager = SerialPortManager::getInstance();
            if (serialManager.getCurrentSerialPortPath() == device.serialPortPath) {
                // Send CMD_GET_INFO command and parse response
                QByteArray response = serialManager.sendSyncCommand(CMD_GET_INFO, false);
                if (!response.isEmpty() && response.size() >= static_cast<int>(sizeof(CmdGetInfoResult))) {
                    CmdGetInfoResult result = CmdGetInfoResult::fromByteArray(response);
                    bool isConnected = (result.targetConnected != 0);
                    
                    qCDebug(log_device_diagnostics) << "Target connection status:" << isConnected 
                                                   << "Response:" << response.toHex(' ');
                    return isConnected;
                }
            }
            break;
        }
    }
    
    // If no serial port available or command failed, assume disconnected
    return false;
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
    appendToLog("Testing target connection status with 3 attempts (1 second interval)...");
    
    // Retry up to 3 times with 1 second intervals
    for (int attempt = 1; attempt <= 3; attempt++) {
        appendToLog(QString("Attempt %1/3: Sending CMD_GET_INFO command...").arg(attempt));
        
        try {
            // Send CMD_GET_INFO command and wait for response
            QByteArray response = serialManager.sendSyncCommand(CMD_GET_INFO, false);
            
            if (response.isEmpty()) {
                appendToLog(QString("Attempt %1: No response received from serial port").arg(attempt));
            } else {
                appendToLog(QString("Attempt %1: Received response: %2").arg(attempt).arg(QString(response.toHex(' '))));
                
                // Validate response structure
                if (response.size() >= static_cast<int>(sizeof(CmdGetInfoResult))) {
                    CmdGetInfoResult result = CmdGetInfoResult::fromByteArray(response);
                    
                    // Check if response has valid header (0x57AB)
                    if (result.prefix == 0xAB57) { // Little-endian format
                        appendToLog(QString("Attempt %1: Valid response - Version: %2, Target Connected: %3")
                                   .arg(attempt)
                                   .arg(result.version)
                                   .arg(result.targetConnected ? "Yes" : "No"));
                        
                        // Check if target is connected
                        if (result.targetConnected != 0) {
                            appendToLog(QString("Target connection detected on attempt %1 - Test PASSED").arg(attempt));
                            return true;
                        } else {
                            appendToLog(QString("Attempt %1: Target not connected").arg(attempt));
                        }
                    } else {
                        appendToLog(QString("Attempt %1: Invalid response header: 0x%2 (expected 0x57AB)")
                                   .arg(attempt)
                                   .arg(result.prefix, 4, 16, QChar('0')));
                    }
                } else {
                    appendToLog(QString("Attempt %1: Response too short: %2 bytes (expected at least %3 bytes)")
                               .arg(attempt)
                               .arg(response.size())
                               .arg(sizeof(CmdGetInfoResult)));
                }
            }
            
        } catch (const std::exception& e) {
            appendToLog(QString("Attempt %1: Exception during serial communication: %2").arg(attempt).arg(e.what()));
        } catch (...) {
            appendToLog(QString("Attempt %1: Unknown error during serial communication").arg(attempt));
        }
        
        // Wait 1 second before next attempt (except for the last attempt)
        if (attempt < 3) {
            appendToLog("Waiting 1 second before next attempt...");
            QEventLoop loop;
            QTimer::singleShot(1000, &loop, &QEventLoop::quit);
            loop.exec();
        }
    }
    
    appendToLog("All 3 attempts completed - Target connection not detected");
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
        // Step 1: Test current communication before reset
        appendToLog("Step 1: Testing communication before factory reset...");
        QByteArray preResetResponse = serialManager.sendSyncCommand(CMD_GET_INFO, true);
        bool preResetSuccess = false;
        
        if (!preResetResponse.isEmpty() && preResetResponse.size() >= static_cast<int>(sizeof(CmdGetInfoResult))) {
            CmdGetInfoResult preResult = CmdGetInfoResult::fromByteArray(preResetResponse);
            if (preResult.prefix == 0xAB57) {
                appendToLog(QString("Pre-reset communication successful - version: %1").arg(preResult.version));
                preResetSuccess = true;
            }
        }
        
        if (!preResetSuccess) {
            appendToLog("Pre-reset communication failed - device may not be responding");
            return false;
        }
        
        // Step 2: Attempt standard factory reset (RTS pin method)
        appendToLog("Step 2: Performing standard factory reset (RTS pin method)...");
        appendToLog("This will hold RTS pin low for 4 seconds, then reconnect...");
        
        bool resetSuccess = serialManager.factoryResetHipChipSync(10000); // 10 second timeout
        
        if (resetSuccess) {
            appendToLog("Step 2: Standard factory reset completed successfully");
            
            // Step 3: Verify communication after reset
            appendToLog("Step 3: Verifying communication after factory reset...");
            
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
                appendToLog("Step 3: Factory reset verification successful!");
                appendToLog("Device has been reset to factory defaults and is responding correctly.");
                return true;
            } else {
                appendToLog("Step 3: Factory reset verification failed - device not responding properly");
                return false;
            }
        } else {
            appendToLog("Step 2: Standard factory reset failed");
            
            // Step 4: Try V191 command method as fallback
            appendToLog("Step 4: Trying V191 factory reset method (command-based) as fallback...");
            
            bool v191Success = serialManager.factoryResetHipChipV191Sync(5000); // 5 second timeout
            
            if (v191Success) {
                appendToLog("Step 4: V191 factory reset completed successfully");
                
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
                appendToLog("Step 4: V191 factory reset also failed");
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
        appendToLog("Already at 115200 baudrate, testing communication...");
        QByteArray testResponse = serialManager.sendSyncCommand(CMD_GET_INFO, true);
        if (!testResponse.isEmpty()) {
            CmdGetInfoResult infoResult = CmdGetInfoResult::fromByteArray(testResponse);
            appendToLog(QString("Communication test successful at 115200 - received response (version: %1)").arg(infoResult.version));
            return true;
        } else {
            appendToLog("Communication test failed at 115200 baudrate");
            return false;
        }
    }
    
    try {
        // First, test current communication to ensure we have a baseline
        appendToLog(QString("Testing baseline communication at %1...").arg(currentBaudrate));
        
        // Try with force=true to ensure command is sent even if ready state is uncertain
        QByteArray baselineResponse = serialManager.sendSyncCommand(CMD_GET_INFO, true);
        if (baselineResponse.isEmpty()) {
            // If first attempt fails, wait a bit and try again 
            appendToLog("First baseline attempt failed, waiting and retrying...");
            QEventLoop retryWait;
            QTimer::singleShot(1000, &retryWait, &QEventLoop::quit);  // 1 second wait
            retryWait.exec();
            
            baselineResponse = serialManager.sendSyncCommand(CMD_GET_INFO, true);
            if (baselineResponse.isEmpty()) {
                appendToLog("Baseline communication test failed after retry - cannot proceed with baudrate test");
                return false;
            }
        }
        
        CmdGetInfoResult baselineResult = CmdGetInfoResult::fromByteArray(baselineResponse);
        appendToLog(QString("Baseline communication successful (version: %1)").arg(baselineResult.version));
        
        // Method: Proper command-based baudrate switching
        appendToLog("Performing proper command-based baudrate switching to 115200...");
        
        // Step 1: Send configuration command at current baudrate (following applyCommandBasedBaudrateChange pattern)
        appendToLog("Step 1: Sending baudrate configuration command at current rate...");
        
        // Get system settings for mode
        static QSettings settings("Techxartisan", "Openterface");
        uint8_t mode = (settings.value("hardware/operatingMode", 0x02).toUInt());
        
        // Build configuration command for 115200
        QByteArray command = CMD_SET_PARA_CFG_PREFIX_115200;
        command[5] = mode;  // Set mode byte
        command.append(CMD_SET_PARA_CFG_MID);
        
        appendToLog("Sending configuration command to device...");
        QByteArray configResponse = serialManager.sendSyncCommand(command, true);
        
        if (configResponse.isEmpty()) {
            appendToLog("Configuration command failed - no response from device");
            return false;
        }
        
        // Parse configuration response
        CmdDataResult configResult = CmdDataResult::fromByteArray(configResponse);
        if (configResult.data != DEF_CMD_SUCCESS) {
            appendToLog(QString("Configuration command failed with status: 0x%1").arg(configResult.data, 2, 16, QChar('0')));
            return false;
        }
        
        appendToLog("Configuration command successful");
        
        // Step 2: Send reset command at current baudrate
        appendToLog("Step 2: Sending reset command...");
        QByteArray resetResponse = serialManager.sendSyncCommand(CMD_RESET, true);
        
        if (resetResponse.isEmpty()) {
            appendToLog("Reset command failed - no response from device");
            return false;
        }
        
        appendToLog("Reset command successful");
        
        // Step 3: Wait for reset to complete
        appendToLog("Step 3: Waiting for device reset to complete...");
        QEventLoop resetWaitLoop;
        QTimer::singleShot(1000, &resetWaitLoop, &QEventLoop::quit);  // 1 second wait
        resetWaitLoop.exec();
        
        // Step 4: Set host-side baudrate to 115200
        appendToLog("Step 4: Setting host-side baudrate to 115200...");
        bool setBaudrateResult = serialManager.setBaudRate(SerialPortManager::BAUDRATE_HIGHSPEED);
        if (!setBaudrateResult) {
            appendToLog("Failed to set host-side baudrate to 115200");
            return false;
        }
        
        // Step 5: 增加波特率切换等待时间
        appendToLog("Step 5: Waiting for baudrate change to stabilize...");
        QEventLoop stabilizeLoop;
        QTimer::singleShot(1500, &stabilizeLoop, &QEventLoop::quit); // 增加到1.5秒
        stabilizeLoop.exec();
        
        // Step 6: 验证波特率设置
        int newBaudrate = serialManager.getCurrentBaudrate();
        appendToLog(QString("Host-side baudrate now set to: %1").arg(newBaudrate));
        
        if (newBaudrate != SerialPortManager::BAUDRATE_HIGHSPEED) {
            appendToLog(QString("Host-side baudrate mismatch - expected 115200, got %1").arg(newBaudrate));
            return false;
        }
        
        // Step 7: 多次尝试高速通信测试
        appendToLog("Step 7: Testing communication at 115200 baudrate...");
        
        bool communicationSuccess = false;
        for (int attempt = 1; attempt <= 3; attempt++) {
            appendToLog(QString("High-speed communication attempt %1/3...").arg(attempt));
            
            QByteArray highSpeedResponse = serialManager.sendSyncCommand(CMD_GET_INFO, true);
            
            if (!highSpeedResponse.isEmpty()) {
                CmdGetInfoResult highSpeedResult = CmdGetInfoResult::fromByteArray(highSpeedResponse);
                if (highSpeedResult.prefix == 0xAB57) {
                    appendToLog(QString("High-speed communication successful on attempt %1 - version: %2")
                               .arg(attempt).arg(highSpeedResult.version));
                    communicationSuccess = true;
                    break;
                } else {
                    appendToLog(QString("Attempt %1: Invalid response header: 0x%2")
                               .arg(attempt).arg(highSpeedResult.prefix, 4, 16, QChar('0')));
                }
            } else {
                appendToLog(QString("Attempt %1: No response received").arg(attempt));
            }
            
            // 在重试之间等待
            if (attempt < 3) {
                appendToLog("Waiting 500ms before retry...");
                QEventLoop retryWait;
                QTimer::singleShot(500, &retryWait, &QEventLoop::quit);
                retryWait.exec();
            }
        }
        
        if (communicationSuccess) {
            appendToLog("Baudrate switch to 115200 completed successfully!");
            return true;
        } else {
            appendToLog("High-speed communication failed - device may not have switched baudrates");
            
            // Recovery: Try to restore original baudrate
            appendToLog("Attempting to recover by restoring original baudrate...");
            bool restoreBaudrate = serialManager.setBaudRate(currentBaudrate);
            if (restoreBaudrate) {
                QEventLoop recoveryLoop;
                QTimer::singleShot(500, &recoveryLoop, &QEventLoop::quit);
                recoveryLoop.exec();
                
                QByteArray recoveryResponse = serialManager.sendSyncCommand(CMD_GET_INFO, false);
                if (!recoveryResponse.isEmpty()) {
                    appendToLog("Successfully restored communication at original baudrate");
                    appendToLog("High baudrate test failed: device did not switch to 115200");
                } else {
                    appendToLog("Failed to restore communication - serial connection may be broken");
                }
            }
            return false;
        }
        
    } catch (const std::exception& e) {
        appendToLog(QString("High baudrate test failed due to exception: %1").arg(e.what()));
        return false;
    } catch (...) {
        appendToLog("High baudrate test failed due to unknown error");
        return false;
    }
}

void DiagnosticsManager::startStressTest()
{
    m_isTestingInProgress = true;
    m_runningTestIndex = 6; // Stress Test index
    
    // Reset test counters
    m_stressTotalCommands = 0;
    m_stressSuccessfulCommands = 0;
    
    m_statuses[6] = TestStatus::InProgress;
    emit statusChanged(6, TestStatus::InProgress);
    
    appendToLog("Started test: Stress Test");
    appendToLog("Testing communication reliability with async commands...");
    appendToLog("Will send 600 commands over 30 seconds and measure response rate.");
    appendToLog("Target response rate: >90% for PASS");
    emit testStarted(6);
    
    // Check if serial port is available
    SerialPortManager& serialManager = SerialPortManager::getInstance();
    QString currentPortPath = serialManager.getCurrentSerialPortPath();
    if (currentPortPath.isEmpty()) {
        appendToLog("Stress test failed: No serial port available");
        m_statuses[6] = TestStatus::Failed;
        emit statusChanged(6, TestStatus::Failed);
        emit testCompleted(6, false);
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
        if (m_runningTestIndex == 6) {
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
        m_statuses[6] = TestStatus::Completed;
        appendToLog(QString("Stress Test: PASSED - Response rate %1% exceeds 90% threshold")
                   .arg(responseRate, 0, 'f', 1));
    } else {
        m_statuses[6] = TestStatus::Failed;
        appendToLog(QString("Stress Test: FAILED - Response rate %1% is below 90% threshold")
                   .arg(responseRate, 0, 'f', 1));
    }
    
    emit statusChanged(6, m_statuses[6]);
    emit testCompleted(6, success);
    
    m_isTestingInProgress = false;
    m_runningTestIndex = -1;
    
    // Check if all tests completed
    checkAllTestsCompletion();
    
    qCDebug(log_device_diagnostics) << "Stress Test finished:" << (success ? "PASS" : "FAIL")
                                   << "Response rate:" << responseRate << "%";
}
