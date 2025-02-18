#include "ui/driverdialog.h"
#include "ui_driverdialog.h"
#include <QPushButton> // Include QPushButton header
#include <QMessageBox> // Include QMessageBox header
#include <QCloseEvent> // Include QCloseEvent header
#include <QApplication> // Include QApplication header
#include <QProcess> // Include QProcess header
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

    // Connect buttons to their respective slots
    connect(ui->okButton, &QPushButton::clicked, this, &DriverDialog::accept); // Close dialog on OK
    connect(ui->quitButton, &QPushButton::clicked, this, &DriverDialog::reject); // Close dialog on Quit
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

// Override accept method
void DriverDialog::accept()
{
    // Implement logic for when OK is clicked
    QMessageBox::information(this, "Information", "Proceeding with driver installation.");
    
    // Log the driver installation attempt
    std::cout << "Attempting to install driver using pnputil." << std::endl;

    // Execute pnputil to install the driver
    QProcess::execute("pnputil.exe", QStringList() << "/add-driver" << "CH341SER.INF" << "/install");
    
    // Log the completion of the installation attempt
    std::cout << "Driver installation command executed." << std::endl;

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
