#!/bin/bash
# ============================================================================
# FFmpeg Static Build Script - MSYS2 MinGW64
# ============================================================================
# This script runs inside MSYS2 MinGW64 environment
# ============================================================================

set -e  # Exit on error
set -u  # Exit on undefined variable

# Configuration
FFMPEG_VERSION="6.1.1"
LIBJPEG_TURBO_VERSION="3.0.4"
FFMPEG_INSTALL_PREFIX="/c/ffmpeg-static"
BUILD_DIR="$(pwd)/ffmpeg-build-temp"
DOWNLOAD_URL="https://ffmpeg.org/releases/ffmpeg-${FFMPEG_VERSION}.tar.bz2"
LIBJPEG_TURBO_URL="https://github.com/libjpeg-turbo/libjpeg-turbo/archive/refs/tags/${LIBJPEG_TURBO_VERSION}.tar.gz"

# Number of CPU cores for parallel compilation
NUM_CORES=$(nproc)

echo "============================================================================"
echo "FFmpeg Static Build - MSYS2 MinGW64"
echo "============================================================================"
echo "FFmpeg Version: ${FFMPEG_VERSION}"
echo "libjpeg-turbo Version: ${LIBJPEG_TURBO_VERSION}"
echo "Install Prefix: ${FFMPEG_INSTALL_PREFIX}"
echo "Build Directory: ${BUILD_DIR}"
echo "CPU Cores: ${NUM_CORES}"
echo "============================================================================"
echo ""

# Update MSYS2 and install required packages
echo "Step 1/8: Updating MSYS2 and installing dependencies..."
echo "This may take a while on first run..."

# Update package database
pacman -Sy --noconfirm

# Install build tools and dependencies
pacman -S --needed --noconfirm \
    mingw-w64-x86_64-gcc \
    mingw-w64-x86_64-binutils \
    mingw-w64-x86_64-nasm \
    mingw-w64-x86_64-yasm \
    mingw-w64-x86_64-pkgconf \
    mingw-w64-x86_64-cmake \
    mingw-w64-x86_64-ffnvcodec-headers \
    mingw-w64-x86_64-libmfx \
    mingw-w64-x86_64-zlib \
    mingw-w64-x86_64-bzip2 \
    mingw-w64-x86_64-xz \
    make \
    diffutils \
    tar \
    bzip2 \
    wget \
    git

echo "✓ Dependencies installed"
echo ""

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
    echo "Configuring libjpeg-turbo..."
    cmake .. -G "MSYS Makefiles" -DCMAKE_INSTALL_PREFIX="${FFMPEG_INSTALL_PREFIX}" -DCMAKE_BUILD_TYPE=Release -DENABLE_STATIC=ON -DENABLE_SHARED=OFF -DWITH_JPEG8=ON -DWITH_TURBOJPEG=ON -DWITH_ZLIB=ON
    echo "Building libjpeg-turbo with ${NUM_CORES} cores..."
    make -j${NUM_CORES}
    echo "Installing libjpeg-turbo..."
    make install
    cd "${BUILD_DIR}"
    rm -rf "libjpeg-turbo-${LIBJPEG_TURBO_VERSION}"
    echo "✓ libjpeg-turbo built and installed"
else
    echo "✓ libjpeg-turbo already installed"
fi
echo ""

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
    --enable-libmfx \
    --enable-dxva2 \
    --enable-d3d11va \
    --enable-hwaccels \
    --enable-decoder=mjpeg \
    --enable-cuda \
    --enable-cuvid \
    --enable-nvdec \
    --disable-nvenc \
    --enable-ffnvcodec \
    --enable-decoder=h264_cuvid \
    --enable-decoder=hevc_cuvid \
    --enable-decoder=mjpeg_cuvid \
    --enable-decoder=mpeg1_cuvid \
    --enable-decoder=mpeg2_cuvid \
    --enable-decoder=mpeg4_cuvid \
    --enable-decoder=vc1_cuvid \
    --enable-decoder=vp8_cuvid \
    --enable-decoder=vp9_cuvid \
    --enable-decoder=vp9_cuvid \
    --pkg-config-flags="--static" \
    --extra-cflags="-I${FFMPEG_INSTALL_PREFIX}/include" \
    --extra-ldflags="-L${FFMPEG_INSTALL_PREFIX}/lib -lz -lbz2 -llzma -lmfx -lmingwex -lwinpthread -static -static-libgcc -static-libstdc++"

echo "✓ Configuration complete"
echo ""

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