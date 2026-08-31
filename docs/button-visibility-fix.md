# Control Firmware Page Button Visibility Fix

## Problem Description

In the Preferences > Control Firmware page, the Connect, Disconnect, and Flash buttons are hard to see when no device has been scanned, because:
1. The buttons are set to disabled state during initialization (`setEnabled(false)`)
2. Disabled buttons appear as light gray on some themes, making them hard to distinguish
3. There is no clear visual cue telling the user what action to perform first

## Fix Approach

### 1. Added stylesheets for Connect and Disconnect buttons

**File**: `ui/preferences/controlchipfirmwarepage.cpp`

Added a custom stylesheet to make disabled buttons more visible:

```cpp
QString disabledBtnStyle = R"(
    QPushButton:disabled {
        background-color: #f0f0f0;
        color: #666666;
        border: 1px solid #cccccc;
        border-radius: 4px;
        padding: 5px 15px;
    }
    QPushButton {
        background-color: #ffffff;
        color: #333333;
        border: 1px solid #bbbbbb;
        border-radius: 4px;
        padding: 5px 15px;
    }
    QPushButton:hover {
        background-color: #e8e8e8;
        border: 1px solid #999999;
    }
    QPushButton:pressed {
        background-color: #d0d0d0;
    }
)";
m_connectBtn->setStyleSheet(disabledBtnStyle);
m_disconnectBtn->setStyleSheet(disabledBtnStyle);
```

**Effect**:
- Disabled state: light gray background (#f0f0f0), dark gray text (#666666), clear border
- Enabled state: white background, dark text
- Hover and press states also have clear visual feedback

### 2. Added stylesheet for Flash button

```cpp
QString flashBtnStyle = R"(
    QPushButton:disabled {
        background-color: #f0f0f0;
        color: #666666;
        border: 2px solid #cccccc;
        border-radius: 4px;
        padding: 8px 20px;
        font-weight: bold;
    }
    QPushButton {
        background-color: #4CAF50;
        color: white;
        border: 2px solid #45a049;
        border-radius: 4px;
        padding: 8px 20px;
        font-weight: bold;
    }
    QPushButton:hover {
        background-color: #45a049;
    }
    QPushButton:pressed {
        background-color: #3d8b40;
    }
)";
m_flashBtn->setStyleSheet(flashBtnStyle);
```

**Effect**:
- Disabled state: light gray background, dark gray text, bold border
- Enabled state: green background (#4CAF50), white text, bold font
- Clear visual distinction, making the Flash button more prominent

### 3. Added tooltips

```cpp
m_connectBtn->setToolTip(tr("Click 'Scan Devices' first to find available WCH devices"));
m_disconnectBtn->setToolTip(tr("Connect to a device first before disconnecting"));
m_flashBtn->setToolTip(tr("Connect to a device and select firmware file first"));
```

**Effect**:
- Tooltip text appears when hovering over buttons
- Clearly tells the user what action to perform first
- Improves user experience

## Button State Logic

The button enable/disable logic remains unchanged:

### Connect Button
- **Disabled condition**: Device is connected OR no devices have been scanned
- **Enabled condition**: No device is connected AND devices have been scanned (`m_deviceCombo->count() > 0`)
- **State updated**: In `setConnectedState()` and `onDevicesFound()`

### Disconnect Button
- **Disabled condition**: No device is connected
- **Enabled condition**: Device is connected
- **State updated**: In `setConnectedState()`

### Flash Button
- **Disabled condition**: No device is connected OR no firmware file is selected OR an operation is in progress
- **Enabled condition**: Device is connected AND firmware file is selected AND no operation is in progress
- **State updated**: In `updateFlashButton()`

## Visual Improvement Comparison

### Before Fix
- Disabled buttons: system default gray, possibly hard to see
- No tooltips
- No significant visual distinction between buttons

### After Fix
- Disabled buttons: light gray background, dark gray text, clear border
- Clear tooltips
- Connect/Disconnect buttons are visually distinct from the Flash button
- Hover and press states have visual feedback

## Test Steps

1. Rebuild the application:
   ```bash
   cd build
   make
   ```

2. Run the application:
   ```bash
   ./openterfaceQT
   ```

3. Open Preferences > Control Firmware page

4. Verify:
   - ✅ Connect and Disconnect buttons are clearly visible when not scanned (gray but recognizable)
   - ✅ Flash button is clearly visible when not connected (gray but recognizable)
   - ✅ Tooltip text appears when hovering over buttons
   - ✅ After clicking "Scan Devices", if devices are found, the Connect button becomes enabled (white background)
   - ✅ After connecting a device, the Disconnect button becomes enabled
   - ✅ After selecting a firmware file, the Flash button becomes enabled (green background)

## Related Files

- `ui/preferences/controlchipfirmwarepage.cpp` - Main modified file
- `ui/preferences/controlchipfirmwarepage.h` - Header file (unchanged)

## Compatibility

- ✅ Does not affect existing functionality
- ✅ Does not affect button state logic
- ✅ Only improves visual appearance
- ✅ Supports all Qt themes (uses QSS stylesheets)

## Future Suggestions

For further improvements, consider:
1. Adding animation effects for smooth transitions when button states change
2. Using icons to enhance button visual identification
3. Adding a step indicator showing the current workflow (Scan → Connect → Select Firmware → Flash)
4. Adding status text next to buttons, such as "Waiting for scan..." / "Ready to connect"
