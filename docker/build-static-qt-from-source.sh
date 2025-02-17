#!/bin/bash
set -e

# To install OpenTerface QT, you can run this script as a user with appropriate permissions.

# Install required packages
sudo apt-get update
sudo apt-get install -y build-essential libgl1-mesa-dev libglu1-mesa-dev
sudo apt-get install -y '^libxcb.*-dev' libx11-xcb-dev libglu1-mesa-dev libxrender-dev libxi-dev
sudo apt-get install -y libglib2.0-dev meson ninja-build bison flex

# Configuration
XKBCOMMON_VERSION=1.7.0
QT_VERSION=6.5.3
QT_MAJOR_VERSION=6.5
INSTALL_PREFIX=/opt/Qt6
BUILD_DIR=$(pwd)/qt-build
MODULES=("qtbase" "qtshadertools" "qtmultimedia" "qtsvg" "qtserialport")
DOWNLOAD_BASE_URL="https://download.qt.io/archive/qt/$QT_MAJOR_VERSION/$QT_VERSION/submodules"

# Check for required tools
command -v curl >/dev/null 2>&1 || { echo "Curl is not installed. Please install Curl."; exit 1; }
command -v cmake >/dev/null 2>&1 || { echo "CMake is not installed. Please install CMake."; exit 1; }
command -v meson >/dev/null 2>&1 || { echo "Meson is not installed. Please install Meson."; exit 1; }
command -v ninja >/dev/null 2>&1 || { echo "Ninja is not installed. Please install Ninja."; exit 1; }

# Create build directory
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Build libxkbcommon from source
echo "Building libxkbcommon $XKBCOMMON_VERSION from source..."
if [ ! -d "libxkbcommon" ]; then
    curl -L -o libxkbcommon.tar.gz "https://xkbcommon.org/download/libxkbcommon-${XKBCOMMON_VERSION}.tar.xz"
    tar xf libxkbcommon.tar.gz
    mv "libxkbcommon-${XKBCOMMON_VERSION}" libxkbcommon
    rm libxkbcommon.tar.gz
fi

cd libxkbcommon
mkdir -p build
cd build
meson setup --prefix=/usr \
    -Denable-docs=false \
    -Denable-wayland=false \
    -Denable-x11=true \
    ..
ninja
sudo ninja install
cd "$BUILD_DIR"

# Download and extract modules
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
mkdir build
cd build
cmake -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX" \
    -DBUILD_SHARED_LIBS=OFF \
    -DFEATURE_dbus=ON \
    -DFEATURE_sql=OFF \
    -DFEATURE_testlib=OFF \
    -DFEATURE_icu=OFF \
    -DFEATURE_opengl=ON \
    ..

echo "Building qtbase..."
cmake --build .
echo "Installing qtbase..."
cmake --install .


# Build qtshadertools
echo "Building qtshadertools..."
cd "$BUILD_DIR/qtshadertools"
mkdir build
cd build
cmake -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX" \
    -DCMAKE_PREFIX_PATH="$INSTALL_PREFIX" \
    -DBUILD_SHARED_LIBS=OFF \
    ..

echo "Building qtshadertools..."
cmake --build . --parallel
echo "Installing qtshadertools..."
cmake --install .

# Build other modules
for module in "${MODULES[@]}"; do
    if [[ "$module" != "qtbase" && "$module" != "qtshadertools" ]]; then
        cd "$BUILD_DIR/$module"
        mkdir build
        cd build
        echo "Building $module..."
        if [[ "$module" == "qtmultimedia" ]]; then
            # Special configuration for qtmultimedia to enable FFmpeg
            cmake -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX" \
                -DCMAKE_PREFIX_PATH="$INSTALL_PREFIX" \
                -DBUILD_SHARED_LIBS=OFF \
                ..
        else
            cmake -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX" \
                -DCMAKE_PREFIX_PATH="$INSTALL_PREFIX" \
                -DBUILD_SHARED_LIBS=OFF \
                ..
        fi
        
        echo "Building $module..."
        cmake --build . --parallel
        echo "Installing $module..."
        cmake --install .
    fi
done

# Quick fix: Add -loleaut32 to qnetworklistmanager.prl
PRL_FILE="$INSTALL_PREFIX/plugins/networkinformation/qnetworklistmanager.prl"
if [ -f "$PRL_FILE" ]; then
    echo "Updating $PRL_FILE to include -loleaut32..."
    echo "QMAKE_PRL_LIBS += -loleaut32" >> "$PRL_FILE"
else
    echo "Warning: $PRL_FILE not found. Please check the build process."
fi

# Check for Qt6Config.cmake
if [ ! -f "$INSTALL_PREFIX/lib/cmake/Qt6/Qt6Config.cmake" ]; then
    echo "Error: Qt6Config.cmake not found. Please check the installation."
    exit 1
fi