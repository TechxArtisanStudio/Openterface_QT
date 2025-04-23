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
# if you are using aarch64, maybe you need to install the following packages:
sudo apt install -y gcc-aarch64-linux-gnu g++-aarch64-linux-gnu
```

### Set up Flatpak Environment

```sh
flatpak remote-add --user --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo
# if you are using aarch64, maybe you need to install the following packages:
flatpak install --user --noninteractive flathub org.kde.Sdk/aarch64/6.7
flatpak install --user --noninteractive flathub org.kde.Platform/aarch64/6.7

# if you are using x86_64, maybe you need to install the following packages:
flatpak install --user --noninteractive flathub org.kde.Sdk/x86_64/6.7
flatpak install --user --noninteractive flathub org.kde.Platform/x86_64/6.7
```

### Download Openterface Flatpak File

Currently, only a preview version is available. We plan to publish on Flathub, but this will take some time. Download the latest version of the Flatpak file and unzip it.

```sh
# This command installs for the user and may prompt you to install some packages; proceed with the installation.
flatpak --user install com.openterface.openterfaceQT-aarch64.flatpak
flatpak --user install com.openterface.openterfaceQT-x86_64.flatpak
# Run the OpenterfaceQT app via Flatpak
flatpak --user run com.openterface.openterfaceQT
```

***After you can run the app, enjoy using the OpenterfaceQT app.***