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
    clang-16 llvm-16-dev libclang-16-dev\
    yasm nasm # Dependencies for FFmpeg compilation

QT_VERSION=6.6.3
QT_MAJOR_VERSION=6.6
LIBUSB_VERSION=1.0.26
INSTALL_PREFIX=/opt/Qt6
BUILD_DIR=$(pwd)/qt-build
FFMPEG_PREFIX=/opt/Qt6
# Update module list to include qtdeclarative (which provides Qt Quick)
MODULES=("qtbase" "qtshadertools" "qtdeclarative" "qtmultimedia" "qtsvg" "qtserialport" "qttools")
DOWNLOAD_BASE_URL="https://download.qt.io/archive/qt/$QT_MAJOR_VERSION/$QT_VERSION/submodules"

# Create the build directory first
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"
# Build or Install libusb from source
if $BUILD_ENABLED; then
    echo "Building libusb $LIBUSB_VERSION from source..."
    if [ ! -d "libusb" ]; then
        curl -L -o libusb.tar.bz2 "https://github.com/libusb/libusb/releases/download/v${LIBUSB_VERSION}/libusb-${LIBUSB_VERSION}.tar.bz2"
        tar xf libusb.tar.bz2
        mv "libusb-${LIBUSB_VERSION}" libusb
        rm libusb.tar.bz2
    fi

    cd libusb
    ./configure --prefix="$FFMPEG_PREFIX" --enable-static --disable-shared --disable-udev
    make -j$(nproc)
fi

if $INSTALL_ENABLED; then
    echo "Installing libusb $LIBUSB_VERSION..."
    cd "$BUILD_DIR"/libusb
    sudo make install
fi
cd "$BUILD_DIR"

# Build FFmpeg statically
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
    --disable-debug

make -j$(nproc)
make install

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

# Define common CMake flags to suppress warnings
CMAKE_COMMON_FLAGS="-Wno-dev -DCMAKE_POLICY_DEFAULT_CMP0177=NEW -DCMAKE_POLICY_DEFAULT_CMP0174=NEW"

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
    -DQT_FEATURE_clang=OFF \
    -DFEATURE_clang=ON \
    ..

ninja
sudo ninja install

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
                -DFEATURE_gstreamer=OFF \
                -DINPUT_gstreamer=OFF \
                -DFEATURE_pulseaudio=ON \
                -DFEATURE_ffmpeg=ON \
                -DINPUT_ffmpeg=ON \
                -DFEATURE_avfoundation=OFF \
                -DCMAKE_FIND_ROOT_PATH="$FFMPEG_PREFIX" \
                -DCMAKE_EXE_LINKER_FLAGS="-L$FFMPEG_PREFIX/lib" \
                -DFFMPEG_PATH="$FFMPEG_PREFIX" \
                ..
        elif [[ "$module" == "qttools" ]]; then
            echo "Building $module..."
            CLANG_PREFIX="/usr/lib/llvm-16"
            cmake -GNinja \
                $CMAKE_COMMON_FLAGS \
                -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX" \
                -DBUILD_SHARED_LIBS=OFF \
                -DFEATURE_linguist=ON \
                -DFEATURE_lupdate=ON \
                -DFEATURE_lrelease=ON \
                -DFEATURE_designer=OFF \
                -DFEATURE_assistant=OFF \
                -DFEATURE_qtattributionsscanner=OFF \
                -DFEATURE_qtdiag=OFF \
                -DFEATURE_qtplugininfo=OFF \
                -DFEATURE_clang=ON \
                -DFEATURE_clangcpp=ON \
                -DLLVM_INSTALL_DIR="$CLANG_PREFIX" \
                -DLLVM_CMAKE_DIR="$CLANG_PREFIX/cmake" \
                ..

        else
            cmake -GNinja \
                $CMAKE_COMMON_FLAGS \
                -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX" \
                -DCMAKE_PREFIX_PATH="$INSTALL_PREFIX" \
                -DBUILD_SHARED_LIBS=OFF \
                ..
        fi
        
        ninja
        sudo ninja install
    fi
done

echo "OpenTerface QT $QT_VERSION has been successfully built and installed to $INSTALL_PREFIX"
