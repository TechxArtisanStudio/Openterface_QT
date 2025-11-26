#!/bin/bash
# ============================================================================
# FFmpeg Static Build with QSV + CUDA Support
# Runs in MSYS2 MinGW64 environment, uses external MinGW toolchain
# ============================================================================

set -e
set -u

FFMPEG_VERSION="${FFMPEG_VERSION:-6.1.1}"
LIBJPEG_TURBO_VERSION="${LIBJPEG_TURBO_VERSION:-3.0.4}"
FFMPEG_INSTALL_PREFIX="${FFMPEG_INSTALL_PREFIX:-/c/ffmpeg-static}"
BUILD_DIR="$(pwd)/ffmpeg-build-temp"

NUM_CORES=$(nproc)

echo "============================================================================"
echo "FFmpeg Static Build (QSV + CUDA)"
echo "FFmpeg: ${FFMPEG_VERSION} | libjpeg-turbo: ${LIBJPEG_TURBO_VERSION}"
echo "Install: ${FFMPEG_INSTALL_PREFIX} | Cores: ${NUM_CORES}"
echo "============================================================================"

# ==============================
# Step 1: Install MSYS2 dependencies (MinGW64 packages)
# ==============================
echo "📦 Installing MinGW64 dependencies..."
pacman -Sy --noconfirm
pacman -S --needed --noconfirm \
    mingw-w64-x86_64-nasm \
    mingw-w64-x86_64-yasm \
    mingw-w64-x86_64-pkgconf \
    mingw-w64-x86_64-cmake \
    mingw-w64-x86_64-ffnvcodec-headers \
    mingw-w64-x86_64-libmfx \
    mingw-w64-x86_64-zlib \
    mingw-w64-x86_64-bzip2 \
    mingw-w64-x86_64-xz \
    make git wget tar bzip2 diffutils

# ==============================
# Step 2: Setup external MinGW toolchain
# ==============================
if [ -n "${EXTERNAL_MINGW:-}" ]; then
    echo "🔧 Using external MinGW: ${EXTERNAL_MINGW}"
    export PATH="${EXTERNAL_MINGW}/bin:$PATH"
    export CC="${EXTERNAL_MINGW}/bin/gcc"
    export CXX="${EXTERNAL_MINGW}/bin/g++"
    export AR="${EXTERNAL_MINGW}/bin/ar"
    export LD="${EXTERNAL_MINGW}/bin/ld"
    export STRIP="${EXTERNAL_MINGW}/bin/strip"
fi

echo "Compiler:"
which gcc
gcc --version | head -n1

# ==============================
# Step 3: Build libjpeg-turbo
# ==============================
mkdir -p "${BUILD_DIR}" && cd "${BUILD_DIR}"

if [ ! -f "${FFMPEG_INSTALL_PREFIX}/lib/libturbojpeg.a" ]; then
    echo "🏗️  Building libjpeg-turbo ${LIBJPEG_TURBO_VERSION}..."
    wget -c "https://github.com/libjpeg-turbo/libjpeg-turbo/archive/refs/tags/${LIBJPEG_TURBO_VERSION}.tar.gz" -O libjpeg-turbo.tar.gz
    tar xzf libjpeg-turbo.tar.gz
    cd "libjpeg-turbo-${LIBJPEG_TURBO_VERSION}"
    mkdir -p build && cd build
    cmake .. \
        -DCMAKE_INSTALL_PREFIX="${FFMPEG_INSTALL_PREFIX}" \
        -DCMAKE_BUILD_TYPE=Release \
        -DENABLE_STATIC=ON \
        -DENABLE_SHARED=OFF \
        -DWITH_JPEG8=ON \
        -DWITH_TURBOJPEG=ON
    make -j${NUM_CORES}
    make install
    cd "${BUILD_DIR}"
fi

# ==============================
# Step 4: Download and build FFmpeg
# ==============================
FFMPEG_TARBALL="ffmpeg-${FFMPEG_VERSION}.tar.bz2"
if [ ! -f "${FFMPEG_TARBALL}" ]; then
    wget -c "https://ffmpeg.org/releases/${FFMPEG_TARBALL}" -O "${FFMPEG_TARBALL}"
fi

if [ ! -d "ffmpeg-${FFMPEG_VERSION}" ]; then
    tar -xf "${FFMPEG_TARBALL}"
fi

cd "ffmpeg-${FFMPEG_VERSION}"

# Ensure pkg-config finds our static libs
export PKG_CONFIG_PATH="${FFMPEG_INSTALL_PREFIX}/lib/pkgconfig:${PKG_CONFIG_PATH:-}"

# ==============================
# Step 5: Configure FFmpeg
# ==============================
echo "⚙️  Configuring FFmpeg..."

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
    --disable-htmlpages --disable-manpages --disable-podpages --disable-txtpages \
    --disable-outdevs \
    --enable-avcodec --enable-avformat --enable-avutil --enable-swresample --enable-swscale \
    --enable-avdevice --enable-avfilter --enable-postproc \
    --enable-network \
    --enable-runtime-cpudetect \
    --enable-pthreads \
    --disable-w32threads \
    --enable-zlib --enable-bzlib --enable-lzma \
    --enable-libmfx \
    --enable-dxva2 --enable-d3d11va --enable-hwaccels \
    --enable-decoder=mjpeg \
    --enable-cuda --enable-cuvid --enable-nvdec --enable-ffnvcodec \
    --pkg-config-flags="--static" \
    --extra-cflags="-I${FFMPEG_INSTALL_PREFIX}/include" \
    --extra-ldflags="-L${FFMPEG_INSTALL_PREFIX}/lib -lz -lbz2 -llzma -lmfx -lwinpthread -static"

# ==============================
# Step 6: Build and install
# ==============================
echo "🔨 Building FFmpeg (this takes time)..."
make -j${NUM_CORES}
make install

# ==============================
# Step 7: Verification
# ==============================
echo "✅ Verifying installation..."
if [ -f "${FFMPEG_INSTALL_PREFIX}/lib/libavcodec.a" ] && [ -f "${FFMPEG_INSTALL_PREFIX}/lib/libturbojpeg.a" ]; then
    echo "🎉 Success! FFmpeg static libraries built with QSV + CUDA support."
    echo "📁 Libraries: ${FFMPEG_INSTALL_PREFIX}/lib"
else
    echo "❌ Verification failed!"
    exit 1
fi