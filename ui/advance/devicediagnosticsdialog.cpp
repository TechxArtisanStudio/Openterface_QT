#include "devicediagnosticsdialog.h"
#include <QMessageBox>
#include <QApplication>
#include <QLoggingCategory>
#include <QRandomGenerator>
#include <QStyle>
#include <QDesktopServices>
#include <QUrl>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <QSvgWidget>
#include "diagnostics/diagnosticsmanager.h"
#include "diagnostics/diagnostics_constants.h"
#include "diagnostics/SupportEmailDialog.h"
#include "../../serial/SerialPortManager.h"

Q_LOGGING_CATEGORY(log_device_diagnostics, "opf.diagnostics")

TestItem::TestItem(const QString &title, int testIndex, QListWidget *parent)
    : QListWidgetItem(title, parent)
    , m_status(TestStatus::NotStarted)
    , m_testIndex(testIndex)
    , m_title(title)
{
    updateIcon();
}

void TestItem::setTestStatus(TestStatus status)
{
    m_status = status;
    updateIcon();
}

void TestItem::updateIcon()
{
    QIcon icon;
    QString tooltip;
    
    switch (m_status) {
    case TestStatus::NotStarted:
        icon = QApplication::style()->standardIcon(QStyle::SP_DialogResetButton);
        tooltip = QObject::tr("Test not started");
        break;
    case TestStatus::InProgress:
        icon = QApplication::style()->standardIcon(QStyle::SP_BrowserReload);
        tooltip = QObject::tr("Test in progress...");
        break;
    case TestStatus::Completed:
        icon = QApplication::style()->standardIcon(QStyle::SP_DialogApplyButton);
        tooltip = QObject::tr("Test completed successfully");
        break;
    case TestStatus::Failed:
        icon = QApplication::style()->standardIcon(QStyle::SP_DialogCancelButton);
        tooltip = QObject::tr("Test failed");
        break;
    }
    
    setIcon(icon);
    setToolTip(tooltip);
}

DeviceDiagnosticsDialog::DeviceDiagnosticsDialog(QWidget *parent)
    : QDialog(parent)
    , m_mainLayout(nullptr)
    , m_splitter(nullptr)
    , m_testListGroup(nullptr)
    , m_testList(nullptr)
    , m_rightPanel(nullptr)
    , m_rightLayout(nullptr)
    , m_testTitleLabel(nullptr)
    , m_statusIconLabel(nullptr)
    , m_reminderLabel(nullptr)
    , m_logFileButton(nullptr)
    , m_logDisplayText(nullptr)
    , m_buttonLayout(nullptr)
    , m_restartButton(nullptr)
    , m_previousButton(nullptr)
    , m_nextButton(nullptr)
    , m_checkNowButton(nullptr)
    , m_supportEmailButton(nullptr)
    , m_currentTestIndex(0)
    , m_connectionSvg(nullptr)
    , m_svgAnimationTimer(nullptr)
    , m_svgAnimationState(false)
    , m_diagnosticsCompleted(false)
{
    setWindowTitle(tr(Diagnostics::WINDOW_TITLE));
    setMinimumSize(900, 600);
    setAttribute(Qt::WA_DeleteOnClose);
    
    setupUI();

    // Initialize SVG animation timer
    m_svgAnimationTimer = new QTimer(this);
    m_svgAnimationTimer->setInterval(500);  // Toggle every 500ms
    connect(m_svgAnimationTimer, &QTimer::timeout, this, [this]() {
        m_svgAnimationState = !m_svgAnimationState;
        updateConnectionSvg();
    });

    // Backend manager for test data and logic
    m_manager = new DiagnosticsManager(this);

    // Populate tests from manager
    m_testTitles = m_manager->testTitles();
    for (int i = 0; i < m_testTitles.size(); ++i) {
        TestItem* item = new TestItem(m_testTitles[i], i, m_testList);
        m_testList->addItem(item);
    }
    if (m_testList->count() > 0) {
        m_testList->setCurrentRow(0);
    }
    showTestPage(0);

    // Connect manager signals to update UI
    connect(m_manager, &DiagnosticsManager::statusChanged, this, [this](int idx, TestStatus st){
        TestItem* item = static_cast<TestItem*>(m_testList->item(idx));
        if (item) item->setTestStatus(st);
        if (idx == m_currentTestIndex) {
            QIcon icon;
            switch (st) {
            case TestStatus::NotStarted:
                icon = style()->standardIcon(QStyle::SP_ComputerIcon);
                stopSvgAnimation();
                break;
            case TestStatus::InProgress:
                icon = style()->standardIcon(QStyle::SP_BrowserReload);
                // Start animation for Target (1) or Host (2) Plug & Play tests
                if (idx == 1 || idx == 2) {
                    startSvgAnimation();
                }
                break;
            case TestStatus::Completed:
                icon = style()->standardIcon(QStyle::SP_DialogApplyButton);
                stopSvgAnimation();
                break;
            case TestStatus::Failed:
                icon = style()->standardIcon(QStyle::SP_DialogCancelButton);
                stopSvgAnimation();
                break;
            }
            m_statusIconLabel->setPixmap(icon.pixmap(24, 24));
            updateConnectionSvg();
        }
        updateNavigationButtons();
    });

    connect(m_manager, &DiagnosticsManager::logAppended, this, &DeviceDiagnosticsDialog::onLogAppended);
    connect(m_manager, &DiagnosticsManager::testStarted, this, &DeviceDiagnosticsDialog::testStarted);
    connect(m_manager, &DiagnosticsManager::testCompleted, this, &DeviceDiagnosticsDialog::testCompleted);
    connect(m_manager, &DiagnosticsManager::diagnosticsCompleted, this, &DeviceDiagnosticsDialog::onDiagnosticsCompleted);
    // Forward manager's completion signal to maintain backward compatibility (no-arg signal)
    connect(m_manager, &DiagnosticsManager::diagnosticsCompleted, this, [this](bool){ emit diagnosticsCompleted(); });

    qCDebug(log_device_diagnostics) << "Device Diagnostics Dialog created";

    // Notify SerialPortManager to suppress periodic GET_INFO while diagnostics dialog is active
    SerialPortManager* spm = &SerialPortManager::getInstance();
    if (spm) {
        QMetaObject::invokeMethod(spm, "setDiagnosticsDialogActive", Qt::QueuedConnection, Q_ARG(bool, true));
    }
}

DeviceDiagnosticsDialog::~DeviceDiagnosticsDialog()
{
    qCDebug(log_device_diagnostics) << "Device Diagnostics Dialog destroyed";

    // Restore periodic GET_INFO polling in SerialPortManager
    SerialPortManager* spm = &SerialPortManager::getInstance();
    if (spm) {
        QMetaObject::invokeMethod(spm, "setDiagnosticsDialogActive", Qt::QueuedConnection, Q_ARG(bool, false));
    }
}

void DeviceDiagnosticsDialog::setupUI()
{
    // Main layout
    m_mainLayout = new QHBoxLayout(this);
    m_mainLayout->setContentsMargins(10, 10, 10, 10);
    m_mainLayout->setSpacing(10);
    
    // Create splitter for resizable panels
    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_mainLayout->addWidget(m_splitter);
    
    setupLeftPanel();
    setupRightPanel();
    
    // Set splitter proportions (30% left, 70% right)
    m_splitter->setSizes({270, 630});
}

void DeviceDiagnosticsDialog::setupLeftPanel()
{
    // Left panel - Test list
    m_testListGroup = new QGroupBox(tr("Diagnostic Tests"), this);
    m_testListGroup->setMinimumWidth(250);
    
    QVBoxLayout* leftLayout = new QVBoxLayout(m_testListGroup);
    
    m_testList = new QListWidget(this);
    m_testList->setSelectionMode(QAbstractItemView::SingleSelection);
    connect(m_testList, &QListWidget::itemClicked, this, &DeviceDiagnosticsDialog::onTestItemClicked);
    
    leftLayout->addWidget(m_testList);
    
    m_splitter->addWidget(m_testListGroup);
}

void DeviceDiagnosticsDialog::setupRightPanel()
{
    // Right panel - Test details
    m_rightPanel = new QWidget(this);
    m_rightLayout = new QVBoxLayout(m_rightPanel);
    m_rightLayout->setContentsMargins(15, 15, 15, 15);
    m_rightLayout->setSpacing(15);
    
    // Top section: Title + SVG icon (left column holds title + reminder)
    QHBoxLayout* titleLayout = new QHBoxLayout();
    titleLayout->setSpacing(10);

    // Left column: title row (title + status icon) and reminder underneath
    QVBoxLayout* leftColumn = new QVBoxLayout();
    leftColumn->setSpacing(6);

    QHBoxLayout* titleRow = new QHBoxLayout();
    titleRow->setSpacing(8);

    m_testTitleLabel = new QLabel(this);
    m_testTitleLabel->setStyleSheet("QLabel { font-size: 16px; font-weight: bold; }");
    titleRow->addWidget(m_testTitleLabel);

    // Status icon next to the title
    m_statusIconLabel = new QLabel(this);
    m_statusIconLabel->setFixedSize(24, 24);
    m_statusIconLabel->setScaledContents(true);
    m_statusIconLabel->setPixmap(style()->standardIcon(QStyle::SP_ComputerIcon).pixmap(24, 24));
    titleRow->addWidget(m_statusIconLabel);

    titleRow->addStretch(); // push title and icon to the left within the left column
    leftColumn->addLayout(titleRow);

    // Small reminder text directly under the title
    m_reminderLabel = new QLabel(this);
    m_reminderLabel->setStyleSheet("QLabel { font-size: 11px; }");
    m_reminderLabel->setWordWrap(true);
    leftColumn->addWidget(m_reminderLabel);

    // Give left column a smaller proportion (40%) and SVG the larger (60%)
    titleLayout->addLayout(leftColumn, 2);

    // Connection status strip (wide, taller with fixed gray background)
    m_connectionSvg = new QSvgWidget(this);
    m_connectionSvg->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_connectionSvg->setFixedHeight(160);
    m_connectionSvg->setStyleSheet("background-color: #6b6b6b; border-radius: 6px; padding: 10px;");
    // Assign stretch factor 3 so SVG gets approximately 60% of the horizontal space
    titleLayout->addWidget(m_connectionSvg, 3);

    m_rightLayout->addLayout(titleLayout);
    
    // Add separator line
    QFrame* line1 = new QFrame(this);
    line1->setFrameShape(QFrame::HLine);
    line1->setFrameShadow(QFrame::Sunken);
    m_rightLayout->addWidget(line1);
    
    // Log file section
    QHBoxLayout* logFileLayout = new QHBoxLayout();
    logFileLayout->setSpacing(10);
    
    QLabel* logFileLabel = new QLabel(tr("Test Log:"), this);
    logFileLabel->setStyleSheet("QLabel { font-weight: bold; }");
    logFileLayout->addWidget(logFileLabel);
    
    m_logFileButton = new QPushButton(tr(Diagnostics::LOG_FILE_NAME), this);
    m_logFileButton->setStyleSheet(
        "QPushButton {"
        "   text-decoration: underline;"
        "   border: none;"
        "   background: transparent;"
        "   text-align: left;"
        "   padding: 2px;"
        "}"
    );
    m_logFileButton->setCursor(Qt::PointingHandCursor);
    connect(m_logFileButton, &QPushButton::clicked, this, &DeviceDiagnosticsDialog::onOpenLogFileClicked);
    logFileLayout->addWidget(m_logFileButton);
    
    logFileLayout->addStretch();
    m_rightLayout->addLayout(logFileLayout);
    
    // Add separator line
    QFrame* line2 = new QFrame(this);
    line2->setFrameShape(QFrame::HLine);
    line2->setFrameShadow(QFrame::Sunken);
    m_rightLayout->addWidget(line2);
    
    // Log display area (replaces instructions text)
    m_logDisplayText = new QTextEdit(this);
    m_logDisplayText->setReadOnly(true);
    m_logDisplayText->setStyleSheet(
        "QTextEdit {"
        "   border-radius: 5px;"
        "   padding: 10px;"
        "   font-size: 11px;"
        "   font-family: 'Consolas', 'Monaco', monospace;"
        "}"
    );
    m_logDisplayText->setPlainText(tr(Diagnostics::LOG_PLACEHOLDER));
    m_rightLayout->addWidget(m_logDisplayText);
    
    // Spacer to push buttons to bottom
    m_rightLayout->addStretch();
    
    // Button layout
    m_buttonLayout = new QHBoxLayout();
    m_buttonLayout->setSpacing(10);
    
    m_restartButton = new QPushButton(tr("Restart"), this);
    m_restartButton->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
    m_restartButton->setMinimumHeight(35);
    connect(m_restartButton, &QPushButton::clicked, this, &DeviceDiagnosticsDialog::onRestartClicked);
    
    m_previousButton = new QPushButton(tr("Previous"), this);
    m_previousButton->setIcon(style()->standardIcon(QStyle::SP_ArrowLeft));
    m_previousButton->setMinimumHeight(35);
    connect(m_previousButton, &QPushButton::clicked, this, &DeviceDiagnosticsDialog::onPreviousClicked);
    
    m_nextButton = new QPushButton(tr("Next"), this);
    m_nextButton->setIcon(style()->standardIcon(QStyle::SP_ArrowRight));
    m_nextButton->setMinimumHeight(35);
    connect(m_nextButton, &QPushButton::clicked, this, &DeviceDiagnosticsDialog::onNextClicked);
    
    m_checkNowButton = new QPushButton(tr("Check Now"), this);
    m_checkNowButton->setIcon(style()->standardIcon(QStyle::SP_DialogApplyButton));
    m_checkNowButton->setMinimumHeight(35);
    connect(m_checkNowButton, &QPushButton::clicked, this, &DeviceDiagnosticsDialog::onCheckNowClicked);
    
    m_supportEmailButton = new QPushButton(tr("Support Email"), this);
    m_supportEmailButton->setIcon(style()->standardIcon(QStyle::SP_MessageBoxInformation));
    m_supportEmailButton->setMinimumHeight(35);
    connect(m_supportEmailButton, &QPushButton::clicked, this, &DeviceDiagnosticsDialog::onSupportEmailClicked);
    
    m_buttonLayout->addWidget(m_restartButton);
    m_buttonLayout->addStretch();
    m_buttonLayout->addWidget(m_previousButton);
    m_buttonLayout->addWidget(m_nextButton);
    m_buttonLayout->addWidget(m_checkNowButton);
    m_buttonLayout->addWidget(m_supportEmailButton);
    
    m_rightLayout->addLayout(m_buttonLayout);
    
    m_splitter->addWidget(m_rightPanel);
}



void DeviceDiagnosticsDialog::showTestPage(int index)
{
    if (index < 0 || index >= m_testTitles.size()) {
        return;
    }
    
    m_currentTestIndex = index;
    
    // Update UI
    m_testTitleLabel->setText(m_testTitles[index]);
    
    // Update reminder text based on test (moved to diagnostics constants)
    const char* reminders[] = {
        Diagnostics::REMINDERS[0],
        Diagnostics::REMINDERS[1],
        Diagnostics::REMINDERS[2],
        Diagnostics::REMINDERS[3],
        Diagnostics::REMINDERS[4],
        Diagnostics::REMINDERS[5],
        Diagnostics::REMINDERS[6]
    };

    QString reminder = (index >= 0 && index < 7) ? tr(reminders[index]) : tr(Diagnostics::FOLLOW_INSTRUCTIONS);
    m_reminderLabel->setText(reminder);
    
    // Update status icon based on current test status (from manager)
    TestStatus status = TestStatus::NotStarted;
    if (m_manager) {
        status = m_manager->testStatus(index);
    } else {
        TestItem* currentItem = static_cast<TestItem*>(m_testList->item(index));
        if (currentItem) status = currentItem->getTestStatus();
    }

    QIcon icon;
    switch (status) {
        case TestStatus::NotStarted:
            icon = style()->standardIcon(QStyle::SP_ComputerIcon);
            break;
        case TestStatus::InProgress:
            icon = style()->standardIcon(QStyle::SP_BrowserReload);
            break;
        case TestStatus::Completed:
            icon = style()->standardIcon(QStyle::SP_DialogApplyButton);
            break;
        case TestStatus::Failed:
            icon = style()->standardIcon(QStyle::SP_DialogCancelButton);
            break;
    }
    m_statusIconLabel->setPixmap(icon.pixmap(24, 24));
    
    // Update connection SVG based on test index and status
    stopSvgAnimation();  // Stop any previous animation
    updateConnectionSvg();
    
    // Update list selection
    m_testList->setCurrentRow(index);
    
    // Update navigation buttons
    updateNavigationButtons();
}

void DeviceDiagnosticsDialog::updateNavigationButtons()
{
    m_previousButton->setEnabled(m_currentTestIndex > 0);
    m_nextButton->setEnabled(m_currentTestIndex < m_testTitles.size() - 1);
    
    // Update check button based on test status
    TestStatus status = TestStatus::NotStarted;
    if (m_manager) {
        status = m_manager->testStatus(m_currentTestIndex);
    } else {
        TestItem* currentItem = static_cast<TestItem*>(m_testList->item(m_currentTestIndex));
        if (currentItem) status = currentItem->getTestStatus();
    }

    if (status == TestStatus::InProgress) {
        m_checkNowButton->setText(tr("Testing..."));
        m_checkNowButton->setEnabled(false);
    } else {
        m_checkNowButton->setText(tr("Check Now"));
        m_checkNowButton->setEnabled(!(m_manager && m_manager->isTestingInProgress()));
    }
}

void DeviceDiagnosticsDialog::onRestartClicked()
{
    QMessageBox::StandardButton reply = QMessageBox::question(
        this, tr(Diagnostics::RESTART_TITLE),
        tr(Diagnostics::RESTART_CONFIRM),
        QMessageBox::Yes | QMessageBox::No);
        
    if (reply == QMessageBox::Yes) {
        // Stop any SVG animation
        stopSvgAnimation();
        // Clear UI log and delegate reset to manager
        m_logDisplayText->clear();
        if (m_manager) m_manager->resetAllTests();
        showTestPage(0);
        qCDebug(log_device_diagnostics) << "Diagnostics restarted";
    }
}

void DeviceDiagnosticsDialog::onPreviousClicked()
{
    if (m_currentTestIndex > 0) {
        showTestPage(m_currentTestIndex - 1);
    }
}

void DeviceDiagnosticsDialog::onNextClicked()
{
    if (m_currentTestIndex < m_testTitles.size() - 1) {
        showTestPage(m_currentTestIndex + 1);
    }
}

void DeviceDiagnosticsDialog::onCheckNowClicked()
{
    if (m_manager && m_manager->isTestingInProgress()) {
        return;
    }

    if (m_manager) {
        m_manager->startTest(m_currentTestIndex);
    }
}

void DeviceDiagnosticsDialog::onTestItemClicked(QListWidgetItem* item)
{
    TestItem* testItem = static_cast<TestItem*>(item);
    if (!testItem) return;

    if (m_manager && m_manager->isTestingInProgress()) return;

    showTestPage(testItem->getTestIndex());
}

void DeviceDiagnosticsDialog::onOpenLogFileClicked()
{
    if (!m_manager) return;

    QString logPath = m_manager->getLogFilePath();

    // Ensure log file exists
    QFileInfo fileInfo(logPath);
    if (!fileInfo.exists()) {
        QFile logFile(logPath);
        if (logFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&logFile);
            out << QString(tr(Diagnostics::TEST_LOG_HEADER)).arg(QDateTime::currentDateTime().toString());
            out << "=" << QString("=").repeated(50) << "\n\n";
            logFile.close();
        }
    }

    // Open the log file directory with system default application
    QString serialLog = m_manager->getSerialLogFilePath();
    QFileInfo serialInfo(serialLog);
    QString dirPath = QFileInfo(logPath).absolutePath();
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(dirPath))) {
        QMessageBox::warning(this, tr(Diagnostics::LOG_OPEN_ERROR_TITLE),
                           tr(Diagnostics::LOG_OPEN_ERROR).arg(dirPath));
    } else {
        QString infoMsg = tr("Please attach the diagnostics_log.txt file to your email.");
        if (!serialLog.isEmpty() && serialInfo.exists()) {
            infoMsg += tr("\nAlso attach the serial log file: %1").arg(serialInfo.fileName());
        }
        QMessageBox::information(this, tr("Log File"), infoMsg);
    }
}

void DeviceDiagnosticsDialog::onLogAppended(const QString &entry)
{
    if (!m_logDisplayText) return;
    m_logDisplayText->append(entry);
}

void DeviceDiagnosticsDialog::onDiagnosticsCompleted(bool allSuccessful)
{
    m_diagnosticsCompleted = true;
    stopSvgAnimation();  // Stop animation when diagnostics complete

    if (!allSuccessful) {
        // Collect failed tests
        QStringList failedTests;
        for (int i = 0; i < m_testTitles.size(); ++i) {
            TestStatus status = m_manager->testStatus(i);
            if (status == TestStatus::Failed) {
                failedTests.append(m_testTitles[i]);
            }
        }

        // Show support email dialog
        QString logFilePath = m_manager->getLogFilePath();
        QString serialLog = m_manager->getSerialLogFilePath();
        SupportEmailDialog* dialog = new SupportEmailDialog(failedTests, logFilePath, serialLog, true, this);
        dialog->exec();
    } else {
        QString message = tr(Diagnostics::DIAGNOSTICS_COMPLETE_SUCCESS);
        QMessageBox::information(this, tr("Diagnostics Complete"), message);
    }
}

void DeviceDiagnosticsDialog::updateConnectionSvg()
{
    if (!m_connectionSvg) return;
    
    TestStatus status = TestStatus::NotStarted;
    if (m_manager) {
        status = m_manager->testStatus(m_currentTestIndex);
    }
    
    QString svgPath;
    
    switch (m_currentTestIndex) {
        case 0:  // Overall Connection
            if (status == TestStatus::NotStarted) {
                // Before check: H0T0V0
                svgPath = ":/images/H0T0V0.svg";
            } else {
                // During/after check: H1T1V1
                svgPath = ":/images/H1T1V1.svg";
            }
            break;
            
        case 1:  // Target Plug & Play
            if (status == TestStatus::InProgress) {
                // During check: alternate between H1T1V1 and H1T0V1
                svgPath = m_svgAnimationState ? ":/images/H1T0V1.svg" : ":/images/H1T1V1.svg";
            } else {
                // Before/after check: H1T1V1
                svgPath = ":/images/H1T1V1.svg";
            }
            break;
            
        case 2:  // Host Plug & Play
            if (status == TestStatus::InProgress) {
                // During check: alternate between H1T1V1 and H0T1V1
                svgPath = m_svgAnimationState ? ":/images/H0T1V1.svg" : ":/images/H1T1V1.svg";
            } else {
                // Before/after check: H1T1V1
                svgPath = ":/images/H1T1V1.svg";
            }
            break;
            
        default:  // All other tests (3-7): always H1T1V1
            svgPath = ":/images/H1T1V1.svg";
            break;
    }
    
    m_connectionSvg->load(svgPath);
}

void DeviceDiagnosticsDialog::startSvgAnimation()
{
    if (m_svgAnimationTimer && !m_svgAnimationTimer->isActive()) {
        m_svgAnimationState = false;
        m_svgAnimationTimer->start();
    }
}

void DeviceDiagnosticsDialog::stopSvgAnimation()
{
    if (m_svgAnimationTimer && m_svgAnimationTimer->isActive()) {
        m_svgAnimationTimer->stop();
        m_svgAnimationState = false;
    }
}

void DeviceDiagnosticsDialog::onSupportEmailClicked()
{
    // Collect failed tests (even if not completed)
    QStringList failedTests;
    for (int i = 0; i < m_testTitles.size(); ++i) {
        TestStatus status = m_manager->testStatus(i);
        if (status == TestStatus::Failed) {
            failedTests.append(m_testTitles[i]);
        }
    }

    // If no failed tests and diagnostics not completed, show a message
    if (failedTests.isEmpty() && !m_diagnosticsCompleted) {
        failedTests.append(tr("Diagnostics not completed"));
    }

    // Show support email dialog
    QString logFilePath = m_manager->getLogFilePath();
    QString serialLog = m_manager->getSerialLogFilePath();
    SupportEmailDialog* dialog = new SupportEmailDialog(failedTests, logFilePath, serialLog, m_diagnosticsCompleted, this);
    dialog->exec();
}

