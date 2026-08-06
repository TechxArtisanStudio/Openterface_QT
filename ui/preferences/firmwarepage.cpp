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

#include "firmwarepage.h"
#include "../../video/videohid.h"
#include "../../video/firmwarereader.h"
#include "../../video/firmwarewriter.h"
#include "../../video/firmwareoperationmanager.h"
#include "../../video/ms2109.h"
#include "../../serial/SerialPortManager.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QFile>
#include <QDir>
#include <QMessageBox>
#include <QApplication>
#include <QThread>
#include <QCoreApplication>
#include <QDebug>

FirmwarePage::FirmwarePage(QWidget *parent)
    : QWidget(parent)
    , currentOperation(None)
    , workerThread(nullptr)
{
    setupUI();
    updateVersionDisplay();
}

FirmwarePage::~FirmwarePage()
{
    if (workerThread) {
        workerThread->requestInterruption();
        workerThread->quit();
        workerThread->wait();
    }
}

void FirmwarePage::setupUI()
{
    // Primary button — orange accent, works on both light and dark themes
    const QString primaryButtonStyle = R"(
        QPushButton {
            background-color: #e8841a;
            border: none;
            border-radius: 6px;
            padding: 6px 14px;
            color: white;
            font-weight: bold;
        }
        QPushButton:hover { background-color: #d4761a; }
        QPushButton:pressed { background-color: #c06818; }
        QPushButton:disabled { background-color: #b89060; color: #eeeeee; }
    )";

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(15);
    mainLayout->setContentsMargins(20, 20, 20, 20);

    // Title
    QLabel *titleLabel = new QLabel(tr("Firmware Management"), this);
    QFont titleFont = titleLabel->font();
    titleFont.setPointSize(titleFont.pointSize() + 2);
    titleFont.setBold(true);
    titleLabel->setFont(titleFont);
    mainLayout->addWidget(titleLabel);

    // Current version display
    versionLabel = new QLabel(this);
    mainLayout->addWidget(versionLabel);

    // Latest version display (from network)
    latestVersionLabel = new QLabel(tr("Latest Firmware Version: Checking..."), this);
    mainLayout->addWidget(latestVersionLabel);

    mainLayout->addSpacing(10);

    // Progress area (hidden by default) — between version info and buttons
    progressBar = new QProgressBar(this);
    progressBar->setRange(0, 100);
    progressBar->setValue(0);
    mainLayout->addWidget(progressBar);

    statusLabel = new QLabel(this);
    statusLabel->setWordWrap(true);
    mainLayout->addWidget(statusLabel);

    QHBoxLayout *cancelLayout = new QHBoxLayout();
    cancelLayout->addStretch();
    // Secondary buttons (cancel/backup/write) use global QPushButton stylesheet from main.cpp
    cancelButton = new QPushButton(tr("Cancel"), this);
    cancelButton->setFixedSize(80, 30);
    connect(cancelButton, &QPushButton::clicked, this, &FirmwarePage::onCancelClicked);
    cancelLayout->addWidget(cancelButton);
    mainLayout->addLayout(cancelLayout);

    progressBar->setVisible(false);
    statusLabel->setVisible(false);
    cancelButton->setVisible(false);

    // Push buttons to the bottom
    mainLayout->addStretch();

    // Operation buttons — horizontal, at the bottom
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(15);

    updateButton = new QPushButton(tr("Firmware Update from Remote"), this);
    updateButton->setToolTip(tr("Check the network for the latest firmware version and install it"));
    updateButton->setStyleSheet(primaryButtonStyle);
    connect(updateButton, &QPushButton::clicked, this, &FirmwarePage::onCheckForUpdatesClicked);
    buttonLayout->addWidget(updateButton);

    backupButton = new QPushButton(tr("Backup Firmware to .bin File"), this);
    backupButton->setToolTip(tr("Read the current firmware from EEPROM and save it to a .bin file"));
    connect(backupButton, &QPushButton::clicked, this, &FirmwarePage::onBackupFirmwareClicked);
    buttonLayout->addWidget(backupButton);

    writeButton = new QPushButton(tr("Write Firmware from .bin File"), this);
    writeButton->setToolTip(tr("Write a firmware .bin file to the device EEPROM"));
    connect(writeButton, &QPushButton::clicked, this, &FirmwarePage::onWriteFirmwareClicked);
    buttonLayout->addWidget(writeButton);

    mainLayout->addLayout(buttonLayout);
}

void FirmwarePage::updateVersionDisplay()
{
    std::string version = VideoHid::getInstance().getFirmwareVersion();
    versionLabel->setText(tr("Current Firmware Version: ") + QString::fromStdString(version));

    // Show "Checking..." while fetching latest version from network
    latestVersionLabel->setText(tr("Latest Firmware Version: Checking..."));
    fetchLatestVersionAsync();
}

void FirmwarePage::fetchLatestVersionAsync()
{
    // Run the network check in a background thread
    QThread *thread = new QThread();

    // Use a lambda to perform the check
    QObject *worker = new QObject();
    connect(thread, &QThread::started, worker, [this, worker]() {
        // Call isLatestFirmware() to populate the latest version
        VideoHid::getInstance().isLatestFirmware();
        // Emit signal to update the label on the GUI thread
        QMetaObject::invokeMethod(this, "onLatestVersionFetched", Qt::QueuedConnection);
        worker->deleteLater();
    });
    connect(thread, &QThread::finished, thread, &QThread::deleteLater);

    worker->moveToThread(thread);
    thread->start();
}

void FirmwarePage::onLatestVersionFetched()
{
    std::string latestVersion = VideoHid::getInstance().getLatestFirmwareVersion();
    if (latestVersion.empty()) {
        latestVersionLabel->setText(tr("Latest Firmware Version: Unknown (network error)"));
    } else {
        latestVersionLabel->setText(tr("Latest Firmware Version: ") + QString::fromStdString(latestVersion));
    }
}

void FirmwarePage::startOperation(OperationType type)
{
    currentOperation = type;

    // Disable all operation buttons
    updateButton->setEnabled(false);
    backupButton->setEnabled(false);
    writeButton->setEnabled(false);

    // Show progress area
    progressBar->setVisible(true);
    statusLabel->setVisible(true);
    cancelButton->setVisible(true);
    progressBar->setValue(0);

    switch (type) {
    case Update:
        statusLabel->setText(tr("Checking for firmware update..."));
        break;
    case Backup:
        statusLabel->setText(tr("Reading firmware from EEPROM..."));
        break;
    case Write:
        statusLabel->setText(tr("Writing firmware to EEPROM..."));
        break;
    default:
        break;
    }
}

void FirmwarePage::finishOperation(bool success)
{
    // Re-enable all operation buttons
    updateButton->setEnabled(true);
    backupButton->setEnabled(true);
    writeButton->setEnabled(true);

    cancelButton->setVisible(false);
    currentOperation = None;

    if (success) {
        statusLabel->setText(tr("Operation completed successfully."));
    } else {
        statusLabel->setText(tr("Operation failed."));
    }

    // Hide progress area after a delay
    QTimer::singleShot(2000, this, [this]() {
        if (currentOperation == None) {
            progressBar->setVisible(false);
            statusLabel->setVisible(false);
        }
    });
}

void FirmwarePage::onProgressUpdate(int value)
{
    progressBar->setValue(value);
    progressBar->repaint();
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
}

void FirmwarePage::onOperationComplete(bool success)
{
    // Clean up worker thread
    if (workerThread) {
        workerThread->quit();
        workerThread->wait();
        workerThread->deleteLater();
        workerThread = nullptr;
    }

    if (success) {
        progressBar->setValue(100);

        if (currentOperation == Update || currentOperation == Write) {
            statusLabel->setText(tr("Firmware operation completed successfully."));
            QMessageBox::information(this, tr("Success"),
                tr("Firmware operation completed successfully.\n\n"
                   "The application will now close.\n"
                   "Please:\n"
                   "1. Restart the application\n"
                   "2. Disconnect and reconnect all cables"));
            emit firmwareUpdateCompleted();
        } else {
            // Backup success
            statusLabel->setText(tr("Firmware backup completed successfully."));
            QMessageBox::information(this, tr("Success"),
                tr("Firmware backup completed successfully."));
            finishOperation(true);
        }
    } else {
        statusLabel->setText(tr("Operation failed."));

        QMessageBox::critical(this, tr("Error"),
            tr("Firmware operation failed.\nPlease try again."));
        finishOperation(false);
    }
}

void FirmwarePage::onCancelClicked()
{
    if (workerThread) {
        workerThread->requestInterruption();
        workerThread->quit();
        workerThread->wait();
        workerThread->deleteLater();
        workerThread = nullptr;
    }

    QMessageBox::warning(this, tr("Cancelled"),
        tr("Firmware operation was cancelled."));
    finishOperation(false);
}

void FirmwarePage::onCheckForUpdatesClicked()
{
    qDebug() << "Checking for latest firmware version...";
    FirmwareResult firmwareStatus = VideoHid::getInstance().isLatestFirmware();
    std::string currentVersion = VideoHid::getInstance().getCurrentFirmwareVersion();
    std::string latestVersion = VideoHid::getInstance().getLatestFirmwareVersion();
    qDebug() << "latestFirmwareVersion" << latestVersion.c_str();

    switch (firmwareStatus) {
    case FirmwareResult::Latest:
        qDebug() << "Firmware is up to date.";
        QMessageBox::information(this, tr("Firmware Update"),
            tr("The firmware is up to date.\nCurrent version: ") +
            QString::fromStdString(currentVersion));
        break;

    case FirmwareResult::Upgradable: {
        qDebug() << "Firmware is upgradable.";
        QString message = tr("Current firmware version: ") + QString::fromStdString(currentVersion) + tr("\n") +
                         tr("Latest firmware version: ") + QString::fromStdString(latestVersion) + tr("\n\n") +
                         tr("The update process will:\n") +
                         tr("1. Stop all video and USB operations\n"
                         "2. Install new firmware\n"
                         "3. Close the application automatically\n\n"
                         "Important:\n"
                         "• Use a high-quality USB cable for host connection\n"
                         "• Disconnect the HDMI cable\n"
                         "• Do not interrupt power during update\n"
                         "• Restart application after completion\n\n"
                         "Do you want to proceed with the update?");

        int ret = QMessageBox::question(this, tr("Firmware Update Confirmation"),
            message, QMessageBox::Yes | QMessageBox::No);

        if (ret != QMessageBox::Yes) {
            qDebug() << "User cancelled firmware update";
            break;
        }

        qDebug() << "User accepted firmware update, proceeding...";
        startOperation(Update);

        try {
            // Stop services
            qDebug() << "Stopping main window operations first...";
            try {
                VideoHid::getInstance().stop();
                qDebug() << "Main window operations stopped successfully";
            } catch (...) {
                qWarning() << "Exception while stopping main window operations - continuing anyway";
            }

            qDebug() << "Stopping video HID polling only...";
            try {
                VideoHid::getInstance().stopPollingOnly();
                qDebug() << "Video HID polling stopped successfully";
            } catch (...) {
                qWarning() << "Exception while stopping video HID polling - continuing anyway";
            }

            QThread::msleep(300);
            QCoreApplication::processEvents();

            qDebug() << "Stopping serial port manager...";
            try {
                SerialPortManager::getInstance().closePort();
                qDebug() << "Serial port closed successfully";
                QThread::msleep(200);
                QCoreApplication::processEvents();
            } catch (...) {
                qWarning() << "Exception while stopping SerialPortManager - continuing anyway";
            }

            QCoreApplication::processEvents();
            QThread::msleep(200);

            qDebug() << "Services stopped successfully, proceeding with firmware update...";

            statusLabel->setText(tr("Updating firmware... Please do not disconnect the device."));

            // Start firmware update
            VideoHid::getInstance().loadFirmwareToEeprom();

            // Connect progress signals
            FirmwareOperationManager* mgr = VideoHid::getInstance().getFirmwareOperationManager();
            connect(mgr, &FirmwareOperationManager::progress,
                    this, &FirmwarePage::onProgressUpdate);
            connect(mgr, &FirmwareOperationManager::writeCompleted,
                    this, &FirmwarePage::onOperationComplete);

        } catch (const std::exception& e) {
            qCritical() << "Exception during firmware update process:" << e.what();
            QMessageBox::critical(this, tr("Error"),
                tr("An error occurred during firmware update:\n%1").arg(e.what()));
            finishOperation(false);
        } catch (...) {
            qCritical() << "Unknown exception during firmware update process";
            QMessageBox::critical(this, tr("Error"),
                tr("An unknown error occurred during firmware update."));
            finishOperation(false);
        }
        break;
    }

    case FirmwareResult::Timeout:
        qDebug() << "Firmware fetch timeout.";
        QMessageBox::warning(this, tr("Firmware Update"),
            tr("Firmware retrieval timed out. Please check your network connection and try again.\nCurrent version: ") +
            QString::fromStdString(currentVersion));
        break;
    }
}

void FirmwarePage::onBackupFirmwareClicked()
{
    QString path = selectSavePath();
    if (path.isEmpty()) {
        return;
    }

    startOperation(Backup);

    // Create worker thread for firmware read
    quint32 firmwareSize = VideoHid::getInstance().readFirmwareSize();
    workerThread = new QThread();
    FirmwareReader* worker = new FirmwareReader(
        &VideoHid::getInstance(), ADDR_EEPROM, firmwareSize, path, nullptr);
    worker->moveToThread(workerThread);

    connect(workerThread, &QThread::started, worker, &FirmwareReader::process);
    connect(worker, &FirmwareReader::progress, this, &FirmwarePage::onProgressUpdate);
    connect(worker, &FirmwareReader::finished, this, [this, path](bool success, const QByteArray &) {
        if (success) {
            QMessageBox::information(this, tr("Success"),
                tr("Firmware read and saved successfully to: ") + path);
        }
        onOperationComplete(success);
    });
    connect(worker, &FirmwareReader::error, this, [this](const QString& errorMessage) {
        QMessageBox::critical(this, tr("Error"), errorMessage);
        finishOperation(false);
    });
    connect(worker, &FirmwareReader::finished, worker, &FirmwareReader::deleteLater);

    workerThread->start();
}

void FirmwarePage::onWriteFirmwareClicked()
{
    QString path = selectFirmwareFile();
    if (path.isEmpty()) {
        QMessageBox::warning(this, tr("Warning"), tr("Please select a firmware file to write"));
        return;
    }

    QByteArray firmware = readBinFileToByteArray(path);
    if (firmware.isEmpty()) {
        QMessageBox::critical(this, tr("Error"), tr("Failed to read firmware file: %1").arg(path));
        return;
    }

    startOperation(Write);

    if (VideoHid::getInstance().getChipType() == VideoChipType::MS2130S) {
        qDebug() << "MS2130S detected - using erase+4096B burst firmware write path";
    }

    // Create worker thread for firmware write
    workerThread = new QThread();
    FirmwareWriter* worker = new FirmwareWriter(
        &VideoHid::getInstance(), ADDR_EEPROM, firmware, nullptr);
    worker->moveToThread(workerThread);

    connect(workerThread, &QThread::started, worker, &FirmwareWriter::process);
    connect(worker, &FirmwareWriter::progress, this, &FirmwarePage::onProgressUpdate);
    connect(worker, &FirmwareWriter::finished, this, &FirmwarePage::onOperationComplete);
    connect(worker, &FirmwareWriter::finished, worker, &FirmwareWriter::deleteLater);

    workerThread->start();
}

QByteArray FirmwarePage::readBinFileToByteArray(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Can't open bin file:" << filePath;
        return QByteArray();
    }
    QByteArray byteArray = file.readAll();
    return byteArray;
}

QString FirmwarePage::selectFirmwareFile()
{
    return QFileDialog::getOpenFileName(
        this,
        tr("Open Firmware File"),
        QDir::currentPath(),
        tr("Firmware Files (*.bin);;All Files (*)")
    );
}

QString FirmwarePage::selectSavePath()
{
    return QFileDialog::getSaveFileName(
        this,
        tr("Save Firmware File"),
        "openterface.bin",
        tr("Firmware Files (*.bin);;All Files (*)")
    );
}
