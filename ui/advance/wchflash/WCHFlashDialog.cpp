#include "WCHFlashDialog.h"
#include "WCHFlashWorker.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QComboBox>
#include <QLabel>
#include <QProgressBar>
#include <QTextEdit>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QApplication>
#include <QClipboard>
#include <QThread>
#include <QDateTime>
#include <QScrollBar>
#include <QMetaObject>
#include <QFont>

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------
WCHFlashDialog::WCHFlashDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("WCH ISP Flash Tool"));
    setMinimumSize(520, 620);
    setupUi();

    // Worker lives on a separate thread
    m_thread = new QThread(this);
    m_worker = new WCHFlashWorker();   // no parent — will be moved to thread
    m_worker->moveToThread(m_thread);

    // Worker → Dialog
    connect(m_worker, &WCHFlashWorker::devicesFound,
            this, &WCHFlashDialog::onDevicesFound);
    connect(m_worker, &WCHFlashWorker::deviceConnected,
            this, &WCHFlashDialog::onDeviceConnected);
    connect(m_worker, &WCHFlashWorker::deviceDisconnected,
            this, &WCHFlashDialog::onDeviceDisconnected);
    connect(m_worker, &WCHFlashWorker::progress,
            this, &WCHFlashDialog::onProgress);
    connect(m_worker, &WCHFlashWorker::finished,
            this, &WCHFlashDialog::onFinished);
    connect(m_worker, &WCHFlashWorker::logMessage,
            this, &WCHFlashDialog::onLogMessage);

    // Thread cleanup
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);

    m_thread->start();

    setConnectedState(false);
}

// ---------------------------------------------------------------------------
// Destructor
// ---------------------------------------------------------------------------
WCHFlashDialog::~WCHFlashDialog()
{
    // Disconnect device on worker thread before shutting down
    if (m_worker) {
        QMetaObject::invokeMethod(m_worker, "disconnectDevice",
                                  Qt::BlockingQueuedConnection);
    }
    m_thread->quit();
    m_thread->wait(3000);
}

// ---------------------------------------------------------------------------
// setupUi
// ---------------------------------------------------------------------------
void WCHFlashDialog::setupUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(10);
    mainLayout->setContentsMargins(14, 14, 14, 14);

    // ---- Device section ----
    auto* deviceGroup = new QGroupBox(tr("Device"), this);
    auto* deviceLayout = new QVBoxLayout(deviceGroup);

    auto* scanRow = new QHBoxLayout();
    m_scanBtn = new QPushButton(tr("Scan Devices"), deviceGroup);
    m_deviceCombo = new QComboBox(deviceGroup);
    m_deviceCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    scanRow->addWidget(m_scanBtn);
    scanRow->addWidget(m_deviceCombo, 1);

    auto* connectRow = new QHBoxLayout();
    m_connectBtn    = new QPushButton(tr("Connect"),    deviceGroup);
    m_disconnectBtn = new QPushButton(tr("Disconnect"), deviceGroup);
    m_connectBtn->setEnabled(false);
    m_disconnectBtn->setEnabled(false);
    connectRow->addWidget(m_connectBtn);
    connectRow->addWidget(m_disconnectBtn);
    connectRow->addStretch();

    deviceLayout->addLayout(scanRow);
    deviceLayout->addLayout(connectRow);

    // ---- Chip info section ----
    auto* infoGroup = new QGroupBox(tr("Chip Information"), this);
    auto* infoLayout = new QVBoxLayout(infoGroup);
    m_chipInfoLabel = new QLabel(tr("(not connected)"), infoGroup);
    m_chipInfoLabel->setWordWrap(true);
    m_chipInfoLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_chipInfoLabel->setFont(QFont("Monospace", 9));
    infoLayout->addWidget(m_chipInfoLabel);

    // ---- Firmware section ----
    auto* fwGroup = new QGroupBox(tr("Firmware"), this);
    auto* fwLayout = new QHBoxLayout(fwGroup);
    m_firmwareLabel = new QLabel(tr("(no file selected)"), fwGroup);
    m_firmwareLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_firmwareLabel->setWordWrap(false);
    m_firmwareLabel->setMinimumWidth(200);
    m_browseBtn = new QPushButton(tr("Browse..."), fwGroup);
    fwLayout->addWidget(m_firmwareLabel, 1);
    fwLayout->addWidget(m_browseBtn);

    // ---- Flash button ----
    m_flashBtn = new QPushButton(tr("Flash, Verify && Reset"), this);
    m_flashBtn->setEnabled(false);
    m_flashBtn->setMinimumHeight(36);
    QFont flashFont = m_flashBtn->font();
    flashFont.setBold(true);
    m_flashBtn->setFont(flashFont);

    // ---- Progress bar ----
    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setTextVisible(true);

    // ---- Log ----
    auto* logGroup = new QGroupBox(tr("Log"), this);
    auto* logLayout = new QVBoxLayout(logGroup);
    m_logEdit = new QTextEdit(logGroup);
    m_logEdit->setReadOnly(true);
    m_logEdit->setFont(QFont("Monospace", 8));
    m_logEdit->setMinimumHeight(160);
    logLayout->addWidget(m_logEdit);

    // ---- Close button ----
    m_closeBtn = new QPushButton(tr("Close"), this);
    auto* closeRow = new QHBoxLayout();
    closeRow->addStretch();
    closeRow->addWidget(m_closeBtn);

    // Assemble main layout
    mainLayout->addWidget(deviceGroup);
    mainLayout->addWidget(infoGroup);
    mainLayout->addWidget(fwGroup);
    mainLayout->addWidget(m_flashBtn);
    mainLayout->addWidget(m_progressBar);
    mainLayout->addWidget(logGroup, 1);
    mainLayout->addLayout(closeRow);

    // Connections
    connect(m_scanBtn,       &QPushButton::clicked, this, &WCHFlashDialog::onScanClicked);
    connect(m_connectBtn,    &QPushButton::clicked, this, &WCHFlashDialog::onConnectClicked);
    connect(m_disconnectBtn, &QPushButton::clicked, this, &WCHFlashDialog::onDisconnectClicked);
    connect(m_browseBtn,     &QPushButton::clicked, this, &WCHFlashDialog::onBrowseClicked);
    connect(m_flashBtn,      &QPushButton::clicked, this, &WCHFlashDialog::onFlashClicked);
    connect(m_closeBtn,      &QPushButton::clicked, this, &QDialog::close);
}

// ---------------------------------------------------------------------------
// State helper
// ---------------------------------------------------------------------------
void WCHFlashDialog::setConnectedState(bool connected)
{
    m_connected = connected;
    m_connectBtn->setEnabled(!connected && m_deviceCombo->count() > 0);
    m_disconnectBtn->setEnabled(connected);
    m_scanBtn->setEnabled(!m_busy);
    updateFlashButton();
}

void WCHFlashDialog::updateFlashButton()
{
    // Keep as a private helper; called whenever state changes
    m_flashBtn->setEnabled(m_connected && !m_firmwarePath.isEmpty() && !m_busy);
}

// ---------------------------------------------------------------------------
// appendLog
// ---------------------------------------------------------------------------
void WCHFlashDialog::appendLog(const QString& text)
{
    QString ts = QDateTime::currentDateTime().toString("hh:mm:ss.zzz");
    m_logEdit->append("[" + ts + "] " + text);
    // Auto-scroll
    QScrollBar* sb = m_logEdit->verticalScrollBar();
    sb->setValue(sb->maximum());
}

// ---------------------------------------------------------------------------
// UI slots
// ---------------------------------------------------------------------------
void WCHFlashDialog::onScanClicked()
{
    m_deviceCombo->clear();
    m_connectBtn->setEnabled(false);
    appendLog(tr("Scanning for WCH ISP devices..."));
    QMetaObject::invokeMethod(m_worker, "scanDevices", Qt::QueuedConnection);
}

void WCHFlashDialog::onConnectClicked()
{
    int idx = m_deviceCombo->currentIndex();
    if (idx < 0) return;
    m_busy = true;
    m_scanBtn->setEnabled(false);
    m_connectBtn->setEnabled(false);
    appendLog(tr("Connecting to device %1...").arg(idx));
    QMetaObject::invokeMethod(m_worker, "connectDevice",
                              Qt::QueuedConnection,
                              Q_ARG(int, idx));
}

void WCHFlashDialog::onDisconnectClicked()
{
    appendLog(tr("Disconnecting..."));
    QMetaObject::invokeMethod(m_worker, "disconnectDevice", Qt::QueuedConnection);
}

void WCHFlashDialog::onBrowseClicked()
{
    QString path = QFileDialog::getOpenFileName(
        this,
        tr("Select WCH Firmware"),
        QString(),
        tr("Firmware Files (*.hex *.bin);;Intel HEX (*.hex);;Binary (*.bin);;All Files (*)")
    );
    if (path.isEmpty()) return;

    m_firmwarePath = path;
    // Show only filename for readability, keep full path internally
    m_firmwareLabel->setText(QFileInfo(path).fileName());
    m_firmwareLabel->setToolTip(path);
    appendLog(tr("Firmware selected: %1").arg(path));
    updateFlashButton();
}

void WCHFlashDialog::onFlashClicked()
{
    if (m_firmwarePath.isEmpty()) {
        QMessageBox::warning(this, tr("No Firmware"), tr("Please select a firmware file first."));
        return;
    }
    if (!m_connected) {
        QMessageBox::warning(this, tr("Not Connected"), tr("Please connect to a WCH device first."));
        return;
    }

    auto reply = QMessageBox::question(
        this,
        tr("Confirm Flash"),
        tr("This will erase and overwrite the firmware on the connected WCH device.\n\n"
           "Firmware: %1\n\nProceed?").arg(QFileInfo(m_firmwarePath).fileName()),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No
    );
    if (reply != QMessageBox::Yes) return;

    m_busy = true;
    m_scanBtn->setEnabled(false);
    m_connectBtn->setEnabled(false);
    m_disconnectBtn->setEnabled(false);
    m_flashBtn->setEnabled(false);
    m_browseBtn->setEnabled(false);
    m_progressBar->setValue(0);

    appendLog(tr("Starting flash operation..."));
    QMetaObject::invokeMethod(m_worker, "flashFirmware",
                              Qt::QueuedConnection,
                              Q_ARG(QString, m_firmwarePath));
}

// ---------------------------------------------------------------------------
// Worker signal handlers
// ---------------------------------------------------------------------------
void WCHFlashDialog::onDevicesFound(const QStringList& devices)
{
    m_deviceCombo->clear();
    m_deviceCombo->addItems(devices);
    m_connectBtn->setEnabled(!devices.isEmpty());
    if (devices.isEmpty())
        appendLog(tr("No WCH ISP devices found. Put device in ISP/bootloader mode."));
    else
        appendLog(tr("%1 device(s) found.").arg(devices.size()));
}

void WCHFlashDialog::onDeviceConnected(const QString& chipInfo)
{
    m_busy = false;
    m_chipInfoLabel->setText(chipInfo);
    setConnectedState(true);
    appendLog(tr("Connected."));
}

void WCHFlashDialog::onDeviceDisconnected()
{
    m_busy = false;
    m_chipInfoLabel->setText(tr("(not connected)"));
    setConnectedState(false);
    appendLog(tr("Disconnected."));
}

void WCHFlashDialog::onProgress(int percent, const QString& /*message*/)
{
    m_progressBar->setValue(percent);
}

static bool isUsbPermissionError(const QString& message)
{
    return message.contains(QLatin1String("LIBUSB_ERROR_ACCESS"), Qt::CaseInsensitive);
}

static QString permissionFixCommands()
{
    return QStringLiteral(
        "sudo tee /etc/udev/rules.d/51-opf-wchflash.rules <<'EOF'\n"
        "SUBSYSTEM==\"usb\", ATTRS{idVendor}==\"1a86\", ATTRS{idProduct}==\"55e0\", TAG+=\"uaccess\", MODE=\"0666\"\n"
        "SUBSYSTEM==\"usb\", ATTRS{idVendor}==\"4348\", ATTRS{idProduct}==\"55e0\", TAG+=\"uaccess\", MODE=\"0666\"\n"
        "EOF\n\n"
        "sudo udevadm control --reload-rules\n"
        "sudo udevadm trigger\n");
}

void WCHFlashDialog::onFinished(bool success, const QString& message)
{
    m_busy = false;
    m_scanBtn->setEnabled(true);
    m_browseBtn->setEnabled(true);
    // Update buttons based on connection state (device resets after flash)
    setConnectedState(m_connected);

    appendLog(success ? tr("SUCCESS: ") + message : tr("ERROR: ") + message);

    if (success) {
        QMessageBox::information(this, tr("Flash Complete"), message);
    } else {
        QString title = tr("Error");
        if (message.startsWith(QLatin1String("Connect failed:"), Qt::CaseInsensitive))
            title = tr("Connection Failed");
        else if (message.startsWith(QLatin1String("Flash error:"), Qt::CaseInsensitive))
            title = tr("Flash Failed");

        if (isUsbPermissionError(message)) {
            QString details = permissionFixCommands();
            QString userMessage = message + "\n\n" +
                tr("Permission denied while opening the USB device. This is usually a Linux udev permission issue.") +
                "\n\n" + tr("Run these commands in a terminal to add the rule and reload udev:") +
                "\n\n";

            QDialog dialog(this);
            dialog.setWindowTitle(title);
            auto* layout = new QVBoxLayout(&dialog);

            auto* label = new QLabel(userMessage, &dialog);
            label->setWordWrap(true);
            layout->addWidget(label);

            auto* commandView = new QPlainTextEdit(details, &dialog);
            commandView->setReadOnly(true);
            commandView->setLineWrapMode(QPlainTextEdit::NoWrap);
            commandView->setMinimumHeight(180);
            commandView->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
            layout->addWidget(commandView);

            auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok, &dialog);
            QPushButton* copyButton = buttons->addButton(tr("Copy commands"), QDialogButtonBox::ActionRole);
            layout->addWidget(buttons);

            connect(copyButton, &QPushButton::clicked, this, [details, this]() {
                QApplication::clipboard()->setText(details);
                appendLog(tr("Permission commands copied to clipboard."));
            });
            connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);

            dialog.exec();
            return;
        }

        QMessageBox::critical(this, title, message);
    }
}

void WCHFlashDialog::onLogMessage(const QString& message)
{
    appendLog(message);
}
