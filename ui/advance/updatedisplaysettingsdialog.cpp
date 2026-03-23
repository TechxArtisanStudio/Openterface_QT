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
#include "../../video/firmwarereader.h"
#include "../../video/firmwarewriter.h"
#include "../../video/ms2109.h"
#include "../../serial/SerialPortManager.h"
#include "../mainwindow.h"
#include <QMessageBox>
#include <QCloseEvent>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QSpacerItem>
#include <QProgressDialog>
#include <QThread>
#include <QTimer>
#include <QEventLoop>
#include <QApplication>
#include <QDebug>
#include <QFile>
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
    mainLayout(nullptr),
    displayNameLayout(nullptr),
    serialNumberLayout(nullptr),
    buttonLayout(nullptr),
    progressDialog(nullptr),
    progressGroup(nullptr),
    progressBar(nullptr),
    progressLabel(nullptr),
    cancelReadingButton(nullptr),
    firmwareReaderThread(nullptr),
    firmwareReader(nullptr),
    m_cleanupInProgress(false),
    m_operationFinished(false)
{
    setupUI();
    
    // Load current EDID settings when dialog is created
    loadCurrentEDIDSettings();
}

UpdateDisplaySettingsDialog::~UpdateDisplaySettingsDialog()
{
    qDebug() << "UpdateDisplaySettingsDialog destructor called";
    
    // Clean up progress dialog first (if it exists)
    if (progressDialog) {
        progressDialog->close();
        progressDialog->deleteLater();
        progressDialog = nullptr;
    }
    
    // Clean up firmware reader thread if still running
    // Use a more aggressive cleanup in destructor
    if (firmwareReaderThread) {
        // Disconnect all signals to prevent callbacks during cleanup
        firmwareReaderThread->disconnect();
        if (firmwareReader) {
            firmwareReader->disconnect();
        }
        
        // If thread is running, force terminate immediately
        if (firmwareReaderThread->isRunning()) {
            qDebug() << "Force terminating firmware reader thread in destructor";
            firmwareReaderThread->requestInterruption();
            firmwareReaderThread->terminate();
            firmwareReaderThread->wait(500); // Very brief wait
        }
        
        // Clean up objects
        if (firmwareReader) {
            firmwareReader->deleteLater();
            firmwareReader = nullptr;
        }
        
        firmwareReaderThread->deleteLater();
        firmwareReaderThread = nullptr;
    }
    
    qDebug() << "UpdateDisplaySettingsDialog destructor completed";
    // Qt will automatically delete child widgets
}

void UpdateDisplaySettingsDialog::cleanupFirmwareReaderThread()
{
    // Prevent double cleanup
    if (m_cleanupInProgress) {
        qDebug() << "Cleanup already in progress, skipping";
        return;
    }
    
    m_cleanupInProgress = true;
    qDebug() << "Starting firmware reader thread cleanup";
    
    if (firmwareReaderThread) {
        // Disconnect all signals to prevent callbacks during cleanup
        firmwareReaderThread->disconnect();
        if (firmwareReader) {
            firmwareReader->disconnect();
        }
        
        // If thread is running, try to stop it gracefully
        if (firmwareReaderThread->isRunning()) {
            qDebug() << "Stopping running firmware reader thread";
            firmwareReaderThread->requestInterruption();
            firmwareReaderThread->quit();
            
            // Wait briefly for graceful shutdown
            if (!firmwareReaderThread->wait(1000)) {
                qWarning() << "Firmware reader thread didn't quit gracefully, terminating...";
                firmwareReaderThread->terminate();
                firmwareReaderThread->wait(500); // Brief wait after terminate
            }
        }
        
        // Clean up objects manually if they haven't been auto-deleted yet
        if (firmwareReader && !firmwareReader->parent()) {
            firmwareReader->deleteLater();
        }
        firmwareReader = nullptr;
        
        // Schedule thread for deletion
        firmwareReaderThread->deleteLater();
        firmwareReaderThread = nullptr;
    }
    
    qDebug() << "Firmware reader thread cleanup completed";
    m_cleanupInProgress = false;
}

void UpdateDisplaySettingsDialog::setupUI()
{
    setWindowTitle(tr("Update Display Settings"));
    setModal(true);
    setMinimumSize(500, 600);
    resize(500, 600);

    mainLayout = new QVBoxLayout(this);

    buildDisplayNameSection();
    buildSerialNumberSection();
    buildProgressSection();
    buildButtonSection();

    setLayout(mainLayout);

    connectUiSignals();

    enableUpdateButton();

    displayNameLineEdit->setFocus();
}

void UpdateDisplaySettingsDialog::buildDisplayNameSection()
{
    displayNameGroup = new QGroupBox(tr("Display Name"), this);
    displayNameLayout = new QVBoxLayout(displayNameGroup);

    displayNameCheckBox = new QCheckBox(tr("Update display name"), this);
    displayNameCheckBox->setChecked(false);
    displayNameLayout->addWidget(displayNameCheckBox);

    displayNameLineEdit = new QLineEdit(this);
    displayNameLineEdit->setPlaceholderText(tr("Loading current display name..."));
    displayNameLineEdit->setEnabled(false);
    displayNameLayout->addWidget(displayNameLineEdit);

    mainLayout->addWidget(displayNameGroup);
}

void UpdateDisplaySettingsDialog::buildSerialNumberSection()
{
    serialNumberGroup = new QGroupBox(tr("Serial Number"), this);
    serialNumberLayout = new QVBoxLayout(serialNumberGroup);

    serialNumberCheckBox = new QCheckBox(tr("Update serial number"), this);
    serialNumberCheckBox->setChecked(false);
    serialNumberLayout->addWidget(serialNumberCheckBox);

    serialNumberLineEdit = new QLineEdit(this);
    serialNumberLineEdit->setPlaceholderText(tr("Loading current serial number..."));
    serialNumberLineEdit->setEnabled(false);
    serialNumberLayout->addWidget(serialNumberLineEdit);

    mainLayout->addWidget(serialNumberGroup);
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

    cancelButton = new QPushButton(tr("Cancel"), this);
    buttonLayout->addWidget(cancelButton);

    mainLayout->addLayout(buttonLayout);
}

void UpdateDisplaySettingsDialog::connectUiSignals()
{
    connect(updateButton, &QPushButton::clicked, this, &UpdateDisplaySettingsDialog::onUpdateButtonClicked);
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
    qDebug() << "Dialog close event triggered";
    
    // If firmware reading is in progress, cancel it first
    if (progressGroup && progressGroup->isVisible()) {
        // Cancel the reading operation
        onCancelReadingClicked();
    }
    
    // Legacy: Also handle old progress dialog if it exists
    if (progressDialog && progressDialog->isVisible()) {
        progressDialog->cancel();
    }
    
    // Don't call cleanupFirmwareReaderThread() here as it will be called in destructor
    // Just ensure the thread is interrupted
    if (firmwareReaderThread && firmwareReaderThread->isRunning()) {
        qDebug() << "Requesting thread interruption in closeEvent";
        firmwareReaderThread->requestInterruption();
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

bool UpdateDisplaySettingsDialog::validateAsciiInput(const QString &text, int maxLen, const QString &fieldName, QString &errorMessage) const
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
        if (row < availableResolutions.size()) {
            availableResolutions[row].userSelected = enable;
        }
    }
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
    if (item && item->column() == 0) { // Only handle checkbox column
        int row = item->row();
        if (row < availableResolutions.size()) {
            availableResolutions[row].userSelected = (item->checkState() == Qt::Checked);
        }
    }
}

void UpdateDisplaySettingsDialog::loadCurrentEDIDSettings()
{
    qDebug() << "Loading current EDID settings from firmware...";
    
    // Get VideoHid instance and stop polling to prevent conflicts
    VideoHid& videoHid = VideoHid::getInstance();
    videoHid.stopPollingOnly();
    
    // Give a moment for polling to stop
    QThread::msleep(100);
    
    // Read firmware size
    quint32 firmwareSize = videoHid.readFirmwareSize();
    if (firmwareSize == 0) {
        qWarning() << "Failed to read firmware size, cannot load current EDID settings";
        displayNameLineEdit->setPlaceholderText(tr("Failed to read firmware - enter display name"));
        serialNumberLineEdit->setPlaceholderText(tr("Failed to read firmware - enter serial number"));
        
        // Restart polling on failure
        videoHid.start();
        return;
    }
    
    qDebug() << "Firmware size:" << firmwareSize << "bytes";
    
    // Show embedded progress components
    progressGroup->setVisible(true);
    progressBar->setValue(0);
    progressLabel->setText(tr("Reading firmware data..."));
    
    // Disable main dialog controls while reading
    setDialogControlsEnabled(false);
    
    // Create temporary file path for firmware reading
    QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    QString tempFirmwarePath = tempDir + "/temp_firmware_read.bin";
    
    // Create firmware reader thread
    firmwareReaderThread = new QThread(this);
    firmwareReader = new FirmwareReader(&videoHid, ADDR_EEPROM, firmwareSize, tempFirmwarePath);
    firmwareReader->moveToThread(firmwareReaderThread);
    
    // Connect signals
    connect(firmwareReaderThread, &QThread::started, firmwareReader, &FirmwareReader::process);
    connect(firmwareReader, &FirmwareReader::progress, this, &UpdateDisplaySettingsDialog::onFirmwareReadProgress);
    connect(firmwareReader, &FirmwareReader::finished, this, &UpdateDisplaySettingsDialog::onFirmwareReadFinished);
    connect(firmwareReader, &FirmwareReader::error, this, &UpdateDisplaySettingsDialog::onFirmwareReadError);
    connect(firmwareReader, &FirmwareReader::finished, firmwareReaderThread, &QThread::quit);
    
    // Note: We handle cleanup manually in cleanupFirmwareReaderThread() instead of auto-deleting
    // to avoid crashes when the dialog is closed while the thread is running
    
    // Start the thread
    firmwareReaderThread->start();
}

void UpdateDisplaySettingsDialog::onFirmwareReadProgress(int percent)
{
    // Update embedded progress bar
    if (progressBar) {
        progressBar->setValue(percent);
    }
    
    // Also update old progress dialog if it exists (for compatibility)
    if (progressDialog) {
        progressDialog->setValue(percent);
    }
}

void UpdateDisplaySettingsDialog::onFirmwareReadFinished(bool success)
{
    // Hide embedded progress components
    if (progressGroup) {
        progressGroup->setVisible(false);
    }

    // Clean up old progress dialog if it exists
    if (progressDialog) {
        progressDialog->close();
        progressDialog->deleteLater();
        progressDialog = nullptr;
    }

    processFirmwareReadResult(success);

    // Clean up firmware reader thread and restart polling
    QTimer::singleShot(0, this, [this]() {
        cleanupFirmwareReaderThread();
        QTimer::singleShot(500, [this]() {
            VideoHid::getInstance().start();
        });
    });
}

void UpdateDisplaySettingsDialog::processFirmwareReadResult(bool success)
{
    // Re-enable controls regardless of result
    setDialogControlsEnabled(true);

    if (!success) {
        qWarning() << "Failed to read firmware data, cannot load current EDID settings";
        displayNameLineEdit->setPlaceholderText(tr("Failed to read firmware - enter display name"));
        serialNumberLineEdit->setPlaceholderText(tr("Failed to read firmware - enter serial number"));
        enableUpdateButton();
        return;
    }

    QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    QString tempFirmwarePath = tempDir + "/temp_firmware_read.bin";

    if (!processFirmwareFile(tempFirmwarePath)) {
        displayNameLineEdit->setPlaceholderText(tr("Failed to parse firmware - enter display name"));
        serialNumberLineEdit->setPlaceholderText(tr("Failed to parse firmware - enter serial number"));
    }

    // Ensure state is refreshed
    enableUpdateButton();
}

bool UpdateDisplaySettingsDialog::processFirmwareFile(const QString &tempFirmwarePath)
{
    QFile file(tempFirmwarePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Failed to open temporary firmware file";
        return false;
    }

    QByteArray firmwareData = file.readAll();
    file.close();
    QFile::remove(tempFirmwarePath);

    if (firmwareData.isEmpty()) {
        qWarning() << "Empty firmware data read from file";
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
    updateResolutionTableFromEDID(edidBlock, firmwareData, edidOffset);
    qDebug() << "=== CURRENT EDID DESCRIPTORS ===";
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

void UpdateDisplaySettingsDialog::applyEdidUpdates(QByteArray &modifiedFirmware, int edidOffset, const QString &newName,
                                                  const QString &newSerial)
{
    QByteArray edidBlock = modifiedFirmware.mid(edidOffset, 128);

    if (!newName.isEmpty()) {
        edid::EDIDUtils::updateEDIDDisplayName(edidBlock, newName);
    }
    if (!newSerial.isEmpty()) {
        edid::EDIDUtils::updateEDIDSerialNumber(edidBlock, newSerial);
    }

    if (hasResolutionChanges()) {
        updateExtensionBlockResolutions(modifiedFirmware, edidOffset);
    }

    quint8 edidChecksum = edid::EDIDUtils::calculateEDIDChecksum(edidBlock);
    edidBlock[127] = edidChecksum;
    modifiedFirmware.replace(edidOffset, 128, edidBlock);
}

void UpdateDisplaySettingsDialog::finalizeEdidBlock(QByteArray &modifiedFirmware, int /*edidOffset*/,
                                                   const QByteArray &originalFirmware,
                                                   const QByteArray &/*originalEdidBlock*/)
{
    quint16 firmwareChecksum = calculateFirmwareChecksumWithDiff(originalFirmware, modifiedFirmware);

    if (modifiedFirmware.size() >= 2) {
        modifiedFirmware[modifiedFirmware.size() - 2] = static_cast<char>((firmwareChecksum >> 8) & 0xFF);
        modifiedFirmware[modifiedFirmware.size() - 1] = static_cast<char>(firmwareChecksum & 0xFF);
    }
}

void UpdateDisplaySettingsDialog::setupProgressDialog()
{
    if (progressDialog) return;
    progressDialog = new QProgressDialog(tr("Updating display settings..."), tr("Cancel"), 0, 100, this);
    progressDialog->setWindowModality(Qt::WindowModal);
    progressDialog->setAutoClose(false);
    progressDialog->setAutoReset(false);
    connect(progressDialog, &QProgressDialog::canceled, this, [this]() {
        qDebug() << "Firmware operation canceled by user";
        onCancelReadingClicked();
    });
    progressDialog->show();
}

void UpdateDisplaySettingsDialog::closeProgressDialog()
{
    if (!progressDialog) return;
    progressDialog->blockSignals(true); // prevent canceled() from firing synchronously during close
    progressDialog->close();
    progressDialog->deleteLater();
    progressDialog = nullptr;
}

void UpdateDisplaySettingsDialog::restartPollingDelayed(const QString &reason)
{
    QTimer::singleShot(500, this, [this, reason]() {
        VideoHid::getInstance().start();
        qDebug() << "Polling restarted after" << reason;
    });
}

void UpdateDisplaySettingsDialog::showErrorAndRestart(const QString &title, const QString &message, const QString &reason)
{
    closeProgressDialog();
    QMessageBox::critical(this, title, message);
    restartPollingDelayed(reason);
}

bool UpdateDisplaySettingsDialog::readFirmwareFile(const QString &path, QByteArray &outData)
{
    QFile firmwareFile(path);
    if (!firmwareFile.open(QIODevice::ReadOnly)) {
        return false;
    }
    outData = firmwareFile.readAll();
    firmwareFile.close();
    return true;
}

void UpdateDisplaySettingsDialog::startFirmwareWrite(const QByteArray &modifiedFirmware, const QString &tempFirmwarePath)
{
    QThread* writerThread = new QThread();
    FirmwareWriter* firmwareWriter = new FirmwareWriter(&VideoHid::getInstance(), ADDR_EEPROM, modifiedFirmware, nullptr);
    firmwareWriter->moveToThread(writerThread);

    connect(firmwareWriter, &FirmwareWriter::progress, this, [this](int percent) {
        if (progressDialog) {
            progressDialog->setValue(40 + percent * 60 / 100);
        }
    });

    connect(firmwareWriter, &FirmwareWriter::finished, this, [this, tempFirmwarePath, writerThread](bool writeSuccess) {
        qDebug() << "FirmwareWriter finished with success:" << writeSuccess;
        closeProgressDialog();
        QFile::remove(tempFirmwarePath);

        if (writeSuccess) {
            m_operationFinished = true;
            progressGroup->setVisible(false);
            closeProgressDialog();
            QMessageBox::information(this, tr("Success"), tr("Display settings updated successfully!\n\nThe application will now exit.\nPlease disconnect and reconnect the entire device to apply the changes."));
            qDebug() << "Success dialog acknowledged, exiting application";
            QApplication::quit();
        } else {
            showErrorAndRestart(tr("Write Error"), tr("Failed to write firmware to device."), tr("firmware write failure"));
        }
    });

    connect(writerThread, &QThread::started, firmwareWriter, &FirmwareWriter::process);
    connect(firmwareWriter, &FirmwareWriter::finished, writerThread, &QThread::quit);
    connect(firmwareWriter, &FirmwareWriter::finished, firmwareWriter, &FirmwareWriter::deleteLater);
    connect(writerThread, &QThread::finished, writerThread, &QThread::deleteLater);

    writerThread->start();
}

void UpdateDisplaySettingsDialog::onFirmwareReadError(const QString& errorMessage)
{
    if (progressGroup) progressGroup->setVisible(false);
    closeProgressDialog();

    setDialogControlsEnabled(true);

    qWarning() << "Firmware read error:" << errorMessage;
    displayNameLineEdit->setPlaceholderText(tr("Error reading firmware - enter display name"));
    serialNumberLineEdit->setPlaceholderText(tr("Error reading firmware - enter serial number"));
    enableUpdateButton();

    showErrorAndRestart(tr("Firmware Read Error"), tr("Failed to read firmware: %1").arg(errorMessage), tr("firmware read error"));
    cleanupFirmwareReaderThread();
}

void UpdateDisplaySettingsDialog::onCancelReadingClicked()
{
    if (m_operationFinished) {
        qDebug() << "Cancel ignored because operation is already finished";
        return;
    }

    qDebug() << "User cancelled firmware reading";

    if (firmwareReaderThread && firmwareReaderThread->isRunning()) {
        qDebug() << "Requesting thread interruption and quit";
        firmwareReaderThread->requestInterruption();
        firmwareReaderThread->quit();
    }

    cleanupFirmwareReaderThread();

    if (progressGroup) {
        progressGroup->setVisible(false);
    }

    closeProgressDialog();

    setDialogControlsEnabled(true);

    displayNameLineEdit->setPlaceholderText(tr("Reading cancelled - enter display name"));
    serialNumberLineEdit->setPlaceholderText(tr("Reading cancelled - enter serial number"));
    enableUpdateButton();

    restartPollingDelayed(tr("user cancellation"));

    // If this was triggered through the dialog cancel button, close the dialog
    if (isVisible()) {
        QDialog::reject();
    }
}


void UpdateDisplaySettingsDialog::addResolutionToList(const QString& description, int width, int height, int refreshRate, 
                                                    quint8 vic, bool isStandardTiming, bool isEnabled)
{
    // Check if resolution already exists to avoid duplicates
    for (const auto& existing : availableResolutions) {
        if (existing.width == width && existing.height == height && existing.refreshRate == refreshRate) {
            return; // Already exists
        }
    }
    
    ResolutionInfo resolution(description, width, height, refreshRate, vic, isStandardTiming);
    resolution.isEnabled = isEnabled;
    resolution.userSelected = isEnabled; // Default to current EDID state
    
    availableResolutions.append(resolution);
    qDebug() << "Added resolution:" << description;
}

void UpdateDisplaySettingsDialog::populateResolutionTable()
{
    if (!resolutionTable) return;  // Skip if resolution table is not shown
    
    resolutionTable->setRowCount(availableResolutions.size());
    
    for (int row = 0; row < availableResolutions.size(); ++row) {
        const ResolutionInfo& resolution = availableResolutions[row];
        
        // Enabled checkbox
        QTableWidgetItem* checkItem = new QTableWidgetItem();
        checkItem->setCheckState(resolution.userSelected ? Qt::Checked : Qt::Unchecked);
        checkItem->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
        resolutionTable->setItem(row, 0, checkItem);
        
        // Resolution text
        QTableWidgetItem* resolutionItem = new QTableWidgetItem(QString("%1x%2").arg(resolution.width).arg(resolution.height));
        resolutionItem->setFlags(Qt::ItemIsEnabled);
        resolutionTable->setItem(row, 1, resolutionItem);
        
        // Refresh rate
        QTableWidgetItem* refreshItem = new QTableWidgetItem(QString("%1 Hz").arg(resolution.refreshRate));
        refreshItem->setFlags(Qt::ItemIsEnabled);
        resolutionTable->setItem(row, 2, refreshItem);
        
        // Source - only show extension sources now
        QString sourceText = tr("Extension");
        if (resolution.vic > 0) {
            sourceText += QString(" (VIC %1)").arg(resolution.vic);
        }
        QTableWidgetItem* sourceItem = new QTableWidgetItem(sourceText);
        sourceItem->setFlags(Qt::ItemIsEnabled);
        resolutionTable->setItem(row, 3, sourceItem);
    }
    
    // Show the resolution group now that we have data
    if (availableResolutions.size() > 0) {
        resolutionGroup->setVisible(true);
        qDebug() << "Resolution table populated with" << availableResolutions.size() << "extension block resolutions";
    } else {
        qDebug() << "No extension block resolutions found";
        // You could optionally show a message or keep the group hidden
    }
}

void UpdateDisplaySettingsDialog::updateResolutionTableFromEDID(const QByteArray &edidBlock, const QByteArray &firmwareData, int baseOffset)
{
    // Reset resolution list and rebuild from EDID
    availableResolutions.clear();
    if (resolutionTable) {
        resolutionTable->setRowCount(0);
    }

    collectResolutionsFromEDID(edidBlock, firmwareData);
    populateResolutionTable();
}

void UpdateDisplaySettingsDialog::collectResolutionsFromEDID(const QByteArray &edidBlock, const QByteArray &firmwareData)
{
    auto standardTimings = edid::EDIDResolutionParser::parseStandardTimings(edidBlock);
    for (const auto &r : standardTimings) {
        addResolutionToList(r.description, r.width, r.height, r.refreshRate, r.vic, r.isStandardTiming, r.isEnabled);
    }

    auto detailedTimings = edid::EDIDResolutionParser::parseDetailedTimingDescriptors(edidBlock);
    for (const auto &r : detailedTimings) {
        addResolutionToList(r.description, r.width, r.height, r.refreshRate, r.vic, r.isStandardTiming, r.isEnabled);
    }

    auto extensionResolutions = edid::EDIDResolutionParser::parseCEA861ExtensionBlocks(firmwareData, edid::EDIDUtils::findEDIDBlock0(firmwareData));
    for (const auto &r : extensionResolutions) {
        addResolutionToList(r.description, r.width, r.height, r.refreshRate, r.vic, r.isStandardTiming, r.isEnabled);
    }
}

QList<ResolutionInfo> UpdateDisplaySettingsDialog::getSelectedResolutions() const
{
    QList<ResolutionInfo> selectedResolutions;
    for (const auto& resolution : availableResolutions) {
        if (resolution.userSelected) {
            selectedResolutions.append(resolution);
        }
    }
    return selectedResolutions;
}

bool UpdateDisplaySettingsDialog::hasResolutionChanges() const
{
    if (!resolutionTable) return false;  // No resolution UI, no changes
    
    // Check if any resolution's current selection differs from its original enabled state
    for (const auto& resolution : availableResolutions) {
        if (resolution.userSelected != resolution.isEnabled) {
            return true;
        }
    }
    return false;
}


void UpdateDisplaySettingsDialog::updateExtensionBlockResolutions(QByteArray &firmwareData, int edidOffset)
{
    qDebug() << "=== UPDATING EXTENSION BLOCK RESOLUTIONS ===";
    
    // Check if there are extension blocks
    if (edidOffset + 126 >= firmwareData.size()) {
        qWarning() << "EDID block too small to check extension count";
        return;
    }
    
    quint8 extensionCount = static_cast<quint8>(firmwareData[edidOffset + 126]);
    if (extensionCount == 0) {
        qDebug() << "No extension blocks found - cannot update resolutions";
        return;
    }
    
    qDebug() << "Found" << extensionCount << "extension block(s) for resolution updates";
    
    // Build sets of enabled and disabled VICs based on user selection
    QSet<quint8> enabledVICs;
    QSet<quint8> disabledVICs;
    
    for (const auto& resolution : availableResolutions) {
        if (resolution.vic > 0) {
            if (resolution.userSelected) {
                enabledVICs.insert(resolution.vic);
                qDebug() << "  Enable VIC" << resolution.vic << ":" << resolution.description;
            } else {
                disabledVICs.insert(resolution.vic);
                qDebug() << "  Disable VIC" << resolution.vic << ":" << resolution.description;
            }
        }
    }
    
    qDebug() << "Total VICs to enable:" << enabledVICs.size() << ", to disable:" << disabledVICs.size();
    
    // Process each extension block
    bool anyBlockModified = false;
    for (int blockIndex = 1; blockIndex <= extensionCount; ++blockIndex) {
        int blockOffset = edidOffset + (blockIndex * 128);
        
        if (blockOffset + 128 > firmwareData.size()) {
            qWarning() << "Extension Block" << blockIndex << "exceeds firmware size";
            continue;
        }
        
        QByteArray extensionBlock = firmwareData.mid(blockOffset, 128);
        quint8 extensionTag = static_cast<quint8>(extensionBlock[0]);
        
        if (extensionTag == 0x02) { // CEA-861 Extension Block
            qDebug() << "Processing CEA-861 extension block" << blockIndex << "at offset" << blockOffset;
            
            if (updateCEA861ExtensionBlockResolutions(extensionBlock, enabledVICs, disabledVICs)) {
                // Calculate new checksum for this extension block
                    quint8 blockChecksum = edid::EDIDUtils::calculateEDIDChecksum(extensionBlock);
                qDebug() << "Updated extension block" << blockIndex << "checksum to 0x" 
                         << QString::number(blockChecksum, 16).toUpper().rightJustified(2, '0');
                
                // Replace the modified extension block back into firmware
                firmwareData.replace(blockOffset, 128, extensionBlock);
                anyBlockModified = true;
                
                qDebug() << "Extension block" << blockIndex << "updated successfully";
            }
        } else {
            qDebug() << "Skipping non-CEA-861 extension block" << blockIndex << "(tag 0x" 
                     << QString::number(extensionTag, 16).toUpper().rightJustified(2, '0') << ")";
        }
    }
    
    if (anyBlockModified) {
        qDebug() << "Extension blocks have been updated with resolution changes";
    } else {
        qDebug() << "No extension blocks were modified";
    }
    
    qDebug() << "=== EXTENSION BLOCK RESOLUTION UPDATE COMPLETE ===";
}

bool UpdateDisplaySettingsDialog::updateCEA861ExtensionBlockResolutions(QByteArray &block, const QSet<quint8> &enabledVICs, const QSet<quint8> &disabledVICs)
{
    if (block.size() != 128) {
        qWarning() << "Invalid CEA-861 extension block size:" << block.size();
        return false;
    }
    
    quint8 dtdOffset = static_cast<quint8>(block[2]);
    if (dtdOffset <= 4 || dtdOffset > 127) {
        qWarning() << "Invalid DTD offset in CEA-861 block:" << dtdOffset;
        return false;
    }
    
    // Parse data blocks to find Video Data Block
    int offset = 4;
    bool foundVideoDataBlock = false;
    bool blockModified = false;
    
    while (offset < dtdOffset && offset < block.size()) {
        quint8 header = static_cast<quint8>(block[offset]);
        quint8 tag = (header >> 5) & 0x07;
        quint8 length = header & 0x1F;
        
        if (tag == 2) { // Video Data Block
            qDebug() << "Found Video Data Block at offset" << offset << "with length" << length;
            foundVideoDataBlock = true;
            
            // Update VIC codes in this block
            for (int i = 1; i <= length && offset + i < block.size(); ++i) {
                quint8 currentVIC = static_cast<quint8>(block[offset + i]) & 0x7F;
                bool isNative = (static_cast<quint8>(block[offset + i]) & 0x80) != 0;
                
                if (currentVIC == 0) {
                    // Skip already disabled entries
                    continue;
                }
                
                if (disabledVICs.contains(currentVIC)) {
                    // Disable this VIC by setting it to 0
                    qDebug() << "  Disabling VIC" << currentVIC << "-> setting to 0x00";
                    block[offset + i] = 0x00;
                    blockModified = true;
                } else if (enabledVICs.contains(currentVIC)) {
                    // Keep this VIC enabled (no change needed)
                    qDebug() << "  VIC" << currentVIC << "remains enabled" << (isNative ? "(native)" : "");
                } else {
                    // VIC not in our list - leave as is for now
                    qDebug() << "  VIC" << currentVIC << "not in selection list - leaving unchanged";
                }
            }
            
            // TODO: Add new VICs if needed (requires expanding the data block)
            // For now, we only disable existing VICs that are not selected
            
            break; // We found and processed the Video Data Block
        }
        
        offset += length + 1;
        if (offset >= dtdOffset) break;
    }
    
    if (!foundVideoDataBlock) {
        qDebug() << "No Video Data Block found in CEA-861 extension block";
        return false;
    }
    
    return blockModified;
}

bool UpdateDisplaySettingsDialog::updateDisplaySettings(const QString &newName, const QString &newSerial)
{
    qDebug() << "Starting display settings update...";
    if (displayNameCheckBox->isChecked()) {
        qDebug() << "  Display name:" << newName;
    }
    if (serialNumberCheckBox->isChecked()) {
        qDebug() << "  Serial number:" << newSerial;
    }
    
    // Stop polling before starting firmware operations to prevent HID access conflicts
    VideoHid& videoHid = VideoHid::getInstance();
    videoHid.stopPollingOnly();
    qDebug() << "Polling stopped before firmware operation";
    
    setupProgressDialog();
    
    // Step 2: Read firmware
    quint32 firmwareSize = VideoHid::getInstance().readFirmwareSize();
    if (firmwareSize == 0) {
        showErrorAndRestart(tr("Firmware Error"), tr("Failed to read firmware size."), tr("firmware size read error"));
        return false;
    }
    
    // Create temporary file path for firmware reading
    QString tempFirmwarePath = QApplication::applicationDirPath() + "/temp_firmware.bin";
    
    // Read firmware using FirmwareReader in a thread
    QThread* readerThread = new QThread();
    FirmwareReader* firmwareReader = new FirmwareReader(&VideoHid::getInstance(), ADDR_EEPROM, firmwareSize, tempFirmwarePath, nullptr);
    firmwareReader->moveToThread(readerThread);
    
    // Track overall progress (reading: 0-30%, processing: 30-40%, writing: 40-100%)
    connect(firmwareReader, &FirmwareReader::progress, this, [this](int percent) {
        if (progressDialog) {
            progressDialog->setValue(percent * 30 / 100); // Scale to 0-30%
        }
    });
    
    // Handle firmware read completion
    connect(firmwareReader, &FirmwareReader::finished, this, [=](bool success) {
        if (!success) {
            showErrorAndRestart(tr("Read Error"), tr("Failed to read firmware from device."), tr("firmware read failure"));
            return;
        }
        
        if (progressDialog) {
            progressDialog->setValue(30); // Reading complete
            progressDialog->setLabelText("Processing EDID settings...");
        }
        
        // Read the firmware file
        QFile firmwareFile(tempFirmwarePath);
        if (!firmwareFile.open(QIODevice::ReadOnly)) {
            showErrorAndRestart(tr("File Error"), tr("Failed to read firmware file."), tr("firmware file read error"));
            return;
        }
        
        QByteArray firmwareData = firmwareFile.readAll();
        firmwareFile.close();
        
        // Process the EDID with new settings
        QByteArray modifiedFirmware = processEDIDDisplaySettings(firmwareData, newName, newSerial);
        if (modifiedFirmware.isEmpty()) {
            showErrorAndRestart(tr("Processing Error"), tr("Failed to process EDID settings."), tr("EDID processing error"));
            return;
        }
        
        if (progressDialog) {
            progressDialog->setValue(40); // Processing complete
            progressDialog->setLabelText("Writing modified firmware...");
        }
        
        startFirmwareWrite(modifiedFirmware, tempFirmwarePath);
        readerThread->quit();
        readerThread->wait();
        readerThread->deleteLater();
    });
    
    connect(firmwareReader, &FirmwareReader::error, this, [=](const QString& errorMessage) {
        showErrorAndRestart(tr("Read Error"), tr("Firmware read failed: %1").arg(errorMessage), tr("firmware read error"));
        readerThread->quit();
        readerThread->wait();
        readerThread->deleteLater();
    });
    
    connect(readerThread, &QThread::started, firmwareReader, &FirmwareReader::process);
    connect(firmwareReader, &FirmwareReader::finished, readerThread, &QThread::quit);
    connect(firmwareReader, &FirmwareReader::finished, firmwareReader, &FirmwareReader::deleteLater);
    connect(readerThread, &QThread::finished, readerThread, &QThread::deleteLater);
    
    readerThread->start();
    
    return true; // Process started successfully
}

void UpdateDisplaySettingsDialog::stopAllDevices()
{
    qDebug() << "Stopping all devices...";
    
    // Stop VideoHid
    VideoHid::getInstance().stop();
    
    // Stop Serial Port Manager
    SerialPortManager::getInstance().stop();
    
    // Get main window to access camera and audio managers
    QWidget *parentWindow = this->parentWidget();
    if (parentWindow) {
        // Try to cast to MainWindow to access device managers
        // This is a simplified approach - in practice you might want a more robust method
        qDebug() << "Main window found, attempting to stop camera and audio managers...";
    } else {
        qDebug() << "Main window not found, continuing with available device shutdowns...";
    }
    
    qDebug() << "All accessible devices stopped.";
}

void UpdateDisplaySettingsDialog::hideMainWindow()
{
    QWidget *mainWindow = this->parentWidget();
    if (mainWindow) {
        mainWindow->hide();
    }
}

quint16 UpdateDisplaySettingsDialog::calculateFirmwareChecksumWithDiff(const QByteArray &originalFirmware, const QByteArray &originalEDID, const QByteArray &modifiedEDID)
{
    return edid::FirmwareUtils::calculateFirmwareChecksumWithDiff(originalFirmware, originalEDID, modifiedEDID);
}

quint16 UpdateDisplaySettingsDialog::calculateFirmwareChecksumWithDiff(const QByteArray &originalFirmware, const QByteArray &modifiedFirmware)
{
    return edid::FirmwareUtils::calculateFirmwareChecksumWithDiff(originalFirmware, modifiedFirmware);
}

QByteArray UpdateDisplaySettingsDialog::processEDIDDisplaySettings(const QByteArray &firmwareData, const QString &newName, const QString &newSerial)
{
    qDebug() << "Processing EDID display settings update...";
    if (!newName.isEmpty()) {
        qDebug() << "  Updating display name to:" << newName;
    }
    if (!newSerial.isEmpty()) {
        qDebug() << "  Updating serial number to:" << newSerial;
    }

    bool hasResolutionUpdate = hasResolutionChanges();
    if (hasResolutionUpdate) {
        qDebug() << "  Updating resolution settings in extension blocks";
    }

    QByteArray modifiedFirmware = firmwareData;

    qDebug() << "=== COMPLETE FIRMWARE BEFORE UPDATE ===";
    qDebug() << "Firmware size:" << firmwareData.size() << "bytes";
    edid::EDIDUtils::showFirmwareHexDump(firmwareData, 0, qMin(256, firmwareData.size()));

    int edidOffset;
    QByteArray edidBlock;
    if (!parseEdidBlock(modifiedFirmware, edidOffset, edidBlock)) {
        qWarning() << "EDID Block 0 not found or incomplete in firmware";
        return QByteArray();
    }

    QByteArray originalEDIDBlock = edidBlock;

    qDebug() << "=== EDID DESCRIPTORS BEFORE UPDATE ===";
    edid::EDIDUtils::showEDIDDescriptors(edidBlock);

    applyEdidUpdates(modifiedFirmware, edidOffset, newName, newSerial);

    qDebug() << "=== EDID DESCRIPTORS AFTER UPDATE ===";
    edid::EDIDUtils::showEDIDDescriptors(modifiedFirmware.mid(edidOffset, 128));

    finalizeEdidBlock(modifiedFirmware, edidOffset, firmwareData, originalEDIDBlock);

    qDebug() << "=== COMPLETE FIRMWARE AFTER UPDATE ===";
    qDebug() << "Modified firmware size:" << modifiedFirmware.size() << "bytes";
    edid::EDIDUtils::showFirmwareHexDump(modifiedFirmware, 0, qMin(256, modifiedFirmware.size()));

    if (modifiedFirmware.size() > 32) {
        qDebug() << "=== FIRMWARE END (last 32 bytes) ===";
        edid::EDIDUtils::showFirmwareHexDump(modifiedFirmware, modifiedFirmware.size() - 32, 32);
    }

    qDebug() << "EDID display settings processing completed successfully";
    return modifiedFirmware;
}


