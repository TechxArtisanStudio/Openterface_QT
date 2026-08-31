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

#include "renamedisplaydialog.h"
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
#include <QSpacerItem>
#include <QProgressDialog>
#include <QThread>
#include <QApplication>
#include <QDebug>
#include <QFile>

RenameDisplayDialog::RenameDisplayDialog(QWidget *parent) :
    QDialog(parent),
    titleLabel(nullptr),
    displayNameLineEdit(nullptr),
    updateButton(nullptr),
    cancelButton(nullptr),
    mainLayout(nullptr),
    buttonLayout(nullptr),
    progressDialog(nullptr)
{
    setWindowTitle(tr("Rename Display"));
    setModal(true);
    setFixedSize(400, 150);
    
    // Create UI components
    titleLabel = new QLabel(tr("Enter new display name:"), this);
    
    displayNameLineEdit = new QLineEdit(this);
    displayNameLineEdit->setPlaceholderText(tr("Display name"));
    
    updateButton = new QPushButton(tr("Update"), this);
    updateButton->setDefault(true);
    
    cancelButton = new QPushButton(tr("Cancel"), this);
    
    // Create layouts
    mainLayout = new QVBoxLayout(this);
    buttonLayout = new QHBoxLayout();
    
    // Add widgets to main layout
    mainLayout->addWidget(titleLabel);
    mainLayout->addWidget(displayNameLineEdit);
    
    // Create button layout with spacer
    QSpacerItem *horizontalSpacer = new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);
    buttonLayout->addItem(horizontalSpacer);
    buttonLayout->addWidget(updateButton);
    buttonLayout->addWidget(cancelButton);
    
    mainLayout->addLayout(buttonLayout);
    
    // Set the layout for the dialog
    setLayout(mainLayout);
    
    // Set the current display name as the default text
    // QString currentName = getCurrentDisplayName();
    // displayNameLineEdit->setText(currentName);
    displayNameLineEdit->selectAll();
    
    // Connect signals to slots
    connect(updateButton, &QPushButton::clicked, this, &RenameDisplayDialog::onUpdateButtonClicked);
    connect(cancelButton, &QPushButton::clicked, this, &RenameDisplayDialog::onCancelButtonClicked);
    
    // Set focus to the line edit
    displayNameLineEdit->setFocus();
}

RenameDisplayDialog::~RenameDisplayDialog()
{
    // Qt will automatically delete child widgets
}

void RenameDisplayDialog::closeEvent(QCloseEvent *event)
{
    event->accept();
}

void RenameDisplayDialog::accept()
{
    onUpdateButtonClicked();
}

void RenameDisplayDialog::reject()
{
    onCancelButtonClicked();
}

void RenameDisplayDialog::onUpdateButtonClicked()
{
    QString newName = displayNameLineEdit->text().trimmed();
    
    // Step 1: Check the name in lineEditor
    if (newName.isEmpty()) {
        QMessageBox::warning(this, tr("Warning"), tr("Display name cannot be empty."));
        return;
    }
    
    // Validate ASCII characters and length
    if (newName.length() > 13) {
        QMessageBox::warning(this, tr("Warning"), tr("Display name cannot exceed 13 characters."));
        return;
    }
    
    // Check for ASCII-only characters
    for (const QChar &ch : newName) {
        if (ch.unicode() > 127) {
            QMessageBox::warning(this, tr("Warning"), tr("Display name must contain only ASCII characters."));
            return;
        }
    }
    
    // Step 2: Start the update process
    if (updateDisplayName(newName)) {
        // The updateDisplayName method handles the async process
        // Success/failure will be handled in the callbacks
    } else {
        QMessageBox::critical(this, tr("Error"), tr("Failed to start display name update process."));
    }
}

void RenameDisplayDialog::onCancelButtonClicked()
{
    QDialog::reject();
}

QString RenameDisplayDialog::getCurrentDisplayName()
{
    // TODO: Implement actual EDID reading to get current display name
    // For now, return a placeholder
    return tr("Default Display");
}

bool RenameDisplayDialog::updateDisplayName(const QString &newName)
{
    
    // Step 1: Stop all devices and hide main window
    stopAllDevices();
    hideMainWindow();
    
    // Create progress dialog
    progressDialog = new QProgressDialog("Updating display name...", "Cancel", 0, 100, this);
    progressDialog->setWindowModality(Qt::WindowModal);
    progressDialog->setAutoClose(false);
    progressDialog->setAutoReset(false);
    progressDialog->show();
    
    // Step 2: Read firmware
    quint32 firmwareSize = VideoHid::getInstance().readFirmwareSize();
    if (firmwareSize == 0) {
        QMessageBox::critical(this, tr("Error"), tr("Failed to determine firmware size."));
        progressDialog->deleteLater();
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
        int overallProgress = (percent * 30) / 100; // Reading takes 30% of total progress
        progressDialog->setValue(overallProgress);
        progressDialog->setLabelText(tr("Reading firmware... %1%").arg(overallProgress));
    });
    
    // Handle firmware read completion
    connect(firmwareReader, &FirmwareReader::finished, this, [=](bool success) {
        if (!success) {
            QMessageBox::critical(this, tr("Error"), tr("Failed to read firmware."));
            progressDialog->deleteLater();
            progressDialog = nullptr;
            readerThread->quit();
            readerThread->wait();
            readerThread->deleteLater();
            return;
        }
        
        progressDialog->setValue(30);
        progressDialog->setLabelText(tr("Processing EDID data..."));
        
        // Step 3: Process EDID data
        QFile tempFile(tempFirmwarePath);
        if (!tempFile.open(QIODevice::ReadOnly)) {
            QMessageBox::critical(this, tr("Error"), tr("Failed to read temporary firmware file."));
            progressDialog->deleteLater();
            progressDialog = nullptr;
            readerThread->quit();
            readerThread->wait();
            readerThread->deleteLater();
            return;
        }
        
        QByteArray firmwareData = tempFile.readAll();
        tempFile.close();
        tempFile.remove(); // Clean up temporary file
        
        // Process EDID to update display name
        QByteArray modifiedFirmware = processEDIDDisplayName(firmwareData, newName);
        if (modifiedFirmware.isEmpty()) {
            QMessageBox::critical(this, tr("Error"), tr("Failed to process EDID data."));
            progressDialog->deleteLater();
            progressDialog = nullptr;
            readerThread->quit();
            readerThread->wait();
            readerThread->deleteLater();
            return;
        }
        
        progressDialog->setValue(40);
        progressDialog->setLabelText(tr("Writing firmware..."));
        
        // Step 4: Write modified firmware
        QThread* writerThread = new QThread();
        FirmwareWriter* firmwareWriter = new FirmwareWriter(&VideoHid::getInstance(), ADDR_EEPROM, modifiedFirmware, this);
        firmwareWriter->moveToThread(writerThread);
        
        // Track writing progress (40-100%)
        connect(firmwareWriter, &FirmwareWriter::progress, this, [this](int percent) {
            int overallProgress = 40 + (percent * 60) / 100; // Writing takes 60% of total progress
            progressDialog->setValue(overallProgress);
            progressDialog->setLabelText(tr("Writing firmware... %1%").arg(overallProgress));
        });
        
        // Handle firmware write completion
        connect(firmwareWriter, &FirmwareWriter::finished, this, [=](bool writeSuccess) {
            progressDialog->setValue(100);
            
            if (writeSuccess) {
                QMessageBox::information(this, tr("Success"), 
                    tr("Display name updated successfully!\n\n"
                       "The application will now close.\n"
                       "Please:\n"
                       "1. Restart the application\n"
                       "2. Disconnect and reconnect all cables"));
            } else {
                QMessageBox::critical(this, tr("Error"), tr("Failed to write firmware."));
            }
            
            progressDialog->deleteLater();
            progressDialog = nullptr;
            writerThread->quit();
            writerThread->wait();
            writerThread->deleteLater();
            
            // Close application as firmware has been modified
            QApplication::quit();
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
        QMessageBox::critical(this, tr("Error"), tr("Firmware read error: %1").arg(errorMessage));
        progressDialog->deleteLater();
        progressDialog = nullptr;
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

void RenameDisplayDialog::stopAllDevices()
{
    
    // Stop VideoHid
    VideoHid::getInstance().stop();
    
    // Stop Serial Port Manager
    SerialPortManager::getInstance().stop();
    
    // Get main window to access camera and audio managers
    QWidget *parentWindow = this->parentWidget();
    if (parentWindow) {
        MainWindow* mainWindow = qobject_cast<MainWindow*>(parentWindow);
        if (mainWindow) {
            // Access camera manager and audio manager from main window
            // We need to use the public interface to stop devices
            
            // This calls the main window's stop method which handles camera and audio
            QMetaObject::invokeMethod(mainWindow, "stop", Qt::DirectConnection);
        } else {
        }
    } else {
    }
    
}

void RenameDisplayDialog::hideMainWindow()
{
    QWidget *mainWindow = this->parentWidget();
    if (mainWindow) {
        mainWindow->hide();
    }
}

int RenameDisplayDialog::findEDIDBlock0(const QByteArray &firmwareData)
{
    // EDID header signature: 00 FF FF FF FF FF FF 00
    const QByteArray edidHeader = QByteArray::fromHex("00FFFFFFFFFFFF00");
    
    for (int i = 0; i <= firmwareData.size() - edidHeader.size(); ++i) {
        if (firmwareData.mid(i, edidHeader.size()) == edidHeader) {
            return i;
        }
    }
    
    return -1;
}

void RenameDisplayDialog::updateEDIDDisplayName(QByteArray &edidBlock, const QString &newName)
{
    // Search for existing display name descriptor (0xFC) or use the first available descriptor
    int targetDescriptorOffset = -1;
    
    // Check all 4 descriptors (offsets: 54, 72, 90, 108)
    for (int descriptorOffset = 54; descriptorOffset <= 54 + 3 * 18; descriptorOffset += 18) {
        if (descriptorOffset + 18 > edidBlock.size()) continue;
        
        // Check if this is a display name descriptor (0xFC)
        if (edidBlock[descriptorOffset] == 0x00 && 
            edidBlock[descriptorOffset + 1] == 0x00 && 
            edidBlock[descriptorOffset + 2] == 0x00 && 
            static_cast<quint8>(edidBlock[descriptorOffset + 3]) == 0xFC) {
            targetDescriptorOffset = descriptorOffset;
            break;
        }
    }
    
    // If no display name descriptor found, use the last descriptor (offset 108)
    if (targetDescriptorOffset == -1) {
        targetDescriptorOffset = 108;
    }
    
    if (targetDescriptorOffset + 18 > edidBlock.size()) {
        qWarning() << "EDID block too small for descriptor at offset" << targetDescriptorOffset;
        return;
    }
    
    QByteArray nameBytes = newName.toUtf8();
    if (nameBytes.size() > 13) {
        nameBytes = nameBytes.left(13); // Truncate to max length
    }
    
    nameBytes.append(0x0A);

    // Pad with spaces and terminate with 0x0A
    while (nameBytes.size() < 13) {
        nameBytes.append(0x20);
    }
    // nameBytes[nameBytes.size() - 1] = 0x0A; // Line feed terminator
    
    // Show descriptor BEFORE update
    QByteArray beforeDescriptor = edidBlock.mid(targetDescriptorOffset, 18);
    QString beforeHex;
    for (int i = 0; i < beforeDescriptor.size(); ++i) {
        beforeHex += QString("%1 ").arg(static_cast<quint8>(beforeDescriptor[i]), 2, 16, QChar('0')).toUpper();
    }
    
    // Set descriptor header for display name
    edidBlock[targetDescriptorOffset] = 0x00;
    edidBlock[targetDescriptorOffset + 1] = 0x00;
    edidBlock[targetDescriptorOffset + 2] = 0x00;
    edidBlock[targetDescriptorOffset + 3] = 0xFC; // Display name tag
    edidBlock[targetDescriptorOffset + 4] = 0x00;
    
    // Copy display name
    for (int i = 0; i < 13; ++i) {
        edidBlock[targetDescriptorOffset + 5 + i] = nameBytes[i];
    }
    
    // Show descriptor AFTER update
    QByteArray afterDescriptor = edidBlock.mid(targetDescriptorOffset, 18);
    QString afterHex;
    for (int i = 0; i < afterDescriptor.size(); ++i) {
        afterHex += QString("%1 ").arg(static_cast<quint8>(afterDescriptor[i]), 2, 16, QChar('0')).toUpper();
    }
    
}

quint8 RenameDisplayDialog::calculateEDIDChecksum(const QByteArray &edidBlock)
{
    if (edidBlock.size() != 128) {
        qWarning() << "Invalid EDID block size:" << edidBlock.size();
        return 0;
    }
    
    quint16 sum = 0;
    for (int i = 0; i < 127; ++i) { // Exclude the checksum byte itself
        sum += static_cast<quint8>(edidBlock[i]);
    }
    
    quint8 checksum = (256 - (sum & 0xFF)) & 0xFF;
    
    return checksum;
}

quint16 RenameDisplayDialog::calculateFirmwareChecksumWithDiff(const QByteArray &originalFirmware, const QByteArray &originalEDID, const QByteArray &modifiedEDID)
{
    if (originalFirmware.size() < 2) {
        qWarning() << "Firmware data too small for checksum calculation";
        return 0;
    }
    
    if (originalEDID.size() != modifiedEDID.size() || originalEDID.size() != 128) {
        qWarning() << "Invalid EDID block sizes for difference calculation";
        return 0;
    }
    
    // Get the original firmware checksum from the last 2 bytes
    quint8 originalLowByte = static_cast<quint8>(originalFirmware[originalFirmware.size() - 2]);
    quint8 originalHighByte = static_cast<quint8>(originalFirmware[originalFirmware.size() - 1]);
    
    // Determine byte order by checking which interpretation makes more sense
    // Try both little-endian and big-endian interpretations
    quint16 originalChecksumLE = originalLowByte | (originalHighByte << 8);  // Little-endian
    quint16 originalChecksumBE = (originalLowByte << 8) | originalHighByte;  // Big-endian
    
    // Calculate the sum difference between original and modified EDID blocks
    qint32 edidDifference = 0;
    for (int i = 0; i < 128; ++i) {
        edidDifference += static_cast<quint8>(modifiedEDID[i]) - static_cast<quint8>(originalEDID[i]);
    }
    
    // Calculate new checksum by adding the difference to the original checksum
    // We'll use big-endian format as it's more common for firmware checksums
    qint32 newChecksumInt = static_cast<qint32>(originalChecksumBE) + edidDifference;
    quint16 newChecksum = static_cast<quint16>(newChecksumInt & 0xFFFF);
    
    // Verify by showing byte breakdown
    
    return newChecksum;
}

QByteArray RenameDisplayDialog::processEDIDDisplayName(const QByteArray &firmwareData, const QString &newName)
{
    
    QByteArray modifiedFirmware = firmwareData; // Make a copy
    
    // Show complete firmware BEFORE update (first 256 bytes for debugging)
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
    showEDIDDescriptors(edidBlock);
    
    // Update display name in EDID block
    updateEDIDDisplayName(edidBlock, newName);
    
    // Show EDID descriptors AFTER update
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
        
        // Show actual bytes written
    } else {
        qWarning() << "Firmware too small to write checksum";
    }
    
    // Show complete firmware AFTER update (first 256 bytes for debugging)
    showFirmwareHexDump(modifiedFirmware, 0, qMin(256, modifiedFirmware.size()));
    
    // Also show the end of firmware (last 32 bytes) to verify checksum
    if (modifiedFirmware.size() > 32) {
        showFirmwareHexDump(modifiedFirmware, modifiedFirmware.size() - 32, 32);
    }
    
    return modifiedFirmware;
}

void RenameDisplayDialog::showEDIDDescriptors(const QByteArray &edidBlock)
{
    
    // Display descriptors starting at offset 54 (4 descriptors, each 18 bytes)
    for (int descriptorOffset = 54; descriptorOffset <= 54 + 3 * 18; descriptorOffset += 18) {
        if (descriptorOffset + 18 > edidBlock.size()) break;
        
        QByteArray descriptor = edidBlock.mid(descriptorOffset, 18);
        QString hexString;
        for (int i = 0; i < descriptor.size(); ++i) {
            hexString += QString("%1 ").arg(static_cast<quint8>(descriptor[i]), 2, 16, QChar('0')).toUpper();
        }
        
        // Check descriptor type
        quint8 descriptorType = static_cast<quint8>(descriptor[3]);
        if (descriptor[0] == 0x00 && descriptor[1] == 0x00 && descriptor[2] == 0x00) {
            switch (descriptorType) {
                case 0xFF:
                    break;
                case 0xFE:
                    break;
                case 0xFD:
                    break;
                case 0xFC:
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
                    }
                    break;
                case 0xFB:
                    break;
                case 0xFA:
                    break;
                default:
                    if (descriptorType == 0x00) {
                    } else {
                    }
                    break;
            }
        } else {
        }
    }
}

void RenameDisplayDialog::showFirmwareHexDump(const QByteArray &firmwareData, int startOffset, int length)
{
    if (startOffset < 0 || startOffset >= firmwareData.size()) {
        qWarning() << "Invalid start offset for firmware hex dump:" << startOffset;
        return;
    }
    
    int actualLength = length;
    if (actualLength == -1) {
        actualLength = firmwareData.size() - startOffset;
    } else {
        actualLength = qMin(actualLength, firmwareData.size() - startOffset);
    }
    
    // Show hex dump in 16-byte rows
    for (int i = 0; i < actualLength; i += 16) {
        QString line;
        QString asciiLine;
        
        // Offset
        line += QString("%1: ").arg(startOffset + i, 8, 16, QChar('0')).toUpper();
        
        // Hex bytes (16 per line)
        for (int j = 0; j < 16; ++j) {
            if (i + j < actualLength) {
                quint8 byte = static_cast<quint8>(firmwareData[startOffset + i + j]);
                line += QString("%1 ").arg(byte, 2, 16, QChar('0')).toUpper();
                
                // ASCII representation
                if (byte >= 32 && byte <= 126) {
                    asciiLine += static_cast<char>(byte);
                } else {
                    asciiLine += '.';
                }
            } else {
                line += "   "; // 3 spaces for missing byte
                asciiLine += ' ';
            }
            
            // Add extra space after 8 bytes for readability
            if (j == 7) {
                line += " ";
            }
        }
        
        // Add ASCII representation
        line += QString(" |%1|").arg(asciiLine);
        
    }
}
