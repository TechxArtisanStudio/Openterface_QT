#!/bin/bash

set -uo pipefail

# =========================
# Build Debian package (.deb)
# =========================
echo "Preparing Debian package..."

PKG_ROOT=/workspace/pkgroot
PKG_OUT=/workspace/build
SRC=/workspace/src
BUILD=/workspace/build

rm -rf "${PKG_ROOT}"
mkdir -p "${PKG_ROOT}/DEBIAN"
mkdir -p "${PKG_ROOT}/usr/local/bin"
mkdir -p "${PKG_ROOT}/usr/share/applications"
mkdir -p "${PKG_ROOT}/usr/share/metainfo"
mkdir -p "${PKG_ROOT}/usr/share/openterfaceQT/translations"

# Determine version from resources/version.h (APP_VERSION macro) if not already set
if [ -z "${VERSION}" ]; then
  VERSION_H="${SRC}/resources/version.h"
  if [ -f "${VERSION_H}" ]; then
	  VERSION=$(grep -Po '^#define APP_VERSION\s+"\K[0-9]+(\.[0-9]+)*' "${VERSION_H}" | head -n1)
  fi
  if [ -z "${VERSION}" ]; then
	  VERSION="0.0.1"
  fi
fi

# Determine architecture (map to Debian arch names) if not already set
if [ -z "${ARCH}" ]; then
  ARCH=$(dpkg --print-architecture 2>/dev/null || true)
  if [ -z "${ARCH}" ]; then
	  UNAME_M=$(uname -m)
	  case "${UNAME_M}" in
		  aarch64|arm64) ARCH=arm64;;
		  x86_64|amd64) ARCH=amd64;;
		  *) ARCH=${UNAME_M};;
	  esac
  fi
fi

# Copy main binary
if [ -f "${BUILD}/openterfaceQT" ]; then
	# Rename the binary to openterfaceQT.bin so it's not directly callable
	# This forces all execution through the wrapper script
	install -m 0755 "${BUILD}/openterfaceQT" "${PKG_ROOT}/usr/local/bin/openterfaceQT.bin"
	echo "✅ Binary installed as openterfaceQT.bin (will be wrapped)"
else
	echo "Error: built binary not found at ${BUILD}/openterfaceQT" >&2
	exit 1
fi

# Install patchelf for rpath manipulation
apt update && apt install -y patchelf

# Copy Qt libraries to bundle them in the deb
# CRITICAL: Must use a proper Qt6 build, NOT system libraries
# System Qt6 libraries have version dependencies that won't work at runtime
QT_LIB_DIR="/opt/Qt6/lib"

if [ ! -d "${QT_LIB_DIR}" ]; then
    echo "❌ ERROR: Qt6 custom build not found at /opt/Qt6/lib"
    echo "   The DEB package requires a properly compiled Qt6 build."
    echo "   System Qt6 libraries cannot be used as they have version dependencies."
    echo "   Please ensure Qt6 is built and installed at /opt/Qt6/ before packaging."
    exit 1
fi

mkdir -p "${PKG_ROOT}/usr/lib/openterfaceqt/qt6"
echo "Copying Qt6 libraries from ${QT_LIB_DIR}..."
echo "   Searching for libQt6*.so* files..."

# Copy only Qt6 libraries (not all system libraries)
COPIED_COUNT=0
find "${QT_LIB_DIR}" -maxdepth 1 -name "libQt6*.so*" -type f 2>/dev/null | while read -r libfile; do
    cp -a "$libfile" "${PKG_ROOT}/usr/lib/openterfaceqt/qt6/"
    COPIED_COUNT=$((COPIED_COUNT + 1))
    basename "$libfile"
done

# Also copy symlinks properly
find "${QT_LIB_DIR}" -maxdepth 1 -name "libQt6*.so*" -type l 2>/dev/null | while read -r libfile; do
    cp -Pa "$libfile" "${PKG_ROOT}/usr/lib/openterfaceqt/qt6/"
done

QT_LIBS=$(ls "${PKG_ROOT}/usr/lib/openterfaceqt/qt6"/libQt6*.so* 2>/dev/null | wc -l)
if [ $QT_LIBS -gt 0 ]; then
    echo "✅ Qt libraries copied successfully ($QT_LIBS files)"
    ls -1 "${PKG_ROOT}/usr/lib/openterfaceqt/qt6"/libQt6*.so* | sed 's/^/     - /'
else
    echo "❌ ERROR: No Qt6 libraries were copied from ${QT_LIB_DIR}"
    echo "   Available files in ${QT_LIB_DIR}:"
    ls -la "${QT_LIB_DIR}"/libQt6*.so* 2>/dev/null | sed 's/^/     /' || echo "     (no libQt6*.so* files found)"
    exit 1
fi

# Copy libjpeg and libturbojpeg libraries from FFmpeg prefix
echo "🔍 Searching for JPEG libraries for FFmpeg support..."
mkdir -p "${PKG_ROOT}/usr/lib/openterfaceqt"

# Copy libjpeg libraries - search multiple locations
echo "📋 DEB: Searching for libjpeg libraries..."
JPEG_FOUND=0
for SEARCH_DIR in /opt/ffmpeg/lib /usr/lib/x86_64-linux-gnu /usr/lib; do
    echo "   Checking: $SEARCH_DIR"
    if [ -d "$SEARCH_DIR" ]; then
        if ls "$SEARCH_DIR"/libjpeg.so* >/dev/null 2>&1; then
            echo "   ✅ Found libjpeg in $SEARCH_DIR"
            JPEG_FILES=$(ls -la "$SEARCH_DIR"/libjpeg.so*)
            echo "   Files found:"
            echo "$JPEG_FILES" | sed 's/^/     /'
            cp -av "$SEARCH_DIR"/libjpeg.so* "${PKG_ROOT}/usr/lib/openterfaceqt/" 2>&1 | sed 's/^/     /'
            echo "   ✅ libjpeg libraries copied to ${PKG_ROOT}/usr/lib/openterfaceqt"
            JPEG_FOUND=1
            break
        else
            echo "   ✗ No libjpeg found in $SEARCH_DIR"
        fi
    else
        echo "   ✗ Directory does not exist: $SEARCH_DIR"
    fi
done
if [ $JPEG_FOUND -eq 0 ]; then
    echo "❌ ERROR: libjpeg libraries not found in any search path!"
    echo "📁 Checking what's available in system library paths:"
    echo "   /opt/ffmpeg/lib contents:"
    ls -la /opt/ffmpeg/lib 2>/dev/null | grep -i jpeg || echo "     (directory not found or no jpeg files)"
    echo "   /usr/lib/x86_64-linux-gnu contents (jpeg):"
    ls -la /usr/lib/x86_64-linux-gnu/*jpeg* 2>/dev/null || echo "     (no jpeg files found)"
    echo "   /usr/lib contents (jpeg):"
    ls -la /usr/lib/*jpeg* 2>/dev/null || echo "     (no jpeg files found)"
else
    echo "✅ libjpeg found and copied"
fi

# Copy libturbojpeg libraries - search multiple locations
echo "📋 DEB: Searching for libturbojpeg libraries..."
TURBOJPEG_FOUND=0
for SEARCH_DIR in /opt/ffmpeg/lib /usr/lib/x86_64-linux-gnu /usr/lib; do
    echo "   Checking: $SEARCH_DIR"
    if [ -d "$SEARCH_DIR" ]; then
        # Use find to detect both files and symlinks
        if find "$SEARCH_DIR" -maxdepth 1 \( -name "libturbojpeg.so*" -type f -o -name "libturbojpeg.so*" -type l \) 2>/dev/null | grep -q .; then
            echo "   ✅ Found libturbojpeg in $SEARCH_DIR"
            # List files found for diagnostics
            ls -la "$SEARCH_DIR"/libturbojpeg.so* 2>/dev/null | while read -r line; do
                echo "     $line"
            done
            # Copy all libturbojpeg files and symlinks - use cp -P to preserve symlinks
            cp -Pv "$SEARCH_DIR"/libturbojpeg.so* "${PKG_ROOT}/usr/lib/openterfaceqt/" 2>&1 | sed 's/^/     /'
            echo "   ✅ libturbojpeg libraries copied to ${PKG_ROOT}/usr/lib/openterfaceqt"
            TURBOJPEG_FOUND=1
            break
        else
            echo "   ✗ No libturbojpeg found in $SEARCH_DIR"
        fi
    else
        echo "   ✗ Directory does not exist: $SEARCH_DIR"
    fi
done
if [ $TURBOJPEG_FOUND -eq 0 ]; then
    echo "❌ ERROR: libturbojpeg libraries not found in any search path!"
    echo "📁 Checking what's available in system library paths:"
    echo "   /opt/ffmpeg/lib contents:"
    ls -la /opt/ffmpeg/lib 2>/dev/null | grep -i turbojpeg || echo "     (directory not found or no turbojpeg files)"
    echo "   /usr/lib/x86_64-linux-gnu contents (turbojpeg):"
    ls -la /usr/lib/x86_64-linux-gnu/*turbojpeg* 2>/dev/null || echo "     (no turbojpeg files found)"
    echo "   /usr/lib contents (turbojpeg):"
    ls -la /usr/lib/*turbojpeg* 2>/dev/null || echo "     (no turbojpeg files found)"
else
    echo "✅ libturbojpeg found and copied"
fi

# Copy VA-API libraries (libva) for hardware acceleration - search multiple locations
echo "📋 DEB: Searching for VA-API libraries..."
VA_FOUND=0
for SEARCH_DIR in /usr/lib/x86_64-linux-gnu /usr/lib; do
    echo "   Checking: $SEARCH_DIR"
    if [ -d "$SEARCH_DIR" ]; then
        if ls "$SEARCH_DIR"/libva.so* >/dev/null 2>&1; then
            echo "   ✅ Found VA-API libraries in $SEARCH_DIR"
            VA_FILES=$(ls -la "$SEARCH_DIR"/libva*.so*)
            echo "   Files found:"
            echo "$VA_FILES" | sed 's/^/     /'
            cp -av "$SEARCH_DIR"/libva*.so* "${PKG_ROOT}/usr/lib/openterfaceqt/" 2>&1 | sed 's/^/     /'
            echo "   ✅ VA-API libraries copied to ${PKG_ROOT}/usr/lib/openterfaceqt"
            VA_FOUND=1
            break
        else
            echo "   ✗ No VA-API libraries found in $SEARCH_DIR"
        fi
    else
        echo "   ✗ Directory does not exist: $SEARCH_DIR"
    fi
done
if [ $VA_FOUND -eq 0 ]; then
    echo "⚠️  Warning: VA-API libraries not found"
fi

# Copy VDPAU libraries (libvdpau) for hardware acceleration - search multiple locations
echo "📋 DEB: Searching for VDPAU libraries..."
VDPAU_FOUND=0
for SEARCH_DIR in /usr/lib/x86_64-linux-gnu /usr/lib; do
    echo "   Checking: $SEARCH_DIR"
    if [ -d "$SEARCH_DIR" ]; then
        if ls "$SEARCH_DIR"/libvdpau.so* >/dev/null 2>&1; then
            echo "   ✅ Found VDPAU libraries in $SEARCH_DIR"
            VDPAU_FILES=$(ls -la "$SEARCH_DIR"/libvdpau*.so*)
            echo "   Files found:"
            echo "$VDPAU_FILES" | sed 's/^/     /'
            cp -av "$SEARCH_DIR"/libvdpau*.so* "${PKG_ROOT}/usr/lib/openterfaceqt/" 2>&1 | sed 's/^/     /'
            echo "   ✅ VDPAU libraries copied to ${PKG_ROOT}/usr/lib/openterfaceqt"
            VDPAU_FOUND=1
            break
        else
            echo "   ✗ No VDPAU libraries found in $SEARCH_DIR"
        fi
    else
        echo "   ✗ Directory does not exist: $SEARCH_DIR"
    fi
done
if [ $VDPAU_FOUND -eq 0 ]; then
    echo "⚠️  Warning: VDPAU libraries not found"
fi

# Copy core FFmpeg libraries (libavdevice, libavcodec, libavformat, libavutil, libswscale, libswresample, libavfilter)
echo "📋 DEB: Searching for FFmpeg core libraries..."
FFMPEG_FOUND=0
FFMPEG_LIBS=(libavdevice.so libavcodec.so libavformat.so libavutil.so libswscale.so libswresample.so libavfilter.so)

for SEARCH_DIR in /opt/ffmpeg/lib /usr/lib/x86_64-linux-gnu /usr/lib; do
    echo "   Checking: $SEARCH_DIR"
    if [ -d "$SEARCH_DIR" ]; then
        # Check if any ffmpeg library exists
        FOUND_ANY=0
        for ffmpeg_lib in "${FFMPEG_LIBS[@]}"; do
            if ls "$SEARCH_DIR"/${ffmpeg_lib}* >/dev/null 2>&1; then
                FOUND_ANY=1
                break
            fi
        done
        
        if [ $FOUND_ANY -eq 1 ]; then
            echo "   ✅ Found FFmpeg libraries in $SEARCH_DIR"
            for ffmpeg_lib in "${FFMPEG_LIBS[@]}"; do
                if ls "$SEARCH_DIR"/${ffmpeg_lib}* >/dev/null 2>&1; then
                    echo "   Found: $ffmpeg_lib"
                    FFMPEG_FILES=$(ls -la "$SEARCH_DIR"/${ffmpeg_lib}* 2>/dev/null)
                    echo "     Files: $FFMPEG_FILES" | sed 's/^/     /'
                    cp -Pv "$SEARCH_DIR"/${ffmpeg_lib}* "${PKG_ROOT}/usr/lib/openterfaceqt/" 2>&1 | sed 's/^/     /'
                fi
            done
            echo "   ✅ FFmpeg core libraries copied to ${PKG_ROOT}/usr/lib/openterfaceqt"
            FFMPEG_FOUND=1
            break
        else
            echo "   ✗ No FFmpeg libraries found in $SEARCH_DIR"
        fi
    else
        echo "   ✗ Directory does not exist: $SEARCH_DIR"
    fi
done

if [ $FFMPEG_FOUND -eq 0 ]; then
    echo "⚠️  Warning: FFmpeg core libraries not found!"
    echo "📁 Checking what's available in system library paths:"
    echo "   /opt/ffmpeg/lib contents:"
    ls -la /opt/ffmpeg/lib 2>/dev/null | grep -E "libav|libsw" || echo "     (directory not found or no ffmpeg files)"
    echo "   /usr/lib/x86_64-linux-gnu contents (ffmpeg):"
    ls -la /usr/lib/x86_64-linux-gnu/libav* /usr/lib/x86_64-linux-gnu/libsw* 2>/dev/null || echo "     (no ffmpeg files found)"
    echo "   /usr/lib contents (ffmpeg):"
    ls -la /usr/lib/libav* /usr/lib/libsw* 2>/dev/null || echo "     (no ffmpeg files found)"
else
    echo "✅ FFmpeg core libraries found and copied"
fi

# Copy GStreamer core and base libraries - search multiple locations
echo "📋 DEB: Searching for GStreamer core and base libraries..."
GSTREAMER_FOUND=0

# Define all GStreamer libraries to bundle (core + base + plugins-base + ORC)
GSTREAMER_LIBS=(
    "libgstreamer-1.0.so"
    "libgstbase-1.0.so"
    "libgstaudio-1.0.so"
    "libgstvideo-1.0.so"
    "libgstapp-1.0.so"
    "libgstpbutils-1.0.so"
    "libgsttag-1.0.so"
    "libgstrtp-1.0.so"
    "libgstrtsp-1.0.so"
    "libgstsdp-1.0.so"
    "libgstallocators-1.0.so"
    "libgstgl-1.0.so"
    "liborc-0.4.so"
)

for SEARCH_DIR in /usr/lib/x86_64-linux-gnu /usr/lib; do
    echo "   Checking: $SEARCH_DIR"
    if [ -d "$SEARCH_DIR" ]; then
        # Check if any gstreamer library exists
        FOUND_ANY=0
        for gst_lib in "${GSTREAMER_LIBS[@]}"; do
            if ls "$SEARCH_DIR"/${gst_lib}* >/dev/null 2>&1; then
                FOUND_ANY=1
                break
            fi
        done
        
        if [ $FOUND_ANY -eq 1 ]; then
            echo "   ✅ Found GStreamer libraries in $SEARCH_DIR"
            for gst_lib in "${GSTREAMER_LIBS[@]}"; do
                if ls "$SEARCH_DIR"/${gst_lib}* >/dev/null 2>&1; then
                    echo "   Copying: $gst_lib"
                    cp -Pv "$SEARCH_DIR"/${gst_lib}* "${PKG_ROOT}/usr/lib/openterfaceqt/" 2>&1 | sed 's/^/     /'
                else
                    echo "   ⚠️  Skipping: $gst_lib (not found)"
                fi
            done
            echo "   ✅ GStreamer libraries copied to ${PKG_ROOT}/usr/lib/openterfaceqt"
            GSTREAMER_FOUND=1
            break
        else
            echo "   ✗ No GStreamer libraries found in $SEARCH_DIR"
        fi
    else
        echo "   ✗ Directory does not exist: $SEARCH_DIR"
    fi
done
if [ $GSTREAMER_FOUND -eq 0 ]; then
    echo "⚠️  Warning: GStreamer libraries not found"
else
    echo "✅ GStreamer core and base libraries bundled successfully"
fi

# Copy ORC libraries (needed by GStreamer) - search multiple locations
echo "📋 DEB: Searching for ORC libraries..."
ORC_FOUND=0
for SEARCH_DIR in /usr/lib/x86_64-linux-gnu /usr/lib; do
    echo "   Checking: $SEARCH_DIR"
    if [ -d "$SEARCH_DIR" ]; then
        if ls "$SEARCH_DIR"/liborc-0.4.so* >/dev/null 2>&1; then
            echo "   ✅ Found ORC libraries in $SEARCH_DIR"
            ORC_FILES=$(ls -la "$SEARCH_DIR"/liborc-0.4.so*)
            echo "   Files found:"
            echo "$ORC_FILES" | sed 's/^/     /'
            cp -av "$SEARCH_DIR"/liborc-0.4.so* "${PKG_ROOT}/usr/lib/openterfaceqt/" 2>&1 | sed 's/^/     /'
            echo "   ✅ ORC libraries copied to ${PKG_ROOT}/usr/lib/openterfaceqt"
            ORC_FOUND=1
            break
        else
            echo "   ✗ No ORC libraries found in $SEARCH_DIR"
        fi
    else
        echo "   ✗ Directory does not exist: $SEARCH_DIR"
    fi
done
if [ $ORC_FOUND -eq 0 ]; then
    echo "⚠️  Warning: ORC libraries not found"
else
    echo "✅ ORC libraries bundled successfully"
fi
echo "📋 DEB: Searching for v4l-utils libraries..."
V4L_FOUND=0
for SEARCH_DIR in /usr/lib/x86_64-linux-gnu /usr/lib; do
    echo "   Checking: $SEARCH_DIR"
    if [ -d "$SEARCH_DIR" ]; then
        if ls "$SEARCH_DIR"/libv4l*.so* >/dev/null 2>&1; then
            echo "   ✅ Found v4l-utils libraries in $SEARCH_DIR"
            V4L_FILES=$(ls -la "$SEARCH_DIR"/libv4l*.so*)
            echo "   Files found:"
            echo "$V4L_FILES" | sed 's/^/     /'
            cp -av "$SEARCH_DIR"/libv4l*.so* "${PKG_ROOT}/usr/lib/openterfaceqt/" 2>&1 | sed 's/^/     /'
            echo "   ✅ v4l-utils libraries copied to ${PKG_ROOT}/usr/lib/openterfaceqt"
            V4L_FOUND=1
            break
        else
            echo "   ✗ No v4l-utils libraries found in $SEARCH_DIR"
        fi
    else
        echo "   ✗ Directory does not exist: $SEARCH_DIR"
    fi
done
if [ $V4L_FOUND -eq 0 ]; then
    echo "⚠️  Warning: v4l-utils libraries not found"
fi

echo "📋 DEB: Final library contents in ${PKG_ROOT}/usr/lib/openterfaceqt:"
ls -lah "${PKG_ROOT}/usr/lib/openterfaceqt/" | head -20

echo "📋 DEB: Verifying all required libraries presence in package..."

# Define all required libraries for verification
REQUIRED_LIBS=(
    "libjpeg.so"
    "libturbojpeg.so"
    "libva.so"
    "libvdpau.so"
    "libgstreamer-1.0.so"
    "liborc-0.4.so"
    "libavcodec.so"
    "libavformat.so"
    "libavutil.so"
    "libswscale.so"
    "libswresample.so"
)

LIBS_FOUND=0
LIBS_MISSING=()

for lib_name in "${REQUIRED_LIBS[@]}"; do
    if ls "${PKG_ROOT}/usr/lib/openterfaceqt/${lib_name}"* >/dev/null 2>&1; then
        echo "   ✅ Found $lib_name:"
        ls -lah "${PKG_ROOT}/usr/lib/openterfaceqt/${lib_name}"* | sed 's/^/      /'
        LIBS_FOUND=$((LIBS_FOUND + 1))
    else
        LIBS_MISSING+=("$lib_name")
        echo "   ⚠️  Missing $lib_name - will rely on system dependencies"
    fi
done

echo "   📊 Summary: $LIBS_FOUND/${#REQUIRED_LIBS[@]} libraries bundled"

if [ ${#LIBS_MISSING[@]} -gt 0 ]; then
    echo "   📝 Missing libraries (will be installed via DEB Depends):"
    for lib in "${LIBS_MISSING[@]}"; do
        echo "       - $lib"
    done
else
    echo "✅ All required libraries successfully bundled in package"
fi

echo ""
echo "📋 DEB: Final package library contents:"
echo "   Total .so files in ${PKG_ROOT}/usr/lib/openterfaceqt:"
ls "${PKG_ROOT}/usr/lib/openterfaceqt/"*.so* 2>/dev/null | wc -l
echo "   Library files present:"
ls -lah "${PKG_ROOT}/usr/lib/openterfaceqt/"*.so* 2>/dev/null | head -15

# Copy Qt plugins (CRITICAL: must be from the same Qt6 build)
QT_PLUGIN_DIR="/opt/Qt6/plugins"

if [ ! -d "${QT_PLUGIN_DIR}" ]; then
    echo "❌ ERROR: Qt6 plugins not found at ${QT_PLUGIN_DIR}"
    echo "   Must use Qt6 from the same build as the libraries."
    exit 1
fi

mkdir -p "${PKG_ROOT}/usr/lib/openterfaceqt/qt6/plugins"
echo "📋 DEB: Copying Qt plugins from ${QT_PLUGIN_DIR}..."
if cp -ra "${QT_PLUGIN_DIR}"/* "${PKG_ROOT}/usr/lib/openterfaceqt/qt6/plugins/" 2>/dev/null; then
    PLUGIN_COUNT=$(find "${PKG_ROOT}/usr/lib/openterfaceqt/qt6/plugins" -type f | wc -l)
    echo "✅ Qt plugins copied successfully ($PLUGIN_COUNT files)"
else
    echo "❌ ERROR: Failed to copy Qt plugins"
    exit 1
fi

# Copy Qt6 XCB QPA library (needed by libqxcb.so plugin) - must be from same Qt6 build
echo "📋 DEB: Searching for Qt6 XCB QPA library..."
XCB_QPA_FOUND=0

# Only look in the Qt6 build location, not system
if [ -d "/opt/Qt6/lib" ] && ls "/opt/Qt6/lib"/libQt6XcbQpa.so* >/dev/null 2>&1; then
    echo "   ✅ Found libQt6XcbQpa.so in /opt/Qt6/lib"
    XCB_QPA_FILES=$(ls -la "/opt/Qt6/lib"/libQt6XcbQpa.so*)
    echo "   Files found:"
    echo "$XCB_QPA_FILES" | sed 's/^/     /'
    cp -Pv "/opt/Qt6/lib"/libQt6XcbQpa.so* "${PKG_ROOT}/usr/lib/openterfaceqt/qt6/" 2>&1 | sed 's/^/     /'
    echo "   ✅ libQt6XcbQpa.so copied to ${PKG_ROOT}/usr/lib/openterfaceqt/qt6/"
    XCB_QPA_FOUND=1
else
    echo "   ✗ No libQt6XcbQpa.so found in /opt/Qt6/lib"
fi

if [ $XCB_QPA_FOUND -eq 0 ]; then
    echo "❌ ERROR: libQt6XcbQpa.so library not found!"
    echo "   This library must be from the same Qt6 build as libQt6Core.so"
    exit 1
else
    echo "✅ libQt6XcbQpa.so found and copied"
fi

# Verify XCB platform plugin is bundled
echo "📋 DEB: Verifying XCB platform plugin..."
if [ -f "${PKG_ROOT}/usr/lib/openterfaceqt/qt6/plugins/platforms/libqxcb.so" ]; then
    echo "   ✅ Found libqxcb.so platform plugin"
    # Update RPATH for the plugin to find libQt6XcbQpa.so
    if patchelf --set-rpath '/usr/lib/openterfaceqt/qt6:$ORIGIN/../..:$ORIGIN/../../..' "${PKG_ROOT}/usr/lib/openterfaceqt/qt6/plugins/platforms/libqxcb.so" 2>/dev/null; then
        echo "   ✅ Updated RPATH for libqxcb.so plugin"
    else
        echo "   ⚠️  Could not update plugin RPATH (may not be critical)"
    fi
else
    echo "   ⚠️  Warning: libqxcb.so platform plugin not found"
fi

# Copy Qt QML imports
QT_QML_DIR="/opt/Qt6/qml"
if [ -d "${QT_QML_DIR}" ]; then
    mkdir -p "${PKG_ROOT}/usr/lib/openterfaceqt/qt6/qml"
    echo "Copying Qt QML imports..."
    cp -ra "${QT_QML_DIR}"/* "${PKG_ROOT}/usr/lib/openterfaceqt/qt6/qml/" 2>/dev/null || true
fi

# Copy SVG-specific Qt libraries and plugins (CRITICAL for SVG icon support)
echo "📋 DEB: Ensuring SVG support for icons..."

# Copy libQt6Svg library
echo "   📦 Searching for libQt6Svg.so..."
SVG_LIB_FOUND=0
for SEARCH_DIR in /opt/Qt6/lib /usr/lib/x86_64-linux-gnu /usr/lib; do
    if [ -d "$SEARCH_DIR" ]; then
        if ls "$SEARCH_DIR"/libQt6Svg.so* >/dev/null 2>&1; then
            echo "   ✅ Found libQt6Svg.so in $SEARCH_DIR"
            cp -Pv "$SEARCH_DIR"/libQt6Svg.so* "${PKG_ROOT}/usr/lib/openterfaceqt/qt6/" 2>&1 | sed 's/^/     /'
            SVG_LIB_FOUND=1
            break
        fi
    fi
done
if [ $SVG_LIB_FOUND -eq 0 ]; then
    echo "   ⚠️  libQt6Svg.so not found - attempting to install..."
    apt update && apt install -y libqt6svg6 2>/dev/null || echo "   ⚠️  Installation failed, SVG support may be limited"
fi

# Copy SVG image format plugin (libqsvg.so)
echo "   📦 Searching for libqsvg.so (SVG image format plugin)..."
SVG_PLUGIN_FOUND=0
for SEARCH_DIR in /opt/Qt6/plugins /usr/lib/qt6/plugins /usr/lib/x86_64-linux-gnu/qt6/plugins; do
    if [ -d "$SEARCH_DIR/imageformats" ]; then
        if [ -f "$SEARCH_DIR/imageformats/libqsvg.so" ]; then
            echo "   ✅ Found libqsvg.so in $SEARCH_DIR"
            mkdir -p "${PKG_ROOT}/usr/lib/openterfaceqt/qt6/plugins/imageformats"
            cp -Pv "$SEARCH_DIR/imageformats/libqsvg.so" "${PKG_ROOT}/usr/lib/openterfaceqt/qt6/plugins/imageformats/" 2>&1 | sed 's/^/     /'
            SVG_PLUGIN_FOUND=1
            break
        fi
    fi
done
if [ $SVG_PLUGIN_FOUND -eq 0 ]; then
    echo "   ⚠️  libqsvg.so not found in: /opt/Qt6/plugins, /usr/lib/qt6/plugins, /usr/lib/x86_64-linux-gnu/qt6/plugins"
    echo "   ⚠️  SVG images won't be loaded properly"
fi

# Copy SVG icon engine plugin (libsvgicon.so)
echo "   📦 Searching for libsvgicon.so (SVG icon engine plugin)..."
SVGICON_PLUGIN_FOUND=0

# First try: search in standard iconengines directories with wildcards
for SEARCH_DIR in /opt/Qt6/plugins /usr/lib/qt6/plugins /usr/lib/x86_64-linux-gnu/qt6/plugins; do
    if [ -d "$SEARCH_DIR/iconengines" ]; then
        # Use find to look for libsvgicon.so with potential version suffixes
        FOUND_FILES=$(find "$SEARCH_DIR/iconengines" -name "libsvgicon.so*" 2>/dev/null)
        if [ -n "$FOUND_FILES" ]; then
            echo "   ✅ Found libsvgicon.so in $SEARCH_DIR/iconengines"
            mkdir -p "${PKG_ROOT}/usr/lib/openterfaceqt/qt6/plugins/iconengines"
            echo "$FOUND_FILES" | while read -r svg_icon_file; do
                cp -Pv "$svg_icon_file" "${PKG_ROOT}/usr/lib/openterfaceqt/qt6/plugins/iconengines/" 2>&1 | sed 's/^/     /'
            done
            SVGICON_PLUGIN_FOUND=1
            break
        fi
    fi
done

# Second try: search for it in the broader plugins directory (might not be in iconengines subdirectory)
if [ $SVGICON_PLUGIN_FOUND -eq 0 ]; then
    for SEARCH_DIR in /opt/Qt6/plugins /usr/lib/qt6/plugins /usr/lib/x86_64-linux-gnu/qt6/plugins; do
        if [ -d "$SEARCH_DIR" ]; then
            FOUND_FILES=$(find "$SEARCH_DIR" -name "libsvgicon.so*" -type f 2>/dev/null)
            if [ -n "$FOUND_FILES" ]; then
                echo "   ✅ Found libsvgicon.so in $SEARCH_DIR (in alternate location)"
                mkdir -p "${PKG_ROOT}/usr/lib/openterfaceqt/qt6/plugins/iconengines"
                echo "$FOUND_FILES" | while read -r svg_icon_file; do
                    cp -Pv "$svg_icon_file" "${PKG_ROOT}/usr/lib/openterfaceqt/qt6/plugins/iconengines/" 2>&1 | sed 's/^/     /'
                done
                SVGICON_PLUGIN_FOUND=1
                break
            fi
        fi
    done
fi

# Third try: attempt to install SVG icon plugin if not found
if [ $SVGICON_PLUGIN_FOUND -eq 0 ]; then
    echo "   ⚠️  libsvgicon.so not found in: /opt/Qt6/plugins, /usr/lib/qt6/plugins, /usr/lib/x86_64-linux-gnu/qt6/plugins"
    echo "   📥 Attempting to install SVG icon plugin packages..."
    
    # Try Debian package
    if command -v apt-get >/dev/null 2>&1; then
        if apt-get update >/dev/null 2>&1 && apt-get install -y qt6-svg 2>/dev/null; then
            echo "   ✅ Installed qt6-svg package"
            # Retry search after installation
            for SEARCH_DIR in /usr/lib/qt6/plugins /usr/lib/x86_64-linux-gnu/qt6/plugins; do
                if [ -d "$SEARCH_DIR/iconengines" ]; then
                    FOUND_FILES=$(find "$SEARCH_DIR/iconengines" -name "libsvgicon.so*" 2>/dev/null)
                    if [ -n "$FOUND_FILES" ]; then
                        echo "   ✅ Found libsvgicon.so after installation"
                        mkdir -p "${PKG_ROOT}/usr/lib/openterfaceqt/qt6/plugins/iconengines"
                        echo "$FOUND_FILES" | while read -r svg_icon_file; do
                            cp -Pv "$svg_icon_file" "${PKG_ROOT}/usr/lib/openterfaceqt/qt6/plugins/iconengines/" 2>&1 | sed 's/^/     /'
                        done
                        SVGICON_PLUGIN_FOUND=1
                        break
                    fi
                fi
            done
        fi
    fi
    
    # Try RPM package
    if [ $SVGICON_PLUGIN_FOUND -eq 0 ] && command -v dnf >/dev/null 2>&1; then
        if dnf install -y qt6-qtsvg 2>/dev/null; then
            echo "   ✅ Installed qt6-qtsvg package"
            # Retry search after installation
            for SEARCH_DIR in /usr/lib/qt6/plugins /usr/lib/x86_64-linux-gnu/qt6/plugins; do
                if [ -d "$SEARCH_DIR/iconengines" ]; then
                    FOUND_FILES=$(find "$SEARCH_DIR/iconengines" -name "libsvgicon.so*" 2>/dev/null)
                    if [ -n "$FOUND_FILES" ]; then
                        echo "   ✅ Found libsvgicon.so after installation"
                        mkdir -p "${PKG_ROOT}/usr/lib/openterfaceqt/qt6/plugins/iconengines"
                        echo "$FOUND_FILES" | while read -r svg_icon_file; do
                            cp -Pv "$svg_icon_file" "${PKG_ROOT}/usr/lib/openterfaceqt/qt6/plugins/iconengines/" 2>&1 | sed 's/^/     /'
                        done
                        SVGICON_PLUGIN_FOUND=1
                        break
                    fi
                fi
            done
        fi
    fi
fi

# Final status
if [ $SVGICON_PLUGIN_FOUND -eq 0 ]; then
    echo "   ⚠️  libsvgicon.so still not found - SVG icons may not display optimally"
    echo "   ℹ️  Note: SVG image format support (libqsvg.so) is still available for SVG rendering"
    echo "   ℹ️  Icons may fall back to other formats or display without SVG icon engine"
fi

# Verify SVG support components
echo ""
if [ $SVG_LIB_FOUND -eq 1 ] && [ $SVG_PLUGIN_FOUND -eq 1 ] && [ $SVGICON_PLUGIN_FOUND -eq 1 ]; then
    echo "✅ Full SVG support successfully bundled for DEB package"
else
    echo "⚠️  WARNING: Some SVG components are missing - SVG display may be incomplete"
    echo "   - libQt6Svg.so: $([ $SVG_LIB_FOUND -eq 1 ] && echo '✅' || echo '❌')"
    echo "   - libqsvg.so: $([ $SVG_PLUGIN_FOUND -eq 1 ] && echo '✅' || echo '❌')"
    echo "   - libsvgicon.so: $([ $SVGICON_PLUGIN_FOUND -eq 1 ] && echo '✅' || echo '❌')"
fi
echo ""

# Update the binary's rpath to point to bundled libraries
# Libraries are bundled in /usr/lib/openterfaceqt (isolated from system libraries)
# Binary is at: /usr/local/bin/openterfaceQT
# Qt6 libraries are at: /usr/lib/openterfaceqt/qt6/
# Other libraries are at: /usr/lib/openterfaceqt/
# So RPATH should include both: /usr/lib/openterfaceqt/qt6:/usr/lib/openterfaceqt
if [ -f "${PKG_ROOT}/usr/local/bin/openterfaceQT" ]; then
    echo "🔧 Updating rpath for bundled libraries..."
    echo "   Setting RPATH to: /usr/lib/openterfaceqt/qt6:/usr/lib/openterfaceqt"
    if patchelf --set-rpath '/usr/lib/openterfaceqt/qt6:/usr/lib/openterfaceqt' "${PKG_ROOT}/usr/local/bin/openterfaceQT"; then
        echo "   ✅ RPATH updated successfully"
        patchelf --print-rpath "${PKG_ROOT}/usr/local/bin/openterfaceQT" | sed 's/^/     Actual RPATH: /'
    else
        echo "   ❌ Failed to update RPATH!"
        exit 1
    fi
fi

# Copy desktop file (ensure Exec uses wrapper script for proper environment setup)
if [ -f "${SRC}/com.openterface.openterfaceQT.desktop" ]; then
	sed -e 's|^Exec=.*$|Exec=/usr/local/bin/openterfaceQT-wrapper.sh|g' \
		-e 's|^Icon=.*$|Icon=openterfaceQT|g' \
		"${SRC}/com.openterface.openterfaceQT.desktop" > "${PKG_ROOT}/usr/share/applications/com.openterface.openterfaceQT.desktop"
fi

# Copy wrapper script to bin
if [ -f "${SRC}/packaging/openterfaceQT-wrapper.sh" ]; then
	install -m 0755 "${SRC}/packaging/openterfaceQT-wrapper.sh" "${PKG_ROOT}/usr/local/bin/openterfaceQT-wrapper.sh"
	echo "✅ Wrapper script installed"
	
	# Create a symlink/alias at the standard binary location that points to the wrapper
	# This ensures EVERY call to openterfaceQT goes through the wrapper with LD_PRELOAD
	ln -sf openterfaceQT-wrapper.sh "${PKG_ROOT}/usr/local/bin/openterfaceQT"
	echo "✅ Created symlink: /usr/local/bin/openterfaceQT → openterfaceQT-wrapper.sh"
else
	echo "⚠️  Warning: wrapper script not found, using inline environment variables as fallback"
	sed -e 's|^Exec=.*$|Exec=env QT_PLUGIN_PATH=/usr/lib/qt6/plugins:/usr/lib/x86_64-linux-gnu/qt6/plugins QML2_IMPORT_PATH=/usr/lib/qt6/qml:/usr/lib/x86_64-linux-gnu/qt6/qml GST_PLUGIN_PATH=/usr/lib/x86_64-linux-gnu/gstreamer-1.0:/usr/lib/gstreamer-1.0 /usr/local/bin/openterfaceQT|g' \
		"${SRC}/com.openterface.openterfaceQT.desktop" > "${PKG_ROOT}/usr/share/applications/com.openterface.openterfaceQT.desktop"
fi

# Install icon into hicolor theme
ICON_SRC=""
for p in \
	"${SRC}/images/icon_256.png" \
	"${SRC}/images/icon_256.svg" \
	"${SRC}/images/icon_128.png" \
	"${SRC}/images/icon_64.png" \
	"${SRC}/images/icon_32.png"; do
	if [ -f "$p" ]; then ICON_SRC="$p"; break; fi
done
if [ -n "${ICON_SRC}" ]; then
	ICON_EXT="${ICON_SRC##*.}"
	if [ "${ICON_EXT}" = "svg" ]; then
		mkdir -p "${PKG_ROOT}/usr/share/icons/hicolor/scalable/apps"
		cp "${ICON_SRC}" "${PKG_ROOT}/usr/share/icons/hicolor/scalable/apps/openterfaceQT.svg"
	else
		mkdir -p "${PKG_ROOT}/usr/share/icons/hicolor/256x256/apps"
		cp "${ICON_SRC}" "${PKG_ROOT}/usr/share/icons/hicolor/256x256/apps/openterfaceQT.png"
	fi
fi

# Copy appstream/metainfo if present
if [ -f "${SRC}/com.openterface.openterfaceQT.metainfo.xml" ]; then
	cp "${SRC}/com.openterface.openterfaceQT.metainfo.xml" "${PKG_ROOT}/usr/share/metainfo/"
fi

# Copy translations from build if present
if ls "${BUILD}"/openterface_*.qm >/dev/null 2>&1; then
	cp "${BUILD}"/openterface_*.qm "${PKG_ROOT}/usr/share/openterfaceQT/translations/" || true
fi

# Generate DEBIAN/control from template
CONTROL_TEMPLATE="${SRC}/packaging/debian/control"
CONTROL_FILE="${PKG_ROOT}/DEBIAN/control"
if [ -f "${CONTROL_TEMPLATE}" ]; then
	if command -v envsubst >/dev/null 2>&1; then
		VERSION="${VERSION}" ARCH="${ARCH}" envsubst < "${CONTROL_TEMPLATE}" > "${CONTROL_FILE}"
	else
		perl -pe 's/\$\{VERSION\}/'"${VERSION}"'/g; s/\$\{ARCH\}/'"${ARCH}"'/g' "${CONTROL_TEMPLATE}" > "${CONTROL_FILE}"
	fi
else
	cat > "${CONTROL_FILE}" <<EOF
Package: openterfaceQT
Version: ${VERSION}
Section: base
Priority: optional
Architecture: ${ARCH}
Depends: libxkbcommon0, libwayland-client0, libegl1, libgles2, libpulse0, libxcb1, libxcb-shm0, libxcb-xfixes0, libxcb-shape0, libx11-6, zlib1g, libbz2-1.0, liblzma5, libva2, libva-drm2, libva-x11-2, libvdpau1, liborc-0.4-0, libgstreamer1.0-0, libv4l-0, libgl1, libglx0, libglvnd0
Maintainer: TechxArtisan <info@techxartisan.com>
Description: OpenterfaceQT Mini-KVM Linux Edition
 Includes bundled FFmpeg 6.1.1 libraries (libavformat, libavcodec,
 libavdevice, libswresample, libswscale, libavutil, libavfilter), libturbojpeg,
 VA-API libraries (libva, libva-drm, libva-x11), VDPAU library (libvdpau),
 ORC library (liborc-0.4), and GStreamer base libraries (libgstbase, libgstaudio,
 libgstvideo, libgstapp, libgstpbutils, libgsttag, libgstrtp, libgstrtsp, libgstsdp,
 libgstallocators, libgstgl)
EOF
fi

# Copy preinst, postinst and postrm scripts if they exist
if [ -f "${SRC}/packaging/debian/preinst" ]; then
	install -m 0755 "${SRC}/packaging/debian/preinst" "${PKG_ROOT}/DEBIAN/preinst"
	echo "✅ preinst script installed"
else
	echo "⚠️  preinst script not found at ${SRC}/packaging/debian/preinst"
fi

if [ -f "${SRC}/packaging/debian/postinst" ]; then
	install -m 0755 "${SRC}/packaging/debian/postinst" "${PKG_ROOT}/DEBIAN/postinst"
	echo "✅ postinst script installed"
else
	echo "⚠️  postinst script not found at ${SRC}/packaging/debian/postinst"
fi

if [ -f "${SRC}/packaging/debian/postrm" ]; then
	install -m 0755 "${SRC}/packaging/debian/postrm" "${PKG_ROOT}/DEBIAN/postrm"
	echo "✅ postrm script installed"
else
	echo "⚠️  postrm script not found at ${SRC}/packaging/debian/postrm"
fi

# Build the .deb
DEB_NAME="openterfaceQT_${VERSION}_${ARCH}.deb"
echo "Building Debian package: ${DEB_NAME}"
dpkg-deb --build "${PKG_ROOT}" "${PKG_OUT}/${DEB_NAME}"
echo "Debian package created at ${PKG_OUT}/${DEB_NAME}"
