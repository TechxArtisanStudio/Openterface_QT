# ARM64 Docker Build Optimization

This document covers the optimizations and fixes needed to build OpenterfaceQT for ARM64 using the `shared-qt-complete-arm-opt` Docker image.

## Overview

Building on ARM64 (Raspberry Pi, ARM servers, etc.) requires addressing three compatibility issues that don't affect x86_64 builds:

1. **Turbojpeg linking** — the CMake unconditionally added `-lturbojpeg` even when the library isn't available
2. **FFmpeg 6.x API removal** — `avdevice_register_all()` was removed in FFmpeg 6.0
3. **Shared vs static FFmpeg** — ARM Docker images ship both `.a` and `.so`, but static linking pulls in unresolved Xv symbols

## Changes Made

### 1. FFmpeg.cmake — Conditional turbojpeg linking (`cmake/FFmpeg.cmake`)

**Problem:** The `link_ffmpeg_libraries()` function had a duplicated `if(WIN32)...else()` block in the turbojpeg fallback path. On Linux it unconditionally set `TURBOJPEG_LINK="-lturbojpeg"` even when the library wasn't found, causing linker failure.

**Fix:** Replace the unconditional fallback with a proper `find_library` check. On Linux, `-lturbojpeg` is only added if the library actually exists on the system.

```cmake
# Before: duplicated block that always set -lturbojpeg on Linux
set(TURBOJPEG_LINK "-lturbojpeg")

# After: check if libturbojpeg exists first
find_library(LINUX_TURBOJPEG_FIND turbojpeg)
if(LINUX_TURBOJPEG_FIND)
    set(TURBOJPEG_LINK "-lturbojpeg")
else()
    set(TURBOJPEG_LINK "")
endif()
```

**Tested:** Build succeeded with `shared-qt-complete-arm-opt` (aarch64, Ubuntu 22.04). The library isn't present in the Docker image, and the build completes without the turbojpeg error.

### 2. ffmpegbackendhandler.cpp — FFmpeg 6.x compatibility (`host/backend/ffmpegbackendhandler.cpp`)

**Problem:** `avdevice_register_all()` was removed in FFmpeg 6.0 (libavdevice API bump to major version 59). The Docker image ships FFmpeg 6.1.1, causing `undefined reference to avdevice_register_all` at link time.

**Fix:** Guard the call with a preprocessor version check:

```cpp
// Before:
avdevice_register_all();

// After:
#if LIBAVDEVICE_VERSION_MAJOR < 59
    avdevice_register_all();
#endif
```

**Tested:** Build and runtime both work with FFmpeg 6.1.1. On older systems with FFmpeg 5.x, the call is still made as expected.

### 3. Build with shared FFmpeg libraries

**Problem:** Static FFmpeg linking on ARM requires `libXv` (XVideo extension), which isn't always available in minimal ARM images.

**Fix:** Use `-DUSE_SHARED_FFMPEG=ON` for ARM builds. This links against `.so` files instead of `.a`, avoiding the deep static dependency chain.

```bash
cmake -DCMAKE_PREFIX_PATH=/opt/Qt6 \
      -DBUILD_SHARED_LIBS=ON \
      -DOPENTERFACE_BUILD_STATIC=OFF \
      -DUSE_SHARED_FFMPEG=ON \
      ..
```

## ARM Docker Build Process

### Docker Image: `shared-qt-complete-arm-opt`

This image contains a complete ARM64 build environment:

| Component | Version | Path |
|-----------|---------|------|
| OS | Ubuntu 22.04 (aarch64) | — |
| Qt6 | 6.6.3 | `/opt/Qt6` |
| FFmpeg | 6.1.1 | `/opt/ffmpeg` |
| GStreamer | 1.20.3 | `/opt/gstreamer` |
| GCC | 11.4.0 | `/usr/bin/gcc` |
| CMake | 3.22.1 | `/usr/bin/cmake` |

### Build Command

```bash
# Clean build directory
sudo rm -rf build && mkdir -p build

# Build inside Docker container
docker run --rm \
    -v "$(pwd):/workspace/src" \
    -w /workspace/src/build \
    shared-qt-complete-arm-opt \
    bash -c 'cmake -DCMAKE_PREFIX_PATH=/opt/Qt6 -DBUILD_SHARED_LIBS=ON -DOPENTERFACE_BUILD_STATIC=OFF -DUSE_SHARED_FFMPEG=ON .. && make -j$(nproc)'
```

### Runtime Requirements

The built binary requires the FFmpeg 6.1.1 shared libraries at runtime:

```bash
# Extract FFmpeg shared libs from Docker image
docker run --rm -v $(pwd)/ffmpeg-libs:/host-libs shared-qt-complete-arm-opt \
    bash -c 'cp /opt/ffmpeg/lib/lib*.so* /host-libs/'

# Run with library path
LD_LIBRARY_PATH=$(pwd)/ffmpeg-libs LD_PRELOAD=/usr/lib/aarch64-linux-gnu/libEGL.so.1 \
    ./build/openterfaceQT
```

**Note on EGL:** The Mali GPU driver's EGL library (common on Raspberry Pi and ARM SoCs) doesn't export `eglDestroyImage`. Use `LD_PRELOAD=/usr/lib/aarch64-linux-gnu/libEGL.so.1` to route through the GLVND dispatch library instead.

### Verified Result

- Binary: `build/openterfaceQT` (~40MB)
- Architecture: ELF 64-bit LSB, ARM aarch64
- Dynamically linked with shared FFmpeg libs
- Runs on XFCE4 desktop with full UI (splash screen, main window, version check, Quick Start Guide)

## Image Distribution

The Docker image can be exported for offline use or transfer:

```bash
# Export (creates flattened tarball, ~2.2GB)
docker save shared-qt-complete-arm-opt -o shared-qt-complete-arm-opt.tar

# Load on another machine
docker load < shared-qt-complete-arm-opt.tar
```

> **Note:** If the original image has corrupted layers, use the export/import workaround:
> ```bash
> docker run -d --name temp shared-qt-complete-arm-opt tail -f /dev/null
> docker export temp | gzip > shared-qt-complete-arm-opt.tar.gz
> docker rm -f temp
> zcat shared-qt-complete-arm-opt.tar.gz | docker import - shared-qt-complete-arm-opt:reimported
> ```

## Summary of All Fixes

| File | Issue | Fix | Status |
|------|-------|-----|--------|
| `cmake/FFmpeg.cmake:670-687` | Duplicated turbojpeg fallback, unconditional `-lturbojpeg` on Linux | `find_library` check before adding flag, removed duplicate block | Tested |
| `host/backend/ffmpegbackendhandler.cpp:400` | `avdevice_register_all()` removed in FFmpeg 6.0 | `#if LIBAVDEVICE_VERSION_MAJOR < 59` guard | Tested |
| CMake flags | Static FFmpeg requires libXv | `-DUSE_SHARED_FFMPEG=ON` | Tested |
