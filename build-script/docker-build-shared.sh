#!/bin/bash
set -e

# Wrapper for shared (dynamic) build packaging
export OPENTERFACE_BUILD_STATIC=OFF
export USE_GSTREAMER_STATIC_PLUGINS=OFF

# Allow optional overrides via environment
# - DEB_DEPENDS: override Debian Depends string
# - SKIP_APPIMAGE=1 to skip AppImage packaging

# Check for pre-downloaded AppImage runtime
echo "Checking AppImage build environment..."
case "$(uname -m)" in
    x86_64) APPIMAGE_ARCH=x86_64;;
    aarch64) APPIMAGE_ARCH=aarch64;;
    armv7l) APPIMAGE_ARCH=armhf;;
    *) APPIMAGE_ARCH=x86_64;;
esac

DOCKER_RUNTIME_FILE="/opt/appimage-runtime/runtime-${APPIMAGE_ARCH}"

if [ -f "${DOCKER_RUNTIME_FILE}" ]; then
    echo "✓ Pre-downloaded AppImage runtime found: ${DOCKER_RUNTIME_FILE}"
    ls -lh "${DOCKER_RUNTIME_FILE}"
else
    echo "⚠ Pre-downloaded AppImage runtime not found at: ${DOCKER_RUNTIME_FILE}"
    echo "Will pre-download runtime to optimize build process..."
    
    # Pre-download the runtime to avoid network downloads during AppImage creation
    mkdir -p /opt/appimage-runtime
    RUNTIME_URL="https://github.com/AppImage/type2-runtime/releases/download/continuous/runtime-${APPIMAGE_ARCH}"
    echo "Downloading runtime from: ${RUNTIME_URL}"
    
    if command -v curl >/dev/null 2>&1; then
        if curl -fL "${RUNTIME_URL}" -o "${DOCKER_RUNTIME_FILE}"; then
            chmod +x "${DOCKER_RUNTIME_FILE}"
            echo "✓ Runtime downloaded successfully: ${DOCKER_RUNTIME_FILE}"
            ls -lh "${DOCKER_RUNTIME_FILE}"
            
            # Cache for future builds if in Jenkins
            if [ -n "${CACHE_DIR}" ]; then
                cp "${DOCKER_RUNTIME_FILE}" "${CACHED_RUNTIME}"
                echo "✓ Runtime cached for future builds: ${CACHED_RUNTIME}"
            fi
        else
            echo "⚠ Failed to download runtime, build will download it automatically"
        fi
    elif command -v wget >/dev/null 2>&1; then
        if wget -qO "${DOCKER_RUNTIME_FILE}" "${RUNTIME_URL}"; then
            chmod +x "${DOCKER_RUNTIME_FILE}"
            echo "✓ Runtime downloaded successfully: ${DOCKER_RUNTIME_FILE}"
            ls -lh "${DOCKER_RUNTIME_FILE}"
            
            # Cache for future builds if in Jenkins
            if [ -n "${CACHE_DIR}" ]; then
                cp "${DOCKER_RUNTIME_FILE}" "${CACHED_RUNTIME}"
                echo "✓ Runtime cached for future builds: ${CACHED_RUNTIME}"
            fi
        else
            echo "⚠ Failed to download runtime, build will download it automatically"
        fi
    else
        echo "⚠ Neither curl nor wget found for downloading runtime"
    fi
fi

# Check for linuxdeploy tools
if command -v linuxdeploy >/dev/null 2>&1; then
    echo "✓ linuxdeploy found: $(command -v linuxdeploy)"
else
    echo "⚠ linuxdeploy not found, will be downloaded during build"
fi

if command -v linuxdeploy-plugin-qt >/dev/null 2>&1; then
    echo "✓ linuxdeploy-plugin-qt found: $(command -v linuxdeploy-plugin-qt)"
else
    echo "⚠ linuxdeploy-plugin-qt not found, will be downloaded during build"
fi

# Run the main build + deb + AppImage
bash /workspace/src/build-script/docker-build.sh

bash /workspace/src/build-script/docker-build-appimage.sh

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
	install -m 0755 "${BUILD}/openterfaceQT" "${PKG_ROOT}/usr/local/bin/openterfaceQT"
else
	echo "Error: built binary not found at ${BUILD}/openterfaceQT" >&2
	exit 1
fi

# Install patchelf for rpath manipulation
apt update && apt install -y patchelf

# Copy Qt libraries to bundle them in the deb
QT_LIB_DIR="/opt/Qt6/lib"
if [ ! -d "${QT_LIB_DIR}" ]; then
    # Fallback to system Qt6 libraries if custom build not found
    QT_LIB_DIR="/usr/lib/x86_64-linux-gnu"
fi
if [ ! -d "${QT_LIB_DIR}" ]; then
    # Try alternative location
    QT_LIB_DIR="/usr/lib"
fi

if [ -d "${QT_LIB_DIR}" ]; then
    mkdir -p "${PKG_ROOT}/usr/lib"
    echo "Copying Qt libraries from ${QT_LIB_DIR}..."
    # Copy only Qt6 libraries (not all system libraries)
    find "${QT_LIB_DIR}" -maxdepth 1 -name "libQt6*.so*" -exec cp -a {} "${PKG_ROOT}/usr/lib/openterfaceqt/qt6" \; 2>/dev/null || true
    echo "✅ Qt libraries copied successfully"
else
    echo "⚠️  Warning: Qt library directory not found"
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

# Copy core FFmpeg libraries (libavdevice, libavcodec, libavformat, libavutil, libswscale, libswresample)
echo "📋 DEB: Searching for FFmpeg core libraries..."
FFMPEG_FOUND=0
FFMPEG_LIBS=(libavdevice.so libavcodec.so libavformat.so libavutil.so libswscale.so libswresample.so)

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

# Define all GStreamer libraries to bundle (core + base + plugins-base)
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

# Copy v4l-utils libraries - search multiple locations
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
    "libgstreamer-1.0.so"
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

# Copy Qt plugins
QT_PLUGIN_DIR="/opt/Qt6/plugins"
if [ ! -d "${QT_PLUGIN_DIR}" ]; then
    # Fallback to system Qt6 plugins if custom build not found
    QT_PLUGIN_DIR="/usr/lib/x86_64-linux-gnu/qt6/plugins"
fi
if [ ! -d "${QT_PLUGIN_DIR}" ]; then
    # Try alternative location
    QT_PLUGIN_DIR="/usr/lib/qt6/plugins"
fi

if [ -d "${QT_PLUGIN_DIR}" ]; then
    mkdir -p "${PKG_ROOT}/usr/lib/openterfaceqt/qt6/plugins"
    echo "Copying Qt plugins from ${QT_PLUGIN_DIR}..."
    cp -ra "${QT_PLUGIN_DIR}"/* "${PKG_ROOT}/usr/lib/openterfaceqt/qt6/plugins/" 2>/dev/null || true
    echo "✅ Qt plugins copied successfully"
else
    echo "⚠️  Warning: Qt plugin directory not found at ${QT_PLUGIN_DIR}"
fi

# Copy Qt QML imports
QT_QML_DIR="/opt/Qt6/qml"
if [ -d "${QT_QML_DIR}" ]; then
    mkdir -p "${PKG_ROOT}/usr/lib/openterfaceqt/qt6/qml"
    echo "Copying Qt QML imports..."
    cp -ra "${QT_QML_DIR}"/* "${PKG_ROOT}/usr/lib/openterfaceqt/qt6/qml/" 2>/dev/null || true
fi

# Update the binary's rpath to point to bundled libraries
# Libraries are bundled in /usr/lib/openterfaceqt (isolated from system libraries)
# Binary is at: /usr/local/bin/openterfaceQT
# Libraries are at: /usr/lib/openterfaceqt/
# So RPATH should be: /usr/lib/openterfaceqt (direct path to bundled libraries)
if [ -f "${PKG_ROOT}/usr/local/bin/openterfaceQT" ]; then
    echo "🔧 Updating rpath for bundled libraries..."
    echo "   Setting RPATH to: /usr/lib/openterfaceqt"
    if patchelf --set-rpath '/usr/lib/openterfaceqt' "${PKG_ROOT}/usr/local/bin/openterfaceQT"; then
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
Depends: libxkbcommon0, libwayland-client0, libegl1, libgles2, libpulse0, libxcb1, libxcb-shm0, libxcb-xfixes0, libxcb-shape0, libx11-6, zlib1g, libbz2-1.0, liblzma5, libva2, libva-drm2, libva-x11-2, libgstreamer1.0-0, libv4l-0, libgl1, libglx0, libglvnd0
Maintainer: TechxArtisan <info@techxartisan.com>
Description: OpenterfaceQT Mini-KVM Linux Edition
 Includes bundled FFmpeg 6.1.1 libraries (libavformat, libavcodec,
 libavdevice, libswresample, libswscale, libavutil), libturbojpeg,
 and GStreamer base libraries (libgstbase, libgstaudio, libgstvideo,
 libgstapp, libgstpbutils, libgsttag, libgstrtp, libgstrtsp, libgstsdp,
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

# =========================
# Build RPM package (.rpm)
# =========================
echo "Preparing RPM package..."

apt install -y rpm
if ! command -v rpmbuild >/dev/null 2>&1; then
	echo "Error: rpmbuild not found in the container. Please ensure 'rpm' is installed in the image." >&2
	exit 1
fi

SRC=/workspace/src
BUILD=/workspace/build
RPMTOP=/workspace/rpmbuild-shared

mkdir -p "${RPMTOP}/SPECS" "${RPMTOP}/SOURCES" "${RPMTOP}/BUILD" "${RPMTOP}/RPMS" "${RPMTOP}/SRPMS"

## VERSION and ARCH already computed above for .deb; reuse here

case "${ARCH}" in
	amd64|x86_64) RPM_ARCH=x86_64;;
	arm64|aarch64) RPM_ARCH=aarch64;;
	armhf|armv7l) RPM_ARCH=armv7hl;;
	*) RPM_ARCH=${ARCH};;
esac

# Copy build output and resources to SOURCES
if [ ! -f "${BUILD}/openterfaceQT" ]; then
	echo "Error: built binary not found at ${BUILD}/openterfaceQT" >&2
	exit 1
fi
cp "${BUILD}/openterfaceQT" "${RPMTOP}/SOURCES/"

# Install patchelf for rpath manipulation if not already installed
apt update && apt install -y patchelf

# Copy Qt libraries to SOURCES for bundling
QT_LIB_DIR="/opt/Qt6/lib"
if [ -d "${QT_LIB_DIR}" ]; then
    echo "Copying Qt libraries to SOURCES..."
    cp -a "${QT_LIB_DIR}"/libQt6*.so* "${RPMTOP}/SOURCES/" 2>/dev/null || true
fi

# Copy Qt plugins to SOURCES
QT_PLUGIN_DIR="/opt/Qt6/plugins"
if [ -d "${QT_PLUGIN_DIR}" ]; then
    mkdir -p "${RPMTOP}/SOURCES/qt6-plugins"
    echo "Copying Qt plugins to SOURCES..."
    cp -ra "${QT_PLUGIN_DIR}"/* "${RPMTOP}/SOURCES/qt6-plugins/" 2>/dev/null || true
fi

# Copy Qt QML imports to SOURCES
QT_QML_DIR="/opt/Qt6/qml"
if [ -d "${QT_QML_DIR}" ]; then
    mkdir -p "${RPMTOP}/SOURCES/qt6-qml"
    echo "Copying Qt QML imports to SOURCES..."
    cp -ra "${QT_QML_DIR}"/* "${RPMTOP}/SOURCES/qt6-qml/" 2>/dev/null || true
fi

# Copy libjpeg and libturbojpeg libraries to SOURCES for bundling
echo "🔍 Searching for JPEG libraries to RPM SOURCES..."

# Copy libjpeg libraries - search multiple locations
echo "📋 RPM: Searching for libjpeg libraries..."
JPEG_FOUND=0
for SEARCH_DIR in /opt/ffmpeg/lib /usr/lib/x86_64-linux-gnu /usr/lib; do
    echo "   Checking: $SEARCH_DIR"
    if [ -d "$SEARCH_DIR" ]; then
        if ls "$SEARCH_DIR"/libjpeg.so* >/dev/null 2>&1; then
            echo "   ✅ Found libjpeg in $SEARCH_DIR"
            JPEG_FILES=$(ls -la "$SEARCH_DIR"/libjpeg.so*)
            echo "   Files found:"
            echo "$JPEG_FILES" | sed 's/^/     /'
            cp -av "$SEARCH_DIR"/libjpeg.so* "${RPMTOP}/SOURCES/" 2>&1 | sed 's/^/     /'
            echo "   ✅ libjpeg libraries copied to ${RPMTOP}/SOURCES"
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
else
    echo "✅ libjpeg found and copied"
fi

# Copy libturbojpeg libraries - search multiple locations
echo "📋 RPM: Searching for libturbojpeg libraries..."
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
            cp -Pv "$SEARCH_DIR"/libturbojpeg.so* "${RPMTOP}/SOURCES/" 2>&1 | sed 's/^/     /'
            echo "   ✅ libturbojpeg libraries copied to ${RPMTOP}/SOURCES"
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
else
    echo "✅ libturbojpeg found and copied"
fi

# Copy VA-API libraries for hardware acceleration - search multiple locations
echo "📋 RPM: Searching for VA-API libraries..."
VA_FOUND=0
for SEARCH_DIR in /usr/lib/x86_64-linux-gnu /usr/lib; do
    echo "   Checking: $SEARCH_DIR"
    if [ -d "$SEARCH_DIR" ]; then
        if ls "$SEARCH_DIR"/libva.so* >/dev/null 2>&1; then
            echo "   ✅ Found VA-API libraries in $SEARCH_DIR"
            VA_FILES=$(ls -la "$SEARCH_DIR"/libva*.so*)
            echo "   Files found:"
            echo "$VA_FILES" | sed 's/^/     /'
            cp -av "$SEARCH_DIR"/libva*.so* "${RPMTOP}/SOURCES/" 2>&1 | sed 's/^/     /'
            echo "   ✅ VA-API libraries copied to ${RPMTOP}/SOURCES"
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
else
    echo "✅ VA-API found and copied"
fi

# Copy core FFmpeg libraries (libavdevice, libavcodec, libavformat, libavutil, libswscale, libswresample)
echo "📋 RPM: Searching for FFmpeg core libraries..."
FFMPEG_FOUND=0
FFMPEG_LIBS=(libavdevice.so libavcodec.so libavformat.so libavutil.so libswscale.so libswresample.so)

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
                    cp -Pv "$SEARCH_DIR"/${ffmpeg_lib}* "${RPMTOP}/SOURCES/" 2>&1 | sed 's/^/     /'
                fi
            done
            echo "   ✅ FFmpeg core libraries copied to ${RPMTOP}/SOURCES"
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

# Copy GStreamer libraries - search multiple locations
echo "📋 RPM: Searching for GStreamer libraries..."
GSTREAMER_FOUND=0
for SEARCH_DIR in /usr/lib/x86_64-linux-gnu /usr/lib; do
    echo "   Checking: $SEARCH_DIR"
    if [ -d "$SEARCH_DIR" ]; then
        if ls "$SEARCH_DIR"/libgstreamer*.so* >/dev/null 2>&1; then
            echo "   ✅ Found GStreamer libraries in $SEARCH_DIR"
            GSTREAMER_FILES=$(ls -la "$SEARCH_DIR"/libgstreamer*.so*)
            echo "   Files found:"
            echo "$GSTREAMER_FILES" | sed 's/^/     /'
            cp -av "$SEARCH_DIR"/libgstreamer*.so* "${RPMTOP}/SOURCES/" 2>&1 | sed 's/^/     /'
            echo "   ✅ GStreamer libraries copied to ${RPMTOP}/SOURCES"
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
    echo "✅ GStreamer found and copied"
fi

# Copy v4l-utils libraries - search multiple locations
echo "📋 RPM: Searching for v4l-utils libraries..."
V4L_FOUND=0
for SEARCH_DIR in /usr/lib/x86_64-linux-gnu /usr/lib; do
    echo "   Checking: $SEARCH_DIR"
    if [ -d "$SEARCH_DIR" ]; then
        if ls "$SEARCH_DIR"/libv4l*.so* >/dev/null 2>&1; then
            echo "   ✅ Found v4l-utils libraries in $SEARCH_DIR"
            V4L_FILES=$(ls -la "$SEARCH_DIR"/libv4l*.so*)
            echo "   Files found:"
            echo "$V4L_FILES" | sed 's/^/     /'
            cp -av "$SEARCH_DIR"/libv4l*.so* "${RPMTOP}/SOURCES/" 2>&1 | sed 's/^/     /'
            echo "   ✅ v4l-utils libraries copied to ${RPMTOP}/SOURCES"
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
else
    echo "✅ v4l-utils found and copied"
fi

# Copy GStreamer video libraries to RPM SOURCES - search multiple locations
echo "📋 RPM: Searching for GStreamer video libraries..."
GSTVIDEO_FOUND=0
for SEARCH_DIR in /usr/lib/x86_64-linux-gnu /usr/lib; do
    echo "   Checking: $SEARCH_DIR"
    if [ -d "$SEARCH_DIR" ]; then
        # Use find to detect both files and symlinks
        if find "$SEARCH_DIR" -maxdepth 1 \( -name "libgstvideo-1.0.so*" -type f -o -name "libgstvideo-1.0.so*" -type l \) 2>/dev/null | grep -q .; then
            echo "   ✅ Found GStreamer video libraries in $SEARCH_DIR"
            # List files found for diagnostics
            ls -la "$SEARCH_DIR"/libgstvideo-1.0.so* 2>/dev/null | while read -r line; do
                echo "     $line"
            done
            # Copy all libgstvideo-1.0 files and symlinks
            cp -Pv "$SEARCH_DIR"/libgstvideo-1.0.so* "${RPMTOP}/SOURCES/" 2>&1 | sed 's/^/     /'
            echo "   ✅ GStreamer video libraries copied to ${RPMTOP}/SOURCES"
            GSTVIDEO_FOUND=1
            break
        else
            echo "   ✗ No GStreamer video libraries found in $SEARCH_DIR"
        fi
    else
        echo "   ✗ Directory does not exist: $SEARCH_DIR"
    fi
done
if [ $GSTVIDEO_FOUND -eq 0 ]; then
    echo "⚠️  Warning: GStreamer video libraries not found"
else
    echo "✅ GStreamer video libraries found and copied"
fi

# Copy GStreamer app libraries to RPM SOURCES - search multiple locations
echo "📋 RPM: Searching for GStreamer app libraries..."
GSTAPP_FOUND=0
for SEARCH_DIR in /usr/lib/x86_64-linux-gnu /usr/lib; do
    echo "   Checking: $SEARCH_DIR"
    if [ -d "$SEARCH_DIR" ]; then
        # Use find to detect both files and symlinks
        if find "$SEARCH_DIR" -maxdepth 1 \( -name "libgstapp-1.0.so*" -type f -o -name "libgstapp-1.0.so*" -type l \) 2>/dev/null | grep -q .; then
            echo "   ✅ Found GStreamer app libraries in $SEARCH_DIR"
            # List files found for diagnostics
            ls -la "$SEARCH_DIR"/libgstapp-1.0.so* 2>/dev/null | while read -r line; do
                echo "     $line"
            done
            # Copy all libgstapp-1.0 files and symlinks
            cp -Pv "$SEARCH_DIR"/libgstapp-1.0.so* "${RPMTOP}/SOURCES/" 2>&1 | sed 's/^/     /'
            echo "   ✅ GStreamer app libraries copied to ${RPMTOP}/SOURCES"
            GSTAPP_FOUND=1
            break
        else
            echo "   ✗ No GStreamer app libraries found in $SEARCH_DIR"
        fi
    else
        echo "   ✗ Directory does not exist: $SEARCH_DIR"
    fi
done
if [ $GSTAPP_FOUND -eq 0 ]; then
    echo "⚠️  Warning: GStreamer app libraries not found"
else
    echo "✅ GStreamer app libraries found and copied"
fi

echo "📋 RPM: Final SOURCES directory contents:"
ls -lah "${RPMTOP}/SOURCES/" | head -30

# Update the binary's rpath to point to /usr/lib/openterfaceqt for RPM
if [ -f "${RPMTOP}/SOURCES/openterfaceQT" ]; then
    echo "Updating rpath for RPM bundled libraries..."
    patchelf --set-rpath '/usr/lib/openterfaceqt' "${RPMTOP}/SOURCES/openterfaceQT"
fi

# Generate spec from template
SPEC_TEMPLATE="${SRC}/packaging/rpm/spec"
SPEC_OUT="${RPMTOP}/SPECS/openterfaceqt.spec"
if [ -f "${SPEC_TEMPLATE}" ]; then
	sed -e "s/\${VERSION}/${VERSION}/g" \
			-e "s/\${ARCH}/${RPM_ARCH}/g" \
			"${SPEC_TEMPLATE}" > "${SPEC_OUT}"
else
	echo "Error: RPM spec template not found at ${SPEC_TEMPLATE}" >&2
	exit 1
fi

# Try to copy icon if available
if [ -f "${SRC}/images/icon_256.png" ]; then
	cp "${SRC}/images/icon_256.png" "${RPMTOP}/SOURCES/"
fi

# Build the RPM
rpmbuild --define "_topdir ${RPMTOP}" -bb "${SPEC_OUT}"

# Move resulting RPM to build output with normalized name
RPM_OUT_NAME="openterfaceQT_${VERSION}_${RPM_ARCH}.rpm"
FOUND_RPM=$(find "${RPMTOP}/RPMS" -name "*.rpm" -type f | head -n1 || true)
if [ -n "${FOUND_RPM}" ]; then
	mv "${FOUND_RPM}" "${BUILD}/${RPM_OUT_NAME}"
	echo "RPM package created at ${BUILD}/${RPM_OUT_NAME}"
else
	echo "Error: RPM build did not produce an output." >&2
	exit 1
fi
