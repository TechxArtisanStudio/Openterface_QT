# Tutorial 02 — Core Features

**Audience:** Beginners to Intermediate — daily use features

This document covers daily features users will interact with.

## Table of Contents

| # | Feature | Description |
|---|---------|-------------|
| 1 | [Video Stream](#1-video-stream) | Resolution, frame rate, zoom, screenshot |
| 2 | [Audio from Target](#2-audio-from-target) | Audio routing, volume control |
| 3 | [Serial Console](#3-serial-console) | Debug communication, message filters |
| 4 | [Script Tool](#4-script-tool) | Automation commands and examples |
| 5 | [Recording](#5-recording) | Capture video/audio to file |
| 6 | [Multi-Language Support](#6-multi-language-support) | Switch UI language |
| 7 | [Keyboard Layouts](#7-keyboard-layouts) | Layout selection and scancode mapping |
| 8 | [Baudrate Selection](#8-baudrate-selection) | Serial communication speed |
| 9 | [Screensaver Prevention](#9-screensaver-prevention) | Keep host display awake |

---

Each subsection links to implementation notes in `docs/` where available.

---

## 1. Video Stream

### Resolution and Frame Rate

The Mini-KVM captures the target's HDMI output and can deliver variable resolutions and frame rates. Supported resolutions include:

| Resolution | Frame Rate Range |
|------------|-----------------|
| 640x480    | 5 – 60 Hz       |
| 720x480    | 5 – 60 Hz       |
| 720x576    | 5 – 60 Hz       |
| 800x600    | 5 – 60 Hz       |
| 1024x768   | 10 – 60 Hz      |
| 1280x720   | 10 – 60 Hz      |
| 1280x960   | 5 – 50 Hz       |
| 1280x1024  | 5 – 30 Hz       |
| 1360x768   | 5 – 30 Hz       |
| 1600x1200  | 5 – 30 Hz       |
| 1920x1080  | 5 – 30 Hz       |

The resolution is determined by what the **target** is outputting over HDMI. The capture chip reads the EDID (Extended Display Identification Data) to negotiate the resolution.

### Changing Resolution

To change the target's output resolution:
1. Open **Settings → Video** to configure the capture card's preferred resolution
2. Or use **Device → Update Display Settings** to push custom EDID values to the target

### Zoom Controls

Three zoom buttons are available on the toolbar:
- **Zoom In** — Enlarge the video display
- **Zoom Out** — Shrink the video display
- **Restore/Fit** — Reset to fit the window

When zoomed in, you can scroll to pan around the video area. The video pane maintains the correct aspect ratio at all zoom levels.

### Screen Capture

Click the **camera icon** on the toolbar to save a screenshot of the current target screen. The image is saved to your OS's default media folder:
- **Linux:** `~/Pictures` or `XDG_PICTURES_DIR`
- **Windows:** `C:\Users\<name>\Pictures`

---

## 2. Audio from Target

The target computer's audio is transmitted to the host through the capture device as an audio input source.

### How It Works

The HDMI capture chip extracts the audio embedded in the HDMI signal and presents it as a USB audio input device to the host OS. The application routes this audio through Qt's audio system (or GStreamer, depending on the backend).

### Volume Control

- **Target side** — Adjust volume on the target computer as normal
- **Host side** — Use your host OS's audio mixer to adjust the capture device's input volume

### Audio Settings

In **Settings → Audio**, you can configure:
- Audio input device selection
- Volume level
- Mute toggle

The mute button is also accessible from the toolbar.

---

## 3. Serial Console

The Serial Console provides real-time visibility into the serial communication between the host application and the HID controller chip (CH9329/CH32V208) inside the Mini-KVM.

### Opening the Console

Menu: **Device → Serial Port Debug** (or equivalent menu item)

### Message Types

The console displays serial protocol messages with directional indicators:
- `>>` — Message sent from host to device
- `<<` — Message received from device to host

### Message Filters

You can filter which message types are displayed:
- **Keyboard** — Keypress and release events
- **Media Keyboard** — Media control keys (play, volume, etc.)
- **Mouse Absolute** — Absolute mouse position commands
- **Mouse Relative** — Relative mouse movement commands
- **Mouse** — Mouse button events
- **HID** — General HID communication
- **Chip Info** — Chip configuration and status information

### Serial Port Debugging

The debug dialog shows:
- Current serial port (`/dev/ttyUSB*` or `COM*`)
- Current baudrate (9600 or 115200)
- Chip type (CH9329 or CH32V208)
- Connection status
- Real-time message log

---

## 4. Script Tool

The Script Tool lets you automate keyboard and mouse actions on the target device using a custom scripting language.

### Opening the Script Tool

Menu: **Device → Script Tool**

### The Script Language

The scripting language is inspired by AutoHotKey with a simpler syntax. It has a full compilation pipeline: **Lexer → Parser → SemanticAnalyzer → ScriptExecutor**.

#### Basic Commands

| Command | Description | Example |
|---------|-------------|---------|
| `Sleep` | Pause execution | `Sleep 1000` (1 second) |
| `Send` | Send keystrokes | `Send Hello World` |
| `Click` | Mouse click | `Click 100 200` (x, y) |
| `SetCapsLockState` | Toggle CapsLock | `SetCapsLockState On` |
| `SetNumLockState` | Toggle NumLock | `SetNumLockState Off` |
| `SetScrollLockState` | Toggle ScrollLock | `SetScrollLockState On` |
| `FullScreenCapture` | Screenshot to file | `FullScreenCapture "/path/to/screenshot.png"` |
| `AreaScreenCapture` | Area screenshot | `AreaScreenCapture "/path.png" 0 0 800 600` |

### Writing and Running Scripts

1. Open the Script Tool
2. Type or paste your script in the text editor
3. Click **Run Script** to execute
4. The script sends commands through the serial port to the target

### Example Script

```
Sleep 1000
Send hello world
Click 500 300
Sleep 500
SetCapsLockState On
Send HELLO
FullScreenCapture "/tmp/screenshot.png"
```

### Supported File Formats

The Script Tool can load and save `.ahk` (AutoHotKey-compatible) files and custom script formats.

---

## 5. Recording

The application can record the target's video and audio stream to a file.

### Starting a Recording

Click the **record button** on the toolbar, or use the menu option. Recording saves to your OS's default media directory.

### Recording Settings

Access advanced recording settings via:
- **Device → Recording Settings**

Configurable options:
- Output format (MP4, etc.)
- Video bitrate
- Audio codec settings
- Output directory

### Recording Indicator

When recording is active, a timer shows in the toolbar/status bar.

### Backend-Specific Recording

- **FFmpeg backend** — Uses `FFmpegRecorder` for direct encoding
- **GStreamer backend** — Uses `RecordingManager` with GStreamer pipelines

See [Settings → Video](03-advanced-features.md#preferences-system) to check which backend is active.

---

## 6. Multi-Language Support

The application supports multiple UI languages.

### Switching Language

Menu: **Settings → Language** → Select your language

### Supported Languages

| Language | Code |
|----------|------|
| English | en (default) |
| Danish | da |
| German | de |
| Spanish | es |
| French | fr |
| Italian | it |
| Japanese | ja |
| Korean | ko |
| Portuguese | pt |
| Romanian | ro |
| Russian | ru |
| Swedish | se |
| Chinese | zh |

Language changes take effect immediately without restarting the application.

---

## 7. Keyboard Layouts

The application supports different physical keyboard layouts to correctly map keys to the target.

### Available Layouts

- QWERTY US (default)
- QWERTY UK
- QWERTY Danish
- QWERTY Spanish
- QWERTY Swedish
- QWERTZ German
- AZERTY French
- AZERTY Belgian
- Japanese

### Changing Layout

Menu: **Settings → Keyboard Layout** → Select your layout

This is important because key scancodes differ between layouts — without the correct layout setting, keys may be sent incorrectly to the target.

### Adding a New Layout

See [keyboard_layout_adding.md](../keyboard_layout_adding.md) for instructions on adding a new keyboard layout JSON file to `config/keyboards/`.

---

## 8. Baudrate Selection

The serial communication speed (baudrate) between the host and the HID chip can be adjusted.

### Available Baudrates

- **9600** (default) — More reliable, better compatibility, especially on ARM devices
- **115200** — Faster communication, better performance on x86

### Changing Baudrate

Menu: **Settings → Baudrate** → Select 9600 or 115200

> **Note:** Changing the baudrate requires disconnecting and reconnecting the device. The application will prompt you to do so.

### ARM Performance Recommendation

On ARM architectures (Raspberry Pi), the application may recommend using 9600 baud for better stability. Higher baudrates on ARM can lead to command loss due to USB controller limitations.

---

## 9. Screensaver Prevention

The application can prevent the host screensaver from activating while you're using the target.

Toggle: **Device → Prevent Screensaver**

This periodically sends a synthetic event to keep the host display awake.

---

## Next Steps

- **[Advanced Features →](03-advanced-features.md)** — EDID management, firmware updates, diagnostics, preferences
- **[Troubleshooting →](07-troubleshooting.md)** — Common problems and solutions
