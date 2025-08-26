#!/bin/bash

set -e

# Quick rebuild script for missing GStreamer components
WORK_DIR="${HOME}/qt-arm64-build"
QT_TARGET_DIR="/opt/Qt6-arm64"
GSTREAMER_VERSION="1.22.11"

echo "Quick rebuild of missing GStreamer components..."
cd "${WORK_DIR}/gstreamer_sources"

# Set up environment
export PKG_CONFIG_PATH="${WORK_DIR}/gstreamer_build/lib/pkgconfig:${WORK_DIR}/ffmpeg_build/lib/pkgconfig"

# Install missing dependencies for GLib and GStreamer
echo "Installing missing development packages..."
sudo apt-get update
sudo apt-get install -y \
  liborc-0.4-dev \
  libpcre2-dev \
  libffi-dev \
  libmount-dev \
  libblkid-dev \
  libselinux1-dev \
  libxml2-dev \
  libjpeg-dev \
  libpng-dev \
  libfreetype6-dev \
  libharfbuzz-dev

# Build GLib first
echo "Building GLib..."
GLIB_VERSION="2.78.4"

if [ ! -d "glib-${GLIB_VERSION}" ]; then
  echo "Downloading GLib ${GLIB_VERSION}..."
  wget https://download.gnome.org/sources/glib/2.78/glib-${GLIB_VERSION}.tar.xz
  tar xf glib-${GLIB_VERSION}.tar.xz
  rm glib-${GLIB_VERSION}.tar.xz
fi

cd glib-${GLIB_VERSION}
if [ ! -f "${WORK_DIR}/gstreamer_build/lib/libglib-2.0.a" ]; then
  echo "Configuring and building GLib..."
  meson setup build \
    --prefix="${WORK_DIR}/gstreamer_build" \
    --libdir=lib \
    --default-library=static \
    -Dtests=false \
    -Dnls=disabled \
    -Dselinux=disabled \
    -Dxattr=false \
    -Dlibmount=disabled \
    -Dinternal_pcre=true \
    --buildtype=release
  
  ninja -C build
  ninja -C build install
  
  echo "GLib build completed"
else
  echo "GLib already built, skipping."
fi
cd ..

# Rebuild gst-plugins-base with proper configuration
echo "Rebuilding gst-plugins-base..."
cd gst-plugins-base-${GSTREAMER_VERSION}

# Remove old build if exists
rm -rf build

echo "Configuring gst-plugins-base with video support..."
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

ninja -C build
ninja -C build install

echo "gst-plugins-base rebuild completed"
cd ..

# Update PKG_CONFIG_PATH to include GStreamer
export PKG_CONFIG_PATH="${WORK_DIR}/gstreamer_build/lib/pkgconfig:${PKG_CONFIG_PATH}"

# Copy all libraries to Qt target directory
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
else
    echo "  ✗ Still missing"
fi

echo "Checking for libglib-2.0.a:"
if [ -f "${QT_TARGET_DIR}/lib/libglib-2.0.a" ]; then
    echo "  ✓ Found!"
else
    echo "  ✗ Still missing"
fi

echo "Quick rebuild completed!"
