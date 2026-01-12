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
#include <QMessageBox>
#include <vector>
#include <utility>
#ifdef _WIN32 // Check if compiling on Windows
#include <windows.h> // Include Windows API header
#include <setupapi.h> // Include SetupAPI for device installation functions
#include <devguid.h> // Include Device Guids
#include <regstr.h> // Include Registry strings
#endif

#ifdef __linux__ // Check if compiling on Linux
#include <fstream> // For file operations
#include <string> // For std::string
#endif
#include <QDesktopServices> // Add this for opening URLs
#include <QUrl> // Add this for handling URLs
#include <QLabel> // Already included, but noting it's used for hyperlink
#include <QFont> // Include QFont for system font information

bool EnvironmentSetupDialog::isDriverInstalled = false;
const QString EnvironmentSetupDialog::tickHtml = "<span style='color: green'>&#x2713;</span>";
const QString EnvironmentSetupDialog::crossHtml = "<span style='color: red'>&#x2717;</span>";
QString EnvironmentSetupDialog::latestFirewareDescription = QString("");
// const QString EnvironmentSetupDialog::latestFirewareDescription = "not the latest firmware version. Please click OK then update it in Advance->Firmware Update...";
FirmwareResult EnvironmentSetupDialog::latestFirmware = FirmwareResult::Checking;

#ifdef __linux__
// Define the static commands
static const uint16_t openterfaceVID = 0x534d;
static const uint16_t openterfacePID = 0x2109;
static const uint16_t openterfaceV2VID = 0x534F;
static const uint16_t openterfaceV2PID = 0x2109;
static const uint16_t openterfaceV3VID = 0x534F;
static const uint16_t openterfaceV3PID = 0x2132;
static const uint16_t ch341VID = 0x1a86;
static const uint16_t ch341PID = 0x7523;
static const uint16_t serialVID = 0x1A86;
static const uint16_t serialPID = 0xFE0C;
libusb_context *context = nullptr;

std::vector<std::pair<uint16_t, uint16_t>> openterfaceDevices = {
    {0x534D, 0x2109},
    {0x534F, 0x2109},
    {0x534F, 0x2132}
};

std::vector<std::pair<uint16_t, uint16_t>> serialDevices = {
    {0x1A86, 0x7523},
    {0x1A86, 0xFE0C}
};

const QString EnvironmentSetupDialog::driverCommands = "# Build and install the driver\n make ; sudo make install\n\n";
const QString EnvironmentSetupDialog::groupCommands = "# Add user to dialout group\n sudo usermod -a -G dialout $USER\n\n";
const QString EnvironmentSetupDialog::udevCommands =
    "#Add udev rules for Openterface Mini-KVM\n"
    "echo 'SUBSYSTEM==\"usb\", ATTRS{idVendor}==\"534d\", ATTRS{idProduct}==\"2109\", TAG+=\"uaccess\"' | sudo tee /etc/udev/rules.d/51-openterface.rules\n"
    "echo 'SUBSYSTEM==\"hidraw\", ATTRS{idVendor}==\"534d\", ATTRS{idProduct}==\"2109\", TAG+=\"uaccess\"' | sudo tee -a /etc/udev/rules.d/51-openterface.rules\n"
    "echo 'SUBSYSTEM==\"usb\", ATTRS{idVendor}==\"534f\", ATTRS{idProduct}==\"2109\", TAG+=\"uaccess\"' | sudo tee -a /etc/udev/rules.d/51-openterface.rules\n"
    "echo 'SUBSYSTEM==\"hidraw\", ATTRS{idVendor}==\"534f\", ATTRS{idProduct}==\"2109\", TAG+=\"uaccess\"' | sudo tee -a /etc/udev/rules.d/51-openterface.rules\n"
    "echo 'SUBSYSTEM==\"usb\", ATTRS{idVendor}==\"534f\", ATTRS{idProduct}==\"2132\", TAG+=\"uaccess\"' | sudo tee -a /etc/udev/rules.d/51-openterface.rules\n"
    "echo 'SUBSYSTEM==\"hidraw\", ATTRS{idVendor}==\"534f\", ATTRS{idProduct}==\"2132\", TAG+=\"uaccess\"' | sudo tee -a /etc/udev/rules.d/51-openterface.rules\n"
    "echo 'SUBSYSTEM==\"ttyUSB\", ATTRS{idVendor}==\"1a86\", ATTRS{idProduct}==\"7523\", TAG+=\"uaccess\"' | sudo tee -a /etc/udev/rules.d/51-openterface.rules\n"
    "echo 'SUBSYSTEM==\"usb\", ATTRS{idVendor}==\"1a86\", ATTRS{idProduct}==\"7523\", TAG+=\"uaccess\"' | sudo tee -a /etc/udev/rules.d/51-openterface.rules\n"
    "echo 'SUBSYSTEM==\"ttyACM\", ATTRS{idVendor}==\"1a86\", ATTRS{idProduct}==\"fe0c\", TAG+=\"uaccess\"' | sudo tee -a /etc/udev/rules.d/51-openterface.rules\n"
    "echo 'SUBSYSTEM==\"usb\", ATTRS{idVendor}==\"1a86\", ATTRS{idProduct}==\"fe0c\", TAG+=\"uaccess\"' | sudo tee -a /etc/udev/rules.d/51-openterface.rules\n"
    "sudo udevadm control --reload-rules\n"
    "sudo udevadm trigger\n\n";
const QString EnvironmentSetupDialog::brlttyCommands =
    "# Remove BRLTTY which may interfere with device access\n"
    "sudo apt-get remove -y brltty\n"
    "sudo apt-get autoremove -y\n\n";

bool EnvironmentSetupDialog::isSerialPermission = false;
bool EnvironmentSetupDialog::isHidPermission = false;
bool EnvironmentSetupDialog::isBrlttyRunning = false;
bool EnvironmentSetupDialog::isDevicePlugged = false;
#endif

// Define the help URL
#ifdef _WIN32
const QString EnvironmentSetupDialog::helpUrl = "https://github.com/TechxArtisanStudio/Openterface_QT/wiki/OpenterfaceQT-Windows-Environment-Setup";
#elif defined(__linux__)
const QString EnvironmentSetupDialog::helpUrl = "https://github.com/TechxArtisanStudio/Openterface_QT/wiki/OpenterfaceQT-Linux-Environment-Setup";
#endif

EnvironmentSetupDialog::EnvironmentSetupDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::EnvironmentSetupDialog)
    // isDevicePlugged(false)
{
    ui->setupUi(this);
    
    QString statusSummary;
    
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
    statusSummary += tr("The following steps help you install the driver and the Openterface firmware update. Current status:<br>");
    QString latestDescription = latestFirewareDescription;
    qDebug() << latestDescription;
    statusSummary += tr("◆ Driver Installed: ") + QString(isDriverInstalled? tickHtml : crossHtml) + "<br>";
    statusSummary += tr("◆ Latest Firmware: ") + QString(latestFirmware == FirmwareResult::Latest ? tickHtml : crossHtml) + QString(latestFirmware == FirmwareResult::Latest ?  QString(""): latestDescription);
    ui->descriptionLabel->setText(statusSummary);

    // if(isDriverInstalled)
    //     ui->descriptionLabel->setText(tickHtml + tr(" The driver is installed. No further action is required."));
    // else
    //     ui->descriptionLabel->setText(crossHtml + tr(" The driver is missing. Openterface Mini-KVM will install it automatically."));
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
    statusSummary = tr("The following steps help you install the driver and access the device permissions and the Openterface firmware update. Current status:<br>");
    statusSummary += tr("◆ Driver Installed: ") + QString(isDriverInstalled ? tickHtml : crossHtml) + "<br>";
    statusSummary += tr("◆ In Serial Port Permission: ") + QString(isSerialPermission ? tickHtml : crossHtml) + "<br>";
    statusSummary += tr("◆ HID Permission: ") + QString(isHidPermission ? tickHtml : crossHtml) + "<br>";
    statusSummary += tr("◆ BRLTTY checking: ") + QString(isBrlttyRunning ? crossHtml + tr(" (needs removal)") : tickHtml + tr(" (not running)")) + "<br>";
    statusSummary += tr("◆ Latest Firmware: ") + QString(latestFirmware == FirmwareResult::Latest ? tickHtml : crossHtml) + QString(latestFirmware == FirmwareResult::Latest ?  QString(""): latestFirewareDescription);
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

// Override the closeEvent to handle it same as quit button
void EnvironmentSetupDialog::closeEvent(QCloseEvent *event)
{
    reject(); // Call reject to close the dialog same as quit button
    event->accept(); // Accept the close event
}

#ifdef _WIN32
void EnvironmentSetupDialog::installDriverForWindows() {
    // Windows-specific installation logic
    qDebug() << "Attempting to install driver using pnputil.";
    QMessageBox msgBox(this);
    msgBox.setWindowTitle(tr("Install Driver"));
    msgBox.setText(tr("The driver is missing. Please install the driver at: https://www.wch.cn/downloads/CH341SER.EXE.html \n\n"
        "After the driver is installed, a system restart and device re-plugging is required for the changes to take effect.\n\n"
        "Please restart your computer after the driver installation."));
    msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    
    // Add button for copy link
    QPushButton *copyButton = msgBox.addButton(tr("Copy Link"), QMessageBox::ActionRole);
    
    QMessageBox::StandardButton reply = static_cast<QMessageBox::StandardButton>(msgBox.exec());
    
    // Check if the copy button was clicked
    if (msgBox.clickedButton() == copyButton) {
        QClipboard *clipboard = QApplication::clipboard();
        clipboard->setText("https://www.wch.cn/downloads/CH341SER.EXE.html");
    }
    qDebug() << "Driver installation command executed.";
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
                qDebug() << "Copied " << QFileInfo(filePath).fileName() << " to " << tempDir;
            } else {
                qDebug() << "Failed to open target file for writing: " << targetPath;
            }
            resourceFile.close();
        } else {
            qDebug() << "Failed to open resource file: " << filePath;
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
        statusSummary += tr("Serial port Permission: ") + QString(isSerialPermission ? tr("Yes") : tr("No")) + "\n";
        statusSummary += tr("HID Permission: ") + QString(isHidPermission ? tr("Yes") : tr("No")) + "\n";
        statusSummary += tr("BRLTTY is Running: ") + QString(isBrlttyRunning ? tr("Yes (needs removal)") : tr("No")) + "\n";

        // Append the status summary to the description label
        ui->descriptionLabel->setText(ui->descriptionLabel->text() + "\n" + statusSummary);
        QMessageBox::StandardButton reply = QMessageBox::question(
            this,
            tr("Restart Required"),
            tr("The driver has been installed. A system restart and device re-plugging is required for the changes to take effect.\n\n"
            "Would you like to restart your computer now?"),
            QMessageBox::Yes | QMessageBox::No
        );
        if (reply == QMessageBox::Yes) {
            QProcess::startDetached("reboot");
        }else{
            QMessageBox::information(
                this,
                tr("Restart Later"),
                tr("Please remember to restart your computer and re-plug the device for the driver to work properly.")
            );
        }
    #endif
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
    if (!isSerialPermission) {
        commands += groupCommands;
    }
    if (!isHidPermission || !isSerialPermission) {
        commands += udevCommands;
    }
    if (isBrlttyRunning) {
        commands += brlttyCommands;
    }

    return commands;
}



bool EnvironmentSetupDialog::checkHidPermission() {
    qDebug() << "Checking HID permissions...";
    
    // First try to list all hidraw devices
    QDir devDir("/dev");
    QStringList devices = devDir.entryList(QStringList() << "hidraw*", QDir::System);
    
    // Check if devices exist at all
    if (devices.isEmpty()) {
        // No devices found - but this could be normal if no HID devices are connected
        qDebug() << "No hidraw devices found. If device is connected, may need udev rules.";
        
        // Also check if the udev rules are properly set up
        QProcess udevProcess;
        udevProcess.start("grep", QStringList() << "-q" << "hidraw" << "/etc/udev/rules.d/*openterface*.rules");
        udevProcess.waitForFinished();
        
        if (udevProcess.exitCode() == 0) {
            // Rules exist, which is good for future devices
            qDebug() << "Openterface udev rules found. Permissions will be correct when device is connected.";
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
            qDebug() << "Found device with RW access: " << device;
            break;
        }
        
        // Get detailed permissions with stat command
        QProcess statProcess;
        statProcess.start("stat", QStringList() << "-c" << "%a %G" << device);
        statProcess.waitForFinished();
        QString statOutput = statProcess.readAllStandardOutput().trimmed();
        qDebug() << "Device " << device << " permissions: " << statOutput;
        
        // Check for 666 permissions (rw for all) or 664 permissions (rw for group)
        QString permString = statOutput.split(' ').first();
        if (permString == "666") {
            hasPermission = true;
            qDebug() << "Device has 666 permissions (rw for everyone)";
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
                qDebug() << "User is in group " << groupName 
                          << " with access to " << device;
                break;
            }
        }
    }
    
    isHidPermission = hasPermission;
    qDebug() << "HID permissions check result: " << (hasPermission ? "Yes" : "No");
    return hasPermission;
}

bool EnvironmentSetupDialog::checkBrlttyRunning() {
    // Check if BRLTTY is installed
    qDebug() << "Checking if BRLTTY is installed.";
    std::string checkInstalled = "which brltty > /dev/null 2>&1";
    std::string checkRunning = "pgrep brltty > /dev/null 2>&1";
    int isInstalled = system(checkInstalled.c_str());
    int pid = isInstalled == 0 ? system(checkRunning.c_str()) : -1;
    isBrlttyRunning = (pid == 0);
    if (isBrlttyRunning) {
        qDebug() << "BRLTTY is running. It may interfere with device access.";
    } else {
        qDebug() << "BRLTTY is not running. Good!";
    }
    return isBrlttyRunning;
}

bool EnvironmentSetupDialog::detectDevices(const std::vector<std::pair<uint16_t, uint16_t>>& devices) {
    libusb_device **dev_list = nullptr;
    ssize_t dev_count = libusb_get_device_list(context, &dev_list);
    if (dev_count < 0) {
        qWarning() << "libusb_get_device_list failed: " << libusb_error_name(static_cast<int>(dev_count));
        return false;
    }

    std::unique_ptr<libusb_device*[], void(*)(libusb_device**)> dev_list_guard(dev_list, [](libusb_device** list) {
        libusb_free_device_list(list, 1);
    });

    bool found = false;

    for (auto& dev_pair : devices) {
        uint16_t vid = dev_pair.first;
        uint16_t pid = dev_pair.second;
        for (ssize_t i = 0; i < dev_count; i++) {
            libusb_device *dev = dev_list[i];
            libusb_device_descriptor desc;
            int ret = libusb_get_device_descriptor(dev, &desc);
            if (ret < 0) {
                qWarning() << "libusb_get_device_descriptor failed: " << libusb_error_name(ret);
                continue;
            }
            if (desc.idVendor == vid && desc.idProduct == pid) {
                found = true;
                isDevicePlugged = true;
                qDebug() << "Device detected with VID: 0x" 
                        << QString::number(vid, 16).rightJustified(4, '0')
                        << " PID: 0x" 
                        << QString::number(pid, 16).rightJustified(4, '0');
                break;
            }
        }
        if (found) break;
    }
    return found;
}

bool EnvironmentSetupDialog::checkPermissions(const std::vector<std::pair<uint16_t, uint16_t>>& devices, bool isSerial) {
    libusb_device **dev_list = nullptr;
    ssize_t dev_count = libusb_get_device_list(context, &dev_list);
    if (dev_count < 0) {
        qWarning() << "libusb_get_device_list failed: " << libusb_error_name(static_cast<int>(dev_count));
        return false;
    }

    std::unique_ptr<libusb_device*[], void(*)(libusb_device**)> dev_list_guard(dev_list, [](libusb_device** list) {
        libusb_free_device_list(list, 1);
    });

    for (auto& dev_pair : devices) {
        uint16_t vid = dev_pair.first;
        uint16_t pid = dev_pair.second;
        for (ssize_t i = 0; i < dev_count; i++) {
            libusb_device *dev = dev_list[i];
            libusb_device_descriptor desc;
            int ret = libusb_get_device_descriptor(dev, &desc);
            if (ret < 0) {
                qWarning() << "libusb_get_device_descriptor failed: " << libusb_error_name(ret);
                continue;
            }
            if (desc.idVendor == vid && desc.idProduct == pid) {
                qDebug() << "Name of device" << desc.iProduct;
                libusb_device_handle* handle = nullptr;
                int ret = libusb_open(dev, &handle);
                if (ret == LIBUSB_SUCCESS) {
                    // close the device handle
                    libusb_close(handle);
                    if (isSerial) {
                        isSerialPermission = true;
                        qDebug() << "Permission check passed for serial device VID: 0x" << QString::number(vid, 16) << " PID: 0x" << QString::number(pid, 16);
                    } else {
                        isHidPermission = true;
                        qDebug() << "Permission check passed for HID device VID: 0x" << QString::number(vid, 16) << " PID: 0x" << QString::number(pid, 16);
                    }
                    return true; 
                } else if (ret == LIBUSB_ERROR_ACCESS) {
                    qWarning() << "Permission denied for the device VID: 0x" << QString::number(vid, 16) << " PID: 0x" << QString::number(pid, 16);
                    return false;
                } else if (ret == LIBUSB_ERROR_BUSY) {
                    qWarning() << "Device is busy VID: 0x" << QString::number(vid, 16) << " PID: 0x" << QString::number(pid, 16);
                    return false;
                } else {
                    qWarning() << "Failed to open device VID: 0x" << QString::number(vid, 16) << " PID: 0x" << QString::number(pid, 16) << ": " << libusb_error_name(ret);
                    return false;
                }
            }
        }
    }
    return false;
}

#endif

// Override reject method
void EnvironmentSetupDialog::reject()
{
    QDialog::reject();
}
#ifdef __linux__
bool EnvironmentSetupDialog::checkDevicePermission(uint16_t vendorID, uint16_t productID) {
    libusb_device **dev_list = nullptr;
    ssize_t dev_count = libusb_get_device_list(context, &dev_list);
    if (dev_count < 0) {
        qWarning() << "libusb_get_device_list failed: " << libusb_error_name(static_cast<int>(dev_count));
        return false;
    }

    std::unique_ptr<libusb_device*[], void(*)(libusb_device**)> dev_list_guard(dev_list, [](libusb_device** list) {
        libusb_free_device_list(list, 1);
    });

    for (ssize_t i =0; i < dev_count; i++) {
        libusb_device *dev = dev_list[i];
        libusb_device_descriptor desc;
        int ret = libusb_get_device_descriptor(dev, &desc);
        if (ret < 0) {
            qWarning() << "libusb_get_device_descriptor failed: " << libusb_error_name(ret);
            continue;
        }
        if (desc.idVendor == vendorID && desc.idProduct == productID) {
            qDebug() << "Name of device" << desc.iProduct;
            libusb_device_handle* handle = nullptr;
            int ret = libusb_open(dev, &handle);
            if (ret == LIBUSB_SUCCESS) {
                // close the device handle
                libusb_close(handle);
                if (vendorID == ch341VID && productID == ch341PID) {
                    isSerialPermission = true;
                    qDebug() << "CH341 permission check passed.";
                } else if (vendorID == openterfaceVID && productID == openterfacePID) {
                    isHidPermission = true;
                    qDebug() << "Openterface permission check passed.";
                }
                return true; 
            } else if (ret == LIBUSB_ERROR_ACCESS) {
                qWarning() << "Permission denied for the device";
                return false;
            } else if (ret == LIBUSB_ERROR_BUSY) {
                qWarning() << "Device is busy";
                return false;
            } else {
                qWarning() << "Failed to open device: " << libusb_error_name(ret);
                return false;
            }
        }
    }
    return false;
}

bool EnvironmentSetupDialog::detectDevice(uint16_t vendorID, uint16_t productID) {
    qDebug() << "Device detected with VID: 0x" 
                    << QString::number(vendorID, 16).rightJustified(4, '0')
                    << "PID: 0x" 
                    << QString::number(productID, 16).rightJustified(4, '0');
    libusb_device **dev_list = nullptr;
    ssize_t dev_count = libusb_get_device_list(context, &dev_list);
    if (dev_count < 0) {
        qWarning() << "libusb_get_device_list failed: " << libusb_error_name(static_cast<int>(dev_count));
        return false;
    }

    std::unique_ptr<libusb_device*[], void(*)(libusb_device**)> dev_list_guard(dev_list, [](libusb_device** list) {
        libusb_free_device_list(list, 1);
    });

    bool found = false;

    for (ssize_t i =0; i < dev_count; i++) {
        libusb_device *dev = dev_list[i];
        libusb_device_descriptor desc;
        int ret = libusb_get_device_descriptor(dev, &desc);
        if (ret < 0) {
            qWarning() << "libusb_get_device_descriptor failed: " << libusb_error_name(ret);
            continue;
        }
        if (desc.idVendor == vendorID && desc.idProduct == productID) {
            found = true;
            isDevicePlugged = true;
            qDebug() << "Device detected with VID: 0x" 
                    << QString::number(vendorID, 16).rightJustified(4, '0')
                    << "PID: 0x" 
                    << QString::number(productID, 16).rightJustified(4, '0');
        }
    }
    return found;
}

#endif

bool EnvironmentSetupDialog::checkEnvironmentSetup() {
    latestFirmware = VideoHid::getInstance().isLatestFirmware();
    std::string version = VideoHid::getInstance().getCurrentFirmwareVersion();
    std::string latestVersion = VideoHid::getInstance().getLatestFirmwareVersion();
    qDebug() << "Driver detect: " << QString::fromStdString(version);
    qDebug() << "Latest driver: " << QString::fromStdString(latestVersion);
    qDebug() << "Driver is latest: " << (latestFirmware == FirmwareResult::Latest ? "yes" : "no" );
    latestFirewareDescription ="<br>Current version: " + QString::fromStdString(version) + 
    "<br>" + "Latest version: " + QString::fromStdString(latestVersion) +
    "<br>" + "Please update driver to latest version." + 
    "<br>" + "click OK then Advance->Firmware Update...";
    qDebug() << latestFirewareDescription;
    #ifdef _WIN32
    return checkDriverInstalled() && latestFirmware == FirmwareResult::Latest;
    #elif defined(__linux__)
    qDebug() << "Checking if Openterface devices are on Linux.";

    // EnvironmentSetupDialog dialog;
    if (context == nullptr){
        int ret = libusb_init(&context);
        if (ret < 0) {
            qWarning() << "Error initializing libusb: " << libusb_error_name(ret);
            qWarning() << "Cannot proceed without libusb context. Skipping device checks.";
            return true; // Skip checks if libusb initialization fails
        }
        qDebug() << "libusb initialized successfully.";
    }

    bool openterfacePlugged = detectDevices(openterfaceDevices);
    bool skipCheck = false;
    if (!openterfacePlugged) {
        qDebug() << "No Openterface device plugged in";
        skipCheck = true;
    }
    bool serialPlugged = detectDevices(serialDevices);
    if (!serialPlugged) {
        qDebug() << "No serial device plugged in";
    }else{
        qDebug() << "Serial device plugged in";
    }

    bool checkSerialPermission = checkPermissions(serialDevices, true);
    if (!checkSerialPermission) {
        qDebug() << "Serial permission check failed.";
    } else {
        qDebug() << "Serial permission check passed.";
    }
    
    checkBrlttyRunning(); // No need to return value here
    bool checkPermission = checkPermissions(openterfaceDevices, false);
    qDebug() << "Check permission result: " << checkPermission;
    return (checkDriverInstalled() && checkSerialPermission && checkPermission && (latestFirmware == FirmwareResult::Latest) && !isBrlttyRunning) || skipCheck;
    #else
    return true;
    #endif
}

bool EnvironmentSetupDialog::checkDriverInstalled() {
#ifdef _WIN32 // Check if compiling on Windows
    qDebug() << "Checking if devices are present...";
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
            if (wcsstr(hwIdBuffer, L"USB\\VID_534D&PID_2109") != NULL ||
                wcsstr(hwIdBuffer, L"USB\\VID_534F&PID_2109") != NULL ||
                wcsstr(hwIdBuffer, L"USB\\VID_534F&PID_2132") != NULL) {
                captureCardFound = true;
            }
            if (wcsstr(hwIdBuffer, L"USB\\VID_1A86&PID_7523") != NULL ||
                wcsstr(hwIdBuffer, L"USB\\VID_1A86&PID_CH32V208") != NULL) {
                ch341Found = true;
            }
        }
    }

    SetupDiDestroyDeviceInfoList(deviceInfoSet);

    if (!captureCardFound && !ch341Found) {
        qDebug() << "Neither device found - skipping driver check";
        return true;
    }
    if (captureCardFound && !ch341Found) {
        qDebug() << "Capture card found but CH341 missing - need driver";
        return false;
    }
    qDebug() << "Devices properly detected";
    isDriverInstalled = true;
    return true;
#elif defined(__linux__) // Check if compiling on Linux
    // Log the start of the driver check
    qDebug() << "Checking if driver is installed on Linux.";

    // If the device file does not exist, check using cat /proc/modules
    std::string command = "cat /proc/modules | grep 'ch341'";
    int result = system(command.c_str());
    if (result == 0) {
        qDebug() << "Driver installation status: Installed (found via cat /proc/modules)";
        isDriverInstalled = true;
        return true; // Driver found via /proc/modules
    }

    qDebug() << "Driver installation status: Not Installed";
    isDriverInstalled = false;
    return false; // Driver not found
#else
    // Implement logic for other platforms if needed
    qDebug() << "Driver check not implemented for this platform.";

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
    qDebug() << "Auto-check preference: " << (autoCheck ? "enabled" : "disabled");
    return autoCheck;
}