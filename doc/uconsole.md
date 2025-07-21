## Troubleshooting uConsole Expansion Card: Keyboard and Mouse Not Working

If the keyboard and mouse connected to your uConsole expansion card are not functioning, please follow the steps below to troubleshoot the issue:

### 1. Check HID and Serial Port Permissions

> Make sure your user account has permission to access HID and serial devices.

Sometimes you may need to run the following commands to set the appropriate permissions:

```bash
sudo chmod 666 /dev/hidraw*
sudo chmod 666 /dev/ttyUSB*
```

### 2. Ensure the Firmware is Up to Date

> Go to **Advanced → Firmware Update** in the software menu and check if your firmware is up to date. Update it if necessary.

### 3. Factory Reset the HID Chip

> Navigate to **Advanced → Factory Reset HID Chip**. Confirm the reset when prompted.
> Wait a few seconds (typically 5–10 seconds) for the reset process to complete.

### 4. Shut Down the uConsole

> After resetting the HID chip, shut down the uConsole from the operating system.
> Wait a few seconds to ensure the KVM device fully powers off (the screen should go black).

### 5. Restart and Test

> Restart the uConsole and open the **Openterface\_QT** software.
> Check if the keyboard and mouse are now working properly.

### 6. Enable Logs and Report

> If the issue persists, go to **Advanced → Serial Console**, enable both serial and HID-related logs, and check for error messages.
> Please send the logs to us for further analysis.

----

Please follow the instructions in the guide and let us know how it goes. If you’re still running into issues, you’re very welcome to:

* **Email us directly** at [support@openterface.com](mailto:support@openterface.com)
* **Report the issue on our GitHub repo**, ideally with error logs or screenshots:
  [https://github.com/TechxArtisanStudio/Openterface\_QT](https://github.com/TechxArtisanStudio/Openterface_QT)

You’re also warmly invited to join our open-source community, where our dev team and other users hang out there regularly and are happy to help:

* Discord: [https://openterface.com/discord](https://openterface.com/discord)
* Reddit: [https://openterface.com/reddit](https://openterface.com/reddit)

### Note

Some uConsole systems may not include the CH341 driver by default, which can prevent the keyboard and mouse from functioning properly. In such cases, the driver needs to be installed manually:

```sh
  git clone https://github.com/juliagoda/CH341SER.git
  cd CH341SER
  make
  sudo make load
```