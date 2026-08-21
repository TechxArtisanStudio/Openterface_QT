#ifndef TEST_DEVICE_BUILDER_H
#define TEST_DEVICE_BUILDER_H

#include "device/DeviceInfo.h"

/**
 * @brief Factory methods for creating test DeviceInfo objects with sensible defaults.
 *
 * Uses the real ::DeviceInfo field names (vid, pid, serialPortPath, etc.)
 * to build device fixtures that mimic Openterface hardware.
 */
class TestDeviceBuilder {
public:
    static DeviceInfo createGen1Device(const QString& portChain = "0002-0001") {
        DeviceInfo d;
        d.portChain = portChain;
        d.serialPortPath = "COM5";
        d.vid = "534D";
        d.pid = "2109";
        d.cameraDeviceId = "usb_openterface_" + portChain;
        d.cameraDevicePath = "video=Openterface_" + portChain;
        d.hidDevicePath = "\\\\.\\hid#" + portChain;
        d.hidDeviceId = "hid_openterface_" + portChain;
        d.audioDeviceId = "Openterface Audio " + portChain;
        d.audioDevicePath = "\\\\.\\audio#" + portChain;
        return d;
    }

    static DeviceInfo createGen2Device(const QString& portChain = "0002-0001") {
        DeviceInfo d = createGen1Device(portChain);
        d.vid = "1A86";
        d.pid = "FE0C";
        return d;
    }

    static DeviceInfo createGen3Device(const QString& portChain = "0002-0001") {
        DeviceInfo d = createGen1Device(portChain);
        d.vid = "345F";
        d.pid = "2132";
        return d;
    }

    static DeviceInfo createUnrelatedDevice(const QString& portChain = "0001-0002") {
        DeviceInfo d;
        d.portChain = portChain;
        d.vid = "046D";  // Logitech
        d.pid = "C52B";
        d.serialPortPath = "COM99";
        return d;
    }

    /**
     * @brief Device with all 4 interfaces (serial + camera + HID + audio).
     */
    static DeviceInfo createFullDevice(const QString& portChain = "0002-0001") {
        return createGen1Device(portChain);  // Gen1 already has all 4
    }

    /**
     * @brief Device with only serial port interface.
     */
    static DeviceInfo createSerialOnlyDevice(const QString& portChain = "0002-0001") {
        DeviceInfo d;
        d.portChain = portChain;
        d.vid = "534D";
        d.pid = "2109";
        d.serialPortPath = "COM5";
        return d;
    }

    /**
     * @brief Device with only camera interface.
     */
    static DeviceInfo createCameraOnlyDevice(const QString& portChain = "0002-0001") {
        DeviceInfo d;
        d.portChain = portChain;
        d.vid = "534D";
        d.pid = "2109";
        d.cameraDeviceId = "usb_openterface_" + portChain;
        d.cameraDevicePath = "video=Openterface_" + portChain;
        return d;
    }

    /**
     * @brief Device with only HID interface.
     */
    static DeviceInfo createHidOnlyDevice(const QString& portChain = "0002-0001") {
        DeviceInfo d;
        d.portChain = portChain;
        d.vid = "534D";
        d.pid = "2109";
        d.hidDevicePath = "\\\\.\\hid#" + portChain;
        d.hidDeviceId = "hid_openterface_" + portChain;
        return d;
    }

    /**
     * @brief Device with only audio interface (Openterface branded).
     */
    static DeviceInfo createAudioOnlyDevice(const QString& portChain = "0002-0001") {
        DeviceInfo d;
        d.portChain = portChain;
        d.vid = "534D";
        d.pid = "2109";
        d.audioDeviceId = "Openterface Audio " + portChain;
        d.audioDevicePath = "\\\\.\\audio#" + portChain;
        return d;
    }
};

#endif // TEST_DEVICE_BUILDER_H
