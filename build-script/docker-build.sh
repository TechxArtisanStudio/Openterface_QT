#!/bin/bash
set -e

echo "Building Openterface QT Application in docker build environment..."

# Copy configuration files
mkdir -p /workspace/build/config/languages
mkdir -p /workspace/build/config/keyboards
cp -r config/keyboards/*.json /workspace/build/config/keyboards/ 2>/dev/null || echo "No keyboard configs"
cp -r config/languages/*.qm /workspace/build/config/languages/ 2>/dev/null || echo "No language files"

# Build with CMake
cd /workspace/build
echo "Configuring with CMake..."

echo "Finding FFmpeg libraries..."
find / -name "libavformat.so*" || echo "No shared libavformat found"
find / -name "libavformat.a*" || echo "No shared libavformat found"
find / -name "avformat.h" || echo "No avformat.h found"

# Debug pkg-config before CMake
pkg-config --version
pkg-config --list-all | grep -E "avformat|avcodec" || echo "No FFmpeg packages found"
echo "========================="

# Default to dynamic linking for the shared build environment; allow override via OPENTERFACE_BUILD_STATIC env
: "${OPENTERFACE_BUILD_STATIC:=OFF}"
echo "OPENTERFACE_BUILD_STATIC set to ${OPENTERFACE_BUILD_STATIC}"
cmake \
	-DCMAKE_BUILD_TYPE=Release \
	-DOPENTERFACE_BUILD_STATIC=${OPENTERFACE_BUILD_STATIC} \
	-DCMAKE_VERBOSE_MAKEFILE=ON \
	/workspace/src

echo "Building with CMake..."
make -j4 VERBOSE=1
echo "Build complete."

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

# Determine version from resources/version.h (APP_VERSION macro)
VERSION_H="${SRC}/resources/version.h"
if [ -f "${VERSION_H}" ]; then
	VERSION=$(grep -Po '^#define APP_VERSION\s+"\K[0-9]+(\.[0-9]+)*' "${VERSION_H}" | head -n1)
fi
if [ -z "${VERSION}" ]; then
	VERSION="0.0.1"
fi

# Determine architecture (map to Debian arch names)
ARCH=$(dpkg --print-architecture 2>/dev/null || true)
if [ -z "${ARCH}" ]; then
	UNAME_M=$(uname -m)
	case "${UNAME_M}" in
		aarch64|arm64) ARCH=arm64;;
		x86_64|amd64) ARCH=amd64;;
		*) ARCH=${UNAME_M};;
	esac
fi

# Copy main binary
if [ -f "${BUILD}/openterfaceQT" ]; then
	install -m 0755 "${BUILD}/openterfaceQT" "${PKG_ROOT}/usr/local/bin/openterfaceQT"
else
	echo "Error: built binary not found at ${BUILD}/openterfaceQT" >&2
	exit 1
fi

# Copy desktop file (ensure Exec uses installed path)
if [ -f "${SRC}/com.openterface.openterfaceQT.desktop" ]; then
	sed 's|^Exec=.*$|Exec=/usr/local/bin/openterfaceQT|g' "${SRC}/com.openterface.openterfaceQT.desktop" > "${PKG_ROOT}/usr/share/applications/com.openterface.openterfaceQT.desktop"
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
	sed \
		-e "s|\\${VERSION}|${VERSION}|g" \
		-e "s|\\${ARCH}|${ARCH}|g" \
		"${CONTROL_TEMPLATE}" > "${CONTROL_FILE}"
else
	# Minimal control if template missing
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
