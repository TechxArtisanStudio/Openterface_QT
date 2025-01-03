# Welcome to Openterface Mini-KVM QT version (For Linux & Windows)

> This is a preview version of the source code and presently, it does not support all the features found in the macOS version. We are in the process of optimizing the code and refining the building methods. Your feedback is invaluable to us. If you have any suggestions or recommendations, feel free to reach out to the project team via email. Alternatively, you can join our [Discord channel](https://discord.gg/sFTJD6a3R8) for direct discussions.

# Current and future features
- [x] Basic KVM operations
- [x] Mouse control absolute mode
- [x] Mouse relative mode
- [x] Audio playing from target
- [x] Paste text to Target device
- [ ] OCR text from Target device
- [ ] Other feature request? Please join the [Discord channel](https://discord.gg/sFTJD6a3R8) and tell me

# Suppported OS
- Window (10/11) 
- Ubuntu 22.04, 24.04
- Linux Mint 21.3 (Need to upgrade QT to >=6.4)
- openSUSE Tumbleweed, built by community
- Raspberry Pi OS (64-bit), working good
- Raspberry Pi OS (32-bit), not yet complete testing

# Download & Run from Github build
1. Download the package from Github release page, and find the latest version to download according to your os and cpu architecture.
2. For Windows user, just run the installer and it will install all required drivers and application to your windows. You can run the application from start menu.
    - Note: If you are running under ARM architecture, an extra step is required to install "Microsoft Visual C++ Redistributable for Visual Studio" which can be downloaded here: [Microsoft Visual C++ Redistributable](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist?view=msvc-170#latest-microsoft-visual-c-redistributable-version)
3. For Linux user, you should install the dependency first then install the package.

 ```bash
# Setup the QT 6.4.2 runtime
sudo apt install -y libqt6core6 libqt6dbus6 libqt6gui6 libqt6network6 libqt6multimedia6 libqt6multimediawidgets6 libqt6serialport6 libqt6svg6
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
# Development
- Using QT Creator
  1. Install [QT for opensource](https://www.qt.io/download-qt-installer-oss?hsCtaTracking=99d9dd4f-5681-48d2-b096-470725510d34%7C074ddad0-fdef-4e53-8aa8-5e8a876d6ab4), recommanded version 6.4.3
  2. Use Qt Maintenance Tool to add following components
     - [QtMultiMedia](https://doc.qt.io/qt-6/qtmultimedia-index.html)
     - [QtSerialPort](https://doc.qt.io/qt-6/qtserialport-index.html)
  3. Download the source and import the project
  4. Now you can run the project

# Build from source & Run
- For Window (TODO)

- For Linux
``` bash
# Build environment preparation   
sudo apt-get update -y
sudo apt-get install -y \
    build-essential \
    qmake6 \
    qt6-base-dev \
    qt6-multimedia-dev \
    qt6-serialport-dev \
    qt6-svg-dev
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
# or 
sudo usermod -a -G dialout <your_username>
sudo reboot
# back to the build floder
./openterfaceQT

```

# Abut the Crowdfunding Hardward
Our [Openterface Mini-KVM](https://openterface.com/) crowdfunding campaign is now live on [Crowd Supply](https://www.crowdsupply.com/techxartisan/openterface-mini-kvm)! Check it out and please consider supporting us by backing our project. Cheers!

![pre-launch-poster](https://pbs.twimg.com/media/GInpcabbYAAsP9J?format=jpg&name=medium)

🚀 **Let's shake things up in KVM technology together!**

We're hard at work developing [the host applications](https://openterface.com/quick-start/#install-host-application) for this handy gadget. Our team is coding away and tweaking these tools to boost their performance and functionality. We’re all about open hardware and open-source software, and we'll keep sharing updates throughout our campaign.

Check out some early demos demonstrating the basic operation of our host application [here](https://openterface.com/basic-testing/).

# Buy Openterface Mini-KVM Now!

The Openterface Mini-KVM has been successfully funded as of June 12, 2024! You can now pre-order this innovative kit on [Crowd Supply](https://www.crowdsupply.com/techxartisan/openterface-mini-kvm).

Secure your Openterface Mini-KVM **at a wallet-friendly price** of $89 - $109 before it hits the market at higher prices. Don’t miss out – pre-order now!
