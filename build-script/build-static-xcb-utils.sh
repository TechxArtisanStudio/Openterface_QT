#!/bin/bash

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SOURCE_DIR="$(dirname "$SCRIPT_DIR")"
XCB_INSTALL_PATH="${SOURCE_DIR}/qt-build/xcb-install"

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

# Set and export PKG_CONFIG_PATH to include our custom install path
export PKG_CONFIG_PATH="${XCB_INSTALL_PATH}/lib/pkgconfig:${PKG_CONFIG_PATH}"
echo "Using PKG_CONFIG_PATH: ${PKG_CONFIG_PATH}"

# Download and build xcb-proto (specific version to ensure 1.17.0+)
if [ ! -d xcb-proto ]; then
    git clone https://gitlab.freedesktop.org/xorg/proto/xcbproto.git xcb-proto
    cd xcb-proto
    # Checkout version 1.17.0 or newer
    git checkout 1.17.0 || git checkout master
else
    cd xcb-proto
    git fetch
    git checkout 1.17.0 || git checkout master
fi

./autogen.sh --prefix="${XCB_INSTALL_PATH}"
make -j$(nproc)
make install
cd ..

# Verify xcb-proto version
XCBPROTO_VERSION=$(pkg-config --modversion xcb-proto 2>/dev/null || echo "unknown")
echo "Installed xcb-proto version: ${XCBPROTO_VERSION}"
if [[ "${XCBPROTO_VERSION}" == "unknown" ]]; then
    echo "Error: xcb-proto not found in pkg-config path after installation."
    echo "Current PKG_CONFIG_PATH: ${PKG_CONFIG_PATH}"
    exit 1
fi

# Download and build libxcb
if [ ! -d libxcb ]; then
    git clone --depth 1 https://gitlab.freedesktop.org/xorg/lib/libxcb.git
fi
cd libxcb
XCBPROTO_CFLAGS="-I${XCB_INSTALL_PATH}/include" \
XCBPROTO_LIBS="-L${XCB_INSTALL_PATH}/lib" \
./autogen.sh --prefix="${XCB_INSTALL_PATH}" --enable-static --disable-shared
make -j$(nproc)
make install
cd ..

# Build xcb-util
if [ ! -d xcb-util ]; then
    git clone --depth 1 https://gitlab.freedesktop.org/xorg/lib/libxcb-util.git xcb-util
fi
cd xcb-util
./autogen.sh --prefix="${XCB_INSTALL_PATH}" --enable-static --disable-shared
make -j$(nproc)
make install
cd ..

# Build xcb-util-image
if [ ! -d xcb-util-image ]; then
    git clone --depth 1 https://gitlab.freedesktop.org/xorg/lib/libxcb-image.git xcb-util-image
fi
cd xcb-util-image
./autogen.sh --prefix="${XCB_INSTALL_PATH}" --enable-static --disable-shared
make -j$(nproc)
make install
cd ..

# Build xcb-util-keysyms
if [ ! -d xcb-util-keysyms ]; then
    git clone --depth 1 https://gitlab.freedesktop.org/xorg/lib/libxcb-keysyms.git xcb-util-keysyms
fi
cd xcb-util-keysyms
./autogen.sh --prefix="${XCB_INSTALL_PATH}" --enable-static --disable-shared
make -j$(nproc)
make install
cd ..

# Build xcb-util-renderutil
if [ ! -d xcb-util-renderutil ]; then
    git clone --depth 1 https://gitlab.freedesktop.org/xorg/lib/libxcb-render-util.git xcb-util-renderutil
fi
cd xcb-util-renderutil
./autogen.sh --prefix="${XCB_INSTALL_PATH}" --enable-static --disable-shared
make -j$(nproc)
make install
cd ..

# Build xcb-util-wm
if [ ! -d xcb-util-wm ]; then
    git clone --depth 1 https://gitlab.freedesktop.org/xorg/lib/libxcb-wm.git xcb-util-wm
fi
cd xcb-util-wm
./autogen.sh --prefix="${XCB_INSTALL_PATH}" --enable-static --disable-shared
make -j$(nproc)
make install
cd ..

# Create summary report
echo "XCB static libraries built successfully at ${XCB_INSTALL_PATH}"
echo "Available libraries:"
find "${XCB_INSTALL_PATH}/lib" -name "*.a" | sort

# Make script executable
chmod +x "${SCRIPT_DIR}/build-static-xcb-utils.sh"
