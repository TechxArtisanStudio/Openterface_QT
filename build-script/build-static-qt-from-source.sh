#!/bin/bash
set -e

# To install OpenTerface QT, you can run this script as a user with appropriate permissions.

# Install minimal build requirements
sudo apt-get update
sudo apt-get install -y build-essential meson ninja-build bison flex pkg-config python3-pip linux-headers-$(uname -r) \
    autoconf automake libtool autoconf-archive cmake libxml2-dev libxrandr-dev \
    libglib2.0-dev libgtk-3-dev

# Add EGL and OpenGL dependencies
sudo apt-get install -y libgl1-mesa-dev libgles2-mesa-dev libegl1-mesa-dev \
    libdrm-dev libgbm-dev libxkbcommon-dev libxkbcommon-x11-dev \
    libxcb-icccm4-dev libxcb-image0-dev libxcb-keysyms1-dev libxcb-render-util0-dev \
    libxcb-xinerama0-dev libxcb-xkb-dev libxcb-randr0-dev libxcb-shape0-dev

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

# Build qtbase first
echo "Building qtbase..."
cd "$BUILD_DIR/qtbase"
mkdir -p build
cd build

cmake -GNinja \
    -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX" \
    -DBUILD_SHARED_LIBS=OFF \
    -DFEATURE_xcb=ON \
    -DFEATURE_xcb_xlib=ON \
    -DFEATURE_xkbcommon=ON \
    -DFEATURE_xkbcommon_x11=ON \
    -DTEST_xcb_syslibs=ON \
    -DFEATURE_egl=ON \
    -DFEATURE_opengl=ON \
    -DFEATURE_harfbuzz=OFF \
    -DFEATURE_androiddeployqt=OFF \
    -DFEATURE_vnc=OFF \
    -DFEATURE_opengl_desktop=ON \
    -DFEATURE_accessibility=ON \
    -DFEATURE_sql=OFF \
    -DINPUT_opengl=desktop \
    -DQT_QMAKE_TARGET_MKSPEC=linux-g++ \
    -DQT_BUILD_EXAMPLES=OFF \
    -DQT_BUILD_TESTS=OFF \
    ..

ninja
sudo ninja install

# Build qtshadertools
echo "Building qtshadertools..."
sudo apt-get install -y libfontconfig1-dev libfreetype6-dev libharfbuzz-dev
cd "$BUILD_DIR/qtshadertools"
mkdir -p build
cd build
cmake -GNinja \
    -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX" \
    -DBUILD_SHARED_LIBS=OFF \
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
            -DBUILD_SHARED_LIBS=OFF \
            ..
        
        ninja
        sudo ninja install
    fi
done

echo "Qt build completed successfully."