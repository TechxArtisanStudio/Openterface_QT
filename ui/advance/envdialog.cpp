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
#include <QMetaObject> // Include QMetaObject for invokeMethod
#ifdef __linux__ // Check if compiling on Linux
#include <fstream> // For file operations
#include <string> // For std::string
#endif
#include <QDesktopServices> // Add this for opening URLs
#include <QUrl> // Add this for handling URLs
#include <QLabel> // Already included, but noting it's used for hyperlink
#include <QFont> // Include QFont for system font information

const QString EnvironmentSetupDialog::tickHtml = "<span style='color: green'>&#x2713;</span>";
const QString EnvironmentSetupDialog::crossHtml = "<span style='color: red'>&#x2717;</span>";
QString EnvironmentSetupDialog::latestFirewareDescription = QString("");
// const QString EnvironmentSetupDialog::latestFirewareDescription = "not the latest firmware version. Please click OK then update it in Advance->Firmware Update...";
FirmwareResult EnvironmentSetupDialog::latestFirmware = FirmwareResult::Checking;

#ifdef __linux__
// Define the static commands
libusb_context *context = nullptr;

std::vector<std::pair<uint16_t, uint16_t>> openterfaceDevices = {
    {0x534D, 0x2109},
    {0x345f, 0x2109},
    {0x345f, 0x2132}
};

std::vector<std::pair<uint16_t, uint16_t>> serialDevices = {
    {0x1A86, 0x7523},
    {0x1A86, 0xFE0C}
};

const QString EnvironmentSetupDialog::groupCommands = "# Add user to dialout group for serial access, and video group for camera access\n sudo usermod -a -G dialout,video $USER\n\n";
const QString EnvironmentSetupDialog::udevCommands =
    "#Add udev rules for Openterface Mini-KVM\n"
    "echo 'SUBSYSTEM==\"usb\", ATTRS{idVendor}==\"534d\", ATTRS{idProduct}==\"2109\", TAG+=\"uaccess\"' | sudo tee /etc/udev/rules.d/51-openterface.rules\n"
    "echo 'SUBSYSTEM==\"hidraw\", ATTRS{idVendor}==\"534d\", ATTRS{idProduct}==\"2109\", TAG+=\"uaccess\"' | sudo tee -a /etc/udev/rules.d/51-openterface.rules\n"
    "echo 'SUBSYSTEM==\"usb\", ATTRS{idVendor}==\"345f\", ATTRS{idProduct}==\"2109\", TAG+=\"uaccess\"' | sudo tee -a /etc/udev/rules.d/51-openterface.rules\n"
    "echo 'SUBSYSTEM==\"hidraw\", ATTRS{idVendor}==\"345f\", ATTRS{idProduct}==\"2109\", TAG+=\"uaccess\"' | sudo tee -a /etc/udev/rules.d/51-openterface.rules\n"
    "echo 'SUBSYSTEM==\"usb\", ATTRS{idVendor}==\"345f\", ATTRS{idProduct}==\"2132\", TAG+=\"uaccess\"' | sudo tee -a /etc/udev/rules.d/51-openterface.rules\n"
    "echo 'SUBSYSTEM==\"hidraw\", ATTRS{idVendor}==\"345f\", ATTRS{idProduct}==\"2132\", TAG+=\"uaccess\"' | sudo tee -a /etc/udev/rules.d/51-openterface.rules\n"
    "echo 'SUBSYSTEM==\"tty\", ATTRS{idVendor}==\"1a86\", ATTRS{idProduct}==\"7523\", TAG+=\"uaccess\"' | sudo tee -a /etc/udev/rules.d/51-openterface.rules\n"
    "echo 'SUBSYSTEM==\"usb\", ATTRS{idVendor}==\"1a86\", ATTRS{idProduct}==\"7523\", TAG+=\"uaccess\"' | sudo tee -a /etc/udev/rules.d/51-openterface.rules\n"
    // NB: CH32V208 presents as /dev/ttyACM* — subsystem is \"tty\", NOT \"ttyACM\".
    // The previous \"ttyACM\" rule matched nothing, which is why users following this
    // guidance still saw \"serial permission check failed\".
    "echo 'SUBSYSTEM==\"tty\", ATTRS{idVendor}==\"1a86\", ATTRS{idProduct}==\"fe0c\", TAG+=\"uaccess\"' | sudo tee -a /etc/udev/rules.d/51-openterface.rules\n"
    "echo 'SUBSYSTEM==\"usb\", ATTRS{idVendor}==\"1a86\", ATTRS{idProduct}==\"fe0c\", TAG+=\"uaccess\"' | sudo tee -a /etc/udev/rules.d/51-openterface.rules\n"
    "sudo udevadm control --reload-rules\n"
    "sudo udevadm trigger\n\n";
const QString EnvironmentSetupDialog::brlttyCommands =
    "# BRLTTY interferes with USB serial/HID device access. Mask it:\n"
    "sudo systemctl mask brltty-udev.service && sudo systemctl stop brltty-udev.service\n"
    "sudo systemctl mask brltty.service && sudo systemctl stop brltty.service\n"
    "# Or remove it: sudo apt-get remove -y brltty && sudo apt-get autoremove -y\n"
    "# Re-plug the device after running the above commands.\n\n";

bool EnvironmentSetupDialog::isSerialPermission = false;
bool EnvironmentSetupDialog::isHidPermission = false;
bool EnvironmentSetupDialog::isVideoPermission = false;
bool EnvironmentSetupDialog::isBrlttyRunning = false;
bool EnvironmentSetupDialog::isDevicePlugged = false;
#endif

// Define the help URL
#ifdef _WIN32
const QString EnvironmentSetupDialog::helpUrl = "https://github.com/TechxArtisanStudio/Openterface_QT/wiki/OpenterfaceQTWindowsEnvironmentSetup";
#elif defined(__linux__)
const QString EnvironmentSetupDialog::helpUrl = "https://github.com/TechxArtisanStudio/Openterface_QT/wiki/OpenterfaceQTLinuxEnvironmentSetup";
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
    statusSummary += tr("Firmware update status:<br>");
    QString latestDescription = latestFirewareDescription;
    statusSummary += tr("◆ Latest Firmware: ") + QString(latestFirmware == FirmwareResult::Latest ? tickHtml : crossHtml) + QString(latestFirmware == FirmwareResult::Latest ?  QString(""): latestDescription);
    ui->descriptionLabel->setText(statusSummary);
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
    ui->step1Label->setVisible(false);
    ui->extractButton->setVisible(false);
    ui->copyButton->setVisible(true);
    ui->step2Label->setVisible(true);
    ui->commandsTextEdit->setText(buildCommands());
    connect(ui->copyButton, &QPushButton::clicked, this, &EnvironmentSetupDialog::copyCommands);

    // Create the status summary
    statusSummary = tr("The following steps help you access the device permissions and the Openterface firmware update. Current status:<br>");
    statusSummary += tr("◆ In Serial Port Permission: ") + QString(isSerialPermission ? tickHtml : crossHtml) + "<br>";
    statusSummary += tr("◆ HID Permission: ") + QString(isHidPermission ? tickHtml : crossHtml) + "<br>";
    statusSummary += tr("◆ Video Permission: ") + QString(isVideoPermission ? tickHtml : crossHtml) + "<br>";
    statusSummary += tr("◆ BRLTTY (brltty-udev.service): ") + QString(isBrlttyRunning
        ? crossHtml + tr(" active - interferes with device access. Run the commands below to fix.")
        : tickHtml + tr(" not active - OK")) + "<br>";
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

void EnvironmentSetupDialog::copyCommands() {
    // Copy the commands to the clipboard
    QClipboard *clipboard = QApplication::clipboard();
    clipboard->setText(ui->commandsTextEdit->toPlainText());
}

// Update the accept method
void EnvironmentSetupDialog::accept()
{
    // Update the setting
    QSettings settings("Openterface", "EnvironmentSetup");
    settings.setValue("autoCheck", ui->autoCheckBox->isChecked());
    settings.sync();

    // Call the base class accept method to close the dialog

    QDialog::accept();
    close();
}

#ifdef __linux__
QString EnvironmentSetupDialog::buildCommands(){
    QString commands = "";
    if (!isSerialPermission || !isVideoPermission) {
        commands += groupCommands;
    }
    if (!isHidPermission || !isSerialPermission || !isVideoPermission) {
        commands += udevCommands;
    }
    if (isBrlttyRunning) {
        commands += brlttyCommands;
    }

    return commands;
}

bool EnvironmentSetupDialog::checkHidPermission() {
    
    // First try to list all hidraw devices
    QDir devDir("/dev");
    QStringList devices = devDir.entryList(QStringList() << "hidraw*", QDir::System);
    
    // Check if devices exist at all
    if (devices.isEmpty()) {
        // No devices found - but this could be normal if no HID devices are connected
        
        // Also check if the udev rules are properly set up
        QProcess udevProcess;
        udevProcess.start("grep", QStringList() << "-q" << "hidraw" << "/etc/udev/rules.d/*openterface*.rules");
        udevProcess.waitForFinished();
        
        if (udevProcess.exitCode() == 0) {
            // Rules exist, which is good for future devices
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
        // Check file permissions using QFileInfo
        QFileInfo fileInfo("/dev/" + device);
        if (!fileInfo.exists()) continue;
        
        if (fileInfo.isReadable() && fileInfo.isWritable()) {
            hasPermission = true;
            break;
        }
        
        // Get detailed permissions with stat command
        QProcess statProcess;
        statProcess.start("stat", QStringList() << "-c" << "%a %G" << device);
        statProcess.waitForFinished();
        QString statOutput = statProcess.readAllStandardOutput().trimmed();
        
        // Check for 666 permissions (rw for all) or 664 permissions (rw for group)
        QString permString = statOutput.split(' ').first();
        if (permString == "666") {
            hasPermission = true;
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
                break;
            }
        }
    }
    
    isHidPermission = hasPermission;
    return hasPermission;
}

bool EnvironmentSetupDialog::checkVideoPermission() {
    
    // First try to list all video devices
    QDir devDir("/dev");
    QStringList devices = devDir.entryList(QStringList() << "video*", QDir::System);
    
    // Check if devices exist at all
    if (devices.isEmpty()) {
        // No devices found - but this could be normal if no video devices are connected
        
        // Also check if the udev rules are properly set up
        QProcess udevProcess;
        udevProcess.start("grep", QStringList() << "-q" << "video" << "/etc/udev/rules.d/*openterface*.rules");
        udevProcess.waitForFinished();
        
        if (udevProcess.exitCode() == 0) {
            // Rules exist, which is good for future devices
            isVideoPermission = true;
            return true;
        }
        
        isVideoPermission = false;
        return false;
    }
    
    // Devices exist - check permissions
    // Check if any device has proper permissions
    bool hasPermission = false;
    for (const QString& device : devices) {
        // Check file permissions using QFileInfo
        QFileInfo fileInfo("/dev/" + device);
        if (!fileInfo.exists()) continue;
        
        if (fileInfo.isReadable() && fileInfo.isWritable()) {
            hasPermission = true;
            break;
        }
        
        // Get detailed permissions with stat command
        QProcess statProcess;
        statProcess.start("stat", QStringList() << "-c" << "%a %G" << "/dev/" + device);
        statProcess.waitForFinished();
        QString statOutput = statProcess.readAllStandardOutput().trimmed();
        
        // Check for 666 permissions (rw for all) or 664 permissions (rw for group)
        QString permString = statOutput.split(' ').first();
        if (permString == "666") {
            hasPermission = true;
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
                break;
            }
        }
    }
    
    isVideoPermission = hasPermission;
    return hasPermission;
}

bool EnvironmentSetupDialog::checkBrlttyRunning() {
    // brltty can interfere via two different systemd activation paths:
    //   1. brltty-udev.service  - udev-triggered, grabs USB serial/HID devices on plug-in (main culprit)
    //   2. brltty.service       - persistent daemon, may also hold the device
    // On distros like KDE neon where brltty cannot be removed (neon-desktop depends on it),
    // masking both services is the recommended fix (no uninstall required).

    auto queryService = [](const QString &unit) -> QString {
        QProcess p;
        p.start("systemctl", QStringList() << "is-active" << unit);
        p.waitForFinished(3000);
        return p.readAllStandardOutput().trimmed();
    };

    QString udevState = queryService("brltty-udev.service");
    QString daemonState = queryService("brltty.service");

    bool eitherActive = (udevState == "active") || (daemonState == "active");

    if (eitherActive) {
        // Double-check: verify the brltty process is actually running
        QProcess procCheck;
        procCheck.start("pgrep", QStringList() << "-x" << "brltty");
        procCheck.waitForFinished(2000);
        isBrlttyRunning = (procCheck.exitCode() == 0);
        if (isBrlttyRunning) {
        } else {
        }
    } else {
        // Both masked / inactive / failed / not-found - safe
        isBrlttyRunning = false;
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
                libusb_device_handle* handle = nullptr;
                int ret = libusb_open(dev, &handle);
                if (ret == LIBUSB_SUCCESS) {
                    // close the device handle
                    libusb_close(handle);
                    if (isSerial) {
                        isSerialPermission = true;
                    } else {
                        isHidPermission = true;
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

bool EnvironmentSetupDialog::checkEnvironmentSetup() {
    // Ensure HID device is properly detected and chip type is identified before firmware check
    VideoHid& videoHid = VideoHid::getInstance();
    
    // Refresh HID device discovery to ensure we have the latest device information
    videoHid.refreshHIDDevice();
    
    // Force chip type detection by calling it via meta-object system since it's Q_INVOKABLE
    QMetaObject::invokeMethod(&videoHid, "detectChipType", Qt::DirectConnection);
    
    // Now proceed with firmware checking
    latestFirmware = videoHid.isLatestFirmware();
    std::string version = videoHid.getCurrentFirmwareVersion();
    std::string latestVersion = videoHid.getLatestFirmwareVersion();
    
    latestFirewareDescription ="<br>Current version: " + QString::fromStdString(version) +
    "<br>" + "Latest version: " + QString::fromStdString(latestVersion) +
    "<br>" + "Please update the firmware to the latest version." +
    "<br>" + "Click OK, then open File->Preferences->Video Firmware and use \"Firmware Update from Remote\".";
    #ifdef _WIN32
    return latestFirmware == FirmwareResult::Latest;
    #elif defined(__linux__)

    // EnvironmentSetupDialog dialog;
    if (context == nullptr){
        int ret = libusb_init(&context);
        if (ret < 0) {
            qWarning() << "Error initializing libusb: " << libusb_error_name(ret);
            qWarning() << "Cannot proceed without libusb context. Skipping device checks.";
            return true; // Skip checks if libusb initialization fails
        }
    }

    bool openterfacePlugged = detectDevices(openterfaceDevices);
    bool skipCheck = false;
    if (!openterfacePlugged) {
        skipCheck = true;
    }
    bool serialPlugged = detectDevices(serialDevices);
    if (!serialPlugged) {
    }else{
    }

    bool checkSerialPermission = checkPermissions(serialDevices, true);
    if (!checkSerialPermission) {
    } else {
    }

    checkBrlttyRunning(); // No need to return value here
    checkVideoPermission(); // Check video device permissions
    bool checkPermission = checkPermissions(openterfaceDevices, false);
    return (checkSerialPermission && checkPermission && (latestFirmware == FirmwareResult::Latest) && !isBrlttyRunning) || skipCheck;
    #else
    return true;
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
    return autoCheck;
}