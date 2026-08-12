# Tutorial 06 — Platform Guides

This guide covers platform-specific considerations for running Openterface Mini-KVM on Linux, Windows, Raspberry Pi, and NixOS.

## Table of Contents

| Section | Covers |
|---------|--------|
| [Linux Deep Dive](#linux-deep-dive) | Permissions, udev, BrlTTY, Wayland vs X11, Flatpak |
| [Windows Deep Dive](#windows-deep-dive) | Drivers, HID transport, installer packaging |
| [Raspberry Pi](#raspberry-pi) | ARM64 build, hardware codecs, performance tuning |
| [NixOS / Nix Flake](#nixos--nix-flake) | Flake builds, NixOS module, reproducible builds |

---

## Linux Deep Dive

### Permissions Model

The Openterface device requires access to three types of Linux device nodes:

| Device Type | Subsystem | VID:PID | Node |
|-------------|-----------|---------|------|
| HDMI Capture (MS2109) | USB | `534d:2109` | `/dev/bus/usb/...` |
| HDMI Capture HID | hidraw | `534d:2109` | `/dev/hidrawN` |
| Serial (CH9329) | ttyUSB | `1a86:7523` | `/dev/ttyUSBN` |
| Serial (CH32V208) | ttyUSB | `1a86:fe0c` | `/dev/ttyUSBN` |

### udev Rules

Create `/etc/udev/rules.d/51-openterface.rules`:

```
SUBSYSTEM=="hidraw", ATTRS{idVendor}=="534d", ATTRS{idProduct}=="2109", TAG+="uaccess"
SUBSYSTEM=="usb", ATTRS{idVendor}=="534d", ATTRS{idProduct}=="2109", TAG+="uaccess"
SUBSYSTEM=="ttyUSB", ATTRS{idVendor}=="1a86", ATTRS{idProduct}=="7523", TAG+="uaccess"
SUBSYSTEM=="usb", ATTRS{idVendor}=="1a86", ATTRS{idProduct}=="7523", TAG+="uaccess"
```

Reload rules:
```bash
sudo udevadm control --reload-rules
sudo udevadm trigger
```

### User Groups

Add your user to the required groups:
```bash
sudo usermod -a -G dialout,video $USER
sudo usermod -a -G plugdev $USER  # optional, for some distros
```

Log out and back in for group changes to take effect.

### BrlTTY Conflict

The `brltty` (Braille terminal) service commonly claims the CH9329/CH32V208 serial chip, preventing Openterface from accessing it. Symptoms: the device appears in `lsusb` but no `/dev/ttyUSB*` node is created, or the node is created but returns "Device or resource busy."

**Fix:**
```bash
# Option 1: Remove brltty entirely (if you don't need Braille support)
sudo apt remove brltty       # Debian/Ubuntu
sudo dnf remove brltty       # Fedora

# Option 2: Blacklist the device from brltty (preferred)
# Add a udev rule to prevent brltty from claiming the chip:
echo 'ATTRS{idVendor}=="1a86", ATTRS{idProduct}=="7523", ENV{BRLTTY_BRAILLE_DRIVER}=""' | sudo tee /etc/udev/rules.d/99-brltty-openterface.rules
sudo udevadm control --reload-rules
```

The application now includes detection and warnings for the BrlTTY conflict (see commit `ffda0c4`).

### Wayland vs X11

The application runs on both X11 and Wayland display servers. Qt determines the platform via `QT_QPA_PLATFORM`:

- **X11:** `QT_QPA_PLATFORM=xcb` (default on most X11 desktops)
- **Wayland:** `QT_QPA_PLATFORM=wayland` (default on GNOME/Fedora, KDE Plasma 6)

The application's `setupEnv()` function in `main.cpp` auto-detects the available display server. The GStreamer video overlay uses `GstVideoOverlay` API which works differently on X11 (XID embedding) vs Wayland (requires additional protocol support).

**Known issues on Wayland:**
- Video embedding may fall back to XWayland if the GStreamer sink doesn't support Wayland natively
- Some desktop compositors may not properly handle the video overlay window

If you experience video display issues on Wayland, try forcing X11:
```bash
QT_QPA_PLATFORM=xcb ./openterfaceQT
```

### Package Formats

The project supports multiple Linux package formats:

| Format | Use Case | Notes |
|--------|----------|-------|
| `.deb` | Debian/Ubuntu | Direct installation via `dpkg -i` |
| `.rpm` | Fedora/RHEL/openSUSE | Direct installation via `rpm -i` or `dnf install` |
| AppImage | Portable, no install | Self-contained, runs anywhere |
| Flatpak | Sandboxed | See [Flatpak section](#flatpak) below |
| Nix Flake | Reproducible builds | See [NixOS section](#nixos--nix-flake) |

### Flatpak

Flatpak provides sandboxed execution with restricted filesystem and device access.

**Installation:**
```bash
flatpak install flathub com.openterface.openterfaceQT
flatpak run com.openterface.openterfaceQT
```

**Sandbox Permissions:**
Flatpak requires explicit permission grants for device access. The manifest should include:
- `--device=all` or specific device permissions for USB/HID access
- `--filesystem=home` for saving recordings

If the Flatpak cannot detect the device, run with additional permissions:
```bash
flatpak run --device=all com.openterface.openterfaceQT
```

**Runtime:** The Flatpak uses the KDE 6.9 runtime (`org.kde.Platform`).

---

## Windows Deep Dive

### Driver Installation

**CH340 Serial Driver:**
The CH9329/CH32V208 serial chips use the CH340/CH341 USB-to-serial driver on Windows.

- Download from the manufacturer or use Windows Update
- Verify in Device Manager under "Ports (COM & LPT)" — the device should appear as "USB-SERIAL CH340 (COMx)"
- If the device shows as "Unknown device," install the CH340 driver manually

**HID Driver:**
The MS2109 HDMI capture chip uses the standard Windows HID driver stack — no additional driver needed.

### Windows-Specific HID Transport

`WindowsHIDTransport` uses Win32 HID APIs:

- `HidD_SetFeature` / `HidD_GetFeature` for feature reports
- Overlapped I/O via `CreateFile()` with `FILE_FLAG_OVERLAPPED`
- `HidSendFeatureNoTimeout()` bypasses the 5-second `HidD_SetFeature` timeout using `IOCTL_HID_SET_FEATURE`
- `reopenSync()` closes the overlapped handle and re-opens a synchronous handle for SPI flash operations (firmware updates)

### USB SetupAPI

The application links `setupapi` and `cfgmgr32` on Windows for device enumeration and management. `usb_win.h` provides Windows-specific USB utilities.

### Installer Packaging

Two installer formats are provided:

- **Inno Setup:** [`installer.iss`](installer.iss) — recommended for most Windows users
- **NSIS:** [`installer.nsi`](installer.nsi) — alternative installer format

Both create Start Menu shortcuts, register file associations, and include the CH340 driver installer.

### Admin Elevation

The installer requires administrator privileges to:
- Install USB drivers (CH340)
- Write to `Program Files`
- Create Start Menu shortcuts

The application itself does **not** require admin elevation to run — device access is granted through the standard Windows HID and COM port APIs.

---

## Raspberry Pi

### Hardware Requirements

- **Model:** Raspberry Pi 4 or 5 recommended (Pi 3 may have performance issues)
- **RAM:** 4GB+ recommended for smooth video decoding
- **OS:** Raspberry Pi OS (64-bit) or Ubuntu Server/Desktop (ARM64)

### ARM64-Specific Build

The build system auto-detects arm64 architecture and applies platform-specific settings:

```bash
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH=/usr/lib/aarch64-linux-gnu/cmake/Qt6 \
    -DOPENTERFACE_BUILD_STATIC=OFF \
    -DUSE_SHARED_FFMPEG=ON \
    -DFFMPEG_PREFIX=/usr/lib/aarch64-linux-gnu
```

**Key differences from x86_64 builds:**
- LTO is **disabled** (causes segfaults on ARM64)
- `-Os` optimization is **skipped** (causes linker issues)
- `--gc-sections` is **skipped**
- Shared FFmpeg libraries are **required** (static FFmpeg libs rarely available on ARM64)

### Hardware Codec Support

On Raspberry Pi, FFmpeg can use hardware-accelerated decoding:

- **V4L2-M2M:** Video4Linux2 memory-to-memory decoders (available on Pi 4/5)
- **VAAPI:** Not available on Raspberry Pi (Intel-specific)

Check available hardware accelerations:
```bash
# List V4L2 decoders
ffmpeg -hide_banner -decoders | grep v4l2
```

Set hardware acceleration preference in **Preferences → Video → Hardware Acceleration**.

### Performance Tuning

- **Lower resolution/frame rate** if the Pi struggles with 1080p@30fps decoding
- **Use FFmpeg backend** over GStreamer (better performance on ARM)
- **Enable TurboJPEG** fast MJPEG decoding if available (`libturbojpeg0-dev`)
- **Close other applications** to free RAM and CPU

### FFmpeg Version

The Qt Multimedia backend requires FFmpeg 6.1.1+. Check your version:

```bash
dpkg -l | grep -E "libavutil|libavcodec|libavformat|libswscale|libswresample"
```

If the system FFmpeg is too old, you may need to build FFmpeg from source or use the Qt-provided installation script.

---

## NixOS / Nix Flake

### Flake Usage

The project provides a Nix flake ([`flake.nix`](flake.nix)) for reproducible builds and NixOS integration.

**Inputs:**
- `nixpkgs`: `github:NixOS/nixpkgs/nixos-25.11`
- `flake-utils`: `github:numtide/flake-utils`

### Building with Nix

```bash
# Build the package
nix build .#openterface-qt

# Run without installing
nix run .#openterface-qt

# Enter a development shell
nix develop
```

### NixOS Module

The flake provides a NixOS module that can be enabled in your `configuration.nix`:

```nix
{
  services.openterface = {
    enable = true;
    # package = pkgs.openterface-qt;  # optional override
  };
}
```

This automatically:
- Installs the `openterface-qt` package
- Installs udev rules for device access
- Creates `dialout` and `video` groups
- Adds udev trigger rules for device hotplug

### Udev Rules (NixOS)

The flake includes udev rules that trigger on device connection:

```
SUBSYSTEM=="usb", ATTRS{idVendor}=="534d", ATTRS{idProduct}=="2109", RUN+="${pkgs.systemd}/bin/udevadm trigger"
SUBSYSTEM=="usb", ATTRS{idVendor}=="1a86", ATTRS{idProduct}=="7523", RUN+="${pkgs.systemd}/bin/udevadm trigger"
```

### Reproducible Builds

The Nix flake ensures reproducible builds by:
- Pinning exact dependency versions via `nixpkgs` revision
- Building all dependencies from source in isolated environments
- Using the same compiler flags and optimization settings on every build

To build a previous version, check out the corresponding git tag before running `nix build`:

```bash
git checkout v0.5.22
nix build .#openterface-qt
```
