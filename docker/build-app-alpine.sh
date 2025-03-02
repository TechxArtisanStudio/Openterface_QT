#!/bin/sh
set -e

# Set build environment variables for ARM64 Alpine
export ARCH=aarch64
export CROSS_COMPILE=aarch64-alpine-linux-musl-
export PKG_CONFIG_PATH=/usr/aarch64-alpine-linux-musl/lib/pkgconfig
export CC=${CROSS_COMPILE}gcc
export CXX=${CROSS_COMPILE}g++
export LD=${CROSS_COMPILE}ld
export AR=${CROSS_COMPILE}ar
export STRIP=${CROSS_COMPILE}strip
export QT_INSTALL_DIR=${QT_INSTALL_DIR:-/opt/Qt6-arm64}
export SOURCE_DIR=${SOURCE_DIR:-/app}
export BUILD_DIR=${BUILD_DIR:-/app/build}

# Create build directory
mkdir -p $BUILD_DIR

# Create toolchain file
cat > $BUILD_DIR/toolchain.cmake << EOF
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_C_COMPILER $CC)
set(CMAKE_CXX_COMPILER $CXX)
set(CMAKE_FIND_ROOT_PATH /usr/aarch64-alpine-linux-musl)
set(CMAKE_SYSROOT /usr/aarch64-alpine-linux-musl)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
EOF

# Change to build directory
cd $BUILD_DIR

# Configure ARM64 build
echo "Configuring Alpine ARM64 build..."
cmake -S "$SOURCE_DIR" -B . \
    -DCMAKE_BUILD_TYPE=MinSizeRel \
    -DBUILD_SHARED_LIBS=OFF \
    -DCMAKE_PREFIX_PATH="$QT_INSTALL_DIR" \
    -DCMAKE_INSTALL_PREFIX=release \
    -DCMAKE_CXX_FLAGS="-Os -ffunction-sections -fdata-sections" \
    -DCMAKE_C_FLAGS="-Os -ffunction-sections -fdata-sections" \
    -DCMAKE_EXE_LINKER_FLAGS="-Wl,--gc-sections -Wl,--strip-all" \
    -DCMAKE_POLICY_DEFAULT_CMP0177=NEW \
    -DCMAKE_POLICY_DEFAULT_CMP0174=NEW \
    -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON \
    -DCMAKE_TOOLCHAIN_FILE=$BUILD_DIR/toolchain.cmake \
    -DQT_DEBUG_FIND_PACKAGE=ON

# Build application
echo "Building application..."
cmake --build . --verbose

if [ ! -f openterfaceQT ]; then
  echo "Error: Failed to build openterfaceQT"
  exit 1
fi

# Strip binary file
echo "Stripping binary file..."
$STRIP --strip-all openterfaceQT

# Add UPX compression (if available)
if command -v upx >/dev/null 2>&1; then
  echo "Compressing executable with UPX..."
  upx --best --lzma openterfaceQT
else
  echo "UPX not available, skipping compression step..."
fi

# Create output directory
mkdir -p /app/output
cp openterfaceQT /app/output/

# Display final file size
echo "Final executable size:"
ls -lh /app/output/openterfaceQT

# Set correct permissions
chmod 755 /app/output/openterfaceQT
