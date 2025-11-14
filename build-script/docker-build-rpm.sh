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

# Copy SVG icon engine plugin (libsvgicon.so) - OPTIONAL for RPM size optimization
echo "   📦 Checking for SVG icon usage in application..."
SVGICON_PLUGIN_FOUND=0

# Only bundle SVG icon plugin if icons actually use SVG format (size optimization)
if find "${SRC}/images" -name "*.svg" 2>/dev/null | grep -q .; then
    echo "   ℹ️  SVG icons detected, attempting to bundle SVG icon engine..."
    
    for SEARCH_DIR in /opt/Qt6/plugins /usr/lib/qt6/plugins /usr/lib/x86_64-linux-gnu/qt6/plugins; do
        if [ -d "$SEARCH_DIR/iconengines" ]; then
            FOUND_FILES=$(find "$SEARCH_DIR/iconengines" -name "libsvgicon.so*" 2>/dev/null)
            if [ -n "$FOUND_FILES" ]; then
                echo "   ✅ Found libsvgicon.so, bundling for SVG icon support..."
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
    echo "   ⚠️  SVG icons found but libsvgicon.so plugin not available - icons will use fallback rendering"
fi

# Copy bzip2 libraries to SOURCES for bundling (needed for compression support in FFmpeg)
echo "🔍 Searching for bzip2 libraries to RPM SOURCES..."

# Copy libbz2 libraries - search multiple locations
echo "📋 RPM: Searching for libbz2 libraries..."
LIBBZ2_FOUND=0
for SEARCH_DIR in /opt/ffmpeg/lib /usr/lib/x86_64-linux-gnu /usr/lib; do
    echo "   Checking: $SEARCH_DIR"
    if [ -d "$SEARCH_DIR" ]; then
        if ls "$SEARCH_DIR"/libbz2.so* >/dev/null 2>&1; then
            echo "   ✅ Found libbz2 in $SEARCH_DIR"
            LIBBZ2_FILES=$(ls -la "$SEARCH_DIR"/libbz2.so*)
            echo "   Files found:"
            echo "$LIBBZ2_FILES" | sed 's/^/     /'
            # Only copy actual files (not symlinks) to avoid duplication
            find "$SEARCH_DIR" -maxdepth 1 -name "libbz2.so*" -type f -exec cp -av {} "${RPMTOP}/SOURCES/" \; 2>&1 | sed 's/^/     /'
            echo "   ✅ libbz2 libraries copied to ${RPMTOP}/SOURCES"
            LIBBZ2_FOUND=1
            break
        else
            echo "   ✗ No libbz2 found in $SEARCH_DIR"
        fi
    else
        echo "   ✗ Directory does not exist: $SEARCH_DIR"
    fi
done
if [ $LIBBZ2_FOUND -eq 0 ]; then
    echo "❌ ERROR: libbz2 libraries not found in any search path!"
else
    echo "✅ libbz2 found and copied"
fi

# Copy libusb libraries to SOURCES for bundling (needed for USB device access)
echo "🔍 Searching for libusb libraries to RPM SOURCES..."

# Copy libusb libraries - search multiple locations
echo "📋 RPM: Searching for libusb libraries..."
LIBUSB_FOUND=0
for SEARCH_DIR in /opt/libusb/lib /opt/ffmpeg/lib /usr/lib/x86_64-linux-gnu /usr/lib; do
    echo "   Checking: $SEARCH_DIR"
    if [ -d "$SEARCH_DIR" ]; then
        if ls "$SEARCH_DIR"/libusb*.so* >/dev/null 2>&1; then
            echo "   ✅ Found libusb in $SEARCH_DIR"
            LIBUSB_FILES=$(ls -la "$SEARCH_DIR"/libusb*.so*)
            echo "   Files found:"
            echo "$LIBUSB_FILES" | sed 's/^/     /'
            # Only copy actual files (not symlinks) to avoid duplication
            find "$SEARCH_DIR" -maxdepth 1 -name "libusb*.so*" -type f -exec cp -av {} "${RPMTOP}/SOURCES/" \; 2>&1 | sed 's/^/     /'
            echo "   ✅ libusb libraries copied to ${RPMTOP}/SOURCES"
            LIBUSB_FOUND=1
            break
        else
            echo "   ✗ No libusb found in $SEARCH_DIR"
        fi
    else
        echo "   ✗ Directory does not exist: $SEARCH_DIR"
    fi
done
if [ $LIBUSB_FOUND -eq 0 ]; then
    echo "❌ ERROR: libusb libraries not found in any search path!"
else
    echo "✅ libusb found and copied"
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
            # Only copy actual files (not symlinks) to avoid duplication
            find "$SEARCH_DIR" -maxdepth 1 -name "libjpeg.so*" -type f -exec cp -av {} "${RPMTOP}/SOURCES/" \; 2>&1 | sed 's/^/     /'
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
            # Only copy actual files (not symlinks) to avoid duplication
            find "$SEARCH_DIR" -maxdepth 1 -name "libturbojpeg.so*" -type f -exec cp -Pv {} "${RPMTOP}/SOURCES/" \; 2>&1 | sed 's/^/     /'
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
            # Only copy actual files (not symlinks) to avoid duplication
            find "$SEARCH_DIR" -maxdepth 1 -name "libva*.so*" -type f -exec cp -av {} "${RPMTOP}/SOURCES/" \; 2>&1 | sed 's/^/     /'
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

# Copy VDPAU libraries for hardware acceleration - search multiple locations
echo "📋 RPM: Searching for VDPAU libraries..."
VDPAU_FOUND=0
for SEARCH_DIR in /usr/lib/x86_64-linux-gnu /usr/lib; do
    echo "   Checking: $SEARCH_DIR"
    if [ -d "$SEARCH_DIR" ]; then
        if ls "$SEARCH_DIR"/libvdpau.so* >/dev/null 2>&1; then
            echo "   ✅ Found VDPAU libraries in $SEARCH_DIR"
            VDPAU_FILES=$(ls -la "$SEARCH_DIR"/libvdpau*.so*)
            echo "   Files found:"
            echo "$VDPAU_FILES" | sed 's/^/     /'
            # Only copy actual files (not symlinks) to avoid duplication
            find "$SEARCH_DIR" -maxdepth 1 -name "libvdpau*.so*" -type f -exec cp -av {} "${RPMTOP}/SOURCES/" \; 2>&1 | sed 's/^/     /'
            echo "   ✅ VDPAU libraries copied to ${RPMTOP}/SOURCES"
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
else
    echo "✅ VDPAU found and copied"
fi

# Copy core FFmpeg libraries (libavdevice, libavcodec, libavformat, libavutil, libswscale, libswresample, libavfilter)
echo "📋 RPM: Searching for FFmpeg core libraries..."
FFMPEG_FOUND=0
FFMPEG_LIBS=(libavdevice.so libavcodec.so libavformat.so libavutil.so libswscale.so libswresample.so libavfilter.so)

mkdir -p "${RPMTOP}/SOURCES/ffmpeg"

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
                    # Only copy versioned files (e.g., libavformat.so.60.16.100), not symlinks (libavformat.so, libavformat.so.60)
                    # This avoids duplicating symlinks that point to the same actual library file
                    find "$SEARCH_DIR" -maxdepth 1 -name "${ffmpeg_lib}*" -type f -exec cp -Pv {} "${RPMTOP}/SOURCES/ffmpeg/" \; 2>&1 | sed 's/^/     /'
                fi
            done
            echo "   ✅ FFmpeg core libraries copied to ${RPMTOP}/SOURCES/ffmpeg"
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
echo "📋 RPM: Searching for GStreamer libraries..."
GSTREAMER_FOUND=0

mkdir -p "${RPMTOP}/SOURCES/gstreamer"

# List of essential GStreamer libraries to bundle
GSTREAMER_LIBS=(
    "libgstreamer-1.0.so"       # Core GStreamer library
    "libgstbase-1.0.so"         # Base classes for GStreamer plugins
    "libgstaudio-1.0.so"        # Audio support library
    "libgstpbutils-1.0.so"      # Playback utility library
    "libgstvideo-1.0.so"        # Video support library
    "libgstapp-1.0.so"          # Application integration library
)

for SEARCH_DIR in /opt/gstreamer/lib/x86_64-linux-gnu /usr/lib/x86_64-linux-gnu /usr/lib; do
    echo "   Checking: $SEARCH_DIR"
    if [ -d "$SEARCH_DIR" ]; then
        LIBS_FOUND_IN_DIR=0
        
        # Search for all essential GStreamer libraries
        for gst_lib in "${GSTREAMER_LIBS[@]}"; do
            if ls "$SEARCH_DIR"/${gst_lib}* >/dev/null 2>&1; then
                LIBS_FOUND_IN_DIR=$((LIBS_FOUND_IN_DIR + 1))
            fi
        done
        
        if [ $LIBS_FOUND_IN_DIR -gt 0 ]; then
            echo "   ✅ Found GStreamer libraries in $SEARCH_DIR"
            
            # Copy each GStreamer library
            for gst_lib in "${GSTREAMER_LIBS[@]}"; do
                if ls "$SEARCH_DIR"/${gst_lib}* >/dev/null 2>&1; then
                    echo "      📦 Copying $gst_lib..."
                    GSTREAMER_FILES=$(ls -la "$SEARCH_DIR"/${gst_lib}* 2>/dev/null)
                    echo "$GSTREAMER_FILES" | sed 's/^/         /'
                    # Only copy actual files (not symlinks) to avoid duplication
                    find "$SEARCH_DIR" -maxdepth 1 -name "${gst_lib}*" -type f -exec cp -Pv {} "${RPMTOP}/SOURCES/gstreamer/" \; 2>&1 | sed 's/^/         /'
                else
                    echo "      ⚠️  $gst_lib not found in $SEARCH_DIR"
                fi
            done
            
            echo "   ✅ GStreamer libraries copied to ${RPMTOP}/SOURCES/gstreamer"
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
    echo "✅ GStreamer libraries found and copied"
fi

# Copy ORC libraries (needed by GStreamer) - search multiple locations
echo "📋 RPM: Searching for ORC libraries..."
ORC_FOUND=0
for SEARCH_DIR in /usr/lib/x86_64-linux-gnu /usr/lib; do
    echo "   Checking: $SEARCH_DIR"
    if [ -d "$SEARCH_DIR" ]; then
        if ls "$SEARCH_DIR"/liborc-0.4.so* >/dev/null 2>&1; then
            echo "   ✅ Found ORC libraries in $SEARCH_DIR"
            ORC_FILES=$(ls -la "$SEARCH_DIR"/liborc-0.4.so*)
            echo "   Files found:"
            echo "$ORC_FILES" | sed 's/^/     /'
            # Only copy actual files (not symlinks) to avoid duplication
            find "$SEARCH_DIR" -maxdepth 1 -name "liborc-0.4.so*" -type f -exec cp -Pv {} "${RPMTOP}/SOURCES/gstreamer/" \; 2>&1 | sed 's/^/     /'
            echo "   ✅ ORC libraries copied to ${RPMTOP}/SOURCES/gstreamer"
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
    echo "✅ ORC libraries found and copied"
fi

# Copy GStreamer plugins (essential for video capture)
echo "📋 RPM: Searching for GStreamer plugins..."
GSTREAMER_PLUGINS_FOUND=0

# Essential GStreamer plugins for video capture
GSTREAMER_PLUGINS=(
    "libgstvideo4linux2.so"        # V4L2 video capture (CRITICAL)
    "libgstv4l2codecs.so"          # V4L2 hardware codecs
    "libgstvideoconvertscale.so"   # Video format conversion and scaling
    "libgstvideorate.so"           # Video frame rate conversion
    "libgstcoreelements.so"        # Core elements (queue, filesrc, etc.)
    "libgsttypefindfunctions.so"   # Type detection
    "libgstapp.so"                 # Application integration
    "libgstplayback.so"           # Playback elements
    "libgstjpeg.so"               # JPEG codec
    "libgstximagesink.so"         # X11 video sink
    "libgstxvimagesink.so"        # XVideo sink
    "libgstautodetect.so"         # Auto detection
    "libgstpulseaudio.so"         # PulseAudio
    "libgstaudioparsers.so"       # Audio parsers
    "libgstaudioconvert.so"       # Audio conversion
    "libgstaudioresample.so"      # Audio resampling
    "libdw.so"                   # DW support (dependency for some plugins) 
)

# Detect GStreamer plugin directories
GSTREAMER_PLUGIN_DIR=""
for SEARCH_DIR in /usr/lib/x86_64-linux-gnu/gstreamer-1.0 /usr/lib/gstreamer-1.0 /opt/gstreamer/lib/x86_64-linux-gnu/gstreamer-1.0; do
    if [ -d "$SEARCH_DIR" ] && ls "$SEARCH_DIR"/libgstvideo4linux2.so >/dev/null 2>&1; then
        GSTREAMER_PLUGIN_DIR="$SEARCH_DIR"
        echo "   ✅ Found GStreamer plugins directory: $GSTREAMER_PLUGIN_DIR"
        break
    fi
done

if [ -n "$GSTREAMER_PLUGIN_DIR" ]; then
    mkdir -p "${RPMTOP}/SOURCES/gstreamer/gstreamer-1.0"
    PLUGINS_COPIED=0
    
    for plugin in "${GSTREAMER_PLUGINS[@]}"; do
        if [ -f "$GSTREAMER_PLUGIN_DIR/$plugin" ]; then
            cp -Pv "$GSTREAMER_PLUGIN_DIR/$plugin" "${RPMTOP}/SOURCES/gstreamer/gstreamer-1.0/" 2>&1 | sed 's/^/     /'
            PLUGINS_COPIED=$((PLUGINS_COPIED + 1))
        else
            echo "     ⚠️  Missing plugin: $plugin"
        fi
    done
    
    echo "   ✅ GStreamer plugins copied ($PLUGINS_COPIED plugins)"
    GSTREAMER_PLUGINS_FOUND=1
else
    echo "   ⚠️  GStreamer plugins directory not found, checking if gstreamer packages are installed..."
    if dpkg -l | grep -q gstreamer1.0-plugins; then
        echo "   ℹ️  GStreamer plugins packages are installed on the system"
        echo "   They will be required as a dependency in the final RPM package"
    fi
fi

if [ $GSTREAMER_PLUGINS_FOUND -eq 0 ]; then
    echo "⚠️  Warning: GStreamer plugins not found in binary form"
    echo "   Note: GStreamer plugins should be listed as dependencies in the RPM package"
else
    echo "✅ GStreamer plugins bundled successfully"
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
            # Only copy actual files (not symlinks) to avoid duplication
            find "$SEARCH_DIR" -maxdepth 1 -name "libv4l*.so*" -type f -exec cp -Pv {} "${RPMTOP}/SOURCES/gstreamer/" \; 2>&1 | sed 's/^/     /'
            echo "   ✅ v4l-utils libraries copied to ${RPMTOP}/SOURCES/gstreamer"
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
            # Only copy actual files (not symlinks) to avoid duplication
            find "$SEARCH_DIR" -maxdepth 1 -name "libgstvideo-1.0.so*" -type f -exec cp -Pv {} "${RPMTOP}/SOURCES/gstreamer/" \; 2>&1 | sed 's/^/     /'
            echo "   ✅ GStreamer video libraries copied to ${RPMTOP}/SOURCES/gstreamer"
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
            # Only copy actual files (not symlinks) to avoid duplication
            find "$SEARCH_DIR" -maxdepth 1 -name "libgstapp-1.0.so*" -type f -exec cp -Pv {} "${RPMTOP}/SOURCES/gstreamer/" \; 2>&1 | sed 's/^/     /'
            echo "   ✅ GStreamer app libraries copied to ${RPMTOP}/SOURCES/gstreamer"
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
# CRITICAL: RPATH must come BEFORE system paths to override /lib64 and /usr/lib
if [ -f "${RPMTOP}/SOURCES/openterfaceQT" ]; then
    echo "Updating rpath for RPM bundled libraries..."
    echo "   Setting RPATH to prioritize bundled libraries over system"
    echo "   RPATH: /usr/lib/openterfaceqt/qt6:/usr/lib/openterfaceqt/ffmpeg:/usr/lib/openterfaceqt/gstreamer:/usr/lib/openterfaceqt"
    
    # Set RPATH with multiple priority entries
    # This ensures bundled libraries are found FIRST before system libraries
    patchelf --set-rpath '/usr/lib/openterfaceqt/qt6:/usr/lib/openterfaceqt/ffmpeg:/usr/lib/openterfaceqt/gstreamer:/usr/lib/openterfaceqt' \
             "${RPMTOP}/SOURCES/openterfaceQT"
    
    # Verify RPATH was set correctly
    ACTUAL_RPATH=$(patchelf --print-rpath "${RPMTOP}/SOURCES/openterfaceQT" 2>/dev/null || echo "FAILED")
    echo "   ✅ RPATH set to: $ACTUAL_RPATH"
    
    # Also strip RUNPATH (if exists) to prevent secondary search
    patchelf --remove-rpath "${RPMTOP}/SOURCES/openterfaceQT" 2>/dev/null || true
    patchelf --set-rpath '/usr/lib/openterfaceqt/qt6:/usr/lib/openterfaceqt/ffmpeg:/usr/lib/openterfaceqt/gstreamer:/usr/lib/openterfaceqt' \
             "${RPMTOP}/SOURCES/openterfaceQT"
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
