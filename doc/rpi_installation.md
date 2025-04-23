# Installation Guide for Raspberry Pi

This guide provides instructions on how to install the OpenterfaceQT App on a Raspberry Pi. There are two methods available: installing via Flatpak or building from source. Both methods require specific environment setups, including device permissions setup as a prerequisite for using OpenterfaceQT.

## Prerequisite: Device Permissions Setup

The OpenterfaceQT app requires access to serial and HID devices. This setup is mandatory for both Flatpak and build-from-source installations. Follow these steps to grant the necessary permissions:

1. **Add User to Groups**:

   - Open a terminal and run the following commands:

     ```bash
     sudo usermod -aG dialout $USER
     sudo usermod -aG plugdev $USER
     ```

2. **Create udev Rule for HIDRAW**:

   - Create a new file `/etc/udev/rules.d/51-openterface.rules` with the following content:

     ```bash
     echo 'KERNEL== "hidraw*", SUBSYSTEM=="hidraw", MODE="0666"' | sudo tee /etc/udev/rules.d/51-openterface.rules
     ```

3. **Reload udev Rules**:

   - Run the following command to reload the udev rules:

     ```bash
     sudo udevadm control --reload-rules
     sudo udevadm trigger
     ```

4. **Apply Changes**:

   - Log out and log back in for the changes to take effect.

## Use Flatpak Install

### Download Flatpak

```sh
sudo apt install -y flatpak flatpak-builder qemu-user-static
sudo apt install -y gcc-aarch64-linux-gnu g++-aarch64-linux-gnu
```

### Set up Flatpak Environment

```sh
flatpak remote-add --user --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo

flatpak install --user --noninteractive flathub org.kde.Sdk/aarch64/6.7
flatpak install --user --noninteractive flathub org.kde.Platform/aarch64/6.7
```

### Download Openterface Flatpak File

Currently, only a preview version is available. We plan to publish on Flathub, but this will take some time. Download the latest version of the Flatpak file and unzip it.

```sh
# This command installs for the user and may prompt you to install some packages; proceed with the installation.
flatpak --user install com.openterface.openterfaceQT-aarch64.flatpak
# Run the OpenterfaceQT app via Flatpak
flatpak --user run com.openterface.openterfaceQT
```

***After you can run the app, enjoy using the OpenterfaceQT app.***

## Build from Source

### Environment Requirements

Before proceeding with the installation, ensure your Raspberry Pi meets the following requirements:

- **Raspberry Pi OS**: Ensure you are using a compatible version of Raspberry Pi OS (formerly Raspbian).
- **Qt Framework**: The application requires Qt version 6.4 or later.
- **FFmpeg Library**: Verify that your FFmpeg version is compatible with the Qt Multimedia backend. The recommended version is 6.1.1, which has been thoroughly tested by the project maintainers. Note that you may need to compile FFmpeg from source to meet these requirements. For more information on Qt Multimedia and its dependencies, refer to the Qt Multimedia documentation.

### Pre-Installation Setup

#### 1. Check FFmpeg Version

- First, check if you already have FFmpeg installed and its version:

  ```bash
  ffmpeg -version
  ```
- Alternatively, you can check the version of FFmpeg libraries; ensure all 6 libraries are installed and the version is greater than 6.1.1:

  ```bash
  dpkg -l | grep -E "libavutil|libavcodec|libavformat|libswscale|libswresample|libpostproc"
  ```
- If the version is 6.1.1 or higher for both the command-line tool and libraries, you can skip the next step (Build FFmpeg 6.1.1).
- If FFmpeg is not installed or the version is lower than 6.1.1, proceed to the next step.

#### 2. Install FFmpeg using Qt's Script

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

### Method 1: Download from GitHub and Install the Package

1. **Download the Package**:

   - Visit the Openterface GitHub repository.
   - Download the latest version of the package.

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
- Check the GitHub issues page for common problems and solutions.

### Conclusion

You have successfully installed the Openterface app on your Raspberry Pi. Choose the method that best suits your needs and enjoy using the app!