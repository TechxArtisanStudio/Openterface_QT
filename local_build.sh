#!/bin/bash

# Simple build script using the specific Docker image
# ghcr.io/techxartisanstudio/o    if ! rm -rf "$BUILD_DIR" 2>/dev/null; then
        # If that fails due to permissions, use Docker to clean it
        log_info "Using Docker to clean build directory due to permission issues..."
        docker run --rm \
            -v "$PROJECT_ROOT:/workspace" \
            -w /workspace \
            --user root \
            "busybox" \
            sh -c "rm -rf build-simple-static" || true
    fi-qtbuild-complete

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$SCRIPT_DIR"
BUILD_TYPE="${1:-static}"  # Default to static since we're using the complete image
DOCKER_IMAGE="ghcr.io/techxartisanstudio/openterface-qtbuild-complete"

# Colors
GREEN='\033[0;32m'
BLUE='\033[0;34m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m'

log_info() { echo -e "${BLUE}[INFO]${NC} $1"; }
log_success() { echo -e "${GREEN}[SUCCESS]${NC} $1"; }
log_error() { echo -e "${RED}[ERROR]${NC} $1"; }
log_warning() { echo -e "${YELLOW}[WARNING]${NC} $1"; }

if [[ "$1" == "-h" || "$1" == "--help" ]]; then
    cat << EOF
Simple Build Script for OpenterfaceQT

Usage: $0 [shared|static]

Arguments:
  shared|static     Build type (default: static)

Examples:
  $0               # Build static version (recommended)
  $0 static        # Build static version
  $0 shared        # Build shared version

This script uses the Docker image:
  ghcr.io/techxartisanstudio/openterface-qtbuild-complete

EOF
    exit 0
fi

# Validate inputs
if [[ ! "$BUILD_TYPE" =~ ^(shared|static)$ ]]; then
    log_error "Invalid build type: $BUILD_TYPE. Must be 'shared' or 'static'"
    exit 1
fi

# Get version
VERSION=$(grep -oP '#define APP_VERSION "\K[^"]+' "$PROJECT_ROOT/resources/version.h" 2>/dev/null || echo "unknown")
log_info "Version: $VERSION"
log_info "Build type: $BUILD_TYPE"
log_info "Docker image: $DOCKER_IMAGE"

# Check if Docker is available
if ! command -v docker &> /dev/null; then
    log_error "Docker is not installed or not in PATH"
    exit 1
fi

if ! docker info &> /dev/null; then
    log_error "Docker is not running or not accessible"
    exit 1
fi

# Check if image exists
log_info "Checking Docker image availability..."

# First try to find the image locally
LOCAL_IMAGE=$(docker images --format "table {{.Repository}}:{{.Tag}}" | grep "ghcr.io/techxartisanstudio/openterface-qtbuild-complete" | head -1 | tr -d ' ')

if [[ -n "$LOCAL_IMAGE" ]]; then
    log_info "Found local image: $LOCAL_IMAGE"
    DOCKER_IMAGE="$LOCAL_IMAGE"
elif docker manifest inspect "$IMAGE_NAME" &> /dev/null; then
    log_info "Image found in registry: $IMAGE_NAME"
    DOCKER_IMAGE="$IMAGE_NAME"
else
    log_error "Docker image not found: $IMAGE_NAME"
    log_error "Available images:"
    docker images | grep openterface-qtbuild || echo "No openterface-qtbuild images found"
    exit 1
fi

log_info "Using Docker image: $DOCKER_IMAGE"

# Prepare build directory
BUILD_DIR="$PROJECT_ROOT/build"
log_info "Preparing build directory: $BUILD_DIR"

# Clean build directory (handle permission issues from Docker)
if [[ -d "$BUILD_DIR" ]]; then
    log_info "Cleaning existing build directory..."
    # Try normal removal first
    if ! rm -rf "$BUILD_DIR" 2>/dev/null; then
        # If that fails due to permissions, use sudo
        log_info "Using sudo to clean build directory due to permission issues..."
        sudo rm -rf "$BUILD_DIR" || true
    fi
fi

mkdir -p "$BUILD_DIR"
mkdir -p "$BUILD_DIR/config/languages"
mkdir -p "$BUILD_DIR/config/keyboards"

log_info "Preparing build directory: $BUILD_DIR"

# Create build script
if [[ "$BUILD_TYPE" == "shared" ]]; then
    log_info "Creating shared build script..."
    cat > /tmp/simple-build.sh << 'EOF'
#!/bin/bash
set -e

echo "=== OpenterfaceQT Shared Build ==="
echo "Working directory: $(pwd)"
echo "Build directory: /workspace/build"
echo "Source directory: /workspace/src"

cd /workspace/src

# Process translations if tools are available
echo "Processing translations..."
if [ -f "/opt/Qt6/bin/lupdate" ]; then
    echo "Running lupdate..."
    /opt/Qt6/bin/lupdate openterfaceQT.pro -no-obsolete || echo "lupdate failed, continuing..."
else
    echo "lupdate not found, skipping..."
fi

if [ -f "/opt/Qt6/bin/lrelease" ]; then
    echo "Running lrelease..."
    /opt/Qt6/bin/lrelease openterfaceQT.pro || echo "lrelease failed, continuing..."
else
    echo "lrelease not found, skipping..."
fi

# Copy configuration files
echo "Copying configuration files..."
cp -r config/keyboards/*.json /workspace/build/config/keyboards/ 2>/dev/null || echo "No keyboard configs to copy"
cp -r config/languages/*.qm /workspace/build/config/languages/ 2>/dev/null || echo "No language files to copy"

# Configure and build
cd /workspace/build
echo "Configuring CMake for shared build..."
cmake -DCMAKE_BUILD_TYPE=Release \
      -DOPENTERFACE_BUILD_STATIC=OFF \
      -DCMAKE_VERBOSE_MAKEFILE=ON \
      /workspace/src

echo "Building with make..."
make -j$(nproc) VERBOSE=1

echo "=== Shared Build Completed ==="
ls -la openterfaceQT || echo "Binary not found!"
EOF
else
    log_info "Creating static build script..."
    cat > /tmp/simple-build.sh << 'EOF'
#!/bin/bash
set -e

echo "=== OpenterfaceQT Static Build ==="
echo "Working directory: $(pwd)"
echo "Build directory: /workspace/build"
echo "Source directory: /workspace/src"

# Install additional dependencies for static linking
echo "Installing additional dependencies..."
apt-get update -y
apt-get install -y \
    libgudev-1.0-dev \
    libv4l-dev \
    libzstd-dev \
    liblzma-dev \
    libbz2-dev \
    libvdpau-dev \
    libva-dev \
    va-driver-all \
    vainfo || echo "Some packages failed to install, continuing..."

cd /workspace/src

# Verify Qt6 static installation
if [ ! -d '/opt/Qt6' ]; then
    echo "ERROR: Qt6 static installation not found at /opt/Qt6"
    exit 1
fi
echo "Qt6 static installation found at /opt/Qt6"

# Process translations if tools are available
echo "Processing translations..."
if [ -f "/opt/Qt6/bin/lupdate" ]; then
    echo "Running lupdate..."
    /opt/Qt6/bin/lupdate openterfaceQT.pro -no-obsolete || echo "lupdate failed, continuing..."
else
    echo "lupdate not found, skipping..."
fi

if [ -f "/opt/Qt6/bin/lrelease" ]; then
    echo "Running lrelease..."
    /opt/Qt6/bin/lrelease openterfaceQT.pro || echo "lrelease failed, continuing..."
else
    echo "lrelease not found, skipping..."
fi

# Copy configuration files
echo "Copying configuration files..."
cp -r config/keyboards/*.json /workspace/build/config/keyboards/ 2>/dev/null || echo "No keyboard configs to copy"
cp -r config/languages/*.qm /workspace/build/config/languages/ 2>/dev/null || echo "No language files to copy"

# Set environment variables for static linking
export PKG_CONFIG_PATH="/opt/Qt6/lib/pkgconfig:/usr/local/lib/pkgconfig:$PKG_CONFIG_PATH"
export CMAKE_PREFIX_PATH="/opt/Qt6:/usr/local:/opt/ffmpeg:/opt/gstreamer:/usr"

# Configure and build
cd /workspace/build
echo "Configuring CMake for static build..."
echo "PKG_CONFIG_PATH: $PKG_CONFIG_PATH"
echo "CMAKE_PREFIX_PATH: $CMAKE_PREFIX_PATH"

cmake -DCMAKE_PREFIX_PATH=${CMAKE_PREFIX_PATH} \
      -DCMAKE_BUILD_TYPE=Release \
      -DOPENTERFACE_BUILD_STATIC=ON \
      -DUSE_FFMPEG_STATIC=ON \
      -DFFMPEG_PREFIX="/opt/ffmpeg" \
      -DUSE_GSTREAMER_STATIC_PLUGINS=ON \
      -DGSTREAMER_PREFIX="/opt/gstreamer" \
      -DCMAKE_LIBRARY_PATH="/opt/Qt6/lib:/usr/local/lib:/opt/ffmpeg/lib:/usr/lib/x86_64-linux-gnu" \
      -DCMAKE_INCLUDE_PATH="/opt/Qt6/include:/usr/local/include:/opt/ffmpeg/include:/usr/include" \
      -DCMAKE_VERBOSE_MAKEFILE=ON \
      /workspace/src

echo "Building with make..."
make -j$(nproc) VERBOSE=1

echo "=== Static Build Completed ==="
ls -la openterfaceQT || echo "Binary not found!"

# Show binary info
if [ -f "openterfaceQT" ]; then
    echo "Binary information:"
    file openterfaceQT
    ldd openterfaceQT 2>/dev/null || echo "Static binary - no dynamic dependencies"
fi
EOF
fi

chmod +x /tmp/simple-build.sh

# Run build
log_info "Starting build in Docker container..."
echo

docker run --rm \
    -v "$PROJECT_ROOT:/workspace/src" \
    -v "$BUILD_DIR:/workspace/build" \
    -v "/tmp/simple-build.sh:/build.sh" \
    -w /workspace/src \
    $([ "$BUILD_TYPE" == "static" ] && echo "--user root" || echo "") \
    "$DOCKER_IMAGE" \
    /build.sh

echo

# Check result
if [[ -f "$BUILD_DIR/openterfaceQT" ]]; then
    log_success "Build completed successfully!"
    echo
    log_info "Binary location: $BUILD_DIR/openterfaceQT"
    
    # Show binary info
    echo
    log_info "Binary information:"
    echo "  Size: $(ls -lh "$BUILD_DIR/openterfaceQT" | awk '{print $5}')"
    echo "  Type: $(file "$BUILD_DIR/openterfaceQT" | cut -d: -f2-)"
    
    # Check if it's static or dynamic
    if ldd "$BUILD_DIR/openterfaceQT" &>/dev/null; then
        log_info "Dynamic dependencies found:"
        ldd "$BUILD_DIR/openterfaceQT" | head -10
        if [[ $(ldd "$BUILD_DIR/openterfaceQT" | wc -l) -gt 10 ]]; then
            echo "  ... and $(($(ldd "$BUILD_DIR/openterfaceQT" | wc -l) - 10)) more"
        fi
    else
        log_success "Binary appears to be statically linked (no dynamic dependencies)"
    fi
    
    # Test basic functionality
    echo
    log_info "Testing binary functionality..."
    if timeout 5 "$BUILD_DIR/openterfaceQT" --help &>/dev/null || timeout 5 "$BUILD_DIR/openterfaceQT" -h &>/dev/null; then
        log_success "Binary responds to --help (basic functionality test passed)"
    else
        log_warning "Binary doesn't respond to --help (may need display/dependencies for full functionality)"
    fi
    
else
    log_error "Build failed - binary not found"
    echo
    log_info "Build directory contents:"
    ls -la "$BUILD_DIR/" || true
    exit 1
fi

# Cleanup
rm -f /tmp/simple-build.sh

echo
log_success "Simple build completed!"
log_info "You can now run: $BUILD_DIR/openterfaceQT"

if [[ "$BUILD_TYPE" == "static" ]]; then
    echo
    log_info "Static binary can be distributed standalone (no Qt dependencies needed)"
else
    echo
    log_info "Shared binary requires Qt6 and other libraries to be installed on target system"
fi
