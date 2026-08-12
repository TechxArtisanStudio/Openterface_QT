# Tutorial 07 — Troubleshooting

Common problems and their solutions for the Openterface Mini-KVM application.

## Table of Contents

| Section | Symptom |
|---------|---------|
| [Device Not Detected](#device-not-detected) | No devices found, disconnected status |
| [Keyboard/Mouse Not Working](#keyboardmouse-not-working) | BrlTTY conflict, driver issues, serial conflicts |
| [No Video / Black Screen](#no-video--black-screen) | HDMI connection, EDID mismatch, backend issues |
| [Audio Issues](#audio-issues) | PulseAudio/PipeWire routing, capture device |
| [Firmware Update Fails](#firmware-update-fails) | WCH permissions, USB stability, recovery |
| [Performance Problems](#performance-problems) | High CPU, frame drops, hardware acceleration |
| [TCP Server Issues](#tcp-server-issues) | Port conflicts, firewall, connection refused |
| [Application Crashes](#application-crashes) | Mode switching, shutdown, ARM64 segfaults |
| [Platform-Specific Issues](#platform-specific-issues) | Qt plugin, CH340, video stuttering, missing libs |

---

## Device Not Detected

### Symptoms
- The device menu shows "No devices found"
- Status bar shows disconnected
- Application starts but cannot switch to any device

### Diagnosis

1. **Check USB enumeration:**
   ```bash
   lsusb | grep -E "534d|1a86"
   ```
   Expected output should show:
   - `534d:2109` — HDMI capture chip (MS2109/MS2109S/MS2130S)
   - `1a86:7523` or `1a86:fe0c` — serial chip (CH9329 or CH32V208)

2. **Check kernel messages:**
   ```bash
   dmesg | tail -20
   ```
   Look for USB device connection messages.

3. **Check device nodes:**
   ```bash
   ls /dev/hidraw*   # HID video chip
   ls /dev/ttyUSB*   # serial chip
   ```

### Solutions

| Problem | Fix |
|---------|-----|
| Device not in `lsusb` | Try a different USB cable/port. The device needs USB 2.0+ |
| Device appears but no nodes | Check udev rules (see [Platform Guides](06-platform-guides.md#linux-deep-dive)) |
| "Permission denied" on nodes | Add user to `dialout` and `video` groups, reload udev rules |
| Device detected then disappears | `brltty` may be claiming the serial port — see [BrlTTY conflict](#brltty-conflict) |

### Port Chain Deduplication

The `DeviceManager` uses a **port chain** abstraction to deduplicate devices across serial/HID/camera/audio subsystems. If you see multiple entries for the same physical device in the device menu, this is expected — the coordinator groups them by USB topology path.

---

## Keyboard/Mouse Not Working

### BrlTTY Conflict

**The most common cause of keyboard/mouse failure on Linux.**

The `brltty` service claims USB serial devices, including the CH9329/CH32V208 chip. Symptoms:
- Device is detected (`lsusb` shows it)
- Serial port appears briefly then disappears
- `/dev/ttyUSB*` returns "Device or resource busy"

**Fix:**
```bash
# Check if brltty is running
systemctl status brltty

# Remove brltty (if you don't need Braille support)
sudo apt remove brltty          # Debian/Ubuntu
sudo dnf remove brltty          # Fedora
sudo pacman -R brltty           # Arch

# Or blacklist the device from brltty
echo 'ATTRS{idVendor}=="1a86", ATTRS{idProduct}=="7523", ENV{BRLTTY_BRAILLE_DRIVER}=""' | sudo tee /etc/udev/rules.d/99-brltty-openterface.rules
sudo udevadm control --reload-rules
```

### CH340 Driver Issues

**Windows:** If the serial chip doesn't appear in Device Manager:
1. Download and install the CH340/CH341 driver
2. Check "Ports (COM & LPT)" — should show "USB-SERIAL CH340 (COMx)"
3. If it shows as "Unknown device," the driver is not installed correctly

**Linux:** The CH340 driver is built into the kernel (`ch341` module). Check:
```bash
lsmod | grep ch341
dmesg | grep ch341
```

### Serial Port Conflicts

Only one process can open a serial port at a time. If another application (screen, minicom, another instance of Openterface) has the port open, the application cannot connect.

**Fix:** Close other applications using the serial port:
```bash
# Find processes using the serial port
sudo lsof /dev/ttyUSB0
sudo fuser /dev/ttyUSB0
```

### Mouse Mode

The application supports two mouse modes:

- **Absolute mode:** Mouse position maps directly to target screen coordinates. Best for most use cases.
- **Relative mode:** Mouse movements are sent as relative deltas. Needed for some target OSes or VMs.

Switch mode via the toolbar button or **Preferences → Target Control → Mouse Mode**. If the mouse cursor behaves erratically on the target, try switching modes.

---

## No Video / Black Screen

### HDMI Connection

1. Verify the target computer's HDMI output is connected to the device's HDMI input
2. Verify the target computer is powered on and outputting video
3. Try a different HDMI cable

### EDID Mismatch

The device sends EDID data to the target computer to communicate supported resolutions. If the target doesn't recognize the EDID:

1. Check the current EDID in **Preferences → Video**
2. Try a different resolution
3. For custom EDID requirements, see [EDID Management](03-advanced-features.md#edid-management) in the Advanced Features guide

### Backend Selection

The video backend affects compatibility:

| Backend | Platform | Best For |
|---------|----------|----------|
| FFmpeg | All | Most reliable, hardware acceleration |
| GStreamer | Linux | Pipeline flexibility |
| Qt Multimedia | Windows | Simple setups |
| Qt | Windows | Native QMediaRecorder |

**Switch backend:** **Preferences → Video → Media Backend**. Restart the application after changing.

If one backend shows a black screen, try another:
```bash
# Force FFmpeg backend
./openterfaceQT  # default is FFmpeg

# Force GStreamer backend (Linux)
QT_MEDIA_BACKEND=gstreamer ./openterfaceQT
```

### Resolution Compatibility

The capture resolution may differ from the target's output resolution. The application scales the video to fit the window. If the target is outputting an unsupported resolution:

1. Check supported resolutions in [Supported Resolutions](../resolutions.md)
2. Change the target's output resolution to a standard VESA timing
3. Enable custom resolution parsing if available

### GStreamer Pipeline Issues

If using GStreamer backend, check pipeline construction:
```bash
GST_DEBUG=3 ./openterfaceQT 2>&1 | grep -i error
```

Try a different sink:
```bash
OPENTERFACE_GST_SINK=xvimagesink ./openterfaceQT
OPENTERFACE_GST_SINK=glimagesink ./openterfaceQT
```

---

## Audio Issues

### PulseAudio/PipeWire Routing

Target audio streams from the capture device to the host's audio subsystem. If no audio is heard:

1. **Check audio output device:** Ensure your system's audio output is set correctly
2. **Check application volume:** The application has a volume control — ensure it's not muted
3. **Check PulseAudio/PipeWire:** Verify the capture device appears as an audio source:
   ```bash
   pactl list sources short       # PulseAudio
   pw-cli list-objects Node       # PipeWire
   ```

### Capture Device Selection

The audio capture device is tied to the video capture device. If multiple capture devices are connected, ensure the correct one is selected in the device menu.

### Audio Codec Settings

In **Preferences → Audio**, check:
- Codec selection (should match backend capabilities)
- Bitrate (higher = better quality, more CPU)
- Sample rate (should match device output, typically 48000 Hz)

---

## Firmware Update Fails

### WCH Chip Permissions

Firmware updates via the WCH ISP protocol require raw USB access:

```bash
# Verify WCH ISP device is detected
lsusb | grep -E "4348|1a86"  # VID 0x4348 or 0x1A86, PID 0x55E0
```

On Linux, ensure the udev rules include the WCH ISP device. The standard 51-openterface.rules may need updating:
```
SUBSYSTEM=="usb", ATTRS{idVendor}=="4348", ATTRS{idProduct}=="55e0", TAG+="uaccess"
SUBSYSTEM=="usb", ATTRS{idVendor}=="1a86", ATTRS{idProduct}=="55e0", TAG+="uaccess"
```

### USB Stability

Firmware updates are sensitive to USB disconnection. During flashing:
- Do not unplug the USB cable
- Do not suspend the host computer
- Use a direct USB port (avoid USB hubs if possible)

### Recovery Procedures

If a firmware update fails and the device is no longer recognized:

1. **Power cycle:** Unplug the device from USB, wait 10 seconds, reconnect
2. **Re-enter ISP mode:** Some devices can be forced into ISP mode by holding a button during power-on
3. **WCH Flash tool:** Use the WCHFlash utility directly to re-flash the bootloader
4. **Contact support:** If the device is bricked, contact TechxArtisan support

### Network Firmware Client

The `FirmwareNetworkClient` downloads firmware from `assets.openterface.com`. If the update dialog shows "Network error":

1. Check internet connectivity
2. Check firewall rules — the client needs HTTPS access to `assets.openterface.com`
3. Try downloading the firmware file manually and flashing from a local file

---

## Performance Problems

### High CPU Usage

Video decoding is CPU-intensive. If the application uses excessive CPU:

1. **Enable hardware acceleration:** **Preferences → Video → Hardware Acceleration**
   - VAAPI on Intel/AMD desktop GPUs
   - V4L2-M2M on Raspberry Pi
2. **Lower resolution:** 720p uses significantly less CPU than 1080p
3. **Lower frame rate:** 15fps instead of 30fps halves decode workload
4. **Switch backend:** FFmpeg with hardware acceleration typically uses less CPU than GStreamer

### Frame Drops

Frame drops occur when the capture/decode pipeline cannot keep up:

1. Check the FPS counter in the status bar
2. If actual FPS is below the target, the pipeline is bottlenecked
3. Enable **frame dropping** in the FFmpeg frame processor to prioritize smooth playback over completeness

### Hardware Acceleration Verification

Verify hardware acceleration is actually being used:

```bash
# Check VAAPI devices
ls -la /dev/dri/renderD128

# Check V4L2 decoders
ffmpeg -hide_banner -decoders | grep v4l2

# Check VAAPI info
vainfo
```

In the application, the FFmpeg backend logs hardware decoder initialization. Check the application log at **Preferences → Log**.

---

## TCP Server Issues

### Port Conflicts

The TCP server binds to a specific port (default: see TCP server settings). If another process is using the port:

```bash
# Check what's using the port
sudo lsof -i :PORT
sudo fuser PORT/tcp
```

Change the port in the TCP server settings.

### Firewall Rules

The server accepts connections on the configured port. If connections are refused:

```bash
# Check firewall status
sudo ufw status          # Ubuntu
sudo firewall-cmd --list-all  # Fedora
```

Allow the port:
```bash
sudo ufw allow PORT/tcp
sudo firewall-cmd --add-port=PORT/tcp --permanent && sudo firewall-cmd --reload
```

### Connection Refused

If `TcpServer::startServer()` fails:
1. Check the port is not in use
2. Check the application has permission to bind to the port (ports below 1024 require root on Linux)
3. Check the log for specific error messages

### Security Considerations

The TCP server has **no authentication, encryption, or rate limiting**. Recommendations:

- Only enable the server on trusted networks
- Use a non-default port to reduce automated scanning
- Do not expose the server port to the internet
- Consider using a firewall to restrict which IPs can connect

---

## Application Crashes

### Repeated Target Control Mode Switching

Switching the target control mode repeatedly can cause the application to crash. This was a known bug fixed in commit `e82a7ef`. Ensure you are running a recent version.

### Qt Multimedia During Shutdown

The application uses `g_applicationShuttingDown` (atomic flag) to prevent Qt Multimedia operations during shutdown. If you see crashes during close, ensure:
- You are running a recent version
- The shutdown flag is properly checked before Qt Multimedia calls

### ARM64 Segfaults

On ARM64, LTO optimization causes segfaults. The CMakeLists.txt already disables LTO for arm64 builds. If you still experience crashes on ARM64:
1. Ensure LTO is disabled: check CMake output for "Skipping LTO"
2. Ensure `-Os` is skipped: check CMake output for "Skipping -Os"
3. Try a Debug build to get a backtrace

### Getting a Backtrace

```bash
# Build with Debug symbols
cmake .. -DCMAKE_BUILD_TYPE=Debug
make -j$(nproc)

# Run under gdb
gdb --args ./openterfaceQT

# Or under lldb
lldb -- ./openterfaceQT
```

When the crash occurs, use `bt` (backtrace) in the debugger to get the call stack.

---

## Platform-Specific Issues

### Linux: Qt Platform Plugin

Error: `This application failed to start because no Qt platform plugin could be initialized.`

```bash
# Find Qt plugins
find /usr -name "libqxcb.so" 2>/dev/null

# Set plugin path
export QT_PLUGIN_PATH=/usr/lib/x86_64-linux-gnu/qt6/plugins
export QT_QPA_PLATFORM_PLUGIN_PATH=$QT_PLUGIN_PATH/platforms
export QT_QPA_PLATFORM=xcb
```

### Windows: CH340 Driver

If the CH340 driver fails to install:
1. Disable Driver Signature Enforcement temporarily
2. Download the latest CH340 driver from the manufacturer
3. Install manually via Device Manager → Update Driver → Browse

### Raspberry Pi: Video Stuttering

On Pi 3 or low-memory Pi 4:
1. Lower resolution to 720p
2. Use FFmpeg backend (not GStreamer)
3. Close other applications
4. Consider overclocking (Pi 4 only, with adequate cooling)

### NixOS: Missing Libraries

If the application fails to find shared libraries on NixOS:
1. Use the Nix flake build (`nix build .#openterface-qt`)
2. The flake ensures all dependencies are available in the derivation
3. Do not mix Nix-built binaries with non-Nix libraries
