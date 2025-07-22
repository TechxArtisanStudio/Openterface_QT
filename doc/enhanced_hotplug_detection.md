# Enhanced Openterface Device Hotplug Detection

## Overview

This refactor implements enhanced cross-platform device hotplug detection for Openterface devices based on the Python port chain detection approach. The system now provides superior device tracking and hotplug monitoring capabilities.

## Key Enhancements

### 1. Port Chain-Based Detection
- **Cross-platform port chain mapping**: Maps USB port chains (e.g., "1-1.2.3") to device groups
- **Complete device discovery**: Finds all child devices (serial, HID, camera, audio) for each Openterface hardware unit
- **Reliable device correlation**: Groups related sub-devices by their USB port chain location

### 2. Enhanced Hotplug Monitoring
- **HotplugMonitor class**: Advanced monitoring with callbacks and event tracking
- **Real-time device events**: Instant notification of device additions, removals, and modifications
- **Event statistics**: Tracks hotplug event history and timing
- **Callback system**: Allows multiple components to subscribe to device changes

### 3. Improved Device Management
- **DeviceInfo structure**: Comprehensive device information storage
- **Platform abstraction**: Unified interface for Windows and Linux device detection
- **Device selection**: Smart device selection by port chain with auto-fallback
- **Cache management**: Optimized device discovery with intelligent caching

## New Components

### Core Classes
- `HotplugMonitor`: Advanced hotplug detection with callback support
- `DeviceInfo`: Comprehensive device information container
- `DeviceManager`: Cross-platform device discovery coordinator
- `DeviceSelectorDialog`: GUI for manual device selection and monitoring

### Platform-Specific Managers
- `LinuxDeviceManager`: Enhanced Linux device detection using sysfs and udev
- `WindowsDeviceManager`: Windows device detection using SetupAPI and device trees

## Integration Points

### SerialPortManager Enhancements
- **Dual detection system**: Enhanced hotplug + legacy fallback for compatibility
- **Smart device selection**: Automatic device selection with manual override capability
- **Enhanced event handling**: Detailed device change event processing
- **Debug capabilities**: Comprehensive device status debugging

### UI Integration
- **Device Selector Dialog**: Advanced device selection and monitoring interface
- **Real-time updates**: Live device inventory updates in UI
- **Device details**: Comprehensive device information display
- **Hotplug statistics**: Event tracking and statistics display

## Benefits

### For Users
- **Reliable detection**: More reliable device detection and reconnection
- **Better feedback**: Clear indication of device status and changes
- **Manual control**: Ability to manually select devices when multiple are connected
- **Real-time monitoring**: Live updates when devices are plugged/unplugged

### For Developers
- **Clean architecture**: Well-structured, maintainable code
- **Cross-platform**: Unified interface with platform-specific implementations
- **Extensible**: Easy to add support for new platforms or device types
- **Debuggable**: Comprehensive logging and debugging capabilities

## Technical Details

### Port Chain Detection
```
USB Hub Structure -> Port Chain -> Device Group
├── 1-1 (Hub)
│   ├── 1-1.1 (Other device)
│   └── 1-1.2 (Openterface device)
│       ├── Serial Port (1A86:7523)
│       ├── HID Device (534D:2109)
│       ├── Camera Device
│       └── Audio Device
```

### Event Flow
```
Device Change -> Platform Detection -> HotplugMonitor -> 
Callbacks -> SerialPortManager -> UI Updates
```

### Configuration
- **Poll interval**: Configurable device polling interval (default: 2000ms)
- **Cache timeout**: Device discovery cache timeout (default: 1000ms)
- **Auto-selection**: Automatic device selection behavior
- **Debug logging**: Comprehensive logging categories

## Usage Examples

### Device Selection by Port Chain
```cpp
SerialPortManager& manager = SerialPortManager::getInstance();
bool success = manager.selectDeviceByPortChain("1-1.2.3");
```

### Hotplug Event Monitoring
```cpp
HotplugMonitor* monitor = manager.getHotplugMonitor();
monitor->addCallback([](const DeviceChangeEvent& event) {
    qDebug() << "Devices changed:" << event.addedDevices.size() << "added";
});
```

### Device Status Debugging
```cpp
manager.debugDeviceStatus(); // Comprehensive device status output
```

## Backward Compatibility

The enhanced system maintains full backward compatibility:
- **Legacy detection**: Original serial port detection still works as fallback
- **Existing APIs**: All existing SerialPortManager APIs remain functional
- **Configuration**: Existing settings and configurations are preserved
- **Performance**: No performance impact on existing functionality

## Future Enhancements

Potential future improvements:
- **macOS support**: Add macOS platform device manager
- **Device properties**: Extended device property detection
- **Power management**: USB device power state monitoring
- **Device filtering**: Advanced device filtering and grouping options
- **Network devices**: Support for network-connected Openterface devices
