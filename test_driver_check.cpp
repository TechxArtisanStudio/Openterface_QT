// Standalone test for CH9329 USB device detection and CH340 driver check
// Compile: g++ -o test_driver_check test_driver_check.cpp -lsetupapi -lole32
// Run:     ./test_driver_check.exe

#include <windows.h>
#include <initguid.h>
#include <setupapi.h>
#include <devguid.h>
#include <regstr.h>
#include <stdio.h>

// Define USB device interface GUID
DEFINE_GUID(GUID_DEVINTERFACE_USB_DEVICE_TEST, 0xA5DCBF10L, 0x6530, 0x11D2, 0x90, 0x1F, 0x00, 0xC0, 0x4F, 0xB9, 0x51, 0xED);
#include <QString>
#include <QCoreApplication>
#include <QSerialPortInfo>
#include <QList>

void printAllVIDDevices() {
    printf("\n========== ALL devices with VID_ in Hardware ID ==========\n");
    HDEVINFO deviceInfoSet = SetupDiGetClassDevsW(NULL, NULL, NULL, DIGCF_PRESENT | DIGCF_ALLCLASSES);
    if (deviceInfoSet == INVALID_HANDLE_VALUE) {
        printf("  SetupDiGetClassDevs failed: error %lu\n", GetLastError());
        return;
    }

    SP_DEVINFO_DATA deviceInfoData;
    deviceInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
    WCHAR hwIdBuffer[512];
    int count = 0;

    for (DWORD i = 0; SetupDiEnumDeviceInfo(deviceInfoSet, i, &deviceInfoData); i++) {
        // Try SPDRP_HARDWAREID
        if (SetupDiGetDeviceRegistryPropertyW(deviceInfoSet, &deviceInfoData, SPDRP_HARDWAREID, NULL,
            (PBYTE)hwIdBuffer, sizeof(hwIdBuffer), NULL)) {
            QString hwId = QString::fromWCharArray(hwIdBuffer);
            if (hwId.contains("VID_", Qt::CaseInsensitive)) {
                count++;
                printf("  [%03d] %s\n", count, hwId.toUtf8().constData());

                // Also get device description
                WCHAR descBuffer[256];
                if (SetupDiGetDeviceRegistryPropertyW(deviceInfoSet, &deviceInfoData, SPDRP_DEVICEDESC, NULL,
                    (PBYTE)descBuffer, sizeof(descBuffer), NULL)) {
                    printf("        Desc: %s\n", QString::fromWCharArray(descBuffer).toUtf8().constData());
                }
                // Get class
                WCHAR classBuffer[64];
                if (SetupDiGetDeviceRegistryPropertyW(deviceInfoSet, &deviceInfoData, SPDRP_CLASS, NULL,
                    (PBYTE)classBuffer, sizeof(classBuffer), NULL)) {
                    printf("        Class: %s\n", QString::fromWCharArray(classBuffer).toUtf8().constData());
                }
            }
        }
    }
    SetupDiDestroyDeviceInfoList(deviceInfoSet);
    printf("  Total VID devices: %d\n", count);
}

void printUSBDevices() {
    printf("\n========== USB class devices (GUID_DEVINTERFACE_USB_DEVICE) ==========\n");
    // Also try enumerating just USB devices
    HDEVINFO deviceInfoSet = SetupDiGetClassDevsW(&GUID_DEVINTERFACE_USB_DEVICE_TEST, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (deviceInfoSet == INVALID_HANDLE_VALUE) {
        printf("  SetupDiGetClassDevs(USB) failed: error %lu\n", GetLastError());
        return;
    }

    SP_DEVICE_INTERFACE_DATA interfaceData;
    interfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);
    int count = 0;

    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(deviceInfoSet, NULL, &GUID_DEVINTERFACE_USB_DEVICE_TEST, i, &interfaceData); i++) {
        // Get required buffer size
        DWORD requiredSize = 0;
        SetupDiGetDeviceInterfaceDetailW(deviceInfoSet, &interfaceData, NULL, 0, &requiredSize, NULL);

        SP_DEVICE_INTERFACE_DETAIL_DATA_W* detailData = (SP_DEVICE_INTERFACE_DETAIL_DATA_W*)malloc(requiredSize);
        detailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

        SP_DEVINFO_DATA devInfoData;
        devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);

        if (SetupDiGetDeviceInterfaceDetailW(deviceInfoSet, &interfaceData, detailData, requiredSize, NULL, &devInfoData)) {
            WCHAR hwIdBuffer[512];
            if (SetupDiGetDeviceRegistryPropertyW(deviceInfoSet, &devInfoData, SPDRP_HARDWAREID, NULL,
                (PBYTE)hwIdBuffer, sizeof(hwIdBuffer), NULL)) {
                count++;
                printf("  [%03d] %s\n", count, QString::fromWCharArray(hwIdBuffer).toUtf8().constData());
                printf("        Path: %s\n", QString::fromWCharArray(detailData->DevicePath).toUtf8().constData());
            }
        }
        free(detailData);
    }
    SetupDiDestroyDeviceInfoList(deviceInfoSet);
    printf("  Total USB interface devices: %d\n", count);
}

bool checkCH340DriverInstalled() {
    printf("\n========== Step 1: Check COM ports (QSerialPortInfo) ==========\n");
    bool ch340ComPortFound = false;
    const auto ports = QSerialPortInfo::availablePorts();
    printf("Total COM ports found: %d\n", ports.size());
    for (const QSerialPortInfo& port : ports) {
        printf("  Port: %s  VID=%04X  PID=%04X  %s\n",
            port.portName().toUtf8().constData(),
            port.vendorIdentifier(),
            port.productIdentifier(),
            (port.vendorIdentifier() == 0x1A86 && port.productIdentifier() == 0x7523) ? "<-- CH340/CH9329!" : "");
        if (port.vendorIdentifier() == 0x1A86 && port.productIdentifier() == 0x7523) {
            ch340ComPortFound = true;
        }
    }

    if (ch340ComPortFound) {
        printf("\n[RESULT] CH340 COM port found -> Driver is INSTALLED\n");
        return true;
    }
    printf("\n[RESULT] No CH340 COM port found\n");

    printf("\n========== Step 2: DIGCF_ALLCLASSES enumeration ==========\n");
    bool ch9329UsbDeviceFound = false;
    bool captureCardUsbDeviceFound = false;
    int totalDevices = 0;

    HDEVINFO deviceInfoSet = SetupDiGetClassDevsW(NULL, NULL, NULL, DIGCF_PRESENT | DIGCF_ALLCLASSES);
    if (deviceInfoSet == INVALID_HANDLE_VALUE) {
        printf("  SetupDiGetClassDevs failed: error %lu\n", GetLastError());
        return true;
    }

    SP_DEVINFO_DATA deviceInfoData;
    deviceInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
    WCHAR hwIdBuffer[512];

    for (DWORD i = 0; SetupDiEnumDeviceInfo(deviceInfoSet, i, &deviceInfoData); i++) {
        totalDevices++;
        if (SetupDiGetDeviceRegistryPropertyW(deviceInfoSet, &deviceInfoData, SPDRP_HARDWAREID, NULL,
            (PBYTE)hwIdBuffer, sizeof(hwIdBuffer), NULL)) {
            QString hwId = QString::fromWCharArray(hwIdBuffer);
            if (hwId.contains("VID_1A86") && hwId.contains("PID_7523")) {
                ch9329UsbDeviceFound = true;
                printf("  [FOUND] CH9329: %s\n", hwId.toUtf8().constData());
            }
            if (hwId.contains("VID_534D") || hwId.contains("VID_345F")) {
                captureCardUsbDeviceFound = true;
                printf("  [FOUND] Capture card: %s\n", hwId.toUtf8().constData());
            }
        }
    }
    SetupDiDestroyDeviceInfoList(deviceInfoSet);
    printf("  Total devices: %d, CH9329: %s, Capture card: %s\n",
        totalDevices,
        ch9329UsbDeviceFound ? "YES" : "NO",
        captureCardUsbDeviceFound ? "YES" : "NO");

    printf("\n========== Final Result ==========\n");
    if (ch9329UsbDeviceFound) {
        printf("[DRIVER MISSING] CH9329 present but no COM port -> driver NOT installed\n");
        return false;
    }
    if (captureCardUsbDeviceFound) {
        printf("[DRIVER MISSING] Capture card found but CH9329 not exposed -> driver likely NOT installed\n");
        return false;
    }
    printf("[NO DEVICE] No Openterface device found\n");
    return true;
}

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);

    printf("========================================\n");
    printf("  CH9329 Driver Check Test\n");
    printf("========================================\n");

    bool driverOk = checkCH340DriverInstalled();

    // Print all VID devices for debugging
    printAllVIDDevices();

    // Print USB interface devices
    printUSBDevices();

    printf("\n========================================\n");
    printf("  Driver check returned: %s\n", driverOk ? "INSTALLED (OK)" : "MISSING (NEED INSTALL)");
    printf("========================================\n");

    return driverOk ? 0 : 1;
}
