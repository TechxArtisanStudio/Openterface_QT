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

# Build qtbase first
echo "Building qtbase..."
cd "$BUILD_DIR/qtbase"
mkdir -p build
cd build

cmake -GNinja \
    -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX" \
    -DBUILD_SHARED_LIBS=OFF \
    -DFEATURE_dbus=OFF \
    -DFEATURE_sql=OFF \
    -DFEATURE_testlib=OFF \
    -DFEATURE_icu=OFF \
    -DFEATURE_opengl=ON \
    -DFEATURE_xcb=ON \
    -DFEATURE_xcb_xinput=system \
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
    -DBUILD_SHARED_LIBS=OFF \
    -DFEATURE_static_runtime=ON \
    -DCMAKE_EXE_LINKER_FLAGS="$DEPS_INSTALL_PREFIX/lib/libXau.a $DEPS_INSTALL_PREFIX/lib/libXdmcp.a $DEPS_INSTALL_PREFIX/lib/libexpat.a $DEPS_INSTALL_PREFIX/lib/libfreetype.a $DEPS_INSTALL_PREFIX/lib/libfontconfig.a" \
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