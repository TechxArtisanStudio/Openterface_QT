#!/bin/bash

set -euo pipefail
IFS=$'\n\t'

APPIMAGE_DIR="${BUILD_DIR}/appimage"

# Determine architecture
ARCH=$(uname -m)
case "${ARCH}" in
    aarch64|arm64) APPIMAGE_ARCH=aarch64;;
    x86_64|amd64) APPIMAGE_ARCH=x86_64;;
    *) APPIMAGE_ARCH=${ARCH};;
esac

# Create build directory
mkdir -p "${BUILD_DIR}" "${APPIMAGE_DIR}"

# Essential GStreamer plugins for video capture
GSTREAMER_PLUGINS=(
    "libgstvideo4linux2.so"        # V4L2 video capture (CRITICAL)
    "libgstv4l2codecs.so"          # V4L2 hardware codecs
    "libgstvideoconvert.so"        # Video format conversion
    "libgstvideoscale.so"          # Video scaling
    "libgstvideorate.so"           # Video frame rate conversion
    "libgstcoreelements.so"        # Core elements (queue, filesrc, etc.)
    "libgsttypefindfunctions.so"   # Type detection
    "libgstapp.so"                 # Application integration
    "libgstplayback.so"           # Playback elements
    "libgstjpeg.so"               # JPEG codec
    "libgsth264parse.so"          # H.264 parser
    "libgstximagesink.so"         # X11 video sink
    "libgstxvimagesink.so"        # XVideo sink
    "libgstautodetect.so"         # Auto detection
    "libgstpulse.so"              # PulseAudio
    "libgstalsa.so"               # ALSA audio
    "libgstaudioparsers.so"       # Audio parsers
    "libgstaudioconvert.so"       # Audio conversion
    "libgstaudioresample.so"      # Audio resampling
)

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
    VERSION=$(grep -Po '^#define APP_VERSION\s+"\K[0-9]+(\.[0-9]+)*' "${VERSION_H}" | head -n1)
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

echo '📦 Copying essential GStreamer plugins...'
COPIED_COUNT=0
for plugin in "${GSTREAMER_PLUGINS[@]}"; do
	if [ -f "$GSTREAMER_HOST_DIR/$plugin" ]; then
		echo "✅ Included $plugin"
		cp "$GSTREAMER_HOST_DIR/$plugin" "appimage/AppDir/usr/lib/gstreamer-1.0/"
		chmod +x "appimage/AppDir/usr/lib/gstreamer-1.0/$plugin"
		COPIED_COUNT=$((COPIED_COUNT + 1))
	else
		echo "⚠️ Missing $plugin"
	fi
done
echo "📦 Copied $COPIED_COUNT essential GStreamer plugins"

# Copy dependencies of GStreamer plugins
echo "📦 Copying dependencies for GStreamer plugins..."
mkdir -p "appimage/AppDir/usr/lib"
for plugin in "${GSTREAMER_PLUGINS[@]}"; do
	if [ -f "appimage/AppDir/usr/lib/gstreamer-1.0/$plugin" ]; then
		echo "Checking dependencies for $plugin"
		ldd "appimage/AppDir/usr/lib/gstreamer-1.0/$plugin" 2>/dev/null | grep -v "linux-vdso" | grep -v "ld-linux" | awk '{print $3}' | while read -r dep; do
			if [ -f "$dep" ] && [[ "$dep" == /usr/lib/* || "$dep" == /lib/* ]] && [ ! -f "appimage/AppDir/usr/lib/$(basename "$dep")" ]; then
				echo "  Copying dependency: $(basename "$dep")"
				cp "$dep" "appimage/AppDir/usr/lib/"
			fi
		done
	fi
done
echo "✅ GStreamer plugin dependencies copied"


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
echo "📦 Copied $COPIED_COUNT essential GStreamer plugins"
echo "✅ Proceeding to comprehensive AppImage creation with Docker runtime support"
cd /workspace

# Determine linuxdeploy architecture tag from Debian arch
case "${ARCH}" in
	amd64|x86_64) APPIMAGE_ARCH=x86_64;;
	arm64|aarch64) APPIMAGE_ARCH=aarch64;;
	armhf|armv7l) APPIMAGE_ARCH=armhf;;
	*) echo "Warning: unknown arch '${ARCH}', defaulting to x86_64"; APPIMAGE_ARCH=x86_64;;
esac

# Set up variables for comprehensive AppImage creation
SRC="/workspace/src"
BUILD="/workspace/build" 
APPDIR="${APPIMAGE_DIR}/AppDir"
DESKTOP_OUT="${APPDIR}/openterfaceqt.desktop"
APPIMAGE_OUT="${BUILD}"

# Create comprehensive AppDir structure for the final AppImage
rm -rf "${APPDIR}"
mkdir -p "${APPDIR}/usr/bin" "${APPDIR}/usr/lib" "${APPDIR}/usr/share/applications" "${APPDIR}/usr/share/pixmaps"

# Copy the executable to the comprehensive AppDir
cp "${BUILD}/openterfaceQT" "${APPDIR}/usr/bin/"
chmod +x "${APPDIR}/usr/bin/openterfaceQT"

echo "✅ Setting up comprehensive AppImage structure with Docker runtime support"

# Create desktop file for comprehensive AppImage
cat > "${APPDIR}/usr/share/applications/openterfaceqt.desktop" << 'EOF'
[Desktop Entry]
Type=Application
Name=OpenterfaceQT
Comment=Openterface Mini-KVM Host Application  
Exec=openterfaceQT
Icon=openterfaceqt
Categories=Utility;
StartupNotify=true
Terminal=false
EOF

# Copy desktop file to root of AppDir
cp "${APPDIR}/usr/share/applications/openterfaceqt.desktop" "${DESKTOP_OUT}"

# Copy GStreamer plugins to comprehensive AppImage
echo "Including GStreamer plugins for video capture in comprehensive AppImage..."
mkdir -p "${APPDIR}/usr/lib/gstreamer-1.0"
for plugin in "${GSTREAMER_PLUGINS[@]}"; do
	if [ -f "$GSTREAMER_HOST_DIR/$plugin" ]; then
		cp "$GSTREAMER_HOST_DIR/$plugin" "${APPDIR}/usr/lib/gstreamer-1.0/"
		echo "✓ Copied plugin: ${plugin}"
	fi
done

# Copy AppStream/metainfo (optional)
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
	fi
	# Also copy to pixmaps and root
	mkdir -p "${APPDIR}/usr/share/pixmaps"
	cp "${ICON_SRC}" "${APPDIR}/usr/share/pixmaps/openterfaceqt.${ICON_EXT}" || true
	cp "${ICON_SRC}" "${APPDIR}/openterfaceqt.${ICON_EXT}" || true
else
	echo "No icon found; continuing without a custom icon."
fi

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

# Set LD_LIBRARY_PATH to include ffmpeg and gstreamer libraries for dependency resolution
export LD_LIBRARY_PATH="/opt/ffmpeg/lib:/opt/gstreamer/lib:/opt/gstreamer/lib/${ARCH}-linux-gnu:/opt/Qt6/lib:$LD_LIBRARY_PATH"
export PATH="/opt/Qt6/bin:$PATH"
export QT_PLUGIN_PATH="/opt/Qt6/plugins:$QT_PLUGIN_PATH"
export QML2_IMPORT_PATH="/opt/Qt6/qml:$QML2_IMPORT_PATH"

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
"${LINUXDEPLOY_BIN}" "${LINUXDEPLOY_ARGS_NO_OUTPUT[@]}"

# Create custom AppRun script with proper environment setup after linuxdeploy to avoid override
cat > "${APPDIR}/AppRun" << 'EOF'
#!/bin/bash
# AppRun script for OpenterfaceQT enhanced AppImage with GStreamer plugins

# Set the directory where this script is located
HERE="$(dirname "$(readlink -f "${0}")")"

# Set GStreamer plugin path to our bundled plugins
export GST_PLUGIN_PATH="${HERE}/usr/lib/gstreamer-1.0:${GST_PLUGIN_PATH}"

# Set library path for our bundled libraries
export LD_LIBRARY_PATH="${HERE}/usr/lib:${LD_LIBRARY_PATH}"

# Set Qt plugin path
export QT_PLUGIN_PATH="${HERE}/usr/plugins:${QT_PLUGIN_PATH}"

# Run the application
exec "${HERE}/usr/bin/openterfaceQT" "$@"
EOF

chmod +x "${APPDIR}/AppRun"

# Then use appimagetool directly with explicit runtime file
echo "Running appimagetool with explicit runtime file..."
if command -v appimagetool >/dev/null 2>&1; then
	if [ -f "${RUNTIME_FILE}" ]; then
		appimagetool --runtime-file "${RUNTIME_FILE}" "${APPDIR}"
	else
		echo "Warning: Runtime file not found, running appimagetool without runtime file"
		appimagetool "${APPDIR}"
	fi
else
	echo "appimagetool not found, trying linuxdeploy with output plugin..."
	"${LINUXDEPLOY_BIN}" "${LINUXDEPLOY_ARGS[@]}"
fi

# Normalize output name
APPIMAGE_FILENAME="openterfaceQT_${VERSION}_${APPIMAGE_ARCH}.AppImage"
# Move whichever AppImage got produced
FOUND_APPIMAGE=$(ls -1 *.AppImage 2>/dev/null | grep -v -E '^linuxdeploy|^linuxdeploy-plugin-qt' | head -n1 || true)
if [ -n "${FOUND_APPIMAGE}" ]; then
	mv "${FOUND_APPIMAGE}" "${APPIMAGE_OUT}/${APPIMAGE_FILENAME}"
	echo "AppImage created at ${APPIMAGE_OUT}/${APPIMAGE_FILENAME}"
else
	echo "Error: AppImage build did not produce an output." >&2
	exit 1
fi

popd >/dev/null