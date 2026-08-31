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
#include "../advance/edid/firmwareutils.h"
#include "../../video/videohid.h"
#include "../../video/firmwareoperationmanager.h"
#include "../../video/ms2109.h"
#include "../../serial/SerialPortManager.h"
#include <QMessageBox>
#include <QPointer>
#include <QShowEvent>
#include <QThread>
#include <QTimer>
#include <QApplication>
#include <QDebug>
#include <QStandardPaths>

EdidConfigPage::EdidConfigPage(QWidget *parent)
    : QWidget(parent)
    , deviceStatusGroup(nullptr)
    , deviceStatusIconLabel(nullptr)
    , deviceStatusLabel(nullptr)
    , readButton(nullptr)
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
    // Defer the blocking USB read so the dialog renders immediately.
    // The polling thread will also emit hidDeviceConnected/Disconnected
    // signals to keep the status up to date.
    QTimer::singleShot(0, this, &EdidConfigPage::updateDeviceStatus);
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

    buildDeviceStatusSection();
    buildDisplayNameSection();
    buildProgressSection();
    buildButtonSection();

    setLayout(mainLayout);

    connectUiSignals();

    // Display name section starts disabled until firmware is read manually
    displayNameGroup->setEnabled(false);
    applyButton->setEnabled(false);
}

void EdidConfigPage::buildDeviceStatusSection()
{
    deviceStatusGroup = new QGroupBox(tr("Device"), this);
    QHBoxLayout *layout = new QHBoxLayout(deviceStatusGroup);

    deviceStatusIconLabel = new QLabel(this);
    deviceStatusIconLabel->setFixedSize(16, 16);
    layout->addWidget(deviceStatusIconLabel);

    QLabel *statusTextLabel = new QLabel(tr("Device status:"), this);
    layout->addWidget(statusTextLabel);

    deviceStatusLabel = new QLabel(tr("Checking..."), this);
    deviceStatusLabel->setStyleSheet("QLabel { font-weight: bold; }");
    layout->addWidget(deviceStatusLabel);

    layout->addStretch();

    readButton = new QPushButton(tr("Read from Device"), this);
    readButton->setEnabled(false);
    layout->addWidget(readButton);

    static_cast<QVBoxLayout*>(this->layout())->addWidget(deviceStatusGroup);
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
    displayNameLineEdit->setPlaceholderText(tr("Click \"Read from Device\" to load current name"));
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
    connect(readButton, &QPushButton::clicked, this, &EdidConfigPage::onReadButtonClicked);

    // Monitor device connection status
    VideoHid &videoHid = VideoHid::getInstance();
    connect(&videoHid, &VideoHid::hidDeviceConnected, this, [this](const QString &) {
        updateDeviceStatus();
    });
    connect(&videoHid, &VideoHid::hidDeviceDisconnected, this, [this](const QString &) {
        updateDeviceStatus();
    });
    connect(&videoHid, &VideoHid::hidDeviceChanged, this, [this](const QString &, const QString &) {
        updateDeviceStatus();
    });
    // The polling thread continuously tracks HDMI input state; follow its view
    // instead of issuing extra USB reads from the GUI thread.
    connect(&videoHid, &VideoHid::hdmiInputStatusChanged, this, [this](bool) {
        updateDeviceStatus();
    });
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
// Slot: Read from Device button clicked
// ---------------------------------------------------------------------------

void EdidConfigPage::onReadButtonClicked()
{
    VideoHid &videoHid = VideoHid::getInstance();

    // Fast check first — avoid blocking USB read if the device is absent
    if (!videoHid.isHidDevicePresent()) {
        QMessageBox::warning(this, tr("Device Not Connected"),
                             tr("Please connect the device before reading EDID data."));
        updateDeviceStatus();
        return;
    }

    // Only proceed if HDMI input is present; trust the polling cache first
    // and fall back to a direct read only if it reports disconnected.
    if (!videoHid.lastKnownHdmiConnected() && !videoHid.isHdmiConnected()) {
        QMessageBox::warning(this, tr("Device Not Connected"),
                             tr("Please connect the device before reading EDID data."));
        updateDeviceStatus();
        return;
    }

    readButton->setEnabled(false);
    loadCurrentEDIDSettings();
}

// ---------------------------------------------------------------------------
// Device connection status
// ---------------------------------------------------------------------------

void EdidConfigPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    // The one-shot check at construction time can land on a stale or failed
    // read; refresh the status every time the page becomes visible.
    updateDeviceStatus();
}

void EdidConfigPage::updateDeviceStatus()
{
    VideoHid &videoHid = VideoHid::getInstance();

    // isOpen() only reflects whether a handle is momentarily held open; use
    // enumeration-based presence detection instead.
    bool devicePresent = videoHid.isHidDevicePresent();
    bool connected = false;

    if (devicePresent) {
        // Trust the polling thread's cached state — it reads the status
        // register continuously and its reads don't contend with the UI.
        // No direct USB reads here to avoid blocking the GUI thread.
        connected = videoHid.lastKnownHdmiConnected();
    }

    if (connected) {
        deviceStatusLabel->setText(tr("Connected"));
        deviceStatusLabel->setStyleSheet("QLabel { font-weight: bold; color: #2e7d32; }");
        deviceStatusIconLabel->setStyleSheet(
            "background-color: #2e7d32; border-radius: 8px; min-width: 16px; min-height: 16px;");
        readButton->setEnabled(true);
    } else {
        deviceStatusLabel->setText(devicePresent
            ? tr("Disconnected (no HDMI input signal detected)")
            : tr("Disconnected (device not found)"));
        deviceStatusLabel->setStyleSheet("QLabel { font-weight: bold; color: #c62828; }");
        deviceStatusIconLabel->setStyleSheet(
            "background-color: #c62828; border-radius: 8px; min-width: 16px; min-height: 16px;");
        readButton->setEnabled(false);
    }
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
    VideoHid::getInstance().stop();
    SerialPortManager::getInstance().stop();
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
    updateDeviceStatus();
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
    updateDeviceStatus();

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

    VideoHid &videoHid = VideoHid::getInstance();
    videoHid.stopPollingOnly();

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
        return;
    }

    shutdownFirmwareOperation();
    setControlsEnabled(true);
    updateDeviceStatus();
    displayNameLineEdit->setPlaceholderText(tr("Reading cancelled — enter display name"));
    enableApplyButton();
    restartPollingDelayed(tr("user cancellation"));
}
