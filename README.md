# Welcome to Openterface Mini-KVM QT version (For Linux & Windows)

> This is a preview version of the source code and presently, it does not support all the features found in the macOS version. We are in the process of optimizing the code and refining the building methods. Your feedback is invaluable to us. If you have any suggestions or recommendations, feel free to reach out to the project team via email. Alternatively, you can join our [Discord channel](https://discord.gg/sFTJD6a3R8) for direct discussions.

# Table of Contents
- [Welcome to Openterface Mini-KVM QT version (For Linux \& Windows)](#welcome-to-openterface-mini-kvm-qt-version-for-linux--windows)
- [Table of Contents](#table-of-contents)
  - [Features](#features)
  - [Supported OS](#supported-os)
  - [Download \& installing](#download--installing)
    - [For Windows users](#for-windows-users)
    - [For Linux users](#for-linux-users)
  - [Build from source](#build-from-source)
    - [For Windows](#for-windows)
    - [For Linux](#for-linux)
  - [FAQ](#faq)
  - [Asking questions and reporting issues](#asking-questions-and-reporting-issues)
  - [License Information](#license-information)
    - [Third-Party Libraries and Their Licenses](#third-party-libraries-and-their-licenses)
    - [Static Linking](#static-linking)
    - [License Compliance Details](#license-compliance-details)

## Features
- [x] Basic KVM operations
- [x] Mouse control absolute mode
- [x] Mouse relative mode
- [x] Audio playing from target
- [x] Paste text to Target device
- [ ] OCR text from Target device
- [ ] Other feature request? Please join the [Discord channel](https://discord.gg/sFTJD6a3R8) and tell me

> For a detailed list of features, please refer to the [Features Documentation](doc/feature.md).

## Supported OS
- Window (10/11) 
- Ubuntu 22.04 (You need to upgrade QT to >=6.4)
- Ubuntu 24.04
- Linux Mint 21.3 (Need to upgrade QT to >=6.4)
- openSUSE Tumbleweed, built by community
- Raspberry Pi OS (64-bit), working good
- Raspberry Pi OS (32-bit), Not supported, because the QT version is too old

## Download & installing
### For Windows users
1. Download the package from GitHub release page, and find the latest version to download according to your OS and CPU architecture.
2. Run the installer and it will install all required drivers and application to your windows. You can run the application from start menu.

> Note: Users have reported that the Windows installer is unable to automate driver installation correctly on Windows 11 Version 22H2. You may need to manually download and install the driver from the WCH website. For more details, please refer to this [issue](https://github.com/TechxArtisanStudio/Openterface_QT/issues/138). We are also actively working on a solution to improve driver installation for this version.

### For Linux users

#### Option 1: One-Line Installation Script (Recommended)

For a quick and automated installation, run this single command:

```bash
curl -fsSL https://raw.githubusercontent.com/TechxArtisanStudio/Openterface_QT/main/build-script/install-linux.sh | bash
```

> **Note**: By default, this script automatically builds the **stable version** (currently v0.3.19) defined in the source code. If you want to try the latest development version with the newest features, replace `main` with `dev_20250804_add_oneline_buildscript` in the URL above.

This script will automatically:
- Install all required dependencies (Qt6, FFmpeg, build tools)
- Set up user permissions for hardware access
- Configure device permissions (udev rules)
- Clone and build the stable version of the source code
- Install the application system-wide with desktop integration
- Create proper Qt environment wrappers to avoid plugin issues

**To install a specific version:**
```bash
# Install a specific version/tag
BUILD_VERSION="v1.0.0" bash <(curl -fsSL https://raw.githubusercontent.com/TechxArtisanStudio/Openterface_QT/refs/heads/main/build-script/install-linux.sh)

# Install latest development version
BUILD_VERSION="main" bash <(curl -fsSL https://raw.githubusercontent.com/TechxArtisanStudio/Openterface_QT/refs/heads/main/build-script/install-linux.sh)
```

**For interactive installation with customization options:**
```bash
# Download the script first
curl -o install-linux.sh https://raw.githubusercontent.com/TechxArtisanStudio/Openterface_QT/refs/heads/main/build-script/install-linux.sh

# Make it executable and run interactively
chmod +x install-linux.sh
./install-linux.sh
```

#### Option 2: Manual Installation from Release Package

1. Download the package from GitHub release page, and find the latest version to download according to your OS and CPU architecture.
2. Install the dependency
3. Setup dialout and video groups for Serial and camera device access, and the hidraw permission for Switchable USB device
4. Install the package.

 ```bash
# Setup the QT 6.4.2 or later runtime and other dependencies
sudo apt install -y \
    libqt6core6 \
    libqt6dbus6 \
    libqt6gui6 \
    libqt6network6 \
    libqt6multimedia6 \
    libqt6multimediawidgets6 \
    libqt6serialport6 \
    libqt6svg6 \
    libqt6concurrent6t64 \
    libusb-1.0-0-dev \
    libssl-dev \
    libavutil58 \
    libavformat60 \
    libavdevice60 \
    libturbojpeg0 \
    libva1 \
    libva-drm2 \
    libva-x11-2 \
    libgstreamer1.0-0 \
 ```

```bash
# Setup the dialout permission for Serial port and video group for camera access
sudo usermod -a -G dialout,video $USER
# On some distros (e.g. Arch Linux) this might be called uucp
sudo usermod -a -G uucp $USER
```

```bash
# Setup the hidraw and serial port permissions
echo 'SUBSYSTEM=="usb", ATTRS{idVendor}=="534d", ATTRS{idProduct}=="2109", TAG+="uaccess"
SUBSYSTEM=="hidraw", ATTRS{idVendor}=="534d", ATTRS{idProduct}=="2109", TAG+="uaccess"
SUBSYSTEM=="ttyUSB", ATTRS{idVendor}=="1a86", ATTRS{idProduct}=="7523", TAG+="uaccess"
SUBSYSTEM=="usb", ATTRS{idVendor}=="1a86", ATTRS{idProduct}=="7523", TAG+="uaccess"
' | sudo tee /etc/udev/rules.d/51-openterface.rules 
sudo udevadm control --reload-rules
sudo udevadm trigger
```

 ```bash
# Unzip the package and install
unzip openterfaceQT.deb.zip
sudo dpkg -i openterfaceQT.deb
 ```

 ```bash
# Run from terminal 
openterfaceQT
 ```

## Build from source
### For Windows
- Using QT Creator
  1. Install [QT for opensource](https://www.qt.io/download-qt-installer-oss), recommended version 6.4.3
  2. Use Qt Maintenance Tool to add following components
     - [QtMultiMedia](https://doc.qt.io/qt-6/qtmultimedia-index.html)
     - [QtSerialPort](https://doc.qt.io/qt-6/qtserialport-index.html)
  3. Download the source and import the project
  4. Now you can run the project

### For Linux

#### Option 1: Automated Build Script (Recommended)

Use our automated installation script that handles the entire build process. It takes 5 - 30 minutes (Raspberry PI take longer) to complete the whole process.

```bash
curl -fsSL https://raw.githubusercontent.com/TechxArtisanStudio/Openterface_QT/main/build-script/install-linux.sh | bash
```

> **Note**: By default, the script builds the **stable version** (currently v0.3.19) automatically detected from the source code. To build the latest development version instead, use: `BUILD_VERSION="main"` before the command.

This script automatically handles dependency installation, environment setup, building, and system integration.

**To build a specific version:**
```bash
# Build latest development version
BUILD_VERSION="main" bash <(curl -fsSL https://raw.githubusercontent.com/TechxArtisanStudio/Openterface_QT/main/build-script/install-linux.sh)

# Build a specific tag/version
BUILD_VERSION="v1.0.0" bash <(curl -fsSL https://raw.githubusercontent.com/TechxArtisanStudio/Openterface_QT/main/build-script/install-linux.sh)
```

#### Option 2: Manual Build Process

If you prefer to build manually or need to customize the build process:

> **💡 Tips Before Building:**
> 
> **Find your lrelease path**
> The lrelease tool path varies by distribution. Find yours:
> ```bash
> which lrelease
> ```
> Common paths:
> - Ubuntu/Debian: `/usr/lib/qt6/bin/lrelease`
> - Fedora/RHEL: `/usr/lib64/qt6/bin/lrelease`
> - openSUSE: `/usr/lib64/qt6/bin/lrelease`
> 
> **Check for existing installations**
> If you have a previous installation, remove it to avoid conflicts:
> ```bash
> sudo rm -f /usr/local/bin/openterfaceQT
> sudo rm -f /usr/share/applications/openterfaceQT.desktop
> ```

``` bash
# Build environment preparation   
sudo apt-get update -y
sudo apt-get install -y \
    build-essential \
    cmake \
    qt6-base-dev \
    qt6-multimedia-dev \
    qt6-serialport-dev \
    qt6-svg-dev \
    libusb-1.0-0-dev \
    qt6-tools-dev \
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

```bash
# Setup the dialout permission for Serial port and video group for camera access
sudo usermod -a -G dialout,video $USER
# On some distros (e.g. Arch Linux) this might be called uucp
sudo usermod -a -G uucp $USER

# Setup the hidraw and serial port permissions
echo 'SUBSYSTEM=="usb", ATTRS{idVendor}=="534d", ATTRS{idProduct}=="2109", TAG+="uaccess"
SUBSYSTEM=="hidraw", ATTRS{idVendor}=="534d", ATTRS{idProduct}=="2109", TAG+="uaccess"
SUBSYSTEM=="ttyUSB", ATTRS{idVendor}=="1a86", ATTRS{idProduct}=="7523", TAG+="uaccess"
SUBSYSTEM=="usb", ATTRS{idVendor}=="1a86", ATTRS{idProduct}=="7523", TAG+="uaccess"
' | sudo tee /etc/udev/rules.d/51-openterface.rules 
sudo udevadm control --reload-rules
sudo udevadm trigger
```

``` bash
# Get the source
git clone https://github.com/TechxArtisanStudio/Openterface_QT.git
cd Openterface_QT

# Generate language files (find correct lrelease path for your system)
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
if [ -n "$LRELEASE_PATH" ]; then
    $LRELEASE_PATH openterfaceQT.pro
fi

# Build the project with CMake
mkdir build
cd build

# Auto-detect FFmpeg static library paths (more robust than hardcoded paths)
echo "Auto-detecting FFmpeg libraries..."
LIBAVFORMAT=$(find /usr/lib /usr/lib64 -name "libavformat.a" 2>/dev/null | head -1)
LIBAVCODEC=$(find /usr/lib /usr/lib64 -name "libavcodec.a" 2>/dev/null | head -1)
LIBAVUTIL=$(find /usr/lib /usr/lib64 -name "libavutil.a" 2>/dev/null | head -1)
LIBSWRESAMPLE=$(find /usr/lib /usr/lib64 -name "libswresample.a" 2>/dev/null | head -1)
LIBSWSCALE=$(find /usr/lib /usr/lib64 -name "libswscale.a" 2>/dev/null | head -1)

# Detect architecture and set appropriate Qt6 cmake path
ARCH=$(uname -m)
if [ "$ARCH" = "aarch64" ] || [ "$ARCH" = "arm64" ]; then
    QT_CMAKE_PATH="/usr/lib/aarch64-linux-gnu/cmake/Qt6"
    echo "Building for ARM64 architecture"
else
    QT_CMAKE_PATH="/usr/lib/x86_64-linux-gnu/cmake/Qt6"
    echo "Building for x86_64 architecture"
fi

# Build with auto-detected paths (or fallback to defaults)
if [ -n "$LIBAVFORMAT" ] && [ -n "$LIBAVCODEC" ] && [ -n "$LIBAVUTIL" ] && [ -n "$LIBSWRESAMPLE" ] && [ -n "$LIBSWSCALE" ]; then
    FFMPEG_LIBRARIES="$LIBAVFORMAT;$LIBAVCODEC;$LIBAVUTIL;$LIBSWRESAMPLE;$LIBSWSCALE"
    cmake .. \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_PREFIX_PATH="$QT_CMAKE_PATH" \
        -DFFMPEG_LIBRARIES="$FFMPEG_LIBRARIES"
else
    echo "Warning: Some FFmpeg static libraries not found, using default paths"
    cmake .. \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_PREFIX_PATH="$QT_CMAKE_PATH"
fi

make -j$(nproc)
```

``` bash
# Install system-wide (recommended)
sudo make install

# Create Qt environment wrapper script (prevents "Qt platform plugin" errors)
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

# Create desktop entry (for application menu integration)
ICON_FILE="/usr/share/pixmaps/openterfaceQT.png"
if [ -f "images/icon_256.png" ]; then
    sudo cp images/icon_256.png "$ICON_FILE"
elif [ -f "images/icon_128.png" ]; then
    sudo cp images/icon_128.png "$ICON_FILE"
fi

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
if command -v update-desktop-database &> /dev/null; then
    sudo update-desktop-database /usr/share/applications/
fi
echo "✅ Desktop integration completed"

# Run from build directory (without system installation)
# ./openterfaceQT

# Run from system installation (after sudo make install)
# openterfaceQT
```

``` bash
# Troubleshooting: Mouse/Keyboard not responding

## Common Issue: brltty Service Conflict (Runtime)
# The brltty service can claim the serial port, preventing Openterface from accessing it.
# This is a RUNTIME issue (not a build issue) - only remove if you experience problems:
sudo apt remove brltty

# After removing brltty, unplug and replug the Openterface device
# Verify the serial port is now recognized:
ls /dev/ttyUSB*

## Permission Issues
# If still having issues, try running with sudo once to test:
sudo openterfaceQT

# For permanent fix, ensure user has correct group permissions:
sudo usermod -a -G dialout,video $USER
# Log out and log back in (or reboot) for group changes to take effect
```

## FAQ
 - Keyboard and Mouse not responding in Windows
   - The CH340 serial chip driver wasn't installed properly during setup, you have two options:
     1. Download and install the driver directly from the [WCH website](https://www.wch-ic.com/downloads/CH341SER_EXE.html)
     2. Install the driver from our [source repository](https://github.com/TechxArtisanStudio/Openterface_QT/blob/main/driver/windows/CH341SER.INF) by running this command as Administrator:
      ```
        pnputil -a CH341SER.INF
      ```
 - Keyboard and Mouse not responding in Linux
   - Likely the CH340 serial chip driver is missing in your OS, you should 
      1. **Download the driver**: Visit the driver [website](https://www.wch-ic.com/downloads/CH341SER_EXE.html) and download the appropriate driver for Linux.

      2. **Install the driver**:
         - Extract the downloaded file.
         - Open a terminal and navigate to the extracted folder.
         - Run the following commands to compile and install the driver:
           ```bash
           make
           sudo make install
           ```

      3. **Load the driver**:
         After installation, load the driver using:
         ```bash
         sudo modprobe ch341
         ```

      4. **Reconnect the device**: Unplug and reconnect the OpenTouch interface to see if the mouse and keyboard inputs are now being sent to the target.

      If the issue persists, it could also be related to permissions or udev rules. Ensure that your user has the necessary permissions to access the device. Please refer to [For Linux users](#for-linux-users)
  

## Asking questions and reporting issues

We encourage you to engage with us.
- On [Discord](https://discord.gg/sFTJD6a3R8) to ask questions and report issues.
- On [GitHub](https://github.com/TechxArtisanStudio/Openterface_QT/issues) to report issues.
- Email to [techxartisan@gmail.com](mailto:techxartisan@gmail.com) to ask questions and report issues.

## License Information

The Openterface Mini-KVM QT project is licensed under the **AGPL-3.0** (Affero General Public License). This license allows you to use, modify, and redistribute the software under the following conditions:

1. **Source Code Availability**: If you distribute the software, you must make the source code available to the recipients under the same AGPL-3.0 license.
2. **Modification**: You are free to modify the software, but you must also distribute your modifications under the AGPL-3.0 license.
3. **No Warranty**: The software is provided "as is", without warranty of any kind.

For more details, please refer to the full text of the AGPL-3.0 license: [GNU AGPL v3.0](https://www.gnu.org/licenses/agpl-3.0.en.html).

### Third-Party Libraries and Their Licenses

This project uses the following third-party libraries, each with its own licensing terms:

- **Qt Framework**: LGPL v3
- **libusb (1.0.26)**: LGPL v2.1
- **FreeType (2.13.2)**: FreeType License (BSD-style) / GPL v2
- **Fontconfig (2.14.2)**: MIT License
- **D-Bus**: AFL v2.1 or GPL v2
- **PulseAudio (16.1)**: LGPL v2.1+
- **libxkbcommon (1.7.0)**: MIT License
- **xproto, libXdmcp, libXau, xcb, xcb-util**: Generally under permissive licenses

### Static Linking

Most of the libraries used in this project are compatible with AGPL, especially if you provide the source code or object files for LGPL libraries. Ensure compliance with the terms of each license, especially for LGPL libraries.

### License Compliance Details

1. Complete source code is available in this repository.
2. Build instructions are provided above.
3. You have the right to modify and redistribute the code under the terms of the AGPL-3.0.
4. You have the right to relink with different versions of the LGPL libraries.
5. For any modifications to the code, you must make the source code available under AGPL-3.0.

The full text of all licenses can be found in the [license](LICENSE) file of this repository.

For more information about the licenses:
- [GNU AGPL v3.0](https://www.gnu.org/licenses/agpl-3.0.en.html)
- [GNU LGPL v2.1](https://www.gnu.org/licenses/lgpl-2.1.en.html)
- [GNU LGPL v3.0](https://www.gnu.org/licenses/lgpl-3.0.en.html)
- [MIT License](https://opensource.org/licenses/MIT)
- [Academic Free License v2.1](https://opensource.org/licenses/AFL-2.1)
