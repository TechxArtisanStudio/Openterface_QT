#!/bin/bash
# ========================================================================
# Static FFmpeg Build for Windows (MinGW-w64 standalone)
# NO QSV (libmfx disabled), CUDA enabled via ffnvcodec headers
# Uses EXTERNAL_MINGW only — no MSYS2 toolchain!
# ========================================================================

set -e

FFMPEG_VERSION="${FFMPEG_VERSION:-6.1.1}"
LIBJPEG_TURBO_VERSION="${LIBJPEG_TURBO_VERSION:-3.0.4}"
FFMPEG_INSTALL_PREFIX="${FFMPEG_INSTALL_PREFIX:-/c/ffmpeg-static}"
EXTERNAL_MINGW="${EXTERNAL_MINGW:-/c/mingw64}"
VCPKG_DIR="${VCPKG_DIR:-/c/vcpkg}"

BUILD_DIR="$(pwd)/ffmpeg-build-temp"
NUM_CORES=$(nproc || echo "2")

echo "=================================================================="
echo "FFmpeg Static Build (NO QSV, CUDA via ffnvcodec)"
echo "FFmpeg: ${FFMPEG_VERSION} | Prefix: ${FFMPEG_INSTALL_PREFIX}"
echo "External MinGW: ${EXTERNAL_MINGW}"
echo "=================================================================="

# === Use external MinGW toolchain ===
export PATH="${EXTERNAL_MINGW}/bin:${PATH}"
export CC="${EXTERNAL_MINGW}/bin/gcc"
export CXX="${EXTERNAL_MINGW}/bin/g++"
export AR="${EXTERNAL_MINGW}/bin/ar"
export LD="${EXTERNAL_MINGW}/bin/ld"
export STRIP="${EXTERNAL_MINGW}/bin/strip"
export RANLIB="${EXTERNAL_MINGW}/bin/ranlib"
export WINDRES="${EXTERNAL_MINGW}/bin/windres"

echo "Compiler:"
which gcc
gcc --version | head -n1

# === Install ffnvcodec headers (for CUDA/NVDEC) ===
FFNV_CODEC_DIR="${BUILD_DIR}/FFmpeg-nv-codec-headers"
if [ ! -d "${FFNV_CODEC_DIR}" ]; then
    git clone --depth=1 https://git.videolan.org/git/ffmpeg/nv-codec-headers.git "${FFNV_CODEC_DIR}"
    cd "${FFNV_CODEC_DIR}"
    make PREFIX="${FFMPEG_INSTALL_PREFIX}" install
    cd ..
fi

# === Build libjpeg-turbo ===
if [ ! -f "${FFMPEG_INSTALL_PREFIX}/lib/libturbojpeg.a" ]; then
    mkdir -p "${BUILD_DIR}" && cd "${BUILD_DIR}"
    wget -c "https://github.com/libjpeg-turbo/libjpeg-turbo/archive/refs/tags/${LIBJPEG_TURBO_VERSION}.tar.gz" -O libjpeg-turbo.tar.gz
    tar xzf libjpeg-turbo.tar.gz
    cd "libjpeg-turbo-${LIBJPEG_TURBO_VERSION}"
    mkdir build && cd build
    cmake .. \
        -G "Unix Makefiles" \
        -DCMAKE_INSTALL_PREFIX="${FFMPEG_INSTALL_PREFIX}" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_TOOLCHAIN_FILE="${VCPKG_DIR}/scripts/buildsystems/vcpkg.cmake" \
        -DVCPKG_TARGET_TRIPLET=x64-mingw-static \
        -DENABLE_STATIC=ON \
        -DENABLE_SHARED=OFF \
        -DWITH_JPEG8=ON \
        -DCMAKE_C_COMPILER="${CC}" \
        -DCMAKE_CXX_COMPILER="${CXX}"
    make -j${NUM_CORES}
    make install
    cd ../..
fi

# === Download FFmpeg ===
cd "${BUILD_DIR}"
FFMPEG_TARBALL="ffmpeg-${FFMPEG_VERSION}.tar.bz2"
[ -f "${FFMPEG_TARBALL}" ] || wget -c "https://ffmpeg.org/releases/${FFMPEG_TARBALL}"
[ -d "ffmpeg-${FFMPEG_VERSION}" ] || tar -xf "${FFMPEG_TARBALL}"
cd "ffmpeg-${FFMPEG_VERSION}"

# === Set pkg-config path to vcpkg + our prefix ===
export PKG_CONFIG_PATH="${FFMPEG_INSTALL_PREFIX}/lib/pkgconfig:/c/vcpkg/installed/x64-mingw-static/lib/pkgconfig"

# === Configure FFmpeg (NO libmfx!) ===
echo "⚙️ Configuring FFmpeg (cross-compile mode)..."

./configure \
    --prefix="${FFMPEG_INSTALL_PREFIX}" \
    --arch=x86_64 \
    --target-os=mingw32 \
    --enable-cross-compile \
    --cross-prefix="" \
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
    --enable-decoder=mjpeg \
    --enable-cuda --enable-cuvid --enable-nvdec --enable-ffnvcodec \     
    --disable-d3d11va \
    --pkg-config-flags="--static" \
    --extra-cflags="-I${FFMPEG_INSTALL_PREFIX}/include -I/c/vcpkg/installed/x64-mingw-static/include" \
    --extra-ldflags="-L${FFMPEG_INSTALL_PREFIX}/lib -L/c/vcpkg/installed/x64-mingw-static/lib -static -lz -lbz2 -llzma -lwinpthread" \
    || { echo "❌ Configure failed"; cat ffbuild/config.log; exit 1; }

# === Build ===
make -j${NUM_CORES}
make install

echo "✅ FFmpeg static build complete!"
ls -l "${FFMPEG_INSTALL_PREFIX}/lib/libavcodec.a"