# Building Openterface QT from Source

This guide provides detailed instructions for building Openterface QT from source code on Windows and Linux.

> **💡 Recommendation:** For most users, we recommend using the automated installation script (Linux) or pre-built binaries. Only build from source if you need to customize the build or contribute to development.

## Table of Contents
- [Windows](#windows)
- [Linux](#linux)
  - [Option 1: Automated Build Script (Recommended)](#option-1-automated-build-script-recommended)
  - [Option 2: Manual Build Process](#option-2-manual-build-process)
- [CMake Configuration Options](#cmake-configuration-options)
- [Troubleshooting](#troubleshooting)

---

## Windows

### Using QT Creator

1. **Install Qt for opensource**
   - Download from: https://www.qt.io/download-qt-installer-oss
   - Recommended version: 6.4.3

2. **Add Required Components**
   - Use Qt Maintenance Tool to add:
     - [QtMultiMedia](https://doc.qt.io/qt-6/qtmultimedia-index.html)
     - [QtSerialPort](https://doc.qt.io/qt-6/qtserialport-index.html)

3. **Get the Source Code**
   - Clone or download the repository from GitHub

4. **Build and Run**
   - Open the project in Qt Creator
   - Build and run the project

---

## Linux

### Option 1: Automated Build Script (Recommended)

Use our automated installation script that handles the entire build process. It takes 5 - 30 minutes (Raspberry Pi takes longer) to complete.

```bash
curl -fsSL https://raw.githubusercontent.com/TechxArtisanStudio/Openterface_QT/main/build-script/install-linux.sh | bash
```

> **Note**: By default, the script builds the **stable version** (currently v0.3.19) automatically detected from the source code. To build the latest development version instead, use: `BUILD_VERSION="main"` before the command.

This script automatically handles:
- Dependency installation
- Environment setup
- Building and compilation
- System integration (desktop entry, permissions, etc.)

**To build a specific version:**
```bash
# Build latest development version
BUILD_VERSION="main" bash <(curl -fsSL https://raw.githubusercontent.com/TechxArtisanStudio/Openterface_QT/main/build-script/install-linux.sh)

# Build a specific tag/version
BUILD_VERSION="v1.0.0" bash <(curl -fsSL https://raw.githubusercontent.com/TechxArtisanStudio/Openterface_QT/main/build-script/install-linux.sh)
```

---

### Option 2: Manual Build Process

If you prefer to build manually or need to customize the build process, follow these steps:

#### Prerequisites

> **💡 Tips Before Building:**
> 
> **1. Find your lrelease path**
> The lrelease tool path varies by distribution. Find yours:
> ```bash
> which lrelease
> ```
> Common paths:
> - Ubuntu/Debian: `/usr/lib/qt6/bin/lrelease`
> - Fedora/RHEL: `/usr/lib64/qt6/bin/lrelease`
> - openSUSE: `/usr/lib64/qt6/bin/lrelease`
> 
> **2. Check for existing installations**
> If you have a previous installation, remove it to avoid conflicts:
> ```bash
> sudo rm -f /usr/local/bin/openterfaceQT
> sudo rm -f /usr/share/applications/openterfaceQT.desktop
> ```
> 
> **3. Know your library paths**
> - Ubuntu/Debian: `/usr/lib/x86_64-linux-gnu`
> - Fedora/RHEL: `/usr/lib64`
> - openSUSE: `/usr/lib64`

#### Step 1: Install Dependencies

Select the commands for your distribution:

<details>
<summary><strong>🐧 Ubuntu/Debian (click to expand)</strong></summary>

```bash
# Update package lists
sudo apt-get update -y

# Install build dependencies
sudo apt-get install -y \
    build-essential \
    cmake \
    qt6-base-dev \
    qt6-multimedia-dev \
    qt6-serialport-dev \
    qt6-svg-dev \
    qt6-tools-dev \
    libusb-1.0-0-dev \
    libudev-dev \
    libgstreamer1.0-dev \
    libgstreamer-plugins-base1.0-dev \
    pkg-config \
    libx11-dev \
    libxrandr-dev \
    libxrender-dev \
    libexpat1-dev \
    libfreetype6-dev \
    libfontconfig1-dev \
    libbz2-dev \
    libturbojpeg0-dev \
    libva-dev \
    libavformat-dev \
    libavcodec-dev \
    libavdevice-dev \
    libavutil-dev \
    libswresample-dev \
    libswscale-dev \
    ffmpeg \
    libssl-dev
```

</details>

<details>
<summary><strong>🔴 Fedora/RHEL (click to expand)</strong></summary>

```bash
# Install build dependencies
sudo dnf install -y \
    gcc \
    gcc-c++ \
    cmake \
    qt6-qtbase-devel \
    qt6-qtmultimedia-devel \
    qt6-qtserialport-devel \
    qt6-qtsvg-devel \
    qt6-qttools-devel \
    libusb1-devel \
    libudev-devel \
    gstreamer1-devel \
    gstreamer1-plugins-base-devel \
    pkg-config \
    libX11-devel \
    libXrandr-devel \
    libXrender-devel \
    libex-devel \
    freetype-devel \
    fontconfig-devel \
    bzip2-devel \
    turbojpeg-devel \
    libva-devel \
    ffmpeg-devel \
    openssl-devel
```

> **Note:** Fedora uses `/usr/lib64` for libraries instead of `/usr/lib` on Ubuntu/Debian.

</details>

<details>
<summary><strong>🦎 openSUSE (click to expand)</strong></summary>

```bash
# Install build dependencies
sudo zypper install -y \
    cmake \
    gcc-c++ \
    libQt6Base-devel \
    libQt6Multimedia-devel \
    libQt6SerialPort-devel \
    libQt6Svg-devel \
    libQt6Tools-devel \
    libusb-1_0-devel \
    libudev-devel \
    gstreamer-devel \
    gstreamer-plugins-base-devel \
    pkg-config \
    libX11-devel \
    libXrandr-devel \
    libXrender-devel \
    freetype2-devel \
    fontconfig-devel \
    libbz2-devel \
    libjpeg8-devel \
    libva-devel \
    ffmpeg-devel \
    libopenssl-devel
```

</details>

#### Step 2: Configure User Permissions

```bash
# Add user to dialout and video groups for serial port and camera access
sudo usermod -a -G dialout,video $USER

# On some distros (e.g., Arch Linux), you might also need:
sudo usermod -a -G uucp $USER

# Configure device permissions (udev rules)
echo 'SUBSYSTEM=="usb", ATTRS{idVendor}=="534d", ATTRS{idProduct}=="2109", TAG+="uaccess"
SUBSYSTEM=="hidraw", ATTRS{idVendor}=="534d", ATTRS{idProduct}=="2109", TAG+="uaccess"
SUBSYSTEM=="ttyUSB", ATTRS{idVendor}=="1a86", ATTRS{idProduct}=="7523", TAG+="uaccess"
SUBSYSTEM=="usb", ATTRS{idVendor}=="1a86", ATTRS{idProduct}=="7523", TAG+="uaccess"
' | sudo tee /etc/udev/rules.d/51-openterface.rules

sudo udevadm control --reload-rules
sudo udevadm trigger
```

#### Step 3: Get the Source Code

```bash
git clone https://github.com/TechxArtisanStudio/Openterface_QT.git
cd Openterface_QT
```

#### Step 4: Generate Language Files

```bash
# Find correct lrelease path for your system
LRELEASE_PATH=$(which lrelease)
if [ -z "$LRELEASE_PATH" ]; then
    echo "lrelease not found in PATH, trying common locations..."
    if [ -x "/usr/lib/qt6/bin/lrelease" ]; then
        LRELEASE_PATH="/usr/lib/qt6/bin/lrelease"
    elif [ -x "/usr/lib64/qt6/bin/lrelease" ]; then
        LRELEASE_PATH="/usr/lib64/qt6/bin/lrelease"
    else
        echo "Warning: lrelease not found. You may need to install qt6-tools-dev"
    fi
fi

# Generate translation files
if [ -n "$LRELEASE_PATH" ]; then
    $LRELEASE_PATH openterfaceQT.pro
fi
```

#### Step 5: Build with CMake

```bash
# Create build directory
mkdir build
cd build
```

**For Ubuntu/Debian (x86_64):**
```bash
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH=/usr/lib/x86_64-linux-gnu/cmake/Qt6
```

**For Fedora/RHEL (x86_64):**
```bash
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH=/usr/lib64/cmake/Qt6 \
    -DOPENTERFACE_BUILD_STATIC=OFF \
    -DUSE_SHARED_FFMPEG=ON \
    -DFFMPEG_PREFIX=/usr/lib64
```

**For ARM64 (Raspberry Pi, etc.):**
```bash
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH=/usr/lib/aarch64-linux-gnu/cmake/Qt6
```

> **⚠️ FFmpeg Configuration Note:**
> - If you get errors about missing FFmpeg libraries, try adding: `-DUSE_SHARED_FFMPEG=ON -DFFMPEG_PREFIX=/usr/lib64` (Fedora) or `-DFFMPEG_PREFIX=/usr/lib/x86_64-linux-gnu` (Ubuntu/Debian)
> - See [CMake Configuration Options](#cmake-configuration-options) below for more details

```bash
# Compile
make -j$(nproc)
```

#### Step 6: Install System-Wide

```bash
# Install the application
sudo make install
```

#### Step 7: Create Qt Environment Wrapper (Recommended)

This prevents "Qt platform plugin" errors:

```bash
# Find Qt plugin path
QT_PLUGIN_PATH=""
if [ -d "/usr/lib/x86_64-linux-gnu/qt6/plugins" ]; then
    QT_PLUGIN_PATH="/usr/lib/x86_64-linux-gnu/qt6/plugins"
elif [ -d "/usr/lib/aarch64-linux-gnu/qt6/plugins" ]; then
    QT_PLUGIN_PATH="/usr/lib/aarch64-linux-gnu/qt6/plugins"
elif [ -d "/usr/lib/qt6/plugins" ]; then
    QT_PLUGIN_PATH="/usr/lib/qt6/plugins"
fi

if [ -n "$QT_PLUGIN_PATH" ]; then
    # Move the actual binary
    sudo mv /usr/local/bin/openterfaceQT /usr/local/bin/openterfaceQT-bin
    
    # Create wrapper script
    sudo tee /usr/local/bin/openterfaceQT > /dev/null << EOF
#!/bin/bash
export QT_PLUGIN_PATH="$QT_PLUGIN_PATH"
export QT_QPA_PLATFORM_PLUGIN_PATH="$QT_PLUGIN_PATH/platforms"
export QT_QPA_PLATFORM="xcb"
export LD_LIBRARY_PATH="/usr/lib/x86_64-linux-gnu:/usr/lib/aarch64-linux-gnu:/usr/lib:\$LD_LIBRARY_PATH"
exec /usr/local/bin/openterfaceQT-bin "\$@"
EOF
    sudo chmod +x /usr/local/bin/openterfaceQT
    echo "✅ Qt environment wrapper created"
fi
```

#### Step 8: Create Desktop Integration

```bash
# Copy application icon
ICON_FILE="/usr/share/pixmaps/openterfaceQT.png"
if [ -f "images/icon_256.png" ]; then
    sudo cp images/icon_256.png "$ICON_FILE"
elif [ -f "images/icon_128.png" ]; then
    sudo cp images/icon_128.png "$ICON_FILE"
fi

# Create desktop entry
sudo tee /usr/share/applications/openterfaceQT.desktop > /dev/null << EOF
[Desktop Entry]
Version=1.0
Type=Application
Name=Openterface QT
Comment=KVM over USB for seamless computer control
Exec=/usr/local/bin/openterfaceQT
Icon=$ICON_FILE
Terminal=false
Categories=System;Utility;Network;RemoteAccess;
Keywords=KVM;USB;remote;control;openterface;
StartupNotify=true
StartupWMClass=openterfaceQT
EOF

sudo chmod 644 /usr/share/applications/openterfaceQT.desktop

# Update desktop database
if command -v update-desktop-database &> /dev/null; then
    sudo update-desktop-database /usr/share/applications/
fi
echo "✅ Desktop integration completed"
```

#### Step 9: Run the Application

```bash
# After system installation:
openterfaceQT

# Or from build directory (without installation):
cd build
./openterfaceQT
```

---

## CMake Configuration Options

The build system supports several CMake options to customize the build:

| Option | Default | Description |
|--------|---------|-------------|
| `OPENTERFACE_BUILD_STATIC` | `ON` | Link libraries statically where possible. Set to `OFF` for dynamic linking. |
| `USE_SHARED_FFMPEG` | `OFF` | Use shared FFmpeg libraries (`.so`/`.dll`) instead of static (`.a`). Required on systems with only shared FFmpeg libs (e.g., Fedora). |
| `FFMPEG_PREFIX` | `/opt/ffmpeg` | Path to FFmpeg installation. Adjust for your system (see examples below). |
| `CMAKE_PREFIX_PATH` | (auto) | Path to Qt6 CMake configuration. Usually auto-detected, but may need manual setting. |

### Distribution-Specific CMake Examples

**Ubuntu/Debian (static FFmpeg):**
```bash
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH=/usr/lib/x86_64-linux-gnu/cmake/Qt6
```

**Ubuntu/Debian (shared FFmpeg):**
```bash
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH=/usr/lib/x86_64-linux-gnu/cmake/Qt6 \
    -DOPENTERFACE_BUILD_STATIC=OFF \
    -DUSE_SHARED_FFMPEG=ON \
    -DFFMPEG_PREFIX=/usr/lib/x86_64-linux-gnu
```

**Fedora/RHEL (shared FFmpeg - required):**
```bash
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH=/usr/lib64/cmake/Qt6 \
    -DOPENTERFACE_BUILD_STATIC=OFF \
    -DUSE_SHARED_FFMPEG=ON \
    -DFFMPEG_PREFIX=/usr/lib64
```

**openSUSE (shared FFmpeg):**
```bash
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH=/usr/lib64/cmake/Qt6 \
    -DOPENTERFACE_BUILD_STATIC=OFF \
    -DUSE_SHARED_FFMPEG=ON \
    -DFFMPEG_PREFIX=/usr/lib64
```

**ARM64 (Raspberry Pi, etc.):**
```bash
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH=/usr/lib/aarch64-linux-gnu/cmake/Qt6 \
    -DOPENTERFACE_BUILD_STATIC=OFF \
    -DUSE_SHARED_FFMPEG=ON \
    -DFFMPEG_PREFIX=/usr/lib/aarch64-linux-gnu
```

---

## Troubleshooting

### Mouse/Keyboard Not Responding

#### Common Issue: brltty Service Conflict (Runtime)

The `brltty` service can claim the serial port, preventing Openterface from accessing it. This is a **runtime issue** (not a build issue) - only remove if you experience problems:

```bash
# Remove brltty if experiencing serial port conflicts
sudo apt remove brltty

# Unplug and replug the Openterface device
# Verify the serial port is now recognized:
ls /dev/ttyUSB*
```

#### Permission Issues

If you still have issues:

```bash
# Try running with sudo to test
sudo openterfaceQT

# For permanent fix, ensure user has correct group permissions:
sudo usermod -a -G dialout,video $USER

# Log out and log back in (or reboot) for group changes to take effect
```

### FFmpeg Libraries Not Found

**Error:**
```
✗ Missing: /opt/ffmpeg/lib/libavdevice.a
CMake Error at cmake/FFmpeg.cmake:322
```

**Cause:** The build system defaults to looking for FFmpeg in `/opt/ffmpeg`, but most distributions install FFmpeg in system directories.

**Solution:** Use shared FFmpeg libraries and specify the correct path:

**For Fedora/RHEL:**
```bash
cmake .. \
    -DCMAKE_PREFIX_PATH=/usr/lib64/cmake/Qt6 \
    -DOPENTERFACE_BUILD_STATIC=OFF \
    -DUSE_SHARED_FFMPEG=ON \
    -DFFMPEG_PREFIX=/usr/lib64
```

**For Ubuntu/Debian:**
```bash
cmake .. \
    -DCMAKE_PREFIX_PATH=/usr/lib/x86_64-linux-gnu/cmake/Qt6 \
    -DOPENTERFACE_BUILD_STATIC=OFF \
    -DUSE_SHARED_FFMPEG=ON \
    -DFFMPEG_PREFIX=/usr/lib/x86_64-linux-gnu
```

> **Note:** Fedora and most modern distributions only provide shared FFmpeg libraries (`.so`), not static libraries (`.a`). You must use `-DUSE_SHARED_FFMPEG=ON` on these systems.

### Qt Platform Plugin Errors

If you get errors about Qt platform plugins:

```bash
# Set environment variables manually
export QT_PLUGIN_PATH=/usr/lib/x86_64-linux-gnu/qt6/plugins
export QT_QPA_PLATFORM_PLUGIN_PATH=/usr/lib/x86_64-linux-gnu/qt6/plugins/platforms
export QT_QPA_PLATFORM=xcb

# Or use the wrapper script created in Step 7
openterfaceQT
```

**For Fedora:**
```bash
export QT_PLUGIN_PATH=/usr/lib64/qt6/plugins
export QT_QPA_PLATFORM_PLUGIN_PATH=/usr/lib64/qt6/plugins/platforms
export QT_QPA_PLATFORM=xcb
```

### Architecture Mismatch

If you get architecture errors (like "x86_64-binfmt-P" or "cannot execute binary file"):

1. Make sure you're building on the same architecture you plan to run on
2. Clean the build directory: `rm -rf build && mkdir build`
3. Re-run the build process

---

## Additional Resources

- [Installation Guide (Raspberry Pi)](rpi_installation.md)
- [Features Documentation](feature.md)
- [Automated Install Script](../build-script/install-linux.sh)
