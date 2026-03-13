# Openterface Mini-KVM QT Edition

**Control your target computer remotely with keyboard, video, and mouse over a single USB connection.**

[![License: AGPL v3](https://img.shields.io/badge/License-AGPL%20v3-blue.svg)](https://www.gnu.org/licenses/agpl-3.0)
[![Latest Release](https://img.shields.io/github/release/TechxArtisanStudio/Openterface_QT)](https://github.com/TechxArtisanStudio/Openterface_QT/releases)
[![Discord Community](https://img.shields.io/discord/1126137258053894215?label=Discord&logo=discord)](https://discord.gg/sFTJD6a3R8)

## What is This?

Openterface Mini-KVM is a **compact KVM (Keyboard-Video-Mouse) switch** that lets you control a target computer remotely from your host machine using a single USB connection. No network required. Ideal for:
- 💻 Managing headless servers and embedded systems
- 🎮 Controlling IoT devices and Raspberry Pi systems  
- 🔧 Hardware debugging and development
- 🏢 Data center and lab management
- 📱 Controlling phones, tablets, and ARM devices

> **Note:** This is the QT version for Linux & Windows. Currently in active development with feature parity improvements underway.

---

## Table of Contents
- [Quick Start](#quick-start)
- [Features](#features)
- [Supported OS](#supported-os)
- [Installation](#installation)
  - [Windows](#windows-installation)
  - [Linux](#linux-installation)
- [Build from Source](#build-from-source)
- [Development](#development)
- [Troubleshooting](#troubleshooting)
- [Support & Community](#support--community)
- [License](#license)

---

## Quick Start

### 🪟 Windows
1. Download the latest installer from [Releases](https://github.com/TechxArtisanStudio/Openterface_QT/releases)  
2. Run the installer (it installs drivers + app automatically)
3. Launch **Openterface Mini-KVM** from your Start Menu
4. Connect your Mini-KVM device via USB

### 🐧 Linux (Ubuntu/Debian)

**Fastest way (~30 seconds):**
```bash
curl -fsSL https://raw.githubusercontent.com/TechxArtisanStudio/Openterface_QT/main/build-script/install-release.sh | bash
```

Then run:
```bash
openterfaceQT
```

**For other methods and detailed setup**, see [Installation](#installation) section below.

---

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

| OS | Version | Status |
|---|---|---|
| 🪟 **Windows** | 10, 11 (x86 only) | ✅ Supported |
| 🐧 **Ubuntu** | 22.04+ | ✅ Supported |
| **Linux Mint** | 21.3+ | ✅ Supported (Qt 6.4+ required) |
| **openSUSE** | Tumbleweed | ✅ Supported (community-built) |
| 🔋 **Raspberry Pi OS** | 64-bit | ✅ Supported |
| 🔋 **Raspberry Pi OS** | 32-bit | ❌ Not supported (Qt too old) |

---

## Installation

### Windows Installation

1. Download the latest installer from [GitHub Releases](https://github.com/TechxArtisanStudio/Openterface_QT/releases) (x86 architecture)
2. Run the installer — it will automatically install drivers and the application
3. Launch **Openterface Mini-KVM** from your Start Menu or application folder
4. Connect your Mini-KVM device via USB

> **Note:** On Windows 11 Version 22H2, some users report driver installation issues. If keyboard/mouse don't respond, manually install the CH340 driver from the [WCH website](https://www.wch-ic.com/downloads/CH341SER_EXE.html) or use the [repository driver](https://github.com/TechxArtisanStudio/Openterface_QT/blob/main/driver/windows/CH341SER.INF) with `pnputil -a CH341SER.INF` (run as Administrator).

---

### Linux Installation

Choose the method that best suits your needs:

| Method | Speed | Effort | Best For |
|--------|-------|--------|----------|
| **Pre-built Binary** | ⚡ ~30 sec | Minimal | Most users — fastest & easiest |
| **Build from Source** | 5-30 min | Medium | Custom modifications needed |
| **Manual Install** | Varies | High | Advanced users & troubleshooting |

#### Option 1: Pre-built Binary (Recommended for Most Users)

**Install in ~30 seconds (no compilation):**

```bash
curl -fsSL https://raw.githubusercontent.com/TechxArtisanStudio/Openterface_QT/main/build-script/install-release.sh | bash
```

**Supported distributions:** Ubuntu/Debian, Fedora/RHEL, openSUSE, Arch Linux

**What it does automatically:**
- ✅ Downloads pre-built binary for your architecture (x86_64 or ARM64)
- ✅ Installs runtime dependencies (Qt6, FFmpeg, USB libraries)
- ✅ Configures device permissions (udev rules, user groups)
- ✅ Creates desktop menu integration

**To install a specific version:**
```bash
VERSION="v0.5.17" bash <(curl -fsSL https://raw.githubusercontent.com/TechxArtisanStudio/Openterface_QT/main/build-script/install-release.sh)
```

Then launch:
```bash
openterfaceQT
```

---

#### Option 2: Build from Source (for Custom Modifications)

**Quick automated build (5-30 minutes depending on hardware):**

```bash
curl -fsSL https://raw.githubusercontent.com/TechxArtisanStudio/Openterface_QT/main/build-script/install-linux.sh | bash
```

By default, this builds the stable version. To build a specific version:

```bash
# Build a specific version/tag
BUILD_VERSION="v1.0.0" bash <(curl -fsSL https://raw.githubusercontent.com/TechxArtisanStudio/Openterface_QT/refs/heads/main/build-script/install-linux.sh)

# Build latest development version
BUILD_VERSION="main" bash <(curl -fsSL https://raw.githubusercontent.com/TechxArtisanStudio/Openterface_QT/refs/heads/main/build-script/install-linux.sh)
```

**For detailed build instructions and developer setup**, see [Build from Source](#build-from-source) and [Development](#development) sections below.

---

#### Option 3: Manual Installation

For advanced users who prefer manual control:

1. Download the pre-built `.deb` package from [GitHub Releases](https://github.com/TechxArtisanStudio/Openterface_QT/releases)
2. Install dependencies:

```bash
sudo apt install -y \
    libqt6core6 libqt6dbus6 libqt6gui6 libqt6network6 \
    libqt6multimedia6 libqt6multimediawidgets6 libqt6serialport6 \
    libqt6svg6 libqt6concurrent6t64 libusb-1.0-0-dev libssl-dev \
    libavutil58 libavformat60 libavdevice60 libturbojpeg0 \
    libva1 libva-drm2 libva-x11-2 libgstreamer1.0-0
```

3. Configure permissions:

```bash
# Add serial and video permissions
sudo usermod -a -G dialout,video $USER
# On Arch Linux, use 'uucp' instead of 'dialout'
sudo usermod -a -G uucp $USER

# Setup udev rules for device access
echo 'SUBSYSTEM=="usb", ATTRS{idVendor}=="534d", ATTRS{idProduct}=="2109", TAG+="uaccess"
SUBSYSTEM=="hidraw", ATTRS{idVendor}=="534d", ATTRS{idProduct}=="2109", TAG+="uaccess"
SUBSYSTEM=="ttyUSB", ATTRS{idVendor}=="1a86", ATTRS{idProduct}=="7523", TAG+="uaccess"
SUBSYSTEM=="usb", ATTRS{idVendor}=="1a86", ATTRS{idProduct}=="7523", TAG+="uaccess"' | sudo tee /etc/udev/rules.d/51-openterface.rules 

sudo udevadm control --reload-rules
sudo udevadm trigger
```

4. Install the package:

```bash
unzip openterfaceQT.deb.zip
sudo dpkg -i openterfaceQT.deb
```

5. Run the application:

```bash
openterfaceQT
```

> **Note:** You may need to log out and log back in for group permissions to take effect.

---

## Build from Source

For detailed build instructions, see [docs/BUILD.md](docs/BUILD.md).

**Quick links:**
- 🪟 [Windows Build Guide](docs/BUILD.md#windows)
- 🐧 [Linux Build Guide](docs/BUILD.md#linux)
  - [Automated Script (Recommended)](docs/BUILD.md#option-1-automated-build-script-recommended)
  - [Manual Build Process](docs/BUILD.md#option-2-manual-build-process)
- 🔧 [Troubleshooting](docs/BUILD.md#troubleshooting)

---

## Development

Want to contribute? We'd love your help! Here's how to get started:

### Setting Up Your Development Environment

1. **Clone the repository:**
   ```bash
   git clone https://github.com/TechxArtisanStudio/Openterface_QT.git
   cd Openterface_QT
   ```

2. **Install build dependencies:** See [Build from Source](#build-from-source) section for your OS

3. **Build locally:**
   ```bash
   mkdir build && cd build
   cmake ..
   make
   ./openterfaceQT
   ```

### Contributing Code

- **Report bugs:** [Open an issue](https://github.com/TechxArtisanStudio/Openterface_QT/issues)
- **Suggest features:** [Discuss in Issues](https://github.com/TechxArtisanStudio/Openterface_QT/issues)
- **Submit changes:** [Create a Pull Request](https://github.com/TechxArtisanStudio/Openterface_QT/pulls)

For detailed contribution guidelines, see [CONTRIBUTING.md](CONTRIBUTING.md).

**We welcome contributions including:**
- 🐛 Bug fixes
- ✨ Feature implementations
- 📖 Documentation improvements
- 🌍 Translations
- 🧪 Test coverage

---

## Troubleshooting

### Keyboard and Mouse Not Responding

#### Windows
1. Ensure the CH340 serial chip driver is installed. Download from [WCH website](https://www.wch-ic.com/downloads/CH341SER_EXE.html)
2. Alternatively, install from the repository driver:
   ```cmd
   pnputil -a path\to\CH341SER.INF
   ```
   (Run as Administrator)

#### Linux
1. Download the CH340 driver from [WCH website](https://www.wch-ic.com/downloads/CH341SER_EXE.html)
2. Extract and compile:
   ```bash
   tar -xzf CH341SER_LINUX.tar.gz
   cd CH341SER
   make
   sudo make install
   ```
3. Load the driver:
   ```bash
   sudo modprobe ch341
   ```
4. Unplug and reconnect the device
5. If still not working, check permissions using the [Linux Installation - Manual](#option-3-manual-installation) section

### Other Issues

For more troubleshooting, see [docs/BUILD.md#troubleshooting](docs/BUILD.md#troubleshooting).

---

## Support & Community

Have questions or found a bug? We're here to help!

- 💬 **Discord:** [Join our community](https://discord.gg/sFTJD6a3R8) for real-time discussions
- 🐛 **GitHub Issues:** [Report bugs or request features](https://github.com/TechxArtisanStudio/Openterface_QT/issues)
- 📧 **Email:** [techxartisan@gmail.com](mailto:techxartisan@gmail.com)

---

## License

This project is licensed under **AGPL-3.0** (Affero General Public License). 

### Key Terms
- ✅ Free to use, modify, and distribute
- ✅ Source code must be made available to recipients
- ✅ Modifications must be released under AGPL-3.0
- ❌ No warranty provided

**For full license details**, see [LICENSE](LICENSE) file in this repository.

### Third-Party Libraries

This project uses excellent open-source libraries:

| Library | Version | License |
|---------|---------|---------|
| **Qt Framework** | 6.4+ | LGPL v3 |
| **libusb** | 1.0.26+ | LGPL v2.1 |
| **FFmpeg** | Latest | LGPL v2.1+ / GPL v2 |
| **FreeType** | 2.13.2+ | GPL v2 / FreeType License |
| **Fontconfig** | 2.14.2+ | MIT License |
| **PulseAudio** | 16.1+ | LGPL v2.1+ |
| **libxkbcommon** | 1.7.0+ | MIT License |

For detailed license compliance information and LGPL library linking details, see [LICENSE](LICENSE).

---

**Questions about licensing?** [Email us](mailto:techxartisan@gmail.com) or [open an issue](https://github.com/TechxArtisanStudio/Openterface_QT/issues).
