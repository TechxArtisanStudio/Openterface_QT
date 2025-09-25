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
        else
            echo "⚠ Failed to download runtime, build will download it automatically"
        fi
    elif command -v wget >/dev/null 2>&1; then
        if wget -qO "${DOCKER_RUNTIME_FILE}" "${RUNTIME_URL}"; then
            chmod +x "${DOCKER_RUNTIME_FILE}"
            echo "✓ Runtime downloaded successfully: ${DOCKER_RUNTIME_FILE}"
            ls -lh "${DOCKER_RUNTIME_FILE}"
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

# Compute Debian Depends (default for Ubuntu 22.04 Jammy with Qt 6.2.x) for shared builds
DEPENDS="\
libqt6core6 (>= 6.2), \
libqt6gui6 (>= 6.2), \
libqt6widgets6 (>= 6.2), \
libqt6network6 (>= 6.2), \
libqt6multimedia6 (>= 6.2), \
libqt6multimediawidgets6 (>= 6.2), \
libqt6serialport6 (>= 6.2), \
libqt6svg6 (>= 6.2), \
libqt6xml6 (>= 6.2), \
libqt6dbus6 (>= 6.2), \
libqt6opengl6 (>= 6.2), \
libqt6openglwidgets6 (>= 6.2), \
libqt6concurrent6 (>= 6.2), \
libxkbcommon0, \
libwayland-client0, \
libegl1, \
libgles2, \
libpulse0, \
libxcb1, \
libxcb-shm0, \
libxcb-xfixes0, \
libxcb-shape0, \
libx11-6, \
zlib1g, \
libbz2-1.0, \
liblzma5"

# Allow override from environment (set DEB_DEPENDS to customize)
if [ -n "${DEB_DEPENDS}" ]; then
	DEPENDS="${DEB_DEPENDS}"
fi

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

# Copy desktop file (ensure Exec uses installed path and Icon basename is openterfaceQT)
if [ -f "${SRC}/com.openterface.openterfaceQT.desktop" ]; then
	sed -e 's|^Exec=.*$|Exec=/usr/local/bin/openterfaceQT|g' \
		-e 's|^Icon=.*$|Icon=openterfaceQT|g' \
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
		VERSION="${VERSION}" ARCH="${ARCH}" DEPENDS="${DEPENDS}" envsubst < "${CONTROL_TEMPLATE}" > "${CONTROL_FILE}"
	else
		perl -pe 's/\$\{VERSION\}/'"${VERSION}"'/g; s/\$\{ARCH\}/'"${ARCH}"'/g; s/\$\{DEPENDS\}/'"${DEPENDS}"'/g' "${CONTROL_TEMPLATE}" > "${CONTROL_FILE}"
	fi
else
	cat > "${CONTROL_FILE}" <<EOF
Package: openterfaceQT
Version: ${VERSION}
Section: base
Priority: optional
Architecture: ${ARCH}
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

# Try to copy icon if available
if [ -f "${SRC}/images/icon_256.png" ]; then
	cp "${SRC}/images/icon_256.png" "${RPMTOP}/SOURCES/"
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
