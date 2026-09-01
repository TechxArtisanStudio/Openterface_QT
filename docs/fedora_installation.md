# Fedora Installation Guide

This guide covers installing Openterface QT on Fedora Linux (42+).

## Table of Contents
- [Quick Install](#quick-install)
- [Installation Methods](#installation-methods)
  - [Method 1: RPM Package (Recommended)](#method-1-rpm-package-recommended)
  - [Method 2: install-release.sh Script](#method-2-install-releasesh-script)
  - [Method 3: Build from Source](#method-3-build-from-source)
- [Post-Installation Setup](#post-installation-setup)
- [Troubleshooting](#troubleshooting)
- [Uninstallation](#uninstallation)

## Quick Install

For most users, the simplest method is:

```bash
curl -fsSL https://raw.githubusercontent.com/TechxArtisanStudio/Openterface_QT/main/build-script/install-release.sh | bash
```

This automatically detects your Fedora system and installs the appropriate package.

## Installation Methods

### Method 1: RPM Package (Recommended)

The RPM package includes all dependencies and is optimized for Fedora.

#### Step 1: Download the RPM

Go to [GitHub Releases](https://github.com/TechxArtisanStudio/Openterface_QT/releases) and download the latest `.rpm` file for your architecture:

- **x86_64 (Intel/AMD)**: `openterfaceQT_*_amd64.rpm`
- **ARM64**: `openterfaceQT_*_arm64.rpm`

Or download via command line:

```bash
# Get the latest release URL
RELEASE_URL=$(curl -s https://api.github.com/repos/TechxArtisanStudio/Openterface_QT/releases/latest | grep "browser_download_url.*amd64.rpm" | cut -d '"' -f 4)

# Download
curl -L -o openterfaceqt.rpm "$RELEASE_URL"
```

#### Step 2: Install the RPM

```bash
sudo dnf install ./openterfaceqt.rpm
```

The installer will:
- Install the application to `/usr/bin/openterfaceQT`
- Install bundled Qt 6.6.3 libraries to `/usr/lib/openterfaceqt/`
- Create desktop menu entries
- Set up udev rules for USB device access

#### Step 3: Add User to Required Groups

```bash
sudo usermod -a -G dialout,video $USER
```

**Log out and back in** for group changes to take effect.

#### Step 4: Launch the Application

```bash
openterfaceQT
```

Or find "Openterface QT" in your application menu.

---

### Method 2: install-release.sh Script

The automated installation script handles everything:

```bash
curl -fsSL https://raw.githubusercontent.com/TechxArtisanStudio/Openterface_QT/main/build-script/install-release.sh | bash
```

**What it does:**
- Detects your Fedora version and architecture
- Downloads the appropriate RPM package
- Installs dependencies via `dnf`
- Configures device permissions (udev rules)
- Sets up desktop integration

**Install a specific version:**

```bash
RELEASE_VERSION="0.5.30" bash <(curl -fsSL https://raw.githubusercontent.com/TechxArtisanStudio/Openterface_QT/main/build-script/install-release.sh)
```

---

### Method 3: Build from Source

For developers or custom builds:

```bash
# Install build dependencies
sudo dnf install -y \
    qt6-qtbase-devel \
    qt6-qtmultimedia-devel \
    qt6-qtserialport-devel \
    qt6-qtsvg-devel \
    qt6-qtwebsockets-devel \
    qt6-qthttpserver-devel \
    ffmpeg-devel \
    gstreamer1-devel \
    gstreamer1-plugins-base-devel \
    libusb1-devel \
    libudev-devel \
    openssl-devel \
    tesseract-devel \
    opencv-devel \
    cmake \
    gcc-c++ \
    git

# Clone the repository
git clone https://github.com/TechxArtisanStudio/Openterface_QT.git
cd Openterface_QT

# Build
mkdir build && cd build
cmake ..
make -j$(nproc)

# Install
sudo make install
```

See [BUILD.md](BUILD.md) for detailed build instructions.

## Post-Installation Setup

### Device Permissions

The installer sets up udev rules automatically. Verify they're in place:

```bash
ls -l /etc/udev/rules.d/51-openterface.rules
```

If permissions aren't working, manually add your user to the required groups:

```bash
sudo usermod -a -G dialout,video $USER
```

**Log out and back in** for changes to take effect.

### Qt Version Compatibility

Fedora 42+ ships with Qt 6.9+, which can conflict with the bundled Qt 6.6.3. The RPM package handles this automatically:

- **If system Qt >= 6.9**: Uses system Qt libraries
- **If system Qt < 6.9 or not installed**: Uses bundled Qt 6.6.3

You shouldn't need to configure anything manually. See [fedora_qt_version_compatibility.md](fedora_qt_version_compatibility.md) for technical details.

### Verify Installation

Check that the application is installed correctly:

```bash
which openterfaceQT
openterfaceQT --version
```

Check that device permissions are configured:

```bash
groups | grep -E "dialout|video"
```

## Troubleshooting

### "No Video Signal" Warning

If you see "No video signal" even though the device is connected:

1. **Check device connection**:
   ```bash
   lsusb | grep -i "1a86:7523\|534d:2109"
   ```

2. **Verify permissions**:
   ```bash
   groups | grep -E "dialout|video"
   ```
   If missing, run `sudo usermod -a -G dialout,video $USER` and log out/in.

3. **Check udev rules**:
   ```bash
   sudo udevadm control --reload-rules
   sudo udevadm trigger
   ```

4. **Check application logs**:
   ```bash
   ls -t /tmp/openterfaceqt-app-*.log | head -1 | xargs tail -50
   ```

### Qt Library Version Errors

If you see errors like:
```
/lib64/libQt6QmlModels.so.6: version `Qt_6_PRIVATE_API' not found
```

This is a Qt version conflict. The RPM package should handle this automatically, but you can force system Qt:

```bash
# Check detected Qt version
OPENTERFACE_DEBUG=1 openterfaceQT 2>&1 | grep "Qt version"

# Force system Qt (if Fedora has Qt 6.9+)
export OPENTERFACE_FORCE_SYSTEM_QT=1
openterfaceQT
```

See [fedora_qt_version_compatibility.md](fedora_qt_version_compatibility.md) for details.

### Application Won't Start

1. **Check dependencies**:
   ```bash
   ldd /usr/bin/openterfaceQT | grep "not found"
   ```

2. **Install missing dependencies**:
   ```bash
   sudo dnf install qt6-qtbase qt6-qtmultimedia qt6-qtserialport ffmpeg-libs
   ```

3. **Check launcher logs**:
   ```bash
   ls -t /tmp/openterfaceqt-launcher-*.log | head -1 | xargs cat
   ```

### Device Not Detected

1. **Check USB connection**:
   ```bash
   lsusb | grep -E "1a86|534d"
   dmesg | tail -20
   ```

2. **Reload udev rules**:
   ```bash
   sudo udevadm control --reload-rules
   sudo udevadm trigger
   ```

3. **Check permissions**:
   ```bash
   ls -l /dev/ttyUSB* /dev/hidraw*
   ```

### Build from Source Fails

If building from source fails:

1. **Install all dependencies**:
   ```bash
   sudo dnf groupinstall "C Development Tools and Libraries"
   sudo dnf install qt6-qtbase-devel qt6-qtmultimedia-devel ffmpeg-devel gstreamer1-devel
   ```

2. **Clean and rebuild**:
   ```bash
   rm -rf build
   mkdir build && cd build
   cmake ..
   make clean
   make -j$(nproc)
   ```

See [BUILD.md](BUILD.md) for detailed troubleshooting.

## Uninstallation

### Remove RPM Package

```bash
sudo dnf remove openterfaceqt
```

### Remove User Data (Optional)

```bash
rm -rf ~/.config/openterfaceQT
rm -rf ~/.local/share/openterfaceQT
```

### Remove udev Rules (Optional)

```bash
sudo rm /etc/udev/rules.d/51-openterface.rules
sudo udevadm control --reload-rules
```

## Additional Resources

- [Main README](../README.md) - General installation instructions
- [BUILD.md](BUILD.md) - Build from source guide
- [fedora_qt_version_compatibility.md](fedora_qt_version_compatibility.md) - Qt version handling
- [Linux Permission Access Guide](https://github.com/TechxArtisanStudio/Openterface_QT/wiki/Linux-permission-access) - Permission troubleshooting
- [GitHub Issues](https://github.com/TechxArtisanStudio/Openterface_QT/issues) - Report bugs or request features

## Fedora-Specific Notes

- **Supported versions**: Fedora 42 and later
- **Architectures**: x86_64 (Intel/AMD) and ARM64
- **Package manager**: DNF
- **Qt version**: System Qt 6.9+ (Fedora 42+) or bundled Qt 6.6.3
- **Multimedia backend**: GStreamer (default on Linux)

### Known Issues

- **GStreamer plugin warnings**: You may see warnings about missing GStreamer plugins. These are usually benign and don't affect functionality.
- **Qt version detection**: On some systems, the Qt version detection may be slow. This is a one-time cost at startup.

### Performance Tips

- Use the RPM package for best performance (optimized for Fedora)
- Ensure you're in the `video` group for hardware acceleration
- Close other applications using the camera/webcam to avoid conflicts
