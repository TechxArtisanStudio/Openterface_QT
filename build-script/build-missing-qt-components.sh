#!/bin/bash
set -e

# Script to build missing Qt6 components (qtserialport and qtsvg)
# This script is designed to be used in CI environments where these packages are not available

QT_VERSION=6.6.3
QT_MAJOR_VERSION=6.6
INSTALL_PREFIX=/opt/Qt6
BUILD_DIR=$(pwd)/qt-missing-build
MODULES=("qtbase" "qtserialport" "qtsvg")
DOWNLOAD_BASE_URL="https://download.qt.io/archive/qt/$QT_MAJOR_VERSION/$QT_VERSION/submodules"

echo "Building missing Qt6 components: qtserialport and qtsvg"

# Create the build directory
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Define common CMake flags to suppress warnings
CMAKE_COMMON_FLAGS="-Wno-dev -DCMAKE_POLICY_DEFAULT_CMP0177=NEW -DCMAKE_POLICY_DEFAULT_CMP0174=NEW"

# Download and extract required modules
for module in "${MODULES[@]}"; do
    if [ ! -d "$module" ]; then
        echo "Downloading $module..."
        curl -L -o "$module.zip" "$DOWNLOAD_BASE_URL/$module-everywhere-src-$QT_VERSION.zip"
        unzip -q "$module.zip"
        mv "$module-everywhere-src-$QT_VERSION" "$module"
        rm "$module.zip"
    fi
done

# Build qtbase first (minimal build as dependency)
echo "Building qtbase (minimal for dependencies)..."
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
ninja install

# Build qtserialport
echo "Building qtserialport..."
cd "$BUILD_DIR/qtserialport"
mkdir -p build
cd build

cmake -GNinja \
    $CMAKE_COMMON_FLAGS \
    -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX" \
    -DCMAKE_PREFIX_PATH="$INSTALL_PREFIX" \
    -DBUILD_SHARED_LIBS=OFF \
    ..

ninja
ninja install

# Build qtsvg
echo "Building qtsvg..."
cd "$BUILD_DIR/qtsvg"
mkdir -p build
cd build

cmake -GNinja \
    $CMAKE_COMMON_FLAGS \
    -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX" \
    -DCMAKE_PREFIX_PATH="$INSTALL_PREFIX" \
    -DBUILD_SHARED_LIBS=OFF \
    ..

ninja
ninja install

echo "Successfully built and installed qtserialport and qtsvg to $INSTALL_PREFIX"

# Verify installation
echo "Verifying installation..."
if [ -f "$INSTALL_PREFIX/lib/cmake/Qt6SerialPort/Qt6SerialPortConfig.cmake" ]; then
    echo "✅ Qt6SerialPort installed successfully"
else
    echo "❌ Qt6SerialPort installation failed"
    exit 1
fi

if [ -f "$INSTALL_PREFIX/lib/cmake/Qt6Svg/Qt6SvgConfig.cmake" ]; then
    echo "✅ Qt6Svg installed successfully"
else
    echo "❌ Qt6Svg installation failed"
    exit 1
fi

echo "All missing Qt6 components have been built and installed successfully!"
