#!/bin/bash
set -e

# To install OpenTerface QT, you can run this script as a user with appropriate permissions.

# Install minimal build requirements
sudo apt-get update
sudo apt-get install -y build-essential meson ninja-build bison flex pkg-config python3-pip linux-headers-$(uname -r) \
    autoconf automake libtool autoconf-archive cmake libxml2-dev libxrandr-dev libegl-dev libegl-mesa0 libegl1 libgl-dev \
    libgl1-mesa-dev libgles-dev libgles1 libgles2 libglu1-mesa libglu1-mesa-dev libglvnd-core-dev \
    libglvnd-dev libglx-dev libopengl-dev libopengl0 libxcb-cursor-dev \
    libxcb-cursor0 libxcb-icccm4 libxcb-icccm4-dev libxcb-image0 \
    libxcb-image0-dev libxcb-keysyms1 libxcb-keysyms1-dev libxcb-render-util0 \
    libxcb-render-util0-dev libxcb-render0-dev libxcb-shm0-dev libxcb-util1 \
    libxfixes-dev libxi-dev libx11-dev libx11-xcb-dev libxext-dev libxfixes-dev \
    libdbus-1-dev libfontconfig1-dev libfreetype-dev libxkbcommon-dev libxkbcommon-x11-dev libxrandr2 libxrandr-dev \
    libxcb-randr0-dev libxcb-shape0-dev libxcb-sync-dev \
    libxrender-dev libxcb1-dev libxcb-glx0-dev libxcb-xfixes0-dev \
    libxcb-xinerama0-dev libxcb-xkb-dev libxcb-util-dev \
    libdrm-dev libgbm-dev libatspi2.0-dev \
    libvulkan-dev libssl-dev \
    libpulse-dev \
    yasm nasm # Dependencies for FFmpeg compilation

QT_VERSION=6.6.3
QT_MAJOR_VERSION=6.6
INSTALL_PREFIX=/opt/Qt6
BUILD_DIR=$(pwd)/qt-build
FFMPEG_PREFIX="$BUILD_DIR/ffmpeg-install"
# Only include essential modules
MODULES=("qtbase" "qtshadertools" "qtdeclarative" "qtmultimedia" "qtsvg" "qtserialport")
DOWNLOAD_BASE_URL="https://download.qt.io/archive/qt/$QT_MAJOR_VERSION/$QT_VERSION/submodules"

# Create the build directory first
mkdir -p "$BUILD_DIR"

# Build FFmpeg statically with minimal size
echo "Building FFmpeg statically..."
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
    --enable-parser=aac,h264,hevc

make -j$(nproc)
make install
# Strip binaries
find "$FFMPEG_PREFIX" -type f -name "*.a" -exec strip --strip-unneeded {} \;

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
    -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON"

# Build qtbase first
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
    -DFEATURE_concurrent=OFF \
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
    ..

ninja
sudo ninja install
# Strip binaries
sudo find "$INSTALL_PREFIX" -type f -executable -exec strip --strip-unneeded {} \; 2>/dev/null || true

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
    ..

ninja
sudo ninja install

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
    ..

ninja
sudo ninja install

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
                ..
        else
            cmake -GNinja \
                $CMAKE_COMMON_FLAGS \
                -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX" \
                -DCMAKE_PREFIX_PATH="$INSTALL_PREFIX" \
                -DBUILD_SHARED_LIBS=OFF \
                -DFEATURE_optimize_size=ON \
                ..
        fi
        
        ninja
        sudo ninja install
        # Strip binaries after each module installation
        sudo find "$INSTALL_PREFIX" -type f -executable -exec strip --strip-unneeded {} \; 2>/dev/null || true
    fi
done

echo "OpenTerface QT $QT_VERSION has been successfully built and installed to $INSTALL_PREFIX"
echo "Size optimization enabled - final build should be significantly smaller"