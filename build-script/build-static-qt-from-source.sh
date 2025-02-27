#!/bin/bash
set -e

# To install OpenTerface QT, you can run this script as a user with appropriate permissions.

# Install minimal build requirements
sudo apt-get update
sudo apt-get install -y build-essential meson ninja-build bison flex pkg-config python3-pip linux-headers-$(uname -r) \
    autoconf automake libtool autoconf-archive cmake libxml2-dev libxrandr-dev


QT_VERSION=6.5.3
QT_MAJOR_VERSION=6.5
INSTALL_PREFIX=/opt/Qt6
DEPS_INSTALL_PREFIX=/opt/qt6-deps
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

sudo apt-get install -y libgl1-mesa-dev libglu1-mesa-dev libxrender-dev libxi-dev \
    '^libxcb.*-dev' libx11-xcb-dev libxcb-cursor-dev libxcb-icccm4-dev libxcb-keysyms1-dev \
    libxcb-xinput-dev
    
# Install more comprehensive set of XCB libraries
sudo apt-get install -y libgl1-mesa-dev libglu1-mesa-dev libxrender-dev libxi-dev \
    libxcb1-dev \
    libxcb-glx0-dev \
    libxcb-icccm4-dev \
    libxcb-image0-dev \
    libxcb-keysyms1-dev \
    libxcb-randr0-dev \
    libxcb-render0-dev \
    libxcb-render-util0-dev \
    libxcb-shape0-dev \
    libxcb-shm0-dev \
    libxcb-sync-dev \
    libxcb-xfixes0-dev \
    libxcb-xinerama0-dev \
    libxcb-xinput-dev \
    libxcb-xkb-dev \
    libxkbcommon-dev \
    libxkbcommon-x11-dev \
    libx11-xcb-dev \
    libxau-dev

# Install dependencies to DEPS_INSTALL_PREFIX first if they exist there
echo "Setting up dependency paths..."
export PKG_CONFIG_PATH="$DEPS_INSTALL_PREFIX/lib/pkgconfig:/usr/lib/pkgconfig:/usr/lib/x86_64-linux-gnu/pkgconfig:$PKG_CONFIG_PATH"
export LD_LIBRARY_PATH="$DEPS_INSTALL_PREFIX/lib:/usr/lib/x86_64-linux-gnu:$LD_LIBRARY_PATH"
export CMAKE_PREFIX_PATH="$DEPS_INSTALL_PREFIX:$CMAKE_PREFIX_PATH"

# Diagnostic: Check for XCB libraries
echo "Checking XCB libraries with pkg-config..."
pkg-config --exists --print-errors "xcb xcb-render xcb-shape xcb-shm xcb-icccm xcb-keysyms xcb-randr xcb-xfixes xcb-xinput xcb-xkb"
echo "Checking xkbcommon libraries with pkg-config..."
pkg-config --exists --print-errors "xkbcommon xkbcommon-x11"

# Build qtbase first
echo "Building qtbase..."
cd "$BUILD_DIR/qtbase"
mkdir -p build
cd build

# Force use of pkg-config to find XCB
export QT_XCB_CONFIG="system"

cmake -GNinja \
    -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX" \
    -DBUILD_SHARED_LIBS=OFF \
    -DFEATURE_dbus=OFF \
    -DFEATURE_sql=OFF \
    -DFEATURE_testlib=OFF \
    -DFEATURE_icu=OFF \
    -DFEATURE_opengl=ON \
    -DFEATURE_xcb=ON \
    -DFEATURE_xkbcommon=ON \
    -DFEATURE_xkbcommon_x11=ON \
    -DFEATURE_xcb_xinput=system \
    -DCMAKE_PREFIX_PATH="$DEPS_INSTALL_PREFIX" \
    -DPKG_CONFIG_USE_CMAKE_PREFIX_PATH=ON \
    -DQT_XCB_CONFIG="system" \
    -DXCB_XCB_INCLUDE_DIR=/usr/include/xcb \
    -DXCB_XCB_LIBRARY=/usr/lib/x86_64-linux-gnu/libxcb.so \
    -DXKBCOMMON_INCLUDE_DIR=/usr/include \
    -DXKBCOMMON_LIBRARY=/usr/lib/x86_64-linux-gnu/libxkbcommon.so \
    -DXKBCOMMON_X11_INCLUDE_DIR=/usr/include \
    -DXKBCOMMON_X11_LIBRARY=/usr/lib/x86_64-linux-gnu/libxkbcommon-x11.so \
    -DQT_QMAKE_TARGET_MKSPEC=linux-g++ \
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
    -DCMAKE_PREFIX_PATH="$INSTALL_PREFIX" \
    -DXCB_XCB_INCLUDE_DIR=/usr/include/xcb \
    -DXCB_XCB_LIBRARY=/usr/lib/x86_64-linux-gnu/libxcb.so \
    -DBUILD_SHARED_LIBS=OFF \
    -DFEATURE_static_runtime=ON \
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
            -DCMAKE_PREFIX_PATH="$INSTALL_PREFIX;$DEPS_INSTALL_PREFIX" \
            -DBUILD_SHARED_LIBS=OFF \
            -DPKG_CONFIG_USE_CMAKE_PREFIX_PATH=ON \
            ..
        
        ninja
        sudo ninja install
    fi
done