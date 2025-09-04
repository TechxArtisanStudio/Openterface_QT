#ifndef DEVICEMANAGER_IMPL_H
#define DEVICEMANAGER_IMPL_H

#include "DeviceManager.h"
#include "../host/audiomanager.h"

// Template implementations for DeviceManager

template<typename CameraManagerType, typename AudioManagerType>
DeviceManager::DeviceSwitchResult DeviceManager::switchToDeviceByPortChainComplete(const QString& portChain, CameraManagerType* cameraManager, AudioManagerType* audioManager) {
    DeviceSwitchResult result = switchToDeviceByPortChain(portChain);
    
    if (!result.selectedDevice.isValid()) {
        result.statusMessage = "Invalid device selected";
        return result;
    }
    
    // Handle serial port switching if device has serial component
    if (result.selectedDevice.hasSerialPort()) {
        // Get SerialPortManager singleton and switch to the device
        // Note: We'll use extern declaration or include in implementation
        result.serialSuccess = switchSerialPortByPortChain(portChain);
        if (result.serialSuccess) {
            qCInfo(log_device_manager) << "✓ Serial port switched to device at port:" << portChain;
        } else {
            qCWarning(log_device_manager) << "Failed to switch serial port to device at port:" << portChain;
            result.statusMessage += " (Serial port switch failed)";
        }
    } else {
        result.serialSuccess = true; // No serial device to switch
    }
    
    // Handle HID switching if device has HID component
    if (result.selectedDevice.hasHidDevice()) {
        result.hidSuccess = switchHIDDeviceByPortChain(portChain);
        if (result.hidSuccess) {
            qCInfo(log_device_manager) << "✓ HID device switched to device at port:" << portChain;
        } else {
            qCWarning(log_device_manager) << "Failed to switch HID device to device at port:" << portChain;
            result.statusMessage += " (HID switch failed)";
        }
    } else {
        result.hidSuccess = true; // No HID device to switch
    }
    
    // Handle camera switching if camera manager is provided and device has camera
    if (cameraManager && result.selectedDevice.hasCameraDevice()) {
        result.cameraSuccess = cameraManager->switchToCameraDeviceByPortChain(portChain);
        if (result.cameraSuccess) {
            qCInfo(log_device_manager) << "✓ Camera switched to device at port:" << portChain;
        } else {
            qCWarning(log_device_manager) << "Failed to switch camera to device at port:" << portChain;
            result.statusMessage += " (Camera switch failed)";
        }
    } else if (result.selectedDevice.hasCameraDevice()) {
        result.cameraSuccess = false; // Camera device exists but no manager provided
        result.statusMessage += " (Camera manager not provided)";
    } else {
        result.cameraSuccess = true; // No camera device to switch
    }
    
    // Handle audio switching if audio manager is provided and device has audio
    if (audioManager && result.selectedDevice.hasAudioDevice()) {
        result.audioSuccess = audioManager->switchToAudioDeviceByPortChain(portChain);
        if (result.audioSuccess) {
            qCInfo(log_device_manager) << "✓ Audio switched to device at port:" << portChain;
        } else {
            qCWarning(log_device_manager) << "Failed to switch audio to device at port:" << portChain;
            result.statusMessage += " (Audio switch failed)";
        }
    } else if (result.selectedDevice.hasAudioDevice()) {
        result.audioSuccess = false; // Audio device exists but no manager provided
        result.statusMessage += " (Audio manager not provided)";
    } else {
        result.audioSuccess = true; // No audio device to switch
    }
    
    // Update overall success based on component switches
    bool allComponentsSuccessful = result.serialSuccess && result.hidSuccess && result.cameraSuccess && result.audioSuccess;
    if (result.success && !allComponentsSuccessful) {
        result.success = false; // Mark as failure if any component failed
    }
    
    // Update status message with overall result
    if (result.success && allComponentsSuccessful) {
        result.statusMessage = QString("Successfully switched all components to device at port %1").arg(portChain);
    } else if (!result.success) {
        if (result.statusMessage.isEmpty()) {
            result.statusMessage = QString("Failed to switch to device at port %1").arg(portChain);
        }
    }
    
    return result;
}

template<typename CameraManagerType>
DeviceManager::DeviceSwitchResult DeviceManager::switchToDeviceByPortChainComplete(const QString& portChain, CameraManagerType* cameraManager) {
    return switchToDeviceByPortChainComplete(portChain, cameraManager, static_cast<AudioManager*>(nullptr));
}

template<typename CameraManagerType>
DeviceManager::DeviceSwitchResult DeviceManager::switchToDeviceByPortChainWithCamera(const QString& portChain, CameraManagerType* cameraManager) {
    return switchToDeviceByPortChainComplete(portChain, cameraManager);
}

template<typename CameraManagerType, typename AudioManagerType>
DeviceManager::DeviceSwitchResult DeviceManager::switchToDeviceByPortChainWithCamera(const QString& portChain, CameraManagerType* cameraManager, AudioManagerType* audioManager) {
    return switchToDeviceByPortChainComplete(portChain, cameraManager, audioManager);
}

#endif // DEVICEMANAGER_IMPL_H
