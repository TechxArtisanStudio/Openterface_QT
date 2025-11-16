#!/bin/bash

set -e

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
VERSION="${VERSION:-0.0.1}"
ARCH="${ARCH:-amd64}"

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

# Copy launcher script
if [ -f "${SRC}/packaging/rpm/openterfaceQT-launcher.sh" ]; then
	cp "${SRC}/packaging/rpm/openterfaceQT-launcher.sh" "${RPMTOP}/SOURCES/"
	echo "✅ Launcher script copied to SOURCES"
else
	echo "Error: launcher script not found at ${SRC}/packaging/rpm/openterfaceQT-launcher.sh" >&2
	exit 1
fi

# Copy Qt Version Wrapper source and build script
if [ -f "${SRC}/packaging/rpm/qt_version_wrapper.c" ]; then
	cp "${SRC}/packaging/rpm/qt_version_wrapper.c" "${RPMTOP}/SOURCES/"
	echo "✅ Qt Version Wrapper source copied to SOURCES"
else
	echo "Warning: Qt Version Wrapper source not found at ${SRC}/packaging/rpm/qt_version_wrapper.c" >&2
fi

if [ -f "${SRC}/packaging/rpm/build-qt-wrapper.sh" ]; then
	cp "${SRC}/packaging/rpm/build-qt-wrapper.sh" "${RPMTOP}/SOURCES/"
	echo "✅ Qt Version Wrapper build script copied to SOURCES"
fi

if [ -f "${SRC}/packaging/rpm/setup-env.sh" ]; then
	cp "${SRC}/packaging/rpm/setup-env.sh" "${RPMTOP}/SOURCES/"
	echo "✅ Environment setup script copied to SOURCES"
fi

# Install patchelf and gcc for wrapper compilation
apt update && apt install -y patchelf gcc

# Copy Qt libraries to SOURCES for bundling (RPM)
# CRITICAL: Must use a proper Qt6 build, NOT system libraries
QT_LIB_DIR="/opt/Qt6/lib"
if [ ! -d "${QT_LIB_DIR}" ]; then
    echo "❌ ERROR: Qt6 custom build not found at /opt/Qt6/lib"
    echo "   The RPM package requires a properly compiled Qt6 build."
    echo "   System Qt6 libraries cannot be used as they have version dependencies."
    exit 1
fi

# ============================================================
# BUILD Qt Version Wrapper (CRITICAL for Fedora compatibility)
# ============================================================
# This wrapper intercepts dlopen() calls and prevents system Qt6 from being loaded
echo "📋 RPM: Building Qt Version Wrapper library..."
if [ -f "${RPMTOP}/SOURCES/qt_version_wrapper.c" ]; then
    cd "${RPMTOP}/SOURCES"
    
    # Compile the wrapper
    gcc -shared -fPIC -o qt_version_wrapper.so \
        -DBUNDLED_QT_PATH=\"/usr/lib/openterfaceqt/qt6\" \
        qt_version_wrapper.c -ldl 2>&1 | sed 's/^/   /'
    
    if [ -f "qt_version_wrapper.so" ]; then
        echo "✅ Qt Version Wrapper compiled successfully"
        echo "   Size: $(stat -c%s qt_version_wrapper.so) bytes"
    else
        echo "⚠️  Warning: Qt Version Wrapper compilation failed"
        echo "   Application may encounter Qt version conflicts on Fedora"
    fi
    
    cd - > /dev/null
else
    echo "⚠️  Qt Version Wrapper source not available"
fi

# ============================================================
# Copy Qt libraries to RPM SOURCES

echo "Copying Qt libraries to SOURCES..."
QT_LIBS=$(find "${QT_LIB_DIR}" -maxdepth 1 -name "libQt6*.so*" -type f | wc -l)
if [ $QT_LIBS -eq 0 ]; then
    echo "❌ ERROR: No Qt6 libraries found in ${QT_LIB_DIR}"
    exit 1
fi

# Only copy actual files (not symlinks) to avoid duplication
mkdir -p "${RPMTOP}/SOURCES/qt6"
find "${QT_LIB_DIR}" -maxdepth 1 -name "libQt6*.so*" -type f -exec cp -a {} "${RPMTOP}/SOURCES/qt6/" \;
echo "✅ Qt libraries copied to SOURCES/qt6 ($QT_LIBS files)"

# CRITICAL: Also bundle Qt6 base modules and system-provided modules
# These are sometimes provided by system Qt but we need to ensure we use bundled versions
# to prevent version conflicts (e.g., system Qt6.9 vs bundled Qt6.6.3)
echo "📋 RPM: Searching for additional Qt6 base modules..."
QT_CRITICAL_MODULES=(
    "libQt6QmlModels.so"      # CRITICAL: QML models - often causes version conflicts
    "libQt6QmlWorkerScript.so" # CRITICAL: QML worker script support
    "libQt6Core5Compat.so"    # Qt5 compatibility layer (if available)
    "libQt6GuiPrivate.so"     # Private GUI API (if available)
    "libQt6QuickControls2.so"  # Quick Controls 2
    "libQt6QuickShapes.so"     # Quick Shapes
    "libQt6QuickLayouts.so"    # Quick Layouts
    "libQt6QuickTemplates2.so" # Quick Templates 2
    "libQt6QuickParticles.so"  # Quick Particles
    "libQt6OpenGLWidgets.so"   # OpenGL Widgets
    "libQt6WebSockets.so"      # WebSockets support
    "libQt6Positioning.so"     # Positioning (if available)
    "libQt6Sensors.so"         # Sensors (if available)
    "libQt6Sql.so"             # SQL support (if available)
    "libQt6Test.so"            # Test framework (if available)
)

for qt_module in "${QT_CRITICAL_MODULES[@]}"; do
    echo "   Checking for $qt_module..."
    
    # Search in bundled Qt first
    if ls "${QT_LIB_DIR}"/${qt_module}* >/dev/null 2>&1; then
        echo "      ✅ Found in bundled Qt: ${QT_LIB_DIR}/${qt_module}"
        find "${QT_LIB_DIR}" -maxdepth 1 -name "${qt_module%.*}*" -type f -exec cp -Pv {} "${RPMTOP}/SOURCES/qt6/" \; 2>&1 | sed 's/^/         /'
    else
        # Try to find in system Qt if not in bundled (we'll flag these as potential conflicts)
        for system_search_dir in /lib64 /usr/lib64 /usr/lib /usr/lib/x86_64-linux-gnu; do
            if [ -d "$system_search_dir" ] && ls "$system_search_dir"/${qt_module}* >/dev/null 2>&1; then
                echo "      ⚠️  Found in system Qt but NOT in bundled Qt: $system_search_dir/${qt_module}"
                echo "         This will cause version conflicts! Check Qt6 build completeness."
                break
            fi
        done
    fi
done
echo "✅ Qt6 critical modules check complete"

# Copy Qt6 XCB QPA library to SOURCES (needed by libqxcb.so plugin) - must be from same Qt6 build
echo "📋 RPM: Searching for Qt6 XCB QPA library..."
XCB_QPA_FOUND=0

# Only look in the Qt6 build location, not system
if [ -d "/opt/Qt6/lib" ] && ls "/opt/Qt6/lib"/libQt6XcbQpa.so* >/dev/null 2>&1; then
    echo "   ✅ Found libQt6XcbQpa.so in /opt/Qt6/lib"
    XCB_QPA_FILES=$(ls -la "/opt/Qt6/lib"/libQt6XcbQpa.so*)
    echo "   Files found:"
    echo "$XCB_QPA_FILES" | sed 's/^/     /'
    # Only copy actual files (not symlinks) to avoid duplication
    find "/opt/Qt6/lib" -maxdepth 1 -name "libQt6XcbQpa.so*" -type f -exec cp -Pv {} "${RPMTOP}/SOURCES/qt6/" \; 2>&1 | sed 's/^/     /'
    echo "   ✅ libQt6XcbQpa.so copied to ${RPMTOP}/SOURCES/qt6"
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

# Copy Qt plugins to SOURCES (SELECTIVE: only essential plugins to reduce RPM size)
QT_PLUGIN_DIR="/opt/Qt6/plugins"
if [ -d "${QT_PLUGIN_DIR}" ]; then
    echo "📋 RPM: Copying ONLY essential Qt plugins (selective bundling for size optimization)..."
    
    # Only copy essential plugin categories needed by openterfaceQT
    # Excluding: imageformats (handled separately), codecs, tls, iconengines (handled separately)
    ESSENTIAL_PLUGINS=(
        "platforms"     # Platform abstraction (xcb, wayland, minimal)
        "platforminputcontexts"  # Input method contexts
    )
    
    mkdir -p "${RPMTOP}/SOURCES/qt6/plugins"
    
    for plugin_dir in "${ESSENTIAL_PLUGINS[@]}"; do
        if [ -d "${QT_PLUGIN_DIR}/${plugin_dir}" ]; then
            echo "   ✅ Copying ${plugin_dir}..."
            mkdir -p "${RPMTOP}/SOURCES/qt6/plugins/${plugin_dir}"
            cp -r "${QT_PLUGIN_DIR}/${plugin_dir}"/* "${RPMTOP}/SOURCES/qt6/plugins/${plugin_dir}/" 2>/dev/null || true
        fi
    done
    
    echo "   ✅ Essential Qt plugins copied (size-optimized)"
fi

# Copy Qt QML imports to SOURCES (SELECTIVE: only essential modules)
QT_QML_DIR="/opt/Qt6/qml"
if [ -d "${QT_QML_DIR}" ]; then
    echo "📋 RPM: Checking for essential Qt QML modules (selective bundling)..."
    
    # Only copy if actually used by application - check if application uses QML
    if strings "${RPMTOP}/SOURCES/openterfaceQT" 2>/dev/null | grep -q "qml\|QML\|QtQuick"; then
        echo "   ✅ Application uses QML, copying essential modules..."
        mkdir -p "${RPMTOP}/SOURCES/qt6/qml"
        
        # Only copy core QML modules needed at runtime
        ESSENTIAL_QML=(
            "Qt"           # Core Qt QML module
            "QtCore"       # QtCore bindings
        )
        
        for qml_module in "${ESSENTIAL_QML[@]}"; do
            if [ -d "${QT_QML_DIR}/${qml_module}" ]; then
                echo "      Copying ${qml_module}..."
                mkdir -p "${RPMTOP}/SOURCES/qt6/qml/${qml_module}"
                cp -r "${QT_QML_DIR}/${qml_module}"/* "${RPMTOP}/SOURCES/qt6/qml/${qml_module}/" 2>/dev/null || true
            fi
        done
        echo "   ✅ Essential QML modules copied"
    else
        echo "   ℹ️  Application doesn't use QML, skipping QML module bundling"
    fi
fi

# Copy SVG-specific Qt libraries and plugins to SOURCES (CONDITIONAL: only if SVG icons used)
echo "📋 RPM: Checking for SVG icon usage in application..."

# OPTIMIZATION: Only bundle SVG libraries if project contains SVG icons
if find "${SRC}/images" -name "*.svg" 2>/dev/null | grep -q .; then
    echo "   ✅ SVG icons detected - will bundle SVG support"
    
    # Copy libQt6Svg library
    echo "   📦 Searching for libQt6Svg.so..."
    SVG_LIB_FOUND=0
    for SEARCH_DIR in /opt/Qt6/lib /usr/lib/x86_64-linux-gnu /usr/lib; do
        if [ -d "$SEARCH_DIR" ]; then
            if ls "$SEARCH_DIR"/libQt6Svg.so* >/dev/null 2>&1; then
                echo "   ✅ Found libQt6Svg.so in $SEARCH_DIR"
                # Only copy actual files (not symlinks) to avoid duplication
                find "$SEARCH_DIR" -maxdepth 1 -name "libQt6Svg.so*" -type f -exec cp -Pv {} "${RPMTOP}/SOURCES/qt6/" \; 2>&1 | sed 's/^/     /'
                SVG_LIB_FOUND=1
                break
            fi
        fi
    done
    if [ $SVG_LIB_FOUND -eq 0 ]; then
        echo "   ⚠️  libQt6Svg.so not found - SVG support may be limited"
    fi

    # Copy SVG image format plugin (libqsvg.so)
    echo "   📦 Searching for libqsvg.so (SVG image format plugin)..."
    SVG_PLUGIN_FOUND=0
    for SEARCH_DIR in /opt/Qt6/plugins /usr/lib/qt6/plugins /usr/lib/x86_64-linux-gnu/qt6/plugins; do
        if [ -d "$SEARCH_DIR/imageformats" ]; then
            if [ -f "$SEARCH_DIR/imageformats/libqsvg.so" ]; then
                echo "   ✅ Found libqsvg.so in $SEARCH_DIR"
                mkdir -p "${RPMTOP}/SOURCES/qt6/plugins/imageformats"
                cp -Pv "$SEARCH_DIR/imageformats/libqsvg.so" "${RPMTOP}/SOURCES/qt6/plugins/imageformats/" 2>&1 | sed 's/^/     /'
                SVG_PLUGIN_FOUND=1
                break
            fi
        fi
    done
    if [ $SVG_PLUGIN_FOUND -eq 0 ]; then
        echo "   ⚠️  libqsvg.so not found - SVG images won't be loaded"
    fi
else
    echo "   ℹ️  No SVG icons detected - skipping SVG library bundling (size optimization: ~5-10MB saved)"
    SVG_LIB_FOUND=0
    SVG_PLUGIN_FOUND=0
fi

# Copy SVG icon engine plugin (libqsvgicon.so) - OPTIONAL for RPM size optimization
echo "   📦 Checking for SVG icon usage in application..."
SVGICON_PLUGIN_FOUND=0

# Only bundle SVG icon plugin if icons actually use SVG format (size optimization)
if find "${SRC}/images" -name "*.svg" 2>/dev/null | grep -q .; then
    echo "   ℹ️  SVG icons detected, attempting to bundle SVG icon engine..."
    
    for SEARCH_DIR in /opt/Qt6/plugins /usr/lib/qt6/plugins /usr/lib/x86_64-linux-gnu/qt6/plugins; do
        if [ -d "$SEARCH_DIR/iconengines" ]; then
            FOUND_FILES=$(find "$SEARCH_DIR/iconengines" -name "libqsvgicon.so*" 2>/dev/null)
            if [ -n "$FOUND_FILES" ]; then
                echo "   ✅ Found libqsvgicon.so, bundling for SVG icon support..."
                mkdir -p "${RPMTOP}/SOURCES/qt6/plugins/iconengines"
                echo "$FOUND_FILES" | while read -r svg_icon_file; do
                    cp -Pv "$svg_icon_file" "${RPMTOP}/SOURCES/qt6/plugins/iconengines/" 2>&1 | sed 's/^/     /'
                done
                SVGICON_PLUGIN_FOUND=1
                break
            fi
        fi
    done
else
    echo "   ℹ️  No SVG icons in application, skipping SVG icon engine (size optimization ~2MB saved)"
fi

if [ $SVGICON_PLUGIN_FOUND -eq 0 ] && find "${SRC}/images" -name "*.svg" 2>/dev/null | grep -q .; then
    echo "   ⚠️  SVG icons found but libqsvgicon.so plugin not available - icons will use fallback rendering"
fi

# ============================================================
# Generic library copying function
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
    
    echo "� RPM: Searching for ${display_name} libraries..."
    local found=0
    
    for search_dir in "${search_dirs[@]}"; do
        echo "   Checking: $search_dir"
        if [ -d "$search_dir" ]; then
            if ls "$search_dir"/${lib_pattern}* >/dev/null 2>&1; then
                echo "   ✅ Found ${display_name} in $search_dir"
                local files=$(ls -la "$search_dir"/${lib_pattern}* 2>/dev/null)
                echo "   Files found:"
                echo "$files" | sed 's/^/     /'
                # Only copy actual files (not symlinks) to avoid duplication
                find "$search_dir" -maxdepth 1 -name "${lib_pattern}*" -type f -exec cp -av {} "${target_dir}/" \; 2>&1 | sed 's/^/     /'
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
    
    # Export result as a variable (e.g., LIBBZ2_FOUND=1)
    eval "${var_name}_FOUND=$found"
}

# ============================================================
# Define library copying configurations in a unified structure
# Format: variable_name|display_name|lib_pattern|severity|target_subdir|search_dirs...
# target_subdir: "" (root), "ffmpeg", or "gstreamer"
# ============================================================

# Create target directories
mkdir -p "${RPMTOP}/SOURCES/ffmpeg"
mkdir -p "${RPMTOP}/SOURCES/gstreamer"

# Unified library configurations with target subdirectories
declare -a UNIFIED_LIBRARY_CONFIGS=(
    # General libraries -> ${RPMTOP}/SOURCES (empty subdir means root)
    "LIBBZ2|bzip2|libbz2.so|ERROR||/opt/ffmpeg/lib|/usr/lib/x86_64-linux-gnu|/usr/lib"
    "LIBUSB|libusb|libusb*.so|ERROR||/opt/libusb/lib|/opt/ffmpeg/lib|/usr/lib/x86_64-linux-gnu|/usr/lib"
    "JPEG|libjpeg|libjpeg.so|ERROR||/opt/ffmpeg/lib|/usr/lib/x86_64-linux-gnu|/usr/lib"
    "TURBOJPEG|libturbojpeg|libturbojpeg.so|ERROR||/opt/ffmpeg/lib|/usr/lib/x86_64-linux-gnu|/usr/lib"
    "VA|VA-API|libva.so|WARNING||/usr/lib/x86_64-linux-gnu|/usr/lib"
    "VDPAU|VDPAU|libvdpau.so|WARNING||/usr/lib/x86_64-linux-gnu|/usr/lib"
    
    # FFmpeg libraries -> ${RPMTOP}/SOURCES/ffmpeg
    "AVDEVICE|FFmpeg avdevice|libavdevice.so|WARNING|ffmpeg|/opt/ffmpeg/lib|/usr/lib/x86_64-linux-gnu|/usr/lib"
    "AVCODEC|FFmpeg avcodec|libavcodec.so|WARNING|ffmpeg|/opt/ffmpeg/lib|/usr/lib/x86_64-linux-gnu|/usr/lib"
    "AVFORMAT|FFmpeg avformat|libavformat.so|WARNING|ffmpeg|/opt/ffmpeg/lib|/usr/lib/x86_64-linux-gnu|/usr/lib"
    "AVUTIL|FFmpeg avutil|libavutil.so|WARNING|ffmpeg|/opt/ffmpeg/lib|/usr/lib/x86_64-linux-gnu|/usr/lib"
    "SWSCALE|FFmpeg swscale|libswscale.so|WARNING|ffmpeg|/opt/ffmpeg/lib|/usr/lib/x86_64-linux-gnu|/usr/lib"
    "SWRESAMPLE|FFmpeg swresample|libswresample.so|WARNING|ffmpeg|/opt/ffmpeg/lib|/usr/lib/x86_64-linux-gnu|/usr/lib"
    "AVFILTER|FFmpeg avfilter|libavfilter.so|WARNING|ffmpeg|/opt/ffmpeg/lib|/usr/lib/x86_64-linux-gnu|/usr/lib"
    
    # GStreamer libraries -> ${RPMTOP}/SOURCES/gstreamer
    "GSTREAMER|GStreamer core|libgstreamer-1.0.so|WARNING|gstreamer|/opt/gstreamer/lib/x86_64-linux-gnu|/usr/lib/x86_64-linux-gnu|/usr/lib"
    "GSTBASE|GStreamer base|libgstbase-1.0.so|WARNING|gstreamer|/opt/gstreamer/lib/x86_64-linux-gnu|/usr/lib/x86_64-linux-gnu|/usr/lib"
    "GSTAUDIO|GStreamer audio|libgstaudio-1.0.so|WARNING|gstreamer|/opt/gstreamer/lib/x86_64-linux-gnu|/usr/lib/x86_64-linux-gnu|/usr/lib"
    "GSTPBUTILS|GStreamer playback utils|libgstpbutils-1.0.so|WARNING|gstreamer|/opt/gstreamer/lib/x86_64-linux-gnu|/usr/lib/x86_64-linux-gnu|/usr/lib"
    "GSTVIDEO|GStreamer video|libgstvideo-1.0.so|WARNING|gstreamer|/opt/gstreamer/lib/x86_64-linux-gnu|/usr/lib/x86_64-linux-gnu|/usr/lib"
    "GSTAPP|GStreamer app|libgstapp-1.0.so|WARNING|gstreamer|/opt/gstreamer/lib/x86_64-linux-gnu|/usr/lib/x86_64-linux-gnu|/usr/lib"
    "GSTTAG|GStreamer tag|libgsttag-1.0.so|WARNING|gstreamer|/opt/gstreamer/lib/x86_64-linux-gnu|/usr/lib/x86_64-linux-gnu|/usr/lib"
    "GSTRTP|GStreamer RTP|libgstrtp-1.0.so|WARNING|gstreamer|/opt/gstreamer/lib/x86_64-linux-gnu|/usr/lib/x86_64-linux-gnu|/usr/lib"
    "GSTRTSP|GStreamer RTSP|libgstrtsp-1.0.so|WARNING|gstreamer|/opt/gstreamer/lib/x86_64-linux-gnu|/usr/lib/x86_64-linux-gnu|/usr/lib"
    "GSTSDP|GStreamer SDP|libgstsdp-1.0.so|WARNING|gstreamer|/opt/gstreamer/lib/x86_64-linux-gnu|/usr/lib/x86_64-linux-gnu|/usr/lib"
    "GSTALLOCATORS|GStreamer allocators|libgstallocators-1.0.so|WARNING|gstreamer|/opt/gstreamer/lib/x86_64-linux-gnu|/usr/lib/x86_64-linux-gnu|/usr/lib"
    "GSTGL|GStreamer OpenGL|libgstgl-1.0.so|WARNING|gstreamer|/opt/gstreamer/lib/x86_64-linux-gnu|/usr/lib/x86_64-linux-gnu|/usr/lib"
    "ORC|ORC optimization|liborc-0.4.so|WARNING|gstreamer|/usr/lib/x86_64-linux-gnu|/usr/lib"
    "V4L|v4l-utils|libv4l*.so|WARNING|gstreamer|/usr/lib/x86_64-linux-gnu|/usr/lib"
    
    # GStreamer plugins -> ${RPMTOP}/SOURCES/gstreamer/gstreamer-1.0
    "GSTPLUGIN_VIDEO4LINUX2|GStreamer V4L2 video capture|libgstvideo4linux2.so|WARNING|gstreamer/gstreamer-1.0|/usr/lib/x86_64-linux-gnu/gstreamer-1.0|/usr/lib/gstreamer-1.0|/opt/gstreamer/lib/x86_64-linux-gnu/gstreamer-1.0"
    "GSTPLUGIN_V4L2CODECS|GStreamer V4L2 hardware codecs|libgstv4l2codecs.so|WARNING|gstreamer/gstreamer-1.0|/usr/lib/x86_64-linux-gnu/gstreamer-1.0|/usr/lib/gstreamer-1.0|/opt/gstreamer/lib/x86_64-linux-gnu/gstreamer-1.0"
    "GSTPLUGIN_VIDEOCONVERTSCALE|GStreamer video format conversion|libgstvideoconvertscale.so|WARNING|gstreamer/gstreamer-1.0|/usr/lib/x86_64-linux-gnu/gstreamer-1.0|/usr/lib/gstreamer-1.0|/opt/gstreamer/lib/x86_64-linux-gnu/gstreamer-1.0"
    "GSTPLUGIN_VIDEORATE|GStreamer video frame rate conversion|libgstvideorate.so|WARNING|gstreamer/gstreamer-1.0|/usr/lib/x86_64-linux-gnu/gstreamer-1.0|/usr/lib/gstreamer-1.0|/opt/gstreamer/lib/x86_64-linux-gnu/gstreamer-1.0"
    "GSTPLUGIN_COREELEMENTS|GStreamer core elements|libgstcoreelements.so|WARNING|gstreamer/gstreamer-1.0|/usr/lib/x86_64-linux-gnu/gstreamer-1.0|/usr/lib/gstreamer-1.0|/opt/gstreamer/lib/x86_64-linux-gnu/gstreamer-1.0"
    "GSTPLUGIN_TYPEFIND|GStreamer type detection|libgsttypefindfunctions.so|WARNING|gstreamer/gstreamer-1.0|/usr/lib/x86_64-linux-gnu/gstreamer-1.0|/usr/lib/gstreamer-1.0|/opt/gstreamer/lib/x86_64-linux-gnu/gstreamer-1.0"
    "GSTPLUGIN_APP|GStreamer application integration|libgstapp.so|WARNING|gstreamer/gstreamer-1.0|/usr/lib/x86_64-linux-gnu/gstreamer-1.0|/usr/lib/gstreamer-1.0|/opt/gstreamer/lib/x86_64-linux-gnu/gstreamer-1.0"
    "GSTPLUGIN_PLAYBACK|GStreamer playback elements|libgstplayback.so|WARNING|gstreamer/gstreamer-1.0|/usr/lib/x86_64-linux-gnu/gstreamer-1.0|/usr/lib/gstreamer-1.0|/opt/gstreamer/lib/x86_64-linux-gnu/gstreamer-1.0"
    "GSTPLUGIN_JPEG|GStreamer JPEG codec|libgstjpeg.so|WARNING|gstreamer/gstreamer-1.0|/usr/lib/x86_64-linux-gnu/gstreamer-1.0|/usr/lib/gstreamer-1.0|/opt/gstreamer/lib/x86_64-linux-gnu/gstreamer-1.0"
    "GSTPLUGIN_XIMAGESINK|GStreamer X11 video sink|libgstximagesink.so|WARNING|gstreamer/gstreamer-1.0|/usr/lib/x86_64-linux-gnu/gstreamer-1.0|/usr/lib/gstreamer-1.0|/opt/gstreamer/lib/x86_64-linux-gnu/gstreamer-1.0"
    "GSTPLUGIN_XVIMAGESINK|GStreamer XVideo sink|libgstxvimagesink.so|WARNING|gstreamer/gstreamer-1.0|/usr/lib/x86_64-linux-gnu/gstreamer-1.0|/usr/lib/gstreamer-1.0|/opt/gstreamer/lib/x86_64-linux-gnu/gstreamer-1.0"
    "GSTPLUGIN_AUTODETECT|GStreamer auto detection|libgstautodetect.so|WARNING|gstreamer/gstreamer-1.0|/usr/lib/x86_64-linux-gnu/gstreamer-1.0|/usr/lib/gstreamer-1.0|/opt/gstreamer/lib/x86_64-linux-gnu/gstreamer-1.0"
    "GSTPLUGIN_PULSEAUDIO|GStreamer PulseAudio|libgstpulseaudio.so|WARNING|gstreamer/gstreamer-1.0|/usr/lib/x86_64-linux-gnu/gstreamer-1.0|/usr/lib/gstreamer-1.0|/opt/gstreamer/lib/x86_64-linux-gnu/gstreamer-1.0"
    "GSTPLUGIN_AUDIOPARSERS|GStreamer audio parsers|libgstaudioparsers.so|WARNING|gstreamer/gstreamer-1.0|/usr/lib/x86_64-linux-gnu/gstreamer-1.0|/usr/lib/gstreamer-1.0|/opt/gstreamer/lib/x86_64-linux-gnu/gstreamer-1.0"
    "GSTPLUGIN_AUDIOCONVERT|GStreamer audio conversion|libgstaudioconvert.so|WARNING|gstreamer/gstreamer-1.0|/usr/lib/x86_64-linux-gnu/gstreamer-1.0|/usr/lib/gstreamer-1.0|/opt/gstreamer/lib/x86_64-linux-gnu/gstreamer-1.0"
    "GSTPLUGIN_AUDIORESAMPLE|GStreamer audio resampling|libgstaudioresample.so|WARNING|gstreamer/gstreamer-1.0|/usr/lib/x86_64-linux-gnu/gstreamer-1.0|/usr/lib/gstreamer-1.0|/opt/gstreamer/lib/x86_64-linux-gnu/gstreamer-1.0"
    "LIBDW|DW support library|libdw.so|WARNING|gstreamer/gstreamer-1.0|/usr/lib/x86_64-linux-gnu|/usr/lib"
)

# Process all library configurations
echo "🔍 Copying required libraries to RPM SOURCES..."
for config in "${UNIFIED_LIBRARY_CONFIGS[@]}"; do
    IFS='|' read -r var_name display_name lib_pattern severity target_subdir search_dirs_str <<< "$config"
    IFS='|' read -ra search_dirs <<< "$search_dirs_str"
    
    # Determine full target directory
    if [ -z "$target_subdir" ]; then
        target_dir="${RPMTOP}/SOURCES"
    else
        target_dir="${RPMTOP}/SOURCES/${target_subdir}"
    fi
    
    copy_libraries "$var_name" "$display_name" "$lib_pattern" "$severity" "$target_dir" "${search_dirs[@]}"
done

echo "📋 RPM: Final SOURCES directory contents:"
ls -lah "${RPMTOP}/SOURCES/" | head -30

# Update the binary's rpath to point to /usr/lib/openterfaceqt for RPM
# CRITICAL: The binary needs BOTH RPATH and possibly RUNPATH
if [ -f "${RPMTOP}/SOURCES/openterfaceQT" ]; then
    echo "Updating binary loader paths for RPM bundled libraries..."
    echo "   This makes bundled Qt libraries load BEFORE system Qt libraries"
    
    # First, try to REMOVE any existing RPATH/RUNPATH to start clean
    patchelf --remove-rpath "${RPMTOP}/SOURCES/openterfaceQT" 2>/dev/null || true
    
    # Set RPATH (preferred, checked by glibc before /lib64)
    RPATH_DIRS='/usr/lib/openterfaceqt/qt6:/usr/lib/openterfaceqt/ffmpeg:/usr/lib/openterfaceqt/gstreamer:/usr/lib/openterfaceqt'
    echo "   Setting RPATH to: $RPATH_DIRS"
    patchelf --set-rpath "$RPATH_DIRS" "${RPMTOP}/SOURCES/openterfaceQT"
    
    # Verify RPATH was set
    ACTUAL_RPATH=$(patchelf --print-rpath "${RPMTOP}/SOURCES/openterfaceQT" 2>/dev/null || echo "FAILED")
    echo "   ✅ RPATH set to: $ACTUAL_RPATH"
    
    # CRITICAL: Also check if we need to set RUNPATH (used by some linkers)
    # On systems with LD_LIBRARY_PATH, RUNPATH takes precedence, so we want it too
    if patchelf --help 2>/dev/null | grep -q "set-runpath"; then
        echo "   Setting RUNPATH (for LD_LIBRARY_PATH precedence)..."
        patchelf --set-runpath "$RPATH_DIRS" "${RPMTOP}/SOURCES/openterfaceQT" 2>/dev/null || true
    fi
    
    # Verify final state
    echo "   Final loader configuration:"
    patchelf --print-rpath "${RPMTOP}/SOURCES/openterfaceQT" 2>/dev/null | sed 's/^/     RPATH: /'
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

# Copy desktop file to SOURCES
if [ -f "${SRC}/packaging/com.openterface.openterfaceQT.desktop" ]; then
	mkdir -p "${RPMTOP}/SOURCES/packaging"
	cp "${SRC}/packaging/com.openterface.openterfaceQT.desktop" "${RPMTOP}/SOURCES/packaging/"
	echo "✅ Desktop file copied to SOURCES"
else
	echo "Warning: Desktop file not found at ${SRC}/packaging/com.openterface.openterfaceQT.desktop" >&2
fi

# Normalize library symlinks in SOURCES before building RPM
# This ensures ldconfig doesn't complain about non-symlink files during installation
echo "📋 RPM: Normalizing library symlinks in SOURCES..."
if [ -d "${RPMTOP}/SOURCES" ]; then
	cd "${RPMTOP}/SOURCES"
	
	# Process all library files to create proper symlink chains
	# Find all .so.X.X.X files and create symlinks for .so.X and .so
	for fullfile in *.so.*.* ; do
		[ -f "$fullfile" ] || continue
		
		# Extract base name (e.g., libQt6Core from libQt6Core.so.6.6.3)
		base=$(echo "$fullfile" | sed 's/\.so\..*//')
		
		# Extract soname (e.g., libQt6Core.so.6 from libQt6Core.so.6.6.3)
		soname=$(echo "$fullfile" | sed 's/\(.*\.so\.[0-9]*\)\.*.*/\1/')
		
		# Create major version symlink if needed
		if [ "$soname" != "$fullfile" ]; then
			if [ ! -L "$soname" ] && [ ! -f "$soname" ]; then
				ln -sf "$fullfile" "$soname"
				echo "   ✅ Created symlink: $soname -> $fullfile"
			elif [ -f "$soname" ]; then
				# If real file exists, remove and replace with symlink
				rm -f "$soname"
				ln -sf "$fullfile" "$soname"
				echo "   ✅ Converted to symlink: $soname -> $fullfile"
			fi
		fi
		
		# Create base .so symlink if needed
		if [ ! -L "$base.so" ] && [ ! -f "$base.so" ]; then
			ln -sf "$fullfile" "$base.so"
			echo "   ✅ Created symlink: $base.so -> $fullfile"
		fi
	done
	
	echo "✅ Library symlinks normalized in SOURCES"
	cd - > /dev/null
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
