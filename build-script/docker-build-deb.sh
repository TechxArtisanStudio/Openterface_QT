#!/bin/bash

set -e

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
VERSION="${VERSION:-0.0.1}"
ARCH="${ARCH:-amd64}"

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
# Use -o to match both regular files and symlinks in a single pass
# This ensures all variants (libQt6Core.so, libQt6Core.so.6, libQt6Core.so.6.6.3, etc.) are copied
find "${QT_LIB_DIR}" -maxdepth 1 \( -name "libQt6*.so*" -type f -o -name "libQt6*.so*" -type l \) 2>/dev/null | while read -r libfile; do
    cp -Pa "$libfile" "${PKG_ROOT}/usr/lib/openterfaceqt/qt6/"
    basename "$libfile"
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
    
    echo "📋 DEB: Searching for ${display_name} libraries..."
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
mkdir -p "${PKG_ROOT}/usr/lib/openterfaceqt/ffmpeg"
mkdir -p "${PKG_ROOT}/usr/lib/openterfaceqt/gstreamer"
mkdir -p "${PKG_ROOT}/usr/lib/openterfaceqt/gstreamer/gstreamer-1.0"
mkdir -p "${PKG_ROOT}/usr/lib/openterfaceqt/qt6"
mkdir -p "${PKG_ROOT}/usr/lib/openterfaceqt/qt6/plugins"
mkdir -p "${PKG_ROOT}/usr/lib/openterfaceqt/qt6/plugins/imageformats"
mkdir -p "${PKG_ROOT}/usr/lib/openterfaceqt/qt6/plugins/iconengines"
mkdir -p "${PKG_ROOT}/usr/lib/openterfaceqt/qt6/plugins/platforms"
mkdir -p "${PKG_ROOT}/usr/lib/openterfaceqt/qt6/plugins/platforminputcontexts"

# Common search directories for FFmpeg libraries
FFMPEG_LIB_SEARCH_DIRS="/opt/ffmpeg/lib /usr/lib/x86_64-linux-gnu /usr/lib"
    
# Common search directories for GStreamer libraries
GSTREAMER_LIB_SEARCH_DIRS="/opt/gstreamer/lib/x86_64-linux-gnu /usr/lib/x86_64-linux-gnu /usr/lib"

# Common search directories for GStreamer plugins
GSTREAMER_PLUGIN_SEARCH_DIRS="/usr/lib/x86_64-linux-gnu/gstreamer-1.0 /usr/lib/gstreamer-1.0 /opt/gstreamer/lib/x86_64-linux-gnu/gstreamer-1.0"

# Unified library configurations with target subdirectories
declare -a UNIFIED_LIBRARY_CONFIGS=(
    # Core media libraries
    "LIBBZ2|bzip2|libbz2.so|ERROR||/opt/ffmpeg/lib /usr/lib/x86_64-linux-gnu /usr/lib"
    "LIBUSB|libusb|libusb*.so|ERROR||/opt/libusb/lib /opt/ffmpeg/lib /usr/lib/x86_64-linux-gnu /usr/lib"
    "JPEG|libjpeg|libjpeg.so|ERROR||/opt/ffmpeg/lib /usr/lib/x86_64-linux-gnu /usr/lib"
    "TURBOJPEG|libturbojpeg|libturbojpeg.so|ERROR||/opt/ffmpeg/lib /usr/lib/x86_64-linux-gnu /usr/lib"
    "VA|VA-API|libva.so|WARNING||/usr/lib/x86_64-linux-gnu /usr/lib"
    "VADRM|VA-API DRM|libva-drm.so|WARNING||/usr/lib/x86_64-linux-gnu /usr/lib"
    "VAX11|VA-API X11|libva-x11.so|WARNING||/usr/lib/x86_64-linux-gnu /usr/lib"
    "VDPAU|VDPAU|libvdpau.so|WARNING||/usr/lib/x86_64-linux-gnu /usr/lib"
    
    # GPU/Rendering libraries (CRITICAL for display)
    "EGL|libEGL|libEGL.so|WARNING||/lib/x86_64-linux-gnu /opt/Qt6/lib /usr/lib/x86_64-linux-gnu /usr/lib /usr/lib64 /lib"
    "GL|libGL|libGL.so|WARNING||/lib/x86_64-linux-gnu /opt/Qt6/lib /usr/lib/x86_64-linux-gnu /usr/lib /usr/lib64 /lib"
    "GLX|libGLX|libGLX.so|WARNING||/lib/x86_64-linux-gnu /opt/Qt6/lib /usr/lib/x86_64-linux-gnu /usr/lib /usr/lib64 /lib"
    "GLESV2|libGLESv2|libGLESv2.so|WARNING||/lib/x86_64-linux-gnu /opt/Qt6/lib /usr/lib/x86_64-linux-gnu /usr/lib /usr/lib64 /lib"
    "GLVND|libglvnd|libglvnd.so|WARNING||/lib/x86_64-linux-gnu /opt/Qt6/lib /usr/lib/x86_64-linux-gnu /usr/lib /usr/lib64 /lib"
    "GLDISPATCH|libGLdispatch|libGLdispatch.so|WARNING||/lib/x86_64-linux-gnu /opt/Qt6/lib /usr/lib/x86_64-linux-gnu /usr/lib /usr/lib64 /lib"
    "OPENGL|libOpenGL|libOpenGL.so|WARNING||/lib/x86_64-linux-gnu /opt/Qt6/lib /usr/lib/x86_64-linux-gnu /usr/lib /usr/lib64 /lib"
    
    # FFmpeg libraries -> ${PKG_ROOT}/usr/lib/openterfaceqt/ffmpeg
    "AVDEVICE|FFmpeg avdevice|libavdevice.so|WARNING|ffmpeg|/opt/ffmpeg/lib /usr/lib/x86_64-linux-gnu /usr/lib"
    "AVCODEC|FFmpeg avcodec|libavcodec.so|WARNING|ffmpeg|/opt/ffmpeg/lib /usr/lib/x86_64-linux-gnu /usr/lib"
    "AVFORMAT|FFmpeg avformat|libavformat.so|WARNING|ffmpeg|/opt/ffmpeg/lib /usr/lib/x86_64-linux-gnu /usr/lib"
    "AVUTIL|FFmpeg avutil|libavutil.so|WARNING|ffmpeg|/opt/ffmpeg/lib /usr/lib/x86_64-linux-gnu /usr/lib"
    "SWSCALE|FFmpeg swscale|libswscale.so|WARNING|ffmpeg|/opt/ffmpeg/lib /usr/lib/x86_64-linux-gnu /usr/lib"
    "SWRESAMPLE|FFmpeg swresample|libswresample.so|WARNING|ffmpeg|/opt/ffmpeg/lib /usr/lib/x86_64-linux-gnu /usr/lib"
    "AVFILTER|FFmpeg avfilter|libavfilter.so|WARNING|ffmpeg|/opt/ffmpeg/lib /usr/lib/x86_64-linux-gnu /usr/lib"
    
    # GStreamer libraries -> ${PKG_ROOT}/usr/lib/openterfaceqt/gstreamer
    "GSTREAMER|GStreamer core|libgstreamer-1.0.so|WARNING|gstreamer|/opt/gstreamer/lib/x86_64-linux-gnu /usr/lib/x86_64-linux-gnu /usr/lib"
    "GSTBASE|GStreamer base|libgstbase-1.0.so|WARNING|gstreamer|/opt/gstreamer/lib/x86_64-linux-gnu /usr/lib/x86_64-linux-gnu /usr/lib"
    "GSTAUDIO|GStreamer audio|libgstaudio-1.0.so|WARNING|gstreamer|/opt/gstreamer/lib/x86_64-linux-gnu /usr/lib/x86_64-linux-gnu /usr/lib"
    "GSTPBUTILS|GStreamer playback utils|libgstpbutils-1.0.so|WARNING|gstreamer|/opt/gstreamer/lib/x86_64-linux-gnu /usr/lib/x86_64-linux-gnu /usr/lib"
    "GSTVIDEO|GStreamer video|libgstvideo-1.0.so|WARNING|gstreamer|/opt/gstreamer/lib/x86_64-linux-gnu /usr/lib/x86_64-linux-gnu /usr/lib"
    "GSTAPP|GStreamer app|libgstapp-1.0.so|WARNING|gstreamer|/opt/gstreamer/lib/x86_64-linux-gnu /usr/lib/x86_64-linux-gnu /usr/lib"
    "GSTTAG|GStreamer tag|libgsttag-1.0.so|WARNING|gstreamer|/opt/gstreamer/lib/x86_64-linux-gnu /usr/lib/x86_64-linux-gnu /usr/lib"
    "GSTRTP|GStreamer RTP|libgstrtp-1.0.so|WARNING|gstreamer|/opt/gstreamer/lib/x86_64-linux-gnu /usr/lib/x86_64-linux-gnu /usr/lib"
    "GSTRTSP|GStreamer RTSP|libgstrtsp-1.0.so|WARNING|gstreamer|/opt/gstreamer/lib/x86_64-linux-gnu /usr/lib/x86_64-linux-gnu /usr/lib"
    "GSTSDP|GStreamer SDP|libgstsdp-1.0.so|WARNING|gstreamer|/opt/gstreamer/lib/x86_64-linux-gnu /usr/lib/x86_64-linux-gnu /usr/lib"
    "GSTALLOCATORS|GStreamer allocators|libgstallocators-1.0.so|WARNING|gstreamer|/opt/gstreamer/lib/x86_64-linux-gnu /usr/lib/x86_64-linux-gnu /usr/lib"
    "GSTGL|GStreamer OpenGL|libgstgl-1.0.so|WARNING|gstreamer|/opt/gstreamer/lib/x86_64-linux-gnu /usr/lib/x86_64-linux-gnu /usr/lib"
    "ORC|ORC optimization|liborc-0.4.so|WARNING|gstreamer|/usr/lib/x86_64-linux-gnu /usr/lib"
    "V4L|v4l-utils|libv4l*.so|WARNING|gstreamer|/usr/lib/x86_64-linux-gnu /usr/lib"
    "V4L1|v4l1 compatibility|libv4l1.so|WARNING|gstreamer|/usr/lib/x86_64-linux-gnu /usr/lib"
    "V4LCONVERT|v4l format conversion|libv4lconvert.so|WARNING|gstreamer|/usr/lib/x86_64-linux-gnu /usr/lib"
    
    # Qt6 libraries -> /usr/lib/openterfaceqt/qt6
    # Using common Qt6 library search directories
    "QTCORE|Qt6 core|libQt6Core.so|ERROR|qt6|/opt/Qt6/lib /usr/lib/x86_64-linux-gnu /usr/lib"
    "QTGUI|Qt6 gui|libQt6Gui.so|ERROR|qt6|/opt/Qt6/lib /usr/lib/x86_64-linux-gnu /usr/lib"
    "QTWIDGETS|Qt6 widgets|libQt6Widgets.so|ERROR|qt6|/opt/Qt6/lib /usr/lib/x86_64-linux-gnu /usr/lib"
    "QTSERIALPORT|Qt6 serial port|libQt6SerialPort.so|WARNING|qt6|/opt/Qt6/lib /usr/lib/x86_64-linux-gnu /usr/lib"
    "QTSHADERTOOLS|Qt6 shader tools|libQt6ShaderTools.so|WARNING|qt6|/opt/Qt6/lib /usr/lib/x86_64-linux-gnu /usr/lib"
    "QTSVGWIDGETS|Qt6 SVG widgets|libQt6SvgWidgets.so|WARNING|qt6|/opt/Qt6/lib /usr/lib/x86_64-linux-gnu /usr/lib"
    "QTUITOOLS|Qt6 UI tools|libQt6UiTools.so|WARNING|qt6|/opt/Qt6/lib /usr/lib/x86_64-linux-gnu /usr/lib"
    "QTXML|Qt6 XML|libQt6Xml.so|WARNING|qt6|/opt/Qt6/lib /usr/lib/x86_64-linux-gnu /usr/lib"
    "QTTEST|Qt6 test|libQt6Test.so|WARNING|qt6|/opt/Qt6/lib /usr/lib/x86_64-linux-gnu /usr/lib"
    "QTQML|Qt6 qml|libQt6Qml.so|ERROR|qt6|/opt/Qt6/lib /usr/lib/x86_64-linux-gnu /usr/lib"
    "QTQMLCOMPILER|Qt6 QML compiler|libQt6QmlCompiler.so|WARNING|qt6|/opt/Qt6/lib /usr/lib/x86_64-linux-gnu /usr/lib"
    "QTQMLCORE|Qt6 QML core|libQt6QmlCore.so|WARNING|qt6|/opt/Qt6/lib /usr/lib/x86_64-linux-gnu /usr/lib"
    "QTQMLXMLLISTMODEL|Qt6 QML XML list model|libQt6QmlXmlListModel.so|WARNING|qt6|/opt/Qt6/lib /usr/lib/x86_64-linux-gnu /usr/lib"
    "QTQMLMODELS|Qt6 QML models|libQt6QmlModels.so|WARNING|qt6|/opt/Qt6/lib /usr/lib/x86_64-linux-gnu /usr/lib"
    "QTQMLWORKERSCRIPT|Qt6 QML worker script|libQt6QmlWorkerScript.so|WARNING|qt6|/opt/Qt6/lib /usr/lib/x86_64-linux-gnu /usr/lib"
    "QTQUICK|Qt6 quick|libQt6Quick.so|WARNING|qt6|/opt/Qt6/lib /usr/lib/x86_64-linux-gnu /usr/lib"
    "QTWAYLANDCLIENT|Qt6 Wayland Client|libQt6WaylandClient.so|WARNING|qt6|/opt/Qt6/lib /usr/lib/x86_64-linux-gnu /usr/lib"
    "QTXCBQPA|Qt6 XCB QPA|libQt6XcbQpa.so|ERROR|qt6|/opt/Qt6/lib /usr/lib/x86_64-linux-gnu /usr/lib"
    "QTCORE5COMPAT|Qt6 core5 compat|libQt6Core5Compat.so|WARNING|qt6|/opt/Qt6/lib /usr/lib/x86_64-linux-gnu /usr/lib"
    "QTGUIPRIVATE|Qt6 gui private|libQt6GuiPrivate.so|WARNING|qt6|/opt/Qt6/lib /usr/lib/x86_64-linux-gnu /usr/lib"
    "QTDBUS|Qt6 D-Bus|libQt6DBus.so|WARNING|qt6|/opt/Qt6/lib /usr/lib/x86_64-linux-gnu /usr/lib"
    "QTMULTIMEDIA|Qt6 multimedia|libQt6Multimedia.so|WARNING|qt6|/opt/Qt6/lib /usr/lib/x86_64-linux-gnu /usr/lib"
    "QTMULTIMEDIAQUICK|Qt6 multimedia quick|libQt6MultimediaQuick.so|WARNING|qt6|/opt/Qt6/lib /usr/lib/x86_64-linux-gnu /usr/lib"
    "QTMULTIMEDIAWIDGETS|Qt6 multimedia widgets|libQt6MultimediaWidgets.so|WARNING|qt6|/opt/Qt6/lib /usr/lib/x86_64-linux-gnu /usr/lib"
    "QTNETWORK|Qt6 network|libQt6Network.so|WARNING|qt6|/opt/Qt6/lib /usr/lib/x86_64-linux-gnu /usr/lib"
    "QTOPENGL|Qt6 OpenGL|libQt6OpenGL.so|WARNING|qt6|/opt/Qt6/lib /usr/lib/x86_64-linux-gnu /usr/lib"
    "QTQUICKCONTROLS2|Qt6 quick controls 2|libQt6QuickControls2.so|WARNING|qt6|/opt/Qt6/lib /usr/lib/x86_64-linux-gnu /usr/lib"
    "QTQUICKSHAPES|Qt6 quick shapes|libQt6QuickShapes.so|WARNING|qt6|/opt/Qt6/lib /usr/lib/x86_64-linux-gnu /usr/lib"
    "QTQUICKLAYOUTS|Qt6 quick layouts|libQt6QuickLayouts.so|WARNING|qt6|/opt/Qt6/lib /usr/lib/x86_64-linux-gnu /usr/lib"
    "QTQUICKTEMPLATES2|Qt6 quick templates 2|libQt6QuickTemplates2.so|WARNING|qt6|/opt/Qt6/lib /usr/lib/x86_64-linux-gnu /usr/lib"
    "QTQUICKPARTICLES|Qt6 quick particles|libQt6QuickParticles.so|WARNING|qt6|/opt/Qt6/lib /usr/lib/x86_64-linux-gnu /usr/lib"
    "QTOPENGLWIDGETS|Qt6 opengl widgets|libQt6OpenGLWidgets.so|WARNING|qt6|/opt/Qt6/lib /usr/lib/x86_64-linux-gnu /usr/lib"
    "QTSVG|Qt6 svg|libQt6Svg.so|WARNING|qt6|/opt/Qt6/lib /usr/lib/x86_64-linux-gnu /usr/lib"
    
    # Qt6 plugins -> /usr/lib/openterfaceqt/qt6/plugins/imageformats and iconengines
    "QTPLUGIN_SVG|Qt6 SVG image format plugin|libqsvg.so|WARNING|qt6/plugins/imageformats|/opt/Qt6/plugins/imageformats /usr/lib/qt6/plugins/imageformats /usr/lib/x86_64-linux-gnu/qt6/plugins/imageformats"
    "QTPLUGIN_SVGICON|Qt6 SVG icon engine plugin|libqsvgicon.so|WARNING|qt6/plugins/iconengines|/opt/Qt6/plugins/iconengines /usr/lib/qt6/plugins/iconengines /usr/lib/x86_64-linux-gnu/qt6/plugins/iconengines"
    
    # Qt6 platform plugins -> /usr/lib/openterfaceqt/qt6/plugins/platforms
    "QTPLUGIN_QXC|Qt6 XCB platform|libqxcb.so|ERROR|qt6/plugins/platforms|/opt/Qt6/plugins/platforms /usr/lib/qt6/plugins/platforms /usr/lib/x86_64-linux-gnu/qt6/plugins/platforms"
    "QTPLUGIN_WAYLAND_EGL|Qt6 Wayland EGL platform|libqwayland-egl.so|WARNING|qt6/plugins/platforms|/opt/Qt6/plugins/platforms /usr/lib/qt6/plugins/platforms /usr/lib/x86_64-linux-gnu/qt6/plugins/platforms"
    "QTPLUGIN_WAYLAND_GENERIC|Qt6 Wayland generic platform|libqwayland-generic.so|WARNING|qt6/plugins/platforms|/opt/Qt6/plugins/platforms /usr/lib/qt6/plugins/platforms /usr/lib/x86_64-linux-gnu/qt6/plugins/platforms"
    "QTPLUGIN_EGLFS|Qt6 EGLFS platform|libqeglfs.so|WARNING|qt6/plugins/platforms|/opt/Qt6/plugins/platforms /usr/lib/qt6/plugins/platforms /usr/lib/x86_64-linux-gnu/qt6/plugins/platforms"
    "QTPLUGIN_MINIMAL|Qt6 minimal platform|libqminimal.so|WARNING|qt6/plugins/platforms|/opt/Qt6/plugins/platforms /usr/lib/qt6/plugins/platforms /usr/lib/x86_64-linux-gnu/qt6/plugins/platforms"
    "QTPLUGIN_MINIMALEGL|Qt6 minimal EGL platform|libqminimalegl.so|WARNING|qt6/plugins/platforms|/opt/Qt6/plugins/platforms /usr/lib/qt6/plugins/platforms /usr/lib/x86_64-linux-gnu/qt6/plugins/platforms"
    "QTPLUGIN_LINUXFB|Qt6 Linux framebuffer platform|libqlinuxfb.so|WARNING|qt6/plugins/platforms|/opt/Qt6/plugins/platforms /usr/lib/qt6/plugins/platforms /usr/lib/x86_64-linux-gnu/qt6/plugins/platforms"
    "QTPLUGIN_OFFSCREEN|Qt6 offscreen platform|libqoffscreen.so|WARNING|qt6/plugins/platforms|/opt/Qt6/plugins/platforms /usr/lib/qt6/plugins/platforms /usr/lib/x86_64-linux-gnu/qt6/plugins/platforms"
    "QTPLUGIN_VNC|Qt6 VNC platform|libqvnc.so|WARNING|qt6/plugins/platforms|/opt/Qt6/plugins/platforms /usr/lib/qt6/plugins/platforms /usr/lib/x86_64-linux-gnu/qt6/plugins/platforms"
    "QTPLUGIN_VKKHRDISPLAY|Qt6 VK KHR display platform|libqvkkhrdisplay.so|WARNING|qt6/plugins/platforms|/opt/Qt6/plugins/platforms /usr/lib/qt6/plugins/platforms /usr/lib/x86_64-linux-gnu/qt6/plugins/platforms"
    
    # Qt6 platform input context plugins -> /usr/lib/openterfaceqt/qt6/plugins/platforminputcontexts
    "QTPLUGIN_COMPOSE|Qt6 compose platform input context|libcomposeplatforminputcontextplugin.so|WARNING|qt6/plugins/platforminputcontexts|/opt/Qt6/plugins/platforminputcontexts /usr/lib/qt6/plugins/platforminputcontexts /usr/lib/x86_64-linux-gnu/qt6/plugins/platforminputcontexts"
    "QTPLUGIN_IBUS|Qt6 IBus platform input context|libibusplatforminputcontextplugin.so|WARNING|qt6/plugins/platforminputcontexts|/opt/Qt6/plugins/platforminputcontexts /usr/lib/qt6/plugins/platforminputcontexts /usr/lib/x86_64-linux-gnu/qt6/plugins/platforminputcontexts"
    
    # GStreamer plugins -> ${PKG_ROOT}/usr/lib/openterfaceqt/gstreamer/gstreamer-1.0
    "GSTPLUGIN_VIDEO4LINUX2|GStreamer V4L2 video capture|libgstvideo4linux2.so|WARNING|gstreamer/gstreamer-1.0|/usr/lib/x86_64-linux-gnu/gstreamer-1.0 /usr/lib/gstreamer-1.0 /opt/gstreamer/lib/x86_64-linux-gnu/gstreamer-1.0"
    "GSTPLUGIN_V4L2CODECS|GStreamer V4L2 hardware codecs|libgstv4l2codecs.so|WARNING|gstreamer/gstreamer-1.0|/usr/lib/x86_64-linux-gnu/gstreamer-1.0 /usr/lib/gstreamer-1.0 /opt/gstreamer/lib/x86_64-linux-gnu/gstreamer-1.0"
    "GSTPLUGIN_VIDEOCONVERTSCALE|GStreamer video format conversion|libgstvideoconvertscale.so|WARNING|gstreamer/gstreamer-1.0|/usr/lib/x86_64-linux-gnu/gstreamer-1.0 /usr/lib/gstreamer-1.0 /opt/gstreamer/lib/x86_64-linux-gnu/gstreamer-1.0"
    "GSTPLUGIN_VIDEORATE|GStreamer video frame rate conversion|libgstvideorate.so|WARNING|gstreamer/gstreamer-1.0|/usr/lib/x86_64-linux-gnu/gstreamer-1.0 /usr/lib/gstreamer-1.0 /opt/gstreamer/lib/x86_64-linux-gnu/gstreamer-1.0"
    "GSTPLUGIN_COREELEMENTS|GStreamer core elements|libgstcoreelements.so|WARNING|gstreamer/gstreamer-1.0|/usr/lib/x86_64-linux-gnu/gstreamer-1.0 /usr/lib/gstreamer-1.0 /opt/gstreamer/lib/x86_64-linux-gnu/gstreamer-1.0"
    "GSTPLUGIN_TYPEFIND|GStreamer type detection|libgsttypefindfunctions.so|WARNING|gstreamer/gstreamer-1.0|/usr/lib/x86_64-linux-gnu/gstreamer-1.0 /usr/lib/gstreamer-1.0 /opt/gstreamer/lib/x86_64-linux-gnu/gstreamer-1.0"
    "GSTPLUGIN_APP|GStreamer application integration|libgstapp.so|WARNING|gstreamer/gstreamer-1.0|/usr/lib/x86_64-linux-gnu/gstreamer-1.0 /usr/lib/gstreamer-1.0 /opt/gstreamer/lib/x86_64-linux-gnu/gstreamer-1.0"
    "GSTPLUGIN_PLAYBACK|GStreamer playback elements|libgstplayback.so|WARNING|gstreamer/gstreamer-1.0|/usr/lib/x86_64-linux-gnu/gstreamer-1.0 /usr/lib/gstreamer-1.0 /opt/gstreamer/lib/x86_64-linux-gnu/gstreamer-1.0"
    "GSTPLUGIN_JPEG|GStreamer JPEG codec|libgstjpeg.so|WARNING|gstreamer/gstreamer-1.0|/usr/lib/x86_64-linux-gnu/gstreamer-1.0 /usr/lib/gstreamer-1.0 /opt/gstreamer/lib/x86_64-linux-gnu/gstreamer-1.0"
    "GSTPLUGIN_XIMAGESINK|GStreamer X11 video sink|libgstximagesink.so|WARNING|gstreamer/gstreamer-1.0|/usr/lib/x86_64-linux-gnu/gstreamer-1.0 /usr/lib/gstreamer-1.0 /opt/gstreamer/lib/x86_64-linux-gnu/gstreamer-1.0"
    "GSTPLUGIN_XVIMAGESINK|GStreamer XVideo sink|libgstxvimagesink.so|WARNING|gstreamer/gstreamer-1.0|/usr/lib/x86_64-linux-gnu/gstreamer-1.0 /usr/lib/gstreamer-1.0 /opt/gstreamer/lib/x86_64-linux-gnu/gstreamer-1.0"
    "GSTPLUGIN_AUTODETECT|GStreamer auto detection|libgstautodetect.so|WARNING|gstreamer/gstreamer-1.0|/usr/lib/x86_64-linux-gnu/gstreamer-1.0 /usr/lib/gstreamer-1.0 /opt/gstreamer/lib/x86_64-linux-gnu/gstreamer-1.0"
    "GSTPLUGIN_PULSEAUDIO|GStreamer PulseAudio|libgstpulseaudio.so|WARNING|gstreamer/gstreamer-1.0|/usr/lib/x86_64-linux-gnu/gstreamer-1.0 /usr/lib/gstreamer-1.0 /opt/gstreamer/lib/x86_64-linux-gnu/gstreamer-1.0"
    "GSTPLUGIN_AUDIOPARSERS|GStreamer audio parsers|libgstaudioparsers.so|WARNING|gstreamer/gstreamer-1.0|/usr/lib/x86_64-linux-gnu/gstreamer-1.0 /usr/lib/gstreamer-1.0 /opt/gstreamer/lib/x86_64-linux-gnu/gstreamer-1.0"
    "GSTPLUGIN_AUDIOCONVERT|GStreamer audio conversion|libgstaudioconvert.so|WARNING|gstreamer/gstreamer-1.0|/usr/lib/x86_64-linux-gnu/gstreamer-1.0 /usr/lib/gstreamer-1.0 /opt/gstreamer/lib/x86_64-linux-gnu/gstreamer-1.0"
    "GSTPLUGIN_AUDIORESAMPLE|GStreamer audio resampling|libgstaudioresample.so|WARNING|gstreamer/gstreamer-1.0|/usr/lib/x86_64-linux-gnu/gstreamer-1.0 /usr/lib/gstreamer-1.0 /opt/gstreamer/lib/x86_64-linux-gnu/gstreamer-1.0"
    "LIBDW|DW support library|libdw.so|WARNING|gstreamer/gstreamer-1.0|/usr/lib/x86_64-linux-gnu /usr/lib"
    
    # X11/XCB support libraries (CRITICAL for libqxcb.so platform plugin)
    "XCBCURSOR|XCB cursor support|libxcb-cursor.so|ERROR|qt6|/usr/lib/x86_64-linux-gnu /usr/lib"
    "XCB|X11 XCB|libxcb.so|ERROR|qt6|/usr/lib/x86_64-linux-gnu /usr/lib"
    "XCBUTIL|XCB utilities|libxcb-util.so|WARNING|qt6|/usr/lib/x86_64-linux-gnu /usr/lib"
)

# Process all library configurations
echo "🔍 Copying required libraries to DEB package root..."
for config in "${UNIFIED_LIBRARY_CONFIGS[@]}"; do
    IFS='|' read -r var_name display_name lib_pattern severity target_subdir search_dirs_str <<< "$config"
    
    # Determine full target directory
    if [ -z "$target_subdir" ]; then
        target_dir="${PKG_ROOT}/usr/lib/openterfaceqt"
    else
        target_dir="${PKG_ROOT}/usr/lib/openterfaceqt/${target_subdir}"
    fi
    
    # Split search directories
    read -ra search_dirs <<< "$search_dirs_str"
    
    copy_libraries "$var_name" "$display_name" "$lib_pattern" "$severity" "$target_dir" "${search_dirs[@]}"
done

echo "📋 DEB: Final library contents in ${PKG_ROOT}/usr/lib/openterfaceqt:"
find "${PKG_ROOT}/usr/lib/openterfaceqt" -type f -name "*.so*" | sort | head -30
echo "   Total .so files in ${PKG_ROOT}/usr/lib/openterfaceqt:"
find "${PKG_ROOT}/usr/lib/openterfaceqt" -type f -name "*.so*" | wc -l

# Update RPATH for libqxcb.so plugin to find libQt6XcbQpa.so
if [ -f "${PKG_ROOT}/usr/lib/openterfaceqt/qt6/plugins/platforms/libqxcb.so" ]; then
    echo "🔧 DEB: Updating RPATH for libqxcb.so platform plugin..."
    if patchelf --set-rpath '/usr/lib/openterfaceqt/qt6:$ORIGIN/../..:$ORIGIN/../../..' "${PKG_ROOT}/usr/lib/openterfaceqt/qt6/plugins/platforms/libqxcb.so" 2>/dev/null; then
        echo "   ✅ Updated RPATH for libqxcb.so plugin"
    else
        echo "   ⚠️  Could not update plugin RPATH (may not be critical)"
    fi
else
    echo "   ⚠️  Warning: libqxcb.so platform plugin not found"
fi

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

# Copy Qt QML imports
QT_QML_DIR="/opt/Qt6/qml"
if [ -d "${QT_QML_DIR}" ]; then
    mkdir -p "${PKG_ROOT}/usr/lib/openterfaceqt/qt6/qml"
    echo "Copying Qt QML imports..."
    cp -ra "${QT_QML_DIR}"/* "${PKG_ROOT}/usr/lib/openterfaceqt/qt6/qml/" 2>/dev/null || true
fi

# Copy desktop file (ensure Exec uses wrapper script for proper environment setup)

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
if [ -f "${SRC}/packaging/com.openterface.openterfaceQT.desktop" ]; then
	sed -e 's|^Exec=.*$|Exec=/usr/local/bin/openterfaceQT-launcher.sh|g' \
		-e 's|^Icon=.*$|Icon=openterfaceQT|g' \
		"${SRC}/packaging/com.openterface.openterfaceQT.desktop" > "${PKG_ROOT}/usr/share/applications/com.openterface.openterfaceQT.desktop"
fi

# Copy wrapper script to bin
if [ -f "${SRC}/packaging/debian/openterfaceQT-launcher.sh" ]; then
	install -m 0755 "${SRC}/packaging/debian/openterfaceQT-launcher.sh" "${PKG_ROOT}/usr/local/bin/openterfaceQT-launcher.sh"
	echo "✅ Launcher script installed"
	
	# Create a symlink/alias at the standard binary location that points to the wrapper
	# This ensures EVERY call to openterfaceQT goes through the wrapper with LD_PRELOAD
	ln -sf openterfaceQT-launcher.sh "${PKG_ROOT}/usr/local/bin/openterfaceQT"
	echo "✅ Created symlink: /usr/local/bin/openterfaceQT → openterfaceQT-launcher.sh"
else
	echo "⚠️  Warning: wrapper script not found, using inline environment variables as fallback"
	sed -e 's|^Exec=.*$|Exec=env QT_PLUGIN_PATH=/usr/lib/qt6/plugins:/usr/lib/x86_64-linux-gnu/qt6/plugins QML2_IMPORT_PATH=/usr/lib/qt6/qml:/usr/lib/x86_64-linux-gnu/qt6/qml GST_PLUGIN_PATH=/usr/lib/x86_64-linux-gnu/gstreamer-1.0:/usr/lib/gstreamer-1.0 /usr/local/bin/openterfaceQT|g' \
		"${SRC}/packaging/com.openterface.openterfaceQT.desktop" > "${PKG_ROOT}/usr/share/applications/com.openterface.openterfaceQT.desktop"
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
