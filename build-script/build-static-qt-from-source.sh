#!/bin/bash
set -e

# To install OpenTerface QT, you can run this script as a user with appropriate permissions.

# Install minimal build requirements
sudo apt-get update
sudo apt-get install -y build-essential meson ninja-build bison flex pkg-config python3-pip linux-headers-$(uname -r) \
    autoconf automake libtool autoconf-archive cmake libxml2-dev libxrandr-dev \
    libglib2.0-dev libgtk-3-dev


QT_VERSION=6.5.3
QT_MAJOR_VERSION=6.5
INSTALL_PREFIX=/opt/Qt6
DEPS_INSTALL_PREFIX=/opt/qt-deps
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
    libxcb-cursor-dev libxcb-icccm4-dev libxcb-keysyms1-dev \
    libglib2.0-dev libglib2.0-bin libglib2.0-0 libgthread-2.0-0 libgtk-3-dev

# Build qtbase first
echo "Building qtbase..."
cd "$BUILD_DIR/qtbase"
mkdir -p build
cd build

export LD_LIBRARY_PATH=/usr/lib:/usr/local/lib:$DEPS_INSTALL_PREFIX/lib:$LD_LIBRARY_PATH

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
    -DQT_QMAKE_TARGET_MKSPEC=linux-g++ \
    -DCMAKE_EXE_LINKER_FLAGS="-lXau -lXdmcp" \
    ..

ninja
sudo ninja install


# Build qtshadertools
echo "Building qtshadertools..."
sudo apt-get install -y libfontconfig1-dev libfreetype6-dev
cd "$BUILD_DIR/qtshadertools"
mkdir -p build
cd build
cmake -GNinja \
    -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX" \
    -DCMAKE_PREFIX_PATH="$INSTALL_PREFIX" \
    -DBUILD_SHARED_LIBS=OFF \
    -DFEATURE_static_runtime=ON \
    -DCMAKE_EXE_LINKER_FLAGS="$DEPS_INSTALL_PREFIX/lib/libXau.a $DEPS_INSTALL_PREFIX/lib/libXdmcp.a -lfontconfig -lfreetype" \
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