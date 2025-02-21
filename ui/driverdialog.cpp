#include "ui/driverdialog.h"
#include "ui_driverdialog.h"
#include <QPushButton> // Include QPushButton header
#include <QMessageBox> // Include QMessageBox header
#include <QCloseEvent> // Include QCloseEvent header
#include <QApplication> // Include QApplication header
#include <QProcess> // Include QProcess header
#include <QDir> // Include QDir for directory operations
#include <QFileInfo> // Include QFileInfo for file information
#include <QTextEdit> // Include QTextEdit for displaying text
#include <QSizePolicy> // Include QSizePolicy for setting size policy
#include <QFileDialog> // Include QFileDialog for file dialog
#include <QLabel> // Include QLabel for displaying labels
#include <QVBoxLayout> // Include QVBoxLayout for layout management
#include <QClipboard> // Include QClipboard for clipboard operations
#include <QHBoxLayout> // Include QHBoxLayout for horizontal layout
#ifdef _WIN32 // Check if compiling on Windows
#include <windows.h> // Include Windows API header
#include <setupapi.h> // Include SetupAPI for device installation functions
#include <devguid.h> // Include Device Guids
#include <regstr.h> // Include Registry strings
#include <iostream> // For std::cout
#endif

#ifdef __linux__ // Check if compiling on Linux
#include <iostream> // For std::cout
#include <fstream> // For file operations
#include <string> // For std::string
#endif

DriverDialog::DriverDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::DriverDialog)
{
    ui->setupUi(this);
    

#ifdef _WIN32
    setFixedSize(250, 120); // Set width to 250 and height to 120 for Windows
    ui->descriptionLabel->setText("The driver is missing. Openterface Mini-KVM will install it automatically.");
#else
    setFixedSize(400, 300); // Set width to 400 and height to 250 for Linux
    ui->descriptionLabel->setText("Driver Installation Instructions.");
    ui->commandsTextEdit->setVisible(true); 
    ui->step1Label->setVisible(true);
    ui->extractButton->setVisible(true);
    ui->step2Label->setVisible(true);
    ui->commandsTextEdit->setVisible(true);
    connect(ui->extractButton, &QPushButton::clicked, this, &DriverDialog::extractDriverFiles);
    connect(ui->copyButton, &QPushButton::clicked, this, &DriverDialog::copyCommands);
#endif
    // Connect buttons to their respective slots
    connect(ui->okButton, &QPushButton::clicked, this, &DriverDialog::accept); 
    connect(ui->quitButton, &QPushButton::clicked, this, &DriverDialog::reject); 
}

DriverDialog::~DriverDialog()
{
    delete ui;
}

// Override the closeEvent to prevent closing the dialog
void DriverDialog::closeEvent(QCloseEvent *event)
{
    event->ignore(); // Ignore the close event
}

#ifdef _WIN32
void DriverDialog::installDriverForWindows() {
    // Windows-specific installation logic
    std::cout << "Attempting to install driver using pnputil." << std::endl;
    QProcess::execute("pnputil.exe", QStringList() << "/add-driver" << "CH341SER.INF" << "/install");
    std::cout << "Driver installation command executed." << std::endl;
}
#endif

// Add the new method for extracting driver files
void DriverDialog::extractDriverFiles() {
    // Open a file dialog to select the destination directory
    QString selectedDir = QFileDialog::getExistingDirectory(this, "Select Destination Directory", QDir::homePath());

    if (selectedDir.isEmpty()) {
        // If no directory was selected, return early
        return;
    }

    QString tempDir = selectedDir + "/ch341-drivers"; // Create a subdirectory for the drivers
    QDir().mkpath(tempDir); // Create the temporary directory if it doesn't exist

    // Copy files from the resource path to the selected directory
    QStringList files = {":/drivers/linux/ch341.c", ":/drivers/linux/ch341.h", ":/drivers/linux/Makefile"}; // Add all necessary files
    for (const QString &filePath : files) {
        QFileInfo fileInfo(filePath);
        QString targetPath = tempDir + "/" + fileInfo.fileName();
        if (QFile::copy(filePath, targetPath)) {
            std::cout << "Copied " << fileInfo.fileName().toStdString() << " to " << tempDir.toStdString() << std::endl;
        } else {
            std::cout << "Failed to copy " << fileInfo.fileName().toStdString() << std::endl;
        }
    }

    // Update the commands to include the new path
    QString commands = "cd " + tempDir + "; make ; sudo make install";

    // Update the QTextEdit with the new commands
    ui->commandsTextEdit->setPlainText(commands); // Assuming commandsTextEdit is the name of your QTextEdit
}

void DriverDialog::copyCommands() {
    // Copy the commands to the clipboard
    QClipboard *clipboard = QApplication::clipboard();
    clipboard->setText(ui->commandsTextEdit->toPlainText());
}

// Update the accept method to call the new installDriver method
void DriverDialog::accept()
{   
    #ifdef _WIN32
    installDriverForWindows();
    #endif

    // Prompt user to restart computer
    QMessageBox::StandardButton reply = QMessageBox::question(
        this,
        "Restart Required",
        "The driver has been installed. A system restart is required for the changes to take effect.\n\n"
        "Would you like to restart your computer now?",
        QMessageBox::Yes | QMessageBox::No
    );

    if (reply == QMessageBox::Yes) {
#ifdef _WIN32
        // Execute system restart command
        QProcess::startDetached("shutdown", QStringList() << "-r" << "-t" << "0");
#elif defined(__linux__)
        // For Linux systems
        QProcess::startDetached("reboot");
#endif
    } else {
        QMessageBox::information(
            this,
            "Restart Later",
            "Please remember to restart your computer for the driver to work properly."
        );
    }

    // Call the base class accept method to close the dialog
    QDialog::accept();
}

// Override reject method
void DriverDialog::reject()
{
    QDialog::reject();
}

bool DriverDialog::isDriverInstalled() {
#ifdef _WIN32 // Check if compiling on Windows
    std::cout << "Checking if devices are present..." << std::endl;
    const GUID GUID_DEVINTERFACE_USB_DEVICE = { 0xA5DCBF10, 0x6530, 0x11D2, {0x90, 0x1F, 0x00, 0xC0, 0x4F, 0xB9, 0x51, 0xED} };
    HDEVINFO deviceInfoSet = SetupDiGetClassDevs(&GUID_DEVINTERFACE_USB_DEVICE, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (deviceInfoSet == INVALID_HANDLE_VALUE) {
        return false;
    }

    SP_DEVINFO_DATA deviceInfoData;
    deviceInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
    WCHAR hwIdBuffer[256];
    bool captureCardFound = false;
    bool ch341Found = false;

    for (DWORD i = 0; SetupDiEnumDeviceInfo(deviceInfoSet, i, &deviceInfoData); i++) {
        if (SetupDiGetDeviceRegistryPropertyW(deviceInfoSet, &deviceInfoData, SPDRP_HARDWAREID, NULL,
            (PBYTE)hwIdBuffer, sizeof(hwIdBuffer), NULL)) {
            if (wcsstr(hwIdBuffer, L"USB\\VID_534D&PID_2109") != NULL) {
                captureCardFound = true;
            }
            if (wcsstr(hwIdBuffer, L"USB\\VID_1A86&PID_7523") != NULL) {
                ch341Found = true;
            }
        }
    }

    SetupDiDestroyDeviceInfoList(deviceInfoSet);

    if (!captureCardFound && !ch341Found) {
        std::cout << "Neither device found - skipping driver check" << std::endl;
        return true;
    }
    if (captureCardFound && !ch341Found) {
        std::cout << "Capture card found but CH341 missing - need driver" << std::endl;
        return false;
    }
    std::cout << "Devices properly detected" << std::endl;
    return true;
#elif defined(__linux__) // Check if compiling on Linux
    // Log the start of the driver check
    std::cout << "Checking if driver is installed on Linux." << std::endl;

    // Check if the driver is loaded by looking for the device file
    std::ifstream deviceFile("/dev/ttyUSB0"); // Adjust the path as necessary for your driver
    bool isInstalled = deviceFile.good();
    std::cout << "Driver installation status: " << (isInstalled ? "Installed" : "Not Installed") << std::endl;
    return isInstalled; // Returns true if the device file exists
#else
    // Implement logic for other platforms if needed
    std::cout << "Driver check not implemented for this platform." << std::endl;

    return false; // Assume not installed for non-Windows and non-Linux platforms
#endif
}
