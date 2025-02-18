# Welcome to Openterface Mini-KVM QT version (For Linux & Windows)

> This is a preview version of the source code and presently, it does not support all the features found in the macOS version. We are in the process of optimizing the code and refining the building methods. Your feedback is invaluable to us. If you have any suggestions or recommendations, feel free to reach out to the project team via email. Alternatively, you can join our [Discord channel](https://discord.gg/sFTJD6a3R8) for direct discussions.

# Table of Contents
- [Welcome to Openterface Mini-KVM QT version (For Linux \& Windows)](#welcome-to-openterface-mini-kvm-qt-version-for-linux--windows)
- [Table of Contents](#table-of-contents)
  - [Features](#features)
  - [Suppported OS](#suppported-os)
  - [Download \& installing](#download--installing)
    - [For Windows users](#for-windows-users)
    - [For Linux users](#for-linux-users)
  - [Build from source](#build-from-source)
    - [For Windows](#for-windows)
    - [For Linux](#for-linux)
  - [FAQ](#faq)
  - [Asking questions and reporting issues](#asking-questions-and-reporting-issues)
  - [License](#license)

## Features
- [x] Basic KVM operations
- [x] Mouse control absolute mode
- [x] Mouse relative mode
- [x] Audio playing from target
- [x] Paste text to Target device
- [ ] OCR text from Target device
- [ ] Other feature request? Please join the [Discord channel](https://discord.gg/sFTJD6a3R8) and tell me

> For a detailed list of features, please refer to the [Features Documentation](doc/feature.md).

## Suppported OS
- Window (10/11) 
- Ubuntu 22.04 (You need to upgrade QT to >=6.4)
- Ubuntu 24.04
- Linux Mint 21.3 (Need to upgrade QT to >=6.4)
- openSUSE Tumbleweed, built by community
- Raspberry Pi OS (64-bit), working good
- Raspberry Pi OS (32-bit), Not supported, because the QT version is too old

## Download & installing
### For Windows users
1. Download the package from Github release page, and find the latest version to download according to your os and cpu architecture.
2. Run the installer and it will install all required drivers and application to your windows. You can run the application from start menu.

> Note: Users have reported that the Windows installer is unable to automate driver installation correctly on Windows 11 Version 22H2. You may need to manually download and install the driver from the WCH website. For more details, please refer to this [issue](https://github.com/TechxArtisanStudio/Openterface_QT/issues/138). We are also actively working on a solution to improve driver installation for this version.

### For Linux users

1. Download the package from Github release page, and find the latest version to download according to your os and cpu architecture.
2. Install the dependency
3. Setup dialout for Serial permissions and the hidraw permission for Switchable USB device
4. Install the package.

 ```bash
# Setup the QT 6.4.2 or laterruntime and other dependencies
sudo apt install -y libqt6core6 libqt6dbus6 libqt6gui6 libqt6network6 libqt6multimedia6 libqt6multimediawidgets6 libqt6serialport6 libqt6svg6 libusb-1.0-0-dev
 ```

```bash
# Setup the dialout permission for Serial port
sudo usermod -a -G dialout $USER
```

```bash
# Setup the hidraw permission
echo 'KERNEL== "hidraw*", SUBSYSTEM=="hidraw", MODE="0666"' | sudo tee /etc/udev/rules.d/51-openterface.rules 
sudo udevadm control --reload-rules
sudo udevadm trigger
```

 ```bash
# Unszip the package and install
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
  1. Install [QT for opensource](https://www.qt.io/download-qt-installer-oss), recommanded version 6.4.3
  2. Use Qt Maintenance Tool to add following components
     - [QtMultiMedia](https://doc.qt.io/qt-6/qtmultimedia-index.html)
     - [QtSerialPort](https://doc.qt.io/qt-6/qtserialport-index.html)
  3. Download the source and import the project
  4. Now you can run the project

### For Linux
``` bash
# Build environment preparation   
sudo apt-get update -y
sudo apt-get install -y \
    build-essential \
    qmake6 \
    qt6-base-dev \
    qt6-multimedia-dev \
    qt6-serialport-dev \
    qt6-svg-dev \
    libusb-1.0-0-dev
```

```bash
# Setup the dialout permission for Serial port
sudo usermod -a -G dialout $USER
# On some distros (e.g. Arch Linux) this might be called uucp
sudo usermod -a -G uucp $USER

# Setup the hidraw permission
echo 'KERNEL== "hidraw*", SUBSYSTEM=="hidraw", MODE="0666"' | sudo tee /etc/udev/rules.d/51-openterface.rules 
sudo udevadm control --reload-rules
sudo udevadm trigger
```

``` bash
# Get the source
git clone https://github.com/TechxArtisanStudio/Openterface_QT.git
cd Openterface_QT
mkdir build
cd build
qmake6 ..
make -j$(nproc)
```

``` bash
# Run
./openterfaceQT
```

``` bash
# If you can't control the mouse and keyboard (with high probability that did not correctly recognize the serial port)

# solution
sudo apt remove brltty
# after run this plug out the openterface and pulg in again
ls /dev/ttyUSB*
# if you can list the usb the serial port correctly recognized
# Then we need give the permissions to user for control serial port you can do this:
sudo ./openterfaceQT
# or (dialout/uucp)
sudo usermod -a -G dialout <your_username>
sudo reboot
# back to the build floder
./openterfaceQT

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
- On [Github](https://github.com/TechxArtisanStudio/Openterface_QT/issues) to report issues.
- Email to [techxartisan@gmail.com](mailto:techxartisan@gmail.com) to ask questions and report issues.

## License

This project is licensed under the AGPL-3.0 - see the [LICENSE](LICENSE) file for details.
