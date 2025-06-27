#include "firmwaremanagerdialog.h"
#include "video/videohid.h"
#include "video/firmwarereader.h"
#include "video/firmwarewriter.h"
#include "video/ms2109.h"
#include "serial/SerialPortManager.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QFile>
#include <QString>
#include <vector>
#include <QThread>
#include <QMessageBox>
#include <QApplication>


FirmwareManagerDialog::FirmwareManagerDialog(QWidget *parent) :
    QDialog(parent),
    progressDialog(nullptr)
{
    this->resize(200, 130);
    this->setWindowTitle("Firmware Manager");

    std::string curreantFirmwareVersion = VideoHid::getInstance().getFirmwareVersion();
    QVBoxLayout *verticalLayout = new QVBoxLayout(this);
    verticalLayout->setSpacing(15);
    verticalLayout->setContentsMargins(20, 20, 20, 20);

    versionLabel = new QLabel("Current Firmware Version: " + QString::fromStdString(curreantFirmwareVersion), this);
    verticalLayout->addWidget(versionLabel);

    QHBoxLayout *horzontal = new QHBoxLayout;

    readLoacalFirmwareBtn = new QPushButton("Restore firmware", this);
    connect(readLoacalFirmwareBtn, &QPushButton::clicked, this, &FirmwareManagerDialog::onReadFromFileClicked);
    horzontal->addWidget(readLoacalFirmwareBtn);

    writeFirmwareFromFileBtn = new QPushButton("Write firmware from bin", this);
    connect(writeFirmwareFromFileBtn, &QPushButton::clicked, this, &FirmwareManagerDialog::onWriteFirmwareFromFileClick);
    horzontal->addWidget(writeFirmwareFromFileBtn);

    verticalLayout->addLayout(horzontal);
}

FirmwareManagerDialog::~FirmwareManagerDialog() {
    if (progressDialog) {
        delete progressDialog;
        progressDialog = nullptr;
    }
}

QByteArray FirmwareManagerDialog::readBinFileToByteArray(const QString &filePath){
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning("Can't open bin file");
        return NULL;
    }
    QByteArray byteArray = file.readAll();
    return byteArray;
}

void FirmwareManagerDialog::onWriteFirmwareFromFileClick() {
    QString path = selectFirmware();
    if (path.isEmpty()) {
        QMessageBox::warning(this, tr("Warning"), tr("Please select a firmware file to write"));
        return;
    }

    QByteArray firmware = readBinFileToByteArray(path);
    if (firmware.isEmpty()) {
        QMessageBox::critical(this, tr("Error"), tr("Failed to read firmware file: %1").arg(path));
        return;
    }

    QWidget *mainWindow = this->parentWidget();
    if (mainWindow){
        VideoHid::getInstance().stop();
        SerialPortManager::getInstance().stop();
        mainWindow->close();
    }


    // Create and configure the progress dialog
    progressDialog = new QProgressDialog("Writing firmware to EEPROM...", "Cancel", 0, 100, this);
    progressDialog->setWindowModality(Qt::WindowModal);
    progressDialog->setAutoClose(false);
    progressDialog->setAutoReset(false);
    progressDialog->show();

    // Create the FirmwareWriter
    QThread* thread = new QThread();
    FirmwareWriter* worker = new FirmwareWriter(&VideoHid::getInstance(), ADDR_EEPROM, firmware, this);
    worker->moveToThread(thread);

    // Connect signals for progress, completion, and error handling
    connect(thread, &QThread::started, worker, &FirmwareWriter::process);
    connect(worker, &FirmwareWriter::progress, progressDialog, &QProgressDialog::setValue);
    connect(worker, &FirmwareWriter::finished, this, [=](bool success) {
        progressDialog->setValue(100); // Ensure progress bar reaches 100% on completion
        if (success) {
            QMessageBox::information(this, tr("Success"), tr("Firmware written successfully to EEPROM"
            "The application will now close.\n"
            "Please:\n"
            "1. Restart the application\n"
            "2. Disconnect and reconnect all cables"));
            QApplication::quit();
        } else {
            QMessageBox::critical(this, tr("Error"), tr("Failed to write firmware to EEPROM\n"
            "Please try again"));
        }
        progressDialog->deleteLater();
        progressDialog = nullptr;
    });
    connect(worker, &FirmwareWriter::finished, thread, &QThread::quit);
    connect(worker, &FirmwareWriter::finished, worker, &FirmwareWriter::deleteLater);
    connect(thread, &QThread::finished, thread, &QThread::deleteLater);
    connect(progressDialog, &QProgressDialog::canceled, this, [=]() {
        thread->requestInterruption();
        thread->quit();
        thread->wait();
        progressDialog->deleteLater();
        progressDialog = nullptr;
        QMessageBox::warning(this, tr("Cancelled"), tr("Firmware write operation was cancelled"));
    });

    thread->start();
}

QString FirmwareManagerDialog::selectFirmware(){
    QString fileName = QFileDialog::getOpenFileName(
        nullptr,
        "Open Firmware File",
        QDir::currentPath(),
        "Firmware Files (*.bin);;All Files (*)"
    );
    if (!fileName.isEmpty()) {
        return fileName;
    }
    return QString("");
}

QString FirmwareManagerDialog::onSelectPathClicked() {
    QString defaultFileName = "openterface.bin";
    QString filePath = QFileDialog::getSaveFileName(
        this,
        "Save Firmware File",
        defaultFileName,
        "Firmware Files (*.bin);;All Files (*)"
    );
    if (!filePath.isEmpty()) {
        return filePath;
    }
    return QString("");
}

void FirmwareManagerDialog::onReadFromFileClicked() {
    qDebug() << "onReadFromFileClicked";
    QString path = onSelectPathClicked();
    if (path.isEmpty()){
        QMessageBox::warning(this, tr("Warning"), tr("Please select a file path"));
        return;
    }

    QWidget *mainWindow = this->parentWidget();
    if (mainWindow){
        VideoHid::getInstance().stop();
        SerialPortManager::getInstance().stop();
        mainWindow->close();
    }

    // Create and configure the progress dialog
    progressDialog = new QProgressDialog("Reading firmware from EEPROM...", "Cancel", 0, 100, this);
    progressDialog->setWindowModality(Qt::WindowModal);
    progressDialog->setAutoClose(false);
    progressDialog->setAutoReset(false);
    progressDialog->show();

    // Create the FirmwareReader
    quint32 firmwareSize = VideoHid::getInstance().readFirmwareSize();
    QThread* thread = new QThread();
    FirmwareReader* worker = new FirmwareReader(&VideoHid::getInstance(), ADDR_EEPROM, firmwareSize, path, this);
    worker->moveToThread(thread);

    // Connect signals for progress, completion, and error handling
    connect(thread, &QThread::started, worker, &FirmwareReader::process);
    connect(worker, &FirmwareReader::progress, progressDialog, &QProgressDialog::setValue);
    connect(worker, &FirmwareReader::finished, this, [=](bool success) {
        progressDialog->setValue(100); // Ensure progress bar reaches 100% on completion
        if (success) {
            QMessageBox::information(this, "Success", "Firmware read and saved successfully to: " + path +"\nYou can restart the app or wirte the firmware");
        } else {
            QMessageBox::critical(this, "Error", "Failed to read and save firmware.");
        }
        progressDialog->deleteLater();
        progressDialog = nullptr;
    });
    connect(worker, &FirmwareReader::error, this, [=](const QString& errorMessage) {
        QMessageBox::critical(this, "Error", errorMessage);
        progressDialog->deleteLater();
        progressDialog = nullptr;
    });
    connect(worker, &FirmwareReader::finished, thread, &QThread::quit);
    connect(worker, &FirmwareReader::finished, worker, &FirmwareReader::deleteLater);
    connect(thread, &QThread::finished, thread, &QThread::deleteLater);
    connect(progressDialog, &QProgressDialog::canceled, this, [=]() {
        thread->requestInterruption();
        thread->quit();
        thread->wait();
        progressDialog->deleteLater();
        progressDialog = nullptr;
        QMessageBox::warning(this, "Cancelled", "Firmware read operation was cancelled.");
    });

    thread->start();
}
