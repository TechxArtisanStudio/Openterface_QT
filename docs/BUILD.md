# Building Openterface QT from Source

This guide provides detailed instructions for building Openterface QT from source code on Windows and Linux.

> **💡 Quick Start (Linux):** For most users, we recommend the **one-liner release installer** (installs pre-built binary in seconds). Only build from source if you need to customize the build or contribute to development.

## Table of Contents
- [Windows](#windows)
- [Linux](#linux)
  - [Option 1: One-Liner Release Installer (Fastest)](#option-1-one-liner-release-installer-fastest)
  - [Option 2: Automated Build Script](#option-2-automated-build-script)
  - [Option 3: Manual Build Process](#option-3-manual-build-process)
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

### Option 1: One-Liner Release Installer (Fastest) ⚡

**Install the latest release in seconds** (no compilation required):

```bash
curl -fsSL https://raw.githubusercontent.com/TechxArtisanStudio/Openterface_QT/main/build-script/install-release.sh | bash
```

**Install a specific version:**
```bash
VERSION="v0.5.17" bash <(curl -fsSL https://raw.githubusercontent.com/TechxArtisanStudio/Openterface_QT/main/build-script/install-release.sh)
```

**What it does:**
- ✅ Downloads pre-built binary for your architecture (x86_64 or ARM64)
- ✅ Installs runtime dependencies (Qt6, FFmpeg, USB libraries)
- ✅ Configures device permissions (udev rules, user groups)
- ✅ Creates desktop menu integration
- ✅ Sets up Qt environment wrapper

**Supported Distributions:**
- Ubuntu/Debian (apt)
- Fedora/RHEL (dnf)
- openSUSE (zypper)
- Arch Linux (pacman)

**Installation Time:** ~30 seconds (vs 5-30 minutes for building from source)

> **💡 Recommendation:** Use this for production deployments and regular usage. Only build from source if you need custom modifications or are contributing to development.

---

### Option 2: Automated Build Script

Use our automated build script that compiles from source. Takes 5 - 30 minutes depending on hardware (Raspberry Pi takes longer).

```bash
curl -fsSL https://raw.githubusercontent.com/TechxArtisanStudio/Openterface_QT/main/build-script/install-linux.sh | bash
```

> **Note**: By default, the script builds the **latest stable version** (currently v0.5.17) automatically detected from the source code. To build an older version or the latest development version instead, use: `BUILD_VERSION="v0.3.19"` or `BUILD_VERSION="main"` before the command.

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

### Option 3: Manual Build Process

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
    libexpat-devel \
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
    libexpat-devel \
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
    -DFFMPEG_PREFIX=/usr
```

> **Note:** On Fedora, FFmpeg headers are at `/usr/include/ffmpeg/` and libraries at `/usr/lib64/`. Use `-DFFMPEG_PREFIX=/usr` (not `/usr/lib64`) to correctly locate both.

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
# Install the application, desktop entry, and icon
sudo make install
```

> **✅ What gets installed:**
> - Binary: `/usr/local/bin/openterfaceQT`
> - Desktop entry: `/usr/share/applications/com.openterface.openterfaceQT.desktop` (appears in application menu)
> - Icon: `/usr/share/icons/hicolor/256x256/apps/com.openterface.openterfaceQT.png`
>
> **⚠️ Required:** Update desktop database for menu appearance (may need logout/login otherwise):
> ```bash
> sudo update-desktop-database /usr/local/share/applications/ 2>/dev/null || true
> sudo update-desktop-database /usr/share/applications/ 2>/dev/null || true
> sudo gtk-update-icon-cache -f /usr/local/share/icons/hicolor 2>/dev/null || true
> ```
>
> **💡 Fedora GNOME Tip:** GNOME uses search-based app launcher. Press **Super** (Windows key) and type "OpenterfaceQT" or "KVM" to find it quickly.

#### Step 7: Create Qt Environment Wrapper (If Needed)

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

#### Step 8: Create Desktop Integration (Optional)

> **Note:** Desktop integration is now handled automatically by `sudo make install` (Step 6). Only follow this step if you need custom desktop entry configuration or if the automatic installation failed.

```bash
# Copy application icon
ICON_FILE="/usr/share/pixmaps/openterfaceQT.png"
if [ -f "images/icon_256.png" ]; then
    sudo cp images/icon_256.png "$ICON_FILE"
elif [ -f "images/icon_128.png" ]; then
    sudo cp images/icon_128.png "$ICON_FILE"
fi

# Create desktop entry
sudo tee /usr/share/applications/com.openterface.openterfaceQT.desktop > /dev/null << EOF
[Desktop Entry]
Version=1.0
Type=Application
Name=OpenterfaceQT
Comment=KVM over USB for seamless computer control
Exec=/usr/local/bin/openterfaceQT
Icon=$ICON_FILE
Terminal=false
Categories=Utility;Accessory;
Keywords=KVM;USB;remote;control;openterface;server;management;hardware;accessory;
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
    -DFFMPEG_PREFIX=/usr
```

> **Note:** Fedora uses `/usr/include/ffmpeg/` for headers and `/usr/lib64/` for libraries. Use `-DFFMPEG_PREFIX=/usr` to correctly locate both.

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

### CMake Errors Building v0.5.17+ on Fedora

**Error 1: FFmpeg libraries not found**
```
✗ Missing: /opt/ffmpeg/lib/libavdevice.a
CMake Error at cmake/FFmpeg.cmake:322
```

**Solution:** Fedora uses `/usr/lib64` (not `/usr/lib`) and `/usr/include/ffmpeg/` (not `/usr/include/libavformat/`).

**Required fixes in `cmake/FFmpeg.cmake`:**
```cmake
# Add lib64 to search paths
set(LIB_PATHS "${SEARCH_PATH}/lib/x86_64-linux-gnu" "${SEARCH_PATH}/lib/aarch64-linux-gnu" "${SEARCH_PATH}/lib64" "${SEARCH_PATH}/lib")

# Add Fedora-style header detection
set(_ffmpeg_header_standard "${SEARCH_PATH}/include/libavformat/avformat.h")
set(_ffmpeg_header_fedora "${SEARCH_PATH}/include/ffmpeg/libavformat/avformat.h")
```

**Error 2: qt_generate_deploy_app_script not found**
```
CMake Error at cmake/Resources.cmake:XX
Unknown CMake command "qt_generate_deploy_app_script"
```

**Solution:** This is macOS-only. Wrap in conditional in `cmake/Resources.cmake`:
```cmake
if(APPLE AND COMMAND qt_generate_deploy_app_script)
    # Only run on macOS
endif()
```

**Error 3: QElapsedTimer not declared**
```
error: 'QElapsedTimer' was not declared in this scope
```

**Solution:** Add include in `host/backend/ffmpeg/ffmpeg_capture_manager.cpp`:
```cpp
#include <QElapsedTimer>
```

> **Note:** These fixes are already applied in the latest source. If building v0.5.17 or later, they should be present.

---

## Additional Resources

- [Installation Guide (Raspberry Pi)](rpi_installation.md)
- [Features Documentation](feature.md)
- [Release Installer Script](../build-script/install-release.sh) - One-liner install from pre-built releases
- [Automated Build Script](../build-script/install-linux.sh) - Build from source
