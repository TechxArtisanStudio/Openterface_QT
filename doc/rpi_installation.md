# Installation Guide for Raspberry Pi

This guide provides instructions on how to install the OpenterfaceQT App on a Raspberry Pi. There are two methods available: downloading from GitHub and installing the package, or building from source. Both methods require specific environment setups.

## Environment Requirements

Before proceeding with the installation, ensure your Raspberry Pi meets the following requirements:

- **Raspberry Pi Release Version**: Ensure you are using a compatible version of Raspberry Pi OS.
- **Qt Version**: The app requires Qt 6.4 or higher.

## Permissions Setup

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

## Method 1: Download from GitHub and Install the Package

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

## Method 2: Build from Source

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
     cd Openterface_QT
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

## Troubleshooting

- If you encounter any issues during installation, ensure all environment requirements are met and dependencies are correctly installed.
- Check the [GitHub issues page](https://github.com/your-repo/openterface/issues) for common problems and solutions.

## Conclusion

You have successfully installed the Openterface app on your Raspberry Pi. Choose the method that best suits your needs and enjoy using the app!
