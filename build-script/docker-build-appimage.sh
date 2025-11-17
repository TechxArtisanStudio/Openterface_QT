#!/bin/bash

set -uo pipefail
IFS=$'\n\t'

# Disable exit-on-error for non-critical operations
# Re-enable with "set -e" when needed
ORIGINAL_OPTS="$-"

APPIMAGE_DIR="${BUILD_DIR}/appimage"
# Set up variables for comprehensive AppImage creation
SRC="/workspace/src"
BUILD="/workspace/build" 
APPDIR="${APPIMAGE_DIR}/AppDir"
DESKTOP_OUT="${APPDIR}/openterfaceqt.desktop"
APPIMAGE_OUT="${BUILD}"
# Create comprehensive AppDir structure for the final AppImage
mkdir -p "${APPDIR}/usr/bin" "${APPDIR}/usr/lib" "${APPDIR}/usr/share/applications" "${APPDIR}/usr/share/pixmaps"

# Enhanced AppImage build script with comprehensive GStreamer plugin support
# This script builds an AppImage with all necessary GStreamer plugins for video capture

echo "Building Enhanced Openterface AppImage with GStreamer plugins..."

# Determine architecture
ARCH=$(uname -m)
case "${ARCH}" in
    aarch64|arm64) APPIMAGE_ARCH=aarch64;;
    x86_64|amd64) APPIMAGE_ARCH=x86_64;;
    *) APPIMAGE_ARCH=${ARCH};;
esac

# Create build directory
mkdir -p "${BUILD_DIR}" "${APPIMAGE_DIR}"

# ============================================================
# Generic library copying function (from docker-build-deb.sh)
# ============================================================
# This function eliminates code duplication for library copying
# Usage: copy_libraries "VARNAME" "Display Name" "lib_pattern" "ERROR|WARNING" "target_dir" "search_dir1" "search_dir2" ...
copy_libraries() {
    local var_name="$1"
    local display_name="$2"
    local lib_pattern="$3"
    local severity="$4"  # ERROR or WARNING
    local target_dir="$5"
    shift 5
    local search_dirs=("$@")
    
    echo "📋 APPIMAGE: Searching for ${display_name} libraries..."
    local found=0
    
    for search_dir in "${search_dirs[@]}"; do
        echo "   Checking: $search_dir"
        if [ -d "$search_dir" ]; then
            if ls "$search_dir"/${lib_pattern}* >/dev/null 2>&1; then
                echo "   ✅ Found ${display_name} in $search_dir"
                local files=$(ls -la "$search_dir"/${lib_pattern}* 2>/dev/null)
                echo "   Files found:"
                echo "$files" | sed 's/^/     /'
                # Copy both actual files AND symlinks to preserve library versioning chains
                find "$search_dir" -maxdepth 1 -name "${lib_pattern}*" \( -type f -o -type l \) -exec cp -avP {} "${target_dir}/" \; 2>&1 | sed 's/^/     /'
                echo "   ✅ ${display_name} libraries copied to ${target_dir}"
                found=1
                break
            else
                echo "   ✗ No ${display_name} found in $search_dir"
            fi
        else
            echo "   ✗ Directory does not exist: $search_dir"
        fi
    done
    
    if [ $found -eq 0 ]; then
        if [ "$severity" = "ERROR" ]; then
            echo "❌ ERROR: ${display_name} libraries not found in any search path!"
        else
            echo "⚠️  Warning: ${display_name} libraries not found"
        fi
    else
        echo "✅ ${display_name} found and copied"
    fi
    
    # Export result as a variable (e.g., GSTREAMER_FOUND=1)
    eval "${var_name}_FOUND=$found"
}

echo "Detecting GStreamer plugin directories..."

# Detect GStreamer plugins in container
GSTREAMER_HOST_DIRS=(
	"/opt/gstreamer/lib/${ARCH}-linux-gnu/gstreamer-1.0"
	"/opt/gstreamer/lib/${ARCH}-linux-gnu/"
    "/usr/lib/${ARCH}-linux-gnu/gstreamer-1.0"
    "/usr/lib/gstreamer-1.0"
)

GSTREAMER_HOST_DIR=""
for dir in "${GSTREAMER_HOST_DIRS[@]}"; do
    if [ -d "$dir" ]; then
        GSTREAMER_HOST_DIR="$dir"
        echo "✅ Found GStreamer plugin directory: $GSTREAMER_HOST_DIR"
        break
    fi
done

if [ -z "$GSTREAMER_HOST_DIR" ] || [ ! -f "$GSTREAMER_HOST_DIR/libgstvideo4linux2.so" ]; then
    echo "❌ GStreamer V4L2 plugin not found in container!"
    echo "Installing GStreamer plugins in container..."
    apt update && apt install -y gstreamer1.0-plugins-base gstreamer1.0-plugins-good gstreamer1.0-plugins-bad
    
    # Retry detection after installation
    for dir in "${GSTREAMER_HOST_DIRS[@]}"; do
        if [ -d "$dir" ] && [ -f "$dir/libgstvideo4linux2.so" ]; then
            GSTREAMER_HOST_DIR="$dir"
            echo "✅ Found GStreamer plugins after installation: $GSTREAMER_HOST_DIR"
            break
        fi
    done
    
    if [ -z "$GSTREAMER_HOST_DIR" ]; then
        echo "❌ Still no GStreamer plugins found after installation!"
        exit 1
    fi
fi

# Determine version from resources/version.h
VERSION_H="/workspace/src/resources/version.h"
if [ -f "${VERSION_H}" ]; then
    VERSION=$(grep '^#define APP_VERSION' "${VERSION_H}" | grep -oE '[0-9]+(\.[0-9]+)*' | head -n1)
fi
if [ -z "${VERSION}" ]; then
    VERSION="0.4.3.248"
fi

echo "Building AppImage version: ${VERSION}"

echo "🎯 Creating AppImage with essential GStreamer plugins..."

# Create enhanced AppImage
cd /workspace
rm -rf appimage/AppDir
mkdir -p appimage/AppDir/usr/bin appimage/AppDir/usr/lib/gstreamer-1.0 appimage/AppDir/usr/share/applications

echo '✅ Executable copied'
cp build/openterfaceQT appimage/AppDir/usr/bin/
chmod +x appimage/AppDir/usr/bin/openterfaceQT

echo '📦 Copying critical GLIBC libraries for compatibility...'
# Create target directories for initial AppDir
mkdir -p "appimage/AppDir/usr/lib"

# ============================================================
# Define unified library copying configurations
# Format: variable_name|display_name|lib_pattern|severity|target_subdir|search_dirs...
# target_subdir: "" (root) or subdirectory path
# ============================================================

# Merged comprehensive AppImage library configurations
# Format: variable_name|display_name|lib_pattern|severity|target_subdir|search_dirs...
# Combines initial pass and comprehensive pass into single unified array
declare -a APPIMAGE_LIBRARY_CONFIGS=(
    # Core GLIBC libraries
    "GLIBC|GLIBC|libc.so|WARNING||/usr/lib/x86_64-linux-gnu /lib64 /lib /usr/lib"
    "GLIBC_LIBM|libm|libm.so|WARNING||/usr/lib/x86_64-linux-gnu /lib64 /lib /usr/lib"
    "GLIBC_PTHREAD|libpthread|libpthread.so|WARNING||/usr/lib/x86_64-linux-gnu /lib64 /lib /usr/lib"
    "GLIBC_DL|libdl|libdl.so|WARNING||/usr/lib/x86_64-linux-gnu /lib64 /lib /usr/lib"
    "GLIBC_RT|librt|librt.so|WARNING||/usr/lib/x86_64-linux-gnu /lib64 /lib /usr/lib"
    "GLIBC_NSS|libnss|libnss*.so|WARNING||/usr/lib/x86_64-linux-gnu /lib64 /lib /usr/lib"
    "GLIBC_RESOLV|libresolv|libresolv.so|WARNING||/usr/lib/x86_64-linux-gnu /lib64 /lib /usr/lib"
    "GLIBC_CRYPT|libcrypt|libcrypt.so|WARNING||/usr/lib/x86_64-linux-gnu /lib64 /lib /usr/lib"
    "GLIBC_UTIL|libutil|libutil.so|WARNING||/usr/lib/x86_64-linux-gnu /lib64 /lib /usr/lib"
    
    # C++ runtime libraries (CRITICAL for Qt/C++ applications)
    "GCC_S|libgcc_s|libgcc_s.so|ERROR||/usr/lib/x86_64-linux-gnu /lib/x86_64-linux-gnu /lib64 /lib /usr/lib"
    "STDCXX|libstdc++|libstdc++.so|ERROR||/usr/lib/x86_64-linux-gnu /lib/x86_64-linux-gnu /lib64 /lib /usr/lib"
    
    # Critical system libraries (libusb, libdrm, libudev)
    "LIBUSB|libusb|libusb*.so|ERROR||/opt/ffmpeg/lib /opt /usr/lib/x86_64-linux-gnu /usr/lib /lib/x86_64-linux-gnu /lib"
    "LIBDRM|libdrm|libdrm.so|WARNING||/usr/lib/x86_64-linux-gnu /usr/lib /lib/x86_64-linux-gnu /lib"
    "LIBUDEV|libudev|libudev.so|WARNING||/usr/lib/x86_64-linux-gnu /usr/lib /lib/x86_64-linux-gnu /lib"
    
    # Coreutils support
    "STDBUF|libstdbuf|libstdbuf.so|WARNING||/usr/libexec/coreutils /opt /usr/lib/x86_64-linux-gnu /usr/lib /lib/x86_64-linux-gnu /lib"
    
    # JPEG libraries
    "JPEG|libjpeg|libjpeg.so|WARNING||/opt/ffmpeg/lib /usr/lib/x86_64-linux-gnu /usr/lib"
    "TURBOJPEG|libturbojpeg|libturbojpeg.so|WARNING||/opt/ffmpeg/lib /usr/lib/x86_64-linux-gnu /usr/lib"
    
    # Compression libraries
    "BZ2|libbz2|libbz2.so|WARNING||/usr/lib/x86_64-linux-gnu /usr/lib /lib"
    
    # EGL and GPU rendering libraries (with wildcard patterns to match all versions)
    "EGL|libEGL|libEGL.so|WARNING||/usr/lib/x86_64-linux-gnu /usr/lib /usr/lib64 /lib"
    "GL|libGL|libGL.so|WARNING||/usr/lib/x86_64-linux-gnu /usr/lib /usr/lib64 /lib"
    "GLX|libGLX|libGLX.so|WARNING||/usr/lib/x86_64-linux-gnu /usr/lib /usr/lib64 /lib"
    "GLESV2|libGLESv2|libGLESv2.so|WARNING||/usr/lib/x86_64-linux-gnu /usr/lib /usr/lib64 /lib"
    "GLVND|libglvnd|libglvnd.so|WARNING||/usr/lib/x86_64-linux-gnu /usr/lib /usr/lib64 /lib"
    "GLDISPATCH|libGLdispatch|libGLdispatch.so|WARNING||/usr/lib/x86_64-linux-gnu /usr/lib /usr/lib64 /lib"
    "OPENGL|libOpenGL|libOpenGL.so|WARNING||/usr/lib/x86_64-linux-gnu /usr/lib /usr/lib64 /lib"
    
    # Qt platform plugins (CRITICAL for GUI applications)
    "QTPLUGIN_XCB|Qt6 XCB platform|libqxcb.so|ERROR|qt6/plugins/platforms|/opt/Qt6/plugins/platforms /usr/lib/qt6/plugins/platforms /usr/lib/x86_64-linux-gnu/qt6/plugins/platforms"
    "QTPLUGIN_WAYLAND_EGL|Qt6 Wayland EGL|libqwayland-egl.so|WARNING|qt6/plugins/platforms|/opt/Qt6/plugins/platforms /usr/lib/qt6/plugins/platforms /usr/lib/x86_64-linux-gnu/qt6/plugins/platforms"
    "QTPLUGIN_WAYLAND_GENERIC|Qt6 Wayland Generic|libqwayland-generic.so|WARNING|qt6/plugins/platforms|/opt/Qt6/plugins/platforms /usr/lib/qt6/plugins/platforms /usr/lib/x86_64-linux-gnu/qt6/plugins/platforms"
    "QTPLUGIN_OFFSCREEN|Qt6 Offscreen|libqoffscreen.so|WARNING|qt6/plugins/platforms|/opt/Qt6/plugins/platforms /usr/lib/qt6/plugins/platforms /usr/lib/x86_64-linux-gnu/qt6/plugins/platforms"
    "QTPLUGIN_MINIMAL|Qt6 Minimal|libqminimal.so|WARNING|qt6/plugins/platforms|/opt/Qt6/plugins/platforms /usr/lib/qt6/plugins/platforms /usr/lib/x86_64-linux-gnu/qt6/plugins/platforms"
    
    # Wayland dependencies (required for wayland plugin)
    "WAYLAND_CLIENT|libwayland-client|libwayland-client.so|WARNING||/usr/lib/x86_64-linux-gnu /usr/lib /lib/x86_64-linux-gnu /lib"
    "WAYLAND_CURSOR|libwayland-cursor|libwayland-cursor.so|WARNING||/usr/lib/x86_64-linux-gnu /usr/lib /lib/x86_64-linux-gnu /lib"
    "WAYLAND_EGL|libwayland-egl|libwayland-egl.so|WARNING||/usr/lib/x86_64-linux-gnu /usr/lib /lib/x86_64-linux-gnu /lib"
    "XKBCOMMON|libxkbcommon|libxkbcommon.so|WARNING||/usr/lib/x86_64-linux-gnu /usr/lib /lib/x86_64-linux-gnu /lib"
    
    # XCB dependencies (required by xcb plugin)
    "XCB_CURSOR|libxcb-cursor|libxcb-cursor.so|WARNING||/usr/lib/x86_64-linux-gnu /usr/lib /lib/x86_64-linux-gnu /lib"
    "XCB|libxcb|libxcb.so|WARNING||/usr/lib/x86_64-linux-gnu /usr/lib /lib/x86_64-linux-gnu /lib"
    
    # Qt platform plugin libraries (also as system libraries for backup copy)
    "QTLIB_MINIMAL|Qt6 Minimal plugin library|libqminimal.so|WARNING||/opt/Qt6/plugins/platforms /opt/Qt6/lib /usr/lib/qt6/plugins/platforms /usr/lib/x86_64-linux-gnu/qt6/plugins/platforms /usr/lib/x86_64-linux-gnu /usr/lib /lib/x86_64-linux-gnu /lib"
    "QTLIB_OFFSCREEN|Qt6 Offscreen plugin library|libqoffscreen.so|WARNING||/opt/Qt6/plugins/platforms /opt/Qt6/lib /usr/lib/qt6/plugins/platforms /usr/lib/x86_64-linux-gnu/qt6/plugins/platforms /usr/lib/x86_64-linux-gnu /usr/lib /lib/x86_64-linux-gnu /lib"
    "XCB|libxcb|libxcb.so|WARNING||/usr/lib/x86_64-linux-gnu /usr/lib /lib/x86_64-linux-gnu /lib"
    
    # Essential GStreamer plugins for video capture
    "GSTV4L2|GStreamer V4L2|libgstvideo4linux2.so|ERROR|gstreamer-1.0|/opt/gstreamer/lib/x86_64-linux-gnu/gstreamer-1.0 /opt/gstreamer/lib/gstreamer-1.0 /usr/lib/x86_64-linux-gnu/gstreamer-1.0 /usr/lib/gstreamer-1.0"
    "GSTV4L2CODECS|GStreamer V4L2 codecs|libgstv4l2codecs.so|WARNING|gstreamer-1.0|/opt/gstreamer/lib/x86_64-linux-gnu/gstreamer-1.0 /opt/gstreamer/lib/gstreamer-1.0 /usr/lib/x86_64-linux-gnu/gstreamer-1.0 /usr/lib/gstreamer-1.0"
    "GSTJPEG|GStreamer JPEG|libgstjpeg.so|WARNING|gstreamer-1.0|/opt/gstreamer/lib/x86_64-linux-gnu/gstreamer-1.0 /opt/gstreamer/lib/gstreamer-1.0 /usr/lib/x86_64-linux-gnu/gstreamer-1.0 /usr/lib/gstreamer-1.0"
    "GSTCOREELEMENTS|GStreamer core elements|libgstcoreelements.so|WARNING|gstreamer-1.0|/opt/gstreamer/lib/x86_64-linux-gnu/gstreamer-1.0 /opt/gstreamer/lib/gstreamer-1.0 /usr/lib/x86_64-linux-gnu/gstreamer-1.0 /usr/lib/gstreamer-1.0"
    "GSTAPP|GStreamer app|libgstapp.so|WARNING|gstreamer-1.0|/opt/gstreamer/lib/x86_64-linux-gnu/gstreamer-1.0 /opt/gstreamer/lib/gstreamer-1.0 /usr/lib/x86_64-linux-gnu/gstreamer-1.0 /usr/lib/gstreamer-1.0"
    "GSTPLAYBACK|GStreamer playback|libgstplayback.so|WARNING|gstreamer-1.0|/opt/gstreamer/lib/x86_64-linux-gnu/gstreamer-1.0 /opt/gstreamer/lib/gstreamer-1.0 /usr/lib/x86_64-linux-gnu/gstreamer-1.0 /usr/lib/gstreamer-1.0"
    "GSTVIDEOCONVERT|GStreamer video convert/scale|libgstvideoconvertscale.so|WARNING|gstreamer-1.0|/opt/gstreamer/lib/x86_64-linux-gnu/gstreamer-1.0 /opt/gstreamer/lib/gstreamer-1.0 /usr/lib/x86_64-linux-gnu/gstreamer-1.0 /usr/lib/gstreamer-1.0"
    "GSTVIDEORATE|GStreamer video rate|libgstvideorate.so|WARNING|gstreamer-1.0|/opt/gstreamer/lib/x86_64-linux-gnu/gstreamer-1.0 /opt/gstreamer/lib/gstreamer-1.0 /usr/lib/x86_64-linux-gnu/gstreamer-1.0 /usr/lib/gstreamer-1.0"
    "GSTXIMAGESINK|GStreamer X image sink|libgstximagesink.so|WARNING|gstreamer-1.0|/opt/gstreamer/lib/x86_64-linux-gnu/gstreamer-1.0 /opt/gstreamer/lib/gstreamer-1.0 /usr/lib/x86_64-linux-gnu/gstreamer-1.0 /usr/lib/gstreamer-1.0"
    "GSTXVIMAGESINK|GStreamer XV image sink|libgstxvimagesink.so|WARNING|gstreamer-1.0|/opt/gstreamer/lib/x86_64-linux-gnu/gstreamer-1.0 /opt/gstreamer/lib/gstreamer-1.0 /usr/lib/x86_64-linux-gnu/gstreamer-1.0 /usr/lib/gstreamer-1.0"
    "GSTPULSEAUDIO|GStreamer PulseAudio|libgstpulseaudio.so|WARNING|gstreamer-1.0|/opt/gstreamer/lib/x86_64-linux-gnu/gstreamer-1.0 /opt/gstreamer/lib/gstreamer-1.0 /usr/lib/x86_64-linux-gnu/gstreamer-1.0 /usr/lib/gstreamer-1.0"
    "GSTAUDIOPARSERS|GStreamer audio parsers|libgstaudioparsers.so|WARNING|gstreamer-1.0|/opt/gstreamer/lib/x86_64-linux-gnu/gstreamer-1.0 /opt/gstreamer/lib/gstreamer-1.0 /usr/lib/x86_64-linux-gnu/gstreamer-1.0 /usr/lib/gstreamer-1.0"
    "GSTAUDIOCONVERT|GStreamer audio convert|libgstaudioconvert.so|WARNING|gstreamer-1.0|/opt/gstreamer/lib/x86_64-linux-gnu/gstreamer-1.0 /opt/gstreamer/lib/gstreamer-1.0 /usr/lib/x86_64-linux-gnu/gstreamer-1.0 /usr/lib/gstreamer-1.0"
    "GSTAUDIORESAMPLE|GStreamer audio resample|libgstaudioresample.so|WARNING|gstreamer-1.0|/opt/gstreamer/lib/x86_64-linux-gnu/gstreamer-1.0 /opt/gstreamer/lib/gstreamer-1.0 /usr/lib/x86_64-linux-gnu/gstreamer-1.0 /usr/lib/gstreamer-1.0"
    "GSTAUTOPLUG|GStreamer autodetect|libgstautodetect.so|WARNING|gstreamer-1.0|/opt/gstreamer/lib/x86_64-linux-gnu/gstreamer-1.0 /opt/gstreamer/lib/gstreamer-1.0 /usr/lib/x86_64-linux-gnu/gstreamer-1.0 /usr/lib/gstreamer-1.0"
    "GSTTYPEFIND|GStreamer type find|libgsttypefindfunctions.so|WARNING|gstreamer-1.0|/opt/gstreamer/lib/x86_64-linux-gnu/gstreamer-1.0 /opt/gstreamer/lib/gstreamer-1.0 /usr/lib/x86_64-linux-gnu/gstreamer-1.0 /usr/lib/gstreamer-1.0"
    
    # System loader
    "LDLINUX|Linux dynamic linker|ld-linux-x86-64.so.2|WARNING||/usr/lib/x86_64-linux-gnu /lib64 /lib /usr/lib"
)

# Process merged AppImage library configurations for initial AppDir
echo "🔍 Copying required libraries to initial AppImage AppDir..."
mkdir -p "${APPDIR}/usr/lib"
for config in "${APPIMAGE_LIBRARY_CONFIGS[@]}"; do
    IFS='|' read -r var_name display_name lib_pattern severity target_subdir search_dirs_str <<< "$config"
    
    # Determine full target directory
    if [ -z "$target_subdir" ]; then
        target_dir="${APPDIR}/usr/lib"
    else
        target_dir="${APPDIR}/usr/lib/${target_subdir}"
        mkdir -p "$target_dir"
    fi
    
    # Split search directories (reset IFS to default for proper space splitting)
    IFS=' ' read -ra search_dirs <<< "$search_dirs_str"
    
    copy_libraries "$var_name" "$display_name" "$lib_pattern" "$severity" "$target_dir" "${search_dirs[@]}"
done

echo '✅ All GLIBC, critical, GPU, and GStreamer libraries copied'

# Diagnostic: Verify critical libraries were actually copied
echo ""
echo "🔍 Diagnostic: Verifying GPU libraries in appimage/AppDir/usr/lib..."
echo "   Checking for libEGL libraries:"
ls -1 appimage/AppDir/usr/lib/libEGL* 2>/dev/null || echo "   ⚠️  NO libEGL files found!"
echo "   Checking for libGL libraries:"
ls -1 appimage/AppDir/usr/lib/libGL* 2>/dev/null || echo "   ⚠️  NO libGL files found!"
echo "   Total .so files in appimage/AppDir/usr/lib:"
find appimage/AppDir/usr/lib -maxdepth 1 -name "*.so*" -type f -o -type l 2>/dev/null | wc -l
echo ""

# Try to find and copy icon
mkdir -p appimage/AppDir/usr/share/pixmaps
mkdir -p appimage/AppDir/usr/share/icons/hicolor/256x256/apps

ICON_FOUND=false
for icon_path in \
    '/workspace/src/images/icon_256.png' \
    '/workspace/src/images/icon_128.png' \
    '/workspace/src/resources/icons/openterfaceQT.png' \
    '/workspace/src/icons/openterface.png'; do
    if [ -f "$icon_path" ]; then
        cp "$icon_path" "appimage/AppDir/usr/share/icons/hicolor/256x256/apps/openterfaceqt.png"
        cp "$icon_path" "appimage/AppDir/usr/share/pixmaps/openterfaceqt.png"
        echo "✅ Icon copied from $icon_path"
        ICON_FOUND=true
        break
    fi
done

if [ "$ICON_FOUND" = false ]; then
    echo '⚠️ No icon found, creating placeholder...'
    # Create a minimal PNG placeholder using ImageMagick if available
    convert -size 64x64 xc:blue appimage/AppDir/usr/share/pixmaps/openterfaceqt.png 2>/dev/null || {
        echo 'No ImageMagick, using text placeholder...'
        echo 'OpenterfaceQT Icon' > appimage/AppDir/usr/share/pixmaps/openterfaceqt.txt
    }
fi

# Copy icon to root  
cp appimage/AppDir/usr/share/pixmaps/openterfaceqt.png appimage/AppDir/ 2>/dev/null || true

# Continue to comprehensive AppImage creation section with Docker runtime support
# (This section was simplified and moved to the end of the script for proper runtime handling)
echo "✅ Initial AppImage setup complete"
echo "✅ Proceeding to comprehensive AppImage creation with Docker runtime support"
cd /workspace

# Copy the executable to the comprehensive AppDir
cp "${BUILD}/openterfaceQT" "${APPDIR}/usr/bin/"
chmod +x "${APPDIR}/usr/bin/openterfaceQT"

# Create desktop file for comprehensive AppImage
cp "${SRC}/packaging/com.openterface.openterfaceQT.desktop" "${APPDIR}/usr/share/applications/openterfaceqt.desktop"

# Update icon name in desktop file for appimage (use simpler name for appimage compatibility)
sed -i 's/^Icon=.*/Icon=openterfaceQT/' "${APPDIR}/usr/share/applications/openterfaceqt.desktop"

# Copy desktop file to root of AppDir
cp "${APPDIR}/usr/share/applications/openterfaceqt.desktop" "${DESKTOP_OUT}"

# Try to find glibc version directory for proper loader setup
GLIBC_VERSION=$(ldd --version 2>/dev/null | head -1 | grep -oE '[0-9]+\.[0-9]+' | head -1 || echo "unknown")
echo "  ℹ️  Build environment glibc version: $GLIBC_VERSION"

echo "✅ All critical, comprehensive, and platform plugin libraries copied"
if [ -f "${SRC}/packaging/appimage/com.openterface.openterfaceQT.metainfo.xml" ]; then
	mkdir -p "${APPDIR}/usr/share/metainfo"
	cp "${SRC}/packaging/appimage/com.openterface.openterfaceQT.metainfo.xml" "${APPDIR}/usr/share/metainfo/"
fi

# Copy translations if present
if ls "${BUILD}"/openterface_*.qm >/dev/null 2>&1; then
	mkdir -p "${APPDIR}/usr/share/openterfaceQT/translations"
	cp "${BUILD}"/openterface_*.qm "${APPDIR}/usr/share/openterfaceQT/translations/" || true
fi

# Try to locate an icon and install it as openterfaceQT.png
ICON_SRC=""
for p in \
	"${SRC}/images/icon_256.png" \
	"${SRC}/images/icon_256.svg" \
	"${SRC}/images/icon_128.png" \
	"${SRC}/images/icon_64.png" \
	"${SRC}/images/icon_32.png" \
	"${SRC}/resources/icons/openterfaceQT.png" \
	"${SRC}/resources/icons/openterface.png" \
	"${SRC}/icons/openterfaceQT.png" \
	"${SRC}/icons/openterface.png" \
	"${SRC}"/openterface*.png \
	"${SRC}"/resources/*.png; do
	if [ -f "$p" ]; then ICON_SRC="$p"; break; fi
done
if [ -n "${ICON_SRC}" ]; then
	ICON_EXT="${ICON_SRC##*.}"
	if [ "${ICON_EXT}" = "svg" ]; then
		mkdir -p "${APPDIR}/usr/share/icons/hicolor/scalable/apps"
		cp "${ICON_SRC}" "${APPDIR}/usr/share/icons/hicolor/scalable/apps/openterfaceQT.svg" || true
	else
		mkdir -p "${APPDIR}/usr/share/icons/hicolor/256x256/apps"
		cp "${ICON_SRC}" "${APPDIR}/usr/share/icons/hicolor/256x256/apps/openterfaceQT.${ICON_EXT}" || true
		# Also copy with full app ID name for better desktop integration
		cp "${ICON_SRC}" "${APPDIR}/usr/share/icons/hicolor/256x256/apps/com.openterface.openterfaceQT.${ICON_EXT}" || true
	fi
	# Also copy to pixmaps and root
	mkdir -p "${APPDIR}/usr/share/pixmaps"
	cp "${ICON_SRC}" "${APPDIR}/usr/share/pixmaps/openterfaceqt.${ICON_EXT}" || true
	cp "${ICON_SRC}" "${APPDIR}/openterfaceqt.${ICON_EXT}" || true
	# Copy with full app ID to root as well for appimage tools
	cp "${ICON_SRC}" "${APPDIR}/com.openterface.openterfaceQT.${ICON_EXT}" || true
else
	echo "No icon found; continuing without a custom icon."
fi

# Create diagnostic helper script for troubleshooting GLIBC issues
mkdir -p "${APPDIR}/usr/bin"
cat > "${APPDIR}/usr/bin/diagnose-appimage.sh" << 'EOFDIAG'
#!/bin/bash
# Diagnostic script for OpenterfaceQT AppImage
# This script helps troubleshoot GLIBC and library compatibility issues

HERE="$(dirname "$(readlink -f "${0}")/../..")"
echo "=== OpenterfaceQT AppImage Diagnostic Report ==="
echo ""
echo "📍 AppImage Location: $HERE"
echo ""

echo "📊 System Information:"
echo "  Architecture: $(uname -m)"
echo "  System GLIBC: $(ldd --version 2>/dev/null | head -1)"
echo "  System libc path: $(ldconfig -p 2>/dev/null | grep libc.so.6 | head -1 | awk '{print $NF}')"
echo ""

echo "📦 Bundled GLIBC in AppImage:"
if [ -f "$HERE/usr/lib/libc.so.6" ]; then
    echo "  ✓ Found: $HERE/usr/lib/libc.so.6"
    file "$HERE/usr/lib/libc.so.6"
    # Try to get version info
    echo "  Version info:"
    strings "$HERE/usr/lib/libc.so.6" 2>/dev/null | grep "^GLIBC_" | sort | tail -5 || echo "    (unable to extract)"
else
    echo "  ✗ NOT FOUND: $HERE/usr/lib/libc.so.6"
fi
echo ""

echo "📦 Bundled critical libraries:"
for lib in libc.so.6 libm.so.6 libpthread.so.0 libdl.so.2 librt.so.1 libusb-1.0.so.0 libstdbuf.so; do
    if [ -f "$HERE/usr/lib/$lib" ] || [ -f "$HERE/usr/lib/${lib}.0" ] || [ -f "$HERE/usr/lib/${lib}.1" ]; then
        echo "  ✓ Found: $lib"
    else
        echo "  ✗ Missing: $lib"
    fi
done
echo ""

echo "🔍 Library dependency check for main executable:"
if [ -f "$HERE/usr/bin/openterfaceQT" ]; then
    echo "  Checking: $HERE/usr/bin/openterfaceQT"
    if command -v ldd >/dev/null 2>&1; then
        echo "  Dependencies:"
        ldd "$HERE/usr/bin/openterfaceQT" 2>&1 | head -20 || echo "    (unable to list)"
        echo ""
        echo "  Missing dependencies:"
        ldd "$HERE/usr/bin/openterfaceQT" 2>&1 | grep "not found" || echo "    (none found - OK!)"
    else
        echo "  ldd not available"
    fi
else
    echo "  ✗ Executable not found: $HERE/usr/bin/openterfaceQT"
fi
echo ""

echo "📁 AppImage library directory contents:"
echo "  usr/lib/ has $(ls -1 "$HERE/usr/lib/" 2>/dev/null | wc -l) files"
echo "  First 20 files:"
ls -1 "$HERE/usr/lib/" 2>/dev/null | head -20 | sed 's/^/    /'
echo ""

echo "✅ Diagnostic report complete"
EOFDIAG

chmod +x "${APPDIR}/usr/bin/diagnose-appimage.sh"
echo "✓ Created diagnostic helper: diagnose-appimage.sh (run inside extracted AppImage)"


# Prefer preinstalled linuxdeploy and plugin in the image; fallback to download
TOOLS_DIR="${APPIMAGE_DIR}/tools"
LINUXDEPLOY_BIN=""

# Ensure AppImages run inside containers without FUSE (also set in Dockerfile)
export APPIMAGE_EXTRACT_AND_RUN=${APPIMAGE_EXTRACT_AND_RUN:-1}

# Check for pre-downloaded runtime from Docker image
mkdir -p "${TOOLS_DIR}"
DOCKER_RUNTIME_FILE="/opt/appimage-runtime/runtime-${APPIMAGE_ARCH}"
TOOLS_RUNTIME_FILE="${TOOLS_DIR}/runtime-${APPIMAGE_ARCH}"

# Prioritize pre-downloaded runtime from Docker image
if [ -f "${DOCKER_RUNTIME_FILE}" ]; then
	echo "✓ Using pre-downloaded runtime from Docker environment: ${DOCKER_RUNTIME_FILE}"
	cp "${DOCKER_RUNTIME_FILE}" "${TOOLS_RUNTIME_FILE}"
	chmod +x "${TOOLS_RUNTIME_FILE}"
	echo "✓ Runtime copied to tools directory: ${TOOLS_RUNTIME_FILE}"
elif [ -f "${TOOLS_RUNTIME_FILE}" ]; then
	echo "✓ Runtime already available in tools directory: ${TOOLS_RUNTIME_FILE}"
else
	echo "⚠ Warning: No pre-downloaded runtime found at ${DOCKER_RUNTIME_FILE}"
	echo "⚠ AppImage creation will proceed without custom runtime (linuxdeploy will download automatically)"
fi

if command -v linuxdeploy >/dev/null 2>&1 && command -v linuxdeploy-plugin-qt >/dev/null 2>&1; then
	echo "Using preinstalled linuxdeploy and linuxdeploy-plugin-qt"
	LINUXDEPLOY_BIN="$(command -v linuxdeploy)"
	# Ensure the plugin directory is on PATH
	PLUGIN_DIR="$(dirname "$(command -v linuxdeploy-plugin-qt)")"
	export PATH="${PLUGIN_DIR}:${PATH}"
else
	echo "linuxdeploy not found in image; downloading tools..."
	# Download helper (curl with wget fallback)
	_fetch() {
		local url="$1" out="$2"
		if command -v curl >/dev/null 2>&1; then
			curl -fL "${url}" -o "${out}"
		elif command -v wget >/dev/null 2>&1; then
			wget -qO "${out}" "${url}"
		else
			echo "Neither curl nor wget found for downloading ${url}" >&2
			return 1
		fi
	}

	mkdir -p "${TOOLS_DIR}"
	pushd "${TOOLS_DIR}" >/dev/null
	LDEPLOY_URL="https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-${APPIMAGE_ARCH}.AppImage"
	LDEPLOY_QT_URL="https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-${APPIMAGE_ARCH}.AppImage"
	
	echo "Downloading linuxdeploy from ${LDEPLOY_URL}"
	_fetch "${LDEPLOY_URL}" linuxdeploy.AppImage
	echo "Downloading linuxdeploy-plugin-qt from ${LDEPLOY_QT_URL}"
	_fetch "${LDEPLOY_QT_URL}" linuxdeploy-plugin-qt.AppImage
	
	# Use pre-downloaded runtime from Docker image if available
	if [ -f "/opt/appimage-runtime/runtime-${APPIMAGE_ARCH}" ]; then
		echo "✓ Using pre-downloaded runtime from Docker environment"
		cp "/opt/appimage-runtime/runtime-${APPIMAGE_ARCH}" "runtime-${APPIMAGE_ARCH}"
		chmod +x "runtime-${APPIMAGE_ARCH}"
		echo "✓ Runtime copied successfully"
	else
		echo "⚠ No pre-downloaded runtime found, linuxdeploy will download automatically"
	fi
	
	chmod +x linuxdeploy.AppImage linuxdeploy-plugin-qt.AppImage
	# Use the downloaded linuxdeploy and make plugin available on PATH
	LINUXDEPLOY_BIN="${TOOLS_DIR}/linuxdeploy.AppImage"
	export PATH="${TOOLS_DIR}:${PATH}"
	popd >/dev/null
fi

# Build AppImage with comprehensive runtime support
pushd "${APPIMAGE_DIR}" >/dev/null

# For static builds, skip Qt plugin since Qt is statically linked
if [ "${OPENTERFACE_BUILD_STATIC}" != "ON" ]; then
	USE_QT_PLUGIN=true
else
	USE_QT_PLUGIN=false
fi

# Set runtime file path - prioritize Docker pre-downloaded runtime
RUNTIME_FILE="${TOOLS_DIR}/runtime-${APPIMAGE_ARCH}"
DOCKER_RUNTIME_FILE="/opt/appimage-runtime/runtime-${APPIMAGE_ARCH}"
LOCAL_RUNTIME_FILE="./runtime-${APPIMAGE_ARCH}"

# Copy runtime to current directory where appimagetool can find it
if [ -f "${RUNTIME_FILE}" ]; then
	echo "✓ Copying runtime to AppImage build directory for appimagetool"
	cp "${RUNTIME_FILE}" "${LOCAL_RUNTIME_FILE}"
	chmod +x "${LOCAL_RUNTIME_FILE}"
elif [ -f "${DOCKER_RUNTIME_FILE}" ]; then
	echo "✓ Copying Docker runtime to AppImage build directory for appimagetool"
	cp "${DOCKER_RUNTIME_FILE}" "${LOCAL_RUNTIME_FILE}"
	chmod +x "${LOCAL_RUNTIME_FILE}"
	RUNTIME_FILE="${LOCAL_RUNTIME_FILE}"
fi
DOCKER_RUNTIME_FILE="/opt/appimage-runtime/runtime-${APPIMAGE_ARCH}"

# Ensure we have the runtime from Docker environment
if [ -f "${DOCKER_RUNTIME_FILE}" ] && [ ! -f "${RUNTIME_FILE}" ]; then
	echo "✓ Copying Docker pre-downloaded runtime to tools directory"
	cp "${DOCKER_RUNTIME_FILE}" "${RUNTIME_FILE}"
	chmod +x "${RUNTIME_FILE}"
fi

if [ -f "${LOCAL_RUNTIME_FILE}" ]; then
	# Set multiple environment variables that appimagetool might recognize
	export APPIMAGE_RUNTIME_FILE="${LOCAL_RUNTIME_FILE}"
	export RUNTIME_FILE="${LOCAL_RUNTIME_FILE}"
	export APPIMAGETOOL_RUNTIME="${LOCAL_RUNTIME_FILE}"
	export RUNTIME="${LOCAL_RUNTIME_FILE}"
	# Unset UPDATE_INFORMATION to avoid format errors in appimagetool
	unset UPDATE_INFORMATION
	unset LDAI_UPDATE_INFORMATION
	echo "✓ Using local runtime file: ${LOCAL_RUNTIME_FILE}"
	ls -lh "${LOCAL_RUNTIME_FILE}"
elif [ -f "${DOCKER_RUNTIME_FILE}" ]; then
	# Use Docker runtime directly if tools copy doesn't exist
	echo "✓ Using Docker runtime directly: ${DOCKER_RUNTIME_FILE}"
	export APPIMAGE_RUNTIME_FILE="${DOCKER_RUNTIME_FILE}"
	export RUNTIME_FILE="${DOCKER_RUNTIME_FILE}"
	export APPIMAGETOOL_RUNTIME="${DOCKER_RUNTIME_FILE}"
	export RUNTIME="${DOCKER_RUNTIME_FILE}"
	unset UPDATE_INFORMATION
	unset LDAI_UPDATE_INFORMATION
	ls -lh "${DOCKER_RUNTIME_FILE}"
else
	echo "⚠ Warning: No runtime file available, linuxdeploy will download it automatically"
	unset APPIMAGE_RUNTIME_FILE
	unset RUNTIME_FILE
	unset APPIMAGETOOL_RUNTIME
	unset RUNTIME
	unset UPDATE_INFORMATION
	unset LDAI_UPDATE_INFORMATION
fi

# Debug: Show environment variables for AppImage creation
echo "=== AppImage Build Environment ==="
echo "APPIMAGE_EXTRACT_AND_RUN=${APPIMAGE_EXTRACT_AND_RUN}"
echo "APPIMAGE_RUNTIME_FILE=${APPIMAGE_RUNTIME_FILE:-<not set>}"
echo "RUNTIME_FILE=${RUNTIME_FILE:-<not set>}"
echo "APPIMAGETOOL_RUNTIME=${APPIMAGETOOL_RUNTIME:-<not set>}"
echo "RUNTIME=${RUNTIME:-<not set>}"
echo "UPDATE_INFORMATION=${UPDATE_INFORMATION:-<not set>}"
echo "LINUXDEPLOY_BIN=${LINUXDEPLOY_BIN}"
echo "================================="

# Debug: Show the actual linuxdeploy command being executed
echo "=== LinuxDeploy Command Debug ==="
echo "LINUXDEPLOY_BIN: ${LINUXDEPLOY_BIN}"
echo "APPDIR: ${APPDIR}"
echo "DESKTOP_OUT: ${DESKTOP_OUT}"
echo "ICON_SRC: ${ICON_SRC}"
echo "USE_QT_PLUGIN: ${USE_QT_PLUGIN}"
echo "================================"

# Debug: Show what libraries are in the AppDir before linuxdeploy
echo "📊 Verifying libusb in AppDir before linuxdeploy..."
LIBUSB_COUNT=$(find "${APPDIR}/usr/lib" -name "libusb*" 2>/dev/null | wc -l)
if [ "$LIBUSB_COUNT" -gt 0 ]; then
	echo "  ✅ libusb found in AppDir:"
	find "${APPDIR}/usr/lib" -name "libusb*" 2>/dev/null | while read -r lib; do
		echo "     - $lib"
		file "$lib" 2>/dev/null || echo "       (unable to determine file type)"
	done
else
	echo "  ❌ libusb NOT found in AppDir!"
	echo "  Available system libraries:"
	ls -lh "${APPDIR}/usr/lib/" | head -20
fi
echo ""

# Set LD_LIBRARY_PATH to include ffmpeg and gstreamer libraries for dependency resolution
export LD_LIBRARY_PATH="/opt/ffmpeg/lib:/opt/gstreamer/lib:/opt/gstreamer/lib/${ARCH}-linux-gnu:/opt/Qt6/lib:$LD_LIBRARY_PATH"
export PATH="/opt/Qt6/bin:$PATH"
export QT_PLUGIN_PATH="/opt/Qt6/plugins:$QT_PLUGIN_PATH"
export QML2_IMPORT_PATH="/opt/Qt6/qml:$QML2_IMPORT_PATH"

# CRITICAL: Save libraries BEFORE linuxdeploy (it will blacklist some)
echo "📦 Pre-linuxdeploy: Saving critical libraries to prevent blacklisting..."
mkdir -p "${APPDIR}/usr/lib/linuxdeploy_protected"

# These libraries WILL be blacklisted by linuxdeploy - save them now
PROTECTED_LIBS=(
	"libc.so.6"
	"libm.so.6" 
	"libpthread.so.0"
	"libdl.so.2"
	"librt.so.1"
	"libgcc_s.so.1"
	"libstdc++.so.6"
	"ld-linux-x86-64.so.2"
	"libnss_compat.so.2"
	"libnss_files.so.2"
	"libnss_dns.so.2"
	"libresolv.so.2"
	"libcrypt.so.1"
	"libutil.so.1"
	# GPU/EGL rendering libraries that are also blacklisted but needed
	"libEGL.so.1"
	"libEGL.so"
	"libGLX.so.0"
	"libGLX.so"
	"libGL.so.1"
	"libGL.so"
	"libGLESv2.so.2"
	"libGLESv2.so"
	"libglvnd.so.0"
	"libglvnd.so"
	"libGLdispatch.so.0"
	"libGLdispatch.so"
	"libOpenGL.so.0"
	"libOpenGL.so"
	# Additional rendering support
	"libxcb-dri2.so.0"
	"libxcb-dri3.so.0"
	"libxcb-present.so.0"
	"libxcb-sync.so.1"
	"libxshmfence.so.1"
	"libdrm.so.2"
	"libdrm.so"
	# Compression libraries
	"libbz2.so.1"
	"libbz2.so"
)

PROTECTED_COUNT=0
PROTECTED_MISSING=0
for lib in "${PROTECTED_LIBS[@]}"; do
	# Use wildcard to match versioned libraries (e.g., libEGL.so.1, libEGL.so.0, libEGL.so)
	# Find all files/symlinks matching this pattern
	lib_found=0
	
	if [ -f "${APPDIR}/usr/lib/${lib}" ] || [ -L "${APPDIR}/usr/lib/${lib}" ]; then
		# Exact match found
		cp -P "${APPDIR}/usr/lib/${lib}" "${APPDIR}/usr/lib/linuxdeploy_protected/" 2>/dev/null || true
		PROTECTED_COUNT=$((PROTECTED_COUNT + 1))
		lib_found=1
	else
		# Try wildcard match for versioned libraries
		shopt -s nullglob
		for libfile in "${APPDIR}/usr/lib/${lib}"*; do
			if [ -f "$libfile" ] || [ -L "$libfile" ]; then
				cp -P "$libfile" "${APPDIR}/usr/lib/linuxdeploy_protected/" 2>/dev/null || true
				PROTECTED_COUNT=$((PROTECTED_COUNT + 1))
				lib_found=1
			fi
		done
		shopt -u nullglob
	fi
	
	if [ $lib_found -eq 0 ]; then
		PROTECTED_MISSING=$((PROTECTED_MISSING + 1))
	fi
done
echo "  ✓ Protected $PROTECTED_COUNT critical libraries"
if [ $PROTECTED_MISSING -gt 0 ]; then
	echo "  ⚠️  $PROTECTED_MISSING libraries were not found to backup"
	echo "     First 20 libs in ${APPDIR}/usr/lib:"
	ls -1 "${APPDIR}/usr/lib" 2>/dev/null | head -20 | sed 's/^/       /'
fi

# Build the command with proper argument handling
LINUXDEPLOY_ARGS=(
	"--appdir" "${APPDIR}"
	"--executable" "${APPDIR}/usr/bin/openterfaceQT"
	"--desktop-file" "${DESKTOP_OUT}"
)

if [ -n "${ICON_SRC}" ]; then
	LINUXDEPLOY_ARGS+=("--icon-file" "${ICON_SRC}")
fi

if [ "${USE_QT_PLUGIN}" = "true" ]; then
	LINUXDEPLOY_ARGS+=("--plugin" "qt")
fi

LINUXDEPLOY_ARGS+=("--output" "appimage")

echo "Final command: ${LINUXDEPLOY_BIN}" "${LINUXDEPLOY_ARGS[@]}"

# Try running linuxdeploy without the appimage output plugin first
LINUXDEPLOY_ARGS_NO_OUTPUT=(
	"--appdir" "${APPDIR}"
	"--executable" "${APPDIR}/usr/bin/openterfaceQT"
	"--desktop-file" "${DESKTOP_OUT}"
)

if [ -n "${ICON_SRC}" ]; then
	LINUXDEPLOY_ARGS_NO_OUTPUT+=("--icon-file" "${ICON_SRC}")
fi

if [ "${USE_QT_PLUGIN}" = "true" ]; then
	LINUXDEPLOY_ARGS_NO_OUTPUT+=("--plugin" "qt")
fi

echo "Running linuxdeploy without output plugin..."
echo "⚠️  IMPORTANT: linuxdeploy will skip/blacklist certain libraries"
echo "    (libc.so.6, libgcc_s.so.1, libstdc++.so.6, libEGL.so.1, libGL.so.1, libdrm.so.2, etc.)"
echo "    These have been backed up and will be restored after linuxdeploy completes"
echo ""

# Run linuxdeploy with environment variable to suppress some warnings
# Note: linuxdeploy will still skip blacklisted libraries, but we'll restore them after
export LDAI_SKIP_GLIBC=1
if "${LINUXDEPLOY_BIN}" "${LINUXDEPLOY_ARGS_NO_OUTPUT[@]}"; then
	echo "✓ linuxdeploy completed"
else
	LINUXDEPLOY_EXIT=$?
	echo "⚠️  linuxdeploy exited with code: $LINUXDEPLOY_EXIT"
	if [ $LINUXDEPLOY_EXIT -eq 141 ] || [ $LINUXDEPLOY_EXIT -eq 3 ]; then
		echo "   (This may be a SIGPIPE or similar non-critical error)"
		echo "   Continuing..."
	fi
fi

# CRITICAL FIX: Restore critical libraries that linuxdeploy blacklisted
echo "🔧 Restoring critical libraries that linuxdeploy skipped..."
echo "   (linuxdeploy blacklists: libc.so.6, libgcc_s.so.1, libstdc++.so.6, libEGL.so.1, libGL.so.1, libdrm.so.2, etc.)"

if [ -d "${APPDIR}/usr/lib/linuxdeploy_protected" ]; then
	echo "   Checking protected backup directory..."
	PROTECTED_LIB_COUNT=$(find "${APPDIR}/usr/lib/linuxdeploy_protected" -type f 2>/dev/null | wc -l)
	echo "   Found $PROTECTED_LIB_COUNT libraries in protected backup"
	
	if [ "$PROTECTED_LIB_COUNT" -eq 0 ]; then
		echo "   ⚠️  No files in protected directory!"
		ls -la "${APPDIR}/usr/lib/linuxdeploy_protected/" 2>/dev/null | head -10
	else
		RESTORED_COUNT=0
		for lib in "${APPDIR}/usr/lib/linuxdeploy_protected"/*; do
			if [ -f "$lib" ]; then
				libname=$(basename "$lib")
				# Check if linuxdeploy removed it
				if [ ! -f "${APPDIR}/usr/lib/$libname" ]; then
					echo "  ✓ Restoring blacklisted library: $libname"
					cp -P "$lib" "${APPDIR}/usr/lib/" || echo "    ⚠️  Failed to restore $libname"
					RESTORED_COUNT=$((RESTORED_COUNT + 1))
				else
					echo "  ℹ️  Library still present: $libname"
				fi
			fi
		done
		echo "  ✅ Restored $RESTORED_COUNT blacklisted libraries"
	fi
	rm -rf "${APPDIR}/usr/lib/linuxdeploy_protected"
else
	echo "  ⚠️  No protected backup found!"
	echo "     Available at: ${APPDIR}/usr/lib/linuxdeploy_protected"
fi

# Double-check critical libraries are present
echo ""
echo "📊 Verifying critical libraries after linuxdeploy..."
CRITICAL_LIBS=("libc.so.6" "libm.so.6" "libpthread.so.0" "libdl.so.2" "libgcc_s.so.1" "libstdc++.so.6")
MISSING_LIBS=()
for lib in "${CRITICAL_LIBS[@]}"; do
	if [ -f "${APPDIR}/usr/lib/$lib" ]; then
		echo "  ✓ Present: $lib"
	else
		echo "  ✗ MISSING: $lib"
		MISSING_LIBS+=("$lib")
	fi
done

if [ ${#MISSING_LIBS[@]} -gt 0 ]; then
	echo ""
	echo "❌ ERROR: Critical libraries are missing!"
	echo "Missing: ${MISSING_LIBS[*]}"
	echo ""
	echo "This may cause the AppImage to fail with GLIBC version errors."
	echo "Available libraries in ${APPDIR}/usr/lib:"
	ls -1 "${APPDIR}/usr/lib" | grep -E "lib(c|m|gcc|stdc)" | head -20
	echo ""
	echo "📍 Diagnostic Info:"
	echo "   Total files in ${APPDIR}/usr/lib:"
	find "${APPDIR}/usr/lib" -type f 2>/dev/null | wc -l
	echo "   Subdirectories:"
	ls -d "${APPDIR}/usr/lib"/*/ 2>/dev/null | sed 's|^|   |'
	echo ""
	echo "⚠️  CRITICAL: Libraries may not have been copied to AppDir before linuxdeploy!"
	echo "   Check that copy_libraries() function is working correctly."
	echo ""
fi

# After linuxdeploy, clean up coreutils libraries that have incompatible GLIBC versions
echo "🔧 Cleaning up incompatible coreutils libraries..."

# NOTE: GLIBC libraries (libc.so.6, libm.so.6, etc.) are KEPT in the AppImage
# These are critical and must be bundled for the application to run on systems
# with different GLIBC versions. The LD_LIBRARY_PATH in AppRun ensures proper loading.

# Remove coreutils binaries
PROBLEM_BINS=(
    "stdbuf"           # Most common culprit - uses libstdbuf.so
    "time"             # May also have version issues
    "timeout"          # May have version issues
)

for bin in "${PROBLEM_BINS[@]}"; do
    if [ -f "${APPDIR}/usr/bin/$bin" ]; then
        echo "  🗑️  Removing ${bin} to avoid GLIBC compatibility issues"
        rm -f "${APPDIR}/usr/bin/$bin"
    fi
done

# Remove problematic coreutils libraries from /usr/lib/
echo "Removing coreutils libraries that have incompatible GLIBC versions..."
rm -f "${APPDIR}/usr/lib/libstdbuf.so"* 2>/dev/null || true
rm -f "${APPDIR}/lib/libstdbuf.so"* 2>/dev/null || true
echo "  🗑️  Removed libstdbuf.so variants"

# Remove the coreutils libexec directory
rm -rf "${APPDIR}/usr/libexec/coreutils/" 2>/dev/null || true
rm -rf "${APPDIR}/libexec/coreutils/" 2>/dev/null || true
echo "  ✓ GLIBC and coreutils libraries removed - will use host system versions"

# Copy AppRun script from packaging directory with proper environment setup
cp "${SRC}/packaging/appimage/AppRun" "${APPDIR}/AppRun"
chmod +x "${APPDIR}/AppRun"
echo "✓ AppRun script copied from packaging/appimage/"

# Then use appimagetool directly with explicit runtime file
echo "Running appimagetool with explicit runtime file..."
APPIMAGETOOL_FAILED=0
if command -v appimagetool >/dev/null 2>&1; then
	if [ -f "${RUNTIME_FILE}" ]; then
		echo "Using runtime file: ${RUNTIME_FILE}"
		appimagetool --runtime-file "${RUNTIME_FILE}" "${APPDIR}" || APPIMAGETOOL_FAILED=$?
	else
		echo "Warning: Runtime file not found, running appimagetool without runtime file"
		appimagetool "${APPDIR}" || APPIMAGETOOL_FAILED=$?
	fi
else
	echo "appimagetool not found, trying linuxdeploy with output plugin..."
	LINUXDEPLOY_ARGS+=("--output" "appimage")
	"${LINUXDEPLOY_BIN}" "${LINUXDEPLOY_ARGS[@]}" || APPIMAGETOOL_FAILED=$?
fi

if [ $APPIMAGETOOL_FAILED -ne 0 ] && [ $APPIMAGETOOL_FAILED -ne 141 ]; then
	echo "⚠️  appimagetool exited with code: $APPIMAGETOOL_FAILED"
fi

# Normalize output name
APPIMAGE_FILENAME="openterfaceQT_${VERSION}_${APPIMAGE_ARCH}.AppImage"
# Move whichever AppImage got produced
FOUND_APPIMAGE=$(ls -1 *.AppImage 2>/dev/null | grep -v -E '^linuxdeploy|^linuxdeploy-plugin-qt' | head -n1 || echo "")
if [ -n "${FOUND_APPIMAGE}" ]; then
	chmod +x "${FOUND_APPIMAGE}"
	mv "${FOUND_APPIMAGE}" "${APPIMAGE_OUT}/${APPIMAGE_FILENAME}"
	echo "✅ AppImage created at ${APPIMAGE_OUT}/${APPIMAGE_FILENAME}"
	echo "📊 AppImage size: $(ls -lh "${APPIMAGE_OUT}/${APPIMAGE_FILENAME}" | awk '{print $5}')"
else
	echo "⚠️  No AppImage found in current directory"
	echo "Checking for any .AppImage files in system..."
	find . -name "*.AppImage" -type f 2>/dev/null | head -5 | while read -r img; do
		echo "  Found: $img"
		if [ -f "$img" ]; then
			SIZE=$(ls -lh "$img" | awk '{print $5}')
			echo "  Size: $SIZE"
			chmod +x "$img"
			FINAL_NAME=$(basename "$img" | sed "s/_continuous/${VERSION}/g")
			mv "$img" "${APPIMAGE_OUT}/${FINAL_NAME}" 2>/dev/null && echo "  ✓ Moved to ${APPIMAGE_OUT}/${FINAL_NAME}" || echo "  ✗ Failed to move"
		fi
	done
	
	# Final check
	if ls "${APPIMAGE_OUT}"/*.AppImage >/dev/null 2>&1; then
		echo "✅ AppImage created successfully"
		ls -lh "${APPIMAGE_OUT}"/*.AppImage
	else
		echo "❌ Error: AppImage build did not produce an output." >&2
		exit 1
	fi
fi

popd >/dev/null