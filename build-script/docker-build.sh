#!/bin/bash
set -e

echo "Building Openterface QT Application in docker build environment..."

# Copy configuration files
mkdir -p /workspace/build/config/languages
mkdir -p /workspace/build/config/keyboards
cp -r config/keyboards/*.json /workspace/build/config/keyboards/ 2>/dev/null || echo "No keyboard configs"
cp -r config/languages/*.qm /workspace/build/config/languages/ 2>/dev/null || echo "No language files"

# Build with CMake
cd /workspace/build
echo "Configuring with CMake..."

echo "Finding FFmpeg libraries..."
find / -name "libavformat.so*" || echo "No shared libavformat found"
find / -name "libavformat.a*" || echo "No shared libavformat found"
find / -name "avformat.h" || echo "No avformat.h found"

# Debug pkg-config before CMake
pkg-config --version
pkg-config --list-all | grep -E "avformat|avcodec" || echo "No FFmpeg packages found"
echo "========================="

# Default to dynamic linking for the shared build environment; allow override via OPENTERFACE_BUILD_STATIC env
: "${OPENTERFACE_BUILD_STATIC:=OFF}"
echo "OPENTERFACE_BUILD_STATIC set to ${OPENTERFACE_BUILD_STATIC}"
cmake \
	-DCMAKE_BUILD_TYPE=Release \
	-DOPENTERFACE_BUILD_STATIC=${OPENTERFACE_BUILD_STATIC} \
	-DCMAKE_VERBOSE_MAKEFILE=ON \
	/workspace/src

echo "Building with CMake..."
make -j4 VERBOSE=1

echo "Build completed successfully"
