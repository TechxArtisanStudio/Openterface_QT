## Debugging KVM Device: Keyboard and Mouse Not Working
If your KVM device’s keyboard and mouse are not functioning, please follow these steps to troubleshoot:

1. Check HID and Serial Port Permissions
Ensure your user account has permission to access HID and serial devices.
Some time you need run follow command to access device permission
```bash
sudo chmod 666 /dev/hidraw*
sudo chmod 666 /dev/ttyUSB*
```

2. Factory Reset the HID Chip
In the software, go to the menu: Advance → Factory Reset HID Chip.
Confirm the reset operation, need wait few second (5-10s).
3. Shutdown the uConsole
After resetting, shut down the uConsole from the software.
Wait a few seconds to ensure the KVM device powers off (screen should go black).
4. Restart and Test
Reopen the software.
Check if the keyboard and mouse are now working with the KVM device.
If the issue persists after following these steps, please in the Advance open the dialog Serial Console and check the logs.
