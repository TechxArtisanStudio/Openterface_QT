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
    
    // Execute pnputil to install the driver
    QProcess::execute("pnputil.exe", QStringList() << "/add-driver" << "CH341SER.INF" << "/install");
    
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
    // Use SetupDiGetClassDevs to get a list of devices
    HDEVINFO deviceInfoSet = SetupDiGetClassDevs(NULL, L"USB\\VID_1A86&PID_7523", NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (deviceInfoSet == INVALID_HANDLE_VALUE) {
        return false; // Error occurred
    }

    // Check if any devices are present
    SP_DEVINFO_DATA deviceInfoData;
    deviceInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
    for (DWORD i = 0; SetupDiEnumDeviceInfo(deviceInfoSet, i, &deviceInfoData); i++) {
        // Here you can check the device's properties if needed
        // For now, we just return true if we find any device
        SetupDiDestroyDeviceInfoList(deviceInfoSet);
        return true; // Driver is installed
    }

    SetupDiDestroyDeviceInfoList(deviceInfoSet);
    return false; // Driver not found
#elif defined(__linux__) // Check if compiling on Linux
    // Check if the driver is loaded by looking for the device file
    std::ifstream deviceFile("/dev/ttyUSB0"); // Adjust the path as necessary for your driver
    return deviceFile.good(); // Returns true if the device file exists
#else
    // Implement logic for other platforms if needed
    return false; // Assume not installed for non-Windows and non-Linux platforms
#endif
}
