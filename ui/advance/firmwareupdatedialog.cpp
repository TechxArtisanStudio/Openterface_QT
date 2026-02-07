#include "firmwareupdatedialog.h"
#include <QMessageBox>
#include <QDebug>

FirmwareUpdateDialog::FirmwareUpdateDialog(QWidget *parent)
    : QDialog(parent, Qt::WindowTitleHint | Qt::CustomizeWindowHint), updateResult(false)
{
    qDebug() << "FirmwareUpdateDialog constructor called";
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

void FirmwareUpdateDialog::beginLoad()
{
    qDebug() << "FirmwareUpdateDialog::beginLoad() called - starting firmware load";
    // Kick off the firmware write operation
    VideoHid::getInstance().loadFirmwareToEeprom();
    qDebug() << "VideoHid::loadFirmwareToEeprom() called";
}

#include <QCoreApplication>

bool FirmwareUpdateDialog::startUpdate()
{
    qDebug() << "FirmwareUpdateDialog::startUpdate() called";
    statusLabel->setText(tr("Updating firmware... Please do not disconnect the device."));

    // Connect signals before starting
    connect(&VideoHid::getInstance(), &VideoHid::firmwareWriteProgress, this, &FirmwareUpdateDialog::updateProgress);
    connect(&VideoHid::getInstance(), &VideoHid::firmwareWriteComplete, this, &FirmwareUpdateDialog::updateComplete);
    qDebug() << "Signals connected, showing dialog";

    // Show the dialog first so the user sees it immediately, then start the firmware load
    // on the next event loop iteration to ensure the UI is visible before the write starts.
    this->show();
    QTimer::singleShot(0, this, &FirmwareUpdateDialog::beginLoad);

    exec();
    return updateResult;
}

void FirmwareUpdateDialog::updateProgress(int value)
{
    // Update value
    progressBar->setValue(value);
    // Force an immediate repaint so progress is visible even if heavy work is blocking
    progressBar->repaint();
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
}

void FirmwareUpdateDialog::updateComplete(bool success)
{
    updateResult = success;
    
    // Disconnect the signals to prevent further updates
    disconnect(&VideoHid::getInstance(), &VideoHid::firmwareWriteProgress, this, &FirmwareUpdateDialog::updateProgress);
    disconnect(&VideoHid::getInstance(), &VideoHid::firmwareWriteComplete, this, &FirmwareUpdateDialog::updateComplete);
    
    if (success) {
        statusLabel->setText(tr("Firmware update completed successfully.\nThe application will close. Please restart it to apply the new firmware."));
        QMessageBox::information(this, tr("Firmware Update"), 
                    tr("Firmware update completed successfully.\n\n"
                    "The application will now close.\n"
                    "Please:\n"
                    "1. Restart the application\n"
                    "2. Disconnect and reconnect all cables"));
        
        // Stop VideoHid after successful firmware update
        VideoHid::getInstance().stop();
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
    : QDialog(parent), m_accepted(false)
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
    
    // Connect to custom slots for better debugging
    connect(okButton, &QPushButton::clicked, this, &FirmwareUpdateConfirmDialog::onOkClicked);
    connect(cancelButton, &QPushButton::clicked, this, &FirmwareUpdateConfirmDialog::onCancelClicked);
}

FirmwareUpdateConfirmDialog::~FirmwareUpdateConfirmDialog()
{
    // Qt handles deletion of child widgets
}

void FirmwareUpdateConfirmDialog::onOkClicked()
{
    qDebug() << "OK button clicked - proceeding with firmware update";
    m_accepted = true;
    accept();
}

void FirmwareUpdateConfirmDialog::onCancelClicked()
{
    qDebug() << "Cancel button clicked - aborting firmware update";
    m_accepted = false;
    reject();
}

bool FirmwareUpdateConfirmDialog::showConfirmDialog(const std::string& currentVersion, const std::string& latestVersion)
{
    qDebug() << "Current firmware version: " << QString::fromStdString(currentVersion);
    qDebug() << "Latest firmware version: " << QString::fromStdString(latestVersion);
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
    
    qDebug() << "About to show confirmation dialog";
    m_accepted = false; // Reset before showing
    
    int result = exec();
    
    qDebug() << "Dialog closed with result:" << result;
    qDebug() << "QDialog::Accepted =" << QDialog::Accepted;
    qDebug() << "QDialog::Rejected =" << QDialog::Rejected;
    qDebug() << "m_accepted =" << m_accepted;
    
    return m_accepted;
}