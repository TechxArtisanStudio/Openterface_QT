#include <QCoreApplication>
#include <QDebug>
#include "wch/WCHFlasher.h"
#include "wch/WCHUSBTransport.h"

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);

    qDebug() << "=== Flash Protection Diagnostic ===\n";

    try {
        WCHUSBTransport transport;

        // Scan for devices
        auto devices = transport.scanDevices();
        if (devices.empty()) {
            qCritical() << "No WCH ISP devices found!";
            qCritical() << "Please ensure device is in ISP mode (4348:55e0 or 1a86:55e0)";
            return 1;
        }

        qDebug() << "Found" << devices.size() << "WCH ISP device(s)";
        for (size_t i = 0; i < devices.size(); i++) {
            qDebug() << "  Device" << i << ":" << QString::fromStdString(devices[i]);
        }

        // Open first device
        qDebug() << "\nOpening device 0...";
        transport.open(0);
        qDebug() << "✓ Transport opened";

        // Create flasher
        qDebug() << "\nCreating flasher...";
        WCHFlasher flasher(&transport);
        qDebug() << "✓ Flasher created";

        // Get chip info
        std::string chipInfo = flasher.getChipInfo();
        qDebug() << "\n=== Chip Information ===";
        qDebug() << QString::fromStdString(chipInfo);

        // Check protection status
        bool isProtected = flasher.isCodeFlashProtected();
        qDebug() << "\n=== Protection Status ===";
        qDebug() << "Flash protected:" << (isProtected ? "YES ⚠️" : "NO ✓");

        if (isProtected) {
            qDebug() << "\n⚠️  Flash is PROTECTED";
            qDebug() << "This means:";
            qDebug() << "  1. Flash cannot be written until unprotected";
            qDebug() << "  2. Unprotect requires writing config and resetting device";
            qDebug() << "  3. After reset, device drops USB and must reconnect";
            qDebug() << "  4. Reconnection can fail if timing is wrong";
            qDebug() << "\nAttempting to unprotect...";

            try {
                flasher.unprotect();
                qDebug() << "✓ Unprotect succeeded";
                qDebug() << "Device should have reset and reconnected";
            } catch (const std::exception& e) {
                qCritical() << "✗ Unprotect failed:" << e.what();
                qCritical() << "This is likely why flash appeared to succeed but didn't work!";
            }
        } else {
            qDebug() << "\n✓ Flash is UNPROTECTED";
            qDebug() << "Ready to flash firmware";
        }

        transport.close();
        qDebug() << "\n=== Diagnostic Complete ===";

    } catch (const std::exception& e) {
        qCritical() << "\n✗ Error:" << e.what();
        return 1;
    }

    return 0;
}
