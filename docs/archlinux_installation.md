# Arch Linux Installation Guide

This guide covers installing Openterface Mini-KVM on Arch Linux (rolling release) using the native pacman package.

## Overview

Arch Linux is fully supported with a native `.pkg.tar.zst` package that:
- Uses system libraries (Qt6, FFmpeg, GStreamer) — no bundled duplicates
- Automatically installs all dependencies via pacman
- Configures udev rules for USB device access
- Installs icons, desktop entry, and AppStream metadata
- Supports both x86_64 and ARM64 architectures

**Package size:** ~2.2 MB compressed / ~4.8 MB installed (vs ~150 MB if Qt6 were bundled)

## Installation Methods

### Method 1: Pre-built Package (Recommended)

Download the latest `.pkg.tar.zst` from [GitHub Releases](https://github.com/TechxArtisanStudio/Openterface_QT/releases):

```bash
# Download the package (replace URL with the latest release)
wget https://github.com/TechxArtisanStudio/Openterface_QT/releases/download/v0.5.29/openterfaceqt-0.5.29.232-1-x86_64.pkg.tar.zst

# Install — pacman auto-resolves all dependencies
sudo pacman -U openterfaceqt-*.pkg.tar.zst
```

pacman will automatically install all runtime dependencies from the Arch repos:
- Qt6 (base, declarative, multimedia, svg, serialport, wayland)
- FFmpeg, GStreamer
- libpulse, libxkbcommon, libusb, v4l-utils
- Graphics stack (libglvnd, wayland, libxcb, libx11)

### Method 2: Build from CI Artifact

Download the package built by the [Arch Linux CI workflow](https://github.com/TechxArtisanStudio/Openterface_QT/actions/workflows/build-arch-package.yml):

1. Go to the [Actions tab](https://github.com/TechxArtisanStudio/Openterface_QT/actions) and open a recent run of **Build Arch Linux Package**
2. Download the `openterfaceqt-archlinux-<version>` artifact
3. Extract and install:

```bash
unzip openterfaceqt-archlinux-*.zip
sudo pacman -U openterfaceqt-*.pkg.tar.zst
```

### Method 3: Build Locally with makepkg

Build the package from source using the PKGBUILD included in the repository:

```bash
# Clone the repository
git clone https://github.com/TechxArtisanStudio/Openterface_QT.git
cd Openterface_QT/packaging/archlinux

# Build the package (makepkg handles dependencies, compilation, and packaging)
makepkg -si

# The -s flag auto-installs build dependencies
# The -i flag auto-installs the resulting package
```

### Method 4: One-liner Installer

The generic Linux installer also supports Arch Linux:

```bash
curl -fsSL https://raw.githubusercontent.com/TechxArtisanStudio/Openterface_QT/main/build-script/install-release.sh | bash
```

## Post-Installation

### USB Device Permissions

The package installs udev rules automatically. After installation, add your user to the required groups:

```bash
# Arch Linux uses 'uucp' for serial devices (not 'dialout' like Debian/Ubuntu)
sudo usermod -a -G video,uucp $USER
```

**Log out and log back in** for group changes to take effect.

### Verify Installation

```bash
# Check the binary is installed
which openterfaceQT

# Check the package is installed
pacman -Q openterfaceqt

# Check all library dependencies are satisfied
ldd $(which openterfaceQT) | grep "not found"
# (should produce no output)

# Launch the application
openterfaceQT
```

### Installed Files

| Path | Purpose |
|------|---------|
| `/usr/bin/openterfaceQT` | Application binary |
| `/usr/lib/udev/rules.d/99-openterfaceqt.rules` | USB device access rules |
| `/usr/share/applications/com.openterface.openterfaceQT.desktop` | Desktop entry |
| `/usr/share/icons/hicolor/*/apps/com.openterface.openterfaceQT.*` | Icons (32, 64, 128, 256 px + SVG) |
| `/usr/share/metainfo/com.openterface.openterfaceQT.metainfo.xml` | AppStream metadata |
| `/usr/share/openterfaceQT/translations/` | Translation files (.qm) |

## Supported Devices

The udev rules grant access to these USB devices:

| Device | VID:PID | Subsystem |
|--------|---------|-----------|
| MS2109 HDMI Capture | 534d:2109 | usb, hidraw |
| CH340 Serial (CH9329) | 1a86:7523 | tty, ttyUSB, usb |
| CH32V208 (normal mode) | 1a86:fe0c | tty, usb |
| CH32V208 (ISP mode) | 1a86:55e0, 4348:55e0 | usb |

The `tty` subsystem rules are critical: without them, `/dev/ttyACM*` and `/dev/ttyUSB*`
stay root-only even though the underlying USB device is accessible. If keyboard input
stops working after an update, verify the rules are loaded:

```bash
udevadm info -a -n /dev/ttyACM0 | grep -E 'SUBSYSTEM|idVendor|idProduct'
```

## Troubleshooting

### Device not accessible (permission denied)

```bash
# Check if udev rules are loaded
ls /etc/udev/rules.d/99-openterfaceqt.rules || ls /usr/lib/udev/rules.d/99-openterfaceqt.rules

# Reload rules manually
sudo udevadm control --reload-rules
sudo udevadm trigger

# Verify group membership
groups $USER
# Should include: video uucp
```

### BrlTTY conflict

The `brltty` service may claim the CH340 serial chip. Fix:

```bash
# Option 1: Remove brltty (if not needed)
sudo pacman -R brltty

# Option 2: Blacklist the device from brltty
echo 'ATTRS{idVendor}=="1a86", ATTRS{idProduct}=="7523", ENV{BRLTTY_BRAILLE_DRIVER}=""' | sudo tee /etc/udev/rules.d/99-brltty-openterface.rules
sudo udevadm control --reload-rules
```

### Application fails to start (display issues)

```bash
# Try forcing X11 on Wayland
QT_QPA_PLATFORM=xcb openterfaceQT

# Try offscreen (headless/test)
QT_QPA_PLATFORM=offscreen openterfaceQT
```

### Missing shared libraries

```bash
# Check which libraries are missing
ldd $(which openterfaceQT) | grep "not found"

# Install missing packages (example)
sudo pacman -S qt6-multimedia ffmpeg gstreamer
```

## Updating

When a new version is released:

```bash
# Download the new package
wget <new-package-url>

# Install — pacman replaces the old version
sudo pacman -U openterfaceqt-*.pkg.tar.zst
```

## Uninstallation

```bash
sudo pacman -R openterfaceqt
```

## PKGBUILD Details

The PKGBUILD (`packaging/archlinux/PKGBUILD`) builds the package with these key CMake flags:

| Flag | Purpose |
|------|---------|
| `-DCMAKE_PREFIX_PATH=/usr` | Find Qt6 in system paths |
| `-DUSE_SHARED_FFMPEG=ON` | Use system FFmpeg (not bundled) |
| `-DGSTREAMER_PREFIX=/usr` | Use system GStreamer |
| `-DENABLE_QT_DEPLOY=OFF` | Don't bundle Qt6 (keeps package small) |
| `-DOPENTERFACE_BUILD_STATIC=OFF` | Use shared libraries |
| `-DBUILD_SHARED_LIBS=ON` | Build shared libraries |

## See Also

- [BUILD.md](BUILD.md) — Building from source (all platforms)
- [dependencies.md](dependencies.md) — Runtime dependency reference
- [Linux Permission Access Guide](https://github.com/TechxArtisanStudio/Openterface_QT/wiki/Linux-permission-access) — Wiki
- [Arch Linux PKGBUILD wiki](https://wiki.archlinux.org/title/PKGBUILD) — Arch Wiki
