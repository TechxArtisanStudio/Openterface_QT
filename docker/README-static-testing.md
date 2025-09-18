# Openterface QT Static Docker Testing Environment

This directory contains Docker configurations for testing the statically-linked Openterface QT application.

## Files Overview

### Static Testing Environment
- `testos/Dockerfile.ubuntu-test-static` - Minimal Docker image for static builds
- `install-openterface-static.sh` - Installation script for static packages
- `run-openterface-static-docker.sh` - Launcher script for static container

### Key Differences from Dynamic Version

#### Minimal Dependencies
The static Docker image includes only essential runtime dependencies since most libraries are embedded in the static binary:
- Basic X11 libraries for GUI
- USB/HID access libraries
- Essential C/C++ runtime
- No Qt6 runtime packages (embedded in static binary)
- No FFmpeg runtime libraries (embedded if used)

#### Static Package Detection
The installation script automatically detects and downloads static packages:
- Looks for `openterfaceQT.linux.amd64.static.deb`
- Falls back to regular package if static not available
- Verifies if binary is truly static using `ldd`

## Usage

### Quick Start
```bash
# Build and run static container
cd docker
./run-openterface-static-docker.sh --build

# Run existing static container
./run-openterface-static-docker.sh

# Run with shell for debugging
./run-openterface-static-docker.sh --shell

# Run without hardware access (GUI testing only)
./run-openterface-static-docker.sh --no-hardware
```

### Manual Build
```bash
# Build the static image
docker build -f testos/Dockerfile.ubuntu-test-static -t openterface-test-static .

# Run the container
docker run -it --rm \
  --env DISPLAY=$DISPLAY \
  --volume /tmp/.X11-unix:/tmp/.X11-unix:rw \
  --device /dev/bus/usb:/dev/bus/usb \
  --privileged \
  openterface-test-static
```

## Testing Benefits

### Static Build Advantages
1. **Minimal Dependencies**: Fewer runtime library conflicts
2. **Portability**: Runs on more systems without specific library versions
3. **Isolation**: Self-contained binary reduces external dependencies
4. **Performance**: Potential performance benefits from static linking

### Container Benefits
1. **Clean Environment**: Fresh testing environment every run
2. **Reproducible**: Consistent testing conditions
3. **Isolation**: No interference from host system libraries
4. **Easy Cleanup**: Container is automatically removed after testing

## Troubleshooting

### Common Issues

#### GUI Not Working
```bash
# Check X11 setup
echo $DISPLAY
xhost +local:

# Test with shell mode
./run-openterface-static-docker.sh --shell
# Inside container:
echo $DISPLAY
ls /tmp/.X11-unix/
```

#### Hardware Not Detected
```bash
# Check hardware outside container
lsusb | grep -E "534d|1a86"
ls /dev/hidraw* /dev/ttyUSB*

# Run with shell and check inside
./run-openterface-static-docker.sh --shell
# Inside container:
ls /dev/hidraw* /dev/ttyUSB*
```

#### Static Binary Issues
```bash
# Check if binary is truly static
./run-openterface-static-docker.sh --shell
# Inside container:
ldd /usr/local/bin/openterfaceQT  # Should show "not a dynamic executable"
```

### Debug Mode
```bash
# Run container with shell for debugging
./run-openterface-static-docker.sh --shell

# Inside container, manually run components:
/usr/local/bin/start-openterface.sh
```

## Comparison with Dynamic Version

| Aspect | Static Version | Dynamic Version |
|--------|---------------|-----------------|
| Image Size | Smaller base, larger binary | Larger base, smaller binary |
| Dependencies | Minimal runtime deps | Full Qt6 + FFmpeg runtime |
| Portability | Higher | Lower |
| Updates | Full binary replacement | Library-level updates |
| Debug Info | Often stripped | Better debugging |
| Build Time | Longer | Faster |

## Environment Variables

- `DISPLAY` - X11 display (auto-detected)
- `QT_X11_NO_MITSHM` - Disable MIT-SHM for compatibility
- `QT_QPA_PLATFORM` - Force XCB platform

## Hardware Support

The static container supports the same hardware as the dynamic version:
- Openterface Mini-KVM (USB VID:PID 534d:2109)
- CH340 Serial Chip (USB VID:PID 1a86:7523)
- USB HID devices
- Audio devices (basic support)

## Development Notes

When creating static packages:
1. Ensure all dependencies are statically linked
2. Test in minimal environments like this container
3. Verify no unexpected dynamic dependencies
4. Package with appropriate naming convention (`*.static.deb`)
