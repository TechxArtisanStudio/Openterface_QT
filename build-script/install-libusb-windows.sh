#!/bin/bash
# ============================================================================
# LibUSB Shared Library Build and Install Script for Windows (MSYS2 MinGW)
# This script builds and installs LibUSB shared library.
# ============================================================================

set -euo pipefail
set -x  # Verbose execution for CI logs
trap 'echo "LibUSB build failed - last 200 lines of logs:"; tail -n 200 "${BUILD_DIR}/libusb-build.log" || true; tail -n 200 "${BUILD_DIR}/libusb-configure.log" || true; exit 1' ERR

# Get variables from environment (passed from .bat)
LIBUSB_VERSION="${LIBUSB_VERSION:-1.0.27}"
LIBUSB_INSTALL_PREFIX="${LIBUSB_INSTALL_PREFIX:-/c/libusb}"
BUILD_DIR="${BUILD_DIR:-$(pwd)/libusb-build-temp}"
NUM_CORES="${NUM_CORES:-$(nproc)}"
DOWNLOAD_URL="${DOWNLOAD_URL:-https://github.com/libusb/libusb/releases/download/v${LIBUSB_VERSION}/libusb-${LIBUSB_VERSION}.tar.bz2}"

echo "LibUSB Version: $LIBUSB_VERSION"
echo "Install Prefix: $LIBUSB_INSTALL_PREFIX"
echo "Build Directory: $BUILD_DIR"
echo "CPU Cores: $NUM_CORES"
echo "Download URL: $DOWNLOAD_URL"

# Create install directory if it doesn't exist
mkdir -p "$LIBUSB_INSTALL_PREFIX"
# Write a short marker to the Windows filesystem so we can confirm the script started when run under MSYS
# (use || true to avoid failing the script if /c is not writable)
echo "LIBUSB_BUILD_SCRIPT_STARTED: $(date)" > /c/libusb_build_started.txt || true

# Download LibUSB source
echo "Downloading LibUSB $LIBUSB_VERSION..."
if [ ! -f "libusb-${LIBUSB_VERSION}.tar.bz2" ]; then
    curl -L -o "libusb-${LIBUSB_VERSION}.tar.bz2" "$DOWNLOAD_URL"
else
    echo "LibUSB source already downloaded."
fi

# Extract source
echo "Extracting LibUSB source..."
if [ ! -d "libusb-${LIBUSB_VERSION}" ]; then
    tar -xjf "libusb-${LIBUSB_VERSION}.tar.bz2"
else
    echo "LibUSB source already extracted."
fi

# Enter source directory
cd "libusb-${LIBUSB_VERSION}"

# Clean previous build if exists
if [ -f Makefile ]; then
    echo "Cleaning previous build..."
    make distclean
fi

# Set libtool environment to avoid rpath issues
export lt_cv_deplibs_check_method=pass_all
export lt_cv_shlibpath_overrides_runpath=no
export lt_cv_sys_lib_dlsearch_path_spec=""
export lt_cv_sys_lib_search_path_spec=""

# Configure with autotools
echo "Configuring LibUSB with autotools (shared build)..."
CONFIG_LOG="${BUILD_DIR}/libusb-configure.log"
mkdir -p "${BUILD_DIR}"
# Build shared library so we get libusb DLLs for runtime
./configure --host=x86_64-w64-mingw32 --prefix="$LIBUSB_INSTALL_PREFIX" --enable-shared --disable-static 2>&1 | tee "${CONFIG_LOG}"
if [ ${PIPESTATUS[0]} -ne 0 ]; then
    echo "✗ LibUSB configure failed! See ${CONFIG_LOG}"
    tail -n 200 "${CONFIG_LOG}" || true
    exit 1
fi

# Build
echo "Building LibUSB..."
BUILD_LOG="${BUILD_DIR}/libusb-build.log"
make -j"$NUM_CORES" 2>&1 | tee "${BUILD_LOG}"
if [ ${PIPESTATUS[0]} -ne 0 ]; then
    echo "✗ LibUSB build failed! See ${BUILD_LOG}"
    tail -n 200 "${BUILD_LOG}" || true
    exit 1
fi

# Install
echo "Installing LibUSB..."
make install

echo "Installed files in prefix bin and lib (for CI diagnostics):"
ls -la "${LIBUSB_INSTALL_PREFIX}/bin" || true
ls -la "${LIBUSB_INSTALL_PREFIX}/lib" || true

echo "LibUSB build and install completed successfully!"
echo "Installed to: $LIBUSB_INSTALL_PREFIX"