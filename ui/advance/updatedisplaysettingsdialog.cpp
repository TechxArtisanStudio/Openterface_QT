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
#include <QApplication>
#include <QDebug>
#include <QFile>
#include <QGroupBox>
#include <QCheckBox>

UpdateDisplaySettingsDialog::UpdateDisplaySettingsDialog(QWidget *parent) :
    QDialog(parent),
    titleLabel(nullptr),
    displayNameGroup(nullptr),
    displayNameCheckBox(nullptr),
    displayNameLineEdit(nullptr),
    serialNumberGroup(nullptr),
    serialNumberCheckBox(nullptr),
    serialNumberLineEdit(nullptr),
    updateButton(nullptr),
    cancelButton(nullptr),
    mainLayout(nullptr),
    displayNameLayout(nullptr),
    serialNumberLayout(nullptr),
    buttonLayout(nullptr),
    progressDialog(nullptr)
{
    setupUI();
}

UpdateDisplaySettingsDialog::~UpdateDisplaySettingsDialog()
{
    // Qt will automatically delete child widgets
}

void UpdateDisplaySettingsDialog::setupUI()
{
    setWindowTitle(tr("Update Display Settings"));
    setModal(true);
    setFixedSize(450, 280);
    
    // Create main layout
    mainLayout = new QVBoxLayout(this);
    
    // Title label
    titleLabel = new QLabel(tr("Update display EDID settings:"), this);
    titleLabel->setStyleSheet("font-weight: bold; margin-bottom: 10px;");
    mainLayout->addWidget(titleLabel);
    
    // Display Name Group
    displayNameGroup = new QGroupBox(tr("Display Name"), this);
    displayNameLayout = new QVBoxLayout(displayNameGroup);
    
    displayNameCheckBox = new QCheckBox(tr("Update display name"), this);
    displayNameCheckBox->setChecked(true);
    displayNameLayout->addWidget(displayNameCheckBox);
    
    displayNameLineEdit = new QLineEdit(this);
    displayNameLineEdit->setPlaceholderText(tr("Enter new display name (max 13 characters)"));
    displayNameLayout->addWidget(displayNameLineEdit);
    
    mainLayout->addWidget(displayNameGroup);
    
    // Serial Number Group
    serialNumberGroup = new QGroupBox(tr("Serial Number"), this);
    serialNumberLayout = new QVBoxLayout(serialNumberGroup);
    
    serialNumberCheckBox = new QCheckBox(tr("Update serial number"), this);
    serialNumberCheckBox->setChecked(false);
    serialNumberLayout->addWidget(serialNumberCheckBox);
    
    serialNumberLineEdit = new QLineEdit(this);
    serialNumberLineEdit->setPlaceholderText(tr("Enter new serial number (max 13 characters)"));
    serialNumberLineEdit->setEnabled(false);
    serialNumberLayout->addWidget(serialNumberLineEdit);
    
    mainLayout->addWidget(serialNumberGroup);
    
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
    
    // Connect text change signals to update button state
    connect(displayNameLineEdit, &QLineEdit::textChanged, this, &UpdateDisplaySettingsDialog::enableUpdateButton);
    connect(serialNumberLineEdit, &QLineEdit::textChanged, this, &UpdateDisplaySettingsDialog::enableUpdateButton);
    
    // Update initial state
    enableUpdateButton();
    
    // Set focus to the display name line edit
    displayNameLineEdit->setFocus();
}

void UpdateDisplaySettingsDialog::closeEvent(QCloseEvent *event)
{
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
                      (serialNumberCheckBox->isChecked() && !serialNumberLineEdit->text().trimmed().isEmpty());
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
    if (!displayNameCheckBox->isChecked() && !serialNumberCheckBox->isChecked()) {
        QMessageBox::warning(this, tr("No Updates Selected"), tr("Please select at least one setting to update."));
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

QString UpdateDisplaySettingsDialog::getCurrentDisplayName()
{
    // TODO: Implement actual EDID reading to get current display name
    // For now, return a placeholder
    return tr("Default Display");
}

QString UpdateDisplaySettingsDialog::getCurrentSerialNumber()
{
    // TODO: Implement actual EDID reading to get current serial number
    // For now, return a placeholder
    return tr("Unknown");
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

QByteArray UpdateDisplaySettingsDialog::processEDIDDisplaySettings(const QByteArray &firmwareData, const QString &newName, const QString &newSerial)
{
    qDebug() << "Processing EDID display settings update...";
    if (!newName.isEmpty()) {
        qDebug() << "  Updating display name to:" << newName;
    }
    if (!newSerial.isEmpty()) {
        qDebug() << "  Updating serial number to:" << newSerial;
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
    
    // Show EDID descriptors AFTER update
    qDebug() << "=== EDID DESCRIPTORS AFTER UPDATE ===";
    showEDIDDescriptors(edidBlock);
    
    // Calculate and update EDID checksum
    quint8 edidChecksum = calculateEDIDChecksum(edidBlock);
    edidBlock[127] = edidChecksum;
    
    // Replace EDID block in firmware
    modifiedFirmware.replace(edidOffset, 128, edidBlock);
    
    // Calculate and update firmware checksum using differential method
    quint16 firmwareChecksum = calculateFirmwareChecksumWithDiff(firmwareData, originalEDIDBlock, edidBlock);
    
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
