#!/bin/bash

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SOURCE_DIR="$(dirname "$SCRIPT_DIR")"
XCB_INSTALL_PATH="${SOURCE_DIR}/qt-build/xcb-install"

# Define versions
XCB_PROTO_VERSION=1.16.0
XCB_VERSION=1.16
XCB_UTIL_VERSION=0.4.1
XCB_UTIL_IMAGE_VERSION=0.4.1
XCB_UTIL_KEYSYMS_VERSION=0.4.1
XCB_UTIL_RENDERUTIL_VERSION=0.3.10
XCB_UTIL_WM_VERSION=0.4.2

# Check if already built
if [ "$1" == "--no-build" ] && [ -d "${XCB_INSTALL_PATH}" ] && [ -f "${XCB_INSTALL_PATH}/lib/libxcb-util.a" ]; then
    echo "XCB utilities already built, skipping"
    exit 0
fi

# Create build directories
mkdir -p "${SOURCE_DIR}/qt-build"
cd "${SOURCE_DIR}/qt-build"

# Install build dependencies
sudo apt-get update
sudo apt-get install -y \
    xorg-dev \
    libxcb-*-dev \
    xutils-dev \
    autoconf \
    libtool \
    automake \
    pkg-config

# Create installation directory
mkdir -p "${XCB_INSTALL_PATH}"

# Set and export PKG_CONFIG_PATH to include both lib and share pkgconfig paths
export PKG_CONFIG_PATH="${XCB_INSTALL_PATH}/lib/pkgconfig:${XCB_INSTALL_PATH}/share/pkgconfig:${PKG_CONFIG_PATH}"
echo "Using PKG_CONFIG_PATH: ${PKG_CONFIG_PATH}"

# Download and build xcb-proto
if [ ! -d xcb-proto ]; then
    echo "Downloading xcb-proto ${XCB_PROTO_VERSION}..."
    curl -L -o xcb-proto.tar.xz "https://xorg.freedesktop.org/archive/individual/proto/xcb-proto-${XCB_PROTO_VERSION}.tar.xz"
    tar xf xcb-proto.tar.xz
    mv "xcb-proto-${XCB_PROTO_VERSION}" xcb-proto
    rm xcb-proto.tar.xz
fi

cd xcb-proto
./configure --prefix="${XCB_INSTALL_PATH}"
make -j$(nproc)
make install
cd ..

# Verify xcb-proto was installed correctly
if [ ! -f "${XCB_INSTALL_PATH}/share/pkgconfig/xcb-proto.pc" ]; then
    echo "Error: xcb-proto.pc not found in expected location!"
    echo "Expected: ${XCB_INSTALL_PATH}/share/pkgconfig/xcb-proto.pc"
    exit 1
fi

echo "xcb-proto.pc location: $(find ${XCB_INSTALL_PATH} -name xcb-proto.pc)"

# Verify xcb-proto version
XCBPROTO_VERSION=$(pkg-config --modversion xcb-proto 2>/dev/null || echo "unknown")
echo "Installed xcb-proto version: ${XCBPROTO_VERSION}"
if [[ "${XCBPROTO_VERSION}" == "unknown" ]]; then
    echo "Warning: xcb-proto not found via pkg-config after installation."
    echo "Current PKG_CONFIG_PATH: ${PKG_CONFIG_PATH}"
    # Try direct approach if pkg-config fails
    DIRECT_VERSION=$(grep -o "Version: [0-9.]*" "${XCB_INSTALL_PATH}/share/pkgconfig/xcb-proto.pc" | cut -d' ' -f2)
    echo "Direct version check from .pc file: ${DIRECT_VERSION}"
    # Continue anyway since we verified the file exists
    echo "Continuing build despite pkg-config issue..."
fi

# Download and build libxcb
if [ ! -d libxcb ]; then
    echo "Downloading libxcb ${XCB_VERSION}..."
    curl -L -o libxcb.tar.xz "https://xorg.freedesktop.org/archive/individual/lib/libxcb-${XCB_VERSION}.tar.xz"
    tar xf libxcb.tar.xz
    mv "libxcb-${XCB_VERSION}" libxcb
    rm libxcb.tar.xz
fi

cd libxcb
PYTHONPATH="${XCB_INSTALL_PATH}/lib/python3.10/site-packages:${PYTHONPATH}" \
XCBPROTO_CFLAGS="-I${XCB_INSTALL_PATH}/include" \
XCBPROTO_LIBS="-L${XCB_INSTALL_PATH}/lib" \
./configure --prefix="${XCB_INSTALL_PATH}" --enable-static --disable-shared
make -j$(nproc)
make install
cd ..

# Build xcb-util
if [ ! -d xcb-util ]; then
    echo "Downloading xcb-util ${XCB_UTIL_VERSION}..."
    curl -L -o xcb-util.tar.gz "https://xcb.freedesktop.org/dist/xcb-util-${XCB_UTIL_VERSION}.tar.gz"
    tar xf xcb-util.tar.gz
    mv "xcb-util-${XCB_UTIL_VERSION}" xcb-util
    rm xcb-util.tar.gz
fi

cd xcb-util
./configure --prefix="${XCB_INSTALL_PATH}" --enable-static --disable-shared
make -j$(nproc)
make install
cd ..

# Build xcb-util-image
if [ ! -d xcb-util-image ]; then
    echo "Downloading xcb-util-image ${XCB_UTIL_IMAGE_VERSION}..."
    curl -L -o xcb-util-image.tar.gz "https://xcb.freedesktop.org/dist/xcb-util-image-${XCB_UTIL_IMAGE_VERSION}.tar.gz"
    tar xf xcb-util-image.tar.gz
    mv "xcb-util-image-${XCB_UTIL_IMAGE_VERSION}" xcb-util-image
    rm xcb-util-image.tar.gz
fi

cd xcb-util-image
./configure --prefix="${XCB_INSTALL_PATH}" --enable-static --disable-shared
make -j$(nproc)
make install
cd ..

# Build xcb-util-keysyms
if [ ! -d xcb-util-keysyms ]; then
    echo "Downloading xcb-util-keysyms ${XCB_UTIL_KEYSYMS_VERSION}..."
    curl -L -o xcb-util-keysyms.tar.gz "https://xcb.freedesktop.org/dist/xcb-util-keysyms-${XCB_UTIL_KEYSYMS_VERSION}.tar.gz"
    tar xf xcb-util-keysyms.tar.gz
    mv "xcb-util-keysyms-${XCB_UTIL_KEYSYMS_VERSION}" xcb-util-keysyms
    rm xcb-util-keysyms.tar.gz
fi

cd xcb-util-keysyms
./configure --prefix="${XCB_INSTALL_PATH}" --enable-static --disable-shared
make -j$(nproc)
make install
cd ..

# Build xcb-util-renderutil
if [ ! -d xcb-util-renderutil ]; then
    echo "Downloading xcb-util-renderutil ${XCB_UTIL_RENDERUTIL_VERSION}..."
    curl -L -o xcb-util-renderutil.tar.gz "https://xcb.freedesktop.org/dist/xcb-util-renderutil-${XCB_UTIL_RENDERUTIL_VERSION}.tar.gz"
    tar xf xcb-util-renderutil.tar.gz
    mv "xcb-util-renderutil-${XCB_UTIL_RENDERUTIL_VERSION}" xcb-util-renderutil
    rm xcb-util-renderutil.tar.gz
fi

cd xcb-util-renderutil
./configure --prefix="${XCB_INSTALL_PATH}" --enable-static --disable-shared
make -j$(nproc)
make install
cd ..

# Build xcb-util-wm
if [ ! -d xcb-util-wm ]; then
    echo "Downloading xcb-util-wm ${XCB_UTIL_WM_VERSION}..."
    curl -L -o xcb-util-wm.tar.gz "https://xcb.freedesktop.org/dist/xcb-util-wm-${XCB_UTIL_WM_VERSION}.tar.gz"
    tar xf xcb-util-wm.tar.gz
    mv "xcb-util-wm-${XCB_UTIL_WM_VERSION}" xcb-util-wm
    rm xcb-util-wm.tar.gz
fi

cd xcb-util-wm
./configure --prefix="${XCB_INSTALL_PATH}" --enable-static --disable-shared
make -j$(nproc)
make install
cd ..

# Create summary report
echo "XCB static libraries built successfully at ${XCB_INSTALL_PATH}"
echo "Available libraries:"
find "${XCB_INSTALL_PATH}/lib" -name "*.a" | sort

# Make script executable
chmod +x "${SCRIPT_DIR}/build-static-xcb-utils.sh"
