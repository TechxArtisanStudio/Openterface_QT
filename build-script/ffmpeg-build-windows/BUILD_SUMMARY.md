# FFmpeg Static Build Scripts - Summary

## 📦 Created Files

I've created a complete set of scripts to build static FFmpeg libraries for your Windows project using an external MinGW toolchain (e.g., C:\mingw64) and a bash shell (Git Bash). Automated package-managed toolchain support has been removed; the scripts now prefer an external MinGW on PATH.

### Main Build Scripts
1. **`build-script/build-static-ffmpeg-windows.bat`**
   - Main Windows batch script
   - Invokes a POSIX `bash` (e.g., Git Bash) and uses an external MinGW; does **not** require a packaged toolchain
   - Use this from Windows Command Prompt

2. **`build-script/build-static-ffmpeg-windows.sh`**
- Bash build script that runs under a POSIX shell (Git Bash)
- Can be run under Git Bash
   - Invoked by the `.bat` wrapper or use manually

### Documentation
3. **`build-script/ffmpeg-build-windows/README.md`**
   - Complete documentation
   - Troubleshooting guide
   - Advanced usage examples

4. **`build-script/ffmpeg-build-windows/QUICKSTART.md`**
   - Quick reference card
   - Common commands
   - Quick troubleshooting

### Utilities
5. **`build-script/verify-ffmpeg-installation.bat`**
   - Verification script
   - Checks if FFmpeg is properly installed
   - Provides next steps

## 🚀 How to Use

### Step 1: Run the Build
Open Command Prompt and run:
```cmd
cd E:\Openterface_QT\build-script
set EXTERNAL_MINGW=C:\mingw64
build-static-ffmpeg-windows.bat
```

Notes:
- To enable NVENC at build time (requires NVIDIA Video Codec SDK headers on the build host):
  - set ENABLE_NVENC=1 and optionally NVENC_SDK_PATH before running the batch.
- To enable Intel QSV (libmfx) at build time: set ENABLE_LIBMFX=1 and ensure pkg-config can find libmfx.
- By default, QSV and NVENC support is optional and will be attempted at runtime if the system provides the necessary drivers/SDKs.

### Step 2: Wait (30-60 minutes)
The script will:
- ✅ Verify external MinGW and `bash` are available on PATH (gc, cmake, make/mingw32-make, nasm/yasm, tar, wget/curl, git)
- ✅ Download FFmpeg 7.0.2 source
- ✅ Configure for static linking
- ✅ Compile with all CPU cores
- ✅ Install to `C:\ffmpeg-static`

### Step 3: Verify Installation
```cmd
verify-ffmpeg-installation.bat
```

### Step 4: Use in Your Project
```cmd
set FFMPEG_PREFIX=C:\ffmpeg-static
cmake -B build -S . -DFFMPEG_PREFIX=C:\ffmpeg-static
cmake --build build --config Release
```

## 📋 What Gets Built

The build creates fully static FFmpeg libraries with:

### Libraries
- `libavcodec.a` - Audio/video codec library
- `libavformat.a` - Muxing/demuxing library
- `libavutil.a` - Utility library
- `libswresample.a` - Audio resampling library
- `libswscale.a` - Video scaling library
- `libavdevice.a` - Device handling library
- `libavfilter.a` - Filtering library
- `libpostproc.a` - Post-processing library

### Features Enabled
- ✅ Static linking (no DLL dependencies)
- ✅ GPL and version3 licenses
- ✅ Hardware acceleration (DXVA2, D3D11VA for Windows)
- ✅ Multi-threading support
- ✅ Network protocols
- ✅ Runtime CPU detection
- ✅ All core codecs

### Features Disabled
- ❌ Shared libraries
- ❌ FFmpeg command-line tools (not needed for library usage)
- ❌ Documentation

## 🎯 Integration with Your Project

Your project's `cmake/FFmpeg.cmake` is already configured to use the `FFMPEG_PREFIX` variable. After building FFmpeg, it will automatically detect:

1. Include directories: `C:\ffmpeg-static\include`
2. Library directory: `C:\ffmpeg-static\lib`
3. Static libraries: All `.a` files

The CMake configuration will link them in the correct order for static linking.

## ⚙️ Configuration Options

You can customize the build by editing variables in the scripts:

### In `build-static-ffmpeg-windows.bat`:
```batch
set FFMPEG_VERSION=7.0.2
set FFMPEG_INSTALL_PREFIX=C:\ffmpeg-static
set EXTERNAL_MINGW=C:\mingw64
```

### As Environment Variables (before running):
```cmd
set FFMPEG_VERSION=6.1.1
set FFMPEG_INSTALL_PREFIX=D:\my-ffmpeg
build-static-ffmpeg-windows.bat
```

## 🔍 Technical Details

### Build Configuration
The FFmpeg configure command includes:
```bash
./configure \
    --prefix=/c/ffmpeg-static \
    --arch=x86_64 \
    --target-os=mingw32 \
    --disable-shared \
    --enable-static \
    --enable-gpl \
    --enable-version3 \
    --enable-dxva2 \
    --enable-d3d11va \
    --enable-runtime-cpudetect \
    --pkg-config-flags="--static" \
    --extra-ldflags="-static -static-libgcc -static-libstdc++"
```

### Why Static Linking?
- ✅ No DLL hell - everything embedded in your executable
- ✅ Easier distribution - single executable
- ✅ Version control - locked to specific FFmpeg version
- ✅ Better compatibility - works across different Windows versions

## 📊 Disk Space Requirements

- **During build**: ~5 GB (source + build files)
- **After build**: ~500 MB (installed libraries)
- **Can delete**: `ffmpeg-build-temp` directory (~3-4 GB) after successful build

## 🐛 Troubleshooting

### Build fails immediately
- **Cause**: Required build toolchain or `bash` not found on PATH (e.g., missing `gcc`, `cmake`, `make`, or `git`)
- **Solution**: Install Git for Windows (for `bash`) and a MinGW distribution (example: place MinGW static toolchain in `C:\mingw64`), and ensure `C:\mingw64\bin` and `C:\Program Files\Git\usr\bin` are on PATH. Verify `gcc --version`, `cmake --version`, `make`/`mingw32-make`, and `nasm` or `yasm`.

### Build fails during package installation
- **Cause**: Network / vcpkg or download failure
- **Solution**: Ensure internet connectivity and retry the vcpkg install (see logs from the `vcpkg` step). If necessary, run `"%VCPKG_DIR%\vcpkg.exe" install --triplet=x64-mingw-static` locally to reproduce the issue.

### Build fails during compilation
- **Cause**: Out of memory or disk space
- **Solution**: 
  - Reduce `NUM_CORES` in the script
  - Free up disk space (~5GB needed)

### CMake can't find FFmpeg
- **Cause**: `FFMPEG_PREFIX` not set
- **Solution**: `set FFMPEG_PREFIX=C:\ffmpeg-static`

## 📚 Additional Resources

- **FFmpeg Documentation**: https://ffmpeg.org/documentation.html
- **Git for Windows (Git Bash)**: https://gitforwindows.org/
- **Your Project FFmpeg Config**: `E:\Openterface_QT\cmake\FFmpeg.cmake`

## 🎉 Success Indicators

After successful build, you should see:
1. Message: "✓ FFmpeg static libraries installed successfully!"
2. Directory `C:\ffmpeg-static\lib` contains *.a files
3. Directory `C:\ffmpeg-static\include` contains libav* subdirectories
4. `verify-ffmpeg-installation.bat` passes all checks

## 🔄 Next Steps After Build

1. **Verify Installation**:
   ```cmd
   verify-ffmpeg-installation.bat
   ```

2. **Set Environment Variable**:
   ```cmd
   set FFMPEG_PREFIX=C:\ffmpeg-static
   ```

3. **Build Your Project**:
   ```cmd
   cd E:\Openterface_QT
   cmake -B build -S . -DFFMPEG_PREFIX=C:\ffmpeg-static
   cmake --build build --config Release
   ```

4. **Optional - Cleanup**:
   ```cmd
   rmdir /s /q ffmpeg-build-temp

### 🐳 Docker cross-build (Linux host)

If you prefer to cross-compile Windows static FFmpeg from a Linux machine or CI runner using Docker, there is a helper Dockerfile and script:

1. Dockerfile: `docker/Dockerfile.ffmpeg-windows-static` — builds a cross-compiling image using mingw-w64 and installs a static FFmpeg tree at `/opt/ffmpeg-win-static`.
2. Helper script: `build-script/build-static-ffmpeg-docker.sh` — builds the image and extracts the resulting `ffmpeg-win-static` tree into `build/ffmpeg-win-static` on the host.

Example:

```bash
# Build image and extract artifacts (from repo root)
./build-script/build-static-ffmpeg-docker.sh

# Result is in build/ffmpeg-win-static
ls -la build/ffmpeg-win-static
```

Note: cross-building complex features (NVENC, CUDA, QSV) usually cannot be fully enabled in a plain Linux-to-Windows cross compile and may require platform-specific SDKs/drivers.

### 🏷️ GitHub Container Registry integration

The CI Docker build workflow will now check GitHub Container Registry (GHCR) for an existing ffmpeg image using the tag `ghcr.io/<owner>/openterface-ffmpeg-win-static:<version>` before building. If the image already exists the workflows will pull and extract artifacts from the image instead of rebuilding. When a build is performed the workflow pushes the resulting image back to GHCR so future runs can reuse it.

Local helper scripts do not automatically push to GHCR by default — CI build steps handle pushing. If you want the helper script to push locally, set up a login (docker login ghcr.io) and push the image after building by tagging to the GHCR name.
   ```

## 📝 License Note

The build uses `--enable-gpl`, making the resulting libraries GPL-licensed. Ensure your project complies with GPL requirements when using this build.

---

**Ready to build?** Just run:
```cmd
cd E:\Openterface_QT\build-script
build-static-ffmpeg-windows.bat
```

Then grab a coffee! ☕ The build takes 30-60 minutes.
