# 🚨 Last Chance Alert! 🚨 
Back us now on [Crowd Supply](https://www.crowdsupply.com/techxartisan/openterface-mini-kvm) to get the Openterface Mini-KVM **at a wallet-friendly price** of $79 - $99 before the campaign ends on **June 13, 2024, at 4:59 PM PDT**. Post-campaign prices are likely to increase as the product matures. Don’t miss out – act now!

# Welcome to Openterface Mini-KVM QT version (Work In Progress)

> This is a preview version of the source code and presently, it does not support all the features found in the macOS version. We are in the process of optimizing the code and refining the building methods. Your feedback is invaluable to us. If you have any suggestions or recommendations, feel free to reach out to the project team via email. Alternatively, you can join our [Discord channel](https://discord.gg/sFTJD6a3R8) for direct discussions.

# Current and future features
- Basic KVM operations, **supported**
- Mouse control absolute mode, **supported**
- Mouse relative mode, **not yet support**
- Audio playing from target, **not yet support**
- Paste text to Target device, **not yet support**
- OCR text from Target device, **not yet support**
- Other feature request? Please join the [Discord channel](https://discord.gg/sFTJD6a3R8) and tell me

# Suppported OS
- Window (10/11) 
- Ubuntu 22.04
- Linux Mint 21.3 (Need to upgrade QT to >=6.4)
- openSUSE Tumbleweed, built by community, not yet verify
- Raspberry Pi OS (64-bit), working good
- Raspberry Pi OS (32-bit), not yet complete testing

# Download & Run from Github build
1. Download the package from Github release page, and find the latest version to download according to your os and cpu architecture.
2. For Windows user, just run the installer and it will install all required drivers and application to your windows. You can run the application from start menu.
3. For Linux user, you should install the dependency first then install the package.

 ```bash
# Setup the QT 6.4.2 runtime
sudo apt install -y libqt6core6 libqt6dbus6 libqt6gui6 libqt6network6 libqt6multimedia6 libqt6multimediawidgets6 libqt6serialport6
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
  1. Install [QT for opensource](https://www.qt.io/download-qt-installer-oss?hsCtaTracking=99d9dd4f-5681-48d2-b096-470725510d34%7C074ddad0-fdef-4e53-8aa8-5e8a876d6ab4), recommanded version 6.6.3
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
    qt6-serialport-dev
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

# Abut the Crowdfunding Hardward
Our [Openterface Mini-KVM](https://openterface.com/) crowdfunding campaign is now live on [Crowd Supply](https://www.crowdsupply.com/techxartisan/openterface-mini-kvm)! Check it out and please consider supporting us by backing our project. Cheers!

![pre-launch-poster](https://pbs.twimg.com/media/GInpcabbYAAsP9J?format=jpg&name=medium)

🚀 **Let's shake things up in KVM technology together!**

We're hard at work developing [the host applications](https://openterface.com/quick-start/#install-host-application) for this handy gadget. Our team is coding away and tweaking these tools to boost their performance and functionality. We’re all about open hardware and open-source software, and we'll keep sharing updates throughout our campaign.

Check out some early demos demonstrating the basic operation of our host application [here](https://openterface.com/basic-testing/).

## 🛠️ Getting Ready for Release

We're sprucing up our code and getting our repos in shape for everyone to use. We want to make sure everything is neat and user-friendly for you. Plus, we'll open up all repos before the end of our crowdfunding campaign! Just bear with us a little longer!

## 🤝 Get Involved

[Keen to contribute?](https://openterface.com/contributing/) Fancy joining our team? Drop us an [email](mailto:info@techxartisan.com)!

Stay tuned for more cool stuff and a huge thanks for your support and enthusiasm for making the Openterface mini-KVM a reality!