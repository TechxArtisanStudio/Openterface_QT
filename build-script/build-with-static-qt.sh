#!/bin/bash
set -e

# Build script for OpenTerface QT using static Qt installation
# This script configures and builds the application with proper library linking

# Configuration
QT_DIR="/opt/Qt6-arm64"
PROJECT_DIR="/home/pi/project/Openterface_QT"
BUILD_DIR="$PROJECT_DIR/build"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

echo_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

echo_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

echo_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Function to check if a command exists
command_exists() {
    command -v "$1" >/dev/null 2>&1
}

# Check prerequisites
echo_info "Checking prerequisites..."

if [ ! -d "$QT_DIR" ]; then
    echo_error "Qt installation not found at $QT_DIR"
    echo_error "Please run the Qt build script first"
    exit 1
fi

if ! command_exists cmake; then
    echo_error "cmake not found. Please install cmake"
    exit 1
fi

if ! command_exists make; then
    echo_error "make not found. Please install build-essential"
    exit 1
fi

if ! command_exists pkg-config; then
    echo_error "pkg-config not found. Please install pkg-config"
    exit 1
fi

echo_success "Prerequisites check passed"

# Set environment variables
echo_info "Setting up environment..."
export CMAKE_PREFIX_PATH="$QT_DIR:$CMAKE_PREFIX_PATH"
export PKG_CONFIG_PATH="$QT_DIR/lib/pkgconfig:/usr/lib/aarch64-linux-gnu/pkgconfig:/usr/lib/pkgconfig:/usr/share/pkgconfig:$PKG_CONFIG_PATH"
export LD_LIBRARY_PATH="$QT_DIR/lib:$LD_LIBRARY_PATH"
export PATH="$QT_DIR/bin:$PATH"

# Check if system has required development packages
echo_info "Checking system dependencies..."
MISSING_DEPS=()

# Check for essential development libraries
if ! pkg-config --exists orc-0.4; then
    MISSING_DEPS+=("liborc-0.4-dev")
fi

if ! pkg-config --exists gstreamer-1.0; then
    MISSING_DEPS+=("libgstreamer1.0-dev")
fi

if ! pkg-config --exists gstreamer-video-1.0; then
    MISSING_DEPS+=("libgstreamer-plugins-base1.0-dev")
fi

if ! pkg-config --exists xrender; then
    MISSING_DEPS+=("libxrender-dev")
fi

if ! pkg-config --exists fontconfig; then
    MISSING_DEPS+=("libfontconfig1-dev")
fi

if ! pkg-config --exists freetype2; then
    MISSING_DEPS+=("libfreetype6-dev")
fi

if ! pkg-config --exists expat; then
    MISSING_DEPS+=("libexpat1-dev")
fi

if [ ${#MISSING_DEPS[@]} -gt 0 ]; then
    echo_warning "Missing development packages detected:"
    for dep in "${MISSING_DEPS[@]}"; do
        echo "  - $dep"
    done
    echo_info "Installing missing dependencies..."
    sudo apt update
    sudo apt install -y "${MISSING_DEPS[@]}"
fi

# Verify Qt installation
echo_info "Verifying Qt installation..."
if [ ! -f "$QT_DIR/bin/qmake" ]; then
    echo_error "qmake not found in Qt installation"
    exit 1
fi

QT_VERSION=$("$QT_DIR/bin/qmake" -query QT_VERSION)
echo_success "Found Qt version: $QT_VERSION"

# Create and clean build directory
echo_info "Preparing build directory..."
cd "$PROJECT_DIR"

if [ -d "$BUILD_DIR" ]; then
    echo_warning "Removing existing build directory..."
    rm -rf "$BUILD_DIR"
fi

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Gather library information
echo_info "Gathering library information..."

# Get ORC libraries - force static linking if available
ORC_LIBS=""
ORC_STATIC_LIB="/usr/lib/aarch64-linux-gnu/liborc-0.4.a"
if [ -f "$ORC_STATIC_LIB" ]; then
    echo_info "Found static ORC library: $ORC_STATIC_LIB"
    ORC_LIBS="$ORC_STATIC_LIB"
elif pkg-config --exists orc-0.4; then
    ORC_LIBS=$(pkg-config --libs orc-0.4)
    echo_info "Using dynamic ORC libraries: $ORC_LIBS"
else
    echo_warning "ORC library not found - this may cause GStreamer linking issues"
fi

# Get GStreamer libraries
GSTREAMER_LIBS=""
if pkg-config --exists gstreamer-1.0 gstreamer-video-1.0; then
    GSTREAMER_LIBS=$(pkg-config --libs gstreamer-1.0 gstreamer-video-1.0)
    echo_info "GStreamer libraries: $GSTREAMER_LIBS"
fi

# Configure additional linker flags
EXTRA_LINK_FLAGS="$ORC_LIBS $GSTREAMER_LIBS -lpthread -ldl -lm -lz -lbz2 -lglib-2.0 -lgobject-2.0"

echo_info "Extra link flags: $EXTRA_LINK_FLAGS"

# Configure with CMake
echo_info "Configuring project with CMake..."
cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$QT_DIR" \
    -DUSE_FFMPEG_STATIC=ON \
    -DCMAKE_EXE_LINKER_FLAGS="$EXTRA_LINK_FLAGS" \
    -DCMAKE_SHARED_LINKER_FLAGS="$EXTRA_LINK_FLAGS" \
    ..

if [ $? -ne 0 ]; then
    echo_error "CMake configuration failed"
    exit 1
fi

echo_success "CMake configuration completed"

# Build the project
echo_info "Building project..."
NUM_CORES=$(nproc)
echo_info "Building with $NUM_CORES cores..."

make -j"$NUM_CORES"

if [ $? -ne 0 ]; then
    echo_error "Build failed"
    echo_info "Trying single-threaded build for better error visibility..."
    make
    if [ $? -ne 0 ]; then
        echo_error "Single-threaded build also failed"
        exit 1
    fi
fi

echo_success "Build completed successfully!"

# Check if executable was created
if [ -f "openterfaceQT" ]; then
    echo_success "Executable 'openterfaceQT' created successfully"
    echo_info "Location: $BUILD_DIR/openterfaceQT"
    
    # Show executable information
    echo_info "Executable information:"
    file openterfaceQT
    echo_info "Size: $(du -h openterfaceQT | cut -f1)"
    
    # Check dependencies
    echo_info "Checking dynamic dependencies:"
    ldd openterfaceQT | head -20
    
    echo_success "Build process completed successfully!"
    echo_info "You can run the application with: $BUILD_DIR/openterfaceQT"
else
    echo_error "Executable not found after build"
    exit 1
fi
