# Runtime Dependencies

This document explains the runtime dependencies required by OpenterfaceQT and what each library does.

## Overview

OpenterfaceQT relies on several system libraries for graphics rendering, audio output, video capture, and hardware acceleration. These dependencies are automatically installed when using the one-liner installer or package managers.

If you're installing manually (e.g., via `dpkg -i`), you may need to install these dependencies yourself.

## Dependency Categories

### Graphics Libraries

| Library | Package Name | Purpose | Impact if Missing |
|---------|--------------|---------|-------------------|
| `libEGL.so.1` | `libegl1` | EGL (Embedded-System Graphics Library) interface for hardware-accelerated rendering | Application cannot initialize GPU context; fails to start |
| `libGLESv2.so.2` | `libgles2` | OpenGL ES 2.0 for rendering the UI and video overlays | UI rendering fails; application cannot display video |
| `libGL.so.1` | `libgl1` | OpenGL core library for desktop rendering | Fallback to software rendering or fails to start |
| `libGLX.so.0` | `libglx0` | GLX extension for X11 OpenGL integration | Cannot create OpenGL contexts on X11 displays |
| `libGLdispatch.so.0` | `libglvnd0` | OpenGL vendor-neutral dispatch library | OpenGL calls cannot be routed to the correct driver |

### Audio Libraries

| Library | Package Name | Purpose | Impact if Missing |
|---------|--------------|---------|-------------------|
| `libpulse.so.0` | `libpulse0` | PulseAudio client library for sound output | No audio output; microphone passthrough disabled |

### Video Capture Libraries

| Library | Package Name | Purpose | Impact if Missing |
|---------|--------------|---------|-------------------|
| `libv4l2.so.0` | `libv4l-0` | Video4Linux2 library for camera/capture device access | Cannot access USB video capture device; application fails to initialize |

### Display Server Libraries

| Library | Package Name | Purpose | Impact if Missing |
|---------|--------------|---------|-------------------|
| `libxkbcommon.so.0` | `libxkbcommon0` | Keyboard handling and keymap parsing | Keyboard input does not work correctly |
| `libwayland-client.so.0` | `libwayland-client0` | Wayland display server protocol support | Cannot run on Wayland sessions (falls back to X11) |
| `libxcb.so.1` | `libxcb1` | X11 C Binding (core X11 protocol) | Cannot create windows on X11 displays |
| `libxcb-shm.so.0` | `libxcb-shm0` | X11 shared memory extension for fast image transfer | Slower screen updates; performance degradation |
| `libxcb-xfixes.so.0` | `libxcb-xfixes0` | X11 region fixing extension for cursor and window management | Cursor rendering issues; window management problems |
| `libX11.so.6` | `libx11-6` | X11 core library | Cannot create windows or handle X11 events |

### Qt6 Platform Plugins

These libraries are required by Qt6's XCB platform plugin for proper window management on X11:

| Library | Package Name | Purpose | Impact if Missing |
|---------|--------------|---------|-------------------|
| `libxcb-icccm.so.4` | `libxcb-icccm4` | ICCCM (Inter-Client Communication Conventions Manual) support | Window hints and properties not set correctly |
| `libxcb-image.so.0` | `libxcb-image0` | XCB image handling for icons and pixmaps | Icons and images fail to render |
| `libxcb-keysyms.so.1` | `libxcb-keysyms1` | XCB keyboard symbol translation | Keyboard shortcuts do not work |
| `libxcb-randr.so.0` | `libxcb-randr0` | XCB screen resize and rotate extension | Cannot detect display changes; multi-monitor issues |
| `libxcb-render-util.so.0` | `libxcb-render-util0` | XCB render utilities for compositing | Rendering artifacts; transparency issues |
| `libxcb-xkb.so.1` | `libxcb-xkb1` | XCB XKB keyboard extension | Advanced keyboard layouts do not work |

### Hardware Acceleration (Optional)

These libraries enable hardware-accelerated video decoding. They are **optional**—the application will use software decoding if they are missing, but performance may be reduced.

| Library | Package Name | Purpose | Impact if Missing |
|---------|--------------|---------|-------------------|
| `libva.so.2` | `libva2` | VA-API (Video Acceleration API) core | Hardware video decoding disabled; higher CPU usage |
| `libva-drm.so.2` | `libva-drm2` | VA-API DRM (Direct Rendering Manager) support | Cannot use VA-API with DRM/KMS displays |
| `libva-x11.so.2` | `libva-x11-2` | VA-API X11 integration | Cannot use VA-API on X11 displays |
| `libvdpau.so.1` | `libvdpau1` | VDPAU (Video Decode and Presentation API for Unix) | Alternative hardware acceleration unavailable |

### Compression Libraries

| Library | Package Name | Purpose | Impact if Missing |
|---------|--------------|---------|-------------------|
| `libturbojpeg.so.0` | `libturbojpeg0` | High-performance JPEG compression for video frames | **Critical**: Application cannot process video frames; fails to start |
| `libz.so.1` | `zlib1g` | General-purpose compression library | Various compression operations fail |
| `libbz2.so.1.0` | `libbz2-1.0` | Bzip2 compression support | Cannot read bzip2-compressed data |

### Debug Libraries

| Library | Package Name | Purpose | Impact if Missing |
|---------|--------------|---------|-------------------|
| `libdw.so.1` | `libdw1` | DWARF debug information library for stack traces | Crash reports lack detailed stack traces; debugging harder |

## Installation by Distribution

### Ubuntu / Debian

```bash
# Required dependencies
sudo apt-get update
sudo apt-get install -y \
    libegl1 \
    libgles2 \
    libpulse0 \
    libv4l-0 \
    libxkbcommon0 \
    libwayland-client0 \
    libxcb1 \
    libxcb-shm0 \
    libxcb-xfixes0 \
    libx11-6 \
    libgl1 \
    libglx0 \
    libglvnd0 \
    libxcb-icccm4 \
    libxcb-image0 \
    libxcb-keysyms1 \
    libxcb-randr0 \
    libxcb-render-util0 \
    libxcb-xkb1 \
    libturbojpeg0 \
    zlib1g \
    libbz2-1.0 \
    libdw1

# Optional: Hardware acceleration
sudo apt-get install -y \
    libva2 \
    libva-drm2 \
    libva-x11-2 \
    libvdpau1
```

### Fedora / RHEL

```bash
# Required dependencies
sudo dnf install -y \
    mesa-libEGL \
    mesa-libGLES \
    pulseaudio-libs \
    libv4l \
    libxkbcommon \
    wayland-libs-client \
    libxcb \
    libX11 \
    mesa-libGL \
    mesa-libGLX \
    mesa-libGLdispatch \
    libxcb-icccm \
    libxcb-image \
    libxcb-keysyms \
    libxcb-randr \
    libxcb-render-util \
    libxcb-xkb \
    turbojpeg \
    zlib \
    bzip2-libs \
    elfutils-libelf

# Optional: Hardware acceleration
sudo dnf install -y \
    libva \
    libva-drm \
    libva-x11 \
    libvdpau
```

### openSUSE

```bash
# Required dependencies
sudo zypper install -y \
    libEGL1 \
    libGLESv2 \
    libpulse0 \
    libv4l2 \
    libxkbcommon0 \
    libwayland-client0 \
    libxcb1 \
    libxcb-shm0 \
    libxcb-xfixes0 \
    libX11-6 \
    libGL1 \
    libGLX0 \
    libGLdispatch0 \
    libxcb-icccm4 \
    libxcb-image0 \
    libxcb-keysyms1 \
    libxcb-randr0 \
    libxcb-render-util0 \
    libxcb-xkb1 \
    libturbojpeg0 \
    libz1 \
    libbz2-1 \
    libdw1

# Optional: Hardware acceleration
sudo zypper install -y \
    libva2 \
    libva-drm2 \
    libva-x11-2 \
    libvdpau1
```

### Arch Linux

```bash
# All runtime dependencies (automatically installed by pacman)
sudo pacman -S \
    qt6-base \
    qt6-declarative \
    qt6-multimedia \
    qt6-svg \
    qt6-serialport \
    qt6-wayland \
    ffmpeg \
    gstreamer \
    gst-plugins-base \
    gst-plugins-good \
    libpulse \
    libxkbcommon \
    libusb \
    v4l-utils \
    libjpeg-turbo \
    zlib \
    libglvnd \
    wayland \
    libxcb \
    libx11

# Optional: Hardware acceleration
sudo pacman -S \
    libva \
    libvdpau
```

> **Note:** The Arch Linux `.pkg.tar.zst` package declares all dependencies in its PKGBUILD, so `pacman -U` installs them automatically. No manual dependency installation needed.

## Checking Dependencies

The pre-installation script (`preinst`) automatically checks for required dependencies before installation. You can also manually verify:

```bash
# Check if a library is available
ldconfig -p | grep libturbojpeg

# Check if a package is installed (Debian/Ubuntu)
dpkg -l | grep libturbojpeg0

# Check if a package is installed (Fedora/RHEL)
rpm -q turbojpeg
```

## Troubleshooting

### Application fails to start with "library not found"

**Symptom:** Error message like `error while loading shared libraries: libXXX.so.X: cannot open shared object file`

**Solution:** Install the missing library using the commands above.

### Video capture not working

**Symptom:** Application starts but cannot detect or access the USB capture device

**Solution:**
1. Install `libv4l-0` (Video4Linux library)
2. Ensure user is in the `video` group: `sudo usermod -a -G video $USER`
3. Check device permissions: `ls -la /dev/video*`

### Poor performance / high CPU usage

**Symptom:** Video playback is choppy or CPU usage is very high

**Solution:** Install hardware acceleration libraries (`libva2`, `libva-drm2`, `libva-x11-2`, `libvdpau1`) to enable GPU-accelerated video decoding.

### Keyboard shortcuts not working

**Symptom:** Keyboard input works but shortcuts like Ctrl+C, Ctrl+V do not

**Solution:** Install `libxcb-keysyms1` for proper keyboard symbol translation.

## See Also

- [BUILD.md](BUILD.md) - Building from source (includes build-time dependencies)
- [Troubleshooting Guide](tutorial/07-troubleshooting.md) - Common issues and solutions
- [Installation Guide](rpi_installation.md) - Raspberry Pi specific instructions
