# Preferences Section Documentation

This document provides a comprehensive explanation of each page in the Openterface Mini KVM application's preferences section.

## Table of Contents
- [Overview](#overview)
- [General Page](#general-page)
- [Video Page](#video-page)
- [Audio Page](#audio-page)
- [Target Control Page](#target-control-page)

---

## Overview

The Preferences dialog is accessible through the main menu (Settings > Preferences) and provides comprehensive configuration options for the Openterface Mini KVM application. The preferences are organized into four main pages:

1. **General** - Logging and application behavior settings
2. **Video** - Video Output and display settings
3. **Audio** - Audio device and recording settings
4. **Target Control** - USB device mode and descriptor settings

Changes can be applied individually using the **Apply** button, or all at once using the **OK** button. The **Cancel** button closes the dialog without saving any changes.

---

## General Page

The General page contains logging settings and general application behavior options that affect the debugging and user experience.

### General Log Settings

This section controls which components of the application output debug information to the console.

**Purpose**: Enables developers and advanced users to troubleshoot issues by viewing detailed logs from specific application components.

#### Log Categories

Each checkbox enables logging for a specific component:

- **Core**: Core application functionality and initialization
- **Serial**: Serial communication with the Openterface hardware device
- **User Interface**: UI events, window management, and user interactions
- **Host**: Host computer operations (keyboard, mouse, camera management)
- **Device**: USB device operations and hardware detection
- **Backend**: Video/audio backend operations (FFmpeg, GStreamer)
- **Scripts**: Script execution and automation features

**Usage Recommendations**:
- Leave all unchecked for normal usage (best performance)
- Enable specific categories when troubleshooting issues
- Enable all when reporting bugs to get comprehensive diagnostic information

#### File Logging

**Enable file logging**: When checked, saves all log output to a file instead of (or in addition to) console output.

**Log File Path**: 
- Click **Browse** to select a directory for the log file
- The application will create an `openterface_log.txt` file in the selected location
- The file is automatically created if it doesn't exist
- Logs are appended to the file, so they persist across application sessions

**Use Cases**:
- Long-term debugging where console output would be lost
- Sharing logs with support team
- Analyzing application behavior over extended periods

### Screen Saver Settings

**Inhibit Screen Saver**: When checked, prevents the system screen saver from activating and keeps the screen from turning off during KVM use.

**Purpose**: Many KVM use cases (remote monitoring, maintenance tasks) require the screen to remain active even without direct user input to the host computer.

**Behavior**:
- When enabled, the application signals the operating system to prevent sleep/screen saver
- Works across Windows, macOS, and Linux platforms
- Automatically releases the inhibition when the application closes
- Setting is saved and persists across application restarts

**Recommended Usage**:
- Enable for active monitoring scenarios
- Enable for long-running maintenance tasks
- Disable if you want normal power management when using the KVM

---

## Video Page

The Video page controls all aspects of video Output, display, and processing. This is one of the most critical configuration sections for optimal KVM performance.

### HDMI Input Override Settings

**Override HDMI Input Setting**: Advanced option to manually specify custom input resolution.

**Purpose**: Allows forcing a specific resolution when the automatic EDID detection doesn't work correctly or when you need to ensure a specific resolution is used.

**Custom Resolution Fields**:
- **Width**: Enter the custom width in pixels (e.g., 1920)
- **Height**: Enter the custom height in pixels (e.g., 1080)

**When to Use**:
- Target computer doesn't support EDID properly
- Need to force a specific resolution for compatibility
- Working with unusual display configurations

**Warning**: Incorrect resolution settings can result in no video display or distorted image.

---

### General Video Settings

#### Output Resolution

**Output resolutions**: Dropdown showing all available video formats supported by the Output device.

The list shows resolutions in the format: `WIDTHxHEIGHT (fps) - PIXEL_FORMAT`

Example: `1920x1080 (30) - MJPEG`

**How to Choose**:
- Higher resolutions provide better quality but require more CPU/GPU resources
- Lower resolutions provide better performance on slower systems
- Match the target computer's output resolution when possible
- Consider network bandwidth if streaming remotely

**Common Resolutions**:
- **1920x1080** (Full HD): Most common, good balance of quality and performance
- **1280x720** (HD): Good for lower-powered host computers

#### Framerate

**Framerate**: Sets the video Output frame rate (frames per second).

**Available Options**: Depends on the selected resolution and hardware capabilities
- Common values: 30, 60 fps

**Recommendations**:
- **30 fps**: Smooth for most use cases, lower CPU usage
- **60 fps**: Smoother motion, better for gaming or video playback

#### Pixel Format

**Pixel format**: Defines how video data is encoded from the Output device.

**Common Formats**:
- **MJPEG**: Motion JPEG - Compressed format, lower bandwidth
- **YUYV**: Uncompressed YUV format - Higher quality, higher bandwidth
- **NV12**: YUV 4:2:0 format - Good balance of quality and efficiency
- **RGB**: Raw RGB data - Highest quality, highest resource usage

**Selection Guide**:
- MJPEG: Best for most users, good quality with reasonable CPU usage
- YUYV/NV12: Better quality when bandwidth isn't a concern
- Format availability depends on Output hardware capabilities

**Note**: On Linux, changing these settings may cause video to go black. If this occurs, unplug and re-plug the host cable (HDMI connection) to reset the video stream.

---

### Media Backend Settings

**Media Backend**: Selects which multimedia framework handles video Output and processing.

#### Available Backends:

**FFmpeg**:
- Most mature and widely compatible
- Best performance on most systems
- Excellent codec support
- Recommended default for most users

**GStreamer**:
- Alternative multimedia framework
- May provide better performance on some Linux configurations
- Useful if FFmpeg has compatibility issues
- Supports plugin-based architecture for extensibility

**Important**: Changing the media backend requires restarting the application for changes to take effect.

#### Hardware Acceleration (FFmpeg only)

**Hardware Acceleration**: Enables GPU-accelerated video processing.

**Available Options** (platform-dependent):
- **none**: Software decoding only
- **VAAPI**: Linux/Intel GPU acceleration
- **VDPAU**: Linux/NVIDIA GPU acceleration  
- **DXVA2**: Windows DirectX acceleration
- **VideoToolbox**: macOS hardware acceleration
- **CUDA**: NVIDIA GPU acceleration (cross-platform)
- **QSV**: Intel Quick Sync Video acceleration

**Benefits**:
- Significantly reduces CPU usage
- Enables higher resolutions/framerates
- Cooler operation for laptops

**Considerations**:
- Not all systems support all acceleration methods
- Requires compatible GPU and drivers
- May have compatibility issues on some systems
- Application restart required after changing

**Recommendation**: Try enabling if your system supports it. If you experience video issues, switch back to "none".

#### GStreamer Sink Priority (GStreamer only)

**GStreamer Sink Priority**: Comma-separated list of video sink plugins to try in order.

**Common Sinks**:
- **qt6videosink**: Qt6 native video rendering (recommended)
- **xvimagesink**: X11 XV extension (Linux)
- **autovideosink**: Automatically selects best available sink
- **glimagesink**: OpenGL-based rendering
- **d3d11videosink**: Direct3D 11 (Windows)

**Format**: Enter sinks separated by commas: `qt6videosink, xvimagesink, autovideosink`

**Purpose**: Allows fine-tuning of video rendering method for optimal performance on different systems.

---

## Audio Page

The Audio page provides comprehensive controls for audio device selection, recording settings, and live audio pass-through.

### Audio Device Selection

**Audio Input Device**: Selects which audio input device to use for capturing audio from the target computer.

**Dropdown Contents**:
- Lists all available audio input devices on the host computer
- Shows device names and descriptions
- Typically includes the Openterface audio Output device plus any other audio inputs

**Refresh Button**: Click to rescan for audio devices
- Use when hot-plugging audio devices
- Use if the Openterface device doesn't appear in the list
- Updates the dropdown with current system audio devices

**Current Device**: Displays information about the currently selected audio device
- Shows device name or "None" if no device selected
- Provides confirmation of which device is active

---

### Audio Recording Settings

These settings control how audio is Outputd and encoded when recording video from the target computer.

#### Audio Codec

**Audio Codec**: Selects the audio compression format for recordings.

**Available Codecs**:
- **AAC** (Advanced Audio Coding): Modern codec, excellent quality/size ratio, widely compatible
- **MP3**: Universal compatibility, good quality, larger files than AAC
- **PCM** (Pulse Code Modulation): Uncompressed audio, highest quality, large file sizes
- **FLAC** (Free Lossless Audio Codec): Lossless compression, high quality, larger than AAC/MP3

**Codec Selection Guide**:
- **AAC**: Best default choice for most recordings
- **MP3**: Use for maximum compatibility with older players
- **PCM**: Use when quality is paramount and file size doesn't matter
- **FLAC**: Use for archival quality with compression

#### Sample Rate

**Sample Rate**: Sets the audio sampling frequency in Hertz (Hz).

**Range**: 8,000 Hz to 192,000 Hz

**Common Values**:
- **8,000 Hz**: Phone quality, voice only
- **22,050 Hz**: AM radio quality
- **44,100 Hz**: CD quality (recommended default)
- **48,000 Hz**: Professional audio standard
- **96,000 Hz / 192,000 Hz**: High-resolution audio (large files)

**Recommendation**: 44,100 Hz provides excellent quality for most use cases

#### Bitrate

**Bitrate**: Sets the audio data rate in kilobits per second (kbps).

**Range**: 32 kbps to 320 kbps

**Quality Guidelines**:
- **32-64 kbps**: Low quality, acceptable for voice
- **128 kbps**: Good quality, good default for AAC/MP3
- **192 kbps**: High quality audio
- **256-320 kbps**: Maximum quality for lossy codecs

**Note**: Higher bitrates create larger files but provide better audio quality. The improvement plateau depends on the codec used.

#### Quality

**Quality**: Visual slider control for overall audio quality (1-10 scale).

- **1-3**: Low quality, smallest files
- **4-7**: Medium quality, balanced (7 recommended)
- **8-10**: High quality, larger files

**Purpose**: Provides a simple quality control that adjusts codec-specific parameters automatically. Some codecs may interpret this value differently.

#### Container Format

**Container Format**: Selects the file format wrapper for recordings.

**Available Formats**:
- **MP4**: Modern standard, good compatibility, recommended default
- **AVI**: Legacy format, wide compatibility, larger files
- **MOV**: Apple QuickTime format, good for macOS workflows
- **MKV**: Matroska format, flexible, good for archival
- **WAV**: Audio-only format, uncompressed

**Selection Guide**:
- **MP4**: Best general-purpose choice
- **AVI**: Use for compatibility with legacy systems
- **MOV**: Use when working primarily with Apple ecosystem
- **MKV**: Use for maximum flexibility and features

---

### Live Audio Settings

Controls real-time audio pass-through from the target computer to the host computer's speakers.

#### Enable Audio Pass-through

**Enable Audio Pass-through**: When checked, routes audio from target to host in real-time.

**Purpose**: 
- Hear audio from the target computer through host speakers
- Essential for monitoring target computer audio output
- Useful for troubleshooting audio issues on target

**Behavior**:
- Real-time audio with minimal latency
- Independent of recording settings
- Can be toggled during operation

#### Volume

**Volume**: Controls the playback volume for live audio pass-through (0-100%).

**Usage**:
- Adjust to comfortable listening level
- Does not affect recorded audio volume
- Setting is saved and persists across sessions

**Slider Values**:
- **0%**: Muted
- **50%**: Medium volume
- **80%**: Default/recommended 
- **100%**: Maximum volume

---

## Target Control Page

The Target Control page configures how the Openterface device presents itself to the target computer over USB, affecting compatibility and functionality.

### Target Control Operating Mode

**Purpose**: Determines what USB device types the Openterface exposes to the target computer.

**Why It Matters**: Different operating systems and BIOS implementations have varying levels of USB support. Selecting the appropriate mode ensures compatibility with your target system.

#### Available Modes:

**[Performance] Standard USB keyboard + USB mouse device + USB custom HID device**
- **Description**: Full-featured mode with complete functionality
- **USB Configuration**: Composite device with keyboard, mouse, and custom HID interfaces
- **Best For**: Windows systems, UEFI BIOS, modern target computers
- **Limitations**: 
  - Mouse may have compatibility issues with macOS
  - Some Linux distributions may not recognize all functions
  - Older BIOS may not support composite devices
- **Functionality**: Complete keyboard, mouse, and custom HID features

**[Keyboard Only] Standard USB keyboard device**
- **Description**: Simplified mode presenting only a keyboard
- **USB Configuration**: Simple HID keyboard device
- **Best For**: 
  - Legacy BIOS that don't support composite devices
  - Systems with strict USB security policies
  - Troubleshooting USB compatibility issues
- **Limitations**: 
  - No mouse control
  - No multimedia keys
  - No custom HID features
- **Functionality**: Basic keyboard input only

**[Compatibility] Standard USB keyboard + USB mouse device**
- **Description**: Balanced mode for maximum compatibility
- **USB Configuration**: Composite device with keyboard and mouse only
- **Best For**: 
  - macOS target computers
  - Linux target computers
  - Android devices
  - Systems where [Performance] mode has mouse issues
- **Limitations**: No custom HID functionality
- **Functionality**: Full keyboard and mouse control with better cross-platform compatibility

**[Custom HID] Standard USB custom HID device**
- **Description**: Custom HID mode for specialized data transmission
- **USB Configuration**: Custom HID device only
- **Best For**: 
  - Custom applications requiring HID communication
  - Data bridge between host serial port and target HID
  - Development and testing scenarios
- **Limitations**: 
  - No standard keyboard or mouse functionality
  - Requires custom software on target
- **Functionality**: Bidirectional data transmission via HID protocol

#### Selecting the Right Mode:

1. **Start with [Performance] mode** if target is a modern Windows computer
2. **Switch to [Compatibility] mode** if using macOS, Linux, or Android target
3. **Use [Keyboard Only] mode** for legacy BIOS or boot-time access
4. **Use [Custom HID] mode** only for specialized applications

**Important**: Changing the operating mode requires:
1. Applying the setting
2. Unplugging the USB cable from the target computer
3. Re-plugging the USB cable to re-enumerate the device

---

### Custom Target USB Composite Device VID and PID

**VID** (Vendor ID) and **PID** (Product ID) are USB identifiers that uniquely identify the device to the target computer.

#### Default Values:
- These are set to Openterface's standard VID/PID by default
- Changing these is an advanced operation

#### When to Customize:
- Testing device driver compatibility
- Bypassing USB device restrictions on target
- Mimicking specific hardware for testing purposes
- Development and prototyping scenarios

**Warning**: Incorrect VID/PID values may prevent the device from being recognized by the target computer.

#### VID Field:
- Enter hexadecimal value (e.g., 1234)
- Must be valid USB vendor ID

#### PID Field:
- Enter hexadecimal value (e.g., ABCD)
- Must be valid USB product ID

---

### Custom Target USB Descriptors

USB descriptors are text strings that describe the device to the target operating system. These appear in device managers and system information tools.

**Enable custom USB flag**: Master switch that enables or disables all custom USB descriptor features.

- When **unchecked**: All descriptor customization options are disabled
- When **checked**: Enables the individual descriptor options below

#### Custom Vendor Descriptor

**Purpose**: Overrides the manufacturer/vendor name string shown to the target computer.

**Default**: "Techxartisan" (or standard Openterface vendor name)

**Usage**:
1. Check the "Custom vendor descriptor" checkbox
2. Enter custom text in the field (e.g., "ACME Corporation")
3. Apply settings and reconnect USB

**Common Use Cases**:
- Corporate branding requirements
- Device driver compatibility testing
- Bypassing software restrictions based on vendor name

#### Custom Product Descriptor

**Purpose**: Overrides the product name string shown to the target computer.

**Default**: "Openterface" (or standard product name)

**Usage**:
1. Check the "Custom product descriptor" checkbox
2. Enter custom text in the field (e.g., "KVM Console")
3. Apply settings and reconnect USB

**Common Use Cases**:
- Custom device naming for inventory management
- Testing device enumeration with specific product names
- Matching expected device names in automated scripts

#### USB Serial Number

**Purpose**: Assigns a unique serial number to the device.

**Default**: Factory-set serial number or blank

**Usage**:
1. Check the "USB serial number" checkbox
2. Enter alphanumeric serial number (e.g., "SN-2024-001")
3. Apply settings and reconnect USB

**Common Use Cases**:
- Tracking multiple Openterface devices in deployment
- Device-specific licensing or configuration
- Asset management and inventory control
- Ensuring consistent device identification across reconnections

---

### Descriptor Customization Notes

**String Length Limits**:
- USB descriptors have maximum length limits (typically 126 characters)
- Very long strings may be truncated
- Recommended: Keep strings under 32 characters

**Character Support**:
- ASCII characters are universally supported
- Unicode support depends on target OS
- Special characters may not display correctly
- Recommended: Use alphanumeric characters, spaces, and basic punctuation

**Persistence**:
- Custom descriptors are stored in the Openterface device firmware
- Settings persist across power cycles and host computer changes
- Reset to defaults by unchecking options and applying

**Application Process**:
1. Make changes in the preferences dialog
2. Click **Apply** or **OK**
3. Settings are written to the device firmware
4. **Unplug USB cable from target computer**
5. **Re-plug USB cable to target computer**
6. Target computer enumerates device with new descriptors

**Troubleshooting**:
- If target doesn't recognize device after changes, revert to defaults
- Some operating systems cache USB descriptors; try a different USB port
- BIOS/UEFI may not refresh descriptors without a cold boot
- Check target's device manager to verify descriptor changes

---

## Applying Settings

### Apply Button
- Applies settings for the **current page only**
- Dialog remains open for further configuration
- Changes take effect immediately (some may require reconnection)

### OK Button
- Applies settings from **all pages**
- Closes the preferences dialog
- Saves all configuration changes

### Cancel Button
- Discards all changes made in the current session
- Closes the preferences dialog
- Previous settings remain in effect

---

## Settings Storage

All preferences are stored persistently using Qt's QSettings mechanism:

**Storage Location**:
- **Windows**: Registry under `HKEY_CURRENT_USER\Software\Techxartisan\Openterface`
- **macOS**: `~/Library/Preferences/com.techxartisan.Openterface.plist`
- **Linux**: `~/.config/Techxartisan/Openterface.conf`

Settings persist across application restarts and system reboots. To reset all settings to defaults, delete the configuration file/registry key while the application is closed.

---

## Performance Considerations

**Video Settings**:
- Higher resolutions increase CPU/memory usage
- Hardware acceleration can significantly improve performance
- MJPEG format typically provides best performance/quality balance

**Audio Settings**:
- Higher sample rates and bitrates increase file sizes
- Real-time pass-through has minimal impact on performance
- PCM/FLAC formats require more disk I/O than compressed codecs

**Logging**:
- Enabling multiple log categories may impact performance
- File logging has minimal overhead
- Disable logging for best runtime performance

---

## Recommendations for Common Scenarios

### Remote Server Management
- **Video**: 1280x720, 30fps, MJPEG, hardware acceleration
- **Audio**: Disabled or minimal settings
- **Logging**: Disable unless troubleshooting
- **Target Control**: [Compatibility] mode for best cross-platform support

### Gaming/Media Testing
- **Video**: 1920x1080 or higher, 60fps, hardware acceleration enabled
- **Audio**: 48kHz, 192kbps AAC, enable pass-through
- **Target Control**: [Performance] mode for full functionality

### Legacy System Maintenance
- **Video**: 1280x720 or 1024x768, 30fps
- **Audio**: 44.1kHz, 128kbps
- **Target Control**: [Keyboard Only] for pre-boot access

### Recording Sessions
- **Video**: Highest resolution supported, 60fps recommended
- **Audio**: 48kHz, 192kbps, AAC codec, MP4 container
- **Logging**: Enable if capturing diagnostic information

### Development/Testing
- **Logging**: Enable relevant categories
- **Target Control**: Custom operating modes and descriptors as needed
- **Video/Audio**: Adjust based on specific testing requirements

---

## Troubleshooting Tips

**Video appears black or corrupted**:
1. Try different resolution/pixel format combination
2. Disable hardware acceleration
3. Unplug and re-plug HDMI cable (especially on Linux)
4. Check other backend (FFmpeg vs GStreamer)

**No audio**:
1. Verify correct audio device selected
2. Click Refresh to rescan audio devices
3. Check enable pass-through is checked
4. Verify volume is not at 0%

**Mouse not working on target**:
1. Switch from [Performance] to [Compatibility] mode
2. Unplug and re-plug USB cable after mode change
3. Verify mouse control works in BIOS/boot screen

**Device not recognized by target**:
1. Ensure correct operating mode selected
2. Verify VID/PID if customized
3. Try [Keyboard Only] mode to test basic connectivity
4. Check USB cable is properly connected

**Settings don't persist**:
1. Always click Apply or OK, not just close dialog
2. Check file permissions on settings storage location
3. Verify not running multiple instances of application

---

## Additional Resources

- **Main Documentation**: `README.md` in project root
- **Build Instructions**: `doc/build_from_source_summary.md`
- **Feature Documentation**: `doc/feature.md`
- **Multi-language Support**: `doc/multi_language.md`
- **Video Settings**: `doc/resolutions.md`

For further assistance, consult the project documentation or visit the Openterface community forums.

---

*Document Version: 1.0*  
*Last Updated: February 2026*  
*Openterface Mini KVM Application*
