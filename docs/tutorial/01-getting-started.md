# Tutorial 01 — Getting Started

**Audience:** Beginners — first-time users of Openterface Mini-KVM

---

## 1. What You Need

### Hardware
- **Openterface Mini-KVM device** — The USB KVM adapter
- **Host computer** — Running Linux or Windows 10/11
- **Target computer** — Any computer with HDMI output
- **HDMI cable** — From target's HDMI output to the Mini-KVM's HDMI input
- **USB cable** — From Mini-KVM to host computer

### Optional
- **Keyboard and mouse** — Plug into the Mini-KVM's USB switchable port to control either host or target
- **Ethernet** — Not required (the device works entirely over USB)

---

## 2. Installation

### Option A: Pre-built Binary (Recommended — Download from Releases)

Preferred installation method: download the suitable package for your OS/architecture from the project's GitHub Releases page:

https://github.com/TechxArtisanStudio/Openterface_QT/releases

Choose the appropriate asset (AppImage, .deb, .rpm, .tar.gz, or Windows `.exe`) and follow the platform steps below.

**Linux (AppImage / .deb / .tar.gz):**

- AppImage (portable): download `openterfaceQT-x.y.z.AppImage`, then:

```bash
chmod +x openterfaceQT-*.AppImage
./openterfaceQT-*.AppImage
```

- .deb (Debian/Ubuntu): download `openterfaceQT_x.y.z_amd64.deb` and install:

```bash
sudo dpkg -i openterfaceQT_*.deb
sudo apt-get install -f   # fix missing deps if needed
```

- .tar.gz (generic): extract and run the provided binary or follow the included README.

**Windows (Installer or Portable):**


- Installer (.exe): Download `openterfaceQT_windows_amd64_installer.exe` from the Releases page, run it, and follow the prompts. The installer bundles any required drivers and creates Start Menu entries.

- Portable (exe): Download `openterfaceQT_windows_amd64_portable.exe` from the Releases Assets. Run the portable executable directly or extract it and run the contained `openterfaceQT.exe`. Portable builds do not modify the system and can be run from USB drives.

Notes:
- Portable builds may require the Microsoft Visual C++ Redistributable; if the app fails to start, install the redistributable from Microsoft's website.
- If your device needs a vendor driver (serial/USB), the installer includes drivers; for portable use, install the CH340/CH9329 driver or a suitable driver from the Releases Assets.

### Option B: Build from Source

See [BUILD.md](../BUILD.md) for detailed instructions. Quick version:

```bash
# Linux
git clone https://github.com/TechxArtisanStudio/Openterface_QT.git
cd Openterface_QT
mkdir build && cd build
cmake ..
make -j$(nproc)
./openterfaceQT
```

### Option C: Package Managers

| Format | Command |
|--------|---------|
| Flatpak | `flatpak install flathub com.openterface.OpenterfaceQT` (if available) |
| Nix | Use the `flake.nix` in the repository root |
| Debian/Ubuntu | Download `.deb` from releases, `sudo dpkg -i openterfaceQT.deb` |
| Fedora/RHEL | Download `.rpm` from releases, `sudo dnf install ./openterfaceQT.rpm` |

---

## 3. Connecting the Device

### Physical Connections

```
┌─────────────┐                        ┌──────────────────┐
│   TARGET    │─── HDMI cable ────────▶│  Openterface     │
│  COMPUTER   │                        │  Mini-KVM        │
└─────────────┘                        │                  │
                                       │  ◄── USB cable ──│
                                       └──────────────────┘
                                                │
                                                ▼
                                       ┌──────────────────┐
                                       │   HOST COMPUTER  │
                                       │  (this app)      │
                                       └──────────────────┘
```

1. Connect the target computer's **HDMI output** to the Mini-KVM's **HDMI input**
2. Connect the Mini-KVM's **USB output** to a **USB port on your host computer**
3. (Optional) Plug your keyboard/mouse into the Mini-KVM's USB switchable port

### Device Detection

Once connected, the Mini-KVM enumerates as multiple USB devices:
- **Video capture device** (MS2109/MS2109S/MS2130S chip) — appears as a webcam to the OS
- **Serial device** (CH340/CH9329 or CH32V208 chip) — appears as `/dev/ttyUSB*` on Linux or `COM*` on Windows
- **HID device** — used for firmware operations and register access

---

## 4. First Launch

### Splash Screen

When you launch `openterfaceQT`, you'll see a splash screen while the application initializes:
1. Settings are loaded (log settings, video settings)
2. Keyboard layouts are loaded from `:/config/keyboards/`
3. Language manager initializes (defaults to English)
4. Main window is created

The initialization is **deferred** — the window appears quickly, and device enumeration, camera setup, and audio initialization happen in the background (150ms after window show).

### Main Window Layout

```
┌─────────────────────────────────────────────────┐
│  Menu Bar    [File] [Device] [Settings] [Help]  │
├─────────────────────────────────────────────────┤
│                                                 │
│                                                 │
│              VIDEO DISPLAY AREA                  │
│         (target's screen capture)                │
│                                                 │
│                                                 │
├─────────────────────────────────────────────────┤
│ Status Bar  │ Port │ Keys │ Mouse │ Resolution │ │
└─────────────────────────────────────────────────┘
```

**Key areas:**

- **Menu Bar** — File operations, device selection, settings, help
- **Video Display Area** — Shows the live video feed from the target's HDMI output
- **Status Bar** — Shows current serial port, last key pressed, mouse position, and video resolution

### The Toolbar

A toggleable toolbar (overlay on the video area) provides quick-access buttons for:
- Mouse mode toggle (relative/absolute)
- USB switch (host/target)
- Screen capture
- Recording
- Zoom controls
- Screensaver prevention
- Ctrl+Alt+Del to target

---

## 5. Basic KVM Control

### Mouse Modes

The application supports two mouse control modes:

**Absolute Mode (default):**
- Both your host mouse and target cursor are visible
- The target cursor follows your host mouse position exactly
- Best for general use

**Relative Mode:**
- Only the target cursor is visible; the host mouse is hidden
- Mouse movements are sent as relative deltas to the target
- Best for gaming or applications that need raw mouse input
- **To exit:** Long-press the **Esc** key

You can switch modes via the toolbar toggle or the menu.

### Keyboard Input

All keyboard input is captured by the application and sent to the target via the serial chip (CH9329/CH32V208). Key features:

- **Standard keys** — All alphanumeric keys, function keys, modifiers
- **Special keys** — CapsLock, NumLock, ScrollLock state tracking
- **Paste to Target** — Send clipboard text to the target (menu: Device → Paste to Target)
- **Ctrl+Alt+Del** — Send via toolbar button or shortcut
- **Repeating keystroke** — Hold down a key at configurable intervals (useful for games)

### USB Switching

The switchable USB port on the device can be toggled between:

- **Switch to Host** — Your keyboard/mouse controls the host computer
- **Switch to Target** — Your keyboard/mouse controls the target computer

This is controlled via the toolbar toggle switch or menu. The USB switch status is also readable from the hardware switch on the physical device (GPIO0 pin).

---

## 6. Device Selection

If you have multiple Openterface devices connected, use the **Device** menu to select which one to control. The application tracks each device by its **USB port chain** — a unique path string that identifies the device's physical USB topology position.

The app auto-selects the first available device on startup.

---

## 7. Next Steps

Now that you have the basics working:
- **[Core Features →](02-core-features.md)** — Video settings, serial console, scripts, recording
- **[Troubleshooting →](07-troubleshooting.md)** — If something isn't working
