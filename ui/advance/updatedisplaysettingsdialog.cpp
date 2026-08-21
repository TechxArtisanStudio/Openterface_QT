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

#include "updatedisplaysettingsdialog.h"
#include "edid/edidutils.h"
#include "edid/edidresolutionparser.h"
#include "edid/firmwareutils.h"
#include "../../video/videohid.h"
#include "edid/edidsettingsmanager.h"
#include "../../video/ms2109.h"
#include "../../serial/SerialPortManager.h"
#include "../mainwindow.h"
#include <QMessageBox>
#include <QCloseEvent>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QSpacerItem>
#include <QThread>
#include <QTimer>
#include <QEventLoop>
#include <QApplication>
#include <QDebug>
#include <QFile>
#include <QFileDialog>
#include <QGroupBox>
#include <QCheckBox>
#include <QStandardPaths>

UpdateDisplaySettingsDialog::UpdateDisplaySettingsDialog(QWidget *parent) :
    QDialog(parent),
    displayNameGroup(nullptr),
    displayNameCheckBox(nullptr),
    displayNameLineEdit(nullptr),
    serialNumberGroup(nullptr),
    serialNumberCheckBox(nullptr),
    serialNumberLineEdit(nullptr),
    resolutionGroup(nullptr),
    resolutionTable(nullptr),
    selectAllButton(nullptr),
    selectNoneButton(nullptr),
    selectDefaultButton(nullptr),
    updateButton(nullptr),
    cancelButton(nullptr),
    progressGroup(nullptr),
    progressBar(nullptr),
    progressLabel(nullptr),
    cancelReadingButton(nullptr),
    mainLayout(nullptr),
    displayNameLayout(nullptr),
    serialNumberLayout(nullptr),
    buttonLayout(nullptr),
    m_edidManager(nullptr),
    m_operationFinished(false),
    m_updateMode(false)
{
    setupUI();
    
    // Load current EDID settings when dialog is created
    loadCurrentEDIDSettings();
}

UpdateDisplaySettingsDialog::~UpdateDisplaySettingsDialog()
{

    if (m_edidManager) {
        m_edidManager->cancel();   // child of this dialog; Qt deletes it
    }

    // Qt will automatically delete child widgets
}

void UpdateDisplaySettingsDialog::setupUI()
{
    setWindowTitle(tr("Update Display Settings"));
    setModal(true);
    setMinimumSize(500, 600);
    resize(500, 600);

    mainLayout = new QVBoxLayout(this);

    displayNameGroup = buildSettingsSection(displayNameCheckBox, displayNameLineEdit, displayNameLayout,
                                            tr("Display Name"), tr("Update display name"),
                                            tr("Loading current display name..."));

    serialNumberGroup = buildSettingsSection(serialNumberCheckBox, serialNumberLineEdit, serialNumberLayout,
                                             tr("Serial Number"), tr("Update serial number"),
                                             tr("Loading current serial number..."));

    buildProgressSection();
    buildButtonSection();

    setLayout(mainLayout);

    connectUiSignals();

    enableUpdateButton();

    displayNameLineEdit->setFocus();
}

void UpdateDisplaySettingsDialog::ensureEdidManager()
{
    if (m_edidManager) {
        return;
    }
    m_edidManager = new edid::EdidSettingsManager(this);
    connect(m_edidManager, &edid::EdidSettingsManager::progress,
            this, &UpdateDisplaySettingsDialog::onFirmwareReadProgress);

    // Initial load: populate the fields and the resolution table.
    connect(m_edidManager, &edid::EdidSettingsManager::identityRead, this,
            [this](bool ok, const edid::EdidIdentity &id, const QString &error) {
        setProgressState(false, tr(""));
        if (!ok) {
            onFirmwareReadError(error);
            return;
        }
        m_pendingFirmwareData = id.rawImage;
        processFirmwareReadResult(true);
    });

    // Update path: the manager has read, written and read back.
    connect(m_edidManager, &edid::EdidSettingsManager::settingsApplied, this,
            [this](bool ok, bool verified, const edid::EdidIdentity &, const edid::EdidIdentity &, const QString &error) {
        if (!ok) {
            showErrorAndRestart(tr("Write Error"),
                                error.isEmpty() ? tr("Failed to write firmware to device.") : error,
                                tr("firmware write failure"));
            return;
        }
        m_operationFinished = true;
        setProgressState(false, tr(""));
        if (!verified) {
            QMessageBox::warning(this, tr("Not Verified"),
                tr("The write completed but the read-back does not match what was written.\n\n%1\n\n"
                   "A copy of the previous firmware image was saved to:\n%2")
                    .arg(error, m_edidManager->backupPath()));
            return;
        }
        QMessageBox::information(this, tr("Success"),
            tr("Display settings updated and verified by read-back!\n\nThe application will now exit.\n"
               "Please disconnect and reconnect the entire device to apply the changes."));
        QApplication::quit();
    });
}

QGroupBox* UpdateDisplaySettingsDialog::buildSettingsSection(QCheckBox *&checkBox,
                                                               QLineEdit *&lineEdit,
                                                               QVBoxLayout *&sectionLayout,
                                                               const QString &title,
                                                               const QString &checkboxText,
                                                               const QString &placeholderText)
{
    QGroupBox *group = new QGroupBox(title, this);
    sectionLayout = new QVBoxLayout(group);

    checkBox = new QCheckBox(checkboxText, this);
    checkBox->setChecked(false);
    sectionLayout->addWidget(checkBox);

    lineEdit = new QLineEdit(this);
    lineEdit->setPlaceholderText(placeholderText);
    lineEdit->setEnabled(false);
    sectionLayout->addWidget(lineEdit);

    mainLayout->addWidget(group);
    return group;
}

void UpdateDisplaySettingsDialog::buildProgressSection()
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
    mainLayout->addWidget(progressGroup);
}

void UpdateDisplaySettingsDialog::buildButtonSection()
{
    buttonLayout = new QHBoxLayout();
    QSpacerItem *horizontalSpacer = new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);
    buttonLayout->addItem(horizontalSpacer);

    updateButton = new QPushButton(tr("Update"), this);
    updateButton->setDefault(true);
    buttonLayout->addWidget(updateButton);

    backupButton = new QPushButton(tr("Backup Firmware"), this);
    backupButton->setEnabled(false); // initially disabled until firmware is loaded
    buttonLayout->addWidget(backupButton);

    cancelButton = new QPushButton(tr("Cancel"), this);
    buttonLayout->addWidget(cancelButton);

    mainLayout->addLayout(buttonLayout);
}

void UpdateDisplaySettingsDialog::connectUiSignals()
{
    connect(updateButton, &QPushButton::clicked, this, &UpdateDisplaySettingsDialog::onUpdateButtonClicked);
    connect(backupButton, &QPushButton::clicked, this, &UpdateDisplaySettingsDialog::onBackupFirmwareClicked);
    connect(cancelButton, &QPushButton::clicked, this, &UpdateDisplaySettingsDialog::onCancelButtonClicked);
    connect(displayNameCheckBox, &QCheckBox::toggled, this, &UpdateDisplaySettingsDialog::onDisplayNameCheckChanged);
    connect(serialNumberCheckBox, &QCheckBox::toggled, this, &UpdateDisplaySettingsDialog::onSerialNumberCheckChanged);
    connect(cancelReadingButton, &QPushButton::clicked, this, &UpdateDisplaySettingsDialog::onCancelReadingClicked);
    connect(displayNameLineEdit, &QLineEdit::textChanged, this, &UpdateDisplaySettingsDialog::enableUpdateButton);
    connect(serialNumberLineEdit, &QLineEdit::textChanged, this, &UpdateDisplaySettingsDialog::enableUpdateButton);
}

void UpdateDisplaySettingsDialog::setupResolutionTable()
{
    resolutionTable = new QTableWidget(this);
    resolutionTable->setColumnCount(4);
    
    QStringList headers;
    headers << tr("Enabled") << tr("Resolution") << tr("Refresh Rate") << tr("Source");
    resolutionTable->setHorizontalHeaderLabels(headers);
    
    // Set column widths
    resolutionTable->setColumnWidth(0, 70);   // Enabled checkbox
    resolutionTable->setColumnWidth(1, 120);  // Resolution
    resolutionTable->setColumnWidth(2, 100);  // Refresh Rate
    resolutionTable->setColumnWidth(3, 120);  // Source
    
    // Configure table properties
    resolutionTable->setAlternatingRowColors(true);
    resolutionTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    resolutionTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    resolutionTable->horizontalHeader()->setStretchLastSection(true);
    resolutionTable->verticalHeader()->setVisible(false);
    resolutionTable->setMinimumHeight(200);
    resolutionTable->setMaximumHeight(300);
    
    // Initially hide the resolution group as we don't have data yet
    resolutionGroup->setVisible(false);
}

void UpdateDisplaySettingsDialog::closeEvent(QCloseEvent *event)
{
    
    // If firmware reading is in progress, cancel it first
    if (progressGroup && progressGroup->isVisible()) {
        // Cancel the reading operation
        onCancelReadingClicked();
    }
    
    // Cancel any active firmware operation (read/write)
    if (m_edidManager) {
        m_edidManager->cancel();
    }
    
    event->accept();
}

void UpdateDisplaySettingsDialog::accept()
{
    onUpdateButtonClicked();
}

void UpdateDisplaySettingsDialog::reject()
{
    onCancelButtonClicked();
}

void UpdateDisplaySettingsDialog::onDisplayNameCheckChanged(bool checked)
{
    displayNameLineEdit->setEnabled(checked);
    enableUpdateButton();
}

void UpdateDisplaySettingsDialog::onSerialNumberCheckChanged(bool checked)
{
    serialNumberLineEdit->setEnabled(checked);
    enableUpdateButton();
}

void UpdateDisplaySettingsDialog::enableUpdateButton()
{
    bool hasChanges = (displayNameCheckBox->isChecked() && !displayNameLineEdit->text().trimmed().isEmpty()) ||
                      (serialNumberCheckBox->isChecked() && !serialNumberLineEdit->text().trimmed().isEmpty()) ||
                      hasResolutionChanges();
    updateButton->setEnabled(hasChanges);
}

void UpdateDisplaySettingsDialog::setDialogControlsEnabled(bool enabled)
{
    displayNameGroup->setEnabled(enabled);
    serialNumberGroup->setEnabled(enabled);
    if (resolutionGroup) {
        resolutionGroup->setEnabled(enabled);
    }
    if (updateButton) {
        updateButton->setEnabled(enabled);
    }
}

void UpdateDisplaySettingsDialog::setProgressState(bool active, const QString &labelText)
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

void UpdateDisplaySettingsDialog::shutdownFirmwareOperation(bool closeDialog)
{
    m_operationFinished = true;
    if (m_edidManager) {
        m_edidManager->cancel();
    }
    setProgressState(false, tr(""));

    if (closeDialog) {
        QDialog::reject();
    }
}

bool UpdateDisplaySettingsDialog::validateAsciiInput(const QString &text, int maxLen, const QString &fieldName, QString &errorMessage) const
{
    Q_UNUSED(maxLen);   // the EDID descriptor length is fixed; the manager owns the rule
    return edid::EdidSettingsManager::validateField(text, fieldName, errorMessage);
}

bool UpdateDisplaySettingsDialog::collectUpdateChanges(QString &newName, QString &newSerial, QStringList &changesSummary) const
{
    if (displayNameCheckBox->isChecked()) {
        newName = displayNameLineEdit->text().trimmed();
        QString err;
        if (!validateAsciiInput(newName, 13, tr("Display name"), err)) {
            QMessageBox::warning(const_cast<UpdateDisplaySettingsDialog*>(this), tr("Invalid Input"), err);
            return false;
        }
        changesSummary << tr("Display Name: %1").arg(newName);
    }

    if (serialNumberCheckBox->isChecked()) {
        newSerial = serialNumberLineEdit->text().trimmed();
        QString err;
        if (!validateAsciiInput(newSerial, 13, tr("Serial number"), err)) {
            QMessageBox::warning(const_cast<UpdateDisplaySettingsDialog*>(this), tr("Invalid Input"), err);
            return false;
        }
        changesSummary << tr("Serial Number: %1").arg(newSerial);
    }

    if (hasResolutionChanges()) {
        changesSummary << tr("Resolution Changes: %1 resolution(s) selected").arg(getSelectedResolutions().size());
    }

    if (newName.isEmpty() && newSerial.isEmpty() && !hasResolutionChanges()) {
        QMessageBox::warning(const_cast<UpdateDisplaySettingsDialog*>(this), tr("No Updates Selected"), tr("Please select at least one setting to update."));
        return false;
    }

    return true;
}

void UpdateDisplaySettingsDialog::onUpdateButtonClicked()
{
    QString newName;
    QString newSerial;
    QStringList changesSummary;

    if (!collectUpdateChanges(newName, newSerial, changesSummary)) {
        return;
    }

    QString summaryText = tr("The following changes will be applied:\n\n%1\n\nDo you want to continue?").arg(changesSummary.join("\n"));
    int reply = QMessageBox::question(this, tr("Confirm Updates"), summaryText,
                                      QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (reply != QMessageBox::Yes) {
        return;
    }

    if (!updateDisplaySettings(newName, newSerial)) {
        QMessageBox::critical(this, tr("Update Failed"), tr("Failed to start the update process."));
    }
}

void UpdateDisplaySettingsDialog::onCancelButtonClicked()
{
    QDialog::reject();
}

void UpdateDisplaySettingsDialog::setAllResolutionSelection(bool enable)
{
    if (!resolutionTable) return;

    for (int row = 0; row < resolutionTable->rowCount(); ++row) {
        QTableWidgetItem* checkItem = resolutionTable->item(row, 0);
        if (checkItem) {
            checkItem->setCheckState(enable ? Qt::Checked : Qt::Unchecked);
        }
    }

    resolutionModel.setSelectionFromTable(resolutionTable);
    enableUpdateButton();
}

void UpdateDisplaySettingsDialog::onSelectAllResolutions()
{
    setAllResolutionSelection(true);
}

void UpdateDisplaySettingsDialog::onSelectNoneResolutions()
{
    setAllResolutionSelection(false);
}

void UpdateDisplaySettingsDialog::onSelectDefaultResolutions()
{
    // Select common/default resolutions
    QStringList defaultResolutions = {
        "1920x1080", "1680x1050", "1280x1024", "1024x768", "800x600", "640x480"
    };
    
    for (int row = 0; row < resolutionTable->rowCount(); ++row) {
        QTableWidgetItem* resolutionItem = resolutionTable->item(row, 1);
        QTableWidgetItem* checkItem = resolutionTable->item(row, 0);
        
        if (resolutionItem && checkItem) {
            QString resolutionText = resolutionItem->text();
            bool isDefault = false;
            
            for (const QString& defaultRes : defaultResolutions) {
                if (resolutionText.contains(defaultRes)) {
                    isDefault = true;
                    break;
                }
            }
            
            checkItem->setCheckState(isDefault ? Qt::Checked : Qt::Unchecked);
        }
    }
}

void UpdateDisplaySettingsDialog::onResolutionItemChanged(QTableWidgetItem* item)
{
    if (item && item->column() == 0 && resolutionTable) {
        resolutionModel.setSelectionFromTable(resolutionTable);
        enableUpdateButton();
    }
}

void UpdateDisplaySettingsDialog::loadCurrentEDIDSettings()
{
    setProgressState(true, tr("Reading firmware data..."));
    setDialogControlsEnabled(false);
    m_updateMode = false;
    ensureEdidManager();
    m_edidManager->readIdentity();
}

void UpdateDisplaySettingsDialog::onFirmwareReadFinished(bool success)
{
    // Kept for the slot table; the manager's identityRead/settingsApplied
    // handlers in ensureEdidManager() do the work now.
    setProgressState(false, tr(""));
    if (!m_updateMode) {
        processFirmwareReadResult(success);
    }
}

void UpdateDisplaySettingsDialog::processFirmwareReadResult(bool success)
{
    // Re-enable controls regardless of result
    setDialogControlsEnabled(true);

    if (!success) {
        qWarning() << "Failed to read firmware data, cannot load current EDID settings";
        displayNameLineEdit->setPlaceholderText(tr("Failed to read firmware - enter display name"));
        serialNumberLineEdit->setPlaceholderText(tr("Failed to read firmware - enter serial number"));
        if (backupButton) {
            backupButton->setEnabled(false);
        }
        enableUpdateButton();
        return;
    }

    if (backupButton) {
        backupButton->setEnabled(true);
    }

    if (!processFirmwareData(m_pendingFirmwareData)) {
        displayNameLineEdit->setPlaceholderText(tr("Failed to parse firmware - enter display name"));
        serialNumberLineEdit->setPlaceholderText(tr("Failed to parse firmware - enter serial number"));
    }

    // Ensure state is refreshed
    enableUpdateButton();
}

bool UpdateDisplaySettingsDialog::processFirmwareData(const QByteArray &firmwareData)
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
        displayNameLineEdit->setText(currentDisplayName);
        displayNameLineEdit->setPlaceholderText(tr("Enter new display name (max 13 characters)"));
    } else {
        displayNameLineEdit->clear();
        displayNameLineEdit->setPlaceholderText(tr("No display name found - enter new name"));
    }

    if (!currentSerialNumber.isEmpty()) {
        serialNumberLineEdit->setText(currentSerialNumber);
        serialNumberLineEdit->setPlaceholderText(tr("Enter new serial number (max 13 characters)"));
    } else {
        serialNumberLineEdit->clear();
        serialNumberLineEdit->setPlaceholderText(tr("No serial number found - enter new serial"));
    }

    edid::EDIDUtils::logSupportedResolutions(edidBlock);
    edid::EDIDUtils::parseEDIDExtensionBlocks(firmwareData, edidOffset);
    readResolutionFromEDID(edidBlock, firmwareData);
    edid::EDIDUtils::showEDIDDescriptors(edidBlock);

    return true;
}

bool UpdateDisplaySettingsDialog::parseEdidBlock(const QByteArray &firmwareData, int &edidOffset, QByteArray &edidBlock) const
{
    edidOffset = edid::EDIDUtils::findEDIDBlock0(firmwareData);
    if (edidOffset == -1 || edidOffset + 128 > firmwareData.size()) {
        return false;
    }

    edidBlock = firmwareData.mid(edidOffset, 128);
    return true;
}

void UpdateDisplaySettingsDialog::restartPollingDelayed(const QString &reason)
{
    QTimer::singleShot(500, this, [this, reason]() {
        VideoHid::getInstance().start();
    });
}

void UpdateDisplaySettingsDialog::showErrorAndRestart(const QString &title, const QString &message, const QString &reason)
{
    setProgressState(false, tr(""));
    QMessageBox::critical(this, title, message);
    restartPollingDelayed(reason);
}

void UpdateDisplaySettingsDialog::onFirmwareReadError(const QString& errorMessage)
{
    qWarning() << "Firmware read error:" << errorMessage;

    setDialogControlsEnabled(true);
    if (updateButton) updateButton->setEnabled(true);
    if (backupButton) backupButton->setEnabled(false);
    displayNameLineEdit->setPlaceholderText(tr("Error reading firmware - enter display name"));
    serialNumberLineEdit->setPlaceholderText(tr("Error reading firmware - enter serial number"));
    enableUpdateButton();

    shutdownFirmwareOperation(false);
    showErrorAndRestart(tr("Firmware Read Error"), tr("Failed to read firmware: %1").arg(errorMessage), tr("firmware read error"));
}

void UpdateDisplaySettingsDialog::onCancelReadingClicked()
{
    if (m_operationFinished) {
        return;
    }

    shutdownFirmwareOperation(true);

    setDialogControlsEnabled(true);
    displayNameLineEdit->setPlaceholderText(tr("Reading cancelled - enter display name"));
    serialNumberLineEdit->setPlaceholderText(tr("Reading cancelled - enter serial number"));
    enableUpdateButton();
    restartPollingDelayed(tr("user cancellation"));
}

void UpdateDisplaySettingsDialog::populateResolutionTableFromModel()
{
    if (!resolutionTable) return;

    resolutionModel.populateTable(resolutionTable);

    if (!resolutionModel.isEmpty()) {
        resolutionGroup->setVisible(true);
    }
}

void UpdateDisplaySettingsDialog::readResolutionFromEDID(const QByteArray &edidBlock, const QByteArray &firmwareData)
{
    resolutionModel.loadFromEDID(edidBlock, firmwareData);
    populateResolutionTableFromModel();
}

QList<ResolutionInfo> UpdateDisplaySettingsDialog::getSelectedResolutions() const
{
    return resolutionModel.selected();
}

bool UpdateDisplaySettingsDialog::hasResolutionChanges() const
{
    return resolutionModel.hasChanges();
}

bool UpdateDisplaySettingsDialog::updateDisplaySettings(const QString &newName, const QString &newSerial)
{
    setProgressState(true, tr("Updating display settings..."));
    if (updateButton) {
        updateButton->setEnabled(false);
    }
    m_updateMode = true;
    ensureEdidManager();
    // Read -> edit -> write -> read back & verify, all inside the manager.
    m_edidManager->applySettings(newName, newSerial, &resolutionModel);
    return true;
}

void UpdateDisplaySettingsDialog::onFirmwareReadProgress(int percent)
{
    if (progressBar) {
        progressBar->setValue(percent);
    }
}

void UpdateDisplaySettingsDialog::onBackupFirmwareClicked()
{
    if (m_pendingFirmwareData.isEmpty()) {
        QMessageBox::warning(this, tr("Backup Firmware"), tr("No firmware data available to backup."));
        return;
    }

    QString filePath = QFileDialog::getSaveFileName(this, tr("Backup firmware to"), QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/firmware_backup.bin", tr("Binary Files (*.bin);;All Files (*)"));
    if (filePath.isEmpty()) {
        return;
    }

    if (!edid::FirmwareUtils::backupFirmware(m_pendingFirmwareData, filePath)) {
        QMessageBox::critical(this, tr("Backup Firmware"), tr("Failed to save firmware backup to %1").arg(filePath));
        return;
    }

    QMessageBox::information(this, tr("Backup Firmware"), tr("Firmware backup saved to %1").arg(filePath));
}

void UpdateDisplaySettingsDialog::stopAllDevices()
{
    
    // Stop VideoHid
    VideoHid::getInstance().stop();
    
    // Stop Serial Port Manager
    SerialPortManager::getInstance().stop();
    
    // Get main window to access camera and audio managers
    QWidget *parentWindow = this->parentWidget();
    if (parentWindow) {
        // Try to cast to MainWindow to access device managers
        // This is a simplified approach - in practice you might want a more robust method
    } else {
    }
    
}

void UpdateDisplaySettingsDialog::hideMainWindow()
{
    QWidget *mainWindow = this->parentWidget();
    if (mainWindow) {
        mainWindow->hide();
    }
}
