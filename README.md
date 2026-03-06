# Welcome to Openterface Mini-KVM QT version (For Linux & Windows)

> This is a preview version of the source code and presently, it does not support all the features found in the macOS version. We are in the process of optimizing the code and refining the building methods. Your feedback is invaluable to us. If you have any suggestions or recommendations, feel free to reach out to the project team via email. Alternatively, you can join our [Discord channel](https://discord.gg/sFTJD6a3R8) for direct discussions.

# Table of Contents
- [Welcome to Openterface Mini-KVM QT version (For Linux \& Windows)](#welcome-to-openterface-mini-kvm-qt-version-for-linux--windows)
- [Table of Contents](#table-of-contents)
  - [Features](#features)
  - [Supported OS](#supported-os)
  - [Download \& Installing](#download--installing)
    - [For Windows Users](#for-windows-users)
    - [For Linux Users](#for-linux-users)
  - [Build from Source](#build-from-source)
  - [FAQ](#faq)
  - [Asking Questions and Reporting Issues](#asking-questions-and-reporting-issues)
  - [License Information](#license-information)

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

#### Option 1: One-Liner Release Installer (Fastest ⚡)

**Install the latest pre-built release in ~30 seconds** (no compilation):

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

**Supported Distributions:** Ubuntu/Debian, Fedora/RHEL, openSUSE, Arch Linux

> **💡 Recommendation:** Use this for fastest installation. Only build from source if you need custom modifications.

#### Option 2: Build from Source (Automated Script)

For building from source with automated dependency handling:

```bash
curl -fsSL https://raw.githubusercontent.com/TechxArtisanStudio/Openterface_QT/main/build-script/install-linux.sh | bash
```

> **Note**: By default, this script automatically builds the **stable version** (currently v0.5.17) defined in the source code. Takes 5-30 minutes depending on hardware.

**To build a specific version:**
```bash
# Build a specific version/tag
BUILD_VERSION="v1.0.0" bash <(curl -fsSL https://raw.githubusercontent.com/TechxArtisanStudio/Openterface_QT/refs/heads/main/build-script/install-linux.sh)

# Build latest development version
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

#### Option 3: Manual Installation from Release Package

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

## Build from Source

For detailed build instructions, see **[docs/BUILD.md](docs/BUILD.md)**.

**Quick links:**
- 🪟 [Windows Build Guide](docs/BUILD.md#windows)
- 🐧 [Linux Build Guide](docs/BUILD.md#linux)
  - [Automated Script (Recommended)](docs/BUILD.md#option-1-automated-build-script-recommended)
  - [Manual Build Process](docs/BUILD.md#option-2-manual-build-process)
- 🔧 [Troubleshooting](docs/BUILD.md#troubleshooting)

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
