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
    libdbus-1-dev libfontconfig1-dev libfreetype6-dev

QT_VERSION=6.5.3
QT_MAJOR_VERSION=6.5
INSTALL_PREFIX=/opt/Qt6
BUILD_DIR=$(pwd)/qt-build
MODULES=("qtbase" "qtshadertools" "qtmultimedia" "qtsvg" "qtserialport")
DOWNLOAD_BASE_URL="https://download.qt.io/archive/qt/$QT_MAJOR_VERSION/$QT_VERSION/submodules"


# Download and extract modules
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"
for module in "${MODULES[@]}"; do
    if [ ! -d "$module" ]; then
        curl -L -o "$module.zip" "$DOWNLOAD_BASE_URL/$module-everywhere-src-$QT_VERSION.zip"
        unzip -q "$module.zip"
        mv "$module-everywhere-src-$QT_VERSION" "$module"
        rm "$module.zip"
    fi
done

# Build qtbase first
echo "Building qtbase..."
cd "$BUILD_DIR/qtbase"
mkdir -p build
cd build

cmake -GNinja \
    -Wno-dev \
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
    ..

ninja
sudo ninja install


# Build qtshadertools
echo "Building qtshadertools..."
cd "$BUILD_DIR/qtshadertools"
mkdir -p build
cd build
cmake -GNinja \
    -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX" \
    -DBUILD_SHARED_LIBS=OFF \
    -DCMAKE_EXE_LINKER_FLAGS="-lfontconfig -lfreetype" \
    ..

ninja
sudo ninja install

# Build other modules
for module in "${MODULES[@]}"; do
    if [[ "$module" != "qtbase" && "$module" != "qtshadertools" ]]; then
        cd "$BUILD_DIR/$module"
        mkdir -p build
        cd build
        echo "Building $module..."

        cmake -GNinja \
            -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX" \
            -DCMAKE_PREFIX_PATH="$INSTALL_PREFIX" \
            -DBUILD_SHARED_LIBS=OFF \
            ..

        
        ninja
        sudo ninja install
    fi
done