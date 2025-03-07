#include "firmwareupdatedialog.h"
#include <QMessageBox>
#include <QDebug>

FirmwareUpdateDialog::FirmwareUpdateDialog(QWidget *parent)
    : QDialog(parent, Qt::WindowTitleHint | Qt::CustomizeWindowHint), updateResult(false)
{
    setWindowTitle("Firmware Update");
    setMinimumWidth(400);
    setModal(true);
    
    // Create UI elements
    statusLabel = new QLabel("Preparing firmware update...");
    statusLabel->setWordWrap(true);
    
    progressBar = new QProgressBar();
    progressBar->setRange(0, 100);
    progressBar->setValue(0);
    
    closeButton = new QPushButton("Close");
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
    statusLabel->setText("Updating firmware... Please do not disconnect the device.");

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
        statusLabel->setText("Firmware update completed successfully.\nThe application will close. Please restart it to apply the new firmware.");
        QMessageBox::information(this, "Firmware Update", 
                                "Firmware update completed successfully.\n\n"
                                "The application will now close. Please restart it to apply the new firmware.");
    } else {
        statusLabel->setText("Firmware update failed. Please try again.");
        QMessageBox::critical(this, "Firmware Update Failed", 
                             "An error occurred during the firmware update.\n\n"
                             "Please try again after restarting the application.");
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

