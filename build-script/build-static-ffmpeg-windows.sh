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
echo "Linker and assembler tools:"
which ar || true
ar --version 2>/dev/null | head -n 1 || true
which ranlib || true
which windres || true
windres --version 2>/dev/null || true

echo "\n🔎 Running compiler diagnostics..."
cat > __ffmpeg_build_test.c <<'EOF'
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
int main(void) { puts("ffmpeg-compiler-test"); return 0; }
EOF

if ! ${CC:-gcc} __ffmpeg_build_test.c -o __ffmpeg_build_test 2> __cc_compile.err; then
    echo "⚠️  Compile step failed. Dumping first 50 lines of compile error:";
    head -n 50 __cc_compile.err || true
    echo "\nNote: If the compiler is a cross-compiler then configure will fail unless --enable-cross-compile is used. Will try to inspect further and continue to configure with --enable-cross-compile as a fallback.\n"
    USE_CROSS_COMPILE=true
else
    echo "✅  Compiler produced an executable. Verifying runtime execution..."
    file __ffmpeg_build_test || true
    if ! ./__ffmpeg_build_test > /dev/null 2>&1; then
        echo "⚠️  Execution of built test program failed — this usually means the binary can't run in this environment (possible cross-compiler, missing runtime, or wrong subsystem)."
        USE_CROSS_COMPILE=true
    else
        echo "✅  Compiler executable runs fine. Proceeding with native configure."
        USE_CROSS_COMPILE=false
    fi
fi
rm -f __ffmpeg_build_test __ffmpeg_build_test.c __cc_compile.err 2>/dev/null || true

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

# Create a safer wrapper: capture configure output + show ffbuild/config.log on failure
FF_CONFIGURE_FLAGS=(
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
)

if [ "${USE_CROSS_COMPILE:-false}" = "true" ]; then
    echo "⚠️  Falling back to cross-compile configure (no runtime test)."
    FF_CONFIGURE_FLAGS+=( --enable-cross-compile --cross-prefix=x86_64-w64-mingw32- )
fi

# Dump the full configure command into a file then run it — tee to capture output
echo "Running configure: ./configure ${FF_CONFIGURE_FLAGS[*]}"
./configure "${FF_CONFIGURE_FLAGS[@]}" 2>&1 | tee configure-output.log || {
    echo "❌ Configure failed. Showing last 200 lines of configure-output.log"
    tail -n 200 configure-output.log || true
    echo "Showing ffbuild/config.log (if present) — this file gives the precise reason configure failed"
    if [ -f ffbuild/config.log ]; then
        tail -n 200 ffbuild/config.log
    else
        echo "ffbuild/config.log not present"
    fi
    exit 1
}

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