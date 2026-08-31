# Software Features

## Logging
- The software provides comprehensive logging capabilities to track user actions and system events.
- **Types of Logging**: 
  - Host
  - Core
  - Serial
  - User Interface
- Users can choose the text file where the logging data will be stored.

## Serial Console
- The software includes a Serial Console that allows users to view and analyze serial communication in real-time.
- **Message Filters**: 
  - Keyboard
  - Media Keyboard
  - Mouse Absolute
  - Mouse Relative
  - HID
  - Chip Info
- Messages are distributed as follows: 
  - `>>(message send)` 
  - `<<(message recv)`

## Video Stream Zooming
- Users can zoom in and out of video streams for better visibility and detail.
- **Button Implementation**: 
  - Three buttons are provided: 
    - **Zoom In**
    - **Zoom Out**
    - **Restore Original Size**

##  Audio playing from target
- The audio from the target device is directly transmitted to the host as an audio input, allowing users to adjust the volume either on the target device or the host.

## Variable Video Resolution and Frame Rate
- The software supports variable video resolution and frame rate settings, allowing users to customize their video output for optimal performance.
- **Resolution Options [Frame Rate]**: 
  - 640x480 [5 - 60 Hz]
  - 720x480 [5 - 60 Hz]
  - 720x576 [5 - 60 Hz]
  - 800x600 [5 - 60 Hz]
  - 1024x768 [10 -60 Hz]
  - 1280x720 [10- 60 Hz]
  - 1280x960 [5 - 50 Hz]
  - 1280x1024 [5 - 30 Hz]
  - 1360x768 [5 -30 Hz]
  - 1600x1200 [5-30 Hz]
  - 1920x1080 [5 -30 Hz]

## Screen Capture
- Users can save a screenshot from the target device to a folder on the host by clicking a button (the folder is the default media path in either Linux or Windows).

## Basic Functions of KVM
- The software supports basic KVM (Keyboard, Video, Mouse) functions, enabling seamless control of multiple devices.
- **Mouse Movement Modes**:
  - **Relative Move**: Only the target mouse is displayed, and the host mouse is disabled.
  - **Absolute Move**: Displays both the host mouse and the target mouse. The target mouse follows the host mouse's movements.
  - To exit the Relative mode, long press the **Esc** button.

## Modifications of Hardware Information
- Users can modify and update hardware information (which display on the target) directly through the software interface.
- **Modifiable Parameters**:
  - VID (Vendor ID)
  - PID (Product ID)
  - Manufacturer Name
  - Manufacturer Description
- **Note**: The modification information of all parameters corresponds to the target side.

## USB Port Switching
- The software allows for easy switching between USB ports, enhancing device management.
- The Openterface KVM has a USB interface to connect devices such as mouse and keyboard.
- Users can switch between the host side and the target side.

## Script Execution
- Users can run scripts using the Script Tool feature in the application.
- To run a script:
  1. Open the Script Tool from the main menu.
  2. Select the desired script file (e.g., `autohotkey.ahk`).
  3. Click on the "Run Script" button to execute the script.

## Editing Scripts
- Users can edit existing scripts directly within the Script Tool.
- To edit a script:
  1. Open the desired script file in the Script Tool.
  2. Make the necessary changes in the text editor provided.
  3. Click the "Save" button to save the changes to the file.

## Supported Commands
The following commands are supported in the scripts:
- **Sleep**: Pauses execution for a specified duration.
- **Send**: Sends keystrokes to the target application.
- **Click**: Simulates mouse clicks.
- **SetCapsLockState**: Toggles the Caps Lock state.
- **SetNumLockState**: Toggles the Num Lock state.
- **SetScrollLockState**: Toggles the Scroll Lock state.
- **FullScreenCapture**: Captures a full-screen image from the target device and saves it to a specified path on the host (the default path is the media directory for Windows/Linux).
- **AreaScreenCapture**: Captures a screen image of a specified area from the target device and saves it to a designated path on the host (the default path is the media directory for Windows/Linux). The area is defined using coordinates (x, y) and dimensions (width, height).

## Supported Languages
- The software currently supports the following languages:
  - Danish
  - English (default)
  - French 
  - German
  - Japanese
  - Swedish
