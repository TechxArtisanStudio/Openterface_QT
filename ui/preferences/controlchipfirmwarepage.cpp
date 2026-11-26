/*
* ========================================================================== *
*                                                                            *
*    This file is part of the Openterface Mini KVM App QT version            *
*                                                                            *
*    Copyright (C) 2024   <info@openterface.com>                             *
*                                                                            *
*    This program is free software: you can redistribute it and/or modify    *
*    it under the terms of the GNU General Public License as published by    *
*    the Free Software Foundation version 3.                                 *
*                                                                            *
*    This program is distributed in the hope that it will be useful, but     *
*    WITHOUT ANY WARRANTY; without even the implied warranty of              *
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU        *
*    General Public License for more details.                                *
*                                                                            *
*    You should have received a copy of the GNU General Public License       *
*    along with this program. If not, see <http://www.gnu.org/licenses/>.    *
*                                                                            *
* ========================================================================== *
*/

#include "controlchipfirmwarepage.h"
#include "../advance/wchflash/WCHFlashWorker.h"
#include "../../serial/SerialPortManager.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QLoggingCategory>
#include <QPointer>

Q_DECLARE_LOGGING_CATEGORY(log_core_serial_cmd)
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
#include <QTimer>

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------
ControlChipFirmwarePage::ControlChipFirmwarePage(QWidget* parent)
    : QWidget(parent)
{
    setupUi();

    // Worker lives on a separate thread
    m_thread = new QThread(this);
    m_worker = new WCHFlashWorker();   // no parent — will be moved to thread
    m_worker->moveToThread(m_thread);

    // Worker → Page
    connect(m_worker, &WCHFlashWorker::devicesFound,
            this, &ControlChipFirmwarePage::onDevicesFound);
    connect(m_worker, &WCHFlashWorker::deviceConnected,
            this, &ControlChipFirmwarePage::onDeviceConnected);
    connect(m_worker, &WCHFlashWorker::deviceDisconnected,
            this, &ControlChipFirmwarePage::onDeviceDisconnected);
    connect(m_worker, &WCHFlashWorker::firmwareVersionRead,
            this, &ControlChipFirmwarePage::onFirmwareVersionRead);
    connect(m_worker, &WCHFlashWorker::progress,
            this, &ControlChipFirmwarePage::onProgress);
    connect(m_worker, &WCHFlashWorker::finished,
            this, &ControlChipFirmwarePage::onFinished);
    connect(m_worker, &WCHFlashWorker::logMessage,
            this, &ControlChipFirmwarePage::onLogMessage);

    // Thread cleanup
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);

    m_thread->start();

    setConnectedState(false);

    // Try to read firmware version immediately if serial is already open
    QTimer::singleShot(500, this, [this]() {
        tryAutoReadFirmwareVersion();
    });
}

// ---------------------------------------------------------------------------
// Destructor
// ---------------------------------------------------------------------------
ControlChipFirmwarePage::~ControlChipFirmwarePage()
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
void ControlChipFirmwarePage::setupUi()
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

    // Style disabled buttons to be more visible
    QString disabledBtnStyle = R"(
        QPushButton:disabled {
            background-color: #f0f0f0;
            color: #666666;
            border: 1px solid #cccccc;
            border-radius: 4px;
            padding: 5px 15px;
        }
        QPushButton {
            background-color: #ffffff;
            color: #333333;
            border: 1px solid #bbbbbb;
            border-radius: 4px;
            padding: 5px 15px;
        }
        QPushButton:hover {
            background-color: #e8e8e8;
            border: 1px solid #999999;
        }
        QPushButton:pressed {
            background-color: #d0d0d0;
        }
    )";
    m_connectBtn->setStyleSheet(disabledBtnStyle);
    m_disconnectBtn->setStyleSheet(disabledBtnStyle);

    // Add tooltip to guide users
    m_connectBtn->setToolTip(tr("Click 'Scan Devices' first to find available WCH devices"));
    m_disconnectBtn->setToolTip(tr("Connect to a device first before disconnecting"));

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

    // ---- Firmware version section ----
    auto* versionGroup = new QGroupBox(tr("Firmware Version"), this);
    auto* versionLayout = new QVBoxLayout(versionGroup);

    // Current firmware version row
    auto* currentVersionRow = new QHBoxLayout();
    currentVersionRow->addWidget(new QLabel(tr("Current:"), versionGroup));
    m_firmwareVersionLabel = new QLabel(tr("(not read)"), versionGroup);
    m_firmwareVersionLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_firmwareVersionLabel->setWordWrap(false);
    m_readVersionBtn = new QPushButton(tr("Read"), versionGroup);
    m_readVersionBtn->setEnabled(false);
    currentVersionRow->addWidget(m_firmwareVersionLabel, 1);
    currentVersionRow->addWidget(m_readVersionBtn);
    versionLayout->addLayout(currentVersionRow);

    // Latest firmware version row (from file)
    auto* latestVersionRow = new QHBoxLayout();
    latestVersionRow->addWidget(new QLabel(tr("Selected file:"), versionGroup));
    m_latestFirmwareLabel = new QLabel(tr("(no file selected)"), versionGroup);
    m_latestFirmwareLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_latestFirmwareLabel->setWordWrap(false);
    latestVersionRow->addWidget(m_latestFirmwareLabel, 1);
    versionLayout->addLayout(latestVersionRow);

    // ---- Firmware section ----
    auto* fwGroup = new QGroupBox(tr("Firmware File"), this);
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

    // Style Flash button for better visibility when disabled
    QString flashBtnStyle = R"(
        QPushButton:disabled {
            background-color: #f0f0f0;
            color: #666666;
            border: 2px solid #cccccc;
            border-radius: 4px;
            padding: 8px 20px;
            font-weight: bold;
        }
        QPushButton {
            background-color: #4CAF50;
            color: white;
            border: 2px solid #45a049;
            border-radius: 4px;
            padding: 8px 20px;
            font-weight: bold;
        }
        QPushButton:hover {
            background-color: #45a049;
        }
        QPushButton:pressed {
            background-color: #3d8b40;
        }
    )";
    m_flashBtn->setStyleSheet(flashBtnStyle);
    m_flashBtn->setToolTip(tr("Connect to a device and select firmware file first"));

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

    // Assemble main layout (no close button — embedded widget)
    mainLayout->addWidget(deviceGroup);
    mainLayout->addWidget(infoGroup);
    mainLayout->addWidget(versionGroup);
    mainLayout->addWidget(fwGroup);
    mainLayout->addWidget(m_flashBtn);
    mainLayout->addWidget(m_progressBar);
    mainLayout->addWidget(logGroup, 1);

    // Connections
    connect(m_scanBtn,       &QPushButton::clicked, this, &ControlChipFirmwarePage::onScanClicked);
    connect(m_connectBtn,    &QPushButton::clicked, this, &ControlChipFirmwarePage::onConnectClicked);
    connect(m_disconnectBtn, &QPushButton::clicked, this, &ControlChipFirmwarePage::onDisconnectClicked);
    connect(m_readVersionBtn, &QPushButton::clicked, this, &ControlChipFirmwarePage::onReadVersionClicked);
    connect(m_browseBtn,     &QPushButton::clicked, this, &ControlChipFirmwarePage::onBrowseClicked);
    connect(m_flashBtn,      &QPushButton::clicked, this, &ControlChipFirmwarePage::onFlashClicked);
}

// ---------------------------------------------------------------------------
// State helper
// ---------------------------------------------------------------------------
void ControlChipFirmwarePage::setConnectedState(bool connected)
{
    m_connected = connected;
    m_connectBtn->setEnabled(!connected && m_deviceCombo->count() > 0);
    m_disconnectBtn->setEnabled(connected);
    m_readVersionBtn->setEnabled(connected);
    m_scanBtn->setEnabled(!m_busy);
    updateFlashButton();
}

void ControlChipFirmwarePage::updateFlashButton()
{
    // Keep as a private helper; called whenever state changes
    bool canFlash = m_connected && !m_firmwarePath.isEmpty() && !m_busy;
    m_flashBtn->setEnabled(canFlash);

    // Update button style based on connection state
    if (canFlash) {
        // Orange theme when connected and ready to flash
        QString flashBtnStyle = R"(
            QPushButton:disabled {
                background-color: #f0f0f0;
                color: #666666;
                border: 2px solid #cccccc;
                border-radius: 4px;
                padding: 8px 20px;
                font-weight: bold;
            }
            QPushButton {
                background-color: #FF9800;
                color: white;
                border: 2px solid #F57C00;
                border-radius: 4px;
                padding: 8px 20px;
                font-weight: bold;
            }
            QPushButton:hover {
                background-color: #F57C00;
            }
            QPushButton:pressed {
                background-color: #E65100;
            }
        )";
        m_flashBtn->setStyleSheet(flashBtnStyle);
        m_flashBtn->setToolTip(tr("Click to flash firmware to the connected device"));
    } else {
        // Default disabled style
        QString flashBtnStyle = R"(
            QPushButton:disabled {
                background-color: #f0f0f0;
                color: #666666;
                border: 2px solid #cccccc;
                border-radius: 4px;
                padding: 8px 20px;
                font-weight: bold;
            }
            QPushButton {
                background-color: #f0f0f0;
                color: #666666;
                border: 2px solid #cccccc;
                border-radius: 4px;
                padding: 8px 20px;
                font-weight: bold;
            }
            QPushButton:hover {
                background-color: #e8e8e8;
            }
            QPushButton:pressed {
                background-color: #d0d0d0;
            }
        )";
        m_flashBtn->setStyleSheet(flashBtnStyle);
        m_flashBtn->setToolTip(tr("Connect to a device and select firmware file first"));
    }
}

// ---------------------------------------------------------------------------
// appendLog
// ---------------------------------------------------------------------------
void ControlChipFirmwarePage::appendLog(const QString& text)
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
void ControlChipFirmwarePage::onScanClicked()
{
    m_deviceCombo->clear();
    m_connectBtn->setEnabled(false);
    appendLog(tr("Scanning for WCH ISP devices..."));
    QMetaObject::invokeMethod(m_worker, "scanDevices", Qt::QueuedConnection);
}

void ControlChipFirmwarePage::onConnectClicked()
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

void ControlChipFirmwarePage::onDisconnectClicked()
{
    appendLog(tr("Disconnecting..."));
    QMetaObject::invokeMethod(m_worker, "disconnectDevice", Qt::QueuedConnection);
}

void ControlChipFirmwarePage::onReadVersionClicked()
{
    m_readVersionBtn->setEnabled(false);
    m_firmwareVersionLabel->setText(tr("Reading..."));
    appendLog(tr("Reading firmware version via serial..."));

    try {
        auto& serialManager = SerialPortManager::getInstance();
        if (serialManager.isPortOpen()) {
            QString version = readFirmwareVersionViaSerial(serialManager);
            if (!version.isEmpty()) {
                onFirmwareVersionRead(version);
            } else {
                m_firmwareVersionLabel->setText(tr("(no response)"));
                appendLog(tr("No response from device"));
                m_readVersionBtn->setEnabled(true);
            }
        } else {
            m_firmwareVersionLabel->setText(tr("(serial not open)"));
            appendLog(tr("Serial port not open - connect target device first"));
            m_readVersionBtn->setEnabled(true);
        }
    } catch (const std::exception& e) {
        m_firmwareVersionLabel->setText(tr("(error)"));
        appendLog(QString("Error reading version: %1").arg(e.what()));
        m_readVersionBtn->setEnabled(true);
    }
}

void ControlChipFirmwarePage::onBrowseClicked()
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

    // Parse firmware file to extract version information
    parseFirmwareVersion(path);
    updateFlashButton();
}

void ControlChipFirmwarePage::onFlashClicked()
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
void ControlChipFirmwarePage::onDevicesFound(const QStringList& devices)
{
    m_deviceCombo->clear();
    m_deviceCombo->addItems(devices);
    m_connectBtn->setEnabled(!devices.isEmpty());
    if (devices.isEmpty())
        appendLog(tr("No WCH ISP devices found. Put device in ISP/bootloader mode."));
    else
        appendLog(tr("%1 device(s) found.").arg(devices.size()));
}

void ControlChipFirmwarePage::onDeviceConnected(const QString& chipInfo)
{
    m_busy = false;
    m_chipInfoLabel->setText(chipInfo);
    setConnectedState(true);
    appendLog(tr("Connected."));

    // Automatically read firmware version via serial port (CH9329 protocol 0x01 command)
    m_firmwareVersionLabel->setText(tr("Reading..."));
    appendLog(tr("Reading firmware version via serial..."));

    try {
        auto& serialManager = SerialPortManager::getInstance();
        if (serialManager.isPortOpen()) {
            QString version = readFirmwareVersionViaSerial(serialManager);
            if (!version.isEmpty()) {
                onFirmwareVersionRead(version);
            } else {
                m_firmwareVersionLabel->setText(tr("(no response)"));
                appendLog(tr("No response from device"));
            }
        } else {
            m_firmwareVersionLabel->setText(tr("(serial not open)"));
            appendLog(tr("Serial port not open - connect target device first"));
        }
    } catch (const std::exception& e) {
        m_firmwareVersionLabel->setText(tr("(error)"));
        appendLog(QString("Error reading version: %1").arg(e.what()));
    }
}

QString ControlChipFirmwarePage::readFirmwareVersionViaSerial(SerialPortManager& manager)
{
    // Send CMD_GET_INFO command (0x01) to get firmware version
    // Command format: 57 AB 00 01 00 <checksum>
    QByteArray cmd = QByteArray::fromHex("57AB000100");

    // Calculate checksum
    uint8_t sum = 0;
    for (int i = 0; i < cmd.size(); i++) {
        sum += static_cast<uint8_t>(cmd[i]);
    }
    cmd.append(static_cast<char>(sum));

    appendLog(QString("Sending GET_INFO command: %1").arg(cmd.toHex(' ').toUpper()));

    // Send synchronous command and wait for response
    QByteArray response = manager.sendSyncCommand(cmd, true);

    if (response.isEmpty()) {
        appendLog(tr("Response is empty"));
        return QString();
    }

    appendLog(QString("Response size: %1 bytes").arg(response.size()));
    appendLog(QString("Response hex: %1").arg(response.toHex(' ').toUpper()));

    // Debug: print each byte
    QString byteDebug;
    for (int i = 0; i < qMin(response.size(), 20); i++) {
        byteDebug += QString("[%1]=0x%2 ").arg(i).arg(static_cast<uint8_t>(response[i]), 2, 16, QChar('0'));
    }
    appendLog(QString("Response bytes: %1").arg(byteDebug));

    // Parse response: 57 AB 00 81 XX <payload> <checksum>
    // Response command is 0x81 (0x01 | 0x80)
    // Payload layout (8 bytes):
    //   [0] Hardware selector
    //   [1] Target USB enumeration state
    //   [2] Keyboard LED bits
    //   [3] Firmware version number ← This is what we want
    //   [4-7] Reserved
    // So firmware version is at response[5+3] = response[8]
    if (response.size() >= 13) {
        appendLog(QString("Checking header: [0]=0x%1 [1]=0x%2 [3]=0x%3")
            .arg(static_cast<uint8_t>(response[0]), 2, 16, QChar('0'))
            .arg(static_cast<uint8_t>(response[1]), 2, 16, QChar('0'))
            .arg(static_cast<uint8_t>(response[3]), 2, 16, QChar('0')));

        if (static_cast<uint8_t>(response[0]) == 0x57 &&
            static_cast<uint8_t>(response[1]) == 0xAB &&
            static_cast<uint8_t>(response[3]) == 0x81) {

            uint8_t version = static_cast<uint8_t>(response[8]);
            QString versionStr = QString("v%1").arg(version);
            appendLog(QString("Parsed firmware version from byte[8]: %1").arg(versionStr));
            return versionStr;
        } else {
            appendLog(tr("Header mismatch - not a valid GetInfo response"));
        }
    } else {
        appendLog(QString("Response too short: %1 bytes (need at least 13)").arg(response.size()));
    }

    return QString();
}

void ControlChipFirmwarePage::onDeviceDisconnected()
{
    m_busy = false;
    m_chipInfoLabel->setText(tr("(not connected)"));
    m_firmwareVersionLabel->setText(tr("(not read)"));
    setConnectedState(false);
    appendLog(tr("Disconnected."));
}

void ControlChipFirmwarePage::onFirmwareVersionRead(const QString& version)
{
    appendLog(QString("DEBUG: onFirmwareVersionRead called with: '%1'").arg(version));
    m_firmwareVersionLabel->setText(version);
    m_readVersionBtn->setEnabled(m_connected);
    appendLog(tr("Firmware version read: %1").arg(version));
}

void ControlChipFirmwarePage::onSerialFirmwareVersionReceived(const QString& version)
{
    m_firmwareVersionLabel->setText(version);
    m_readVersionBtn->setEnabled(m_connected);
    appendLog(tr("Firmware version from serial: %1").arg(version));
}

void ControlChipFirmwarePage::tryAutoReadFirmwareVersion()
{
    // 使用异步方式读取，避免阻塞 UI 线程
    readFirmwareVersionAsync();
}

void ControlChipFirmwarePage::onAutoReadFinished(const QString& version)
{
    if (version.isEmpty()) {
        appendLog(tr("Auto-read failed: no valid response"));
        // 如果自动读取失败，尝试在稍后重试
        QTimer::singleShot(2000, this, [this]() {
            if (m_firmwareVersionLabel->text() == tr("(not read)")) {
                appendLog(tr("Retrying auto-read..."));
                readFirmwareVersionAsync();
            }
        });
    } else if (version.startsWith("error:")) {
        appendLog(tr("Auto-read error: %1").arg(version.mid(7)));
    } else {
        m_firmwareVersionLabel->setText(version);
        appendLog(tr("Firmware version auto-read: %1").arg(version));
    }
}

void ControlChipFirmwarePage::readFirmwareVersionAsync()
{
    // 在后台线程中读取，避免阻塞 UI
    QThread* thread = new QThread();
    QObject* worker = new QObject();

    QPointer<ControlChipFirmwarePage> guard(this);
    connect(thread, &QThread::started, worker, [guard, worker, thread]() {
        try {
            auto& serialManager = SerialPortManager::getInstance();
            QString version;
            if (serialManager.isPortOpen()) {
                // 直接调用静态方法或复制逻辑
                QByteArray cmd = QByteArray::fromHex("57AB000100");
                uint8_t sum = 0;
                for (int i = 0; i < cmd.size(); i++) {
                    sum += static_cast<uint8_t>(cmd[i]);
                }
                cmd.append(static_cast<char>(sum));

                QByteArray response = serialManager.sendSyncCommand(cmd, true);

                if (!response.isEmpty() && response.size() >= 13 &&
                    static_cast<uint8_t>(response[0]) == 0x57 &&
                    static_cast<uint8_t>(response[1]) == 0xAB &&
                    static_cast<uint8_t>(response[3]) == 0x81) {

                    uint8_t fwVersion = static_cast<uint8_t>(response[8]);
                    version = QString("v%1").arg(fwVersion);
                }
            }

            if (guard) {
                QMetaObject::invokeMethod(guard.data(), "onAutoReadFinished",
                                          Qt::QueuedConnection,
                                          Q_ARG(QString, version));
            }
        } catch (const std::exception& e) {
            if (guard) {
                QMetaObject::invokeMethod(guard.data(), "onAutoReadFinished",
                                          Qt::QueuedConnection,
                                          Q_ARG(QString, QString("error: ") + e.what()));
            }
        }
        worker->deleteLater();
        thread->quit();
    });
    connect(thread, &QThread::finished, thread, &QThread::deleteLater);
    worker->moveToThread(thread);
    thread->start();
}

void ControlChipFirmwarePage::parseFirmwareVersion(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        m_latestFirmwareLabel->setText(tr("(cannot open file)"));
        return;
    }

    QByteArray data = file.readAll();
    file.close();

    if (data.isEmpty()) {
        m_latestFirmwareLabel->setText(tr("(empty file)"));
        return;
    }

    // Check if it's Intel HEX format (starts with ':')
    if (data.startsWith(':')) {
        // Parse Intel HEX to find version at specific address
        // For now, just show file size info
        QFileInfo fi(filePath);
        m_latestFirmwareLabel->setText(tr("HEX file, %1 bytes").arg(fi.size()));
        appendLog(tr("Intel HEX firmware file selected, size: %1 bytes").arg(fi.size()));
    } else {
        // Binary format - try to read version from bytes 12-15
        if (data.size() >= 16) {
            uint8_t v0 = static_cast<uint8_t>(data[12]);
            uint8_t v1 = static_cast<uint8_t>(data[13]);
            uint8_t v2 = static_cast<uint8_t>(data[14]);
            uint8_t v3 = static_cast<uint8_t>(data[15]);
            QString version = QString("%1%2%3%4")
                .arg(v0, 2, 10, QChar('0'))
                .arg(v1, 2, 10, QChar('0'))
                .arg(v2, 2, 10, QChar('0'))
                .arg(v3, 2, 10, QChar('0'));
            m_latestFirmwareLabel->setText(version);
            appendLog(tr("Firmware version from file: %1").arg(version));
        } else {
            QFileInfo fi(filePath);
            m_latestFirmwareLabel->setText(tr("Binary file, %1 bytes").arg(fi.size()));
            appendLog(tr("Binary firmware file selected, size: %1 bytes").arg(fi.size()));
        }
    }
}

void ControlChipFirmwarePage::onProgress(int percent, const QString& /*message*/)
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

void ControlChipFirmwarePage::onFinished(bool success, const QString& message)
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

void ControlChipFirmwarePage::onLogMessage(const QString& message)
{
    appendLog(message);
}
