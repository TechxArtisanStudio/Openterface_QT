# Build from source — quick summary

This file collects a compact, practical set of steps to build Openterface_QT from source on a Linux system, common troubleshooting checks, and quick fixes for missing development packages.

**Overview**
- The project uses qmake (Qt) and standard GNU toolchain (`make`, `g++`).
- Optional features/backends require development packages detected by `pkg-config`: `libudev`, `gstreamer-1.0`, FFmpeg (`libav*`), `libjpeg`, `libusb-1.0`.
- Qt modules required: `multimedia`, `multimediawidgets`, `serialport`, `svg`, `svgwidgets`, etc. Use a Qt SDK or distro Qt that provides the matching development packages.

**Prerequisites (general)**
- Build tools: `gcc`/`g++`, `make`, `pkg-config` (or `pkgconf`).
- Qt development (Qt6 or Qt5) including dev packages for `multimedia`, `serialport`, `svg`.
- Development libraries: `libudev-dev` (or `systemd-devel`), `gstreamer` dev packages, FFmpeg dev packages, `libjpeg` dev, `libusb1-devel`.

Recommended quick checks before configuring:
```bash
which qmake
qmake -v
pkg-config --version
```
- If `qmake` prints Qt6/Qt5, install matching Qt dev packages or use the Qt SDK `qmake` binary (e.g. `~/Qt/6.x.x/gcc_64/bin/qmake`).

**Fedora (example - most of the packages used by the project)**
```bash
sudo dnf update
# Qt6 development packages
sudo dnf install qt6-qtbase-devel qt6-qtmultimedia-devel qt6-qtserialport-devel qt6-qtsvg-devel pkgconf-pkg-config
# System libraries
sudo dnf install libusb1-devel systemd-devel gstreamer1-devel gstreamer1-plugins-base-devel ffmpeg-devel libjpeg-turbo-devel
```
Notes for Fedora: FFmpeg packages may require RPM Fusion depending on your Fedora setup. If `ffmpeg-devel` is not found, enable RPM Fusion and retry.

**Debian / Ubuntu**
```bash
sudo apt update
sudo apt install build-essential pkg-config qt6-base-dev qt6-multimedia-dev qt6-serialport-dev qt6-svg-dev \
  libusb-1.0-0-dev libudev-dev libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev libavformat-dev libavcodec-dev libavutil-dev libswscale-dev libjpeg-dev
```
(For Qt5 replace packages with `qtbase5-dev`, `libqt5multimedia5-plugins`, `libqt5serialport5-dev`, `libqt5svg5-dev`, etc.)

**Arch / Manjaro**
```bash
sudo pacman -Syu qt6-base qt6-multimedia qt6-serialport qt6-svg pkgconf ffmpeg libusb systemd gstreamer libjpeg-turbo
```

**openSUSE**
```bash
sudo zypper install libqt6-qtbase-devel libqt6-qtmultimedia-devel libqt6-qtserialport-devel libqt6-qtsvg-devel pkg-config \
  libusb-1_0-devel systemd-devel gstreamer-devel ffmpeg-devel libjpeg8-devel
```

**Alpine**
```bash
sudo apk add build-base pkgconfig qt6-qtbase-dev qt6-qtmultimedia-dev qt6-qtserialport-dev qt6-qtsvg-dev \
  libusb-dev libsystemd-dev gstreamer-dev ffmpeg-dev libjpeg-turbo-dev
```


**Build steps (from repository root)**
1. Create a build directory and enter it (recommended):
```bash
mkdir -p build && cd build
```
2. Configure with qmake. If you have a non-system Qt install, call that `qmake` explicitly (example for Qt6):
```bash
# Using system qmake
qmake ..
# Or explicit SDK qmake
/path/to/Qt/6.x.x/gcc_64/bin/qmake ..
```
3. Build:
```bash
make -j$(nproc)
```
4. (Optional) Install to system path (if you want):
```bash
sudo make install
```

**Common problems & fixes**
- "Project ERROR: libudev development package not found" — install `libudev-dev` (Debian) or `systemd-devel` (Fedora) and ensure `pkg-config` is present.
- "Project ERROR: gstreamer-1.0 development package not found" — install `gstreamer1-devel` and `gstreamer1-plugins-base-devel` (Fedora) or `libgstreamer1.0-dev` (Debian).
- "Project ERROR: libavformat development package not found" — install `ffmpeg/ffmpeg-devel` or `libavformat-dev`.
- "Project ERROR: libjpeg development package not found" — install `libjpeg-dev` or `libjpeg-turbo-devel`.
- "Unknown module(s) in QT: multimedia multimediawidgets serialport svg ..." — this usually means the `qmake` binary in PATH is not the Qt installation that contains those modules. Check `qmake -v` and either:
  - install matching Qt dev packages for your distro, or
  - point PATH to your Qt SDK bin directory (example: `export PATH=/home/you/Qt/6.x.x/gcc_64/bin:$PATH`) and re-run `qmake ..`.
- Missing `libusb-1.0/libusb.h` — install `libusb1-devel` (Fedora) or `libusb-1.0-0-dev` (Debian).
- "undefined reference to `va_fool_postp'" — This indicates VA-API library conflicts. The custom VA-API libraries in `/usr/lib/openterfaceqt/` may be incompatible. Solution:
  ```bash
  sudo mkdir -p /usr/lib/openterfaceqt/libva_backup
  sudo mv /usr/lib/openterfaceqt/libva*.so* /usr/lib/openterfaceqt/libva_backup/
  sudo ldconfig
  ```
  Then rebuild with `qmake .. && make -j$(nproc)`.
- "fatal error: turbojpeg.h: No such file or directory" — The pkg-config for libturbojpeg may not be adding include paths correctly. Solution: Add `-lturbojpeg` to LIBS in the .pro file instead of relying on PKGCONFIG:
  ```qmake
  LIBS += -lturbojpeg
  ```
  And remove `libturbojpeg` from the PKGCONFIG line.

Verification commands useful when troubleshooting:
```bash
pkg-config --exists libudev && echo "libudev OK" || echo "libudev NOT found"
pkg-config --exists gstreamer-1.0 && echo "gstreamer OK" || echo "gstreamer NOT found"
pkg-config --exists libavformat && echo "libavformat OK" || echo "libavformat NOT found"
pkg-config --exists libusb-1.0 && echo "libusb OK" || echo "libusb NOT found"
```

**Optional: make some dependencies non-fatal**
If you must produce a build in an environment where some optional features can't be installed, you can guard the `PKGCONFIG` entries in `openterfaceQT.pro` so missing packages only disable features rather than abort configuration. Example snippet to add (replace the block where `PKGCONFIG` is defined under `unix:!macx`):

```qmake
CONFIG += link_pkgconfig

# libudev (optional)
LIBUDEV_OK = $$system(pkg-config --exists libudev && echo yes || echo no)
equals(LIBUDEV_OK,yes) {
    PKGCONFIG += libudev
    DEFINES += HAVE_LIBUDEV
} else {
    message("pkg-config: libudev not found; building without udev support")
}

# gstreamer (optional)
GST_OK = $$system(pkg-config --exists gstreamer-1.0 && echo yes || echo no)
equals(GST_OK,yes) {
    PKGCONFIG += gstreamer-1.0 gstreamer-video-1.0
    DEFINES += HAVE_GSTREAMER
} else {
    message("pkg-config: gstreamer-1.0 not found; building without GStreamer support")
}

# FFmpeg (optional)
FF_OK = $$system(pkg-config --exists libavformat && echo yes || echo no)
equals(FF_OK,yes) {
    PKGCONFIG += libavformat libavcodec libavutil libswscale libavdevice
    DEFINES += HAVE_FFMPEG
} else {
    message("pkg-config: libavformat not found; building without FFmpeg support")
}

# libjpeg (optional)
LIBJPEG_OK = $$system(pkg-config --exists libjpeg && echo yes || echo no)
equals(LIBJPEG_OK,yes) {
    PKGCONFIG += libjpeg
    DEFINES += HAVE_LIBJPEG_TURBO
} else {
    message("pkg-config: libjpeg not found; building without libjpeg support")
}
```

Trade-offs: guarding dependencies lets `qmake` succeed but disables functionality that depends on the missing libs (hotplugging, specific media backends, JPEG support, etc.). Prefer installing dev packages for a full-featured build.

**Docker / CI**
- The repository includes `docker/` and `build/` helper scripts (see `docker/docker-build-*` and `build/` scripts). Use these if you want reproducible builds or to avoid local dependency installation.

**Where to look next in this repo**
- `openterfaceQT.pro` — primary qmake project file; modules and pkg-config entries are declared here.
- `build/` and `docker/` — helper scripts for building packaged artifacts (AppImage, RPM, DEB) and cross-building.
- `doc/` — other installation docs and platform-specific notes (udev rules, flatpak, rpi, etc.).

---
**Build Status: SUCCESS ✅**

The Openterface_QT application builds successfully from source on Fedora Linux with all dependencies resolved, including the latest turbojpeg header fix. The final executable is approximately 3.9MB and requires a graphical environment to run (expected behavior in headless environments).

If you'd like, I can:
- Tailor this summary to a single distribution and add more exact package names and pre-check commands, or
- Apply the `.pro` guarding patch automatically (I can create a patch and commit it), or
- Add a short quick-start script under `build/` that runs the checks and prints missing packages.

Which next step would you prefer?