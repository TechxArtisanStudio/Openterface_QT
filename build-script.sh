#!/bin/bash
set -e

echo "Building Openterface QT Application (shared Qt) for amd64..."

echo "Finding FFmpeg libraries..."
find / -name "libavformat.so*" || echo "No libavformat found"
find / -name "avformat.h" || echo "No avformat.h found"

# Copy configuration files
mkdir -p /workspace/build/config/languages
mkdir -p /workspace/build/config/keyboards
cp -r config/keyboards/*.json /workspace/build/config/keyboards/ 2>/dev/null || echo "No keyboard configs"
cp -r config/languages/*.qm /workspace/build/config/languages/ 2>/dev/null || echo "No language files"

# Build with CMake
cd /workspace/build
echo "Configuring with CMake..."

# Debug pkg-config before CMake
pkg-config --version
pkg-config --list-all | grep -E "avformat|avcodec" || echo "No FFmpeg packages found"
echo "========================="

cmake        -DCMAKE_BUILD_TYPE=Release       -DOPENTERFACE_BUILD_STATIC=ON       -DCMAKE_VERBOSE_MAKEFILE=ON       /workspace/src

echo "Building with CMake..."
make -j4 VERBOSE=1

echo "Build completed successfully"
