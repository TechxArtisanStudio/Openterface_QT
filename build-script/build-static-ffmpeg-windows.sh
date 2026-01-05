#!/bin/bash
# ============================================================================
# FFmpeg Static Build Script - Windows (external MinGW)
# ============================================================================
# This script builds static FFmpeg and libjpeg-turbo on Windows using an external
# MinGW toolchain (e.g., C:\mingw64) and a bash shell (Git Bash). Automated
# package-managed toolchain support has been removed from the automated flow.
# ============================================================================

set -e  # Exit on error
set -u  # Exit on undefined variable

MODE="${1:-full}"

# Configuration
# Environment variables supported by this script (set before running / from caller):
#   SKIP_PACKAGE_MINGW=1          -> Skip package-managed install and prefer external MinGW (caller should set EXTERNAL_MINGW_POSIX) 
#   EXTERNAL_MINGW_POSIX=/c/mingw64 -> Path to external mingw in POSIX-style (set by caller wrapper when using EXTERNAL_MINGW)
#   ENABLE_NVENC=1             -> Attempt to enable NVENC support (requires NVENC SDK/headers)
#   NVENC_SDK_PATH=...         -> Path to NVENC SDK (optional, used when ENABLE_NVENC=1)

FFMPEG_VERSION="6.1.1"
LIBJPEG_TURBO_VERSION="3.0.4"
FFMPEG_INSTALL_PREFIX="/c/ffmpeg-static"
BUILD_DIR="$(pwd)/ffmpeg-build-temp"
DOWNLOAD_URL="https://ffmpeg.org/releases/ffmpeg-${FFMPEG_VERSION}.tar.bz2"
LIBJPEG_TURBO_URL="https://github.com/libjpeg-turbo/libjpeg-turbo/archive/refs/tags/${LIBJPEG_TURBO_VERSION}.tar.gz"

# Number of CPU cores for parallel compilation
NUM_CORES=$(nproc)

echo "============================================================================"
echo "FFmpeg Static Build - Windows (external MinGW)"
echo "============================================================================"
echo "FFmpeg Version: ${FFMPEG_VERSION}"
echo "libjpeg-turbo Version: ${LIBJPEG_TURBO_VERSION}"
echo "Install Prefix: ${FFMPEG_INSTALL_PREFIX}"
echo "Build Directory: ${BUILD_DIR}"
echo "CPU Cores: ${NUM_CORES}"
echo "============================================================================"
echo ""

# Prepare/verify required toolchain and utilities (external MinGW + bash)
# Default to SKIP_PACKAGE_MINGW=1 (use external MinGW) unless explicitly overridden
if [ "${SKIP_PACKAGE_MINGW:-1}" = "1" ]; then
    echo "Step 1/8: SKIP_PACKAGE_MINGW set (or default) - using external MinGW build environment"
    # Prefer MSYS2 mingw64 if present and user didn't set EXTERNAL_MINGW_POSIX
    if [ -z "${EXTERNAL_MINGW_POSIX:-}" ] && [ -d "/c/msys64/mingw64" ]; then
        EXTERNAL_MINGW_POSIX="/c/msys64/mingw64"
    fi
    echo "External MinGW (posix style): ${EXTERNAL_MINGW_POSIX:-/c/mingw64}"
    # Prepend external mingw to PATH so the toolchain is used
    EXTERNAL_MINGW_BIN="${EXTERNAL_MINGW_POSIX:-/c/mingw64}/bin"
    if [ -d "${EXTERNAL_MINGW_BIN}" ]; then
        export PATH="${EXTERNAL_MINGW_BIN}:$PATH"
        echo "PATH updated to prefer external MinGW: ${EXTERNAL_MINGW_BIN}"
        if [ -n "${EXTERNAL_MINGW_POSIX:-}" ] && [ "${EXTERNAL_MINGW_POSIX}" = "/c/msys64/mingw64" ]; then
            echo "Note: Using MSYS2 MinGW64 at ${EXTERNAL_MINGW_POSIX}"
        fi
    else
        echo "WARNING: External MinGW bin directory not found: ${EXTERNAL_MINGW_BIN}"
        echo "Please ensure your external MinGW (gcc, make, cmake, nasm/yasm) are on PATH."
    fi

    # Sanity checks for mandatory tools (gcc, make/cmake, nasm/yasm, tar, wget/git)
    MISSING=0
    command -v gcc >/dev/null 2>&1 || { echo "ERROR: gcc not found on PATH"; MISSING=1; }
    command -v cmake >/dev/null 2>&1 || { echo "ERROR: cmake not found on PATH"; MISSING=1; }
    command -v make >/dev/null 2>&1 || command -v mingw32-make >/dev/null 2>&1 || { echo "ERROR: make (or mingw32-make) not found on PATH"; MISSING=1; }
    command -v nasm >/dev/null 2>&1 || command -v yasm >/dev/null 2>&1 || { echo "ERROR: nasm or yasm not found on PATH"; MISSING=1; }
    command -v tar >/dev/null 2>&1 || { echo "ERROR: tar not found on PATH"; MISSING=1; }
    command -v wget >/dev/null 2>&1 || command -v curl >/dev/null 2>&1 || { echo "ERROR: wget or curl not found on PATH"; MISSING=1; }
    if [ "$MISSING" -eq 1 ]; then
        echo "One or more required tools are missing. Install the missing tools (gcc, cmake, make/mingw32-make, nasm/yasm, tar, wget/curl, git, bash) and ensure they are on PATH."
        exit 1
    fi
else
    echo "ERROR: SKIP_PACKAGE_MINGW is not set to 1. Automated package-managed installation was removed from this script."
    echo "Please run this script with SKIP_PACKAGE_MINGW=1 (the default) and provide an external MinGW toolchain (set EXTERNAL_MINGW or ensure /c/mingw64 exists)."    exit 1
fi

# Choose CMake generator for external MinGW
GENERATOR="MinGW Makefiles"

# Check for Intel Media SDK/Media Driver
echo "Checking for Intel QSV support..."
echo "Note: Intel QSV works with modern Intel graphics drivers that include Media Driver components"
echo "      The old standalone Media SDK is deprecated"
echo ""

# Check for Intel graphics driver version (rough check)
if [ -d "/c/Windows/System32/DriverStore/FileRepository" ]; then
    INTEL_DRIVER_COUNT=$(find "/c/Windows/System32/DriverStore/FileRepository" -name "*igfx*" -o -name "*intel*" | grep -i "igd\|gfx\|intel" | wc -l 2>/dev/null || echo "0")
    if [ "$INTEL_DRIVER_COUNT" -gt 0 ]; then
        echo "✓ Found Intel graphics drivers installed ($INTEL_DRIVER_COUNT driver files)"
        echo "  This should include QSV/Media Driver support"
    else
        echo "⚠ No Intel graphics drivers found"
        echo "  Install latest Intel graphics drivers from:"
        echo "  https://www.intel.com/content/www/us/en/download/19344/intel-graphics-windows-dxe.html"
    fi
else
    echo "⚠ Cannot check driver installation"
fi

# Check for libmfx library
if pkg-config --exists libmfx; then
    echo "✓ libmfx library found (QSV support available)"
    LIBMFX_VERSION=$(pkg-config --modversion libmfx 2>/dev/null || echo "unknown")
    echo "  Version: $LIBMFX_VERSION"
else
    echo "⚠ libmfx library not found in pkg-config"
    echo "  This is normal if using system drivers instead of SDK"
fi

echo ""
echo "Intel QSV Setup Instructions:"
echo "1. Ensure you have Intel integrated graphics or discrete Intel GPU"
echo "2. Install latest Intel graphics drivers:"
echo "   https://www.intel.com/content/www/us/en/download/19344/intel-graphics-windows-dxe.html"
echo "3. For older systems, you may need Intel Media Driver:"
echo "   https://www.intel.com/content/www/us/en/download-center/select-download/s/intel-media-driver-windows"
echo ""
echo "QSV will work if your Intel GPU supports it and drivers are installed."
echo ""

# Check for CUDA installation
echo "Checking for NVIDIA CUDA Toolkit..."
if [ -d "/c/Program Files/NVIDIA GPU Computing Toolkit/CUDA" ]; then
    CUDA_VERSION=$(ls "/c/Program Files/NVIDIA GPU Computing Toolkit/CUDA" | grep "^v" | sort -V | tail -n 1)
    if [ -n "$CUDA_VERSION" ]; then
        echo "✓ Found CUDA: $CUDA_VERSION"
        export CUDA_PATH="/c/Program Files/NVIDIA GPU Computing Toolkit/CUDA/$CUDA_VERSION"
        export PATH="$CUDA_PATH/bin:$PATH"
    else
        echo "⚠ CUDA Toolkit not found - GPU acceleration may not work"
        echo "  Download from: https://developer.nvidia.com/cuda-downloads"
    fi
else
    echo "⚠ CUDA Toolkit not found at standard location"
    echo "  Download from: https://developer.nvidia.com/cuda-downloads"
fi
echo ""

# Verify cross-compilation tools are available
echo "Verifying MinGW64 toolchain..."
which gcc
which nm
which ar
echo "✓ MinGW64 toolchain verified"
echo ""

# Create build directory
echo "Step 2/8: Creating build directory..."
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"
echo "✓ Build directory ready: ${BUILD_DIR}"
echo ""

# Download and build libjpeg-turbo
echo "Step 3/8: Building libjpeg-turbo ${LIBJPEG_TURBO_VERSION}..."
if [ ! -f "${FFMPEG_INSTALL_PREFIX}/lib/libturbojpeg.a" ]; then
    echo "Downloading libjpeg-turbo..."
    if [ ! -f "libjpeg-turbo.tar.gz" ]; then
        wget "${LIBJPEG_TURBO_URL}" -O "libjpeg-turbo.tar.gz"
    fi
    echo "Extracting libjpeg-turbo..."
    tar xzf libjpeg-turbo.tar.gz
    cd "libjpeg-turbo-${LIBJPEG_TURBO_VERSION}"
    mkdir -p build
    cd build
    echo "Configuring libjpeg-turbo... (generator: ${GENERATOR})"
    cmake .. -G "${GENERATOR}" -DCMAKE_INSTALL_PREFIX="${FFMPEG_INSTALL_PREFIX}" -DCMAKE_BUILD_TYPE=Release -DENABLE_STATIC=ON -DENABLE_SHARED=OFF -DWITH_JPEG8=ON -DWITH_TURBOJPEG=ON -DWITH_ZLIB=ON
    echo "Building libjpeg-turbo with ${NUM_CORES} cores..."
    if [ "${GENERATOR}" = "MinGW Makefiles" ]; then
        mingw32-make -j${NUM_CORES} || make -j${NUM_CORES}
        if [ $? -ne 0 ]; then
            echo "Build failed with mingw32-make, trying make..."
            make -j${NUM_CORES}
        fi
    else
        make -j${NUM_CORES}
    fi
    echo "Installing libjpeg-turbo..."
    if [ "${GENERATOR}" = "MinGW Makefiles" ]; then
        mingw32-make install || make install
    else
        make install
    fi
    cd "${BUILD_DIR}"
    rm -rf "libjpeg-turbo-${LIBJPEG_TURBO_VERSION}"
    echo "✓ libjpeg-turbo built and installed"
else
    echo "✓ libjpeg-turbo already installed"
fi

# Determine optional flags for QSV (libmfx) and NVENC
# By default, do NOT force libmfx/NVENC linking; they should be optional and allowed to be missing at runtime.
ENABLE_LIBMFX=""
EXTRA_CFLAGS="-I${FFMPEG_INSTALL_PREFIX}/include"
# Start with common link flags; add bz2/lzma/zstd only if present in known MinGW/MSYS2 lib dirs
# Force static linking with -Wl,-Bstatic for third-party libs
EXTRA_LDFLAGS="-L${FFMPEG_INSTALL_PREFIX}/lib -Wl,-Bstatic -lz -lzstd -Wl,-Bdynamic -lmingwex -lwinpthread -static -static-libgcc -static-libstdc++"
# Probe candidate lib dirs for libbz2/liblzma (respect EXTERNAL_MINGW_POSIX if set)
CANDIDATE_LIB_DIRS="${EXTERNAL_MINGW_POSIX:-/c/mingw64} /c/msys64/mingw64 /mingw64"
for d in $CANDIDATE_LIB_DIRS; do
    if [ -d "$d/lib" ]; then
        if ls "$d/lib"/libbz2.* >/dev/null 2>&1; then
            EXTRA_LDFLAGS="${EXTRA_LDFLAGS} -lbz2"
        fi
        if ls "$d/lib"/liblzma.* >/dev/null 2>&1; then
            EXTRA_LDFLAGS="${EXTRA_LDFLAGS} -llzma"
        fi
    fi
done
# Debug info about chosen flags (helpful when troubleshooting)
echo "Using EXTRA_LDFLAGS: ${EXTRA_LDFLAGS}"

# libmfx (QSV): Only enable if user explicitly requests it via ENABLE_LIBMFX=1 and pkg-config can find it.
if [ "${ENABLE_LIBMFX:-0}" = "1" ]; then
    if pkg-config --exists libmfx; then
        echo "libmfx found via pkg-config; enabling QSV (libmfx)"
        ENABLE_LIBMFX="--enable-libmfx"
        # Do NOT append -lmfx by default; rely on import library if present, but avoid forcing static link
    else
        echo "ERROR: ENABLE_LIBMFX=1 but libmfx not found via pkg-config. Install headers/libs or unset ENABLE_LIBMFX."
        exit 1
    fi
else
    echo "libmfx not enabled; QSV support will be attempted at runtime if available via drivers/SDK (dynamic loading by FFmpeg)."
fi

# NVENC: If requested, enable support but do NOT force linking against nvEncodeAPI - configure will detect headers if present. If SDK not present, enable will fail and you must either provide SDK or leave ENABLE_NVENC unset.
if [ "${ENABLE_NVENC:-0}" = "1" ]; then
    echo "NVENC enable requested via ENABLE_NVENC=1"
    NVENC_ARG="--enable-nvenc"
    NVENC_SDK_PATH="${NVENC_SDK_PATH:-/c/Program Files/NVIDIA Video Codec SDK}"
    if [ -d "${NVENC_SDK_PATH}" ]; then
        echo "NVENC SDK path: ${NVENC_SDK_PATH} (headers will be picked up if configure can find them)"
        EXTRA_CFLAGS="${EXTRA_CFLAGS} -I${NVENC_SDK_PATH}/include"
    else
        echo "NVENC SDK path not found: ${NVENC_SDK_PATH}. If headers/libs are not available, configure may fail."
    fi
else
    NVENC_ARG="--disable-nvenc"
fi
echo ""

# Determine CUDA/ffnvcodec availability and set configure args accordingly
# Default to disabling CUDA-related options unless both CUDA and ffnvcodec/SDK are present
CUDA_ARG="--disable-cuda"
CUVID_ARG="--disable-cuvid"
NVDEC_ARG="--disable-nvdec"
FFNV_ARG="--disable-ffnvcodec"

# Candidate lib dirs were defined earlier in CANDIDATE_LIB_DIRS
FOUND_FFNV=0
if [ -n "${CUDA_PATH:-}" ]; then
    # Check for libffnvcodec in known lib dirs or NVENC SDK headers
    for d in $CANDIDATE_LIB_DIRS; do
        if [ -d "$d/lib" ] && ls "$d/lib"/libffnvcodec.* >/dev/null 2>&1; then
            FOUND_FFNV=1
            break
        fi
    done
    # Also check NVENC SDK path if provided
    if [ "$FOUND_FFNV" -eq 0 ] && [ -n "${NVENC_SDK_PATH:-}" ]; then
        if [ -f "${NVENC_SDK_PATH}/include/nvEncodeAPI.h" ] || ls "${NVENC_SDK_PATH}/lib"/libnv* >/dev/null 2>&1; then
            FOUND_FFNV=1
        fi
    fi

    if [ "$FOUND_FFNV" -eq 1 ]; then
        echo "✓ Found ffnvcodec/NVENC headers/libs; enabling CUDA/CUVID/NVDEC/FFNV support"
        CUDA_ARG="--enable-cuda"
        CUVID_ARG="--enable-cuvid"
        NVDEC_ARG="--enable-nvdec"
        FFNV_ARG="--enable-ffnvcodec"
        EXTRA_CFLAGS="${EXTRA_CFLAGS} -I${CUDA_PATH}/include"
    else
        echo "⚠ CUDA found but ffnvcodec (NVENC SDK headers/libs) not found. Disabling CUDA/CUVID/NVDEC/FFNV to avoid configure failure."
        CUDA_ARG="--disable-cuda"
        CUVID_ARG="--disable-cuvid"
        NVDEC_ARG="--disable-nvdec"
        FFNV_ARG="--disable-ffnvcodec"
    fi
fi

# Download FFmpeg source
echo "Step 4/8: Downloading FFmpeg ${FFMPEG_VERSION}..."
if [ ! -f "ffmpeg-${FFMPEG_VERSION}.tar.bz2" ]; then
    wget "${DOWNLOAD_URL}" -O "ffmpeg-${FFMPEG_VERSION}.tar.bz2"
    echo "✓ Downloaded FFmpeg source"
else
    echo "✓ FFmpeg source already downloaded"
fi
echo ""

# Extract source
echo "Step 5/8: Extracting source code..."
if [ ! -d "ffmpeg-${FFMPEG_VERSION}" ]; then
    tar -xf "ffmpeg-${FFMPEG_VERSION}.tar.bz2"
    echo "✓ Source extracted"
else
    echo "✓ Source already extracted"
fi
cd "ffmpeg-${FFMPEG_VERSION}"
echo ""

# Configure FFmpeg
echo "Step 6/8: Configuring FFmpeg for static build..."
echo "This may take a few minutes..."
echo ""

# Set PKG_CONFIG_PATH to find libjpeg-turbo
export PKG_CONFIG_PATH="${FFMPEG_INSTALL_PREFIX}/lib/pkgconfig:${PKG_CONFIG_PATH:-}"

./configure \
    --prefix="${FFMPEG_INSTALL_PREFIX}" \
    --arch=x86_64 \
    --target-os=mingw32 \
    --disable-shared \
    --enable-static \
    --enable-gpl \
    --enable-version3 \
    --enable-nonfree \
    --disable-debug \
    --disable-programs \
    --disable-doc \
    --disable-htmlpages \
    --disable-manpages \
    --disable-podpages \
    --disable-txtpages \
    --disable-outdevs \
    --enable-avcodec \
    --enable-avformat \
    --enable-avutil \
    --enable-swresample \
    --enable-swscale \
    --enable-avdevice \
    --enable-avfilter \
    --enable-postproc \
    --enable-network \
    --enable-runtime-cpudetect \
    --enable-pthreads \
    --disable-w32threads \
    --enable-zlib \
    --enable-bzlib \
    --enable-lzma \
    ${ENABLE_LIBMFX} \
    --enable-dxva2 \
    --enable-d3d11va \
    --enable-hwaccels \
    --enable-decoder=mjpeg \
    ${CUDA_ARG} \
    ${CUVID_ARG} \
    ${NVDEC_ARG} \
    ${NVENC_ARG} \
    ${FFNV_ARG} \
    --enable-decoder=h264_cuvid \
    --enable-decoder=hevc_cuvid \
    --enable-decoder=mjpeg_cuvid \
    --enable-decoder=mpeg1_cuvid \
    --enable-decoder=mpeg2_cuvid \
    --enable-decoder=mpeg4_cuvid \
    --enable-decoder=vc1_cuvid \
    --enable-decoder=vp8_cuvid \
    --enable-decoder=vp9_cuvid \
    --enable-cross-compile \
    --pkg-config-flags="--static" \
    --extra-cflags="${EXTRA_CFLAGS}" \
    --extra-ldflags="${EXTRA_LDFLAGS}"

echo "✓ Configuration complete"
echo ""

if [ "$MODE" = "configure" ]; then

    echo "Configuration complete. Exiting for separate build."

    exit 0

fi

# Build FFmpeg
echo "Step 7/8: Building FFmpeg..."
echo "This will take 30-60 minutes depending on your CPU..."
echo "Using ${NUM_CORES} CPU cores for compilation"
echo ""

make -j${NUM_CORES}

echo "✓ Build complete"
echo ""

# Install FFmpeg
echo "Step 8/8: Installing FFmpeg to ${FFMPEG_INSTALL_PREFIX}..."
make install

echo "✓ Installation complete"
echo ""

# Verify installation
echo "============================================================================"
echo "Verifying installation..."
echo "============================================================================"

if [ -d "${FFMPEG_INSTALL_PREFIX}/include/libavcodec" ] && [ -f "${FFMPEG_INSTALL_PREFIX}/lib/libavcodec.a" ] && [ -f "${FFMPEG_INSTALL_PREFIX}/lib/libturbojpeg.a" ]; then
    echo "✓ FFmpeg and libjpeg-turbo static libraries installed successfully!"
    echo ""
    echo "Installed FFmpeg libraries:"
    ls -lh "${FFMPEG_INSTALL_PREFIX}/lib/"*.a | grep -E 'libav|libsw|libpostproc'
    echo ""
    echo "Installed libjpeg-turbo libraries:"
    ls -lh "${FFMPEG_INSTALL_PREFIX}/lib/"*jpeg*.a 2>/dev/null || true
    echo ""
    echo "Include directories:"
    ls -d "${FFMPEG_INSTALL_PREFIX}/include/"lib* 2>/dev/null || true
    echo ""
    echo "============================================================================"
    echo "Installation Summary"
    echo "============================================================================"
    echo "Install Path: ${FFMPEG_INSTALL_PREFIX}"
    echo "Libraries: ${FFMPEG_INSTALL_PREFIX}/lib"
    echo "Headers: ${FFMPEG_INSTALL_PREFIX}/include"
    echo "pkg-config: ${FFMPEG_INSTALL_PREFIX}/lib/pkgconfig"
    echo ""
    echo "Components installed:"
    echo "  ✓ FFmpeg ${FFMPEG_VERSION} (static)"
    echo "  ✓ libjpeg-turbo ${LIBJPEG_TURBO_VERSION} (static)"
    echo ""
    echo "============================================================================"
else
    echo "✗ Installation verification failed!"
    exit 1
fi

echo ""
echo "Build completed successfully!"
echo ""
echo "To use this FFmpeg in your CMake project:"
echo "  set FFMPEG_PREFIX=${FFMPEG_INSTALL_PREFIX}"
echo "  or pass -DFFMPEG_PREFIX=${FFMPEG_INSTALL_PREFIX} to cmake"

exit 0