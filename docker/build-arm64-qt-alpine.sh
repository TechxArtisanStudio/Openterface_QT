#!/bin/sh
set -e

# Qt build script for ARM64 cross-compilation (Alpine version)

# Set environment variables
export ARCH=aarch64
export CROSS_COMPILE=aarch64-alpine-linux-musl-
export PKG_CONFIG_PATH=/usr/aarch64-alpine-linux-musl/lib/pkgconfig
export CC=${CROSS_COMPILE}gcc
export CXX=${CROSS_COMPILE}g++
export LD=${CROSS_COMPILE}ld
export AR=${CROSS_COMPILE}ar
export STRIP=${CROSS_COMPILE}strip

# Configure variables
QT_VERSION=${QT_VERSION:-6.6.3}
QT_MAJOR_VERSION=$(echo $QT_VERSION | cut -d. -f1-2)
INSTALL_PREFIX=/opt/output/Qt6-arm64
BUILD_DIR=/opt/build/qt-build
FFMPEG_PREFIX="$BUILD_DIR/ffmpeg-install"
# Only include necessary modules
MODULES=("qtbase" "qtshadertools" "qtdeclarative" "qtmultimedia" "qtsvg" "qtserialport")
DOWNLOAD_BASE_URL="https://download.qt.io/archive/qt/$QT_MAJOR_VERSION/$QT_VERSION/submodules"

# Create build directories
mkdir -p "$BUILD_DIR"
mkdir -p "$INSTALL_PREFIX"

# Build FFmpeg
echo "Building static FFmpeg for ARM64..."
FFMPEG_VERSION="6.1.1"
cd "$BUILD_DIR"
if [ ! -d "ffmpeg-$FFMPEG_VERSION" ]; then
    curl -L -o ffmpeg.tar.bz2 "https://ffmpeg.org/releases/ffmpeg-$FFMPEG_VERSION.tar.bz2"
    tar xjf ffmpeg.tar.bz2
    rm ffmpeg.tar.bz2
fi

# Build static FFmpeg
cd "ffmpeg-$FFMPEG_VERSION"
./configure \
    --prefix="$FFMPEG_PREFIX" \
    --enable-static \
    --disable-shared \
    --disable-doc \
    --disable-programs \
    --disable-outdevs \
    --enable-pic \
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
    --cross-prefix=$CROSS_COMPILE \
    --target-os=linux \
    --arch=aarch64 \
    --cpu=generic \
    --extra-cflags="-Os -ffunction-sections -fdata-sections" \
    --extra-ldflags="-Wl,--gc-sections" \
    --pkg-config=$(which pkg-config) \
    --enable-cross-compile

make -j$(nproc)
make install

# Strip binary files
find "$FFMPEG_PREFIX" -type f -name "*.a" -exec $STRIP --strip-unneeded {} \; 2>/dev/null || true

# Download and extract Qt modules
cd "$BUILD_DIR"
for module in "${MODULES[@]}"; do
    if [ ! -d "$module" ]; then
        echo "Downloading $module..."
        curl -L -o "$module.zip" "$DOWNLOAD_BASE_URL/$module-everywhere-src-$QT_VERSION.zip"
        unzip -q "$module.zip"
        mv "$module-everywhere-src-$QT_VERSION" "$module"
        rm "$module.zip"
    fi
done

# Define common CMake flags
CMAKE_COMMON_FLAGS="-Wno-dev -DCMAKE_POLICY_DEFAULT_CMP0177=NEW -DCMAKE_POLICY_DEFAULT_CMP0174=NEW \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_FLAGS=\"-Os -ffunction-sections -fdata-sections\" \
    -DCMAKE_C_FLAGS=\"-Os -ffunction-sections -fdata-sections\" \
    -DCMAKE_EXE_LINKER_FLAGS=\"-Wl,--gc-sections\" \
    -DCMAKE_SHARED_LINKER_FLAGS=\"-Wl,--gc-sections\" \
    -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON \
    -DCMAKE_SYSTEM_NAME=Linux \
    -DCMAKE_SYSTEM_PROCESSOR=aarch64 \
    -DCMAKE_C_COMPILER=$CC \
    -DCMAKE_CXX_COMPILER=$CXX \
    -DQT_FEATURE_reduce_relocations=OFF"

# First build qtbase
echo "Building qtbase..."
cd "$BUILD_DIR/qtbase"
mkdir -p build
cd build

# Create toolchain file for musl libc and Alpine environment
cat > $BUILD_DIR/toolchain.cmake << EOF
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_C_COMPILER $CC)
set(CMAKE_CXX_COMPILER $CXX)
set(CMAKE_FIND_ROOT_PATH /usr/aarch64-alpine-linux-musl)
set(CMAKE_SYSROOT /usr/aarch64-alpine-linux-musl)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
EOF

# Configure qtbase for musl libc and Alpine environment
cmake -GNinja \
    $CMAKE_COMMON_FLAGS \
    -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX" \
    -DBUILD_SHARED_LIBS=OFF \
    -DFEATURE_sql=OFF \
    -DFEATURE_testlib=OFF \
    -DFEATURE_icu=OFF \
    -DFEATURE_opengl=ON \
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
    -DCMAKE_TOOLCHAIN_FILE=$BUILD_DIR/toolchain.cmake \
    -DINPUT_doubleconversion=qt \
    -DINPUT_pcre=qt \
    -DFEATURE_libudev=OFF \
    -DFEATURE_glib=OFF \
    -DFEATURE_eventfd=OFF \
    -DFEATURE_inotify=OFF \
    ..

ninja
ninja install
# Strip binaries
find "$INSTALL_PREFIX" -type f -executable -exec $STRIP --strip-unneeded {} \; 2>/dev/null || true

# Build qtshadertools
echo "Building qtshadertools..."
cd "$BUILD_DIR/qtshadertools"
mkdir -p build
cd build
cmake -GNinja \
    $CMAKE_COMMON_FLAGS \
    -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX" \
    -DBUILD_SHARED_LIBS=OFF \
    -DCMAKE_PREFIX_PATH="$INSTALL_PREFIX" \
    -DFEATURE_optimize_size=ON \
    -DCMAKE_TOOLCHAIN_FILE=$BUILD_DIR/toolchain.cmake \
    ..

ninja
ninja install

# Build qtdeclarative (Qt Quick)
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
    -DCMAKE_TOOLCHAIN_FILE=$BUILD_DIR/toolchain.cmake \
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

        # Add specific flags for qtmultimedia
        if [[ "$module" == "qtmultimedia" ]]; then
            PKG_CONFIG_PATH="$FFMPEG_PREFIX/lib/pkgconfig:$PKG_CONFIG_PATH" \
            cmake -GNinja \
                $CMAKE_COMMON_FLAGS \
                -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX" \
                -DCMAKE_PREFIX_PATH="$INSTALL_PREFIX;$FFMPEG_PREFIX" \
                -DBUILD_SHARED_LIBS=OFF \
                -DQT_FEATURE_gstreamer=OFF \
                -DQT_FEATURE_pulseaudio=OFF \
                -DQT_FEATURE_ffmpeg=ON \
                -DFFMPEG_avcodec_LIBRARY="$FFMPEG_PREFIX/lib/libavcodec.a" \
                -DFFMPEG_avformat_LIBRARY="$FFMPEG_PREFIX/lib/libavformat.a" \
                -DFFMPEG_avutil_LIBRARY="$FFMPEG_PREFIX/lib/libavutil.a" \
                -DFFMPEG_swresample_LIBRARY="$FFMPEG_PREFIX/lib/libswresample.a" \
                -DFFMPEG_swscale_LIBRARY="$FFMPEG_PREFIX/lib/libswscale.a" \
                -DFFMPEG_INCLUDE_DIR="$FFMPEG_PREFIX/include" \
                -DCMAKE_EXE_LINKER_FLAGS="-L$FFMPEG_PREFIX/lib" \
                -DCMAKE_TOOLCHAIN_FILE=$BUILD_DIR/toolchain.cmake \
                ..
        else
            cmake -GNinja \
                $CMAKE_COMMON_FLAGS \
                -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX" \
                -DCMAKE_PREFIX_PATH="$INSTALL_PREFIX" \
                -DBUILD_SHARED_LIBS=OFF \
                -DFEATURE_optimize_size=ON \
                -DCMAKE_TOOLCHAIN_FILE=$BUILD_DIR/toolchain.cmake \
                ..
        fi
        
        ninja
        ninja install
        # Strip binaries after each module installation
        find "$INSTALL_PREFIX" -type f -executable -exec $STRIP --strip-unneeded {} \; 2>/dev/null || true
    fi
done

# Clean up build directory to reduce image size
rm -rf "$BUILD_DIR/*/build"

echo "OpenTerface Alpine ARM64 QT $QT_VERSION successfully built and installed to $INSTALL_PREFIX"
echo "Size optimizations enabled - final build should be significantly smaller"
du -sh "$INSTALL_PREFIX"

# Ensure output directory permissions are correct
chmod -R 755 "$INSTALL_PREFIX"

# QT version
QT_VERSION="6.6.1"
QT_MAJOR="6.6"

# Download Qt source
echo "Downloading Qt ${QT_VERSION}..."
mkdir -p /opt/qt-src
cd /opt/qt-src
wget -q https://download.qt.io/official_releases/qt/${QT_MAJOR}/${QT_VERSION}/single/qt-everywhere-src-${QT_VERSION}.tar.xz
tar xf qt-everywhere-src-${QT_VERSION}.tar.xz
cd qt-everywhere-src-${QT_VERSION}

# Create build directory
mkdir -p build
cd build

# Configure Qt for cross-compilation
echo "Configuring Qt for ARM64 cross-compilation..."
../configure \
    -release \
    -opensource \
    -confirm-license \
    -xplatform linux-aarch64-gnu-g++ \
    -nomake examples \
    -nomake tests \
    -skip qtwebengine \
    -prefix /opt/qt6-arm64

# Build Qt
echo "Building Qt..."
make -j$(nproc)

# Install Qt
echo "Installing Qt..."
make install

echo "Qt ${QT_VERSION} has been successfully built and installed to /opt/qt6-arm64"
