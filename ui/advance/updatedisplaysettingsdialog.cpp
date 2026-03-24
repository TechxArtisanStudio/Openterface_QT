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
#include <QPointer>
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

    // Disconnect but avoid touching in-progress QObjects prematurely.
    if (firmwareReader) {
        firmwareReader->disconnect();
    }

    if (firmwareReaderThread) {
        firmwareReaderThread->disconnect();

        if (firmwareReaderThread->isRunning()) {
            qDebug() << "Requesting reader thread interruption";
            firmwareReaderThread->requestInterruption();
            firmwareReaderThread->quit();

            if (!firmwareReaderThread->wait(2000)) {
                qWarning() << "Firmware reader thread didn't quit gracefully, terminating";
                firmwareReaderThread->terminate();
                firmwareReaderThread->wait(500);
            }
        }

        firmwareReaderThread->deleteLater();
        firmwareReaderThread = nullptr;
    }

    if (firmwareReader) {
        firmwareReader->deleteLater();
        firmwareReader = nullptr;
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

void UpdateDisplaySettingsDialog::startFirmwareReadTask(QThread*& thread, FirmwareReader*& reader, quint32 firmwareSize, const QString& tempFirmwarePath,
                                                        std::function<void(int)> progressCallback,
                                                        std::function<void(bool)> finishedCallback,
                                                        std::function<void(const QString&)> errorCallback)
{
    // Clean up existing thread if this is the member context.
    if (thread && thread == firmwareReaderThread) {
        cleanupFirmwareReaderThread();
    }

    thread = new QThread(this);
    reader = new FirmwareReader(&VideoHid::getInstance(), ADDR_EEPROM, firmwareSize, tempFirmwarePath);
    reader->moveToThread(thread);

    connect(thread, &QThread::started, reader, &FirmwareReader::process);
    connect(reader, &FirmwareReader::progress, this, [progressCallback](int percent) {
        if (progressCallback) {
            progressCallback(percent);
        }
    });
    connect(reader, &FirmwareReader::finished, this, [this, finishedCallback](bool success) {
        if (finishedCallback) {
            finishedCallback(success);
        }
    });
    connect(reader, &FirmwareReader::error, this, [this, errorCallback](const QString &msg) {
        if (errorCallback) {
            errorCallback(msg);
        }
    });
    connect(reader, &FirmwareReader::finished, thread, &QThread::quit);
    connect(reader, &FirmwareReader::error, thread, &QThread::quit);
    // Do NOT connect auto-deleteLater here.
    // cleanupFirmwareReaderThread() owns all deletion to avoid double-free / use-after-free.

    thread->start();
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
    
    startFirmwareReadTask(firmwareReaderThread, firmwareReader, firmwareSize, tempFirmwarePath,
                          [this](int percent){ onFirmwareReadProgress(percent); },
                          [this](bool success){ onFirmwareReadFinished(success); },
                          [this](const QString &err){ onFirmwareReadError(err); });
    // Note: We handle cleanup manually in cleanupFirmwareReaderThread() instead of auto-deleting
    // to avoid crashes when the dialog is closed while the thread is running
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

    // Clean up firmware reader thread and restart polling safely.
    // Use QPointer for safe object lifetime checking.
    QPointer<UpdateDisplaySettingsDialog> selfPtr(this);
    QTimer::singleShot(0, this, [this, selfPtr]() {
        if (!selfPtr) {
            return;
        }

        cleanupFirmwareReaderThread();

        QTimer::singleShot(500, this, [this, selfPtr]() {
            if (!selfPtr) {
                return;
            }
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
    readResolutionFromEDID(edidBlock, firmwareData);
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
    if (firmwareReaderThread) {
        cleanupFirmwareReaderThread();
    }
    firmwareReaderThread = nullptr;
    firmwareReader = nullptr;

    QThread* readerThread = firmwareReaderThread;
    FirmwareReader* reader = firmwareReader;

    auto onProgress = [this](int percent){
        if (progressDialog) {
            progressDialog->setValue(percent * 30 / 100); // Scale to 0-30%
        }
    };

    auto onFinished = [this, tempFirmwarePath, newName, newSerial](bool success) {
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
        EdidProcessor processor(resolutionModel);
        QByteArray modifiedFirmware = processor.processDisplaySettings(firmwareData, newName, newSerial);
        if (modifiedFirmware.isEmpty()) {
            showErrorAndRestart(tr("Processing Error"), tr("Failed to process EDID settings."), tr("EDID processing error"));
            return;
        }

        if (progressDialog) {
            progressDialog->setValue(40); // Processing complete
            progressDialog->setLabelText("Writing modified firmware...");
        }

        startFirmwareWrite(modifiedFirmware, tempFirmwarePath);
    };

    auto onError = [this](const QString &errorMessage) {
        showErrorAndRestart(tr("Read Error"), tr("Firmware read failed: %1").arg(errorMessage), tr("firmware read error"));
    };

    startFirmwareReadTask(firmwareReaderThread, firmwareReader, firmwareSize, tempFirmwarePath, onProgress, onFinished, onError);
    return true;
}

void UpdateDisplaySettingsDialog::onFirmwareReadProgress(int percent)
{
    if (progressBar) {
        progressBar->setValue(percent);
    }
    if (progressDialog) {
        progressDialog->setValue(percent);
    }
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



