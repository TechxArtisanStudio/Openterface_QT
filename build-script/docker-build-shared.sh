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

# =========================
# Bundle Qt 6.6 libraries and plugins
# =========================
echo "Bundling Qt 6.6 libraries and plugins..."

# Create application-specific lib and plugins directories
APPDIR_LIB="${PKG_ROOT}/usr/local/openterface/lib"
APPDIR_PLUGINS="${PKG_ROOT}/usr/local/openterface/plugins"

mkdir -p "${APPDIR_LIB}"
mkdir -p "${APPDIR_PLUGINS}"

# Find Qt6 library directory
QT_LIB_DIR="/opt/Qt6/lib"
if [ ! -d "${QT_LIB_DIR}" ]; then
    # Fallback to system Qt6 libraries if custom build not found
    QT_LIB_DIR="/usr/lib/x86_64-linux-gnu"
fi
if [ ! -d "${QT_LIB_DIR}" ]; then
    # Try alternative location
    QT_LIB_DIR="/usr/lib"
fi

# Copy Qt6 Core, Gui, Widgets, and related libraries to appdir/lib
if [ -d "${QT_LIB_DIR}" ]; then
    echo "Copying Qt6 libraries from ${QT_LIB_DIR}..."
    
    # Copy all Qt6 .so libraries
    find "${QT_LIB_DIR}" -maxdepth 1 -name "libQt6*.so*" -type f -exec cp -a {} "${APPDIR_LIB}/" \; 2>/dev/null || true
    find "${QT_LIB_DIR}" -maxdepth 1 -name "libQt6*.so*" -type l -exec cp -a {} "${APPDIR_LIB}/" \; 2>/dev/null || true
    
    if ls "${APPDIR_LIB}"/libQt6*.so* >/dev/null 2>&1; then
        echo "✅ Qt6 libraries copied successfully to ${APPDIR_LIB}"
        ls -lh "${APPDIR_LIB}"/libQt6*.so* | head -10
    else
        echo "⚠️  Warning: No Qt6 libraries found"
    fi
else
    echo "⚠️  Warning: Qt library directory not found"
fi

# Find and copy Qt6 plugins
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
    echo "Copying Qt6 plugins from ${QT_PLUGIN_DIR}..."
    # Copy all plugin subdirectories (platforms, iconengines, imageformats, etc.)
    cp -ra "${QT_PLUGIN_DIR}"/* "${APPDIR_PLUGINS}/" 2>/dev/null || true
    
    if [ -d "${APPDIR_PLUGINS}/platforms" ]; then
        echo "✅ Qt6 plugins copied successfully to ${APPDIR_PLUGINS}"
        find "${APPDIR_PLUGINS}" -maxdepth 2 -type d | head -10
    else
        echo "⚠️  Warning: Qt plugins directory structure not created"
    fi
else
    echo "⚠️  Warning: Qt plugin directory not found at ${QT_PLUGIN_DIR}"
fi

# Copy Qt QML imports if present
QT_QML_DIR="/opt/Qt6/qml"
APPDIR_QML="${PKG_ROOT}/usr/local/openterface/qml"
if [ -d "${QT_QML_DIR}" ]; then
    mkdir -p "${APPDIR_QML}"
    echo "Copying Qt QML imports..."
    cp -ra "${QT_QML_DIR}"/* "${APPDIR_QML}/" 2>/dev/null || true
    echo "✅ Qt QML imports copied"
fi

# =========================
# Copy FFmpeg dependencies
# =========================
echo "Copying FFmpeg dependencies..."

# FFmpeg libraries
FFMPEG_LIB_DIRS="/opt/ffmpeg/lib /usr/lib/x86_64-linux-gnu /usr/lib /usr/lib/aarch64-linux-gnu"
FFMPEG_LIBS="libavcodec.so libavformat.so libavutil.so libswscale.so libswresample.so libavdevice.so libavfilter.so"

for LIB_DIR in ${FFMPEG_LIB_DIRS}; do
    if [ -d "${LIB_DIR}" ]; then
        for LIB in ${FFMPEG_LIBS}; do
            # Find the library with any version suffix (e.g., libavcodec.so.60)
            if find "${LIB_DIR}" -maxdepth 1 -name "${LIB}*" -type f 2>/dev/null | grep -q .; then
                echo "Found FFmpeg libraries in ${LIB_DIR}"
                mkdir -p "${PKG_ROOT}/usr/lib"
                find "${LIB_DIR}" -maxdepth 1 -name "libav*.so*" -exec cp -a {} "${PKG_ROOT}/usr/lib/" \; 2>/dev/null || true
                find "${LIB_DIR}" -maxdepth 1 -name "libsw*.so*" -exec cp -a {} "${PKG_ROOT}/usr/lib/" \; 2>/dev/null || true
                echo "✅ FFmpeg libraries copied successfully"
                break 2  # Break both loops
            fi
        done
    fi
done

# Copy FFmpeg plugins/filters if they exist
FFMPEG_PLUGIN_DIRS="/opt/ffmpeg/lib/x86_64-linux-gnu/ffmpeg-plugins /opt/ffmpeg/lib/aarch64-linux-gnu/ffmpeg-plugins /opt/ffmpeg/lib/ffmpeg-plugins /usr/lib/x86_64-linux-gnu/ffmpeg-plugins /usr/lib/aarch64-linux-gnu/ffmpeg-plugins"
for PLUGIN_DIR in ${FFMPEG_PLUGIN_DIRS}; do
    if [ -d "${PLUGIN_DIR}" ]; then
        mkdir -p "${PKG_ROOT}/usr/lib/ffmpeg-plugins"
        echo "Copying FFmpeg plugins from ${PLUGIN_DIR}..."
        cp -ra "${PLUGIN_DIR}"/* "${PKG_ROOT}/usr/lib/ffmpeg-plugins/" 2>/dev/null || true
        break
    fi
done

# =========================
# Copy libturbojpeg dependencies
# =========================
echo "Copying libturbojpeg dependencies..."

TURBOJPEG_LIB_DIRS="/opt/ffmpeg/lib /opt/turbojpeg/lib /usr/lib/x86_64-linux-gnu /usr/lib /usr/lib/aarch64-linux-gnu"
TURBOJPEG_LIBS="libturbojpeg.so"

for LIB_DIR in ${TURBOJPEG_LIB_DIRS}; do
    if [ -d "${LIB_DIR}" ]; then
        for LIB in ${TURBOJPEG_LIBS}; do
            # Find the library with any version suffix (e.g., libturbojpeg.so.0)
            if find "${LIB_DIR}" -maxdepth 1 -name "${LIB}*" -type f 2>/dev/null | grep -q .; then
                echo "Found libturbojpeg libraries in ${LIB_DIR}"
                mkdir -p "${PKG_ROOT}/usr/lib"
                find "${LIB_DIR}" -maxdepth 1 -name "libturbojpeg.so*" -exec cp -a {} "${PKG_ROOT}/usr/lib/" \; 2>/dev/null || true
                echo "✅ libturbojpeg libraries copied successfully"
                break 2  # Break both loops
            fi
        done
    fi
done

# Update the binary's rpath to point to bundled libraries in appdir
if [ -f "${PKG_ROOT}/usr/local/bin/openterfaceQT" ]; then
    echo "Updating rpath for bundled Qt, FFmpeg, and turbojpeg libraries..."
    patchelf --set-rpath '$ORIGIN/../openterface/lib:/usr/lib:/usr/lib/ffmpeg-plugins' "${PKG_ROOT}/usr/local/bin/openterfaceQT"
fi

# Copy desktop file (ensure Exec uses installed path and Icon basename is openterfaceQT)
if [ -f "${SRC}/com.openterface.openterfaceQT.desktop" ]; then
	sed -e 's|^Exec=.*$|Exec=env QT_PLUGIN_PATH=/usr/lib/qt6/plugins:/usr/lib/x86_64-linux-gnu/qt6/plugins QML2_IMPORT_PATH=/usr/lib/qt6/qml:/usr/lib/x86_64-linux-gnu/qt6/qml /usr/local/bin/openterfaceQT|g' \
		-e 's|^Icon=.*$|Icon=openterfaceQT|g' \
		"${SRC}/com.openterface.openterfaceQT.desktop" > "${PKG_ROOT}/usr/share/applications/com.openterface.openterfaceQT.desktop"
fi

# Create launcher script wrapper that sets LD_LIBRARY_PATH and Qt environment for bundled libraries
mkdir -p "${PKG_ROOT}/usr/local/bin"
cat > "${PKG_ROOT}/usr/local/bin/start-openterface.sh" << 'EOF'
#!/bin/bash
# Openterface QT Launcher Wrapper - Sets up bundled library paths and Qt environment

# Get the directory where this script is located
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# Calculate the application directory (one level up, then into openterface)
APPDIR="$(cd "${SCRIPT_DIR}/../openterface" && pwd)"

# Set library path for bundled Qt and other libraries
APPDIR_LIB="${APPDIR}/lib"
if [ -n "${LD_LIBRARY_PATH:-}" ]; then
  export LD_LIBRARY_PATH="${APPDIR_LIB}:/usr/lib:/usr/lib/x86_64-linux-gnu:/usr/lib/aarch64-linux-gnu:/opt/ffmpeg/lib:${LD_LIBRARY_PATH}"
else
  export LD_LIBRARY_PATH="${APPDIR_LIB}:/usr/lib:/usr/lib/x86_64-linux-gnu:/usr/lib/aarch64-linux-gnu:/opt/ffmpeg/lib"
fi

# Set Qt environment variables to use bundled plugins and QML
export QT_PLUGIN_PATH="${APPDIR}/plugins:${APPDIR}/plugins/platforms:/usr/lib/qt6/plugins:/usr/lib/x86_64-linux-gnu/qt6/plugins"
export QML2_IMPORT_PATH="${APPDIR}/qml:/usr/lib/qt6/qml:/usr/lib/x86_64-linux-gnu/qt6/qml"
export QT_QPA_PLATFORM_PLUGIN_PATH="${APPDIR}/plugins/platforms:/usr/lib/qt6/plugins/platforms:/usr/lib/x86_64-linux-gnu/qt6/plugins/platforms"
export QT_QPA_PLATFORM="xcb"
export QT_X11_NO_MITSHM=1

# Execute the actual binary
exec /usr/local/bin/openterfaceQT "$@"
EOF
chmod +x "${PKG_ROOT}/usr/local/bin/start-openterface.sh"

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
Depends: libxkbcommon0, libwayland-client0, libegl1, libgles2, libpulse0, libxcb1, libxcb-shm0, libxcb-xfixes0, libxcb-shape0, libx11-6, zlib1g, libbz2-1.0, liblzma5
Maintainer: TechxArtisan <info@techxartisan.com>
Description: OpenterfaceQT Mini-KVM Linux Edition
EOF
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

# Update the binary's rpath to point to /usr/lib for RPM
if [ -f "${RPMTOP}/SOURCES/openterfaceQT" ]; then
    echo "Updating rpath for RPM bundled Qt libraries..."
    patchelf --set-rpath '/usr/lib' "${RPMTOP}/SOURCES/openterfaceQT"
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
