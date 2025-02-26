#include "ui/envdialog.h"
#include "envdialog.h"
#include "ui_envdialog.h"
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
#include <cstdlib>
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

// Define the static commands
const QString EnvironmentSetupDialog::driverCommands = "# Build and install the driver\n make ; sudo make install\n\n";
const QString EnvironmentSetupDialog::groupCommands = "# Add user to dialout group\n sudo usermod -a -G dialout $USER\n\n";
const QString EnvironmentSetupDialog::udevCommands =
    "#Add udev rules for Openterface Mini-KVM\n"
    "echo 'KERNEL== \"hidraw*\", SUBSYSTEM==\"hidraw\", MODE=\"0666\"' | sudo tee /etc/udev/rules.d/51-openterface.rules\n"
    "echo 'SUBSYSTEM==\"usb\", ATTR{idVendor}==\"1a86\", ATTR{idProduct}==\"7523\", ENV{BRL TTY_BRAILLY_DRIVER}=\"none\"' | sudo tee -a /etc/udev/rules.d/51-openterface.rules"
    "sudo udevadm control --reload-rules\n"
    "sudo udevadm trigger\n\n";

bool EnvironmentSetupDialog::isDriverInstalled = false;
bool EnvironmentSetupDialog::isInRightUserGroup = false;
bool EnvironmentSetupDialog::isHidPermission = false;

EnvironmentSetupDialog::EnvironmentSetupDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::EnvironmentSetupDialog)  
{
    ui->setupUi(this);

#ifdef _WIN32
    setFixedSize(250, 120); 
    ui->step1Label->setVisible(false);
    ui->extractButton->setVisible(false);
    ui->step2Label->setVisible(false);
    ui->copyButton->setVisible(false);
    ui->commandsTextEdit->setVisible(false);
    ui->descriptionLabel->setText("The driver is missing. Openterface Mini-KVM will install it automatically.");
#else
    setFixedSize(450, 400); 
    ui->commandsTextEdit->setVisible(true); 
    ui->step1Label->setVisible(!isDriverInstalled);
    ui->extractButton->setVisible(!isDriverInstalled);
    ui->copyButton->setVisible(true);
    ui->step2Label->setVisible(true);
    ui->commandsTextEdit->setText(buildCommands());
    connect(ui->extractButton, &QPushButton::clicked, this, &EnvironmentSetupDialog::extractDriverFiles);
    connect(ui->copyButton, &QPushButton::clicked, this, &EnvironmentSetupDialog::copyCommands);

    // Create the status summary
    QString statusSummary = "The following steps help you install the driver and add user to correct group. Current status:\n";
    statusSummary += "‣ Driver Installed: " + QString(isDriverInstalled ? "✓" : "✗") + "\n";
    statusSummary += "‣ In Dialout Group: " + QString(isInRightUserGroup ? "✓" : "✗") + "\n";
    statusSummary += "‣ HID Permission: " + QString(isHidPermission ? "✓" : "✗") + "\n";
    ui->descriptionLabel->setText(statusSummary);
#endif
    // Connect buttons to their respective slots
    connect(ui->okButton, &QPushButton::clicked, this, &EnvironmentSetupDialog::accept); 
    connect(ui->quitButton, &QPushButton::clicked, this, &EnvironmentSetupDialog::reject); 
}

EnvironmentSetupDialog::~EnvironmentSetupDialog()
{
    delete ui;
}

// Override the closeEvent to prevent closing the dialog
void EnvironmentSetupDialog::closeEvent(QCloseEvent *event)
{
    event->ignore(); // Ignore the close event
}

#ifdef _WIN32
void EnvironmentSetupDialog::installDriverForWindows() {
    // Windows-specific installation logic
    std::cout << "Attempting to install driver using pnputil." << std::endl;
    QProcess::execute("pnputil.exe", QStringList() << "/add-driver" << "CH341SER.INF" << "/install");
    std::cout << "Driver installation command executed." << std::endl;
}
#endif

// Add the new method for extracting driver files
void EnvironmentSetupDialog::extractDriverFiles() {
    // Open a file dialog to select the destination directory
    QString selectedDir = QFileDialog::getExistingDirectory(this, "Select Destination Directory", QDir::homePath());

    if (selectedDir.isEmpty()) {
        // If no directory was selected, return early
        return;
    }

    QString tempDir = selectedDir + "/ch341-drivers"; // Create a subdirectory for the drivers
    QDir().mkpath(tempDir); // Create the temporary directory if it doesn't exist

    // List of resource files to copy
    QStringList files = {":/drivers/linux/ch341.c", ":/drivers/linux/ch341.h", ":/drivers/linux/Makefile"}; // Add all necessary files
    for (const QString &filePath : files) {
        QFile resourceFile(filePath);
        if (resourceFile.open(QIODevice::ReadOnly)) {
            QString targetPath = tempDir + "/" + QFileInfo(filePath).fileName();
            QFile targetFile(targetPath);
            if (targetFile.open(QIODevice::WriteOnly)) {
                targetFile.write(resourceFile.readAll()); // Read from resource and write to target
                targetFile.close();
                std::cout << "Copied " << QFileInfo(filePath).fileName().toStdString() << " to " << tempDir.toStdString() << std::endl;
            } else {
                std::cout << "Failed to open target file for writing: " << targetPath.toStdString() << std::endl;
            }
            resourceFile.close();
        } else {
            std::cout << "Failed to open resource file: " << filePath.toStdString() << std::endl;
        }
    }

    // Update the QTextEdit with the static commands
    ui->commandsTextEdit->setPlainText("cd " + tempDir + "\n" + buildCommands());
}

void EnvironmentSetupDialog::copyCommands() {
    // Copy the commands to the clipboard
    QClipboard *clipboard = QApplication::clipboard();
    clipboard->setText(ui->commandsTextEdit->toPlainText());
}

// Update the accept method to call the new installDriver method
void EnvironmentSetupDialog::accept()
{   
    checkEnvironmentSetup(); // Ensure the status variables are updated

    #ifdef _WIN32
    installDriverForWindows();
    #endif

    // Check the current status
    QString statusSummary;
    statusSummary += "Driver Installed: " + QString(isDriverInstalled ? "Yes" : "No") + "\n";
    statusSummary += "In Dialout Group: " + QString(isInRightUserGroup ? "Yes" : "No") + "\n";
    statusSummary += "HID Permission: " + QString(isHidPermission ? "Yes" : "No") + "\n";

    // Append the status summary to the description label
    ui->descriptionLabel->setText(ui->descriptionLabel->text() + "\n" + statusSummary);

    // Prompt user to restart computer
    QMessageBox::StandardButton reply = QMessageBox::question(
        this,
        "Restart Required",
        "The driver has been installed. A system restart and device re-plugging is required for the changes to take effect.\n\n"
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
            "Please remember to restart your computer and re-plug the device for the driver to work properly."
        );
    }

    // Call the base class accept method to close the dialog
    QDialog::accept();
}

QString EnvironmentSetupDialog::buildCommands(){
    QString commands = "";   
    if (!isDriverInstalled) {
        commands += driverCommands;
    }
    if (!isInRightUserGroup) {
        commands += groupCommands;
    }
    if (!isHidPermission) {
        commands += udevCommands;
    }

    return commands;
}

// Override reject method
void EnvironmentSetupDialog::reject()
{
    QDialog::reject();
}

bool EnvironmentSetupDialog::checkEnvironmentSetup() {
    #ifdef _WIN32
    return checkDriverInstalled();
    #elif defined(__linux__)
    std::cout << "Checking if MS2109 is on Linux." << std::endl;

    // If the device file does not exist, check using lsusb for VID and PID
    std::string command = "lsusb | grep -i '534d:2109'";
    int result = system(command.c_str());
    if (result == 0) {
        std::cout << "MS2109 not exist, so no Openterface plugged in" << std::endl;
        return true;
    }


    return checkDriverInstalled() && checkInRightUserGroup() && checkHidPermission();
    #else
    return true;
    #endif
}

bool EnvironmentSetupDialog::checkDriverInstalled() {
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
    isDriverInstalled = true;
    return true;
#elif defined(__linux__) // Check if compiling on Linux
    // Log the start of the driver check
    std::cout << "Checking if driver is installed on Linux." << std::endl;

    // If the device file does not exist, check using lsusb for VID and PID
    std::string command = "lsusb | grep -i '1a86:7523'";
    int result = system(command.c_str());
    if (result == 0) {
        std::cout << "Driver installation status: Installed (found via lsusb)" << std::endl;
        isDriverInstalled = true;
        return true; // Driver found via lsusb
    }

    std::cout << "Driver installation status: Not Installed" << std::endl;
    isDriverInstalled = false;
    return false; // Driver not found
#else
    // Implement logic for other platforms if needed
    std::cout << "Driver check not implemented for this platform." << std::endl;

    return false; // Assume not installed for non-Windows and non-Linux platforms
#endif
}

#ifdef __unix__
bool EnvironmentSetupDialog::checkInRightUserGroup() {
    // Check if the user is in the dialout group
    std::string command = "groups | grep -i dialout";
    int result = system(command.c_str());
    isInRightUserGroup = (result == 0); 
    return isInRightUserGroup;
}

bool EnvironmentSetupDialog::checkHidPermission() {
    // Check if the user has HID permission
    std::string command = "ls -l /dev/hidraw*";
    int result = system(command.c_str());
    isHidPermission = (result == 0); // Returns true if the user has HID permission
    return isHidPermission;
}

#endif