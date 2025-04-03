#include "firmwareupdatedialog.h"
#include <QMessageBox>
#include <QDebug>

FirmwareUpdateDialog::FirmwareUpdateDialog(QWidget *parent)
    : QDialog(parent, Qt::WindowTitleHint | Qt::CustomizeWindowHint), updateResult(false)
{
    setWindowTitle(tr("Firmware Update"));
    setMinimumWidth(400);
    setModal(true);
    
    // Create UI elements
    statusLabel = new QLabel(tr("Preparing firmware update..."));
    statusLabel->setWordWrap(true);
    
    progressBar = new QProgressBar();
    progressBar->setRange(0, 100);
    progressBar->setValue(0);
    
    closeButton = new QPushButton(tr("Close"));
    closeButton->setEnabled(false);
    
    // Layout
    QVBoxLayout *layout = new QVBoxLayout();
    layout->addWidget(statusLabel);
    layout->addWidget(progressBar);
    layout->addWidget(closeButton);
    
    setLayout(layout);
    
    // Connect signals and slots
    connect(closeButton, &QPushButton::clicked, this, &FirmwareUpdateDialog::onCloseButtonClicked);
    
    progressTimer = new QTimer(this);
    connect(progressTimer, &QTimer::timeout, this, &FirmwareUpdateDialog::onProgressTimerTimeout);
}

FirmwareUpdateDialog::~FirmwareUpdateDialog()
{
    if (progressTimer) {
        progressTimer->stop();
        delete progressTimer;
    }
}

void FirmwareUpdateDialog::onCloseButtonClicked()
{
    if (updateResult) {
        QApplication::quit();
    } else {
        reject();
    }
}

void FirmwareUpdateDialog::onProgressTimerTimeout()
{
    int currentValue = progressBar->value();
    if (currentValue < 95) {
        updateProgress(currentValue + 5);
    }
}

bool FirmwareUpdateDialog::startUpdate()
{
    statusLabel->setText(tr("Updating firmware... Please do not disconnect the device."));

    connect(&VideoHid::getInstance(), &VideoHid::firmwareWriteProgress, this, &FirmwareUpdateDialog::updateProgress);
    connect(&VideoHid::getInstance(), &VideoHid::firmwareWriteComplete, this, &FirmwareUpdateDialog::updateComplete);
    VideoHid::getInstance().loadFirmwareToEeprom();
    
    exec();
    return updateResult;
}

void FirmwareUpdateDialog::updateProgress(int value)
{
    progressBar->setValue(value);
}

void FirmwareUpdateDialog::updateComplete(bool success)
{
    updateResult = success;
    
    if (success) {
        statusLabel->setText(tr("Firmware update completed successfully.\nThe application will close. Please restart it to apply the new firmware."));
        QMessageBox::information(this, tr("Firmware Update"), 
                    tr("Firmware update completed successfully.\n\n"
                    "The application will now close.\n"
                    "Please:\n"
                    "1. Restart the application\n"
                    "2. Disconnect and reconnect all cables"));
    } else {
        statusLabel->setText(tr("Firmware update failed. Please try again."));
        QMessageBox::critical(this, tr("Firmware Update Failed"), 
                             tr("An error occurred during the firmware update.\n\n"
                             "Please try again after restarting the application."));
    }
    
    closeButton->setEnabled(true);
    emit updateFinished(success);
    
    if (success) {
        // Give user a moment to see the success message before auto-closing
        QTimer::singleShot(2000, this, []() {
            QApplication::quit();
        });
    }
}

FirmwareUpdateConfirmDialog::FirmwareUpdateConfirmDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Firmware Update Confirmation"));
    setMinimumWidth(400);
    
    // Create UI elements
    messageLabel = new QLabel();
    messageLabel->setWordWrap(true);
    
    okButton = new QPushButton(tr("Update"));
    cancelButton = new QPushButton(tr("Cancel"));
    
    // Layout
    QVBoxLayout *mainLayout = new QVBoxLayout();
    mainLayout->addWidget(messageLabel);
    
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    buttonLayout->addWidget(okButton);
    buttonLayout->addWidget(cancelButton);
    
    mainLayout->addLayout(buttonLayout);
    setLayout(mainLayout);
    
    // Connect signals and slots
    connect(okButton, &QPushButton::clicked, this, &FirmwareUpdateConfirmDialog::accept);
    connect(cancelButton, &QPushButton::clicked, this, &FirmwareUpdateConfirmDialog::reject);
}

FirmwareUpdateConfirmDialog::~FirmwareUpdateConfirmDialog()
{
    // Qt handles deletion of child widgets
}

bool FirmwareUpdateConfirmDialog::showConfirmDialog(const std::string& currentVersion, const std::string& latestVersion)
{
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
    
    messageLabel->setText(message);
    
    int result = exec();
    return result == QDialog::Accepted;
}