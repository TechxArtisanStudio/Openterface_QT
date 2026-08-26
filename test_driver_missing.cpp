// Test that simulates driver-missing scenario
// Skips Step 1 (COM port check) to simulate "driver uninstalled"
// Only runs Step 2 (USB device enumeration)
#include <windows.h>
#include <initguid.h>
#include <setupapi.h>
#include <devguid.h>
#include <regstr.h>
#include <stdio.h>
#include <QString>
#include <QCoreApplication>
#include <QSerialPortInfo>

DEFINE_GUID(GUID_DEVINTERFACE_USB_DEVICE_TEST, 0xA5DCBF10L, 0x6530, 0x11D2, 0x90, 0x1F, 0x00, 0xC0, 0x4F, 0xB9, 0x51, 0xED);

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);

    printf("========================================\n");
    printf("  SIMULATE: Driver Missing Test\n");
    printf("  (Skipping COM port check to simulate uninstalled driver)\n");
    printf("========================================\n");

    // Step 1: Show current COM ports (but pretend we found nothing)
    printf("\n========== Step 1: COM ports (SKIPPED - simulating driver uninstalled) ==========\n");
    const auto ports = QSerialPortInfo::availablePorts();
    printf("  (Actual COM ports: %d, but pretending none are CH340)\n", ports.size());
    for (const QSerialPortInfo& port : ports) {
        printf("    %s  VID=%04X  PID=%04X\n",
            port.portName().toUtf8().constData(),
            port.vendorIdentifier(),
            port.productIdentifier());
    }
    bool ch340ComPortFound = false; // Force to false to simulate missing driver

    // Step 2: Enumerate ALL devices (same as real check)
    printf("\n========== Step 2: DIGCF_ALLCLASSES enumeration ==========\n");
    bool ch9329UsbDeviceFound = false;
    bool captureCardUsbDeviceFound = false;
    int totalDevices = 0;

    HDEVINFO deviceInfoSet = SetupDiGetClassDevsW(NULL, NULL, NULL, DIGCF_PRESENT | DIGCF_ALLCLASSES);
    if (deviceInfoSet == INVALID_HANDLE_VALUE) {
        printf("  SetupDiGetClassDevs failed: error %lu\n", GetLastError());
        return 1;
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
                printf("  [FOUND] CH9329 USB device: %s\n", hwId.toUtf8().constData());
            }
            if (hwId.contains("VID_534D") && hwId.contains("PID_2109")) {
                captureCardUsbDeviceFound = true;
                printf("  [FOUND] Capture card (534D:2109): %s\n", hwId.toUtf8().constData());
            }
            if (hwId.contains("VID_345F") && (hwId.contains("PID_2109") || hwId.contains("PID_2132"))) {
                captureCardUsbDeviceFound = true;
                printf("  [FOUND] Capture card (345F): %s\n", hwId.toUtf8().constData());
            }
        }
    }
    SetupDiDestroyDeviceInfoList(deviceInfoSet);

    printf("\n  Total devices: %d\n", totalDevices);
    printf("  CH9329 USB device: %s\n", ch9329UsbDeviceFound ? "YES" : "NO");
    printf("  Capture card: %s\n", captureCardUsbDeviceFound ? "YES" : "NO");
    printf("  CH340 COM port: %s (simulated: NO)\n", ch340ComPortFound ? "YES" : "NO");

    // Step 3: Same logic as checkCH340DriverInstalled()
    printf("\n========== Decision Logic ==========\n");
    bool driverInstalled = false;

    if (ch340ComPortFound) {
        printf("  -> COM port found -> Driver INSTALLED\n");
        driverInstalled = true;
    } else if (ch9329UsbDeviceFound) {
        printf("  -> CH9329 USB device present but no COM port -> Driver MISSING\n");
        printf("  -> ACTION: Show install driver dialog!\n");
        driverInstalled = false;
    } else if (captureCardUsbDeviceFound) {
        printf("  -> Capture card found but CH9329 not exposed -> Driver likely MISSING\n");
        printf("  -> ACTION: Show install driver dialog!\n");
        driverInstalled = false;
    } else {
        printf("  -> No Openterface device found -> Nothing to check\n");
        driverInstalled = true;
    }

    printf("\n========================================\n");
    printf("  Result: %s\n", driverInstalled ? "INSTALLED (no prompt)" : "MISSING (show prompt!)");
    printf("========================================\n");

    return driverInstalled ? 0 : 1;
}
