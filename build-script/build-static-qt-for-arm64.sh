#!/bin/bash

set -e

# Default settings
QT_VERSION=${QT_VERSION:-"6.6.3"}
QT_MODULES=${QT_MODULES:-"qtbase qtdeclarative qtsvg qttools qtshadertools"}
QT_TARGET_DIR=${QT_TARGET_DIR:-"/opt/Qt6-arm64"}
CROSS_COMPILE=${CROSS_COMPILE:-"aarch64-linux-gnu-"}
FFMPEG_VERSION="6.1.1"

echo "Starting Qt ${QT_VERSION} and FFmpeg ${FFMPEG_VERSION} build for ARM64..."

# Install cross-compilation dependencies
echo "Installing cross-compilation dependencies..."
sudo apt-get update
sudo apt-get install -y \
  build-essential \
  ninja-build \
  cmake \
  pkg-config \
  gcc-aarch64-linux-gnu \
  g++-aarch64-linux-gnu \
  binutils-aarch64-linux-gnu \
  nasm \
  yasm \
  libass-dev \
  git \
  wget

# Create build directory
WORK_DIR=$(mktemp -d)
echo "Working directory: ${WORK_DIR}"
cd "${WORK_DIR}"

# Build FFmpeg
echo "Building static FFmpeg for ARM64..."
mkdir -p ffmpeg_sources ffmpeg_build
cd ffmpeg_sources

# Download FFmpeg
wget -O ffmpeg-${FFMPEG_VERSION}.tar.bz2 https://ffmpeg.org/releases/ffmpeg-${FFMPEG_VERSION}.tar.bz2
tar xjf ffmpeg-${FFMPEG_VERSION}.tar.bz2
cd ffmpeg-${FFMPEG_VERSION}

# Configure FFmpeg for ARM64 cross-compilation
PKG_CONFIG_PATH="${WORK_DIR}/ffmpeg_build/lib/pkgconfig" \
./configure \
  --prefix="${WORK_DIR}/ffmpeg_build" \
  --pkg-config="pkg-config" \
  --cross-prefix=${CROSS_COMPILE} \
  --arch=aarch64 \
  --target-os=linux \
  --enable-cross-compile \
  --enable-static \
  --disable-shared \
  --disable-doc \
  --disable-programs \
  --disable-autodetect \
  --disable-everything \
  --enable-encoder=aac,opus,h264,hevc \
  --enable-decoder=aac,mp3,opus,h264,hevc \
  --enable-parser=aac,mpegaudio,opus,h264,hevc \
  --enable-small \
  --disable-debug

# Build FFmpeg
make -j$(nproc)
make install

# Add ffmpeg install dir to PKG_CONFIG_PATH
export PKG_CONFIG_PATH="${WORK_DIR}/ffmpeg_build/lib/pkgconfig:${PKG_CONFIG_PATH}"
cd "${WORK_DIR}"

# Download Qt sources
echo "Downloading Qt sources..."
git clone https://code.qt.io/qt/qt5.git -b v${QT_VERSION} --depth=1
cd qt5

# Initialize repository with required modules
./init-repository --module-subset="${QT_MODULES}" -f --no-update

# Configure Qt for ARM64 cross-compilation
echo "Configuring Qt for ARM64..."
mkdir -p build
cd build

../configure -static -release \
  -prefix ${QT_TARGET_DIR} \
  -platform linux-aarch64-gnu-g++ \
  -device-option CROSS_COMPILE=${CROSS_COMPILE} \
  -nomake tests -nomake examples \
  -qt-libpng -qt-libjpeg \
  -no-cups -no-sql-sqlite \
  -no-feature-printer -no-feature-sql \
  -no-feature-testlib -no-feature-accessibility \
  -no-feature-future -no-feature-regularexpression \
  -no-feature-xmlstream -no-feature-sessionmanager \
  -no-opengl ${QT_CONFIGURE_OPTS}

# Build and install Qt
echo "Building Qt for ARM64..."
make -j$(nproc)

echo "Installing Qt to ${QT_TARGET_DIR}..."
sudo mkdir -p ${QT_TARGET_DIR}
sudo make install

# Copy FFmpeg libraries to the Qt target directory
echo "Copying FFmpeg libraries to ${QT_TARGET_DIR}..."
sudo mkdir -p ${QT_TARGET_DIR}/lib
sudo cp -a ${WORK_DIR}/ffmpeg_build/lib/. ${QT_TARGET_DIR}/lib/
sudo mkdir -p ${QT_TARGET_DIR}/include
sudo cp -a ${WORK_DIR}/ffmpeg_build/include/. ${QT_TARGET_DIR}/include/

# Clean up
cd /
sudo rm -rf "$WORK_DIR"

echo "Qt ${QT_VERSION} and FFmpeg ${FFMPEG_VERSION} for ARM64 build completed successfully!"
echo "Qt installed to: ${QT_TARGET_DIR}"
echo "FFmpeg libraries installed to: ${QT_TARGET_DIR}/lib"
echo "FFmpeg headers installed to: ${QT_TARGET_DIR}/include"
