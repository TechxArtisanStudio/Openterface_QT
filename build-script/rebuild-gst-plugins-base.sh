#!/bin/bash

set -e

# Quick rebuild of gst-plugins-base with system GLib
WORK_DIR="${HOME}/qt-arm64-build"
QT_TARGET_DIR="/opt/Qt6-arm64"
GSTREAMER_VERSION="1.22.11"

echo "Rebuilding gst-plugins-base with system GLib..."

# Install system GLib packages
echo "Installing system GLib development packages..."
sudo apt-get update
sudo apt-get install -y \
  libglib2.0-dev \
  libgobject-2.0-dev \
  libgio-2.0-dev \
  liborc-0.4-dev

cd "${WORK_DIR}/gstreamer_sources"

# Set up environment
export PKG_CONFIG_PATH="${WORK_DIR}/gstreamer_build/lib/pkgconfig:${WORK_DIR}/ffmpeg_build/lib/pkgconfig:/usr/lib/aarch64-linux-gnu/pkgconfig"

# Rebuild gst-plugins-base
echo "Rebuilding gst-plugins-base..."
cd gst-plugins-base-${GSTREAMER_VERSION}

# Remove old build
rm -rf build

echo "Configuring gst-plugins-base with system GLib..."
meson setup build \
  --prefix="${WORK_DIR}/gstreamer_build" \
  --libdir=lib \
  --default-library=static \
  -Dexamples=disabled \
  -Dtests=disabled \
  -Ddoc=disabled \
  -Dtools=disabled \
  -Dalsa=enabled \
  -Dcdparanoia=disabled \
  -Dlibvisual=disabled \
  -Dorc=enabled \
  -Dtremor=disabled \
  -Dvorbis=disabled \
  -Dx11=enabled \
  -Dxshm=enabled \
  -Dxvideo=enabled \
  -Dgl=enabled \
  -Dgl_platform=glx \
  -Dgl_winsys=x11 \
  -Dvideotestsrc=enabled \
  -Dvideoconvert=enabled \
  -Dvideoscale=enabled \
  -Dapp=enabled \
  -Daudioconvert=enabled \
  -Daudioresample=enabled \
  -Dtypefind=enabled \
  -Dplayback=enabled \
  -Dsubparse=enabled \
  -Dencoding=enabled \
  -Dcompositor=enabled \
  -Doverlaycomposition=enabled \
  -Dpbtypes=enabled \
  -Ddmabuf=enabled \
  -Dnls=disabled

echo "Building gst-plugins-base..."
ninja -C build
ninja -C build install

echo "gst-plugins-base rebuild completed"

# Copy libraries to Qt target directory
echo "Copying libraries to ${QT_TARGET_DIR}..."
sudo cp -a ${WORK_DIR}/gstreamer_build/lib/. ${QT_TARGET_DIR}/lib/
sudo cp -a ${WORK_DIR}/gstreamer_build/include/. ${QT_TARGET_DIR}/include/

# Verify the installation
echo "Verifying installation..."
echo "Checking for gst/video/videooverlay.h:"
if [ -f "${QT_TARGET_DIR}/include/gstreamer-1.0/gst/video/videooverlay.h" ]; then
    echo "  ✓ Found!"
else
    echo "  ✗ Still missing"
fi

echo "Checking for libgstvideo-1.0.a:"
if [ -f "${QT_TARGET_DIR}/lib/libgstvideo-1.0.a" ]; then
    echo "  ✓ Found!"
    file "${QT_TARGET_DIR}/lib/libgstvideo-1.0.a" | head -1
else
    echo "  ✗ Still missing"
fi

echo "Checking for libgstaudio-1.0.a:"
if [ -f "${QT_TARGET_DIR}/lib/libgstaudio-1.0.a" ]; then
    echo "  ✓ Found!"
else
    echo "  ✗ Still missing"
fi

echo "Checking system GLib:"
pkg-config --exists glib-2.0 && echo "  ✓ System GLib found" || echo "  ✗ System GLib not found"
pkg-config --exists gobject-2.0 && echo "  ✓ System GObject found" || echo "  ✗ System GObject not found"

echo "Quick rebuild completed!"
