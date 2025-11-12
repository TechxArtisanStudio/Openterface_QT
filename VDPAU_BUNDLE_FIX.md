# VDPAU Library Bundling Fix

## Problem
When running the DEB test package, the application failed to start with the error:
```
/usr/local/bin/openterfaceQT: error while loading shared libraries: libvdpau.so.1: cannot open shared object file: No such file or directory
```

This was caused by the missing `libvdpau` library in the bundled DEB package, even though it was being linked during the build process for hardware video acceleration support.

## Solution
Added VDPAU library bundling to both DEB and RPM package creation processes.

## Changes Made

### 1. Modified: `build-script/docker-build-shared.sh`

#### DEB Package Section
- **Added** VDPAU library copying after VA-API libraries (libva)
  - Searches for `libvdpau.so*` in system library directories
  - Copies all VDPAU library files to the bundled package directory: `/usr/lib/openterfaceqt/`
  - Includes verification output in build logs

- **Updated** required libraries verification list
  - Added `libvdpau.so` to the `REQUIRED_LIBS` array for validation

#### RPM Package Section
- **Added** identical VDPAU library copying logic for RPM builds
  - Ensures both DEB and RPM packages include VDPAU libraries

#### Generated Control File
- **Updated** inline Debian control file generation
  - Added `libvdpau1` to the `Depends` field
  - Enhanced Description to document VDPAU inclusion
  - Notes mention both VA-API and VDPAU libraries for hardware acceleration

### 2. Modified: `packaging/debian/control`

- **Updated** package description to document bundled VDPAU library
- **Added** note about VA-API and VDPAU support for hardware video acceleration

## Technical Details

### Why VDPAU?
- VDPAU (Video Decode and Presentation API for Unix) provides hardware video decoding and presentation acceleration
- It's used by FFmpeg for GPU-assisted video decoding
- The application already links against VDPAU during build (via FFmpeg dependencies)
- However, it wasn't being bundled with the package

### How It Works
1. During DEB/RPM build, the script searches for `libvdpau.so*` in standard system library paths
2. All found VDPAU library files (including symlinks) are copied to the bundled library directory
3. The binary's RPATH is set to include `/usr/lib/openterfaceqt/` ensuring it finds bundled libraries
4. When the application runs, it can now find `libvdpau.so.1` from the bundled libraries

### Affected Architectures
The fix applies to all architectures:
- x86_64 (amd64)
- ARM64 (aarch64)
- Any other Linux architecture supported by the build

## Build Output
When building now, you should see messages like:
```
📋 DEB: Searching for VDPAU libraries...
   Checking: /usr/lib/x86_64-linux-gnu
   ✅ Found VDPAU libraries in /usr/lib/x86_64-linux-gnu
   Files found:
      -rw-r--r-- ... libvdpau.so.1.0
      lrwxrwxrwx ... libvdpau.so.1 -> libvdpau.so.1.0
   ✅ VDPAU libraries copied to /workspace/pkgroot/usr/lib/openterfaceqt
```

## Testing
To verify the fix:
1. Rebuild the DEB package: `bash docker-build-shared.sh`
2. Install the new DEB: `dpkg -i openterfaceQT_*.deb`
3. Verify VDPAU is bundled: `dpkg -L openterfaceQT | grep libvdpau`
4. Run the application: `openterfaceQT` should start without library errors

## Dependencies Restored
The bundled libraries now include:
- ✅ libvdpau (newly added)
- ✅ libva, libva-drm, libva-x11
- ✅ FFmpeg libraries (libavformat, libavcodec, libavutil, libswscale, libswresample, libavfilter, libavdevice)
- ✅ libturbojpeg
- ✅ GStreamer libraries
- ✅ Qt6 libraries
- ✅ v4l-utils libraries
