#!/bin/bash
# =============================================================================
# Openterface QT Linux Build Script (Shared Libraries)
# =============================================================================
#
# This script builds the Openterface QT application using static FFmpeg libraries
# (when available) combined with shared Qt and system libraries. This provides
# FFmpeg functionality without external dependencies while keeping the build
# size reasonable for other components.
#
# USAGE:
# ./build-sharedlib-app.sh [BUILD_DIR]
#
# PARAMETERS:
# BUILD_DIR: Optional build directory path (default: ./build)
#
# OVERVIEW:
# The script performs the following operations:
# 1. Installs build dependencies (if needed)
# 2. Generates language files
# 3. Configures CMake for shared library linking
# 4. Builds the application
# 5. Provides information about the built binary
#
# REQUIREMENTS:
# - Debian-based Linux distribution (Ubuntu, Kali, etc.)
# - Source code already available (for CI/CD workflows)
# - sudo privileges for dependency installation (optional in CI)
# - Internet connection for package downloads
# - Sufficient disk space for Qt6 development tools and dependencies
#
# SUPPORTED ARCHITECTURES:
# - x86_64 (Intel/AMD 64-bit)
# - ARM64/aarch64 (ARM 64-bit, including Raspberry Pi 4+)
#
# OUTPUT:
# - Built binary: [BUILD_DIR]/openterfaceQT
# - No system installation or integration
# - Suitable for CI/CD workflows and development builds
#
# AUTHOR: TechxArtisan Studio
# LICENSE: See LICENSE file in the project repository
# =============================================================================

set -e

# Parse command line arguments
BUILD_DIR="${1:-build}"

echo "🏗️  Openterface QT Build Script (Static FFmpeg + Shared Qt)"
echo "========================================================"
echo "Build directory: $BUILD_DIR"
echo ""

# Check if we're in the right directory
if [ ! -f "openterfaceQT.pro" ] && [ ! -f "CMakeLists.txt" ]; then
    echo "❌ Error: This script must be run from the Openterface_QT root directory"
    echo "   Expected files: openterfaceQT.pro or CMakeLists.txt"
    echo ""
    echo "   Current directory: $(pwd)"
    echo "   Files found: $(ls -la | head -5)"
    exit 1
fi

# Detect if we're in a CI environment
if [ -n "$CI" ] || [ -n "$GITHUB_ACTIONS" ] || [ -n "$GITLAB_CI" ]; then
    echo "🤖 CI/CD environment detected"
    USE_SUDO=""
    INSTALL_DEPS=true
else
    echo "💻 Local development environment detected"
    USE_SUDO="sudo"
    INSTALL_DEPS=true
fi

echo "📦 Checking and installing build dependencies..."

# Check if we need to install dependencies
MISSING_DEPS=()

# Check for essential build tools
if ! command -v cmake &> /dev/null; then
    MISSING_DEPS+=("cmake")
fi

if ! command -v make &> /dev/null; then
    MISSING_DEPS+=("build-essential")
fi

if ! command -v pkg-config &> /dev/null; then
    MISSING_DEPS+=("pkg-config")
fi

# Check for Qt6 development files
if ! pkg-config --exists Qt6Core &> /dev/null; then
    MISSING_DEPS+=("qt6-base-dev" "qt6-multimedia-dev" "qt6-serialport-dev" "qt6-svg-dev" "qt6-tools-dev")
fi

# Check for FFmpeg development files
if ! pkg-config --exists libavformat &> /dev/null; then
    MISSING_DEPS+=("libavformat-dev" "libavcodec-dev" "libavutil-dev" "libswresample-dev" "libswscale-dev")
fi

# Check for other required libraries
if ! pkg-config --exists libusb-1.0 &> /dev/null; then
    MISSING_DEPS+=("libusb-1.0-0-dev")
fi

if ! pkg-config --exists libudev &> /dev/null; then
    MISSING_DEPS+=("libudev-dev")
fi

# Install missing dependencies if any
if [ ${#MISSING_DEPS[@]} -gt 0 ] && [ "$INSTALL_DEPS" = true ]; then
    echo "  Installing missing dependencies: ${MISSING_DEPS[*]}"
    if [ -n "$USE_SUDO" ]; then
        $USE_SUDO apt-get update -y --allow-releaseinfo-change --allow-unauthenticated || true
        $USE_SUDO apt-get install -y --allow-unauthenticated "${MISSING_DEPS[@]}"
    else
        apt-get update -y --allow-releaseinfo-change --allow-unauthenticated || true
        apt-get install -y --allow-unauthenticated "${MISSING_DEPS[@]}"
    fi
    echo "✅ Dependencies installed successfully"
elif [ ${#MISSING_DEPS[@]} -gt 0 ]; then
    echo "⚠️  Missing dependencies detected but installation skipped: ${MISSING_DEPS[*]}"
    echo "   Please install manually if build fails"
else
    echo "✅ All required dependencies are already installed"
fi

echo "🌐 Generating language files..."
if [ -x "/usr/lib/qt6/bin/lrelease" ]; then
    /usr/lib/qt6/bin/lrelease openterfaceQT.pro
    echo "✅ Language files generated successfully"
elif command -v lrelease &> /dev/null; then
    lrelease openterfaceQT.pro
    echo "✅ Language files generated successfully"
else
    echo "⚠️  lrelease not found, skipping language file generation..."
fi

echo "🏗️ Building project with CMake (shared libraries)..."
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Detect architecture and set Qt6 cmake path
ARCH=$(dpkg --print-architecture 2>/dev/null || echo "unknown")
UNAME_ARCH=$(uname -m)
echo "Detected architecture (dpkg): $ARCH"
echo "Detected architecture (uname): $UNAME_ARCH"

if [ "$ARCH" = "arm64" ] || [ "$ARCH" = "aarch64" ] || [ "$UNAME_ARCH" = "aarch64" ]; then
    QT_CMAKE_PATH="/usr/lib/aarch64-linux-gnu/cmake/Qt6"
    echo "Building for ARM64/aarch64 architecture"
else
    QT_CMAKE_PATH="/usr/lib/x86_64-linux-gnu/cmake/Qt6"
    echo "Building for x86_64 architecture"
fi

echo "Using Qt6 cmake from: $QT_CMAKE_PATH"

# For this build, we use static FFmpeg libraries but shared Qt/system libraries
echo "🔍 Configuring for static FFmpeg + shared Qt libraries..."

# Check if static FFmpeg libraries are available (from base image)
if [ -f "/usr/local/lib/libavformat.a" ]; then
    echo "  ✅ Using static FFmpeg libraries from /usr/local/lib/"
    echo "  Static FFmpeg libraries found:"
    ls -la /usr/local/lib/libav*.a | sed 's/^/    /'
    USE_STATIC_FFMPEG=true
    FFMPEG_STATIC_LIBS="/usr/local/lib/libavformat.a;/usr/local/lib/libavcodec.a;/usr/local/lib/libavutil.a;/usr/local/lib/libswresample.a;/usr/local/lib/libswscale.a"
    FFMPEG_INCLUDE_DIRS="/usr/local/include"
else
    echo "  ⚠️  Static FFmpeg libraries not found, falling back to shared libraries"
    # Check for shared FFmpeg libraries
    echo "  Checking FFmpeg shared libraries availability:"
    for lib in libavformat libavcodec libavutil libswresample libswscale; do
        if pkg-config --exists $lib; then
            VERSION=$(pkg-config --modversion $lib)
            echo "    ✅ $lib: $VERSION"
        else
            echo "    ❌ $lib: NOT FOUND"
            echo "    Please install ${lib}-dev package"
            exit 1
        fi
    done
    USE_STATIC_FFMPEG=false
fi

echo "  📦 Using shared Qt6 and system libraries"

# Configure CMake
echo "⚙️  Configuring CMake..."
if [ "$USE_STATIC_FFMPEG" = true ]; then
    cmake .. \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_PREFIX_PATH="$QT_CMAKE_PATH" \
        -DCMAKE_SYSTEM_PROCESSOR="$UNAME_ARCH" \
        -DUSE_FFMPEG_STATIC=ON \
        -DFFMPEG_LIBRARIES="$FFMPEG_STATIC_LIBS" \
        -DFFMPEG_INCLUDE_DIRS="$FFMPEG_INCLUDE_DIRS" \
        -DBUILD_SHARED_LIBS=OFF
else
    cmake .. \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_PREFIX_PATH="$QT_CMAKE_PATH" \
        -DCMAKE_SYSTEM_PROCESSOR="$UNAME_ARCH" \
        -DUSE_FFMPEG_STATIC=OFF \
        -DFFMPEG_LIBRARIES="" \
        -DFFMPEG_INCLUDE_DIRS="" \
        -DBUILD_SHARED_LIBS=ON
fi

echo "✅ CMake configuration completed"

# Clean previous build
echo "🧹 Cleaning previous build..."
make clean || true

echo "🔨 Compiling application..."
make -j$(nproc)

echo "✅ Build completed successfully!"

# Check the built binary
echo ""
echo "🔍 Verifying built binary..."
if [ -f "openterfaceQT" ]; then
    echo "  Binary location: $(pwd)/openterfaceQT"
    echo "  Binary size: $(du -h openterfaceQT | cut -f1)"
    
    # Check binary architecture
    echo "  Binary architecture information:"
    file openterfaceQT | sed 's/^/    /'
    
    # Check binary dependencies
    echo "  Shared library dependencies:"
    ldd openterfaceQT | grep -E "(Qt|libav|libsw)" | sed 's/^/    /' || echo "    (No Qt/FFmpeg dependencies found in ldd output)"
    
    # Extract version from binary if possible
    if [ -f "../resources/version.h" ]; then
        APP_VERSION=$(grep '#define APP_VERSION' ../resources/version.h | sed 's/.*"\(.*\)".*/\1/')
        if [ -n "$APP_VERSION" ]; then
            echo "  Application version: $APP_VERSION"
        fi
    fi
    
    echo ""
    echo "🚀 How to run the application:"
    echo "  Method 1 (Direct): $(pwd)/openterfaceQT"
    echo "  Method 2 (With env): cd $(pwd) && ./openterfaceQT"
    echo ""
    echo "🔧 If you encounter Qt plugin issues, try:"
    echo "  export QT_PLUGIN_PATH=/usr/lib/x86_64-linux-gnu/qt6/plugins"
    echo "  export QT_QPA_PLATFORM=xcb"
    echo "  $(pwd)/openterfaceQT"
    
else
    echo "❌ Binary not found after build!"
    echo "  Expected location: $(pwd)/openterfaceQT"
    echo "  Build may have failed. Check the output above for errors."
    exit 1
fi

echo ""
echo "📋 Build Summary:"
if [ "$USE_STATIC_FFMPEG" = true ]; then
    echo "  Build type: Release (static FFmpeg + shared Qt)"
    echo "  FFmpeg: Static libraries (/usr/local/lib/)"
else
    echo "  Build type: Release (shared libraries)"
    echo "  FFmpeg: Shared libraries (system packages)"
fi
echo "  Qt Libraries: Shared libraries (system packages)"
echo "  Architecture: $UNAME_ARCH"
echo "  Build directory: $(pwd)"
echo "  Binary: $(pwd)/openterfaceQT"
echo "  Status: ✅ Ready to run"
echo ""
echo "💡 Note: This hybrid build optimizes for FFmpeg portability while keeping Qt shared."
echo "   For CI/CD: Binary is ready for packaging or testing."
echo "   For local use: Use the install-linux.sh script for system-wide installation."
