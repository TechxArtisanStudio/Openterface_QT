# Control Chip Firmware Integration — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rename "WCH Flash" to "Control Chip Firmware", integrate it as a page inside AdvancedSettingsDialog, and rename the existing "Firmware" page to "Video Firmware".

**Architecture:** Convert `WCHFlashDialog` (QDialog) into `ControlChipFirmwarePage` (QWidget) following the existing page pattern (FirmwarePage, McpPage). Embed it in the AdvancedSettingsDialog sidebar between "Video Firmware" and "MCP". Remove the standalone menu item and dialog.

**Tech Stack:** Qt 6.5, C++, CMake/qmake

## Global Constraints

- Preserve all existing WCHFlashDialog functionality (scan, connect, disconnect, browse, flash, progress, log, USB permission error dialog)
- WCHFlashWorker remains unchanged — reused by the new page
- Sidebar order: Video Firmware → Control Chip Firmware → MCP
- OK/Apply/Cancel buttons only visible on MCP page

---

### Task 1: Create ControlChipFirmwarePage

**Files:**
- Create: `ui/preferences/controlchipfirmwarepage.h`
- Create: `ui/preferences/controlchipfirmwarepage.cpp`

**Interfaces:**
- Consumes: `WCHFlashWorker` from `ui/advance/wchflash/WCHFlashWorker.h`
- Produces: `ControlChipFirmwarePage` class — a QWidget with all WCH flash functionality

- [ ] **Step 1: Create the header file**

Create `ui/preferences/controlchipfirmwarepage.h`:

```cpp
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

#ifndef CONTROLCHIPFIRMWAREPAGE_H
#define CONTROLCHIPFIRMWAREPAGE_H

#include <QWidget>
#include <QStringList>

class QPushButton;
class QComboBox;
class QLabel;
class QProgressBar;
class QTextEdit;
class QThread;
class WCHFlashWorker;

// ---------------------------------------------------------------------------
// ControlChipFirmwarePage
//
// Page for flashing firmware to the keyboard/mouse control chip (WCH MCU)
// via USB ISP bootloader. Embedded in AdvancedSettingsDialog.
//
// Layout (top-to-bottom):
//   [Scan Devices] [Device combo] [Connect] [Disconnect]
//   --- Chip Info ---
//   [Firmware: path label] [Browse]
//   [Flash, Verify && Reset]
//   [Progress bar]
//   [Log text area]
// ---------------------------------------------------------------------------
class ControlChipFirmwarePage : public QWidget {
    Q_OBJECT

public:
    explicit ControlChipFirmwarePage(QWidget* parent = nullptr);
    ~ControlChipFirmwarePage() override;

private slots:
    // Worker signal handlers
    void onDevicesFound(const QStringList& devices);
    void onDeviceConnected(const QString& chipInfo);
    void onDeviceDisconnected();
    void onProgress(int percent, const QString& message);
    void onFinished(bool success, const QString& message);
    void onLogMessage(const QString& message);

    // UI button handlers
    void onScanClicked();
    void onConnectClicked();
    void onDisconnectClicked();
    void onBrowseClicked();
    void onFlashClicked();

private:
    void setupUi();
    void setConnectedState(bool connected);
    void updateFlashButton();
    void appendLog(const QString& text);

    // UI elements
    QPushButton* m_scanBtn       = nullptr;
    QComboBox*   m_deviceCombo   = nullptr;
    QPushButton* m_connectBtn    = nullptr;
    QPushButton* m_disconnectBtn = nullptr;
    QLabel*      m_chipInfoLabel = nullptr;
    QLabel*      m_firmwareLabel = nullptr;
    QPushButton* m_browseBtn     = nullptr;
    QPushButton* m_flashBtn      = nullptr;
    QProgressBar* m_progressBar  = nullptr;
    QTextEdit*   m_logEdit       = nullptr;

    // Worker thread
    QThread*         m_thread = nullptr;
    WCHFlashWorker*  m_worker = nullptr;

    QString m_firmwarePath;
    bool    m_connected = false;
    bool    m_busy      = false;
};

#endif // CONTROLCHIPFIRMWAREPAGE_H
```

- [ ] **Step 2: Create the implementation file**

Create `ui/preferences/controlchipfirmwarepage.cpp`. Copy the content from `ui/advance/wchflash/WCHFlashDialog.cpp` with these changes:
- Replace all `WCHFlashDialog` with `ControlChipFirmwarePage`
- Remove `setWindowTitle()` and `setMinimumSize()` calls
- Remove `m_closeBtn` and the close button row
- Remove the `connect(m_closeBtn, ...)` line
- Add the GPL header from `firmwarepage.cpp`

```cpp
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

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
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
#include <QDialog>
#include <QDialogButtonBox>

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

    // Assemble main layout (no close button — embedded in dialog)
    mainLayout->addWidget(deviceGroup);
    mainLayout->addWidget(infoGroup);
    mainLayout->addWidget(fwGroup);
    mainLayout->addWidget(m_flashBtn);
    mainLayout->addWidget(m_progressBar);
    mainLayout->addWidget(logGroup, 1);

    // Connections
    connect(m_scanBtn,       &QPushButton::clicked, this, &ControlChipFirmwarePage::onScanClicked);
    connect(m_connectBtn,    &QPushButton::clicked, this, &ControlChipFirmwarePage::onConnectClicked);
    connect(m_disconnectBtn, &QPushButton::clicked, this, &ControlChipFirmwarePage::onDisconnectClicked);
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
    m_scanBtn->setEnabled(!m_busy);
    updateFlashButton();
}

void ControlChipFirmwarePage::updateFlashButton()
{
    m_flashBtn->setEnabled(m_connected && !m_firmwarePath.isEmpty() && !m_busy);
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

void ControlChipFirmwarePage::onBrowseClicked()
{
    QString path = QFileDialog::getOpenFileName(
        this,
        tr("Select Control Chip Firmware"),
        QString(),
        tr("Firmware Files (*.hex *.bin);;Intel HEX (*.hex);;Binary (*.bin);;All Files (*)")
    );
    if (path.isEmpty()) return;

    m_firmwarePath = path;
    m_firmwareLabel->setText(QFileInfo(path).fileName());
    m_firmwareLabel->setToolTip(path);
    appendLog(tr("Firmware selected: %1").arg(path));
    updateFlashButton();
}

void ControlChipFirmwarePage::onFlashClicked()
{
    if (m_firmwarePath.isEmpty()) {
        QMessageBox::warning(this, tr("No Firmware"), tr("Please select a firmware file first."));
        return;
    }
    if (!m_connected) {
        QMessageBox::warning(this, tr("Not Connected"), tr("Please connect to a device first."));
        return;
    }

    auto reply = QMessageBox::question(
        this,
        tr("Confirm Flash"),
        tr("This will erase and overwrite the firmware on the connected device.\n\n"
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
}

void ControlChipFirmwarePage::onDeviceDisconnected()
{
    m_busy = false;
    m_chipInfoLabel->setText(tr("(not connected)"));
    setConnectedState(false);
    appendLog(tr("Disconnected."));
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
```

- [ ] **Step 3: Commit**

```bash
git add ui/preferences/controlchipfirmwarepage.h ui/preferences/controlchipfirmwarepage.cpp
git commit -m "feat: add ControlChipFirmwarePage widget"
```

---

### Task 2: Integrate ControlChipFirmwarePage into AdvancedSettingsDialog

**Files:**
- Modify: `ui/preferences/advancedsettingsdialog.h:33-67`
- Modify: `ui/preferences/advancedsettingsdialog.cpp:23-207`

**Interfaces:**
- Consumes: `ControlChipFirmwarePage` from Task 1
- Produces: Updated AdvancedSettingsDialog with three sidebar pages

- [ ] **Step 1: Update the header file**

In `ui/preferences/advancedsettingsdialog.h`:

1. Add `#include "controlchipfirmwarepage.h"` after line 34 (after `#include "firmwarepage.h"`)
2. Add `ControlChipFirmwarePage *controlChipFirmwarePage;` member after line 50 (after `FirmwarePage *firmwarePage;`)

- [ ] **Step 2: Update the implementation file**

In `ui/preferences/advancedsettingsdialog.cpp`:

1. Add include at top (after line 25):
```cpp
#include "controlchipfirmwarepage.h"
```

2. In constructor (line 44), add initialization after `firmwarePage(new FirmwarePage(this))`:
```cpp
, controlChipFirmwarePage(new ControlChipFirmwarePage(this))
```

3. In `createSettingTree()` (line 96), change the names list:
```cpp
// Before:
QStringList names = {tr("Firmware"), tr("MCP")};
// After:
QStringList names = {tr("Video Firmware"), tr("Control Chip Firmware"), tr("MCP")};
```

4. In `createPages()` (line 103-115), add the new page between firmware and mcp:
```cpp
void AdvancedSettingsDialog::createPages() {
    auto addScrollablePage = [this](QWidget *page) {
        QScrollArea *scrollArea = new QScrollArea(this);
        scrollArea->setWidget(page);
        scrollArea->setWidgetResizable(true);
        scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        stackedWidget->addWidget(scrollArea);
    };

    addScrollablePage(firmwarePage);
    addScrollablePage(controlChipFirmwarePage);
    addScrollablePage(mcpPage);
}
```

5. In `changePage()` (line 151-181), update the page index logic:
```cpp
void AdvancedSettingsDialog::changePage(QTreeWidgetItem *current, QTreeWidgetItem *previous) {
    if (m_changingPage) {
        return;
    }

    if (!current) {
        current = previous;
        if (!current) return;
    }

    QString itemText = current->text(0);
    int newPageIndex = -1;

    if (itemText == tr("Video Firmware")) {
        newPageIndex = 0;
    } else if (itemText == tr("Control Chip Firmware")) {
        newPageIndex = 1;
    } else if (itemText == tr("MCP")) {
        newPageIndex = 2;
    }

    if (newPageIndex != -1 && newPageIndex != m_currentPageIndex) {
        m_changingPage = true;

        stackedWidget->setCurrentIndex(newPageIndex);
        m_currentPageIndex = newPageIndex;

        // Show OK/Apply/Cancel buttons only on MCP page
        buttonWidget->setVisible(newPageIndex == 2);

        m_pageChangeTimer->start(200);
    }
}
```

6. In `applyAccordingPage()` (line 183-194), update the switch:
```cpp
void AdvancedSettingsDialog::applyAccordingPage() {
    int currentPageIndex = stackedWidget->currentIndex();
    switch (currentPageIndex) {
    case 0: // Video Firmware - no apply action needed
        break;
    case 1: // Control Chip Firmware - no apply action needed
        break;
    case 2:
        mcpPage->applyMcpSettings();
        break;
    default:
        break;
    }
}
```

- [ ] **Step 3: Commit**

```bash
git add ui/preferences/advancedsettingsdialog.h ui/preferences/advancedsettingsdialog.cpp
git commit -m "feat: integrate ControlChipFirmwarePage into AdvancedSettingsDialog"
```

---

### Task 3: Remove WCHFlashDialog from MainWindow

**Files:**
- Modify: `ui/mainwindow.h:46,230,323`
- Modify: `ui/mainwindow.cpp:1992-2004`
- Modify: `ui/mainwindow.ui:137,323-330,635-640,678`

- [ ] **Step 1: Update mainwindow.h**

1. Remove line 46: `#include "ui/advance/wchflash/WCHFlashDialog.h"`
2. Remove line 230: `void showWCHFlashDialog();`
3. Remove line 323: `WCHFlashDialog *wchFlashDialog = nullptr;`

- [ ] **Step 2: Update mainwindow.cpp**

Remove the `showWCHFlashDialog()` method (lines 1992-2004):
```cpp
// Remove this entire method:
void MainWindow::showWCHFlashDialog() {
    if (!wchFlashDialog) {
        wchFlashDialog = new WCHFlashDialog(this);
        connect(wchFlashDialog, &QDialog::finished, this, [this]() {
            wchFlashDialog->deleteLater();
            wchFlashDialog = nullptr;
        });
        wchFlashDialog->show();
    } else {
        wchFlashDialog->raise();
        wchFlashDialog->activateWindow();
    }
}
```

- [ ] **Step 3: Update mainwindow.ui**

1. Remove line 137: `<addaction name="actionWCHFlash"/>`
2. Remove lines 323-330 (the action definition):
```xml
  <action name="actionWCHFlash">
   <property name="text">
    <string>WCH Flash</string>
   </property>
   <property name="toolTip">
    <string>Flash WCH microcontroller firmware via USB ISP bootloader</string>
   </property>
   <property name="shortcut">
    ... (any shortcut defined)
   </property>
  </action>
```
3. Remove lines 635-640 (the connection):
```xml
  <connection>
   <sender>actionWCHFlash</sender>
   <signal>triggered()</signal>
   <receiver>MainWindow</receiver>
   <slot>showWCHFlashDialog()</slot>
  </connection>
```
4. Remove line 678: `<slot>showWCHFlashDialog()</slot>`

- [ ] **Step 4: Commit**

```bash
git add ui/mainwindow.h ui/mainwindow.cpp ui/mainwindow.ui
git commit -m "refactor: remove WCHFlashDialog from MainWindow"
```

---

### Task 4: Update Build System Files

**Files:**
- Modify: `cmake/SourceFiles.cmake:241,310`
- Modify: `openterfaceQT.pro:119,281`

- [ ] **Step 1: Update cmake/SourceFiles.cmake**

1. In line 241, replace `WCHFlashDialog` with `controlchipfirmwarepage`:
```cmake
# Remove this line:
    ui/advance/wchflash/WCHFlashDialog.cpp ui/advance/wchflash/WCHFlashDialog.h
```

2. In line 310, add the new file after firmwarepage:
```cmake
    ui/preferences/firmwarepage.cpp ui/preferences/firmwarepage.h
    ui/preferences/controlchipfirmwarepage.cpp ui/preferences/controlchipfirmwarepage.h
```

- [ ] **Step 2: Update openterfaceQT.pro**

1. In line 119, remove:
```
    ui/advance/wchflash/WCHFlashDialog.cpp \
```

2. In line 281, remove:
```
    ui/advance/wchflash/WCHFlashDialog.h \
```

3. Add new files to SOURCES (after line 137 `mcppage.cpp`):
```
    ui/preferences/controlchipfirmwarepage.cpp \
```

4. Add new files to HEADERS (after the preferences headers section):
```
    ui/preferences/controlchipfirmwarepage.h \
```

- [ ] **Step 3: Commit**

```bash
git add cmake/SourceFiles.cmake openterfaceQT.pro
git commit -m "build: update build files for ControlChipFirmwarePage"
```

---

### Task 5: Delete Old WCHFlashDialog Files

**Files:**
- Delete: `ui/advance/wchflash/WCHFlashDialog.h`
- Delete: `ui/advance/wchflash/WCHFlashDialog.cpp`

- [ ] **Step 1: Delete the files**

```bash
git rm ui/advance/wchflash/WCHFlashDialog.h ui/advance/wchflash/WCHFlashDialog.cpp
```

- [ ] **Step 2: Commit**

```bash
git commit -m "refactor: remove old WCHFlashDialog files"
```

---

### Task 6: Build Verification

- [ ] **Step 1: Build with CMake**

```bash
cd build
cmake ..
cmake --build . --config Debug
```

Expected: Build succeeds with no errors.

- [ ] **Step 2: Verify the application launches**

Run the application and verify:
1. Advanced menu no longer shows "WCH Flash"
2. Advanced > Settings opens dialog with three pages: Video Firmware, Control Chip Firmware, MCP
3. Control Chip Firmware page shows all controls (scan, connect, disconnect, browse, flash, progress, log)
4. OK/Apply/Cancel buttons are hidden on Video Firmware and Control Chip Firmware pages
5. OK/Apply/Cancel buttons are visible on MCP page

- [ ] **Step 3: Final commit (if any fixes needed)**

```bash
git add -A
git commit -m "fix: address build issues from ControlChipFirmwarePage integration"
```
