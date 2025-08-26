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
    titleLabel(nullptr),
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
    m_cleanupInProgress(false)
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
    
    // Create main layout
    mainLayout = new QVBoxLayout(this);
    
    // Display Name Group
    displayNameGroup = new QGroupBox(tr("Display Name"), this);
    displayNameLayout = new QVBoxLayout(displayNameGroup);
    
    displayNameCheckBox = new QCheckBox(tr("Update display name"), this);
    displayNameCheckBox->setChecked(false); // Start unchecked, will be enabled after loading current settings
    displayNameLayout->addWidget(displayNameCheckBox);
    
    displayNameLineEdit = new QLineEdit(this);
    displayNameLineEdit->setPlaceholderText(tr("Loading current display name..."));
    displayNameLineEdit->setEnabled(false); // Will be enabled when checkbox is checked
    displayNameLayout->addWidget(displayNameLineEdit);
    
    mainLayout->addWidget(displayNameGroup);
    
    // Serial Number Group
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
    
    // Resolution Group
    resolutionGroup = new QGroupBox(tr("Extension Block Resolutions"), this);
    QVBoxLayout *resolutionLayout = new QVBoxLayout(resolutionGroup);
    
    // Setup the resolution table
    setupResolutionTable();
    resolutionLayout->addWidget(resolutionTable);
    
    // Control buttons for resolution selection
    QHBoxLayout *resolutionButtonLayout = new QHBoxLayout();
    selectAllButton = new QPushButton(tr("Select All"), this);
    selectNoneButton = new QPushButton(tr("Select None"), this);
    selectDefaultButton = new QPushButton(tr("Select Defaults"), this);
    
    resolutionButtonLayout->addWidget(selectAllButton);
    resolutionButtonLayout->addWidget(selectNoneButton);
    resolutionButtonLayout->addWidget(selectDefaultButton);
    resolutionButtonLayout->addStretch();
    
    resolutionLayout->addLayout(resolutionButtonLayout);
    
    mainLayout->addWidget(resolutionGroup);
    
    // Progress Group (initially hidden)
    progressGroup = new QGroupBox(tr("Reading Firmware"), this);
    QVBoxLayout *progressLayout = new QVBoxLayout(progressGroup);
    
    progressLabel = new QLabel(tr("Reading firmware data..."), this);
    progressLayout->addWidget(progressLabel);
    
    progressBar = new QProgressBar(this);
    progressBar->setRange(0, 100);
    progressBar->setValue(0);
    progressLayout->addWidget(progressBar);
    
    // Cancel reading button
    QHBoxLayout *progressButtonLayout = new QHBoxLayout();
    progressButtonLayout->addStretch();
    cancelReadingButton = new QPushButton(tr("Cancel Reading"), this);
    progressButtonLayout->addWidget(cancelReadingButton);
    progressLayout->addLayout(progressButtonLayout);
    
    // Initially hide the progress group
    progressGroup->setVisible(false);
    mainLayout->addWidget(progressGroup);
    
    // Button layout
    buttonLayout = new QHBoxLayout();
    QSpacerItem *horizontalSpacer = new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);
    buttonLayout->addItem(horizontalSpacer);
    
    updateButton = new QPushButton(tr("Update"), this);
    updateButton->setDefault(true);
    buttonLayout->addWidget(updateButton);
    
    cancelButton = new QPushButton(tr("Cancel"), this);
    buttonLayout->addWidget(cancelButton);
    
    mainLayout->addLayout(buttonLayout);
    
    // Set the layout for the dialog
    setLayout(mainLayout);
    
    // Connect signals to slots
    connect(updateButton, &QPushButton::clicked, this, &UpdateDisplaySettingsDialog::onUpdateButtonClicked);
    connect(cancelButton, &QPushButton::clicked, this, &UpdateDisplaySettingsDialog::onCancelButtonClicked);
    connect(displayNameCheckBox, &QCheckBox::toggled, this, &UpdateDisplaySettingsDialog::onDisplayNameCheckChanged);
    connect(serialNumberCheckBox, &QCheckBox::toggled, this, &UpdateDisplaySettingsDialog::onSerialNumberCheckChanged);
    
    // Resolution table connections
    connect(selectAllButton, &QPushButton::clicked, this, &UpdateDisplaySettingsDialog::onSelectAllResolutions);
    connect(selectNoneButton, &QPushButton::clicked, this, &UpdateDisplaySettingsDialog::onSelectNoneResolutions);
    connect(selectDefaultButton, &QPushButton::clicked, this, &UpdateDisplaySettingsDialog::onSelectDefaultResolutions);
    connect(resolutionTable, &QTableWidget::itemChanged, this, &UpdateDisplaySettingsDialog::onResolutionItemChanged);
    connect(cancelReadingButton, &QPushButton::clicked, this, &UpdateDisplaySettingsDialog::onCancelReadingClicked);
    
    // Connect text change signals to update button state
    connect(displayNameLineEdit, &QLineEdit::textChanged, this, &UpdateDisplaySettingsDialog::enableUpdateButton);
    connect(serialNumberLineEdit, &QLineEdit::textChanged, this, &UpdateDisplaySettingsDialog::enableUpdateButton);
    
    // Update initial state
    enableUpdateButton();
    
    // Set focus to the display name line edit
    displayNameLineEdit->setFocus();
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

void UpdateDisplaySettingsDialog::onUpdateButtonClicked()
{
    QString newName;
    QString newSerial;
    
    // Validate display name if enabled
    if (displayNameCheckBox->isChecked()) {
        newName = displayNameLineEdit->text().trimmed();
        if (newName.isEmpty()) {
            QMessageBox::warning(this, tr("Invalid Input"), tr("Display name cannot be empty when enabled."));
            return;
        }
        
        if (newName.length() > 13) {
            QMessageBox::warning(this, tr("Invalid Input"), tr("Display name cannot exceed 13 characters."));
            return;
        }
        
        // Check for ASCII-only characters
        for (const QChar &ch : newName) {
            if (ch.unicode() > 127) {
                QMessageBox::warning(this, tr("Invalid Input"), tr("Display name must contain only ASCII characters."));
                return;
            }
        }
    }
    
    // Validate serial number if enabled
    if (serialNumberCheckBox->isChecked()) {
        newSerial = serialNumberLineEdit->text().trimmed();
        if (newSerial.isEmpty()) {
            QMessageBox::warning(this, tr("Invalid Input"), tr("Serial number cannot be empty when enabled."));
            return;
        }
        
        if (newSerial.length() > 13) {
            QMessageBox::warning(this, tr("Invalid Input"), tr("Serial number cannot exceed 13 characters."));
            return;
        }
        
        // Check for ASCII-only characters
        for (const QChar &ch : newSerial) {
            if (ch.unicode() > 127) {
                QMessageBox::warning(this, tr("Invalid Input"), tr("Serial number must contain only ASCII characters."));
                return;
            }
        }
    }
    
    // Check that at least one option is enabled
    bool hasNameChange = displayNameCheckBox->isChecked() && !displayNameLineEdit->text().trimmed().isEmpty();
    bool hasSerialChange = serialNumberCheckBox->isChecked() && !serialNumberLineEdit->text().trimmed().isEmpty();
    bool hasResolutionChange = hasResolutionChanges();
    
    if (!hasNameChange && !hasSerialChange && !hasResolutionChange) {
        QMessageBox::warning(this, tr("No Updates Selected"), tr("Please select at least one setting to update."));
        return;
    }
    
    // Show summary of changes to user
    QStringList changesSummary;
    if (hasNameChange) {
        changesSummary << tr("Display Name: %1").arg(newName);
    }
    if (hasSerialChange) {
        changesSummary << tr("Serial Number: %1").arg(newSerial);
    }
    if (hasResolutionChange) {
        QList<ResolutionInfo> selected = getSelectedResolutions();
        changesSummary << tr("Resolution Changes: %1 resolution(s) selected").arg(selected.size());
    }
    
    QString summaryText = tr("The following changes will be applied:\n\n%1\n\nDo you want to continue?").arg(changesSummary.join("\n"));
    
    int reply = QMessageBox::question(this, tr("Confirm Updates"), summaryText, 
                                      QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (reply != QMessageBox::Yes) {
        return;
    }
    
    // Start the update process
    if (updateDisplaySettings(newName, newSerial)) {
        // Success will be handled in the callback
    } else {
        QMessageBox::critical(this, tr("Update Failed"), tr("Failed to start the update process."));
    }
}

void UpdateDisplaySettingsDialog::onCancelButtonClicked()
{
    QDialog::reject();
}

void UpdateDisplaySettingsDialog::onSelectAllResolutions()
{
    for (int row = 0; row < resolutionTable->rowCount(); ++row) {
        QTableWidgetItem* checkItem = resolutionTable->item(row, 0);
        if (checkItem) {
            checkItem->setCheckState(Qt::Checked);
        }
    }
}

void UpdateDisplaySettingsDialog::onSelectNoneResolutions()
{
    for (int row = 0; row < resolutionTable->rowCount(); ++row) {
        QTableWidgetItem* checkItem = resolutionTable->item(row, 0);
        if (checkItem) {
            checkItem->setCheckState(Qt::Unchecked);
        }
    }
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
    
    // Read firmware size
    quint32 firmwareSize = VideoHid::getInstance().readFirmwareSize();
    if (firmwareSize == 0) {
        qWarning() << "Failed to read firmware size, cannot load current EDID settings";
        displayNameLineEdit->setPlaceholderText(tr("Failed to read firmware - enter display name"));
        serialNumberLineEdit->setPlaceholderText(tr("Failed to read firmware - enter serial number"));
        return;
    }
    
    qDebug() << "Firmware size:" << firmwareSize << "bytes";
    
    // Show embedded progress components
    progressGroup->setVisible(true);
    progressBar->setValue(0);
    progressLabel->setText(tr("Reading firmware data..."));
    
    // Disable main dialog controls while reading
    displayNameGroup->setEnabled(false);
    serialNumberGroup->setEnabled(false);
    resolutionGroup->setEnabled(false);
    updateButton->setEnabled(false);
    
    // Create temporary file path for firmware reading
    QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    QString tempFirmwarePath = tempDir + "/temp_firmware_read.bin";
    
    // Create firmware reader thread
    firmwareReaderThread = new QThread(this);
    firmwareReader = new FirmwareReader(&VideoHid::getInstance(), ADDR_EEPROM, firmwareSize, tempFirmwarePath);
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
    
    // Re-enable dialog controls
    displayNameGroup->setEnabled(true);
    serialNumberGroup->setEnabled(true);
    resolutionGroup->setEnabled(true);
    
    if (!success) {
        qWarning() << "Failed to read firmware data, cannot load current EDID settings";
        displayNameLineEdit->setPlaceholderText(tr("Failed to read firmware - enter display name"));
        serialNumberLineEdit->setPlaceholderText(tr("Failed to read firmware - enter serial number"));
        enableUpdateButton();
        return;
    }
    
    // Read the firmware data from the temporary file
    QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    QString tempFirmwarePath = tempDir + "/temp_firmware_read.bin";
    
    QFile file(tempFirmwarePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Failed to open temporary firmware file";
        displayNameLineEdit->setPlaceholderText(tr("Failed to read firmware - enter display name"));
        serialNumberLineEdit->setPlaceholderText(tr("Failed to read firmware - enter serial number"));
        return;
    }
    
    QByteArray firmwareData = file.readAll();
    file.close();
    
    // Clean up temporary file
    QFile::remove(tempFirmwarePath);
    
    if (firmwareData.isEmpty()) {
        qWarning() << "Empty firmware data read from file";
        displayNameLineEdit->setPlaceholderText(tr("Failed to read firmware - enter display name"));
        serialNumberLineEdit->setPlaceholderText(tr("Failed to read firmware - enter serial number"));
        return;
    }
    
    qDebug() << "Successfully read firmware data, size:" << firmwareData.size() << "bytes";
    
    // Find EDID Block 0
    int edidOffset = findEDIDBlock0(firmwareData);
    if (edidOffset == -1) {
        qWarning() << "EDID Block 0 not found in firmware";
        displayNameLineEdit->setPlaceholderText(tr("EDID not found - enter display name"));
        serialNumberLineEdit->setPlaceholderText(tr("EDID not found - enter serial number"));
        return;
    }
    
    // Extract EDID block (128 bytes)
    if (edidOffset + 128 > firmwareData.size()) {
        qWarning() << "Incomplete EDID block in firmware";
        displayNameLineEdit->setPlaceholderText(tr("Invalid EDID - enter display name"));
        serialNumberLineEdit->setPlaceholderText(tr("Invalid EDID - enter serial number"));
        return;
    }
    
    QByteArray edidBlock = firmwareData.mid(edidOffset, 128);
    qDebug() << "Found EDID Block 0 at offset:" << edidOffset;
    
    // Parse current display name and serial number
    QString currentDisplayName;
    QString currentSerialNumber;
    parseEDIDDescriptors(edidBlock, currentDisplayName, currentSerialNumber);
    
    // Set the current values in the line edits
    if (!currentDisplayName.isEmpty()) {
        displayNameLineEdit->setText(currentDisplayName);
        displayNameLineEdit->setPlaceholderText(tr("Enter new display name (max 13 characters)"));
        qDebug() << "Current display name:" << currentDisplayName;
    } else {
        displayNameLineEdit->clear();
        displayNameLineEdit->setPlaceholderText(tr("No display name found - enter new name"));
        qDebug() << "No display name found in EDID";
    }
    
    if (!currentSerialNumber.isEmpty()) {
        serialNumberLineEdit->setText(currentSerialNumber);
        serialNumberLineEdit->setPlaceholderText(tr("Enter new serial number (max 13 characters)"));
        qDebug() << "Current serial number:" << currentSerialNumber;
    } else {
        serialNumberLineEdit->clear();
        serialNumberLineEdit->setPlaceholderText(tr("No serial number found - enter new serial"));
        qDebug() << "No serial number found in EDID";
    }
    
    // Log supported resolutions
    logSupportedResolutions(edidBlock);
    
    // Parse extension blocks for additional resolution information
    parseEDIDExtensionBlocks(firmwareData, edidOffset);
    
    // Update resolution table with all found resolutions
    updateResolutionTableFromEDID(edidBlock, firmwareData, edidOffset);
    
    // Show EDID descriptors for debugging
    qDebug() << "=== CURRENT EDID DESCRIPTORS ===";
    showEDIDDescriptors(edidBlock);
    
    // Update button state after loading is complete
    enableUpdateButton();
    
    // Clean up the firmware reader thread now that we're done
    // Use a timer to ensure this happens after all signals are processed
    QTimer::singleShot(0, this, [this]() {
        cleanupFirmwareReaderThread();
    });
}

void UpdateDisplaySettingsDialog::onFirmwareReadError(const QString& errorMessage)
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
    
    // Re-enable dialog controls
    displayNameGroup->setEnabled(true);
    serialNumberGroup->setEnabled(true);
    resolutionGroup->setEnabled(true);
    
    qWarning() << "Firmware read error:" << errorMessage;
    displayNameLineEdit->setPlaceholderText(tr("Error reading firmware - enter display name"));
    serialNumberLineEdit->setPlaceholderText(tr("Error reading firmware - enter serial number"));
    
    // Update button state
    enableUpdateButton();
    
    QMessageBox::warning(this, tr("Firmware Read Error"), 
                        tr("Failed to read firmware: %1").arg(errorMessage));
    
    // Clean up the firmware reader thread
    QTimer::singleShot(0, this, [this]() {
        cleanupFirmwareReaderThread();
    });
}

void UpdateDisplaySettingsDialog::onCancelReadingClicked()
{
    qDebug() << "User cancelled firmware reading";
    
    // Request thread interruption immediately
    if (firmwareReaderThread && firmwareReaderThread->isRunning()) {
        qDebug() << "Requesting thread interruption";
        firmwareReaderThread->requestInterruption();
        
        // Also request quit signal
        firmwareReaderThread->quit();
    }
    
    // Hide progress components immediately
    if (progressGroup) {
        progressGroup->setVisible(false);
    }
    
    // Clean up old progress dialog if it exists
    if (progressDialog) {
        progressDialog->cancel();
        progressDialog->close();
        progressDialog->deleteLater();
        progressDialog = nullptr;
    }
    
    // Re-enable dialog controls
    displayNameGroup->setEnabled(true);
    serialNumberGroup->setEnabled(true);
    resolutionGroup->setEnabled(true);
    
    // Update placeholders to indicate cancellation
    displayNameLineEdit->setPlaceholderText(tr("Reading cancelled - enter display name"));
    serialNumberLineEdit->setPlaceholderText(tr("Reading cancelled - enter serial number"));
    
    // Update button state
    enableUpdateButton();
    
    // Schedule thread cleanup for later (non-blocking)
    QTimer::singleShot(1000, this, [this]() {
        if (firmwareReaderThread && firmwareReaderThread->isRunning()) {
            qWarning() << "Thread still running after 1 second, attempting termination";
            firmwareReaderThread->terminate();
            // Don't wait here - let the destructor handle final cleanup
        }
    });
}

void UpdateDisplaySettingsDialog::parseEDIDDescriptors(const QByteArray &edidBlock, QString &displayName, QString &serialNumber)
{
    if (edidBlock.size() != 128) {
        qWarning() << "Invalid EDID block size:" << edidBlock.size();
        return;
    }
    
    displayName.clear();
    serialNumber.clear();
    
    // Parse all 4 descriptors (offsets: 54, 72, 90, 108)
    for (int descriptorOffset = 54; descriptorOffset <= 54 + 3 * 18; descriptorOffset += 18) {
        if (descriptorOffset + 18 > edidBlock.size()) break;
        
        // Check if this is a text descriptor (non-timing descriptor)
        if (edidBlock[descriptorOffset] == 0x00 && 
            edidBlock[descriptorOffset + 1] == 0x00 && 
            edidBlock[descriptorOffset + 2] == 0x00) {
            
            quint8 descriptorType = static_cast<quint8>(edidBlock[descriptorOffset + 3]);
            
            if (descriptorType == 0xFC) { // Display Product Name
                QByteArray nameBytes = edidBlock.mid(descriptorOffset + 5, 13);
                for (int i = 0; i < nameBytes.size(); ++i) {
                    char c = nameBytes[i];
                    if (c == 0x0A) break; // Line feed terminator
                    if (c >= 32 && c <= 126) { // Printable ASCII
                        displayName += c;
                    }
                }
                displayName = displayName.trimmed();
                
            } else if (descriptorType == 0xFF) { // Display Serial Number
                QByteArray serialBytes = edidBlock.mid(descriptorOffset + 5, 13);
                for (int i = 0; i < serialBytes.size(); ++i) {
                    char c = serialBytes[i];
                    if (c == 0x0A) break; // Line feed terminator
                    if (c >= 32 && c <= 126) { // Printable ASCII
                        serialNumber += c;
                    }
                }
                serialNumber = serialNumber.trimmed();
            }
        }
    }
}

void UpdateDisplaySettingsDialog::logSupportedResolutions(const QByteArray &edidBlock)
{
    if (edidBlock.size() != 128) {
        qWarning() << "Invalid EDID block size for resolution parsing:" << edidBlock.size();
        return;
    }
    
    qDebug() << "=== SUPPORTED RESOLUTIONS FROM EDID ===";
    
    // Check if there are extension blocks
    quint8 extensionCount = static_cast<quint8>(edidBlock[126]);
    qDebug() << "EDID Extension blocks count:" << extensionCount;
    
    if (extensionCount > 0) {
        qDebug() << "";
        qDebug() << "This EDID has" << extensionCount << "extension block(s).";
        qDebug() << "Modern resolution information is in the extension blocks (CEA-861, etc.)";
        qDebug() << "which contain detailed timing descriptors and VIC codes for current resolutions.";
        qDebug() << "Standard timings in Block 0 are often legacy and may not reflect actual capabilities.";
        qDebug() << "";
    } else {
        qDebug() << "No extension blocks found - this may be a basic/legacy EDID.";
    }
    
    // Skip detailed standard timing analysis - just note them briefly
    qDebug() << "Standard Timings (bytes 35-42): [Skipping detailed analysis - focusing on extension blocks]";
    
    // Parse detailed timing descriptors from Block 0 (briefly)
    qDebug() << "Detailed Timing Descriptors (Block 0): [May contain some legacy timings]";
    for (int descriptorOffset = 54; descriptorOffset <= 54 + 3 * 18; descriptorOffset += 18) {
        if (descriptorOffset + 18 > edidBlock.size()) break;
        
        // Check if this is a detailed timing descriptor (not a text descriptor)
        if (!(edidBlock[descriptorOffset] == 0x00 && 
              edidBlock[descriptorOffset + 1] == 0x00 && 
              edidBlock[descriptorOffset + 2] == 0x00)) {
            
            // Parse detailed timing
            quint16 pixelClock = static_cast<quint16>(edidBlock[descriptorOffset]) | 
                               (static_cast<quint16>(edidBlock[descriptorOffset + 1]) << 8);
            
            if (pixelClock > 0) {
                quint16 hActive = static_cast<quint16>(edidBlock[descriptorOffset + 2]) | 
                                ((static_cast<quint16>(edidBlock[descriptorOffset + 4]) & 0xF0) << 4);
                
                quint16 vActive = static_cast<quint16>(edidBlock[descriptorOffset + 5]) | 
                                ((static_cast<quint16>(edidBlock[descriptorOffset + 7]) & 0xF0) << 4);
                
                double pixelClockMHz = pixelClock / 100.0;
                qDebug() << "  " << hActive << "x" << vActive << "@ pixel clock" << pixelClockMHz << "MHz";
            }
        }
    }
    
    // Note about extension blocks
    if (extensionCount > 0) {
        qDebug() << "";
        qDebug() << "=> FOCUS: Extension blocks contain the actual supported resolutions.";
        qDebug() << "=> Resolution table will show VIC codes and detailed timings from extension blocks.";
        qDebug() << "=> Standard timings above are often legacy and may not reflect true capabilities.";
    } else {
        qDebug() << "";
        qDebug() << "=> WARNING: No extension blocks found. This may be a basic/legacy display.";
        qDebug() << "=> Modern displays typically use extension blocks for resolution information.";
    }
    
    qDebug() << "=== END SUPPORTED RESOLUTIONS ===";
}

void UpdateDisplaySettingsDialog::parseEDIDExtensionBlocks(const QByteArray &firmwareData, int baseBlockOffset)
{
    if (baseBlockOffset < 0 || baseBlockOffset + 128 > firmwareData.size()) {
        qWarning() << "Invalid base block offset for extension parsing:" << baseBlockOffset;
        return;
    }
    
    // Get extension count from Block 0, byte 126
    quint8 extensionCount = static_cast<quint8>(firmwareData[baseBlockOffset + 126]);
    
    if (extensionCount == 0) {
        qDebug() << "No EDID extension blocks found";
        return;
    }
    
    qDebug() << "=== PARSING EDID EXTENSION BLOCKS ===";
    qDebug() << "Extension count:" << extensionCount;
    
    for (int blockIndex = 1; blockIndex <= extensionCount; ++blockIndex) {
        int blockOffset = baseBlockOffset + (blockIndex * 128);
        
        // Check if block exists in firmware
        if (blockOffset + 128 > firmwareData.size()) {
            qWarning() << "Extension Block" << blockIndex << "not found in firmware (offset" << blockOffset << ")";
            continue;
        }
        
        QByteArray extensionBlock = firmwareData.mid(blockOffset, 128);
        quint8 extensionTag = static_cast<quint8>(extensionBlock[0]);
        
        qDebug() << "";
        qDebug() << "=== EXTENSION BLOCK" << blockIndex << "===";
        qDebug() << "Block offset:" << blockOffset;
        qDebug() << "Extension tag: 0x" << QString::number(extensionTag, 16).toUpper().rightJustified(2, '0');
        
        switch (extensionTag) {
            case 0x02: // CEA-861 Extension Block
                qDebug() << "Type: CEA-861 Extension Block";
                parseCEA861ExtensionBlock(extensionBlock, blockIndex);
                break;
            case 0x10: // Video Timing Extension Block  
                qDebug() << "Type: Video Timing Extension Block";
                parseVideoTimingExtensionBlock(extensionBlock, blockIndex);
                break;
            case 0x20: // EDID 2.0 Extension Block
                qDebug() << "Type: EDID 2.0 Extension Block";
                break;
            case 0x30: // Color Information Extension Block
                qDebug() << "Type: Color Information Extension Block";
                break;
            case 0x40: // DVI Feature Extension Block
                qDebug() << "Type: DVI Feature Extension Block";
                break;
            case 0x50: // Touch Screen Extension Block
                qDebug() << "Type: Touch Screen Extension Block";
                break;
            case 0x60: // Block Map Extension Block
                qDebug() << "Type: Block Map Extension Block";
                break;
            case 0x70: // Display Device Data Extension Block
                qDebug() << "Type: Display Device Data Extension Block";
                break;
            case 0xF0: // Block Map Extension Block
                qDebug() << "Type: Block Map Extension Block (alternate)";
                break;
            default:
                qDebug() << "Type: Unknown/Proprietary Extension Block";
                break;
        }
        
        // Show hex dump of first 32 bytes for debugging
        qDebug() << "First 32 bytes:";
        QString hexDump;
        for (int i = 0; i < qMin(32, extensionBlock.size()); ++i) {
            hexDump += QString("%1 ").arg(static_cast<quint8>(extensionBlock[i]), 2, 16, QChar('0')).toUpper();
        }
        qDebug() << hexDump;
    }
    
    qDebug() << "=== END EXTENSION BLOCKS ===";
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
    // Clear existing resolutions
    availableResolutions.clear();
    resolutionTable->setRowCount(0);
    
    // Skip standard timings - only parse extension blocks for resolutions
    // parseStandardTimingsForResolutions(edidBlock);
    
    // Skip detailed timing descriptors from Block 0 - focus on extension blocks
    // parseDetailedTimingDescriptorsForResolutions(edidBlock);
    
    // Parse extension blocks for additional resolutions (this is where modern resolutions are)
    parseExtensionBlocksForResolutions(firmwareData, baseOffset);
    
    // Populate the table with all found resolutions
    populateResolutionTable();
}

void UpdateDisplaySettingsDialog::parseStandardTimingsForResolutions(const QByteArray &edidBlock)
{
    qDebug() << "Parsing standard timings for resolution table...";
    
    // Parse standard timings (bytes 35-42)
    for (int i = 35; i <= 42; i += 2) {
        if (i + 1 >= edidBlock.size()) break;
        
        quint8 byte1 = static_cast<quint8>(edidBlock[i]);
        quint8 byte2 = static_cast<quint8>(edidBlock[i + 1]);
        
        // Skip unused timings
        if ((byte1 == 0x01 && byte2 == 0x01) || 
            (byte1 == 0x00 && byte2 == 0x00) ||
            (byte1 == 0xFF && byte2 == 0xFF)) {
            continue;
        }
        
        // Calculate resolution
        int horizontalRes = (byte1 + 31) * 8;
        int aspectRatio = (byte2 >> 6) & 0x03;
        int refreshRate = (byte2 & 0x3F) + 60;
        
        int verticalRes = 0;
        switch (aspectRatio) {
            case 0: verticalRes = (horizontalRes * 10) / 16; break; // 16:10
            case 1: verticalRes = (horizontalRes * 3) / 4; break;   // 4:3
            case 2: verticalRes = (horizontalRes * 4) / 5; break;   // 5:4
            case 3: verticalRes = (horizontalRes * 9) / 16; break;  // 16:9
        }
        
        // Only add valid resolutions
        if (horizontalRes >= 640 && horizontalRes <= 8192 && 
            verticalRes >= 480 && verticalRes <= 8192 && 
            refreshRate >= 60 && refreshRate <= 200) {
            
            QString description = QString("%1x%2 @ %3Hz").arg(horizontalRes).arg(verticalRes).arg(refreshRate);
            addResolutionToList(description, horizontalRes, verticalRes, refreshRate, 0, true, true);
        }
    }
}

void UpdateDisplaySettingsDialog::parseDetailedTimingDescriptorsForResolutions(const QByteArray &edidBlock)
{
    qDebug() << "Parsing detailed timing descriptors for resolution table...";
    
    // Parse detailed timing descriptors (offsets: 54, 72, 90, 108)
    for (int descriptorOffset = 54; descriptorOffset <= 54 + 3 * 18; descriptorOffset += 18) {
        if (descriptorOffset + 18 > edidBlock.size()) break;
        
        // Check if this is a detailed timing descriptor (not a text descriptor)
        if (!(edidBlock[descriptorOffset] == 0x00 && 
              edidBlock[descriptorOffset + 1] == 0x00 && 
              edidBlock[descriptorOffset + 2] == 0x00)) {
            
            // Parse detailed timing
            quint16 pixelClock = static_cast<quint16>(edidBlock[descriptorOffset]) | 
                               (static_cast<quint16>(edidBlock[descriptorOffset + 1]) << 8);
            
            if (pixelClock > 0) {
                quint16 hActive = static_cast<quint16>(edidBlock[descriptorOffset + 2]) | 
                                ((static_cast<quint16>(edidBlock[descriptorOffset + 4]) & 0xF0) << 4);
                
                quint16 hBlank = static_cast<quint16>(edidBlock[descriptorOffset + 3]) | 
                               ((static_cast<quint16>(edidBlock[descriptorOffset + 4]) & 0x0F) << 8);
                
                quint16 vActive = static_cast<quint16>(edidBlock[descriptorOffset + 5]) | 
                                ((static_cast<quint16>(edidBlock[descriptorOffset + 7]) & 0xF0) << 4);
                
                quint16 vBlank = static_cast<quint16>(edidBlock[descriptorOffset + 6]) | 
                               ((static_cast<quint16>(edidBlock[descriptorOffset + 7]) & 0x0F) << 8);
                
                double pixelClockMHz = pixelClock / 100.0;
                double hTotal = hActive + hBlank;
                double vTotal = vActive + vBlank;
                double refreshRate = (pixelClockMHz * 1000000.0) / (hTotal * vTotal);
                
                if (hActive >= 640 && vActive >= 480 && refreshRate >= 30 && refreshRate <= 200) {
                    QString description = QString("%1x%2 @ %3Hz").arg(hActive).arg(vActive).arg(refreshRate, 0, 'f', 1);
                    addResolutionToList(description, hActive, vActive, qRound(refreshRate), 0, false, true);
                }
            }
        }
    }
}

void UpdateDisplaySettingsDialog::parseExtensionBlocksForResolutions(const QByteArray &firmwareData, int baseOffset)
{
    qDebug() << "Parsing extension blocks for resolution table...";
    
    // Get extension count from Block 0
    if (baseOffset + 126 >= firmwareData.size()) return;
    
    quint8 extensionCount = static_cast<quint8>(firmwareData[baseOffset + 126]);
    if (extensionCount == 0) return;
    
    for (int blockIndex = 1; blockIndex <= extensionCount; ++blockIndex) {
        int blockOffset = baseOffset + (blockIndex * 128);
        
        if (blockOffset + 128 > firmwareData.size()) continue;
        
        QByteArray extensionBlock = firmwareData.mid(blockOffset, 128);
        quint8 extensionTag = static_cast<quint8>(extensionBlock[0]);
        
        if (extensionTag == 0x02) { // CEA-861 Extension Block
            parseCEA861ExtensionBlockForResolutions(extensionBlock, blockIndex);
        }
    }
}

void UpdateDisplaySettingsDialog::parseCEA861ExtensionBlockForResolutions(const QByteArray &block, int blockNumber)
{
    if (block.size() != 128) return;
    
    quint8 dtdOffset = static_cast<quint8>(block[2]);
    
    // Parse detailed timing descriptors in the extension block
    if (dtdOffset >= 4 && dtdOffset < 128) {
        for (int dtdIndex = dtdOffset; dtdIndex <= 128 - 18; dtdIndex += 18) {
            QByteArray dtd = block.mid(dtdIndex, 18);
            
            quint16 pixelClock = static_cast<quint16>(dtd[0]) | (static_cast<quint16>(dtd[1]) << 8);
            
            if (pixelClock > 0) {
                quint16 hActive = static_cast<quint16>(dtd[2]) | ((static_cast<quint16>(dtd[4]) & 0xF0) << 4);
                quint16 hBlank = static_cast<quint16>(dtd[3]) | ((static_cast<quint16>(dtd[4]) & 0x0F) << 8);
                quint16 vActive = static_cast<quint16>(dtd[5]) | ((static_cast<quint16>(dtd[7]) & 0xF0) << 4);
                quint16 vBlank = static_cast<quint16>(dtd[6]) | ((static_cast<quint16>(dtd[7]) & 0x0F) << 8);
                
                double pixelClockMHz = pixelClock / 100.0;
                double hTotal = hActive + hBlank;
                double vTotal = vActive + vBlank;
                double refreshRate = (pixelClockMHz * 1000000.0) / (hTotal * vTotal);
                
                if (hActive >= 640 && vActive >= 480 && refreshRate >= 30 && refreshRate <= 200) {
                    QString description = QString("%1x%2 @ %3Hz (CEA-861)").arg(hActive).arg(vActive).arg(refreshRate, 0, 'f', 1);
                    addResolutionToList(description, hActive, vActive, qRound(refreshRate), 0, false, true);
                }
            }
        }
    }
    
    // Parse Video Data Block for VIC codes
    if (dtdOffset > 4) {
        parseVideoDataBlockForResolutions(block.mid(4, dtdOffset - 4));
    }
}

void UpdateDisplaySettingsDialog::parseVideoDataBlockForResolutions(const QByteArray &dataBlockCollection)
{
    int offset = 0;
    while (offset < dataBlockCollection.size()) {
        if (offset >= dataBlockCollection.size()) break;
        
        quint8 header = static_cast<quint8>(dataBlockCollection[offset]);
        quint8 tag = (header >> 5) & 0x07;
        quint8 length = header & 0x1F;
        
        if (tag == 2) { // Video Data Block
            for (int i = 1; i <= length && offset + i < dataBlockCollection.size(); ++i) {
                quint8 vic = static_cast<quint8>(dataBlockCollection[offset + i]) & 0x7F;
                bool isNative = (static_cast<quint8>(dataBlockCollection[offset + i]) & 0x80) != 0;
                
                auto resolution = getVICResolutionInfo(vic);
                if (resolution.width > 0 && resolution.height > 0) {
                    QString description = QString("%1x%2 @ %3Hz (VIC %4%5)")
                                        .arg(resolution.width).arg(resolution.height)
                                        .arg(resolution.refreshRate).arg(vic)
                                        .arg(isNative ? ", Native" : "");
                    addResolutionToList(description, resolution.width, resolution.height, 
                                      resolution.refreshRate, vic, false, true);
                }
            }
        }
        
        offset += length + 1;
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
    // Check if any resolution's current selection differs from its original enabled state
    for (const auto& resolution : availableResolutions) {
        if (resolution.userSelected != resolution.isEnabled) {
            return true;
        }
    }
    return false;
}

ResolutionInfo UpdateDisplaySettingsDialog::getVICResolutionInfo(quint8 vic)
{
    // Common VIC codes from CEA-861 standard
    switch (vic) {
        case 1: return ResolutionInfo("640x480 @ 60Hz", 640, 480, 60, vic);
        case 2: return ResolutionInfo("720x480 @ 60Hz", 720, 480, 60, vic);
        case 3: return ResolutionInfo("720x480 @ 60Hz", 720, 480, 60, vic);
        case 4: return ResolutionInfo("1280x720 @ 60Hz", 1280, 720, 60, vic);
        case 5: return ResolutionInfo("1920x1080i @ 60Hz", 1920, 1080, 60, vic);
        case 6: return ResolutionInfo("1440x480i @ 60Hz", 1440, 480, 60, vic);
        case 7: return ResolutionInfo("1440x480i @ 60Hz", 1440, 480, 60, vic);
        case 16: return ResolutionInfo("1920x1080 @ 60Hz", 1920, 1080, 60, vic);
        case 17: return ResolutionInfo("720x576 @ 50Hz", 720, 576, 50, vic);
        case 18: return ResolutionInfo("720x576 @ 50Hz", 720, 576, 50, vic);
        case 19: return ResolutionInfo("1280x720 @ 50Hz", 1280, 720, 50, vic);
        case 20: return ResolutionInfo("1920x1080i @ 50Hz", 1920, 1080, 50, vic);
        case 31: return ResolutionInfo("1920x1080 @ 50Hz", 1920, 1080, 50, vic);
        case 32: return ResolutionInfo("1920x1080 @ 24Hz", 1920, 1080, 24, vic);
        case 33: return ResolutionInfo("1920x1080 @ 25Hz", 1920, 1080, 25, vic);
        case 34: return ResolutionInfo("1920x1080 @ 30Hz", 1920, 1080, 30, vic);
        case 39: return ResolutionInfo("1920x1080i @ 50Hz", 1920, 1080, 50, vic);
        case 40: return ResolutionInfo("1920x1080i @ 100Hz", 1920, 1080, 100, vic);
        case 41: return ResolutionInfo("1280x720 @ 100Hz", 1280, 720, 100, vic);
        case 42: return ResolutionInfo("720x576 @ 100Hz", 720, 576, 100, vic);
        case 43: return ResolutionInfo("720x576 @ 100Hz", 720, 576, 100, vic);
        case 44: return ResolutionInfo("1440x576i @ 100Hz", 1440, 576, 100, vic);
        case 45: return ResolutionInfo("1440x576i @ 100Hz", 1440, 576, 100, vic);
        case 46: return ResolutionInfo("1920x1080i @ 120Hz", 1920, 1080, 120, vic);
        case 47: return ResolutionInfo("1280x720 @ 120Hz", 1280, 720, 120, vic);
        case 48: return ResolutionInfo("720x480 @ 120Hz", 720, 480, 120, vic);
        case 49: return ResolutionInfo("720x480 @ 120Hz", 720, 480, 120, vic);
        case 50: return ResolutionInfo("1440x480i @ 120Hz", 1440, 480, 120, vic);
        case 51: return ResolutionInfo("1440x480i @ 120Hz", 1440, 480, 120, vic);
        case 60: return ResolutionInfo("1280x720 @ 24Hz", 1280, 720, 24, vic);
        case 61: return ResolutionInfo("1280x720 @ 25Hz", 1280, 720, 25, vic);
        case 62: return ResolutionInfo("1280x720 @ 30Hz", 1280, 720, 30, vic);
        case 63: return ResolutionInfo("1920x1080 @ 120Hz", 1920, 1080, 120, vic);
        case 64: return ResolutionInfo("1920x1080 @ 100Hz", 1920, 1080, 100, vic);
        case 93: return ResolutionInfo("3840x2160 @ 24Hz", 3840, 2160, 24, vic);
        case 94: return ResolutionInfo("3840x2160 @ 25Hz", 3840, 2160, 25, vic);
        case 95: return ResolutionInfo("3840x2160 @ 30Hz", 3840, 2160, 30, vic);
        case 96: return ResolutionInfo("3840x2160 @ 50Hz", 3840, 2160, 50, vic);
        case 97: return ResolutionInfo("3840x2160 @ 60Hz", 3840, 2160, 60, vic);
        case 98: return ResolutionInfo("4096x2160 @ 24Hz", 4096, 2160, 24, vic);
        case 99: return ResolutionInfo("4096x2160 @ 25Hz", 4096, 2160, 25, vic);
        case 100: return ResolutionInfo("4096x2160 @ 30Hz", 4096, 2160, 30, vic);
        case 101: return ResolutionInfo("4096x2160 @ 50Hz", 4096, 2160, 50, vic);
        case 102: return ResolutionInfo("4096x2160 @ 60Hz", 4096, 2160, 60, vic);
        default: 
            return ResolutionInfo(QString("Unknown VIC %1").arg(vic), 0, 0, 0, vic);
    }
}

void UpdateDisplaySettingsDialog::parseCEA861ExtensionBlock(const QByteArray &block, int blockNumber)
{
    if (block.size() != 128) {
        qWarning() << "Invalid CEA-861 block size:" << block.size();
        return;
    }
    
    quint8 revision = static_cast<quint8>(block[1]);
    quint8 dtdOffset = static_cast<quint8>(block[2]);
    quint8 flags = static_cast<quint8>(block[3]);
    
    qDebug() << "CEA-861 Revision:" << revision;
    qDebug() << "DTD offset:" << dtdOffset;
    qDebug() << "Flags: 0x" << QString::number(flags, 16).toUpper().rightJustified(2, '0');
    
    bool hasUnderscan = (flags & 0x80) != 0;
    bool hasBasicAudio = (flags & 0x40) != 0;
    bool hasYCC444 = (flags & 0x20) != 0;
    bool hasYCC422 = (flags & 0x10) != 0;
    
    qDebug() << "Capabilities:";
    qDebug() << "  Underscan support:" << (hasUnderscan ? "Yes" : "No");
    qDebug() << "  Basic audio support:" << (hasBasicAudio ? "Yes" : "No");
    qDebug() << "  YCC 4:4:4 support:" << (hasYCC444 ? "Yes" : "No");
    qDebug() << "  YCC 4:2:2 support:" << (hasYCC422 ? "Yes" : "No");
    
    // Parse detailed timing descriptors (if any)
    if (dtdOffset >= 4 && dtdOffset < 128) {
        qDebug() << "Detailed Timing Descriptors (CEA-861):";
        
        for (int dtdIndex = dtdOffset; dtdIndex <= 128 - 18; dtdIndex += 18) {
            QByteArray dtd = block.mid(dtdIndex, 18);
            
            // Check if it's a valid DTD (non-zero pixel clock)
            quint16 pixelClock = static_cast<quint16>(dtd[0]) | (static_cast<quint16>(dtd[1]) << 8);
            
            if (pixelClock > 0) {
                quint16 hActive = static_cast<quint16>(dtd[2]) | ((static_cast<quint16>(dtd[4]) & 0xF0) << 4);
                quint16 hBlank = static_cast<quint16>(dtd[3]) | ((static_cast<quint16>(dtd[4]) & 0x0F) << 8);
                quint16 vActive = static_cast<quint16>(dtd[5]) | ((static_cast<quint16>(dtd[7]) & 0xF0) << 4);
                quint16 vBlank = static_cast<quint16>(dtd[6]) | ((static_cast<quint16>(dtd[7]) & 0x0F) << 8);
                
                quint16 hSyncOffset = static_cast<quint16>(dtd[8]) | ((static_cast<quint16>(dtd[11]) & 0xC0) << 2);
                quint16 hSyncWidth = static_cast<quint16>(dtd[9]) | ((static_cast<quint16>(dtd[11]) & 0x30) << 4);
                quint16 vSyncOffset = ((static_cast<quint16>(dtd[10]) & 0xF0) >> 4) | ((static_cast<quint16>(dtd[11]) & 0x0C) << 2);
                quint16 vSyncWidth = (static_cast<quint16>(dtd[10]) & 0x0F) | ((static_cast<quint16>(dtd[11]) & 0x03) << 4);
                
                double pixelClockMHz = pixelClock / 100.0;
                double hTotal = hActive + hBlank;
                double vTotal = vActive + vBlank;
                double refreshRate = (pixelClockMHz * 1000000.0) / (hTotal * vTotal);
                
                qDebug() << QString("  %1 x %2 @ %3 Hz (pixel clock: %4 MHz)")
                            .arg(hActive).arg(vActive).arg(refreshRate, 0, 'f', 2).arg(pixelClockMHz, 0, 'f', 2);
                
                qDebug() << QString("    H: %1/%2/%3/%4, V: %5/%6/%7/%8")
                            .arg(hActive).arg(hSyncOffset).arg(hSyncWidth).arg(hTotal)
                            .arg(vActive).arg(vSyncOffset).arg(vSyncWidth).arg(vTotal);
            }
        }
    }
    
    // Parse Data Block Collection (if any)
    if (dtdOffset > 4) {
        qDebug() << "Data Block Collection:";
        
        for (int offset = 4; offset < dtdOffset; ) {
            if (offset >= block.size()) break;
            
            quint8 header = static_cast<quint8>(block[offset]);
            quint8 tag = (header >> 5) & 0x07;
            quint8 length = header & 0x1F;
            
            if (offset + 1 + length > dtdOffset || offset + 1 + length > block.size()) {
                qWarning() << "Invalid data block at offset" << offset;
                break;
            }
            
            switch (tag) {
                case 1: // Audio Data Block
                    qDebug() << "  Audio Data Block (length:" << length << ")";
                    break;
                case 2: // Video Data Block
                    qDebug() << "  Video Data Block (length:" << length << ")";
                    parseVideoDataBlock(block.mid(offset + 1, length));
                    break;
                case 3: // Vendor Specific Data Block
                    qDebug() << "  Vendor Specific Data Block (length:" << length << ")";
                    break;
                case 4: // Speaker Allocation Data Block
                    qDebug() << "  Speaker Allocation Data Block (length:" << length << ")";
                    break;
                case 7: // Extended Tag
                    qDebug() << "  Extended Tag Data Block (length:" << length << ")";
                    break;
                default:
                    qDebug() << "  Unknown Data Block (tag:" << tag << ", length:" << length << ")";
                    break;
            }
            
            offset += 1 + length;
        }
    }
}

void UpdateDisplaySettingsDialog::parseVideoTimingExtensionBlock(const QByteArray &block, int blockNumber)
{
    if (block.size() != 128) {
        qWarning() << "Invalid Video Timing Extension block size:" << block.size();
        return;
    }
    
    qDebug() << "Video Timing Extension Block parsing not fully implemented";
    qDebug() << "This block contains additional timing information";
}

void UpdateDisplaySettingsDialog::parseVideoDataBlock(const QByteArray &vdbData)
{
    qDebug() << "    Video Data Block contains" << vdbData.size() << "Short Video Descriptors:";
    
    for (int i = 0; i < vdbData.size(); ++i) {
        quint8 svd = static_cast<quint8>(vdbData[i]);
        quint8 vic = svd & 0x7F;
        bool isNative = (svd & 0x80) != 0;
        
        QString resolutionInfo = getVICResolution(vic);
        qDebug() << QString("      VIC %1: %2%3")
                    .arg(vic)
                    .arg(resolutionInfo)
                    .arg(isNative ? " (Native)" : "");
    }
}

QString UpdateDisplaySettingsDialog::getVICResolution(quint8 vic)
{
    // Common VIC (Video Identification Code) to resolution mapping
    // Based on CEA-861 standard
    switch (vic) {
        case 1: return "640x480p @ 59.94/60Hz";
        case 2: return "720x480p @ 59.94/60Hz";
        case 3: return "720x480p @ 59.94/60Hz";
        case 4: return "1280x720p @ 59.94/60Hz";
        case 5: return "1920x1080i @ 59.94/60Hz";
        case 6: return "720x480i @ 59.94/60Hz";
        case 7: return "720x480i @ 59.94/60Hz";
        case 16: return "1920x1080p @ 59.94/60Hz";
        case 17: return "720x576p @ 50Hz";
        case 18: return "720x576p @ 50Hz";
        case 19: return "1280x720p @ 50Hz";
        case 20: return "1920x1080i @ 50Hz";
        case 31: return "1920x1080p @ 50Hz";
        case 32: return "1920x1080p @ 23.98/24Hz";
        case 33: return "1920x1080p @ 25Hz";
        case 34: return "1920x1080p @ 29.97/30Hz";
        case 39: return "1920x1080i @ 50Hz";
        case 40: return "1920x1080i @ 100Hz";
        case 41: return "1280x720p @ 100Hz";
        case 42: return "720x576p @ 100Hz";
        case 43: return "720x576p @ 100Hz";
        case 44: return "720x576i @ 100Hz";
        case 45: return "720x576i @ 100Hz";
        case 46: return "1920x1080i @ 119.88/120Hz";
        case 47: return "1280x720p @ 119.88/120Hz";
        case 48: return "720x480p @ 119.88/120Hz";
        case 49: return "720x480p @ 119.88/120Hz";
        case 50: return "720x480i @ 119.88/120Hz";
        case 51: return "720x480i @ 119.88/120Hz";
        case 60: return "1280x720p @ 23.98/24Hz";
        case 61: return "1280x720p @ 25Hz";
        case 62: return "1280x720p @ 29.97/30Hz";
        case 63: return "1920x1080p @ 119.88/120Hz";
        case 64: return "1920x1080p @ 100Hz";
        case 93: return "3840x2160p @ 23.98/24Hz";
        case 94: return "3840x2160p @ 25Hz";
        case 95: return "3840x2160p @ 29.97/30Hz";
        case 96: return "3840x2160p @ 50Hz";
        case 97: return "3840x2160p @ 59.94/60Hz";
        case 98: return "4096x2160p @ 23.98/24Hz";
        case 99: return "4096x2160p @ 25Hz";
        case 100: return "4096x2160p @ 29.97/30Hz";
        case 101: return "4096x2160p @ 50Hz";
        case 102: return "4096x2160p @ 59.94/60Hz";
        default: return QString("Unknown VIC %1").arg(vic);
    }
}

QString UpdateDisplaySettingsDialog::getCurrentDisplayName()
{
    // This method is now called by loadCurrentEDIDSettings()
    // Return empty string as the actual value is loaded asynchronously
    return QString();
}

QString UpdateDisplaySettingsDialog::getCurrentSerialNumber()
{
    // This method is now called by loadCurrentEDIDSettings()
    // Return empty string as the actual value is loaded asynchronously
    return QString();
}

void UpdateDisplaySettingsDialog::applyResolutionChangesToEDID(QByteArray &edidBlock, const QByteArray &firmwareData)
{
    qDebug() << "=== APPLYING RESOLUTION CHANGES TO EDID ===";
    qDebug() << "Note: This method is deprecated. Use updateExtensionBlockResolutions instead.";
    qDebug() << "=== RESOLUTION CHANGES APPLIED ===";
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
                quint8 blockChecksum = calculateEDIDChecksum(extensionBlock);
                extensionBlock[127] = blockChecksum;
                
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
    
    // Step 1: Stop all devices and hide main window
    stopAllDevices();
    hideMainWindow();
    
    // Create progress dialog
    progressDialog = new QProgressDialog("Updating display settings...", "Cancel", 0, 100, this);
    progressDialog->setWindowModality(Qt::WindowModal);
    progressDialog->setAutoClose(false);
    progressDialog->setAutoReset(false);
    progressDialog->show();
    
    // Step 2: Read firmware
    quint32 firmwareSize = VideoHid::getInstance().readFirmwareSize();
    if (firmwareSize == 0) {
        QMessageBox::critical(this, tr("Firmware Error"), tr("Failed to read firmware size."));
        delete progressDialog;
        progressDialog = nullptr;
        return false;
    }
    
    // Create temporary file path for firmware reading
    QString tempFirmwarePath = QApplication::applicationDirPath() + "/temp_firmware.bin";
    
    // Read firmware using FirmwareReader in a thread
    QThread* readerThread = new QThread();
    FirmwareReader* firmwareReader = new FirmwareReader(&VideoHid::getInstance(), ADDR_EEPROM, firmwareSize, tempFirmwarePath, this);
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
            if (progressDialog) {
                progressDialog->close();
                delete progressDialog;
                progressDialog = nullptr;
            }
            QMessageBox::critical(this, tr("Read Error"), tr("Failed to read firmware from device."));
            return;
        }
        
        if (progressDialog) {
            progressDialog->setValue(30); // Reading complete
            progressDialog->setLabelText("Processing EDID settings...");
        }
        
        // Read the firmware file
        QFile firmwareFile(tempFirmwarePath);
        if (!firmwareFile.open(QIODevice::ReadOnly)) {
            if (progressDialog) {
                progressDialog->close();
                delete progressDialog;
                progressDialog = nullptr;
            }
            QMessageBox::critical(this, tr("File Error"), tr("Failed to read firmware file."));
            return;
        }
        
        QByteArray firmwareData = firmwareFile.readAll();
        firmwareFile.close();
        
        // Process the EDID with new settings
        QByteArray modifiedFirmware = processEDIDDisplaySettings(firmwareData, newName, newSerial);
        if (modifiedFirmware.isEmpty()) {
            if (progressDialog) {
                progressDialog->close();
                delete progressDialog;
                progressDialog = nullptr;
            }
            QMessageBox::critical(this, tr("Processing Error"), tr("Failed to process EDID settings."));
            return;
        }
        
        if (progressDialog) {
            progressDialog->setValue(40); // Processing complete
            progressDialog->setLabelText("Writing modified firmware...");
        }
        
        // Create firmware writer thread
        QThread* writerThread = new QThread();
        FirmwareWriter* firmwareWriter = new FirmwareWriter(&VideoHid::getInstance(), ADDR_EEPROM, modifiedFirmware, this);
        firmwareWriter->moveToThread(writerThread);
        
        connect(firmwareWriter, &FirmwareWriter::progress, this, [this](int percent) {
            if (progressDialog) {
                progressDialog->setValue(40 + percent * 60 / 100); // Scale to 40-100%
            }
        });
        
        connect(firmwareWriter, &FirmwareWriter::finished, this, [=](bool writeSuccess) {
            if (progressDialog) {
                progressDialog->close();
                delete progressDialog;
                progressDialog = nullptr;
            }
            
            // Clean up temporary file
            QFile::remove(tempFirmwarePath);
            
            if (writeSuccess) {
                QMessageBox::information(this, tr("Success"), tr("Display settings updated successfully!\\n\\nPlease reconnect the device to see the changes."));
                QDialog::accept();
            } else {
                QMessageBox::critical(this, tr("Write Error"), tr("Failed to write firmware to device."));
            }
            
            writerThread->quit();
            writerThread->wait();
            writerThread->deleteLater();
        });
        
        connect(writerThread, &QThread::started, firmwareWriter, &FirmwareWriter::process);
        connect(firmwareWriter, &FirmwareWriter::finished, writerThread, &QThread::quit);
        connect(firmwareWriter, &FirmwareWriter::finished, firmwareWriter, &FirmwareWriter::deleteLater);
        connect(writerThread, &QThread::finished, writerThread, &QThread::deleteLater);
        
        writerThread->start();
        
        readerThread->quit();
        readerThread->wait();
        readerThread->deleteLater();
    });
    
    connect(firmwareReader, &FirmwareReader::error, this, [=](const QString& errorMessage) {
        if (progressDialog) {
            progressDialog->close();
            delete progressDialog;
            progressDialog = nullptr;
        }
        
        QMessageBox::critical(this, tr("Read Error"), tr("Firmware read failed: %1").arg(errorMessage));
        
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

int UpdateDisplaySettingsDialog::findEDIDBlock0(const QByteArray &firmwareData)
{
    // EDID header signature: 00 FF FF FF FF FF FF 00
    const QByteArray edidHeader = QByteArray::fromHex("00FFFFFFFFFFFF00");
    
    for (int i = 0; i <= firmwareData.size() - edidHeader.size(); ++i) {
        if (firmwareData.mid(i, edidHeader.size()) == edidHeader) {
            qDebug() << "EDID Block 0 found at offset:" << i;
            return i;
        }
    }
    
    qDebug() << "EDID Block 0 not found in firmware";
    return -1;
}

void UpdateDisplaySettingsDialog::updateEDIDDisplayName(QByteArray &edidBlock, const QString &newName)
{
    // Search for existing display name descriptor (0xFC) or use the first available descriptor
    int targetDescriptorOffset = -1;
    
    // Check all 4 descriptors (offsets: 54, 72, 90, 108)
    for (int descriptorOffset = 54; descriptorOffset <= 54 + 3 * 18; descriptorOffset += 18) {
        if (descriptorOffset + 18 > edidBlock.size()) break;
        
        // Check if this is a display name descriptor
        if (edidBlock[descriptorOffset] == 0x00 && 
            edidBlock[descriptorOffset + 1] == 0x00 && 
            edidBlock[descriptorOffset + 2] == 0x00 && 
            edidBlock[descriptorOffset + 3] == static_cast<char>(0xFC)) {
            targetDescriptorOffset = descriptorOffset;
            break;
        }
    }
    
    // If no display name descriptor found, use the last descriptor (offset 108)
    if (targetDescriptorOffset == -1) {
        targetDescriptorOffset = 108;
        qDebug() << "No existing display name descriptor found, using descriptor at offset 108";
    }
    
    if (targetDescriptorOffset + 18 > edidBlock.size()) {
        qWarning() << "Target descriptor offset exceeds EDID block size";
        return;
    }
    
    QByteArray nameBytes = newName.toUtf8();
    if (nameBytes.size() > 13) {
        nameBytes = nameBytes.left(13);
    }
    
    nameBytes.append(0x0A);

    // Pad with spaces and terminate with 0x0A
    while (nameBytes.size() < 13) {
        nameBytes.append(' ');
    }
    
    qDebug() << "Updating display name descriptor at offset:" << targetDescriptorOffset;
    
    // Show descriptor BEFORE update
    qDebug() << "=== DESCRIPTOR BEFORE UPDATE (offset" << targetDescriptorOffset << ") ===";
    QByteArray beforeDescriptor = edidBlock.mid(targetDescriptorOffset, 18);
    QString beforeHex;
    for (int i = 0; i < beforeDescriptor.size(); ++i) {
        beforeHex += QString("%1 ").arg(static_cast<quint8>(beforeDescriptor[i]), 2, 16, QChar('0')).toUpper();
    }
    qDebug() << "Before:" << beforeHex;
    
    // Set descriptor header for display name
    edidBlock[targetDescriptorOffset] = 0x00;
    edidBlock[targetDescriptorOffset + 1] = 0x00;
    edidBlock[targetDescriptorOffset + 2] = 0x00;
    edidBlock[targetDescriptorOffset + 3] = 0xFC; // Display name tag
    edidBlock[targetDescriptorOffset + 4] = 0x00;
    
    // Copy display name
    for (int i = 0; i < 13; ++i) {
        if (i < nameBytes.size()) {
            edidBlock[targetDescriptorOffset + 5 + i] = nameBytes[i];
        } else {
            edidBlock[targetDescriptorOffset + 5 + i] = ' ';
        }
    }
    
    // Show descriptor AFTER update
    qDebug() << "=== DESCRIPTOR AFTER UPDATE (offset" << targetDescriptorOffset << ") ===";
    QByteArray afterDescriptor = edidBlock.mid(targetDescriptorOffset, 18);
    QString afterHex;
    for (int i = 0; i < afterDescriptor.size(); ++i) {
        afterHex += QString("%1 ").arg(static_cast<quint8>(afterDescriptor[i]), 2, 16, QChar('0')).toUpper();
    }
    qDebug() << "After:" << afterHex;
    
    qDebug() << "Display name updated to:" << newName;
}

void UpdateDisplaySettingsDialog::updateEDIDSerialNumber(QByteArray &edidBlock, const QString &newSerial)
{
    // Search for existing serial number descriptor (0xFF) or use an available descriptor
    int targetDescriptorOffset = -1;
    
    // Check all 4 descriptors (offsets: 54, 72, 90, 108)
    for (int descriptorOffset = 54; descriptorOffset <= 54 + 3 * 18; descriptorOffset += 18) {
        if (descriptorOffset + 18 > edidBlock.size()) break;
        
        // Check if this is a serial number descriptor
        if (edidBlock[descriptorOffset] == 0x00 && 
            edidBlock[descriptorOffset + 1] == 0x00 && 
            edidBlock[descriptorOffset + 2] == 0x00 && 
            edidBlock[descriptorOffset + 3] == static_cast<char>(0xFF)) {
            targetDescriptorOffset = descriptorOffset;
            break;
        }
    }
    
    // If no serial number descriptor found, look for an unused descriptor
    if (targetDescriptorOffset == -1) {
        for (int descriptorOffset = 54; descriptorOffset <= 54 + 3 * 18; descriptorOffset += 18) {
            if (descriptorOffset + 18 > edidBlock.size()) break;
            
            // Check if this descriptor is unused (all zeros or display name if we're updating both)
            bool isUnused = true;
            for (int i = 0; i < 18; ++i) {
                if (edidBlock[descriptorOffset + i] != 0x00) {
                    isUnused = false;
                    break;
                }
            }
            
            // Also check if it's not a display name descriptor (to avoid conflicts)
            if (!isUnused && 
                edidBlock[descriptorOffset] == 0x00 && 
                edidBlock[descriptorOffset + 1] == 0x00 && 
                edidBlock[descriptorOffset + 2] == 0x00 && 
                edidBlock[descriptorOffset + 3] == static_cast<char>(0xFC)) {
                continue; // Skip display name descriptor
            }
            
            if (isUnused) {
                targetDescriptorOffset = descriptorOffset;
                break;
            }
        }
    }
    
    // If still no descriptor found, use the first available one (54)
    if (targetDescriptorOffset == -1) {
        targetDescriptorOffset = 72; // Use second descriptor to avoid conflicts with display name
        qDebug() << "No existing serial number descriptor found, using descriptor at offset 72";
    }
    
    if (targetDescriptorOffset + 18 > edidBlock.size()) {
        qWarning() << "Target descriptor offset exceeds EDID block size";
        return;
    }
    
    QByteArray serialBytes = newSerial.toUtf8();
    if (serialBytes.size() > 13) {
        serialBytes = serialBytes.left(13);
    }
    
    serialBytes.append(0x0A);

    // Pad with spaces
    while (serialBytes.size() < 13) {
        serialBytes.append(' ');
    }
    
    qDebug() << "Updating serial number descriptor at offset:" << targetDescriptorOffset;
    
    // Show descriptor BEFORE update
    qDebug() << "=== SERIAL DESCRIPTOR BEFORE UPDATE (offset" << targetDescriptorOffset << ") ===";
    QByteArray beforeDescriptor = edidBlock.mid(targetDescriptorOffset, 18);
    QString beforeHex;
    for (int i = 0; i < beforeDescriptor.size(); ++i) {
        beforeHex += QString("%1 ").arg(static_cast<quint8>(beforeDescriptor[i]), 2, 16, QChar('0')).toUpper();
    }
    qDebug() << "Before:" << beforeHex;
    
    // Set descriptor header for serial number
    edidBlock[targetDescriptorOffset] = 0x00;
    edidBlock[targetDescriptorOffset + 1] = 0x00;
    edidBlock[targetDescriptorOffset + 2] = 0x00;
    edidBlock[targetDescriptorOffset + 3] = 0xFF; // Serial number tag
    edidBlock[targetDescriptorOffset + 4] = 0x00;
    
    // Copy serial number
    for (int i = 0; i < 13; ++i) {
        if (i < serialBytes.size()) {
            edidBlock[targetDescriptorOffset + 5 + i] = serialBytes[i];
        } else {
            edidBlock[targetDescriptorOffset + 5 + i] = ' ';
        }
    }
    
    // Show descriptor AFTER update
    qDebug() << "=== SERIAL DESCRIPTOR AFTER UPDATE (offset" << targetDescriptorOffset << ") ===";
    QByteArray afterDescriptor = edidBlock.mid(targetDescriptorOffset, 18);
    QString afterHex;
    for (int i = 0; i < afterDescriptor.size(); ++i) {
        afterHex += QString("%1 ").arg(static_cast<quint8>(afterDescriptor[i]), 2, 16, QChar('0')).toUpper();
    }
    qDebug() << "After:" << afterHex;
    
    qDebug() << "Serial number updated to:" << newSerial;
}

quint8 UpdateDisplaySettingsDialog::calculateEDIDChecksum(const QByteArray &edidBlock)
{
    if (edidBlock.size() != 128) {
        qWarning() << "EDID block size is not 128 bytes:" << edidBlock.size();
        return 0;
    }
    
    quint16 sum = 0;
    for (int i = 0; i < 127; ++i) {
        sum += static_cast<quint8>(edidBlock[i]);
    }
    
    quint8 checksum = (256 - (sum & 0xFF)) & 0xFF;
    qDebug() << "Calculated EDID checksum:" << QString("0x%1").arg(checksum, 2, 16, QChar('0'));
    
    return checksum;
}

quint16 UpdateDisplaySettingsDialog::calculateFirmwareChecksumWithDiff(const QByteArray &originalFirmware, const QByteArray &originalEDID, const QByteArray &modifiedEDID)
{
    if (originalFirmware.size() < 2) {
        qWarning() << "Firmware too small for checksum calculation";
        return 0;
    }
    
    if (originalEDID.size() != modifiedEDID.size() || originalEDID.size() != 128) {
        qWarning() << "EDID blocks must be 128 bytes and same size";
        return 0;
    }
    
    qDebug() << "Calculating firmware checksum using EDID difference method:";
    qDebug() << "  Total firmware size:" << originalFirmware.size() << "bytes";
    
    // Get the original firmware checksum from the last 2 bytes
    quint8 originalLowByte = static_cast<quint8>(originalFirmware[originalFirmware.size() - 2]);
    quint8 originalHighByte = static_cast<quint8>(originalFirmware[originalFirmware.size() - 1]);
    
    // Determine byte order by checking which interpretation makes more sense
    // Try both little-endian and big-endian interpretations
    quint16 originalChecksumLE = originalLowByte | (originalHighByte << 8);  // Little-endian
    quint16 originalChecksumBE = (originalLowByte << 8) | originalHighByte;  // Big-endian
    
    qDebug() << "  Original last 2 bytes: 0x" << QString::number(originalLowByte, 16).toUpper().rightJustified(2, '0') << 
                " 0x" << QString::number(originalHighByte, 16).toUpper().rightJustified(2, '0');
    qDebug() << "  Original checksum (little-endian): 0x" << QString::number(originalChecksumLE, 16).toUpper().rightJustified(4, '0');
    qDebug() << "  Original checksum (big-endian): 0x" << QString::number(originalChecksumBE, 16).toUpper().rightJustified(4, '0');
    
    // Calculate the sum difference between original and modified EDID blocks
    qint32 edidDifference = 0;
    for (int i = 0; i < 128; ++i) {
        edidDifference += static_cast<quint8>(modifiedEDID[i]) - static_cast<quint8>(originalEDID[i]);
    }
    
    qDebug() << "  EDID byte sum difference:" << edidDifference;
    
    // Calculate new checksum by adding the difference to the original checksum
    // We'll use big-endian format as it's more common for firmware checksums
    qint32 newChecksumInt = static_cast<qint32>(originalChecksumBE) + edidDifference;
    quint16 newChecksum = static_cast<quint16>(newChecksumInt & 0xFFFF);
    
    qDebug() << "  Original checksum (using big-endian): 0x" << QString::number(originalChecksumBE, 16).toUpper().rightJustified(4, '0');
    qDebug() << "  New checksum calculation: 0x" << QString::number(originalChecksumBE, 16).toUpper() << 
                " + " << edidDifference << " = 0x" << QString::number(newChecksumInt, 16).toUpper();
    qDebug() << "  Final checksum (16-bit): 0x" << QString::number(newChecksum, 16).toUpper().rightJustified(4, '0');
    
    // Verify by showing byte breakdown
    qDebug() << "  New checksum breakdown:";
    qDebug() << "    High byte: 0x" << QString::number((newChecksum >> 8) & 0xFF, 16).toUpper().rightJustified(2, '0');
    qDebug() << "    Low byte: 0x" << QString::number(newChecksum & 0xFF, 16).toUpper().rightJustified(2, '0');
    
    return newChecksum;
}

quint16 UpdateDisplaySettingsDialog::calculateFirmwareChecksumWithDiff(const QByteArray &originalFirmware, const QByteArray &modifiedFirmware)
{
    if (originalFirmware.size() < 2 || modifiedFirmware.size() < 2) {
        qWarning() << "Firmware too small for checksum calculation";
        return 0;
    }
    
    if (originalFirmware.size() != modifiedFirmware.size()) {
        qWarning() << "Original and modified firmware must be same size";
        return 0;
    }
    
    qDebug() << "Calculating firmware checksum using complete firmware difference method:";
    qDebug() << "  Total firmware size:" << originalFirmware.size() << "bytes";
    
    // Get the original firmware checksum from the last 2 bytes
    quint8 originalLowByte = static_cast<quint8>(originalFirmware[originalFirmware.size() - 2]);
    quint8 originalHighByte = static_cast<quint8>(originalFirmware[originalFirmware.size() - 1]);
    
    // Use big-endian format as it's more common for firmware checksums
    quint16 originalChecksum = (originalLowByte << 8) | originalHighByte;
    
    qDebug() << "  Original last 2 bytes: 0x" << QString::number(originalLowByte, 16).toUpper().rightJustified(2, '0') << 
                " 0x" << QString::number(originalHighByte, 16).toUpper().rightJustified(2, '0');
    qDebug() << "  Original checksum (big-endian): 0x" << QString::number(originalChecksum, 16).toUpper().rightJustified(4, '0');
    
    // Calculate the sum difference between original and modified firmware (excluding last 2 checksum bytes)
    qint32 firmwareDifference = 0;
    int checksumExcludeSize = originalFirmware.size() - 2;
    
    for (int i = 0; i < checksumExcludeSize; ++i) {
        firmwareDifference += static_cast<quint8>(modifiedFirmware[i]) - static_cast<quint8>(originalFirmware[i]);
    }
    
    qDebug() << "  Firmware byte sum difference (excluding checksum):" << firmwareDifference;
    
    // Calculate new checksum by adding the difference to the original checksum
    qint32 newChecksumInt = static_cast<qint32>(originalChecksum) + firmwareDifference;
    quint16 newChecksum = static_cast<quint16>(newChecksumInt & 0xFFFF);
    
    qDebug() << "  New checksum calculation: 0x" << QString::number(originalChecksum, 16).toUpper() << 
                " + " << firmwareDifference << " = 0x" << QString::number(newChecksumInt, 16).toUpper();
    qDebug() << "  Final checksum (16-bit): 0x" << QString::number(newChecksum, 16).toUpper().rightJustified(4, '0');
    
    // Verify by showing byte breakdown
    qDebug() << "  New checksum breakdown:";
    qDebug() << "    High byte: 0x" << QString::number((newChecksum >> 8) & 0xFF, 16).toUpper().rightJustified(2, '0');
    qDebug() << "    Low byte: 0x" << QString::number(newChecksum & 0xFF, 16).toUpper().rightJustified(2, '0');
    
    return newChecksum;
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
    
    QByteArray modifiedFirmware = firmwareData; // Make a copy
    
    // Show complete firmware BEFORE update (first 256 bytes for debugging)
    qDebug() << "=== COMPLETE FIRMWARE BEFORE UPDATE ===";
    qDebug() << "Firmware size:" << firmwareData.size() << "bytes";
    showFirmwareHexDump(firmwareData, 0, qMin(256, firmwareData.size()));
    
    // Find EDID Block 0
    int edidOffset = findEDIDBlock0(modifiedFirmware);
    if (edidOffset == -1) {
        qWarning() << "EDID Block 0 not found in firmware";
        return QByteArray(); // Return empty array to indicate failure
    }
    
    // Extract EDID block (128 bytes)
    if (edidOffset + 128 > modifiedFirmware.size()) {
        qWarning() << "Incomplete EDID block in firmware";
        return QByteArray();
    }
    
    QByteArray edidBlock = modifiedFirmware.mid(edidOffset, 128);
    
    // Store original EDID block for checksum difference calculation
    QByteArray originalEDIDBlock = edidBlock; // Make a copy before modification
    
    // Show EDID descriptors BEFORE update
    qDebug() << "=== EDID DESCRIPTORS BEFORE UPDATE ===";
    showEDIDDescriptors(edidBlock);
    
    // Update display settings
    if (!newName.isEmpty()) {
        updateEDIDDisplayName(edidBlock, newName);
    }
    
    if (!newSerial.isEmpty()) {
        updateEDIDSerialNumber(edidBlock, newSerial);
    }
    
    // Apply resolution changes to extension blocks if any
    if (hasResolutionUpdate) {
        updateExtensionBlockResolutions(modifiedFirmware, edidOffset);
    }
    
    // Show EDID descriptors AFTER update
    qDebug() << "=== EDID DESCRIPTORS AFTER UPDATE ===";
    showEDIDDescriptors(edidBlock);
    
    // Calculate and update EDID checksum
    quint8 edidChecksum = calculateEDIDChecksum(edidBlock);
    edidBlock[127] = edidChecksum;
    
    // Replace EDID block in firmware
    modifiedFirmware.replace(edidOffset, 128, edidBlock);
    
    // Calculate and update firmware checksum using differential method for all changes
    quint16 firmwareChecksum = calculateFirmwareChecksumWithDiff(firmwareData, modifiedFirmware);
    
    // Write 16-bit checksum to last 2 bytes 
    // Try different byte order - some firmwares store as [high_byte][low_byte]
    if (modifiedFirmware.size() >= 2) {
        // Try storing as big-endian (high byte first, then low byte)
        modifiedFirmware[modifiedFirmware.size() - 2] = static_cast<char>((firmwareChecksum >> 8) & 0xFF); // High byte first
        modifiedFirmware[modifiedFirmware.size() - 1] = static_cast<char>(firmwareChecksum & 0xFF);        // Low byte second
        
        qDebug() << "Written firmware checksum to last 2 bytes (big-endian, differential method):";
        qDebug() << "  Checksum value: 0x" << QString::number(firmwareChecksum, 16).toUpper().rightJustified(4, '0');
        qDebug() << "  High byte (pos-2): 0x" << QString::number((firmwareChecksum >> 8) & 0xFF, 16).toUpper().rightJustified(2, '0');
        qDebug() << "  Low byte (pos-1): 0x" << QString::number(firmwareChecksum & 0xFF, 16).toUpper().rightJustified(2, '0');
        
        // Show actual bytes written
        qDebug() << "  Actual last 2 bytes: 0x" << 
            QString::number(static_cast<quint8>(modifiedFirmware[modifiedFirmware.size() - 2]), 16).toUpper().rightJustified(2, '0') << 
            " 0x" << 
            QString::number(static_cast<quint8>(modifiedFirmware[modifiedFirmware.size() - 1]), 16).toUpper().rightJustified(2, '0');
    } else {
        qWarning() << "Firmware too small to write checksum";
    }
    
    // Show complete firmware AFTER update (first 256 bytes for debugging)
    qDebug() << "=== COMPLETE FIRMWARE AFTER UPDATE ===";
    qDebug() << "Modified firmware size:" << modifiedFirmware.size() << "bytes";
    showFirmwareHexDump(modifiedFirmware, 0, qMin(256, modifiedFirmware.size()));
    
    // Also show the end of firmware (last 32 bytes) to verify checksum
    if (modifiedFirmware.size() > 32) {
        qDebug() << "=== FIRMWARE END (last 32 bytes) ===";
        showFirmwareHexDump(modifiedFirmware, modifiedFirmware.size() - 32, 32);
    }
    
    qDebug() << "EDID display settings processing completed successfully";
    return modifiedFirmware;
}

void UpdateDisplaySettingsDialog::showEDIDDescriptors(const QByteArray &edidBlock)
{
    qDebug() << "EDID Block size:" << edidBlock.size();
    
    // Display descriptors starting at offset 54 (4 descriptors, each 18 bytes)
    for (int descriptorOffset = 54; descriptorOffset <= 54 + 3 * 18; descriptorOffset += 18) {
        if (descriptorOffset + 18 > edidBlock.size()) break;
        
        QByteArray descriptor = edidBlock.mid(descriptorOffset, 18);
        QString hexString;
        for (int i = 0; i < descriptor.size(); ++i) {
            hexString += QString("%1 ").arg(static_cast<quint8>(descriptor[i]), 2, 16, QChar('0')).toUpper();
        }
        
        qDebug() << QString("Descriptor at offset %1:").arg(descriptorOffset);
        qDebug() << "  Hex:" << hexString;
        
        // Check descriptor type
        quint8 descriptorType = static_cast<quint8>(descriptor[3]);
        if (descriptor[0] == 0x00 && descriptor[1] == 0x00 && descriptor[2] == 0x00) {
            switch (descriptorType) {
                case 0xFF:
                    qDebug() << "  Type: Display Serial Number";
                    if (descriptor.size() >= 18) {
                        QByteArray serialBytes = descriptor.mid(5, 13);
                        QString serialNumber;
                        for (int i = 0; i < serialBytes.size(); ++i) {
                            char c = serialBytes[i];
                            if (c == 0x0A) break; // Line feed terminator
                            if (c >= 32 && c <= 126) { // Printable ASCII
                                serialNumber += c;
                            }
                        }
                        qDebug() << "  Serial Number:" << serialNumber.trimmed();
                    }
                    break;
                case 0xFE:
                    qDebug() << "  Type: Unspecified Text";
                    break;
                case 0xFD:
                    qDebug() << "  Type: Display Range Limits";
                    break;
                case 0xFC:
                    qDebug() << "  Type: Display Product Name";
                    // Extract display name (13 bytes starting at offset 5)
                    if (descriptor.size() >= 18) {
                        QByteArray nameBytes = descriptor.mid(5, 13);
                        QString displayName;
                        for (int i = 0; i < nameBytes.size(); ++i) {
                            char c = nameBytes[i];
                            if (c == 0x0A) break; // Line feed terminator
                            if (c >= 32 && c <= 126) { // Printable ASCII
                                displayName += c;
                            }
                        }
                        qDebug() << "  Display Name:" << displayName.trimmed();
                    }
                    break;
                case 0xFB:
                    qDebug() << "  Type: Color Point Data";
                    break;
                case 0xFA:
                    qDebug() << "  Type: Standard Timing Identifications";
                    break;
                default:
                    if (descriptorType == 0x00) {
                        qDebug() << "  Type: Empty/Unused Descriptor";
                    } else {
                        qDebug() << "  Type: Unknown (" << QString("0x%1").arg(descriptorType, 2, 16, QChar('0')).toUpper() << ")";
                    }
                    break;
            }
        } else {
            qDebug() << "  Type: Detailed Timing Descriptor";
        }
    }
}

void UpdateDisplaySettingsDialog::showFirmwareHexDump(const QByteArray &firmwareData, int startOffset, int length)
{
    if (length == -1) {
        length = firmwareData.size() - startOffset;
    }
    
    length = qMin(length, firmwareData.size() - startOffset);
    
    for (int i = 0; i < length; i += 16) {
        QString line = QString("0x%1: ").arg(startOffset + i, 8, 16, QChar('0')).toUpper();
        
        // Hex bytes
        for (int j = 0; j < 16 && (i + j) < length; ++j) {
            quint8 byte = static_cast<quint8>(firmwareData[startOffset + i + j]);
            line += QString("%1 ").arg(byte, 2, 16, QChar('0')).toUpper();
        }
        
        // Pad if incomplete line
        for (int j = (i + 16 > length) ? length - i : 16; j < 16; ++j) {
            line += "   ";
        }
        
        line += " | ";
        
        // ASCII representation
        for (int j = 0; j < 16 && (i + j) < length; ++j) {
            char c = firmwareData[startOffset + i + j];
            if (c >= 32 && c <= 126) {
                line += c;
            } else {
                line += '.';
            }
        }
        
        qDebug() << line;
    }
}
