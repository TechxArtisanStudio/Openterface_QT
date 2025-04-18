# Installation Guide for Raspberry Pi

This guide provides instructions on how to install the OpenterfaceQT App on a Raspberry Pi. There are two methods available: downloading from GitHub and installing the package, or building from source. Both methods require specific environment setups.

## Use flatpak insatll
### Download flatpak
```sh
sudo apt install -y flatpak flatpak-builder qemu-user-static
sudo apt install -y gcc-aarch64-linux-gnu g++-aarch64-linux-gnu
```
### Set up flapak environment
```sh
flatpak remote-add --user --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo

flatpak install --user --noninteractive flathub org.kde.Sdk/aarch64/6.7
flatpak install --user --noninteractive flathub org.kde.Platform/aarch64/6.7
```

### Download openterface flatpak file
Now just the preview version, we're going to publish on flathub, but it need some times.
Just download the lastest version of flapak,and unzip it.
```sh 
# This command is install for user and it may reminds you to install some pkg, just insatll it.
flatpak --user insatll com.openterfae.openterfaceQT.yaml
# run the openterfaceQT by flatpak
flatpak --user run com.openterfae.openterfaceQT
```

## Build form source
### Environment Requirements

Before proceeding with the installation, ensure your Raspberry Pi meets the following requirements:

- **Raspberry Pi OS**: Ensure you are using a compatible version of Raspberry Pi OS (formerly Raspbian).
- **Qt Framework**: The application requires Qt version 6.4 or later.
- **FFmpeg Library**: Verify that your FFmpeg version is compatible with the Qt Multimedia backend. The recommended version is 6.1.1, which has been thoroughly tested by the project maintainers. Note that you may need to compile FFmpeg from source to meet these requirements. For more information on Qt Multimedia and its dependencies, refer to the [Qt Multimedia documentation](https://doc.qt.io/qt-6.5/qtmultimedia-index.html).

### Pre-Installation Setup

#### 1. Check FFmpeg Version
   - First, check if you already have FFmpeg installed and its version:
     ```bash
     ffmpeg -version
     ```
   - Alternatively, you can check the version of FFmpeg libraries, you will see all 6 libraries are installed and version is greater than 6.1.1:
     ```bash
     dpkg -l | grep -E "libavutil|libavcodec|libavformat|libswscale|libswresample|libpostproc"
     ```
   - If the version is 6.1.1 or higher for both the command-line tool and libraries, you can skip the next step (Build FFmpeg 6.1.1).
   - If FFmpeg is not installed or the version is lower than 6.1.1, proceed to the next step.
#### 2. Install FFmpeg using Qt's script
   - To ensure compatibility with Qt, we recommend using the FFmpeg installation script provided by Qt:
     ```bash
     # Download the script
     wget https://code.qt.io/cgit/qt/qt5.git/plain/coin/provisioning/common/linux/install-ffmpeg.sh?h=6.4.3 -O install-ffmpeg.sh
     
     # Make the script executable
     chmod +x install-ffmpeg.sh
     
     # Run the script (you may need to run this with sudo depending on your system configuration)
     ./install-ffmpeg.sh
     
     # Verify the installation
     ffmpeg -version
     ```
   - This script will install the version of FFmpeg that is compatible with Qt, ensuring optimal performance with OpenterfaceQT.

#### 3. Device Permissions Setup

The OpenterfaceQT app requires access to serial and HID devices. To grant the necessary permissions, follow these steps:

1. **Add User to Groups**:
   - Open a terminal and run the following commands:
     ```bash
     sudo usermod -aG dialout $USER
     sudo usermod -aG plugdev $USER
     ```

2. **Create udev Rule for HIDRAW**:
   - Create a new file `/etc/udev/rules.d/99-hidraw-permissions.rules` with the following content:
     ```bash
     sudo nano /etc/udev/rules.d/99-hidraw-permissions.rules
     ```
   - Add the following line to the file:
     ```
     KERNEL=="hidraw*", SUBSYSTEM=="hidraw", MODE="0666"
     ```

3. **Reload udev Rules**:
   - Run the following command to reload the udev rules:
     ```bash
     sudo udevadm control --reload-rules
     sudo udevadm trigger
     ```

4. **Apply Changes**:
   - Log out and log back in for the changes to take effect.

### Method 1: Download from GitHub and Install the Package

1. **Download the Package**:
   - Visit the [Openterface GitHub repository](https://github.com/TechxArtisanStudio/Openterface_QT/releases).
   - Download the latest version of package.

2. **Install the Package**:
   - Open a terminal on your Raspberry Pi.
   - Navigate to the directory where the package is downloaded.
   - Run the following command to install:
     ```bash
     sudo dpkg -i openterface_${VERSION}.deb
     ```

3. **Verify Installation**:
   - Run the app to ensure it is installed correctly:
     ```bash
     openterfaceQT
     ```

### Method 2: Build from Source

1. **Clone the Repository**:
   - Open a terminal and run:
     ```bash
     git clone https://github.com/TechxArtisanStudio/Openterface_QT.git
     ```

2. **Install Dependencies**:
   - Ensure all necessary build tools and libraries are installed:
     ```bash
     sudo apt-get update -y
     sudo apt-get install -y \
         build-essential \
         qmake6 \
         qt6-base-dev \
         qt6-multimedia-dev \
         qt6-serialport-dev \
         qt6-svg-dev
     ```

3. **Build the App**:
   - Navigate to the cloned directory and create a build directory:
     ```bash
     cd ~/Openterface_QT
     mkdir build
     cd build
     ```
   - Run the build commands:
     ```bash
     qmake6 ..
     make -j$(nproc)
     ```

4. **Run the App**:
   - After building, run the app to verify:
     ```bash
     ./openterfaceQT
     ```

### Troubleshooting

- If you encounter any issues during installation, ensure all environment requirements are met and dependencies are correctly installed.
- If FFmpeg-related errors occur, verify that the manually built FFmpeg is correctly installed and accessible.
- Check the [GitHub issues page](https://github.com/your-repo/openterface/issues) for common problems and solutions.

### Conclusion

You have successfully installed the Openterface app on your Raspberry Pi. Choose the method that best suits your needs and enjoy using the app!
