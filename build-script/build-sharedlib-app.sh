#!/bin/bash
# =============================================================================
# Openterface QT Linux Build Script (Shared Libraries)
# =============================================================================
#
# This script builds the Openterface QT application using shared libraries
# without installing it system-wide. The built binary will be available in
# the build directory for testing and development purposes.
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

echo "🏗️  Openterface QT Build Script (Shared Libraries)"
echo "=================================================="
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

# For shared library build, we use system-installed shared FFmpeg libraries
echo "🔍 Configuring for shared FFmpeg libraries..."
echo "  Using system FFmpeg shared libraries (libavformat.so, libavcodec.so, etc.)"

# Configure CMake for shared library build
echo "⚙️  Configuring CMake..."
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$QT_CMAKE_PATH" \
    -DCMAKE_SYSTEM_PROCESSOR="$UNAME_ARCH" \
    -DBUILD_SHARED_LIBS=ON

echo "✅ CMake configuration completed"

# Clean previous build
echo "🧹 Cleaning previous build..."
make clean || true

# Determine number of CPUs to use for make
if [[ "$ARCH" == "arm64" || "$ARCH" == "aarch64" || "$UNAME_ARCH" == "aarch64" ]]; then
    MAKE_JOBS=2
    echo "Using $MAKE_JOBS parallel jobs for ARM64 build"
else
    CPU_COUNT=$(nproc)
    if [ "$CPU_COUNT" -gt 1 ]; then
        MAKE_JOBS=$((CPU_COUNT - 1))
    else
        MAKE_JOBS=1
    fi
    echo "Using $MAKE_JOBS parallel jobs (detected $CPU_COUNT CPUs)"
fi

echo "🔨 Compiling application..."
make -j$MAKE_JOBS

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
echo "  Build type: Release (shared libraries)"
echo "  Architecture: $UNAME_ARCH"
echo "  Build directory: $(pwd)"
echo "  Binary: $(pwd)/openterfaceQT"
echo "  Status: ✅ Ready to run"
echo ""
echo "💡 Note: This build uses shared libraries and is not installed system-wide."
echo "   For CI/CD: Binary is ready for packaging or testing."
echo "   For local use: Use the install-linux.sh script for system-wide installation."
