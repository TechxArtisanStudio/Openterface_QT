# Openterface QT Testing Docker Environment

This Docker setup provides a clean testing environment for the Openterface QT application. It downloads and installs the latest release package with only the necessary runtime dependencies.

## Files

- `Dockerfile.openterface-qt` - Clean testing environment with minimal runtime dependencies
- `install-openterface-shared.sh` - Installation script that downloads and sets up the latest release
- `run-openterface-docker.sh` - **Smart GUI launcher script** with automatic environment detection
- `run-vnc-root.sh` - VNC-enabled launcher for remote GUI access
- `test-x11.sh` - X11 environment diagnostic tool
- `README-testing.md` - This documentation file

## Features

- **Clean Environment**: Only runtime dependencies, no build tools
- **Automatic Downloads**: Fetches the latest release from GitHub automatically
- **Device Permissions**: Proper udev rules and permissions for Openterface hardware
- **Multi-location Support**: Finds the binary in various installation locations
- **User Safety**: Runs as non-root user with proper group memberships
- **🆕 Smart Environment Detection**: Automatically detects local vs remote (SSH) environments
- **🆕 Remote GUI Support**: Multiple options for remote GUI access (X11 forwarding, VNC)
- **🆕 XCB Backend**: Optimized for X11/XCB display systems

## Quick Start

### Easy Launch with GUI Script (Recommended)

The launcher script automatically detects your environment (local vs remote/SSH) and configures X11 appropriately.

```bash
# From the docker directory
./run-openterface-docker.sh
```

**Script Options:**
- `./run-openterface-docker.sh` - Run with full GUI and hardware support
- `./run-openterface-docker.sh --shell` - Start interactive shell for debugging
- `./run-openterface-docker.sh --no-hardware` - Run without hardware access (software testing)
- `./run-openterface-docker.sh --help` - Show help message

### For Remote/SSH Environments

If you're connected via SSH and want to see the GUI:

**Option 1: X11 Forwarding (Recommended)**
```bash
# Connect with X11 forwarding enabled
ssh -X username@hostname
# or
ssh -Y username@hostname

# Then run the script normally
./run-openterface-docker.sh
```

**Option 2: Use VNC for Remote GUI**
```bash
# Use the VNC helper scripts
./run-vnc-root.sh

# Connect with VNC client to:
# Server: YOUR_SERVER_IP:5901
# Password: openterface123
```

**Option 3: Local Display (if server has desktop)**
```bash
# Set display to server's local display
export DISPLAY=:0
./run-openterface-docker.sh
```

### Manual Docker Commands

#### Build the Docker Image

```bash
# From the docker directory
docker build -f Dockerfile.openterface-qt -t openterface-test .
```

#### Run the Container

```bash
# For GUI applications (Linux with X11)
xhost +local:
docker run -it --rm \
    --privileged \
    -v /dev:/dev \
    -v /tmp/.X11-unix:/tmp/.X11-unix:rw \
    -e DISPLAY=$DISPLAY \
    openterface-test
```

## Testing Scenarios

### 1. **Quick Installation Test** (No GUI)
```bash
# Test if the package installs and binary works
./run-openterface-docker.sh --shell

# Inside container:
openterfaceQT --help
openterfaceQT --version
```

### 2. **Hardware Testing** (with GUI)
```bash
# Connect Openterface hardware and run
./run-openterface-docker.sh

# The script will:
# - Detect your environment
# - Set up proper display
# - Enable hardware access
# - Launch the GUI application
```

### 3. **Software-only Testing** (No hardware needed)
```bash
# Test without hardware access
./run-openterface-docker.sh --no-hardware

# Good for testing GUI functionality without devices
```

### 4. **Remote Testing via VNC**
```bash
# Start VNC server with GUI
./run-vnc-root.sh

# Connect from your local machine:
# VNC Client -> YOUR_SERVER_IP:5901
# Password: openterface123
```

### 5. **Diagnostic Testing**
```bash
# Check X11 environment
./test-x11.sh

# This helps diagnose display issues
```

## What the Installation Script Does

### 1. Downloads Latest Release
- Fetches version info from GitHub API
- Downloads the appropriate `.deb` package
- Falls back to known version if API fails

### 2. Installs Dependencies
The script ensures these runtime dependencies are available:
- Qt6 runtime libraries (core, gui, widgets, multimedia, serialport, svg)
- FFmpeg libraries (libavformat, libavcodec, libavutil, libswresample, libswscale)
- USB libraries (libusb-1.0-0, libudev1)
- X11 libraries (libx11, libxrandr, libxrender)
- GStreamer plugins (base, good, bad, ugly, libav, alsa, pulseaudio)

### 3. Configures Device Access
- Sets up udev rules for Openterface hardware (VID: 534d, PID: 2109)
- Configures serial interface access (VID: 1a86, PID: 7523)
- Ensures proper permissions for USB and HID devices

### 4. Creates Launcher
- Smart device detection and permission setting
- Finds the binary in multiple possible locations
- Provides detailed status information during startup

## Package Download URLs

The script automatically detects the latest version, but packages follow this pattern:
```
https://github.com/TechxArtisanStudio/Openterface_QT/releases/download/{VERSION}/openterfaceQT.linux.amd64.shared.deb
```

Example:
```
https://github.com/TechxArtisanStudio/Openterface_QT/releases/download/v0.3.19/openterfaceQT.linux.amd64.shared.deb
```

## Supported Hardware

- **Openterface Main Device**: USB VID:534d PID:2109
- **Serial Interface**: USB VID:1a86 PID:7523 (CH340 chip)

## Docker Run Options

### For Hardware Testing
```bash
docker run -it --rm \
    --privileged \
    --device=/dev/bus/usb \
    -v /dev:/dev \
    -v /tmp/.X11-unix:/tmp/.X11-unix:rw \
    -e DISPLAY=$DISPLAY \
    openterface-test
```

### For CI/CD Testing
```bash
# Test installation only
docker run --rm openterface-test /usr/local/bin/start-openterface.sh --version
```

## Troubleshooting

### Binary Not Found
The launcher searches these locations:
- `/usr/bin/openterfaceQT`
- `/opt/openterface/bin/openterfaceQT`
- `/usr/local/bin/openterfaceQT`

### Permission Issues
- Ensure Docker runs with `--privileged` for hardware access
- Check that user is in `dialout` and `uucp` groups
- Verify udev rules are loaded

### Display Issues

**For Local Environments:**
- Set correct `DISPLAY` environment variable
- Try: `export DISPLAY=:0`
- Run: `xhost +local:` before the script

**For Remote/SSH Environments:**
- Enable X11 forwarding: `ssh -X username@hostname`
- Verify forwarding works: `echo $DISPLAY` (should show something like `localhost:10.0`)
- Test with simple GUI: `xeyes` or `xclock`

**For VNC (Remote GUI):**
- Use VNC script: `./run-vnc-root.sh`
- Connect to: `YOUR_SERVER_IP:5901`
- Password: `openterface123`

### Common Error Messages

**"qt.qpa.xcb: could not connect to display"**
- X11 not properly configured
- Try VNC approach: `./run-vnc-root.sh`

**"Permission denied" for devices**
- Add `--privileged` flag (script does this automatically)
- Check if hardware is connected

**"Docker image not found"**
- Build first: `docker build -f Dockerfile.openterface-qt -t openterface-test .`

## Environment Variables

- `DISPLAY`: X11 display for GUI applications
- `QT_X11_NO_MITSHM=1`: Prevents Qt shared memory issues in containers
- `QT_QPA_PLATFORM=xcb`: Forces Qt to use X11 backend

## Differences from Build Environment

This testing environment:
- ❌ No build tools (cmake, gcc, build-essential)
- ❌ No development headers (-dev packages)
- ❌ No source code compilation
- ✅ Only runtime libraries
- ✅ Automatic package download
- ✅ Clean installation verification
- ✅ Smaller image size
- ✅ Faster startup

Perfect for testing releases without the overhead of a full development environment.
