#!/bin/bash
set -e

# Qt build script for ARM64 cross-compilation

# Set ARM64 cross-compilation environment variables
export ARCH=arm64
export CROSS_COMPILE=aarch64-linux-gnu-
export PKG_CONFIG_PATH=/usr/lib/aarch64-linux-gnu/pkgconfig
export PKG_CONFIG_LIBDIR=/usr/lib/aarch64-linux-gnu

QT_VERSION=6.6.3
QT_MAJOR_VERSION=6.6
INSTALL_PREFIX=/opt/output/Qt6-arm64
BUILD_DIR=/opt/build/qt-build
FFMPEG_PREFIX="$BUILD_DIR/ffmpeg-install"
# Only include necessary modules
MODULES=("qtbase" "qtshadertools" "qtdeclarative" "qtmultimedia" "qtsvg" "qtserialport")
DOWNLOAD_BASE_URL="https://download.qt.io/archive/qt/$QT_MAJOR_VERSION/$QT_VERSION/submodules"

# Create build directories
mkdir -p "$BUILD_DIR"
mkdir -p /opt/output

# Build static FFmpeg with size optimization
echo "Building static FFmpeg for ARM64..."
FFMPEG_VERSION="6.1.1"
cd "$BUILD_DIR"
if [ ! -d "ffmpeg-$FFMPEG_VERSION" ]; then
    curl -L -o ffmpeg.tar.bz2 "https://ffmpeg.org/releases/ffmpeg-$FFMPEG_VERSION.tar.bz2"
    tar xjf ffmpeg.tar.bz2
    rm ffmpeg.tar.bz2
fi

cd "ffmpeg-$FFMPEG_VERSION"
./configure \
    --prefix="$FFMPEG_PREFIX" \
    --enable-static \
    --disable-shared \
    --disable-doc \
    --disable-programs \
    --disable-outdevs \
    --enable-pic \
    --enable-libpulse \
    --disable-debug \
    --disable-everything \
    --enable-small \
    --disable-iconv \
    --disable-network \
    --optflags="-Os -ffunction-sections -fdata-sections" \
    --enable-decoder=aac,mp3,pcm_s16le,h264,hevc \
    --enable-demuxer=aac,mp3,mov,matroska \
    --enable-protocol=file \
    --enable-parser=aac,h264,hevc \
    --cross-prefix=aarch64-linux-gnu- \
    --target-os=linux \
    --arch=arm64 \
    --cpu=generic \
    --enable-cross-compile

make -j$(nproc)
make install
# Strip binary files
find "$FFMPEG_PREFIX" -type f -name "*.a" -exec aarch64-linux-gnu-strip --strip-unneeded {} \;

# Download and extract modules
cd "$BUILD_DIR"
for module in "${MODULES[@]}"; do
    if [ ! -d "$module" ]; then
        curl -L -o "$module.zip" "$DOWNLOAD_BASE_URL/$module-everywhere-src-$QT_VERSION.zip"
        unzip -q "$module.zip"
        mv "$module-everywhere-src-$QT_VERSION" "$module"
        rm "$module.zip"
    fi
done

# Define common CMake flags with size optimization
CMAKE_COMMON_FLAGS="-Wno-dev -DCMAKE_POLICY_DEFAULT_CMP0177=NEW -DCMAKE_POLICY_DEFAULT_CMP0174=NEW \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_FLAGS=\"-Os\" \
    -DCMAKE_C_FLAGS=\"-Os\" \
    -DCMAKE_EXE_LINKER_FLAGS=\"-Wl,--gc-sections\" \
    -DCMAKE_SHARED_LINKER_FLAGS=\"-Wl,--gc-sections\" \
    -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON \
    -DCMAKE_SYSTEM_NAME=Linux \
    -DCMAKE_SYSTEM_PROCESSOR=aarch64 \
    -DCMAKE_C_COMPILER=aarch64-linux-gnu-gcc \
    -DCMAKE_CXX_COMPILER=aarch64-linux-gnu-g++ \
    -DQT_FEATURE_reduce_relocations=OFF"

# First build qtbase
echo "Building qtbase..."
cd "$BUILD_DIR/qtbase"
mkdir -p build
cd build

cmake -GNinja \
    $CMAKE_COMMON_FLAGS \
    -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX" \
    -DBUILD_SHARED_LIBS=OFF \
    -DFEATURE_dbus=ON \
    -DFEATURE_sql=OFF \
    -DFEATURE_testlib=OFF \
    -DFEATURE_icu=OFF \
    -DFEATURE_opengl=ON \
    -DFEATURE_xlib=ON \
    -DFEATURE_xcb_xlib=ON \
    -DFEATURE_xkbcommon=ON \
    -DFEATURE_xkbcommon_x11=ON \
    -DTEST_xcb_syslibs=ON \
    -DFEATURE_optimize_size=ON \
    -DFEATURE_developer_build=OFF \
    -DFEATURE_accessibility=OFF \
    -DFEATURE_printsupport=OFF \
    -DFEATURE_concurrent=ON \
    -DFEATURE_system_zlib=ON \
    -DFEATURE_network=ON \
    -DFEATURE_imageformat_bmp=OFF \
    -DFEATURE_imageformat_ppm=OFF \
    -DFEATURE_imageformat_xbm=OFF \
    -DFEATURE_widgets=ON \
    -DFEATURE_sql_sqlite=OFF \
    -DFEATURE_sql_mysql=OFF \
    -DFEATURE_sql_psql=OFF \
    -DFEATURE_vulkan=ON \
    -DFEATURE_gif=OFF \
    -DCMAKE_FIND_ROOT_PATH=/usr/aarch64-linux-gnu/ \
    -DCMAKE_SYSROOT=/usr/aarch64-linux-gnu/ \
    ..

ninja
ninja install
# Strip binary files
find "$INSTALL_PREFIX" -type f -executable -exec aarch64-linux-gnu-strip --strip-unneeded {} \; 2>/dev/null || true

# Build qtshadertools
echo "Building qtshadertools..."
cd "$BUILD_DIR/qtshadertools"
mkdir -p build
cd build
cmake -GNinja \
    $CMAKE_COMMON_FLAGS \
    -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX" \
    -DBUILD_SHARED_LIBS=OFF \
    -DCMAKE_EXE_LINKER_FLAGS="-lfontconfig -lfreetype" \
    -DFEATURE_optimize_size=ON \
    -DCMAKE_FIND_ROOT_PATH=/usr/aarch64-linux-gnu/ \
    -DCMAKE_SYSROOT=/usr/aarch64-linux-gnu/ \
    ..

ninja
ninja install

# Build qtdeclarative (Qt Quick) before qtmultimedia
echo "Building qtdeclarative..."
cd "$BUILD_DIR/qtdeclarative"
mkdir -p build
cd build
cmake -GNinja \
    $CMAKE_COMMON_FLAGS \
    -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX" \
    -DCMAKE_PREFIX_PATH="$INSTALL_PREFIX" \
    -DBUILD_SHARED_LIBS=OFF \
    -DFEATURE_optimize_size=ON \
    -DFEATURE_qml_debug=OFF \
    -DFEATURE_quick_designer=OFF \
    -DCMAKE_FIND_ROOT_PATH=/usr/aarch64-linux-gnu/ \
    -DCMAKE_SYSROOT=/usr/aarch64-linux-gnu/ \
    ..

ninja
ninja install

# Build other modules
for module in "${MODULES[@]}"; do
    if [[ "$module" != "qtbase" && "$module" != "qtshadertools" && "$module" != "qtdeclarative" ]]; then
        cd "$BUILD_DIR/$module"
        mkdir -p build
        cd build
        echo "Building $module..."

        # Add specific flags for qtmultimedia to enable FFmpeg and PulseAudio but disable GStreamer
        if [[ "$module" == "qtmultimedia" ]]; then
            PKG_CONFIG_PATH="$FFMPEG_PREFIX/lib/pkgconfig:$PKG_CONFIG_PATH" \
            cmake -GNinja \
                $CMAKE_COMMON_FLAGS \
                -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX" \
                -DCMAKE_PREFIX_PATH="$INSTALL_PREFIX;$FFMPEG_PREFIX" \
                -DBUILD_SHARED_LIBS=OFF \
                -DQT_FEATURE_gstreamer=OFF \
                -DQT_FEATURE_pulseaudio=ON \
                -DQT_FEATURE_ffmpeg=ON \
                -DFFMPEG_avcodec_LIBRARY="$FFMPEG_PREFIX/lib/libavcodec.a" \
                -DFFMPEG_avformat_LIBRARY="$FFMPEG_PREFIX/lib/libavformat.a" \
                -DFFMPEG_avutil_LIBRARY="$FFMPEG_PREFIX/lib/libavutil.a" \
                -DFFMPEG_swresample_LIBRARY="$FFMPEG_PREFIX/lib/libswresample.a" \
                -DFFMPEG_swscale_LIBRARY="$FFMPEG_PREFIX/lib/libswscale.a" \
                -DFFMPEG_INCLUDE_DIR="$FFMPEG_PREFIX/include" \
                -DCMAKE_EXE_LINKER_FLAGS="-L$FFMPEG_PREFIX/lib -lpulse" \
                -DCMAKE_FIND_ROOT_PATH=/usr/aarch64-linux-gnu/ \
                -DCMAKE_SYSROOT=/usr/aarch64-linux-gnu/ \
                ..
        else
            cmake -GNinja \
                $CMAKE_COMMON_FLAGS \
                -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX" \
                -DCMAKE_PREFIX_PATH="$INSTALL_PREFIX" \
                -DBUILD_SHARED_LIBS=OFF \
                -DFEATURE_optimize_size=ON \
                -DCMAKE_FIND_ROOT_PATH=/usr/aarch64-linux-gnu/ \
                -DCMAKE_SYSROOT=/usr/aarch64-linux-gnu/ \
                ..
        fi
        
        ninja
        ninja install
        # Strip binary files after each module installation
        find "$INSTALL_PREFIX" -type f -executable -exec aarch64-linux-gnu-strip --strip-unneeded {} \; 2>/dev/null || true
    fi
done

echo "OpenTerface ARM64 QT $QT_VERSION has been successfully built and installed to $INSTALL_PREFIX"
echo "Size optimization enabled - final build should be significantly smaller"
echo "Build artifacts are located at: /opt/output/"

# Set correct permissions for output files so host user can access them
chown -R 1000:1000 /opt/output || true
