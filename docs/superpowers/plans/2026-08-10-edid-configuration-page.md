# EDID Configuration Page — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Integrate the existing "Update Display Settings" standalone dialog into the Advanced Settings dialog as a new "EDID Configuration" page, scoped down to Display Name only.

**Architecture:** Create a new `EdidConfigPage` (QWidget) in `ui/preferences/` that ports the display-name firmware read/write flow from `UpdateDisplaySettingsDialog`. Register it as the 4th page in `AdvancedSettingsDialog`. Remove the old menu action `actionRenameDisplay` and its wiring in `MainWindow`.

**Tech Stack:** C++17, Qt 6.5, CMake, existing `FirmwareOperationManager` / `EDIDUtils` / `VideoHid` infrastructure.

## Global Constraints

- All new files follow the project's GPL v3 license header (lines 1-21 of any `.h`/`.cpp` in `ui/preferences/`).
- New page lives in `ui/preferences/` alongside existing pages (McpPage, FirmwarePage, etc.).
- No changes to `EDIDUtils`, `FirmwareOperationManager`, `VideoHid`, or `EdidProcessor` — only reuse them.
- The old `UpdateDisplaySettingsDialog` files are left in place (still compile, just unreachable from UI).
- No automated test framework exists; verification is build-success + manual UI checklist.
- EDID display name max length: 13 ASCII characters (EDID descriptor limit).

---

### Task 1: Create EdidConfigPage header file

**Files:**
- Create: `ui/preferences/edidconfigpage.h`

**Interfaces:**
- Produces: `EdidConfigPage : public QWidget` — all public/private members as defined below.
- Consumes: Forward declarations for `VideoHid`, `FirmwareOperationManager`.

- [ ] **Step 1: Create the header file**

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

#ifndef EDIDCONFIGPAGE_H
#define EDIDCONFIGPAGE_H

#include <QWidget>
#include <QCheckBox>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QProgressBar>
#include <QByteArray>
#include <QString>

class VideoHid;
class FirmwareOperationManager;

class EdidConfigPage : public QWidget
{
    Q_OBJECT

public:
    explicit EdidConfigPage(QWidget *parent = nullptr);
    ~EdidConfigPage();

private slots:
    void onDisplayNameCheckChanged(bool checked);
    void onApplyButtonClicked();
    void onCancelReadingClicked();

    // Firmware operation signal handlers
    void onFirmwareReadProgress(int percent);
    void onFirmwareReadFinished(bool success);
    void onFirmwareReadError(const QString &errorMessage);
    void onFirmwareWriteFinished(bool success);

private:
    // ---- UI widgets ----
    QGroupBox *displayNameGroup;
    QLabel *currentNameLabel;
    QCheckBox *displayNameCheckBox;
    QLineEdit *displayNameLineEdit;
    QPushButton *applyButton;

    QGroupBox *progressGroup;
    QProgressBar *progressBar;
    QLabel *progressLabel;
    QPushButton *cancelReadingButton;

    QLabel *infoLabel;

    // ---- Firmware operation state ----
    FirmwareOperationManager *firmwareOperationManager;
    QString m_tempFirmwarePath;
    QByteArray m_pendingFirmwareData;
    bool m_operationFinished;
    bool m_updateMode;

    // ---- UI construction ----
    void setupUI();
    void buildDisplayNameSection();
    void buildProgressSection();
    void buildButtonSection();
    void connectUiSignals();

    // ---- Firmware/EDID logic (ported from UpdateDisplaySettingsDialog) ----
    void ensureFirmwareOperationManager();
    void loadCurrentEDIDSettings();
    bool processFirmwareData(const QByteArray &firmwareData);
    bool parseEdidBlock(const QByteArray &firmwareData, int &edidOffset, QByteArray &edidBlock) const;
    bool updateDisplayName(const QString &newName);
    bool processAndWriteFirmware();

    // ---- State helpers ----
    void enableApplyButton();
    void setControlsEnabled(bool enabled);
    void setProgressState(bool active, const QString &labelText);
    bool validateAsciiInput(const QString &text, int maxLen, const QString &fieldName, QString &errorMessage) const;
    void shutdownFirmwareOperation();
    void restartPollingDelayed(const QString &reason);
    void showErrorAndRestart(const QString &title, const QString &message, const QString &reason);
    void stopAllDevices();
};

#endif // EDIDCONFIGPAGE_H
```

- [ ] **Step 2: Commit**

```bash
git add ui/preferences/edidconfigpage.h
git commit -m "feat: add EdidConfigPage header with display name EDID config interface"
```

---

### Task 2: Create EdidConfigPage implementation — UI construction

**Files:**
- Create: `ui/preferences/edidconfigpage.cpp`

**Interfaces:**
- Consumes: The header from Task 1.
- Produces: Full `.cpp` with constructor, destructor, UI building, signal connections, state helpers, and the full firmware read/write/EDID flow.

- [ ] **Step 1: Create the implementation file**

Write the complete file below to `ui/preferences/edidconfigpage.cpp`. This includes the constructor (which calls `setupUI()` then `loadCurrentEDIDSettings()`), all UI builders, all signal connections, all state helpers, and the full firmware read/write flow ported from `UpdateDisplaySettingsDialog` (stripped of serial number and resolution logic).

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

#include "edidconfigpage.h"
#include "../advance/edid/edidutils.h"
#include "../../video/videohid.h"
#include "../../video/firmwareoperationmanager.h"
#include "../../video/ms2109.h"
#include "../../serial/SerialPortManager.h"
#include <QMessageBox>
#include <QPointer>
#include <QThread>
#include <QTimer>
#include <QApplication>
#include <QDebug>
#include <QStandardPaths>

EdidConfigPage::EdidConfigPage(QWidget *parent)
    : QWidget(parent)
    , displayNameGroup(nullptr)
    , currentNameLabel(nullptr)
    , displayNameCheckBox(nullptr)
    , displayNameLineEdit(nullptr)
    , applyButton(nullptr)
    , progressGroup(nullptr)
    , progressBar(nullptr)
    , progressLabel(nullptr)
    , cancelReadingButton(nullptr)
    , infoLabel(nullptr)
    , firmwareOperationManager(nullptr)
    , m_tempFirmwarePath()
    , m_pendingFirmwareData()
    , m_operationFinished(false)
    , m_updateMode(false)
{
    setupUI();
    loadCurrentEDIDSettings();
}

EdidConfigPage::~EdidConfigPage()
{
    if (firmwareOperationManager) {
        firmwareOperationManager->cancel();
        firmwareOperationManager->deleteLater();
        firmwareOperationManager = nullptr;
    }
}

// ---------------------------------------------------------------------------
// UI construction
// ---------------------------------------------------------------------------

void EdidConfigPage::setupUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    buildDisplayNameSection();
    buildProgressSection();
    buildButtonSection();

    setLayout(mainLayout);

    connectUiSignals();
    enableApplyButton();
}

void EdidConfigPage::buildDisplayNameSection()
{
    displayNameGroup = new QGroupBox(tr("Display Name"), this);
    QVBoxLayout *layout = new QVBoxLayout(displayNameGroup);

    currentNameLabel = new QLabel(tr("Current name: "), this);
    currentNameLabel->setWordWrap(true);
    layout->addWidget(currentNameLabel);

    displayNameCheckBox = new QCheckBox(tr("Update display name"), this);
    displayNameCheckBox->setChecked(false);
    layout->addWidget(displayNameCheckBox);

    displayNameLineEdit = new QLineEdit(this);
    displayNameLineEdit->setPlaceholderText(tr("Loading current display name..."));
    displayNameLineEdit->setEnabled(false);
    displayNameLineEdit->setMaxLength(13);
    layout->addWidget(displayNameLineEdit);

    static_cast<QVBoxLayout*>(this->layout())->addWidget(displayNameGroup);
}

void EdidConfigPage::buildProgressSection()
{
    progressGroup = new QGroupBox(tr("Reading Firmware"), this);
    QVBoxLayout *progressLayout = new QVBoxLayout(progressGroup);

    progressLabel = new QLabel(tr("Reading firmware data..."), this);
    progressLayout->addWidget(progressLabel);

    progressBar = new QProgressBar(this);
    progressBar->setRange(0, 100);
    progressBar->setValue(0);
    progressLayout->addWidget(progressBar);

    QHBoxLayout *progressButtonLayout = new QHBoxLayout();
    progressButtonLayout->addStretch();
    cancelReadingButton = new QPushButton(tr("Cancel Reading"), this);
    progressButtonLayout->addWidget(cancelReadingButton);
    progressLayout->addLayout(progressButtonLayout);

    progressGroup->setVisible(false);
    static_cast<QVBoxLayout*>(this->layout())->addWidget(progressGroup);
}

void EdidConfigPage::buildButtonSection()
{
    QVBoxLayout *mainLayout = static_cast<QVBoxLayout*>(this->layout());

    QHBoxLayout *buttonLayout = new QHBoxLayout();
    QSpacerItem *horizontalSpacer = new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);
    buttonLayout->addItem(horizontalSpacer);

    applyButton = new QPushButton(tr("Apply"), this);
    applyButton->setDefault(true);
    buttonLayout->addWidget(applyButton);

    mainLayout->addLayout(buttonLayout);

    infoLabel = new QLabel(
        tr("Note: After updating, the application will exit.\n"
           "Please disconnect and reconnect the device to apply changes."),
        this);
    infoLabel->setWordWrap(true);
    infoLabel->setStyleSheet("QLabel { color: #666; margin-top: 8px; }");
    mainLayout->addWidget(infoLabel);

    mainLayout->addStretch();
}

void EdidConfigPage::connectUiSignals()
{
    connect(applyButton, &QPushButton::clicked, this, &EdidConfigPage::onApplyButtonClicked);
    connect(displayNameCheckBox, &QCheckBox::toggled, this, &EdidConfigPage::onDisplayNameCheckChanged);
    connect(cancelReadingButton, &QPushButton::clicked, this, &EdidConfigPage::onCancelReadingClicked);
    connect(displayNameLineEdit, &QLineEdit::textChanged, this, &EdidConfigPage::enableApplyButton);
}

// ---------------------------------------------------------------------------
// Slot: checkbox toggled
// ---------------------------------------------------------------------------

void EdidConfigPage::onDisplayNameCheckChanged(bool checked)
{
    displayNameLineEdit->setEnabled(checked);
    enableApplyButton();
}

// ---------------------------------------------------------------------------
// State helpers
// ---------------------------------------------------------------------------

void EdidConfigPage::enableApplyButton()
{
    bool hasChanges = displayNameCheckBox->isChecked()
                      && !displayNameLineEdit->text().trimmed().isEmpty();
    applyButton->setEnabled(hasChanges);
}

void EdidConfigPage::setControlsEnabled(bool enabled)
{
    displayNameGroup->setEnabled(enabled);
    applyButton->setEnabled(enabled);
}

void EdidConfigPage::setProgressState(bool active, const QString &labelText)
{
    if (progressGroup) {
        progressGroup->setVisible(active);
    }
    if (progressLabel) {
        progressLabel->setText(labelText);
    }
    if (progressBar) {
        if (!active) {
            progressBar->setValue(0);
        }
        progressBar->setVisible(active);
    }
    if (cancelReadingButton) {
        cancelReadingButton->setEnabled(active);
    }
}

bool EdidConfigPage::validateAsciiInput(const QString &text, int maxLen,
                                         const QString &fieldName, QString &errorMessage) const
{
    if (text.isEmpty()) {
        errorMessage = tr("%1 cannot be empty when enabled.").arg(fieldName);
        return false;
    }
    if (text.length() > maxLen) {
        errorMessage = tr("%1 cannot exceed %2 characters.").arg(fieldName).arg(maxLen);
        return false;
    }
    for (const QChar &ch : text) {
        if (ch.unicode() > 127) {
            errorMessage = tr("%1 must contain only ASCII characters.").arg(fieldName);
            return false;
        }
    }
    return true;
}

void EdidConfigPage::shutdownFirmwareOperation()
{
    m_operationFinished = true;
    if (firmwareOperationManager) {
        firmwareOperationManager->cancel();
    }
    setProgressState(false, tr(""));
}

void EdidConfigPage::restartPollingDelayed(const QString &reason)
{
    QTimer::singleShot(500, this, [this, reason]() {
        VideoHid::getInstance().start();
        qDebug() << "Polling restarted after" << reason;
    });
}

void EdidConfigPage::showErrorAndRestart(const QString &title, const QString &message, const QString &reason)
{
    setProgressState(false, tr(""));
    QMessageBox::critical(this, title, message);
    restartPollingDelayed(reason);
}

void EdidConfigPage::stopAllDevices()
{
    qDebug() << "Stopping all devices...";
    VideoHid::getInstance().stop();
    SerialPortManager::getInstance().stop();
    qDebug() << "All accessible devices stopped.";
}

// ---------------------------------------------------------------------------
// Firmware operation manager
// ---------------------------------------------------------------------------

void EdidConfigPage::ensureFirmwareOperationManager()
{
    if (firmwareOperationManager) {
        return;
    }

    firmwareOperationManager = new FirmwareOperationManager(&VideoHid::getInstance(), ADDR_EEPROM, this);
    connect(firmwareOperationManager, &FirmwareOperationManager::progress,
            this, &EdidConfigPage::onFirmwareReadProgress);
    connect(firmwareOperationManager, &FirmwareOperationManager::readFinished,
            this, [this](bool success, const QByteArray &firmwareData, const QString &errorMsg) {
        if (!errorMsg.isEmpty()) {
            onFirmwareReadError(errorMsg);
            return;
        }
        if (success) {
            m_pendingFirmwareData = firmwareData;
        }
        onFirmwareReadFinished(success);
    });

    connect(firmwareOperationManager, &FirmwareOperationManager::readCompleted,
            this, [this](bool success, const QByteArray &firmwareData, const QString &errorMsg) {
        if (!success) {
            onFirmwareReadError(errorMsg);
            return;
        }
        if (m_updateMode) {
            if (!processAndWriteFirmware()) {
                showErrorAndRestart(tr("Processing Error"),
                                    tr("Failed to process EDID settings."),
                                    tr("EDID processing error"));
                return;
            }
            return;
        }
        // Normal settings load path — populate UI
        processFirmwareReadResult(success);
        QPointer<EdidConfigPage> selfPtr(this);
        QTimer::singleShot(500, this, [this, selfPtr]() {
            if (!selfPtr) return;
            VideoHid::getInstance().start();
        });
    });

    connect(firmwareOperationManager, &FirmwareOperationManager::writeFinished,
            this, [this](bool success, const QString &errorMsg) {
        if (success) {
            m_operationFinished = true;
            setProgressState(false, tr(""));
            QMessageBox::information(this, tr("Success"),
                tr("Display settings updated successfully!\n\n"
                   "The application will now exit.\n"
                   "Please disconnect and reconnect the entire device to apply the changes."));
            QApplication::quit();
        } else {
            showErrorAndRestart(tr("Write Error"),
                errorMsg.isEmpty() ? tr("Failed to write firmware to device.") : errorMsg,
                tr("firmware write failure"));
        }
    });
}

// ---------------------------------------------------------------------------
// Firmware read — initial load
// ---------------------------------------------------------------------------

void EdidConfigPage::loadCurrentEDIDSettings()
{
    qDebug() << "Loading current EDID settings from firmware...";

    VideoHid &videoHid = VideoHid::getInstance();
    videoHid.stopPollingOnly();
    QThread::msleep(100);

    quint32 firmwareSize = videoHid.readFirmwareSize();
    if (firmwareSize == 0) {
        qWarning() << "Failed to read firmware size, cannot load current EDID settings";
        displayNameLineEdit->setPlaceholderText(tr("Failed to read firmware — enter display name"));
        videoHid.start();
        return;
    }

    qDebug() << "Firmware size:" << firmwareSize << "bytes";
    setProgressState(true, tr("Reading firmware data..."));
    setControlsEnabled(false);

    m_tempFirmwarePath = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                         + "/temp_firmware_read.bin";
    m_updateMode = false;
    ensureFirmwareOperationManager();
    firmwareOperationManager->readFirmware(firmwareSize, m_tempFirmwarePath);
}

void EdidConfigPage::onFirmwareReadProgress(int percent)
{
    if (progressBar) {
        progressBar->setValue(percent);
    }
}

void EdidConfigPage::onFirmwareReadFinished(bool success)
{
    setProgressState(false, tr(""));

    if (m_updateMode) {
        if (!success) {
            showErrorAndRestart(tr("Read Error"),
                                tr("Failed to read firmware from device."),
                                tr("firmware read failure"));
            return;
        }

        QByteArray firmwareData = m_pendingFirmwareData;
        QString newName = displayNameLineEdit->text().trimmed();

        // Apply display name via EDIDUtils directly (no EdidProcessor needed — no resolution changes)
        int edidOffset;
        QByteArray edidBlock;
        if (!parseEdidBlock(firmwareData, edidOffset, edidBlock)) {
            showErrorAndRestart(tr("Processing Error"),
                                tr("Failed to parse EDID block."),
                                tr("EDID parsing error"));
            return;
        }
        edid::EDIDUtils::updateEDIDDisplayName(edidBlock, newName);
        quint8 edidChecksum = edid::EDIDUtils::calculateEDIDChecksum(edidBlock);
        edidBlock[127] = edidChecksum;
        firmwareData.replace(edidOffset, 128, edidBlock);

        // Firmware-level checksum
        quint16 firmwareChecksum = edid::FirmwareUtils::calculateFirmwareChecksumWithDiff(
            m_pendingFirmwareData, firmwareData);
        if (firmwareData.size() >= 2) {
            firmwareData[firmwareData.size() - 2] = static_cast<char>((firmwareChecksum >> 8) & 0xFF);
            firmwareData[firmwareData.size() - 1] = static_cast<char>(firmwareChecksum & 0xFF);
        }

        m_pendingFirmwareData = firmwareData;
        setProgressState(true, tr("Waiting for firmware thread to finish before write..."));
        return;
    }

    // Normal settings load path
    processFirmwareReadResult(success);

    QPointer<EdidConfigPage> selfPtr(this);
    QTimer::singleShot(500, this, [this, selfPtr]() {
        if (!selfPtr) return;
        VideoHid::getInstance().start();
    });
}

void EdidConfigPage::onFirmwareReadError(const QString &errorMessage)
{
    qWarning() << "Firmware read error:" << errorMessage;

    setControlsEnabled(true);
    applyButton->setEnabled(true);
    displayNameLineEdit->setPlaceholderText(tr("Error reading firmware — enter display name"));
    enableApplyButton();
    shutdownFirmwareOperation();
    showErrorAndRestart(tr("Firmware Read Error"),
                        tr("Failed to read firmware: %1").arg(errorMessage),
                        tr("firmware read error"));
}

void EdidConfigPage::onFirmwareWriteFinished(bool success)
{
    // Write-finished is handled in the lambda inside ensureFirmwareOperationManager.
    // This slot exists for completeness; the actual success/failure logic lives in the
    // writeFinished lambda to keep the flow self-contained.
    Q_UNUSED(success);
}

// ---------------------------------------------------------------------------
// Firmware data processing (populate UI from read firmware)
// ---------------------------------------------------------------------------

void EdidConfigPage::processFirmwareReadResult(bool success)
{
    setControlsEnabled(true);

    if (!success) {
        qWarning() << "Failed to read firmware data, cannot load current EDID settings";
        displayNameLineEdit->setPlaceholderText(tr("Failed to read firmware — enter display name"));
        enableApplyButton();
        return;
    }

    if (!processFirmwareData(m_pendingFirmwareData)) {
        displayNameLineEdit->setPlaceholderText(tr("Failed to parse firmware — enter display name"));
    }

    enableApplyButton();
}

bool EdidConfigPage::processFirmwareData(const QByteArray &firmwareData)
{
    if (firmwareData.isEmpty()) {
        qWarning() << "Empty firmware data in memory";
        return false;
    }

    int edidOffset;
    QByteArray edidBlock;
    if (!parseEdidBlock(firmwareData, edidOffset, edidBlock)) {
        qWarning() << "EDID block parsing failed";
        return false;
    }

    QString currentDisplayName;
    QString currentSerialNumber;
    edid::EDIDUtils::parseEDIDDescriptors(edidBlock, currentDisplayName, currentSerialNumber);

    if (!currentDisplayName.isEmpty()) {
        currentNameLabel->setText(tr("Current name: %1").arg(currentDisplayName));
        displayNameLineEdit->setPlaceholderText(tr("Enter new display name (max 13 characters)"));
    } else {
        currentNameLabel->setText(tr("Current name: (none)"));
        displayNameLineEdit->clear();
        displayNameLineEdit->setPlaceholderText(tr("No display name found — enter new name"));
    }

    edid::EDIDUtils::logSupportedResolutions(edidBlock);
    qDebug() << "=== CURRENT EDID DESCRIPTORS ===";
    edid::EDIDUtils::showEDIDDescriptors(edidBlock);

    return true;
}

bool EdidConfigPage::parseEdidBlock(const QByteArray &firmwareData, int &edidOffset, QByteArray &edidBlock) const
{
    edidOffset = edid::EDIDUtils::findEDIDBlock0(firmwareData);
    if (edidOffset == -1 || edidOffset + 128 > firmwareData.size()) {
        return false;
    }
    edidBlock = firmwareData.mid(edidOffset, 128);
    return true;
}

// ---------------------------------------------------------------------------
// Apply — user clicked Apply
// ---------------------------------------------------------------------------

void EdidConfigPage::onApplyButtonClicked()
{
    if (!displayNameCheckBox->isChecked()) {
        QMessageBox::warning(this, tr("No Updates Selected"),
                             tr("Please select at least one setting to update."));
        return;
    }

    QString newName = displayNameLineEdit->text().trimmed();
    QString err;
    if (!validateAsciiInput(newName, 13, tr("Display name"), err)) {
        QMessageBox::warning(this, tr("Invalid Input"), err);
        return;
    }

    QString summaryText = tr("The following change will be applied:\n\n"
                             "Display Name: %1\n\nDo you want to continue?").arg(newName);
    int reply = QMessageBox::question(this, tr("Confirm Updates"), summaryText,
                                      QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (reply != QMessageBox::Yes) {
        return;
    }

    if (!updateDisplayName(newName)) {
        QMessageBox::critical(this, tr("Update Failed"), tr("Failed to start the update process."));
    }
}

bool EdidConfigPage::updateDisplayName(const QString &newName)
{
    qDebug() << "Starting display name update...";
    qDebug() << "  Display name:" << newName;

    VideoHid &videoHid = VideoHid::getInstance();
    videoHid.stopPollingOnly();
    qDebug() << "Polling stopped before firmware operation";

    setProgressState(true, tr("Updating display name..."));

    quint32 firmwareSize = VideoHid::getInstance().readFirmwareSize();
    if (firmwareSize == 0) {
        showErrorAndRestart(tr("Firmware Error"),
                            tr("Failed to read firmware size."),
                            tr("firmware size read error"));
        return false;
    }

    m_tempFirmwarePath = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                         + "/temp_firmware_update.bin";
    m_updateMode = true;
    ensureFirmwareOperationManager();
    firmwareOperationManager->readFirmware(firmwareSize, m_tempFirmwarePath);
    return true;
}

bool EdidConfigPage::processAndWriteFirmware()
{
    if (m_pendingFirmwareData.isEmpty()) {
        qWarning() << "No pending firmware data to write";
        return false;
    }

    applyButton->setEnabled(false);
    setProgressState(true, tr("Writing modified firmware..."));
    firmwareOperationManager->writeFirmware(m_pendingFirmwareData, m_tempFirmwarePath);
    return true;
}

// ---------------------------------------------------------------------------
// Cancel
// ---------------------------------------------------------------------------

void EdidConfigPage::onCancelReadingClicked()
{
    if (m_operationFinished) {
        qDebug() << "Cancel ignored because operation is already finished";
        return;
    }

    qDebug() << "User cancelled firmware reading";
    shutdownFirmwareOperation();
    setControlsEnabled(true);
    displayNameLineEdit->setPlaceholderText(tr("Reading cancelled — enter display name"));
    enableApplyButton();
    restartPollingDelayed(tr("user cancellation"));
}
```

- [ ] **Step 2: Build the project to verify the new files compile**

Run: `cmake --build build` (or the project's standard build command).
Expected: Build fails with "unknown source file" — the new `.cpp` is not yet registered in CMake. This is expected; Task 3 fixes it.

- [ ] **Step 3: Commit**

```bash
git add ui/preferences/edidconfigpage.cpp
git commit -m "feat: add EdidConfigPage implementation with display name EDID update flow"
```

---

### Task 3: Register EdidConfigPage in CMake

**Files:**
- Modify: `cmake/SourceFiles.cmake:304-316` (the `UI_PREFERENCES_SOURCES` section)

**Interfaces:**
- Consumes: The two new files from Tasks 1-2.
- Produces: The files are compiled into the `openterfaceQT` target.

- [ ] **Step 1: Add EdidConfigPage to UI_PREFERENCES_SOURCES**

In `cmake/SourceFiles.cmake`, find the `UI_PREFERENCES_SOURCES` block (around line 304). Add one line at the end of the list:

```cmake
# Before (last line of the block):
    ui/preferences/targetcontrolpage.cpp ui/preferences/targetcontrolpage.h
)

# After:
    ui/preferences/targetcontrolpage.cpp ui/preferences/targetcontrolpage.h
    ui/preferences/edidconfigpage.cpp ui/preferences/edidconfigpage.h
)
```

- [ ] **Step 2: Build the project**

Run: `cmake --build build`
Expected: Build succeeds with no errors. The new `edidconfigpage.cpp` compiles and links.

- [ ] **Step 3: Commit**

```bash
git add cmake/SourceFiles.cmake
git commit -m "build: register edidconfigpage in CMake source list"
```

---

### Task 4: Integrate EdidConfigPage into AdvancedSettingsDialog

**Files:**
- Modify: `ui/preferences/advancedsettingsdialog.h:33-54`
- Modify: `ui/preferences/advancedsettingsdialog.cpp:23-26, 45-47, 98, 115-117, 164-186`

**Interfaces:**
- Consumes: `EdidConfigPage` from Tasks 1-3.
- Produces: A 4th page "EDID Configuration" in the Advanced Settings dialog tree.

- [ ] **Step 1: Add the include and member to the header**

In `ui/preferences/advancedsettingsdialog.h`, add the include after the existing includes (line 36 area):

```cpp
// Add after line 35 (#include "controlchipfirmwarepage.h"):
#include "edidconfigpage.h"
```

Add the member variable after `mcpPage` (around line 53):

```cpp
// In the private section, after:
    McpPage *mcpPage;
// Add:
    EdidConfigPage *edidConfigPage;
```

- [ ] **Step 2: Initialize the page in the constructor**

In `ui/preferences/advancedsettingsdialog.cpp`, add the initialization after `mcpPage(new McpPage(this))` (around line 47):

```cpp
// Before:
    , mcpPage(new McpPage(this))
    , buttonWidget(new QWidget(this))

// After:
    , mcpPage(new McpPage(this))
    , edidConfigPage(new EdidConfigPage(this))
    , buttonWidget(new QWidget(this))
```

- [ ] **Step 3: Add "EDID Configuration" to the tree**

In `advancedsettingsdialog.cpp`, update `createSettingTree()` (around line 98):

```cpp
// Before:
    QStringList names = {tr("Video Firmware"), tr("Control Chip Firmware"), tr("MCP")};

// After:
    QStringList names = {tr("Video Firmware"), tr("Control Chip Firmware"), tr("MCP"), tr("EDID Configuration")};
```

- [ ] **Step 4: Register the page in createPages()**

In `advancedsettingsdialog.cpp`, add after `addScrollablePage(mcpPage);` (around line 117):

```cpp
// Before:
    addScrollablePage(mcpPage);
}

// After:
    addScrollablePage(mcpPage);
    addScrollablePage(edidConfigPage);
}
```

- [ ] **Step 5: Handle page navigation**

In `advancedsettingsdialog.cpp`, update `changePage()` (around line 164-186). Add the new branch for EDID Configuration and update the button visibility condition:

```cpp
// Before (the if/else chain):
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
```

```cpp
// After:
    if (itemText == tr("Video Firmware")) {
        newPageIndex = 0;
    } else if (itemText == tr("Control Chip Firmware")) {
        newPageIndex = 1;
    } else if (itemText == tr("MCP")) {
        newPageIndex = 2;
    } else if (itemText == tr("EDID Configuration")) {
        newPageIndex = 3;
    }

    if (newPageIndex != -1 && newPageIndex != m_currentPageIndex) {
        m_changingPage = true;

        stackedWidget->setCurrentIndex(newPageIndex);
        m_currentPageIndex = newPageIndex;

        // Show OK/Apply/Cancel buttons only on MCP page; EDID page has its own Apply
        buttonWidget->setVisible(newPageIndex == 2);
```

- [ ] **Step 6: Build the project**

Run: `cmake --build build`
Expected: Build succeeds. The Advanced Settings dialog now has a 4th page.

- [ ] **Step 7: Commit**

```bash
git add ui/preferences/advancedsettingsdialog.h ui/preferences/advancedsettingsdialog.cpp
git commit -m "feat: add EDID Configuration page to Advanced Settings dialog"
```

---

### Task 5: Remove actionRenameDisplay from MainWindow

**Files:**
- Modify: `ui/mainwindow.ui` (remove action definition, menu entry, and signal/slot connection)
- Modify: `ui/mainwindow.h` (remove slot declaration and member pointer, remove include)
- Modify: `ui/mainwindow.cpp` (remove slot implementation and include)

**Interfaces:**
- Consumes: The new EdidConfigPage (Task 4) as the replacement entry point.
- Produces: Clean removal of the old "Update Display Settings" menu item and standalone dialog wiring.

- [ ] **Step 1: Remove the menu entry from mainwindow.ui**

In `ui/mainwindow.ui`, remove line 136 (`<addaction name="actionRenameDisplay"/>`) from the File menu.

```xml
<!-- Before: -->
   <addaction name="actionDeviceSelector"/>
   <addaction name="actionRenameDisplay"/>
   <addaction name="actionKeyboardMapEditor"/>

<!-- After: -->
   <addaction name="actionDeviceSelector"/>
   <addaction name="actionKeyboardMapEditor"/>
```

- [ ] **Step 2: Remove the action definition from mainwindow.ui**

Remove the entire `<action name="actionRenameDisplay">` block (lines 311-321):

```xml
<!-- DELETE these 11 lines: -->
  <action name="actionRenameDisplay">
   <property name="text">
    <string>Update Display Settings</string>
   </property>
   <property name="toolTip">
    <string>Update display name and serial number in EDID</string>
   </property>
   <property name="shortcut">
    <string>Ctrl+Shift+U</string>
   </property>
  </action>
```

- [ ] **Step 3: Remove the signal/slot connection from mainwindow.ui**

Remove the `<connection>` block that wires `actionRenameDisplay` (lines 617-622):

```xml
<!-- DELETE these 6 lines: -->
  <connection>
   <sender>actionRenameDisplay</sender>
   <signal>triggered()</signal>
   <receiver>MainWindow</receiver>
   <slot>showUpdateDisplaySettingsDialog()</slot>
  </connection>
```

- [ ] **Step 4: Remove the slot declaration from the `<slots>` section in mainwindow.ui**

Remove line 659 from the `<slots>` block:

```xml
<!-- DELETE this line: -->
  <slot>showUpdateDisplaySettingsDialog()</slot>
```

- [ ] **Step 5: Remove the slot declaration from mainwindow.h**

In `ui/mainwindow.h`, remove these items:

1. Remove the include (line 44):
```cpp
// DELETE:
#include "ui/advance/updatedisplaysettingsdialog.h"
```

2. Remove the slot declaration (line 229):
```cpp
// DELETE:
    void showUpdateDisplaySettingsDialog();
```

3. Remove the member pointer (line 324):
```cpp
// DELETE:
    UpdateDisplaySettingsDialog *updateDisplaySettingsDialog = nullptr;
```

- [ ] **Step 6: Remove the slot implementation from mainwindow.cpp**

In `ui/mainwindow.cpp`, remove the include (line 48):
```cpp
// DELETE:
#include "ui/advance/updatedisplaysettingsdialog.h"
```

Remove the entire method `MainWindow::showUpdateDisplaySettingsDialog()` (lines 2008-2025):
```cpp
// DELETE these 18 lines:
void MainWindow::showUpdateDisplaySettingsDialog() {
    qCDebug(log_ui_mainwindow) << "Opening update display settings dialog";
    if (!updateDisplaySettingsDialog) {
        qCDebug(log_ui_mainwindow) << "Creating update display settings dialog";
        updateDisplaySettingsDialog = new UpdateDisplaySettingsDialog(this);
        
        // Connect the finished signal to clean up
        connect(updateDisplaySettingsDialog, &QDialog::finished, this, [this]() {
            updateDisplaySettingsDialog->deleteLater();
            updateDisplaySettingsDialog = nullptr;
        });
        
        updateDisplaySettingsDialog->show();
    } else {
        updateDisplaySettingsDialog->raise();
        updateDisplaySettingsDialog->activateWindow();
    }
}
```

- [ ] **Step 7: Build the project**

Run: `cmake --build build`
Expected: Build succeeds. The "Update Display Settings" menu item is gone from the File menu.

- [ ] **Step 8: Commit**

```bash
git add ui/mainwindow.ui ui/mainwindow.h ui/mainwindow.cpp
git commit -m "refactor: remove actionRenameDisplay menu entry and showUpdateDisplaySettingsDialog slot"
```

---

### Task 6: Manual verification

**Files:** None (verification only)

**Interfaces:** N/A

- [ ] **Step 1: Build the project**

Run: `cmake --build build`
Expected: Build succeeds with zero errors.

- [ ] **Step 2: Launch the application and verify the integration**

Run the built application and perform this checklist:

1. Open the **File** menu — confirm there is **no** "Update Display Settings" item.
2. Open **File → Settings → Advanced Settings** (or the menu entry for `actionAdvancedSettings`).
3. In the left tree, confirm a new page **"EDID Configuration"** appears as the 4th item (after MCP).
4. Click "EDID Configuration" — confirm:
   - The "Display Name" group box is visible.
   - A progress bar appears briefly while firmware is being read.
   - After reading completes, "Current name:" shows the device's current EDID display name (or a placeholder if read failed).
   - The checkbox "Update display name" is unchecked.
   - The "Apply" button is disabled.
   - The info label at the bottom reads: "Note: After updating, the application will exit. Please disconnect and reconnect the device to apply changes."
5. Check the checkbox — confirm the line edit becomes enabled and Apply becomes enabled (if text is entered).
6. Enter an invalid name (e.g., 14+ characters) — click Apply — confirm a warning dialog appears.
7. Enter a valid name — click Apply — confirm a confirmation dialog appears showing the change summary.
8. Click Yes — confirm the progress bar shows reading, then writing.
9. On success, confirm the success message and app exit.
10. On failure, confirm an error message appears and the page returns to editable state.
11. Verify other Advanced Settings pages (Video Firmware, Control Chip Firmware, MCP) still work normally.

- [ ] **Step 3: Commit any fixes if issues are found**

If issues are found during manual verification, fix them and commit:

```bash
git add -A
git commit -m "fix: resolve issues found during EDID Configuration page verification"
```
