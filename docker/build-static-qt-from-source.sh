#!/bin/bash
set -e

# To install OpenTerface QT, you can run this script as a user with appropriate permissions.

# Install minimal build requirements
sudo apt-get update
sudo apt-get install -y build-essential meson ninja-build bison flex pkg-config python3-pip linux-headers-$(uname -r) \
    autoconf automake libtool autoconf-archive cmake libxml2-dev


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

sudo apt-get install -y libgl1-mesa-dev libglu1-mesa-dev libxrender-dev libxi-dev

# Build qtbase first
echo "Building qtbase..."
cd "$BUILD_DIR/qtbase"
mkdir build
cd build
cmake -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX" \
    -DBUILD_SHARED_LIBS=OFF \
    -DFEATURE_dbus=ON \
    -DFEATURE_sql=OFF \
    -DFEATURE_testlib=OFF \
    -DFEATURE_icu=OFF \
    -DFEATURE_opengl=ON \
    -DFEATURE_opengl_desktop=OFF \
    -DFEATURE_opengles2=ON \
    -DCMAKE_PREFIX_PATH="/usr" \
    -DFEATURE_accessibility=OFF \
    -DCMAKE_BUILD_TYPE=Release \
    -DQT_BUILD_EXAMPLES=OFF \
    -DQT_BUILD_TESTS=OFF \
    -DFEATURE_static_runtime=ON \
    ..

echo "Building qtbase..."
cmake --build .
echo "Installing qtbase..."
cmake --install .


# Build qtshadertools
echo "Building x..."
cd "$BUILD_DIR/qtshadertools"
mkdir build
cd build
cmake -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX" \
    -DCMAKE_PREFIX_PATH="$INSTALL_PREFIX" \
    -DBUILD_SHARED_LIBS=OFF \
    -DFEATURE_static_runtime=ON \
    -DCMAKE_EXE_LINKER_FLAGS="/usr/lib/libXau.a /usr/lib/libXdmcp.a" \
    ..

echo "Building qtshadertools..."
cmake --build .
echo "Installing qtshadertools..."
cmake --install .

# Build other modules
for module in "${MODULES[@]}"; do
    if [[ "$module" != "qtbase" && "$module" != "qtshadertools" ]]; then
        cd "$BUILD_DIR/$module"
        mkdir build
        cd build
        echo "Building $module..."

        cmake -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX" \
            -DCMAKE_PREFIX_PATH="$INSTALL_PREFIX" \
            -DBUILD_SHARED_LIBS=OFF \
            ..

        
        echo "Building $module..."
        cmake --build .
        echo "Installing $module..."
        cmake --install .
    fi
done