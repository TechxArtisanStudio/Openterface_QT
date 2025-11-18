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

# Verify Qt6 build exists
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

# Copy Qt plugins to SOURCES (SELECTIVE: only essential plugins to reduce RPM size)
QT_PLUGIN_DIR="/opt/Qt6/plugins"
if [ -d "${QT_PLUGIN_DIR}" ]; then
    echo "📋 RPM: Qt plugins will be copied via unified library configuration..."
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
else
    echo "   ℹ️  No SVG icons detected - skipping SVG library bundling (size optimization: ~5-10MB saved)"
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
mkdir -p "${RPMTOP}/SOURCES/gstreamer/gstreamer-1.0"
mkdir -p "${RPMTOP}/SOURCES/qt6"
mkdir -p "${RPMTOP}/SOURCES/qt6/plugins"
mkdir -p "${RPMTOP}/SOURCES/qt6/plugins/imageformats"
mkdir -p "${RPMTOP}/SOURCES/qt6/plugins/iconengines"
mkdir -p "${RPMTOP}/SOURCES/qt6/plugins/platforms"
mkdir -p "${RPMTOP}/SOURCES/qt6/plugins/platforminputcontexts"


# Common search directories for Qt6 libraries
QT_LIB_SEARCH_DIRS="/opt/Qt6/lib|/usr/lib/x86_64-linux-gnu|/usr/lib"

# Common search directories for Qt6 plugins
QT_PLUGIN_SEARCH_DIRS="/opt/Qt6/plugins|/opt/Qt6/plugins/imageformats|/opt/Qt6/plugins/iconengines|/opt/Qt6/plugins"

# Common search directories for FFmpeg libraries
FFMPEG_LIB_SEARCH_DIRS="/opt/ffmpeg/lib|/usr/lib/x86_64-linux-gnu|/usr/lib"

# Common search directories for GStreamer libraries
GSTREAMER_LIB_SEARCH_DIRS="/opt/gstreamer/lib/x86_64-linux-gnu|/usr/lib/x86_64-linux-gnu|/usr/lib"

# Common search directories for GStreamer plugins
GSTREAMER_PLUGIN_SEARCH_DIRS="/opt/gstreamer/lib/x86_64-linux-gnu/gstreamer-1.0|/usr/lib/x86_64-linux-gnu/gstreamer-1.0|/usr/lib/gstreamer-1.0"

# Unified library configurations with target subdirectories
declare -a UNIFIED_LIBRARY_CONFIGS=(
    # Qt6 libraries -> ${RPMTOP}/SOURCES/qt6
    # Using common Qt6 library search directories
    "QTCORE|Qt6 core|libQt6Core.so|ERROR|qt6|${QT_LIB_SEARCH_DIRS}"
    "QTGUI|Qt6 gui|libQt6Gui.so|ERROR|qt6|${QT_LIB_SEARCH_DIRS}"
    "QTWIDGETS|Qt6 widgets|libQt6Widgets.so|ERROR|qt6|${QT_LIB_SEARCH_DIRS}"
    # Common search directories for Qt6 libraries
    "QTSERIALPORT|Qt6 serial port|libQt6SerialPort.so|WARNING|qt6|${QT_LIB_SEARCH_DIRS}"
    "QTSHADERTOOLS|Qt6 shader tools|libQt6ShaderTools.so|WARNING|qt6|${QT_LIB_SEARCH_DIRS}"
    "QTSVGWIDGETS|Qt6 SVG widgets|libQt6SvgWidgets.so|WARNING|qt6|${QT_LIB_SEARCH_DIRS}"
    "QTUITOOLS|Qt6 UI tools|libQt6UiTools.so|WARNING|qt6|${QT_LIB_SEARCH_DIRS}"
    "QTXML|Qt6 XML|libQt6Xml.so|WARNING|qt6|${QT_LIB_SEARCH_DIRS}"
    "QTTEST|Qt6 test|libQt6Test.so|WARNING|qt6|${QT_LIB_SEARCH_DIRS}"
    "QTQML|Qt6 qml|libQt6Qml.so|ERROR|qt6|${QT_LIB_SEARCH_DIRS}"
    "QTQMLCOMPILER|Qt6 QML compiler|libQt6QmlCompiler.so|WARNING|qt6|${QT_LIB_SEARCH_DIRS}"
    "QTQMLCORE|Qt6 QML core|libQt6QmlCore.so|WARNING|qt6|${QT_LIB_SEARCH_DIRS}"
    "QTQMLXMLLISTMODEL|Qt6 QML XML list model|libQt6QmlXmlListModel.so|WARNING|qt6|${QT_LIB_SEARCH_DIRS}"
    "QTQMLMODELS|Qt6 QML models|libQt6QmlModels.so|WARNING|qt6|${QT_LIB_SEARCH_DIRS}"
    "QTQMLWORKERSCRIPT|Qt6 QML worker script|libQt6QmlWorkerScript.so|WARNING|qt6|${QT_LIB_SEARCH_DIRS}"
    "QTQUICK|Qt6 quick|libQt6Quick.so|WARNING|qt6|${QT_LIB_SEARCH_DIRS}"
    "QTWAYLANDCLIENT|Qt6 Wayland Client|libQt6WaylandClient.so|WARNING|qt6|${QT_LIB_SEARCH_DIRS}"
    "QTXCBQPA|Qt6 XCB QPA|libQt6XcbQpa.so|ERROR|qt6|${QT_LIB_SEARCH_DIRS}"
    "QTCORE5COMPAT|Qt6 core5 compat|libQt6Core5Compat.so|WARNING|qt6|${QT_LIB_SEARCH_DIRS}"
    "QTGUIPRIVATE|Qt6 gui private|libQt6GuiPrivate.so|WARNING|qt6|${QT_LIB_SEARCH_DIRS}"
    "QTDBUS|Qt6 D-Bus|libQt6DBus.so|WARNING|qt6|${QT_LIB_SEARCH_DIRS}"
    "QTMULTIMEDIA|Qt6 multimedia|libQt6Multimedia.so|WARNING|qt6|${QT_LIB_SEARCH_DIRS}"
    "QTMULTIMEDIAQUICK|Qt6 multimedia quick|libQt6MultimediaQuick.so|WARNING|qt6|${QT_LIB_SEARCH_DIRS}"
    "QTMULTIMEDIAWIDGETS|Qt6 multimedia widgets|libQt6MultimediaWidgets.so|WARNING|qt6|${QT_LIB_SEARCH_DIRS}"
    "QTNETWORK|Qt6 network|libQt6Network.so|WARNING|qt6|${QT_LIB_SEARCH_DIRS}"
    "QTOPENGL|Qt6 OpenGL|libQt6OpenGL.so|WARNING|qt6|${QT_LIB_SEARCH_DIRS}"
    "QTQUICKCONTROLS2|Qt6 quick controls 2|libQt6QuickControls2.so|WARNING|qt6|${QT_LIB_SEARCH_DIRS}"
    "QTQUICKSHAPES|Qt6 quick shapes|libQt6QuickShapes.so|WARNING|qt6|${QT_LIB_SEARCH_DIRS}"
    "QTQUICKLAYOUTS|Qt6 quick layouts|libQt6QuickLayouts.so|WARNING|qt6|${QT_LIB_SEARCH_DIRS}"
    "QTQUICKTEMPLATES2|Qt6 quick templates 2|libQt6QuickTemplates2.so|WARNING|qt6|${QT_LIB_SEARCH_DIRS}"
    "QTQUICKPARTICLES|Qt6 quick particles|libQt6QuickParticles.so|WARNING|qt6|${QT_LIB_SEARCH_DIRS}"
    "QTOPENGLWIDGETS|Qt6 opengl widgets|libQt6OpenGLWidgets.so|WARNING|qt6|${QT_LIB_SEARCH_DIRS}"
    "QTSVG|Qt6 svg|libQt6Svg.so|WARNING|qt6|${QT_LIB_SEARCH_DIRS}"
    
    # Qt6 plugins -> ${RPMTOP}/SOURCES/qt6/plugins/imageformats and iconengines
    # Using common Qt6 plugin search directories
    "QTPLUGIN_SVG|Qt6 SVG image format plugin|libqsvg.so|WARNING|qt6/plugins/imageformats|${QT_PLUGIN_SEARCH_DIRS}"
    "QTPLUGIN_SVGICON|Qt6 SVG icon engine plugin|libqsvgicon.so|WARNING|qt6/plugins/iconengines|${QT_PLUGIN_SEARCH_DIRS}"
    
    # Qt6 platform plugins -> ${RPMTOP}/SOURCES/qt6/plugins/platforms
    "QTPLUGIN_QXC|Qt6 XCB platform|libqxcb.so|ERROR|qt6/plugins/platforms|${QT_PLUGIN_SEARCH_DIRS}/platforms"
    "QTPLUGIN_WAYLAND_EGL|Qt6 Wayland EGL platform|libqwayland-egl.so|WARNING|qt6/plugins/platforms|${QT_PLUGIN_SEARCH_DIRS}/platforms"
    "QTPLUGIN_WAYLAND_GENERIC|Qt6 Wayland generic platform|libqwayland-generic.so|WARNING|qt6/plugins/platforms|${QT_PLUGIN_SEARCH_DIRS}/platforms"
    "QTPLUGIN_EGLFS|Qt6 EGLFS platform|libqeglfs.so|WARNING|qt6/plugins/platforms|${QT_PLUGIN_SEARCH_DIRS}/platforms"
    "QTPLUGIN_MINIMAL|Qt6 minimal platform|libqminimal.so|WARNING|qt6/plugins/platforms|${QT_PLUGIN_SEARCH_DIRS}/platforms"
    "QTPLUGIN_MINIMALEGL|Qt6 minimal EGL platform|libqminimalegl.so|WARNING|qt6/plugins/platforms|${QT_PLUGIN_SEARCH_DIRS}/platforms"
    "QTPLUGIN_LINUXFB|Qt6 Linux framebuffer platform|libqlinuxfb.so|WARNING|qt6/plugins/platforms|${QT_PLUGIN_SEARCH_DIRS}/platforms"
    "QTPLUGIN_OFFSCREEN|Qt6 offscreen platform|libqoffscreen.so|WARNING|qt6/plugins/platforms|${QT_PLUGIN_SEARCH_DIRS}/platforms"
    "QTPLUGIN_VNC|Qt6 VNC platform|libqvnc.so|WARNING|qt6/plugins/platforms|${QT_PLUGIN_SEARCH_DIRS}/platforms"
    "QTPLUGIN_VKKHRDISPLAY|Qt6 VK KHR display platform|libqvkkhrdisplay.so|WARNING|qt6/plugins/platforms|${QT_PLUGIN_SEARCH_DIRS}/platforms"
    
    # Qt6 platform input context plugins -> ${RPMTOP}/SOURCES/qt6/plugins/platforminputcontexts
    "QTPLUGIN_COMPOSE|Qt6 compose platform input context|libcomposeplatforminputcontextplugin.so|WARNING|qt6/plugins/platforminputcontexts|${QT_PLUGIN_SEARCH_DIRS}/platforminputcontexts"
    "QTPLUGIN_IBUS|Qt6 IBus platform input context|libibusplatforminputcontextplugin.so|WARNING|qt6/plugins/platforminputcontexts|${QT_PLUGIN_SEARCH_DIRS}/platforminputcontexts"
    
    # GPU/Rendering libraries (CRITICAL for display)
    "EGL|libEGL|libEGL.so|WARNING||/lib/x86_64-linux-gnu|/opt/Qt6/lib|/usr/lib/x86_64-linux-gnu|/usr/lib|/usr/lib64|/lib"
    "GL|libGL|libGL.so|WARNING||/lib/x86_64-linux-gnu|/opt/Qt6/lib|/usr/lib/x86_64-linux-gnu|/usr/lib|/usr/lib64|/lib"
    "GLX|libGLX|libGLX.so|WARNING||/lib/x86_64-linux-gnu|/opt/Qt6/lib|/usr/lib/x86_64-linux-gnu|/usr/lib|/usr/lib64|/lib"
    "GLESV2|libGLESv2|libGLESv2.so|WARNING||/lib/x86_64-linux-gnu|/opt/Qt6/lib|/usr/lib/x86_64-linux-gnu|/usr/lib|/usr/lib64|/lib"
    "GLVND|libglvnd|libglvnd.so|WARNING||/lib/x86_64-linux-gnu|/opt/Qt6/lib|/usr/lib/x86_64-linux-gnu|/usr/lib|/usr/lib64|/lib"
    "GLDISPATCH|libGLdispatch|libGLdispatch.so|WARNING||/lib/x86_64-linux-gnu|/opt/Qt6/lib|/usr/lib/x86_64-linux-gnu|/usr/lib|/usr/lib64|/lib"
    "OPENGL|libOpenGL|libOpenGL.so|WARNING||/lib/x86_64-linux-gnu|/opt/Qt6/lib|/usr/lib/x86_64-linux-gnu|/usr/lib|/usr/lib64|/lib"
    
    # Core media libraries
    "LIBUSB|libusb|libusb*.so|ERROR||/opt/libusb/lib|/opt/ffmpeg/lib|/usr/lib/x86_64-linux-gnu|/usr/lib"
    "JPEG|libjpeg|libjpeg.so|ERROR||/opt/ffmpeg/lib|/usr/lib/x86_64-linux-gnu|/usr/lib"
    "TURBOJPEG|libturbojpeg|libturbojpeg.so|ERROR||/opt/ffmpeg/lib|/usr/lib/x86_64-linux-gnu|/usr/lib"
    "VA|VA-API|libva.so|WARNING||/usr/lib/x86_64-linux-gnu|/usr/lib"
    "VADRM|VA-API DRM|libva-drm.so|WARNING||/usr/lib/x86_64-linux-gnu|/usr/lib"
    "VAX11|VA-API X11|libva-x11.so|WARNING||/usr/lib/x86_64-linux-gnu|/usr/lib"
    "VDPAU|VDPAU|libvdpau.so|WARNING||/usr/lib/x86_64-linux-gnu|/usr/lib"
    
    # FFmpeg libraries -> ${RPMTOP}/SOURCES/ffmpeg
    # Using common FFmpeg library search directories
    "AVDEVICE|FFmpeg avdevice|libavdevice.so|WARNING|ffmpeg|${FFMPEG_LIB_SEARCH_DIRS}"
    "AVCODEC|FFmpeg avcodec|libavcodec.so|WARNING|ffmpeg|${FFMPEG_LIB_SEARCH_DIRS}"
    "AVFORMAT|FFmpeg avformat|libavformat.so|WARNING|ffmpeg|${FFMPEG_LIB_SEARCH_DIRS}"
    "AVUTIL|FFmpeg avutil|libavutil.so|WARNING|ffmpeg|${FFMPEG_LIB_SEARCH_DIRS}"
    "SWSCALE|FFmpeg swscale|libswscale.so|WARNING|ffmpeg|${FFMPEG_LIB_SEARCH_DIRS}"
    "SWRESAMPLE|FFmpeg swresample|libswresample.so|WARNING|ffmpeg|${FFMPEG_LIB_SEARCH_DIRS}"
    "AVFILTER|FFmpeg avfilter|libavfilter.so|WARNING|ffmpeg|${FFMPEG_LIB_SEARCH_DIRS}"
    
    # GStreamer libraries -> ${RPMTOP}/SOURCES/gstreamer
    # Using common GStreamer library search directories
    "GSTREAMER|GStreamer core|libgstreamer-1.0.so|WARNING|gstreamer|${GSTREAMER_LIB_SEARCH_DIRS}"
    "GSTBASE|GStreamer base|libgstbase-1.0.so|WARNING|gstreamer|${GSTREAMER_LIB_SEARCH_DIRS}"
    "GSTAUDIO|GStreamer audio|libgstaudio-1.0.so|WARNING|gstreamer|${GSTREAMER_LIB_SEARCH_DIRS}"
    "GSTPBUTILS|GStreamer playback utils|libgstpbutils-1.0.so|WARNING|gstreamer|${GSTREAMER_LIB_SEARCH_DIRS}"
    "GSTVIDEO|GStreamer video|libgstvideo-1.0.so|WARNING|gstreamer|${GSTREAMER_LIB_SEARCH_DIRS}"
    "GSTAPP|GStreamer app|libgstapp-1.0.so|WARNING|gstreamer|${GSTREAMER_LIB_SEARCH_DIRS}"
    "GSTTAG|GStreamer tag|libgsttag-1.0.so|WARNING|gstreamer|${GSTREAMER_LIB_SEARCH_DIRS}"
    "GSTRTP|GStreamer RTP|libgstrtp-1.0.so|WARNING|gstreamer|${GSTREAMER_LIB_SEARCH_DIRS}"
    "GSTRTSP|GStreamer RTSP|libgstrtsp-1.0.so|WARNING|gstreamer|${GSTREAMER_LIB_SEARCH_DIRS}"
    "GSTSDP|GStreamer SDP|libgstsdp-1.0.so|WARNING|gstreamer|${GSTREAMER_LIB_SEARCH_DIRS}"
    "GSTALLOCATORS|GStreamer allocators|libgstallocators-1.0.so|WARNING|gstreamer|${GSTREAMER_LIB_SEARCH_DIRS}"
    "GSTGL|GStreamer OpenGL|libgstgl-1.0.so|WARNING|gstreamer|${GSTREAMER_LIB_SEARCH_DIRS}"
    "ORC|ORC optimization|liborc-0.4.so|WARNING|gstreamer|/usr/lib/x86_64-linux-gnu|/usr/lib"
    "V4L|v4l-utils|libv4l*.so|WARNING|gstreamer|/usr/lib/x86_64-linux-gnu|/usr/lib"
    "V4L1|v4l1 compatibility|libv4l1.so|WARNING|gstreamer|/usr/lib/x86_64-linux-gnu|/usr/lib"
    "V4LCONVERT|v4l format conversion|libv4lconvert.so|WARNING|gstreamer|/usr/lib/x86_64-linux-gnu|/usr/lib"
    
    # GStreamer plugins -> ${RPMTOP}/SOURCES/gstreamer/gstreamer-1.0
    # Using common GStreamer plugin search directories
    "GSTPLUGIN_VIDEO4LINUX2|GStreamer V4L2 video capture|libgstvideo4linux2.so|WARNING|gstreamer/gstreamer-1.0|${GSTREAMER_PLUGIN_SEARCH_DIRS}"
    "GSTPLUGIN_V4L2CODECS|GStreamer V4L2 hardware codecs|libgstv4l2codecs.so|WARNING|gstreamer/gstreamer-1.0|${GSTREAMER_PLUGIN_SEARCH_DIRS}"
    "GSTPLUGIN_VIDEOCONVERTSCALE|GStreamer video format conversion|libgstvideoconvertscale.so|WARNING|gstreamer/gstreamer-1.0|${GSTREAMER_PLUGIN_SEARCH_DIRS}"
    "GSTPLUGIN_VIDEORATE|GStreamer video frame rate conversion|libgstvideorate.so|WARNING|gstreamer/gstreamer-1.0|${GSTREAMER_PLUGIN_SEARCH_DIRS}"
    "GSTPLUGIN_COREELEMENTS|GStreamer core elements|libgstcoreelements.so|WARNING|gstreamer/gstreamer-1.0|${GSTREAMER_PLUGIN_SEARCH_DIRS}"
    "GSTPLUGIN_TYPEFIND|GStreamer type detection|libgsttypefindfunctions.so|WARNING|gstreamer/gstreamer-1.0|${GSTREAMER_PLUGIN_SEARCH_DIRS}"
    "GSTPLUGIN_APP|GStreamer application integration|libgstapp.so|WARNING|gstreamer/gstreamer-1.0|${GSTREAMER_PLUGIN_SEARCH_DIRS}"
    "GSTPLUGIN_PLAYBACK|GStreamer playback elements|libgstplayback.so|WARNING|gstreamer/gstreamer-1.0|${GSTREAMER_PLUGIN_SEARCH_DIRS}"
    "GSTPLUGIN_JPEG|GStreamer JPEG codec|libgstjpeg.so|WARNING|gstreamer/gstreamer-1.0|${GSTREAMER_PLUGIN_SEARCH_DIRS}"
    "GSTPLUGIN_XIMAGESINK|GStreamer X11 video sink|libgstximagesink.so|WARNING|gstreamer/gstreamer-1.0|${GSTREAMER_PLUGIN_SEARCH_DIRS}"
    "GSTPLUGIN_XVIMAGESINK|GStreamer XVideo sink|libgstxvimagesink.so|WARNING|gstreamer/gstreamer-1.0|${GSTREAMER_PLUGIN_SEARCH_DIRS}"
    "GSTPLUGIN_AUTODETECT|GStreamer auto detection|libgstautodetect.so|WARNING|gstreamer/gstreamer-1.0|${GSTREAMER_PLUGIN_SEARCH_DIRS}"
    "GSTPLUGIN_PULSEAUDIO|GStreamer PulseAudio|libgstpulseaudio.so|WARNING|gstreamer/gstreamer-1.0|${GSTREAMER_PLUGIN_SEARCH_DIRS}"
    "GSTPLUGIN_AUDIOPARSERS|GStreamer audio parsers|libgstaudioparsers.so|WARNING|gstreamer/gstreamer-1.0|${GSTREAMER_PLUGIN_SEARCH_DIRS}"
    "GSTPLUGIN_AUDIOCONVERT|GStreamer audio conversion|libgstaudioconvert.so|WARNING|gstreamer/gstreamer-1.0|${GSTREAMER_PLUGIN_SEARCH_DIRS}"
    "GSTPLUGIN_AUDIORESAMPLE|GStreamer audio resampling|libgstaudioresample.so|WARNING|gstreamer/gstreamer-1.0|${GSTREAMER_PLUGIN_SEARCH_DIRS}"
    "LIBDW|DW support library|libdw.so|WARNING|gstreamer/gstreamer-1.0|/usr/lib/x86_64-linux-gnu|/usr/lib"
    "XCBCURSOR|XCB cursor support|libxcb-cursor.so|ERROR|qt6|/usr/lib/x86_64-linux-gnu|/usr/lib"
    "XCB|X11 XCB|libxcb.so|ERROR|qt6|/usr/lib/x86_64-linux-gnu|/usr/lib"
    "XCBUTIL|XCB utilities|libxcb-util.so|WARNING|qt6|/usr/lib/x86_64-linux-gnu|/usr/lib"
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

# Copy icon files (both PNG and SVG) if available
# These will be installed to /usr/share/icons/hicolor/ in the spec file
echo "📋 RPM: Copying icon files..."
if [ -f "${SRC}/images/icon_256.png" ]; then
	cp "${SRC}/images/icon_256.png" "${RPMTOP}/SOURCES/icon_256.png"
	echo "   ✅ Icon PNG copied"
else
	echo "   ⚠️  Warning: icon_256.png not found"
fi

if [ -f "${SRC}/images/icon_256.svg" ]; then
	cp "${SRC}/images/icon_256.svg" "${RPMTOP}/SOURCES/icon_256.svg"
	echo "   ✅ Icon SVG copied"
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
