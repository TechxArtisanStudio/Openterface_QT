# CH32 KeyMod Firmware Flashing Guide

This guide walks you through flashing firmware to the CH32V208 controller chip on your Openterface Mini-KVM KeyMod.

The CH32V208 is a WCH (WCH-IC) USB-serial/HID controller. Openterface provides a built-in **WCH ISP Flash Tool** to perform the firmware update over USB — no external programmer is needed.

---

## Table of Contents

- [When to Flash](#when-to-flash)
- [Prerequisites](#prerequisites)
- [Step 1 — Enter ISP (Bootloader) Mode](#step-1--enter-isp-bootloader-mode)
- [Step 2 — Connect the KeyMod to Your Computer](#step-2--connect-the-keymod-to-your-computer)
- [Step 3 — Open the WCH ISP Flash Tool](#step-3--open-the-wch-isp-flash-tool)
- [Step 4 — Scan and Connect to the Device](#step-4--scan-and-connect-to-the-device)
- [Step 5 — Select the Firmware File](#step-5--select-the-firmware-file)
- [Step 6 — Flash the Firmware](#step-6--flash-the-firmware)
- [Step 7 — Finish Up](#step-7--finish-up)
- [Troubleshooting](#troubleshooting)

---

## When to Flash

- You downloaded a new firmware release from the [Releases page](https://github.com/TechxArtisanStudio/Openterface_QT/releases)
- A developer asked you to flash a test/debug firmware
- Your KeyMod is in a broken state and needs a firmware recovery

---

## Prerequisites

| Item | Description |
|------|-------------|
| **Openterface Mini-KVM app** | Installed on your host computer (Windows or Linux) |
| **KeyMod board** | The module with the CH32V208 chip you want to flash |
| **USB cable** | Any USB cable that can connect the KeyMod to your computer |
| **Firmware file** | A `.hex` (Intel HEX) or `.bin` firmware file provided with the release |
| **USB permissions (Linux only)** | See [Linux USB Permissions](#linux-usb-permissions) below |

---

## Step 1 — Enter ISP (Bootloader) Mode

> **⚠️ CRITICAL: You MUST put the KeyMod into ISP mode before connecting it to the computer. Otherwise the flash tool will not detect the device.**

To enter ISP (bootloader) mode on the KeyMod:

1. **Make sure the KeyMod is NOT connected to the computer via USB.**
2. **Locate the BOOT button** on the KeyMod PCB.
3. **Press and hold the BOOT button** on the KeyMod.
4. **While holding the BOOT button, plug the USB cable into the KeyMod and your computer.**
5. **Release the BOOT button** after the KeyMod is connected.

The KeyMod should now be in ISP mode and will enumerate on your computer as a WCH ISP device (VID `0x1A86` or `0x4348`, PID `0x55E0`).

---

## Step 2 — Connect the KeyMod to Your Computer

If you followed [Step 1](#step-1--enter-isp-bootloader-mode) correctly, the KeyMod is already physically connected. You can verify it appeared:

### Windows
- Open **Device Manager** → check under **Universal Serial Bus devices** for a new WCH/CH375 device.

### Linux
Run:
```bash
lsusb | grep -i -E "4348|1a86"
```
You should see a line with ID `4348:55e0` or `1a86:55e0`.

---

## Step 3 — Open the WCH ISP Flash Tool

1. Launch the **Openterface Mini-KVM** application.
2. Open the WCH ISP Flash Tool from the application menu:
   - Go to **Menu → Advanced → WCH ISP Flash** (or the equivalent entry in your version).

The flash dialog will open with the following sections:
- **Device** — scan, select, and connect to the KeyMod
- **Chip Information** — shows detected chip details after connecting
- **Firmware** — file path and browse button
- **Flash, Verify & Reset** — the main action button
- **Log** — real-time progress and status messages

---

## Step 4 — Scan and Connect to the Device

1. Click **Scan Devices**. The tool will search for any WCH ISP devices on the USB bus.
2. If your KeyMod is in ISP mode, it should appear in the dropdown list.
   - If the tool reports **"No WCH ISP devices found"**, the KeyMod is not in ISP mode — go back to [Step 1](#step-1--enter-isp-bootloader-mode).
3. Select the device from the dropdown and click **Connect**.
4. The **Chip Information** section will populate with the chip name, bootloader version, and UID.

---

## Step 5 — Select the Firmware File

1. Click **Browse...**
2. Navigate to the firmware file you want to flash (`.hex` or `.bin`).
3. Select the file and click **Open**.

The file name will appear in the Firmware section.

---

## Step 6 — Flash the Firmware

1. Click the **Flash, Verify & Reset** button.
2. A confirmation dialog will appear showing the firmware file name. Click **Yes** to proceed.
3. Watch the progress bar and the log:
   - **Parsing firmware file...**
   - **Erasing...**
   - **Programming...**
   - **Verifying...**
   - **Resetting device...**
4. When done, a success message will appear:
   > *Firmware flashed and verified successfully. Please reconnect the device.*

> ⚠️ **Do NOT unplug the KeyMod or close the application during the flashing process.** Doing so may brick the device.

---

## Step 7 — Finish Up

1. Click **Close** on the flash tool dialog.
2. **Unplug the KeyMod from the computer.**
3. **Reconnect the KeyMod normally** (no need to hold BOOT this time) — it will now boot with the new firmware.
4. You can verify the new firmware is working by launching the Openterface app and checking the firmware version in **Firmware Manager**.

---

## Troubleshooting

### "No WCH ISP devices found"

- The KeyMod is **not in ISP mode**. Unplug it, then repeat [Step 1](#step-1--enter-isp-bootloader-mode): hold BOOT, plug in, release BOOT.
- Try a different USB cable or USB port.
- On Linux, make sure the udev rules are installed (see below).

### "Permission denied" / LIBUSB_ERROR_ACCESS (Linux only) {#linux-usb-permissions}

This is a Linux udev permission issue. Run the following commands in a terminal to add the required rules:

```bash
sudo tee /etc/udev/rules.d/51-opf-wchflash.rules <<'EOF'
SUBSYSTEM=="usb", ATTRS{idVendor}=="1a86", ATTRS{idProduct}=="55e0", TAG+="uaccess", MODE="0666"
SUBSYSTEM=="usb", ATTRS{idVendor}=="4348", ATTRS{idProduct}=="55e0", TAG+="uaccess", MODE="0666"
EOF

sudo udevadm control --reload-rules
sudo udevadm trigger
```

Then unplug and re-plug the KeyMod, and try scanning again.

### "Connect failed" error

- Make sure no other application (e.g. the main Openterface app) has the device open.
- On Windows, the CH375 driver must be installed. It is typically bundled with the Openterface installer.
- Unplug the KeyMod, re-enter ISP mode (Step 1), and try again.

### Flash fails during programming or verification

- Do not touch the USB cable during flashing.
- Try a different USB port (preferably a USB 2.0 port).
- Download the firmware file again in case it was corrupted.
- Make sure the firmware file matches your KeyMod hardware version.

### KeyMod doesn't boot after flashing

- Unplug and re-plug the KeyMod **without holding BOOT** — it should boot normally.
- If it still doesn't work, re-enter ISP mode and try flashing again.
- If the problem persists, contact support on [Discord](https://discord.gg/sFTJD6a3R8) or [open an issue](https://github.com/TechxArtisanStudio/Openterface_QT/issues).

---

## Quick Reference

| Item | Value |
|------|-------|
| **Chip** | WCH CH32V208 |
| **Normal mode VID:PID** | `1A86:FE0C` |
| **ISP mode VID:PID** | `1A86:55E0` or `4348:55E0` |
| **Firmware format** | `.hex` (Intel HEX) or `.bin` |
| **Windows transport** | CH375DLL64.DLL |
| **Linux/Mac transport** | libusb-1.0 |
