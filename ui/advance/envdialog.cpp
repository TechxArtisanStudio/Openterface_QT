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
#include <QSettings>
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
#include <QDesktopServices> // Add this for opening URLs
#include <QUrl> // Add this for handling URLs
#include <QLabel> // Already included, but noting it's used for hyperlink

bool EnvironmentSetupDialog::isDriverInstalled = false;
const QString EnvironmentSetupDialog::tickHtml = "<span style='color: green; font-size: 16pt'>✓</span>";
const QString EnvironmentSetupDialog::crossHtml = "<span style='color: red; font-size: 16pt'>✗</span>";

#ifdef __linux__
// Define the static commands
const QString EnvironmentSetupDialog::driverCommands = "# Build and install the driver\n make ; sudo make install\n\n";
const QString EnvironmentSetupDialog::groupCommands = "# Add user to dialout group\n sudo usermod -a -G dialout $USER\n\n";
const QString EnvironmentSetupDialog::udevCommands =
    "#Add udev rules for Openterface Mini-KVM\n"
    "echo 'KERNEL== \"hidraw*\", SUBSYSTEM==\"hidraw\", MODE=\"0666\"' | sudo tee /etc/udev/rules.d/51-openterface.rules\n"
    "echo 'SUBSYSTEM==\"usb\", ATTR{idVendor}==\"1a86\", ATTR{idProduct}==\"7523\", ENV{BRL TTY_BRAILLY_DRIVER}=\"none\", MODE=\"0666\"' | sudo tee -a /etc/udev/rules.d/51-openterface.rules\n"
    "sudo udevadm control --reload-rules\n"
    "sudo udevadm trigger\n\n";
const QString EnvironmentSetupDialog::brlttyCommands =
    "# Remove BRLTTY which may interfere with device access\n"
    "sudo apt-get remove -y brltty\n"
    "sudo apt-get autoremove -y\n\n";

bool EnvironmentSetupDialog::isInRightUserGroup = false;
bool EnvironmentSetupDialog::isHidPermission = false;
bool EnvironmentSetupDialog::isBrlttyRunning = false;
#endif

// Define the help URL
#ifdef _WIN32
const QString EnvironmentSetupDialog::helpUrl = "https://github.com/TechxArtisanStudio/Openterface_QT/wiki/OpenterfaceQT-Windows-Environment-Setup";
#elif defined(__linux__)
const QString EnvironmentSetupDialog::helpUrl = "https://github.com/TechxArtisanStudio/Openterface_QT/wiki/OpenterfaceQT-Linux-Environment-Setup";
#endif

EnvironmentSetupDialog::EnvironmentSetupDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::EnvironmentSetupDialog),
    isDevicePlugged(false)
{
    ui->setupUi(this);
    
    // Set labels to interpret rich text
    ui->descriptionLabel->setTextFormat(Qt::RichText);
    ui->helpLabel->setTextFormat(Qt::RichText);

    checkEnvironmentSetup(); // Ensure the status variables are updated
    QSettings settings("Openterface", "EnvironmentSetup");
    bool autoCheck = settings.value("autoCheck", true).toBool();
    ui->autoCheckBox->setChecked(autoCheck);

#ifdef _WIN32
    setFixedSize(250, 140);
    ui->step1Label->setVisible(false);
    ui->extractButton->setVisible(false);
    ui->step2Label->setVisible(false);
    ui->copyButton->setVisible(false);
    ui->commandsTextEdit->setVisible(false);
    if(isDriverInstalled)
        ui->descriptionLabel->setText(tickHtml + tr(" The driver is installed. No further action is required."));
    else
        ui->descriptionLabel->setText(crossHtml + tr(" The driver is missing. Openterface Mini-KVM will install it automatically."));
#else
    if(!isDevicePlugged){
        ui->descriptionLabel->setText(crossHtml + tr(" The device is not plugged in. Please plug it in and try again."));
        ui->step1Label->setVisible(false);
        ui->extractButton->setVisible(false);
        ui->step2Label->setVisible(false);
        ui->copyButton->setVisible(false);
        ui->commandsTextEdit->setVisible(false);
        connect(ui->okButton, &QPushButton::clicked, this, &EnvironmentSetupDialog::reject);
        connect(ui->quitButton, &QPushButton::clicked, this, &EnvironmentSetupDialog::reject);
        return;
    } 
    setFixedSize(450, 450);
    ui->commandsTextEdit->setVisible(true);
    ui->step1Label->setVisible(!isDriverInstalled);
    ui->extractButton->setVisible(!isDriverInstalled);
    ui->copyButton->setVisible(true);
    ui->step2Label->setVisible(true);
    ui->commandsTextEdit->setText(buildCommands());
    connect(ui->extractButton, &QPushButton::clicked, this, &EnvironmentSetupDialog::extractDriverFiles);
    connect(ui->copyButton, &QPushButton::clicked, this, &EnvironmentSetupDialog::copyCommands);

    // Create the status summary
    QString statusSummary = tr("The following steps help you install the driver and add user to correct group. Current status:<br>");
    statusSummary += tr("‣ Driver Installed: ") + QString(isDriverInstalled ? tickHtml : crossHtml) + "<br>";
    statusSummary += tr("‣ In Dialout Group: ") + QString(isInRightUserGroup ? tickHtml : crossHtml) + "<br>";
    statusSummary += tr("‣ HID Permission: ") + QString(isHidPermission ? tickHtml : crossHtml) + "<br>";
    statusSummary += tr("‣ BRLTTY checking: ") + QString(isBrlttyRunning ? crossHtml + tr(" (needs removal)") : tickHtml + tr(" (not running)"));
    ui->descriptionLabel->setText(statusSummary);

    // Create help link
    QLabel* helpLabel = new QLabel(this);
    helpLabel->setOpenExternalLinks(false); // We'll handle the click ourselves
    helpLabel->setTextFormat(Qt::RichText); // Ensure this label also uses rich text
    helpLabel->setAlignment(Qt::AlignCenter);

    // Get the layout from the UI file and add the help label
    QVBoxLayout* layout = qobject_cast<QVBoxLayout*>(this->layout());
    if (layout) {
        layout->addWidget(helpLabel);
    }
    
#endif
    // Connect the help link to our slot
    connect(ui->helpLabel, &QLabel::linkActivated, this, &EnvironmentSetupDialog::openHelpLink);
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
    QString selectedDir = QFileDialog::getExistingDirectory(this, tr("Select Destination Directory"), QDir::homePath());

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

#ifdef __linux__
    // Update the QTextEdit with the static commands
    ui->commandsTextEdit->setPlainText("cd " + tempDir + "\n" + buildCommands());
#endif
}

void EnvironmentSetupDialog::copyCommands() {
    // Copy the commands to the clipboard
    QClipboard *clipboard = QApplication::clipboard();
    clipboard->setText(ui->commandsTextEdit->toPlainText());
}

// Update the accept method to call the new installDriver method
void EnvironmentSetupDialog::accept()
{
    // Update the setting
    QSettings settings("Openterface", "EnvironmentSetup");
    settings.setValue("autoCheck", ui->autoCheckBox->isChecked());
    settings.sync();

    #ifdef _WIN32
    if(!isDriverInstalled)
        installDriverForWindows();
    #elif defined(__linux__)
        // Check the current status
        QString statusSummary;
        statusSummary += tr("Driver Installed: ") + QString(isDriverInstalled ? tr("Yes") : tr("No")) + "\n";
        statusSummary += tr("In Dialout Group: ") + QString(isInRightUserGroup ? tr("Yes") : tr("No")) + "\n";
        statusSummary += tr("HID Permission: ") + QString(isHidPermission ? tr("Yes") : tr("No")) + "\n";
        statusSummary += tr("BRLTTY is Running: ") + QString(isBrlttyRunning ? tr("Yes (needs removal)") : tr("No")) + "\n";

        // Append the status summary to the description label
        ui->descriptionLabel->setText(ui->descriptionLabel->text() + "\n" + statusSummary);
    #endif

    if(!isDriverInstalled){
        QMessageBox::StandardButton reply = QMessageBox::question(
            this,
            tr("Restart Required"),
            tr("The driver has been installed. A system restart and device re-plugging is required for the changes to take effect.\n\n"
            "Would you like to restart your computer now?"),
            QMessageBox::Yes | QMessageBox::No
        );
        if (reply == QMessageBox::Yes) {
    #ifdef _WIN32
            QProcess::startDetached("shutdown", QStringList() << "-r" << "-t" << "0");
    #elif defined(__linux__)
            // For Linux systems
            QProcess::startDetached("reboot");
    #endif
        } else {
            QMessageBox::information(
                this,
                tr("Restart Later"),
                tr("Please remember to restart your computer and re-plug the device for the driver to work properly.")
            );
        }
    }

    // Call the base class accept method to close the dialog
    QDialog::accept();
    close();
}

#ifdef __linux__
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
    if (isBrlttyRunning) {
        commands += brlttyCommands;
    }

    return commands;
}

bool EnvironmentSetupDialog::checkInRightUserGroup() {
    // Check if the user is in the dialout group
    std::string command = "groups | grep -i dialout";
    int result = system(command.c_str());
    isInRightUserGroup = (result == 0);
    // Remove the UI update from the static method
    return isInRightUserGroup;
}

bool EnvironmentSetupDialog::checkHidPermission() {
    std::cout << "Checking HID permissions..." << std::endl;
    
    // First try to list all hidraw devices
    QDir devDir("/dev");
    QStringList devices = devDir.entryList(QStringList() << "hidraw*", QDir::System);
    
    // Check if devices exist at all
    if (devices.isEmpty()) {
        // No devices found - but this could be normal if no HID devices are connected
        std::cout << "No hidraw devices found. If device is connected, may need udev rules." << std::endl;
        
        // Also check if the udev rules are properly set up
        QProcess udevProcess;
        udevProcess.start("grep", QStringList() << "-q" << "hidraw" << "/etc/udev/rules.d/*openterface*.rules");
        udevProcess.waitForFinished();
        
        if (udevProcess.exitCode() == 0) {
            // Rules exist, which is good for future devices
            std::cout << "Openterface udev rules found. Permissions will be correct when device is connected." << std::endl;
            isHidPermission = true;
            return true;
        }
        
        isHidPermission = false;
        return false;
    }
    
    // Devices exist - check permissions
    // Check if any device has proper permissions
    bool hasPermission = false;
    for (const QString& device : devices) {
        qDebug() << "Checking device:" << device;
        // Check file permissions using QFileInfo
        QFileInfo fileInfo("/dev/" + device);
        if (!fileInfo.exists()) continue;
        
        if (fileInfo.isReadable() && fileInfo.isWritable()) {
            hasPermission = true;
            std::cout << "Found device with RW access: " << device.toStdString() << std::endl;
            break;
        }
        
        // Get detailed permissions with stat command
        QProcess statProcess;
        statProcess.start("stat", QStringList() << "-c" << "%a %G" << device);
        statProcess.waitForFinished();
        QString statOutput = statProcess.readAllStandardOutput().trimmed();
        std::cout << "Device " << device.toStdString() << " permissions: " << statOutput.toStdString() << std::endl;
        
        // Check for 666 permissions (rw for all) or 664 permissions (rw for group)
        QString permString = statOutput.split(' ').first();
        if (permString == "666") {
            hasPermission = true;
            std::cout << "Device has 666 permissions (rw for everyone)" << std::endl;
            break;
        } else if (permString == "664" || permString == "660") {
            // Need to check if user belongs to the device group
            QString groupName = statOutput.split(' ').last();
            
            QProcess groupsProcess;
            groupsProcess.start("groups");
            groupsProcess.waitForFinished();
            QString groupsOutput = groupsProcess.readAllStandardOutput();
            
            if (groupsOutput.contains(groupName)) {
                hasPermission = true;
                std::cout << "User is in group " << groupName.toStdString() 
                          << " with access to " << device.toStdString() << std::endl;
                break;
            }
        }
    }
    
    isHidPermission = hasPermission;
    std::cout << "HID permissions check result: " << (hasPermission ? "Yes" : "No") << std::endl;
    return hasPermission;
}

bool EnvironmentSetupDialog::checkBrlttyRunning() {
    // Check if BRLTTY is installed
    std::cout << "Checking if BRLTTY is installed." << std::endl;
    std::string checkInstalled = "which brltty > /dev/null 2>&1";
    std::string checkRunning = "pgrep brltty > /dev/null 2>&1";
    int isInstalled = system(checkInstalled.c_str());
    int pid = isInstalled == 0 ? system(checkRunning.c_str()) : -1;
    isBrlttyRunning = (pid == 0);
    if (isBrlttyRunning) {
        std::cout << "BRLTTY is running. It may interfere with device access." << std::endl;
    } else {
        std::cout << "BRLTTY is not running. Good!" << std::endl;
    }
    return isBrlttyRunning;
}

#endif

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
    bool skipCheck = false;
    if (result != 0) {
        std::cout << "MS2109 not exist, so no Openterface plugged in" << std::endl;
        skipCheck = true;
    }

    checkBrlttyRunning(); // No need to return value here
    return checkDriverInstalled() && checkInRightUserGroup() && checkHidPermission() && !isBrlttyRunning || skipCheck;
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

void EnvironmentSetupDialog::openHelpLink() {
    // Open the help URL in the default web browser
    QDesktopServices::openUrl(QUrl(helpUrl));
}

bool EnvironmentSetupDialog::autoEnvironmentCheck() {
    // Check the config file for the auto-check preference
    QSettings settings("Openterface", "EnvironmentSetup");
    bool autoCheck = settings.value("autoCheck", true).toBool();
    std::cout << "Auto-check preference: " << (autoCheck ? "enabled" : "disabled") << std::endl;
    return autoCheck;
}
